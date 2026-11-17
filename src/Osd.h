#pragma once

#include "Common.h"

// Short-lived, click-through toast at the top of the screen ("Perfil: VHS", "Filtro OFF").
class Osd {
public:
    ~Osd();
    bool Create(HINSTANCE instance);
    void Show(const std::wstring& text);
    HWND Hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Paint();

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    std::wstring text_;
};
