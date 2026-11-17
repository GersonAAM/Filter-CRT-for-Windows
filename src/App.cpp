#include "Common.h"
#include "App.h"

#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace {

constexpr wchar_t kOverlayClass[] = L"RetroCRT_Overlay";
constexpr UINT kTrayId = 1;
constexpr UINT_PTR kTopmostTimer = 1;

enum HotkeyId { kHotkeyToggle = 1, kHotkeyProfile, kHotkeyPanel };

enum MenuId : UINT {
    kMenuToggle = 100,
    kMenuPanel,
    kMenuSave,
    kMenuReloadShader,
    kMenuOpenFolder,
    kMenuExit,
    kMenuProfileBase = 200,
};

App* g_app = nullptr; // for the WinEvent callback, which has no user pointer

// Shell surfaces that Windows draws in z-order bands above every normal topmost window.
bool IsShellUi(HWND hwnd) {
    if (!hwnd) return false;

    wchar_t cls[128] = {};
    GetClassNameW(hwnd, cls, 128);
    static const wchar_t* const kClasses[] = {
        L"XamlExplorerHostIslandWindow",        // Alt+Tab, Win+Tab (task view)
        L"TopLevelWindowForOverflowXamlIsland", // Windows 11 tray overflow (^)
        L"NotifyIconOverflowWindow",            // Windows 10 tray overflow
        L"ForegroundStaging",                   // transient window while Start opens
    };
    for (const wchar_t* c : kClasses)
        if (_wcsicmp(cls, c) == 0) return true;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[MAX_PATH * 2];
    DWORD size = MAX_PATH * 2;
    BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;

    static const wchar_t* const kHosts[] = {
        L"StartMenuExperienceHost.exe", // Start
        L"SearchHost.exe",              // Search (Windows 11)
        L"SearchApp.exe",               // Search (Windows 10)
        L"ShellExperienceHost.exe",     // notifications, calendar, quick settings (older builds)
        L"ShellHost.exe",               // quick settings Wi-Fi/Bluetooth/volume (Windows 11 24H2+)
    };
    for (const wchar_t* h : kHosts)
        if (_wcsicmp(name, h) == 0) return true;
    return false;
}

float RoundRectDistance(float x, float y, float halfW, float halfH, float radius) {
    float qx = std::fabs(x) - halfW + radius;
    float qy = std::fabs(y) - halfH + radius;
    float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
    return outside + std::min(std::max(qx, qy), 0.0f) - radius;
}

// Draws a tiny CRT (grey cabinet, green phosphor screen with scanlines) so the exe needs no .ico.
HICON CreateCrtIcon(int size) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return nullptr;

    auto* pixels = static_cast<uint32_t*>(bits);
    const float pixel = 2.0f / static_cast<float>(size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float fx = (static_cast<float>(x) + 0.5f) * pixel - 1.0f;
            float fy = (static_cast<float>(y) + 0.5f) * pixel - 1.0f;
            float body = RoundRectDistance(fx, fy, 0.96f, 0.84f, 0.30f);
            float screen = RoundRectDistance(fx, fy, 0.74f, 0.62f, 0.24f);
            float alpha = std::min(std::max(0.5f - body / pixel, 0.0f), 1.0f);
            float inScreen = std::min(std::max(0.5f - screen / pixel, 0.0f), 1.0f);

            float glow = 1.0f - 0.35f * (fx * fx + fy * fy);
            float scan = (y % 2 == 0) ? 1.0f : 0.55f;
            float r = 40.0f + (25.0f - 40.0f) * inScreen;
            float g = 44.0f + (230.0f * glow * scan - 44.0f) * inScreen;
            float b = 52.0f + (120.0f * glow * scan - 52.0f) * inScreen;

            auto channel = [alpha](float v) { return static_cast<uint32_t>(std::min(std::max(v, 0.0f), 255.0f) * alpha); };
            pixels[y * size + x] = (static_cast<uint32_t>(alpha * 255.0f) << 24) | (channel(r) << 16) |
                                   (channel(g) << 8) | channel(b);
        }
    }

    std::vector<BYTE> maskBits(static_cast<size_t>(((size + 15) / 16) * 2 * size), 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

} // namespace

App::~App() {
    if (foregroundHook_) UnhookWinEvent(foregroundHook_);
    if (icon_) DestroyIcon(icon_);
    g_app = nullptr;
}

