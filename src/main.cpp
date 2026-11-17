#include "Common.h"
#include "App.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // The manifest already declares Per-Monitor V2; this is a fallback if it is stripped.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\RetroCRT.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Already running: just bring up its settings panel.
        if (HWND existing = FindWindowW(kMainWindowClass, nullptr)) PostMessageW(existing, WM_APP_SHOW_PANEL, 0, 0);
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    int result;
    {
        App app;
        result = app.Run(instance);
    }
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return result;
}
