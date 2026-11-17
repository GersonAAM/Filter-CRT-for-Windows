#include "Common.h"
#include "Osd.h"

namespace {
constexpr wchar_t kOsdClass[] = L"RetroCRT_Osd";
constexpr UINT_PTR kHideTimer = 1;
constexpr UINT kVisibleMs = 1800;
} // namespace

Osd::~Osd() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (font_) DeleteObject(font_);
}

bool Osd::Create(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Osd::WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kOsdClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                            kOsdClass, L"RetroCRT OSD", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    SetLayeredWindowAttributes(hwnd_, 0, 235, LWA_ALPHA);
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);

    int dpi = static_cast<int>(GetDpiForSystem());
    font_ = CreateFontW(-MulDiv(13, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                        L"Segoe UI");
    return true;
}

void Osd::Show(const std::wstring& text) {
    if (!hwnd_) return;
    text_ = text;

    int dpi = static_cast<int>(GetDpiForSystem());
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, font_);
    RECT textRect{0, 0, 0, 0};
    DrawTextW(dc, text_.c_str(), -1, &textRect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);

    int width = textRect.right + 2 * MulDiv(24, dpi, 96);
    int height = textRect.bottom + 2 * MulDiv(12, dpi, 96);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    int x = (mi.rcWork.left + mi.rcWork.right - width) / 2;
    int y = mi.rcWork.top + MulDiv(48, dpi, 96);

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, TRUE);
    SetTimer(hwnd_, kHideTimer, kVisibleMs, nullptr);
}

void Osd::Paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);

    HBRUSH background = CreateSolidBrush(RGB(14, 18, 16));
    FillRect(dc, &rc, background);
    DeleteObject(background);
    HBRUSH border = CreateSolidBrush(RGB(60, 200, 120));
    FrameRect(dc, &rc, border);
    DeleteObject(border);

    HGDIOBJ old = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(130, 255, 170)); // P1 phosphor green
    DrawTextW(dc, text_.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK Osd::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto* self = reinterpret_cast<Osd*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (self) {
            self->hwnd_ = hwnd;
            self->Paint();
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == kHideTimer) {
            KillTimer(hwnd, kHideTimer);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
