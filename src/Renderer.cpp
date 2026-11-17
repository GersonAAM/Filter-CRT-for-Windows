#include "Common.h"
#include "Renderer.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

namespace {

// d3dcompiler_47.dll ships with Windows 10/11; loading it at runtime keeps the exe free of
// import-library differences between MSVC and MinGW.
using PFN_D3DCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR,
                                        LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
constexpr UINT kCompileEnableStrictness = 1u << 11; // D3DCOMPILE_ENABLE_STRICTNESS
constexpr UINT kCompileOptimization3 = 1u << 15;    // D3DCOMPILE_OPTIMIZATION_LEVEL3

// Mirrors the cbuffer in shaders/crt.hlsl.
struct ShaderConstants {
    float outWidth, outHeight, srcWidth, srcHeight;
    float time, glowMip;
    Params params;
};
static_assert(sizeof(ShaderConstants) % 16 == 0,
              "constant buffer size must be a multiple of 16 bytes; pad Params in Config.h and crt.hlsl");

struct LockGuard {
    explicit LockGuard(SRWLOCK* lock) : lock_(lock) { AcquireSRWLockExclusive(lock_); }
    ~LockGuard() { ReleaseSRWLockExclusive(lock_); }
    SRWLOCK* lock_;
};

PFN_D3DCompile GetD3DCompile() {
    static PFN_D3DCompile compile = []() -> PFN_D3DCompile {
        HMODULE module = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return nullptr;
        return reinterpret_cast<PFN_D3DCompile>(reinterpret_cast<void*>(GetProcAddress(module, "D3DCompile")));
    }();
    return compile;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char buffer[4096];
    size_t n;
    out.clear();
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    fclose(f);
    return true;
}

bool LoadEmbeddedShader(std::string& out) {
    HRSRC info = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_CRT_HLSL), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!info) return false;
    HGLOBAL handle = LoadResource(nullptr, info);
    const void* data = handle ? LockResource(handle) : nullptr;
    if (!data) return false;
    out.assign(static_cast<const char*>(data), SizeofResource(nullptr, info));
    return true;
}

std::wstring BlobText(ID3DBlob* blob) {
    if (!blob) return L"D3DCompile falló sin mensaje";
    return FromUtf8(std::string(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize()));
}

std::wstring HrText(const wchar_t* what, HRESULT hr) {
    wchar_t buffer[160];
    _snwprintf(buffer, 160, L"%ls (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
    buffer[159] = L'\0';
    return buffer;
}

} // namespace

Renderer::~Renderer() {
    Stop();
    if (wakeEvent_) CloseHandle(wakeEvent_);
}

