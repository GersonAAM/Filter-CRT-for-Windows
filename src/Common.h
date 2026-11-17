#pragma once
// Must be the first include of every .cpp so the Win32 target version is set before any SDK header.

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS // _wfopen / _snwprintf are used deliberately (MSVC + MinGW)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <string>

// Windows 10 2004+; older MinGW headers may not define it.
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// Messages shared between the UI thread, the render thread and the settings panel.
enum : UINT {
    WM_APP_TRAY = WM_APP + 1,
    WM_APP_SHOW_OVERLAY,   // render thread -> UI: first frame of an enabled session is on screen
    WM_APP_OUTPUT_CHANGED, // render thread -> UI: monitor resolution/position changed
    WM_APP_RENDER_ERROR,   // render thread -> UI: GPU device lost
    WM_APP_SHOW_PANEL,     // second instance -> first instance
};

constexpr const wchar_t* kMainWindowClass = L"RetroCRT_Main";

// Minimal COM smart pointer (avoids depending on <wrl/client.h>, which some MinGW builds lack).
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~ComPtr() { Reset(); }

    ComPtr& operator=(const ComPtr& o) {
        if (this != &o) {
            if (o.p_) o.p_->AddRef();
            Reset();
            p_ = o.p_;
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) {
            Reset();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }
    ComPtr& operator=(std::nullptr_t) { Reset(); return *this; }

    T* Get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    // Releases the current pointer and returns the address for an out-parameter.
    T** Put() { Reset(); return &p_; }
    void** PutVoid() { return reinterpret_cast<void**>(Put()); }

    void Reset() {
        if (p_) {
            T* t = p_;
            p_ = nullptr;
            t->Release();
        }
    }

    template <typename U>
    HRESULT As(ComPtr<U>& out) const {
        if (!p_) return E_POINTER;
        return p_->QueryInterface(__uuidof(U), out.PutVoid());
    }

private:
    T* p_ = nullptr;
};

std::wstring ExeDir();
// Where config.json and retrocrt.log live: next to the exe when it is writable (portable),
// otherwise %APPDATA%\RetroCRT (installed under Program Files).
std::wstring DataDir();
// True when the process runs with UIAccess (signed + installed build): its topmost windows live
// in a z-order band above the Start menu and the quick settings flyout.
bool HasUiAccess();
std::string ToUtf8(const std::wstring& text);
std::wstring FromUtf8(const std::string& text);
void LogLine(const std::wstring& message); // appends to retrocrt.log next to the exe