int App::Run(HINSTANCE instance) {
    instance_ = instance;
    g_app = this;
    uiAccess_ = HasUiAccess();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    // Portable: config.json lives next to the exe. Write the defaults on first run so it can be edited.
    configPath_ = DataDir() + L"\\config.json";
    config_ = Config::Defaults();
    std::wstring loadError;
    if (!config_.Load(configPath_, &loadError)) {
        if (GetFileAttributesW(configPath_.c_str()) == INVALID_FILE_ATTRIBUTES) {
            config_.Save(configPath_);
        } else {
            LogLine(L"config.json ignorado: " + loadError);
            MessageBoxW(nullptr, L"config.json tiene un error de formato; se usan los perfiles por defecto.\n"
                                 L"Guardar desde RetroCRT lo sobrescribirá.",
                        L"RetroCRT", MB_ICONWARNING);
        }
    }

    std::wstring error;
    if (!renderer_.Initialize(&error) || !CreateWindows(&error) || !renderer_.AttachWindow(overlay_, main_, &error)) {
        LogLine(L"Error de inicio: " + error);
        MessageBoxW(nullptr, error.c_str(), L"RetroCRT - error de inicio", MB_ICONERROR);
        return 1;
    }

    SettingsPanel::Callbacks callbacks;
    callbacks.onParamsChanged = [this](const Params& p) { OnPanelParams(p); };
    callbacks.onProfileSelected = [this](int index) { SelectProfile(index, false); };
    callbacks.onSave = [this]() { SaveConfig(); };
    callbacks.onResetProfile = [this]() { ResetActiveProfile(); };
    panel_.Create(instance_, icon_, callbacks);
    panel_.SetProfiles(ProfileNames(), config_.activeProfile);
    panel_.SetParams(config_.profiles[config_.activeProfile].params);
    osd_.Create(instance_);

    renderer_.SetParams(config_.profiles[config_.activeProfile].params);
    renderer_.Start();
    AddTrayIcon();
    RegisterHotkeys();
    // Without UIAccess, Start and the system flyouts are always drawn above the overlay while
    // the overlay also shows a filtered (offset) copy of them. Hide the overlay while they are open.
    if (!uiAccess_)
        foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &App::ForegroundHook,
                                          0, 0, WINEVENT_OUTOFCONTEXT);
    LogLine(uiAccess_ ? L"Modo UIAccess: el filtro cubre también Inicio y los paneles del sistema."
                      : L"Modo portátil: el filtro se oculta mientras Inicio o los paneles del sistema están abiertos.");

    SetEnabled(config_.enabled, false);
    SetTimer(main_, kTopmostTimer, 2000, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (panel_.IsVisible() && IsDialogMessageW(panel_.Hwnd(), &msg)) continue; // Tab between controls
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    renderer_.Stop();
    if (overlay_) DestroyWindow(overlay_);
    return static_cast<int>(msg.wParam);
}

bool App::CreateWindows(std::wstring* error) {
    UINT dpi = GetDpiForSystem();
    int iconSize = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    icon_ = CreateCrtIcon(iconSize > 0 ? iconSize : 16);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::MainProc;
    wc.hInstance = instance_;
    wc.hIcon = icon_;
    wc.lpszClassName = kMainWindowClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW oc{};
    oc.cbSize = sizeof(oc);
    oc.lpfnWndProc = &App::OverlayProc;
    oc.hInstance = instance_;
    oc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    oc.lpszClassName = kOverlayClass;
    RegisterClassExW(&oc);

    // Hidden top-level window: receives hotkeys, tray callbacks and render-thread notifications.
    main_ = CreateWindowExW(0, kMainWindowClass, L"RetroCRT", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!main_) {
        *error = L"No se pudo crear la ventana principal.";
        return false;
    }
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    // Full-screen, always-on-top, click-through overlay. Coordinates are physical pixels
    // because the process is Per-Monitor-V2 DPI aware (manifest).
    RECT r = renderer_.OutputRect();
    overlay_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               kOverlayClass, L"RetroCRT Overlay", WS_POPUP, r.left, r.top, r.right - r.left,
                               r.bottom - r.top, nullptr, nullptr, instance_, this);
    if (!overlay_) {
        *error = L"No se pudo crear la ventana overlay.";
        return false;
    }
    // 254 instead of 255: a fully opaque layered window makes some apps (e.g. Chromium-based
    // browsers) believe they are occluded and stop painting video. 1/255 of bleed is invisible.
    SetLayeredWindowAttributes(overlay_, 0, 254, LWA_ALPHA);

    // Without this the duplication API would capture our own output: infinite feedback loop.
    if (!SetWindowDisplayAffinity(overlay_, WDA_EXCLUDEFROMCAPTURE)) {
        *error = L"SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) falló.\n"
                 L"RetroCRT requiere Windows 10 versión 2004 o superior (Windows 11 recomendado).";
        return false;
    }
    return true;
}