bool Renderer::Initialize(std::wstring* error) {
    QueryPerformanceFrequency(&qpcFrequency_);
    QueryPerformanceCounter(&qpcStart_);
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.PutVoid());
    if (FAILED(hr)) {
        *error = HrText(L"CreateDXGIFactory1 falló", hr);
        return false;
    }

    // Desktop Duplication only works on the adapter that physically drives the output
    // (important on laptops with hybrid graphics).
    HMONITOR primary = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    bool found = false;
    for (UINT a = 0; !found; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, adapter.Put()) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT o = 0; !found; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, output.Put()) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC desc;
            if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop || desc.Monitor != primary) continue;
            if (FAILED(output.As(output_))) continue;
            adapter_ = adapter;
            outputRect_ = desc.DesktopCoordinates;
            found = true;
        }
    }
    if (!found) {
        *error = L"No se encontró la salida DXGI del monitor principal.";
        return false;
    }

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    hr = D3D11CreateDevice(adapter_.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                           device_.Put(), nullptr, context_.Put());
    if (hr == E_INVALIDARG) // runtime without 11.1
        hr = D3D11CreateDevice(adapter_.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels + 1, 1,
                               D3D11_SDK_VERSION, device_.Put(), nullptr, context_.Put());
    if (FAILED(hr)) {
        *error = HrText(L"No se pudo crear el dispositivo Direct3D 11", hr);
        return false;
    }

    // Wait for desktop frames for at most one refresh interval.
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (GetMonitorInfoW(primary, &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmDisplayFrequency > 1)
        frameTimeoutMs_ = std::max(4u, std::min(50u, static_cast<UINT>(1000 / mode.dmDisplayFrequency) + 1));

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device_->CreateSamplerState(&sd, sampler_.Put());
    if (FAILED(hr)) {
        *error = HrText(L"CreateSamplerState falló", hr);
        return false;
    }

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(ShaderConstants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device_->CreateBuffer(&bd, nullptr, constants_.Put());
    if (FAILED(hr)) {
        *error = HrText(L"CreateBuffer (constantes) falló", hr);
        return false;
    }

    return LoadShaders(error);
}

RECT Renderer::OutputRect() {
    LockGuard guard(&lock_);
    return outputRect_;
}

bool Renderer::AttachWindow(HWND overlay, HWND notify, std::wstring* error) {
    overlay_ = overlay;
    notify_ = notify;

    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = adapter_->GetParent(__uuidof(IDXGIFactory2), factory.PutVoid());
    if (FAILED(hr)) {
        *error = HrText(L"DXGI 1.2 no disponible", hr);
        return false;
    }

    RECT r = OutputRect();
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(r.right - r.left);
    desc.Height = static_cast<UINT>(r.bottom - r.top);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swap1;
    hr = factory->CreateSwapChainForHwnd(device_.Get(), overlay, &desc, nullptr, nullptr, swap1.Put());
    if (FAILED(hr)) {
        *error = HrText(L"CreateSwapChainForHwnd falló", hr);
        return false;
    }
    hr = swap1.As(swapChain_);
    if (FAILED(hr)) {
        *error = HrText(L"IDXGISwapChain2 no disponible", hr);
        return false;
    }
    factory->MakeWindowAssociation(overlay, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    // One queued frame at most: the overlay never adds more than one refresh of latency.
    swapChain_->SetMaximumFrameLatency(1);
    frameWaitable_ = swapChain_->GetFrameLatencyWaitableObject();
    swapWidth_ = desc.Width;
    swapHeight_ = desc.Height;

    if (!CreateRenderTarget()) {
        *error = L"No se pudo crear el render target.";
        return false;
    }
    return true;
}

void Renderer::Start() {
    if (thread_) return;
    quit_ = false;
    thread_ = CreateThread(nullptr, 0, &Renderer::ThreadProc, this, 0, nullptr);
}

void Renderer::Stop() {
    if (!thread_) return;
    quit_ = true;
    SetEvent(wakeEvent_);
    // Keep pumping: DXGI may send messages to the overlay window, which lives on this thread.
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &thread_, FALSE, INFINITE, QS_ALLINPUT);
        if (r != WAIT_OBJECT_0 + 1) break;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CloseHandle(thread_);
    thread_ = nullptr;
    if (frameWaitable_) {
        CloseHandle(frameWaitable_);
        frameWaitable_ = nullptr;
    }
}

void Renderer::SetParams(const Params& params) {
    LockGuard guard(&lock_);
    params_ = params;
    paramsDirty_ = true;
}

void Renderer::SetEnabled(bool enabled) {
    if (enabled) {
        notifyShown_ = true;
        invalidate_ = true;
    }
    enabled_ = enabled;
    SetEvent(wakeEvent_);
}

void Renderer::Invalidate() { invalidate_ = true; }

void Renderer::RequestShaderReload() {
    reloadShader_ = true;
    SetEvent(wakeEvent_);
}

DWORD WINAPI Renderer::ThreadProc(void* self) {
    static_cast<Renderer*>(self)->Run();
    return 0;
}

void Renderer::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    while (!quit_) {
        if (!enabled_) {
            // Filter off: free the duplication so capture costs nothing.
            ReleaseDuplication();
            WaitForSingleObject(wakeEvent_, 250);
            continue;
        }
        if (reloadShader_.exchange(false)) {
            std::wstring error;
            if (!LoadShaders(&error)) LogLine(L"Recarga de shader fallida: " + error);
            invalidate_ = true;
        }
        if (!duplication_ && !CreateDuplication()) {
            // Secure desktop (UAC, lock screen) or a mode change in progress: retry shortly.
            WaitForSingleObject(wakeEvent_, 250);
            continue;
        }

        // Blocks until the compositor produces a new desktop frame (or one refresh passes).
        bool newFrame = AcquireFrame(frameTimeoutMs_);
        if (!hasFrame_) continue;

        Params params;
        bool dirty;
        {
            LockGuard guard(&lock_);
            params = params_;
            dirty = paramsDirty_;
            paramsDirty_ = false;
        }

        RECT r = OutputRect();
        UINT width = static_cast<UINT>(r.right - r.left);
        UINT height = static_cast<UINT>(r.bottom - r.top);
        if (width != swapWidth_ || height != swapHeight_) {
            if (!ResizeSwapChain(width, height)) continue;
            dirty = true;
        }

        bool forced = invalidate_.exchange(false);
        // Static desktop and no animated grain: keep the last frame on screen and let the GPU idle.
        if (!newFrame && !dirty && !forced && params.noise <= 0.0f) continue;

        if (frameWaitable_) WaitForSingleObjectEx(frameWaitable_, 100, TRUE);
        RenderFrame(params);
        HRESULT hr = swapChain_->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            LogLine(HrText(L"Dispositivo gráfico perdido", device_->GetDeviceRemovedReason()));
            enabled_ = false;
            PostMessageW(notify_, WM_APP_RENDER_ERROR, 0, 0);
            continue;
        }
        if (notifyShown_.exchange(false)) PostMessageW(notify_, WM_APP_SHOW_OVERLAY, 0, 0);
    }
    ReleaseDuplication();
}

