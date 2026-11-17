#include "Common.h"
#include "SettingsPanel.h"

#include <commctrl.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr wchar_t kPanelClass[] = L"RetroCRT_Panel";
constexpr int kIdProfileList = 100;
constexpr int kIdSave = 101;
constexpr int kIdReset = 102;
constexpr int kIdClose = 103;
constexpr int kIdSliderBase = 1000;

// Layout in 96-DPI units.
constexpr int kMargin = 14;
constexpr int kLabelWidth = 200;
constexpr int kSliderWidth = 210;
constexpr int kValueWidth = 62;
constexpr int kGap = 6;
constexpr int kRowHeight = 26;
constexpr int kClientWidth = kMargin + kLabelWidth + kGap + kSliderWidth + kGap + kValueWidth + kMargin;

} // namespace

SettingsPanel::~SettingsPanel() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (font_) DeleteObject(font_);
    if (boldFont_) DeleteObject(boldFont_);
}

bool SettingsPanel::Create(HINSTANCE instance, HICON icon, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    dpi_ = GetDpiForSystem();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsPanel::WindowProc;
    wc.hInstance = instance;
    wc.hIcon = icon;
    wc.hIconSm = icon;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512) /*IDC_ARROW*/);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = kPanelClass;
    RegisterClassExW(&wc);

    font_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    boldFont_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    const int listHeight = 84;
    const int paramsTop = 12 + 20 + listHeight + 12;
    const int hintTop = paramsTop + kParamCount * kRowHeight + 6;
    const int buttonsTop = hintTop + 24;
    const int clientHeight = buttonsTop + 28 + kMargin;

    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    RECT rc{0, 0, Scale(kClientWidth), Scale(clientHeight)};
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, dpi_);

    hwnd_ = CreateWindowExW(exStyle, kPanelClass, L"RetroCRT · Ajustes en tiempo real", style, CW_USEDEFAULT,
                            CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);

    HWND title = AddChild(L"STATIC", L"Perfil activo (Ctrl+Alt+V para alternar)", SS_LEFT, kMargin, 12,
                          kClientWidth - 2 * kMargin, 18, 0);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(boldFont_), TRUE);
    // A list box instead of a combo: a combo's drop-down is a separate popup that would be captured.
    profileList_ = AddChild(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                            kMargin, 32, kClientWidth - 2 * kMargin, listHeight, kIdProfileList);

    for (int i = 0; i < kParamCount; ++i) {
        const ParamInfo& info = GetParamInfo(i);
        int y = paramsTop + i * kRowHeight;
        AddChild(L"STATIC", info.label, SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, kMargin, y + 4, kLabelWidth, 18, 0);
        HWND slider = AddChild(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                               kMargin + kLabelWidth + kGap, y, kSliderWidth, 24, kIdSliderBase + i);
        SendMessageW(slider, TBM_SETRANGEMIN, FALSE, 0);
        SendMessageW(slider, TBM_SETRANGEMAX, FALSE, info.steps);
        SendMessageW(slider, TBM_SETPAGESIZE, 0, info.steps > 10 ? info.steps / 20 : 1);
        sliders_.push_back(slider);
        valueLabels_.push_back(AddChild(L"STATIC", L"", SS_RIGHT | SS_NOPREFIX,
                                        kMargin + kLabelWidth + kGap + kSliderWidth + kGap, y + 4, kValueWidth, 18, 0));
    }

    AddChild(L"STATIC", L"Ctrl+Alt+C filtro on/off  ·  Ctrl+Alt+V perfil  ·  Ctrl+Alt+M este panel",
             SS_CENTER | SS_NOPREFIX, kMargin, hintTop, kClientWidth - 2 * kMargin, 18, 0);
    AddChild(L"BUTTON", L"Guardar config.json", WS_TABSTOP | BS_PUSHBUTTON, kMargin, buttonsTop, 170, 28, kIdSave);
    AddChild(L"BUTTON", L"Restaurar perfil", WS_TABSTOP | BS_PUSHBUTTON, kMargin + 178, buttonsTop, 140, 28, kIdReset);
    AddChild(L"BUTTON", L"Cerrar", WS_TABSTOP | BS_PUSHBUTTON, kClientWidth - kMargin - 100, buttonsTop, 100, 28,
             kIdClose);
    return true;
}

