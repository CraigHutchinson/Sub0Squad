#pragma once
/// @file platform_win32_util.hpp
/// Private to the Windows backends of the platform library (docs/PLATFORM.md): UTF-8 and UTF-16 conversion, the system's error text, and a
/// handle that closes itself. Strings cross the platform interface as UTF-8; paths are std::filesystem::path, which is wide on Windows.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>   // before windows.h, which would otherwise bring the old winsock.h
#include <ws2tcpip.h>
#include <windows.h>

#include "sub0squad/platform_error.hpp"
#include "sub0squad/platform_process.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace sub0squad::platform::win32 {

/// UTF-8 to UTF-16 (invalid sequences become U+FFFD).
[[nodiscard]] inline std::wstring widen(std::string_view s)
{
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

/// UTF-16 to UTF-8.
[[nodiscard]] inline std::string narrow(std::wstring_view w)
{
    if (w.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

/// The system's text for an error code, without the trailing line break.
[[nodiscard]] inline std::string errorText(DWORD code)
{
    wchar_t* buf = nullptr;
    const DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                     reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::string text = n ? narrow(std::wstring_view(buf, n)) : "error " + std::to_string(code);
    if (buf) ::LocalFree(buf);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.' || text.back() == ' ')) text.pop_back();
    return text;
}

/// A PlatformError for `operation` from GetLastError() (or `code`).
[[nodiscard]] inline PlatformError lastError(std::string operation, DWORD code = ::GetLastError())
{
    std::string message = "cannot " + operation + ": " + errorText(code);
    return PlatformError{std::move(operation), static_cast<int>(code), std::move(message)};
}

/// A HANDLE closed on destruction (INVALID_HANDLE_VALUE and null both mean none).
class Handle
{
public:
    Handle() = default;
    explicit Handle(HANDLE h) : h_(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : h_(o.release()) {}
    Handle& operator=(Handle&& o) noexcept { if (this != &o) { reset(o.release()); } return *this; }
    ~Handle() { reset(); }

    [[nodiscard]] HANDLE get() const { return h_; }
    [[nodiscard]] bool valid() const { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
    HANDLE release() { HANDLE h = h_; h_ = nullptr; return h; }
    void reset(HANDLE h = nullptr) { if (valid()) ::CloseHandle(h_); h_ = h; }

private:
    HANDLE h_ = nullptr;
};

/// Winsock started once for the process (WSAStartup 2.2); true when it is usable. Never cleaned up: the process's end does that.
[[nodiscard]] inline bool winsockReady()
{
    static const bool ready = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}

/// A PlatformError for `operation` from WSAGetLastError() (or `code`).
[[nodiscard]] inline PlatformError socketError(std::string operation, int code = ::WSAGetLastError())
{
    return lastError(std::move(operation), static_cast<DWORD>(code));
}

/// A new socket that is non-blocking and never inherited by a child; INVALID_SOCKET on failure (WSAGetLastError says why).
[[nodiscard]] inline SOCKET newSocket(int family, int type, int protocol)
{
    if (!winsockReady()) return INVALID_SOCKET;
    const SOCKET s = ::WSASocketW(family, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) return s;
    u_long on = 1;
    ::ioctlsocket(s, FIONBIO, &on);
    return s;
}

/// FILETIME as one 64-bit count of 100 ns intervals.
[[nodiscard]] inline std::uint64_t ticks(const FILETIME& t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }

/// The name of the event a process that watches for stop requests (platform_wait) creates, and terminateProcess sets: Windows has no SIGTERM.
[[nodiscard]] inline std::wstring stopEventName(std::int64_t pid) { return L"Local\\sub0squad-stop-" + std::to_wstring(pid); }

/// How a process ended, from its exit code: a crash status of the system becomes the nearest signal, so callers that classify crashes keep working.
[[nodiscard]] inline ExitStatus exitStatusOf(DWORD code)
{
    if (code < 0xC0000000u) return ExitStatus{static_cast<int>(code), 0};
    switch (code) {
    case 0xC0000005u: return ExitStatus{-1, 11};   // access violation: SIGSEGV
    case 0xC000008Eu: case 0xC0000090u: case 0xC0000094u: return ExitStatus{-1, 8};   // divide by zero, float: SIGFPE
    case 0xC000013Au: return ExitStatus{-1, 2};    // Ctrl+C: SIGINT
    default: return ExitStatus{-1, 6};             // stack overrun, abort and the rest: SIGABRT
    }
}

}  // namespace sub0squad::platform::win32