bool Renderer::LoadShaders(std::wstring* error) {
    // An editable copy next to the exe wins over the embedded one.
    std::wstring dir = ExeDir();
    std::wstring data = DataDir();
    const std::wstring candidates[] = {dir + L"\\shaders\\crt.hlsl", dir + L"\\crt.hlsl", data + L"\\shaders\\crt.hlsl",
                                       data + L"\\crt.hlsl"};
    for (const std::wstring& path : candidates) {
        std::string source;
        if (!ReadFileBytes(path, source)) continue;
        std::wstring compileError;
        if (CompileShaders(source, "crt.hlsl", &compileError)) {
            LogLine(L"Shader externo cargado: " + path);
            return true;
        }
        LogLine(L"Error compilando " + path + L", se usa el shader embebido:\n" + compileError);
        break;
    }

    std::string embedded;
    if (!LoadEmbeddedShader(embedded)) {
        *error = L"El shader embebido no está en los recursos del ejecutable.";
        return false;
    }
    if (!CompileShaders(embedded, "crt.hlsl (embebido)", error)) {
        LogLine(L"Error compilando el shader embebido:\n" + *error);
        return false;
    }
    return true;
}

bool Renderer::CompileShaders(const std::string& source, const char* name, std::wstring* error) {
    PFN_D3DCompile compile = GetD3DCompile();
    if (!compile) {
        *error = L"No se pudo cargar d3dcompiler_47.dll.";
        return false;
    }
    const UINT flags = kCompileEnableStrictness | kCompileOptimization3;

    ComPtr<ID3DBlob> vsBlob, psBlob, messages;
    HRESULT hr = compile(source.data(), source.size(), name, nullptr, nullptr, "VSMain", "vs_5_0", flags, 0,
                         vsBlob.Put(), messages.Put());
    if (FAILED(hr)) {
        *error = BlobText(messages.Get());
        return false;
    }
    hr = compile(source.data(), source.size(), name, nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, psBlob.Put(),
                 messages.Put());
    if (FAILED(hr)) {
        *error = BlobText(messages.Get());
        return false;
    }

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    if (FAILED(device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs.Put())) ||
        FAILED(device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps.Put()))) {
        *error = L"CreateVertexShader/CreatePixelShader falló.";
        return false;
    }
    vs_ = vs;
    ps_ = ps;
    return true;
}

bool Renderer::CreateDuplication() {
    DXGI_OUTPUT_DESC desc;
    if (FAILED(output_->GetDesc(&desc))) return false;
    {
        LockGuard guard(&lock_);
        if (!EqualRect(&desc.DesktopCoordinates, &outputRect_)) {
            outputRect_ = desc.DesktopCoordinates;
            PostMessageW(notify_, WM_APP_OUTPUT_CHANGED, 0, 0);
        }
    }

    // DuplicateOutput1 is the variant that per-monitor DPI aware processes are expected to use;
    // asking for BGRA8 also makes DXGI convert HDR desktops for us.
    HRESULT hr = E_NOINTERFACE;
    ComPtr<IDXGIOutput5> output5;
    if (SUCCEEDED(output_.As(output5))) {
        const DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
        hr = output5->DuplicateOutput1(device_.Get(), 0, 1, formats, duplication_.Put());
    }
    if (FAILED(hr)) hr = output_->DuplicateOutput(device_.Get(), duplication_.Put());
    if (FAILED(hr)) {
        duplication_.Reset();
        static HRESULT lastLogged = S_OK;
        if (hr != lastLogged) {
            LogLine(HrText(L"DuplicateOutput falló (se reintentará)", hr));
            lastLogged = hr;
        }
        return false;
    }
    return true;
}