void App::AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = main_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = icon_;
    lstrcpynW(nid.szTip, L"RetroCRT", ARRAYSIZE(nid.szTip));
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    UpdateTrayTip();
}

void App::RemoveTrayIcon() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = main_;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayAdded_ = false;
}

void App::UpdateTrayTip() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = main_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_TIP;
    std::wstring tip = std::wstring(L"RetroCRT · ") + (enabled_ ? L"ON" : L"OFF") + L"\n" +
                       config_.profiles[config_.activeProfile].name;
    lstrcpynW(nid.szTip, tip.c_str(), ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    HMENU profiles = CreatePopupMenu();
    const int count = static_cast<int>(config_.profiles.size());
    for (int i = 0; i < count; ++i)
        AppendMenuW(profiles, MF_STRING, kMenuProfileBase + i, config_.profiles[i].name.c_str());
    CheckMenuRadioItem(profiles, kMenuProfileBase, kMenuProfileBase + count - 1, kMenuProfileBase + config_.activeProfile,
                       MF_BYCOMMAND);

    AppendMenuW(menu, MF_STRING | (enabled_ ? MF_CHECKED : MF_UNCHECKED), kMenuToggle, L"Filtro CRT activo\tCtrl+Alt+C");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles), L"Perfil\tCtrl+Alt+V");
    AppendMenuW(menu, MF_STRING | (panel_.IsVisible() ? MF_CHECKED : MF_UNCHECKED), kMenuPanel,
                L"Ajustes en tiempo real...\tCtrl+Alt+M");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSave, L"Guardar configuración (config.json)");
    AppendMenuW(menu, MF_STRING, kMenuReloadShader, L"Recargar shader");
    AppendMenuW(menu, MF_STRING, kMenuOpenFolder, L"Abrir carpeta de configuración");
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0,
                uiAccess_ ? L"Modo instalado: Inicio y paneles con filtro"
                          : L"Modo portátil: Inicio y paneles sin filtro");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Salir");
    SetMenuDefaultItem(menu, kMenuPanel, FALSE);

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(main_); // required so the menu closes when clicking elsewhere
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, main_, nullptr);
    PostMessageW(main_, WM_NULL, 0, 0);
    DestroyMenu(menu); // also destroys the submenu
}

void App::RegisterHotkeys() {
    struct Hotkey { int id; UINT key; const wchar_t* name; };
    const Hotkey hotkeys[] = {
        {kHotkeyToggle, 'C', L"Ctrl+Alt+C"},
        {kHotkeyProfile, 'V', L"Ctrl+Alt+V"},
        {kHotkeyPanel, 'M', L"Ctrl+Alt+M"},
    };
    std::wstring failed;
    for (const Hotkey& h : hotkeys) {
        if (!RegisterHotKey(main_, h.id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, h.key))
            failed += std::wstring(L"  • ") + h.name + L"\n";
    }
    if (!failed.empty()) {
        LogLine(L"Atajos no registrados (en uso por otra aplicación):\n" + failed);
        MessageBoxW(nullptr,
                    (L"Estos atajos ya los usa otra aplicación y no estarán disponibles:\n\n" + failed +
                     L"\nPuedes seguir usando el icono de la bandeja del sistema.")
                        .c_str(),
                    L"RetroCRT", MB_ICONWARNING);
    }
}

