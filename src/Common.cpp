#include "Common.h"

#include <cstdio>

std::wstring ExeDir() {
    wchar_t buffer[MAX_PATH * 4];
    DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
    std::wstring path(buffer, length);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

std::wstring DataDir() {
    static const std::wstring dir = []() -> std::wstring {
        std::wstring exeDir = ExeDir();
        std::wstring probe = exeDir + L"\\.retrocrt_write_test";
        HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return exeDir;
        }
        wchar_t appData[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return exeDir;
        std::wstring fallback = std::wstring(appData) + L"\\RetroCRT";
        CreateDirectoryW(fallback.c_str(), nullptr);
        return fallback;
    }();
    return dir;
}

bool HasUiAccess() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD uiAccess = 0;
    DWORD size = 0;
    BOOL ok = GetTokenInformation(token, TokenUIAccess, &uiAccess, sizeof(uiAccess), &size);
    CloseHandle(token);
    return ok && uiAccess != 0;
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size);
    return out;
}

void LogLine(const std::wstring& message) {
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);

    std::wstring path = DataDir() + L"\\retrocrt.log";
    if (FILE* f = _wfopen(path.c_str(), L"ab")) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        char head[64];
        snprintf(head, sizeof(head), "[%04u-%02u-%02u %02u:%02u:%02u] ", t.wYear, t.wMonth, t.wDay, t.wHour,
                 t.wMinute, t.wSecond);
        std::string line = head + ToUtf8(message) + "\r\n";
        fwrite(line.data(), 1, line.size(), f);
        fclose(f);
    }
    OutputDebugStringW((L"RetroCRT: " + message + L"\n").c_str());

    ReleaseSRWLockExclusive(&lock);
}