HWND SettingsPanel::AddChild(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, Scale(x), Scale(y), Scale(w), Scale(h),
                                 hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE)), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return child;
}

void SettingsPanel::SetProfiles(const std::vector<std::wstring>& names, int active) {
    SendMessageW(profileList_, LB_RESETCONTENT, 0, 0);
    for (const std::wstring& name : names)
        SendMessageW(profileList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SendMessageW(profileList_, LB_SETCURSEL, static_cast<WPARAM>(active), 0);
}

void SettingsPanel::SetParams(const Params& params) {
    params_ = params;
    for (int i = 0; i < kParamCount; ++i) {
        const ParamInfo& info = GetParamInfo(i);
        float t = (GetParam(params_, i) - info.minValue) / (info.maxValue - info.minValue);
        SendMessageW(sliders_[i], TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(t * info.steps)));
        UpdateValueLabel(i);
    }
}

void SettingsPanel::UpdateValueLabel(int index) {
    const ParamInfo& info = GetParamInfo(index);
    float value = GetParam(params_, index);
    char text[32];
    if (strcmp(info.key, "maskType") == 0) {
        static const char* const kNames[] = {"Grille", "Shadow", "Slot"};
        long type = std::lround(value);
        snprintf(text, sizeof(text), "%s", kNames[type < 0 ? 0 : (type > 2 ? 2 : type)]);
    } else if (info.maxValue >= 100.0f) {
        snprintf(text, sizeof(text), "%.0f", static_cast<double>(value));
    } else {
        snprintf(text, sizeof(text), "%.2f", static_cast<double>(value));
    }
    SetWindowTextW(valueLabels_[index], std::wstring(text, text + strlen(text)).c_str());
}

void SettingsPanel::OnSlider(HWND slider) {
    int index = GetDlgCtrlID(slider) - kIdSliderBase;
    if (index < 0 || index >= kParamCount) return;
    const ParamInfo& info = GetParamInfo(index);
    LRESULT pos = SendMessageW(slider, TBM_GETPOS, 0, 0);
    float value = info.minValue + (info.maxValue - info.minValue) * static_cast<float>(pos) / static_cast<float>(info.steps);
    SetParam(params_, index, value);
    UpdateValueLabel(index);
    if (callbacks_.onParamsChanged) callbacks_.onParamsChanged(params_);
}

void SettingsPanel::Show() {
    if (!positioned_) {
        // First time: top-right corner of the primary monitor's work area.
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        int x = mi.rcWork.right - (wr.right - wr.left) - Scale(24);
        int y = mi.rcWork.top + Scale(24);
        SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        positioned_ = true;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
}

void SettingsPanel::Hide() { ShowWindow(hwnd_, SW_HIDE); }

void SettingsPanel::Toggle() {
    if (IsVisible()) Hide();
    else Show();
}

bool SettingsPanel::IsVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }

LRESULT CALLBACK SettingsPanel::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<SettingsPanel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SettingsPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->Handle(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsPanel::Handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_HSCROLL:
        if (lParam) OnSlider(reinterpret_cast<HWND>(lParam));
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kIdProfileList:
            if (HIWORD(wParam) == LBN_SELCHANGE && callbacks_.onProfileSelected) {
                LRESULT sel = SendMessageW(profileList_, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) callbacks_.onProfileSelected(static_cast<int>(sel));
            }
            return 0;
        case kIdSave:
            if (callbacks_.onSave) callbacks_.onSave();
            return 0;
        case kIdReset:
            if (callbacks_.onResetProfile) callbacks_.onResetProfile();
            return 0;
        case kIdClose:
            Hide();
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CLOSE:
        Hide();
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}
