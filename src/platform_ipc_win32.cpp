// Windows backend of platform_ipc.hpp: AF_UNIX stream sockets (Windows 10 1803 and later), named by the path in UTF-8. sockaddr_un holds 108
// bytes and Windows has no /proc/self/fd way around that, so a longer path is an error that says so. Windows records no peer credentials on these
// sockets: who may connect is decided by the state directory's ACL (owner-only), and peerIsSameUser answers true.
#include "sub0squad/platform_ipc.hpp"

#include "platform_win32_util.hpp"

#include <afunix.h>

#include <cstring>

namespace sub0squad::platform {

namespace {

std::expected<sockaddr_un, PlatformError> addressOf(const std::filesystem::path& path)
{
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    const std::string name = win32::narrow(path.wstring());
    if (name.size() >= sizeof a.sun_path)
        return std::unexpected(PlatformError{"name a socket", WSAENAMETOOLONG,
                                             "cannot name the socket " + name + ": Windows allows at most " + std::to_string(sizeof a.sun_path - 1) + " bytes"});
    std::memcpy(a.sun_path, name.c_str(), name.size() + 1);
    return a;
}

}  // namespace

std::expected<SocketListener, PlatformError> listenLocal(const std::filesystem::path& path)
{
    auto a = addressOf(path);
    if (!a) return std::unexpected(a.error());
    std::error_code ec;
    std::filesystem::remove(path, ec);
    const SOCKET s = win32::newSocket(AF_UNIX, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return std::unexpected(win32::socketError("create a local socket"));
    if (::bind(s, reinterpret_cast<const sockaddr*>(&*a), sizeof *a) != 0 || ::listen(s, 64) != 0) {
        const int err = ::WSAGetLastError();
        ::closesocket(s);
        return std::unexpected(win32::socketError("listen on " + path.string(), err));
    }
    return SocketListener{static_cast<NativeSocket>(s), 0};
}

std::expected<SocketStream, LocalConnectError> connectLocal(const std::filesystem::path& path)
{
    auto a = addressOf(path);
    if (!a) return std::unexpected(LocalConnectError{a.error(), false});
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return std::unexpected(LocalConnectError{PlatformError{"connect to " + path.string(), WSAECONNREFUSED, "cannot connect to " + path.string() + ": no socket there"}, true});
    const SOCKET s = win32::newSocket(AF_UNIX, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return std::unexpected(LocalConnectError{win32::socketError("create a local socket"), false});
    u_long off = 0, on = 1;
    ::ioctlsocket(s, FIONBIO, &off);
    const int rc = ::connect(s, reinterpret_cast<const sockaddr*>(&*a), sizeof *a);
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
