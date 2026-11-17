#pragma once

#include "Common.h"
#include "Config.h"
#include "Osd.h"
#include "Renderer.h"
#include "SettingsPanel.h"

#include <shellapi.h>

// UI thread: overlay window, tray icon, global hotkeys, settings panel and configuration.
class App {
public:
    ~App();
    int Run(HINSTANCE instance);

private:
    static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void CALLBACK ForegroundHook(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                        DWORD thread, DWORD time);
    LRESULT HandleMain(UINT msg, WPARAM wParam, LPARAM lParam);
    void OnForegroundChanged(HWND hwnd);
    void ShowOverlay();

    bool CreateWindows(std::wstring* error);
    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayTip();
    void ShowTrayMenu();
    void RegisterHotkeys();
    void RaiseOverlay();

    void SetEnabled(bool enabled, bool showOsd);
    void SelectProfile(int index, bool showOsd);
    void OnPanelParams(const Params& params);
    void ResetActiveProfile();
    void SaveConfig();
    std::vector<std::wstring> ProfileNames() const;

    HINSTANCE instance_ = nullptr;
    HWND main_ = nullptr;
    HWND overlay_ = nullptr;
    HICON icon_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    bool trayAdded_ = false;
    bool enabled_ = false;
    bool uiAccess_ = false;       // installed build: overlay sits above the Start menu
    bool shellUiOpen_ = false;    // portable build: Start/flyout in front, overlay hidden meanwhile
    HWINEVENTHOOK foregroundHook_ = nullptr;

    Config config_;
    std::wstring configPath_;
    Renderer renderer_;
    SettingsPanel panel_;
    Osd osd_;
};