void Renderer::ReleaseDuplication() { duplication_.Reset(); }

bool Renderer::AcquireFrame(UINT timeoutMs) {
    DXGI_OUTDUPL_FRAME_INFO info;
    ComPtr<IDXGIResource> resource;
    HRESULT hr = duplication_->AcquireNextFrame(timeoutMs, &info, resource.Put());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
    if (FAILED(hr)) {
        // DXGI_ERROR_ACCESS_LOST: mode change, UAC prompt, exclusive fullscreen... recreate it.
        ReleaseDuplication();
        Sleep(30);
        return false;
    }

    bool updated = false;
    // LastPresentTime == 0 means only the mouse pointer moved; the image itself is unchanged.
    if (info.LastPresentTime.QuadPart != 0 || !hasFrame_) {
        ComPtr<ID3D11Texture2D> desktop;
        if (SUCCEEDED(resource.As(desktop))) {
            D3D11_TEXTURE2D_DESC desc;
            desktop->GetDesc(&desc);
            if (EnsureCaptureTexture(desc.Width, desc.Height, desc.Format)) {
                context_->CopySubresourceRegion(captureTex_.Get(), 0, 0, 0, 0, desktop.Get(), 0, nullptr);
                context_->GenerateMips(captureSrv_.Get());
                hasFrame_ = true;
                updated = true;
            }
        }
    }
    duplication_->ReleaseFrame();
    return updated;
}

bool Renderer::EnsureCaptureTexture(UINT width, UINT height, DXGI_FORMAT format) {
    if (captureTex_ && width == captureWidth_ && height == captureHeight_ && format == captureFormat_) return true;
    captureSrv_.Reset();
    captureTex_.Reset();
    hasFrame_ = false;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 0; // full chain, used as a cheap wide blur for halation
    td.ArraySize = 1;
    td.Format = format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    HRESULT hr = device_->CreateTexture2D(&td, nullptr, captureTex_.Put());
    if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(captureTex_.Get(), nullptr, captureSrv_.Put());
    if (FAILED(hr)) {
        LogLine(HrText(L"No se pudo crear la textura de captura", hr));
        captureTex_.Reset();
        captureSrv_.Reset();
        return false;
    }
    captureWidth_ = width;
    captureHeight_ = height;
    captureFormat_ = format;
    return true;
}

bool Renderer::CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.PutVoid()))) return false;
    return SUCCEEDED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr, rtv_.Put()));
}

bool Renderer::ResizeSwapChain(UINT width, UINT height) {
    rtv_.Reset();
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->Flush();
    HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN,
                                           DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    if (FAILED(hr) || !CreateRenderTarget()) {
        LogLine(HrText(L"ResizeBuffers falló", hr));
        return false;
    }
    swapWidth_ = width;
    swapHeight_ = height;
    return true;
}

void Renderer::RenderFrame(const Params& params) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double seconds = static_cast<double>(now.QuadPart - qpcStart_.QuadPart) / static_cast<double>(qpcFrequency_.QuadPart);

    ShaderConstants c{};
    c.outWidth = static_cast<float>(swapWidth_);
    c.outHeight = static_cast<float>(swapHeight_);
    c.srcWidth = static_cast<float>(captureWidth_);
    c.srcHeight = static_cast<float>(captureHeight_);
    c.time = static_cast<float>(std::fmod(seconds, 3600.0));
    // Mip whose height is ~135 px: 3 at 1080p, 4 at 2160p.
    c.glowMip = std::max(0.0f, std::log2(static_cast<float>(captureHeight_) / 135.0f));
    c.params = params;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context_->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &c, sizeof(c));
        context_->Unmap(constants_.Get(), 0);
    }

    ID3D11RenderTargetView* rtv = rtv_.Get();
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(swapWidth_), static_cast<float>(swapHeight_), 0.0f, 1.0f};
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11Buffer* cb = constants_.Get();
    context_->PSSetConstantBuffers(0, 1, &cb);
    ID3D11ShaderResourceView* srv = captureSrv_.Get();
    context_->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* sampler = sampler_.Get();
    context_->PSSetSamplers(0, 1, &sampler);
    context_->Draw(3, 0);

    // Unbind so the next desktop copy / mip generation does not hit a read-write hazard.
    ID3D11ShaderResourceView* nullSrv = nullptr;
    context_->PSSetShaderResources(0, 1, &nullSrv);
}
