#pragma once

#include "Common.h"
#include "Config.h"

#include <functional>
#include <vector>

// Compact native Win32 panel (Ctrl+Alt+M) with one slider per CRT parameter.
// It is excluded from capture, so the filter never shows a ghost copy of it.
class SettingsPanel {
public:
    struct Callbacks {
        std::function<void(const Params&)> onParamsChanged;
        std::function<void(int)> onProfileSelected;
        std::function<void()> onSave;
        std::function<void()> onResetProfile;
    };

    ~SettingsPanel();
    bool Create(HINSTANCE instance, HICON icon, Callbacks callbacks);
    void SetProfiles(const std::vector<std::wstring>& names, int active);
    void SetParams(const Params& params);

    void Show();
    void Hide();
    void Toggle();
    bool IsVisible() const;
    HWND Hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(UINT msg, WPARAM wParam, LPARAM lParam);
    HWND AddChild(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id);
    void OnSlider(HWND slider);
    void UpdateValueLabel(int index);
    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    HWND hwnd_ = nullptr;
    HWND profileList_ = nullptr;
    std::vector<HWND> sliders_;
    std::vector<HWND> valueLabels_;
    HFONT font_ = nullptr;
    HFONT boldFont_ = nullptr;
    UINT dpi_ = 96;
    bool positioned_ = false;
    Params params_{};
    Callbacks callbacks_;
};
