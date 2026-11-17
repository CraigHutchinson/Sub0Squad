// Windows backend of platform_ipc.hpp: AF_UNIX stream sockets (Windows 10 1803 and later), named by the path in UTF-8. sockaddr_un holds 108
// bytes and Windows has no /proc/self/fd way around that, so a longer path is moved to a short stable name (localSocketFile). Windows records no
// peer credentials on these sockets: who may connect is decided by the directory's ACL (owner-only: the state directory, or the user's own
// profile for a moved name), and peerIsSameUser answers true.
#include "sub0squad/platform_ipc.hpp"

#include "platform_win32_util.hpp"

#include <afunix.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <vector>

namespace sub0squad::platform {

namespace {

constexpr std::size_t kMaxName = sizeof(sockaddr_un{}.sun_path) - 1;   // 107 bytes and the terminating nul

/// FNV-1a over the path's UTF-16 units, lowercased: the file system ignores case, so D:\x and d:\X name one socket.
std::uint64_t hashOf(const std::wstring& path)
{
    std::uint64_t h = 14695981039346656037ULL;
    for (wchar_t c : path) {
        h ^= static_cast<std::uint64_t>(::towlower(c));
        h *= 1099511628211ULL;
    }
    return h;
}

/// The directory for moved socket names: %USERPROFILE%\.sub0squad\sockets (owner-only, and the same path in every shell; AppData is not used, as a
/// packaged app sees a virtualized copy of it where AF_UNIX fails with WSAEINVAL), else the user's temp directory.
std::filesystem::path fallbackDir()
{
    wchar_t buf[MAX_PATH + 1];
    const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH + 1);
    if (n > 0 && n <= MAX_PATH) return std::filesystem::path(std::wstring(buf, n)) / L".sub0squad" / L"sockets";
    const DWORD t = ::GetTempPathW(MAX_PATH + 1, buf);
    return std::filesystem::path(std::wstring(buf, t)) / L"sub0squad-sockets";
}

/// The address of a socket file whose name localSocketFile has already checked fits.
sockaddr_un addressOf(const std::filesystem::path& file)
{
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    const std::string name = win32::narrow(file.wstring());
    std::memcpy(a.sun_path, name.c_str(), name.size() + 1);
    return a;
}

}  // namespace

std::expected<std::filesystem::path, PlatformError> localSocketFile(const std::filesystem::path& path)
{
    if (win32::narrow(path.wstring()).size() <= kMaxName) return path;
    std::error_code ec;
    auto full = std::filesystem::absolute(path, ec);
    if (ec) full = path;
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(hashOf(full.lexically_normal().wstring())));
    const auto dir = fallbackDir();
    const auto file = dir / (std::string(hex) + ".sock");
    const std::string name = win32::narrow(file.wstring());
    if (name.size() > kMaxName)
        return std::unexpected(PlatformError{"name a socket", WSAENAMETOOLONG,
                                             "cannot name the socket " + path.string() + ": Windows allows at most " + std::to_string(kMaxName) +
                                                 " bytes, and its short name " + name + " is too long as well"});
    std::filesystem::create_directories(dir, ec);   // an existing directory is fine; a real failure shows at bind
    return file;
}

void removeLocal(const std::filesystem::path& path)
{
    if (const auto file = localSocketFile(path)) ::DeleteFileW(file->wstring().c_str());   // by name: a socket file cannot be opened
}

int clearStaleLocalSockets(std::chrono::seconds minAge)
{
    const std::wstring dir = fallbackDir().wstring();
    WIN32_FIND_DATAW found{};
    const HANDLE h = ::FindFirstFileW((dir + L"\\*.sock").c_str(), &found);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FILETIME nowFt{};
    ::GetSystemTimeAsFileTime(&nowFt);
    const std::uint64_t now = win32::ticks(nowFt), youngest = static_cast<std::uint64_t>(minAge.count()) * 10'000'000ULL;
    std::vector<std::filesystem::path> stale;
    do {
        if (now - win32::ticks(found.ftLastWriteTime) < youngest) continue;   // its daemon may be between bind and listen
        const std::filesystem::path file = std::filesystem::path(dir) / found.cFileName;
        auto c = connectLocal(file);   // a moved name is short: it names itself
        if (!c && c.error().noServer) stale.push_back(file);
    } while (::FindNextFileW(h, &found));
    ::FindClose(h);
    int removed = 0;
    for (const auto& f : stale) removed += ::DeleteFileW(f.wstring().c_str()) ? 1 : 0;
    return removed;
}

std::expected<SocketListener, PlatformError> listenLocal(const std::filesystem::path& path)
{
    const auto file = localSocketFile(path);
    if (!file) return std::unexpected(file.error());
    const sockaddr_un a = addressOf(*file);
    ::DeleteFileW(file->wstring().c_str());   // a stale socket from a daemon that died; std::filesystem::remove would open it (see connectLocal)
    const SOCKET s = win32::newSocket(AF_UNIX, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return std::unexpected(win32::socketError("create a local socket"));
    if (::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0 || ::listen(s, 64) != 0) {
        const int err = ::WSAGetLastError();
        ::closesocket(s);
        return std::unexpected(win32::socketError("listen on " + path.string(), err));
    }
    return SocketListener{static_cast<NativeSocket>(s), 0};
}

std::expected<SocketStream, LocalConnectError> connectLocal(const std::filesystem::path& path)
{
    const auto file = localSocketFile(path);
    if (!file) return std::unexpected(LocalConnectError{file.error(), false});
    const sockaddr_un a = addressOf(*file);
    // A socket file is a reparse point that only AF_UNIX can open: std::filesystem::exists follows it, which Windows Server 2022 refuses (the
    // GitHub runners), so it reported "no socket there" for a listening daemon. The attributes are read without opening the file.
    if (::GetFileAttributesW(file->wstring().c_str()) == INVALID_FILE_ATTRIBUTES)
        return std::unexpected(LocalConnectError{PlatformError{"connect to " + path.string(), WSAECONNREFUSED, "cannot connect to " + path.string() + ": no socket there"}, true});
    const SOCKET s = win32::newSocket(AF_UNIX, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return std::unexpected(LocalConnectError{win32::socketError("create a local socket"), false});
    u_long off = 0, on = 1;
    ::ioctlsocket(s, FIONBIO, &off);
    const int rc = ::connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof a);
    const int err = rc == 0 ? 0 : ::WSAGetLastError();
    ::ioctlsocket(s, FIONBIO, &on);
    if (rc != 0) {
        ::closesocket(s);
        return std::unexpected(LocalConnectError{win32::socketError("connect to " + path.string(), err), err == WSAECONNREFUSED || err == WSAENETDOWN});
    }
    return SocketStream{static_cast<NativeSocket>(s)};
}

bool peerIsSameUser(const SocketStream&) { return true; }

std::string peerUser(const SocketStream&)
{
    wchar_t name[257];
    DWORD n = 257;
    if (!::GetUserNameW(name, &n) || n == 0) return "uid:0";
    return "user:" + win32::narrow(std::wstring_view(name, n - 1));
}

}  // namespace sub0squad::platform
