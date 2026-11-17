#pragma once

#include "Common.h"
#include "Config.h"

#include <d3d11.h>
#include <dxgi1_5.h>
#include <atomic>

// Captures the primary monitor with DXGI Desktop Duplication and draws it through the CRT
// shader into a flip-model swap chain on the overlay window. Runs on its own thread so
// dragging windows or opening menus on the UI thread never stalls the picture.
class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Creates the D3D11 device on the adapter that drives the primary monitor and compiles shaders.
    bool Initialize(std::wstring* error);
    // Desktop rectangle of the primary monitor, in physical pixels.
    RECT OutputRect();
    // Creates the swap chain. `notify` receives the WM_APP_* messages.
    bool AttachWindow(HWND overlay, HWND notify, std::wstring* error);

    void Start();
    void Stop(); // pumps messages while it waits, so the swap chain can talk to the UI thread

    void SetParams(const Params& params);
    void SetEnabled(bool enabled);
    void Invalidate();
    void RequestShaderReload();

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Run();

    bool LoadShaders(std::wstring* error);
    bool CompileShaders(const std::string& source, const char* name, std::wstring* error);
    bool CreateDuplication();
    void ReleaseDuplication();
    bool AcquireFrame(UINT timeoutMs);
    bool EnsureCaptureTexture(UINT width, UINT height, DXGI_FORMAT format);
    bool CreateRenderTarget();
    bool ResizeSwapChain(UINT width, UINT height);
    void RenderFrame(const Params& params);

    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<IDXGIOutput1> output_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain2> swapChain_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> captureTex_; // copy of the desktop with a full mip chain (for glow)
    ComPtr<ID3D11ShaderResourceView> captureSrv_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Buffer> constants_;

    HANDLE frameWaitable_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE wakeEvent_ = nullptr;
    HWND overlay_ = nullptr;
    HWND notify_ = nullptr;

    SRWLOCK lock_ = SRWLOCK_INIT; // guards params_, paramsDirty_, outputRect_
    Params params_{};
    bool paramsDirty_ = true;
    RECT outputRect_{};

    std::atomic<bool> enabled_{false};
    std::atomic<bool> quit_{false};
    std::atomic<bool> reloadShader_{false};
    std::atomic<bool> invalidate_{true};
    std::atomic<bool> notifyShown_{false};

    // Render-thread state.
    UINT swapWidth_ = 0, swapHeight_ = 0;
    UINT captureWidth_ = 0, captureHeight_ = 0;
    DXGI_FORMAT captureFormat_ = DXGI_FORMAT_UNKNOWN;
    bool hasFrame_ = false;
    UINT frameTimeoutMs_ = 16;
    LARGE_INTEGER qpcFrequency_{};
    LARGE_INTEGER qpcStart_{};
};