void App::RaiseOverlay() {
    // Other topmost windows (taskbar, other overlays) can end up above us; push back to the top,
    // then keep our own panel and OSD above the overlay.
    SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (panel_.IsVisible())
        SetWindowPos(panel_.Hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (osd_.Hwnd() && IsWindowVisible(osd_.Hwnd()))
        SetWindowPos(osd_.Hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void App::ShowOverlay() {
    ShowWindow(overlay_, SW_SHOWNOACTIVATE);
    RaiseOverlay();
    renderer_.Invalidate(); // re-present now that the window is visible
}

void CALLBACK App::ForegroundHook(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (g_app && idObject == OBJID_WINDOW) g_app->OnForegroundChanged(hwnd);
}

void App::OnForegroundChanged(HWND hwnd) {
    bool shell = IsShellUi(hwnd);
    if (shell == shellUiOpen_) return;
    shellUiOpen_ = shell;
    if (!enabled_) return;
    if (shell) ShowWindow(overlay_, SW_HIDE);
    else ShowOverlay();
}

void App::SetEnabled(bool enabled, bool showOsd) {
    enabled_ = enabled;
    config_.enabled = enabled;
    renderer_.SetEnabled(enabled);
    // When enabling, the overlay is shown once the first filtered frame is ready (WM_APP_SHOW_OVERLAY),
    // so there is never a flash of an old frame.
    if (!enabled) ShowWindow(overlay_, SW_HIDE);
    UpdateTrayTip();
    if (showOsd)
        osd_.Show(enabled ? L"Filtro CRT: ON  ·  " + config_.profiles[config_.activeProfile].name
                          : std::wstring(L"Filtro CRT: OFF"));
}

void App::SelectProfile(int index, bool showOsd) {
    if (index < 0 || index >= static_cast<int>(config_.profiles.size())) return;
    config_.activeProfile = index;
    const Params& params = config_.profiles[index].params;
    renderer_.SetParams(params);
    panel_.SetProfiles(ProfileNames(), index);
    panel_.SetParams(params);
    UpdateTrayTip();
    if (showOsd) osd_.Show(L"Perfil: " + config_.profiles[index].name);
}

void App::OnPanelParams(const Params& params) {
    config_.profiles[config_.activeProfile].params = params;
    renderer_.SetParams(params);
}

void App::ResetActiveProfile() {
    int index = config_.activeProfile;
    Profile defaults = DefaultProfile(std::min(index, DefaultProfileCount() - 1));
    config_.profiles[index].params = defaults.params;
    SelectProfile(index, false);
}

void App::SaveConfig() {
    if (config_.Save(configPath_)) {
        osd_.Show(L"Configuración guardada en config.json");
    } else {
        MessageBoxW(nullptr, (L"No se pudo escribir:\n" + configPath_).c_str(), L"RetroCRT", MB_ICONERROR);
    }
}

std::vector<std::wstring> App::ProfileNames() const {
    std::vector<std::wstring> names;
    for (const Profile& p : config_.profiles) names.push_back(p.name);
    return names;
}

LRESULT CALLBACK App::MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->main_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMain(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK App::OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT; // belt and braces on top of WS_EX_TRANSPARENT
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::HandleMain(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_ != 0) {
        // Explorer restarted: the tray icon is gone, add it again.
        trayAdded_ = false;
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_HOTKEY:
        switch (wParam) {
        case kHotkeyToggle:
            SetEnabled(!enabled_, true);
            break;
        case kHotkeyProfile:
            SelectProfile((config_.activeProfile + 1) % static_cast<int>(config_.profiles.size()), true);
            break;
        case kHotkeyPanel:
            panel_.Toggle();
            break;
        }
        return 0;

    case WM_APP_TRAY:
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            break;
        case WM_LBUTTONDBLCLK:
            panel_.Toggle();
            break;
        }
        return 0;

    case WM_COMMAND: {
        UINT id = LOWORD(wParam);
        if (id >= kMenuProfileBase && id < kMenuProfileBase + config_.profiles.size()) {
            SelectProfile(static_cast<int>(id - kMenuProfileBase), true);
            return 0;
        }
        switch (id) {
        case kMenuToggle:
            SetEnabled(!enabled_, true);
            break;
        case kMenuPanel:
            panel_.Toggle();
            break;
        case kMenuSave:
            SaveConfig();
            break;
        case kMenuReloadShader:
            renderer_.RequestShaderReload();
            osd_.Show(L"Shader recargado (errores en retrocrt.log)");
            break;
        case kMenuOpenFolder:
            ShellExecuteW(nullptr, L"open", DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kMenuExit:
            DestroyWindow(main_);
            break;
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == kTopmostTimer && enabled_ && IsWindowVisible(overlay_)) RaiseOverlay();
        return 0;

    case WM_APP_SHOW_OVERLAY:
        if (enabled_ && !shellUiOpen_) ShowOverlay();
        return 0;

    case WM_APP_OUTPUT_CHANGED: {
        RECT r = renderer_.OutputRect();
        SetWindowPos(overlay_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        renderer_.Invalidate();
        return 0;
    }

    case WM_APP_RENDER_ERROR:
        SetEnabled(false, false);
        MessageBoxW(nullptr, L"Se perdió el dispositivo gráfico (driver reiniciado o GPU cambiada).\n"
                             L"Vuelve a abrir RetroCRT.",
                    L"RetroCRT", MB_ICONERROR);
        DestroyWindow(main_);
        return 0;

    case WM_APP_SHOW_PANEL:
        panel_.Show();
        return 0;

    case WM_DESTROY:
        KillTimer(main_, kTopmostTimer);
        UnregisterHotKey(main_, kHotkeyToggle);
        UnregisterHotKey(main_, kHotkeyProfile);
        UnregisterHotKey(main_, kHotkeyPanel);
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(main_, msg, wParam, lParam);
}
