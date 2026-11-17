// Windows backend of platform_net.hpp: Winsock 2, sockets non-blocking and not inherited, waited on with WSAPoll. Windows raises no SIGPIPE.
// A listener uses SO_EXCLUSIVEADDRUSE, so a port another listener holds is refused here as on POSIX (plain SO_REUSEADDR would let two share it).
#include "sub0squad/platform_net.hpp"

#include "platform_win32_util.hpp"
#include "platform_util.hpp"

#include <algorithm>
#include <vector>

namespace sub0squad::platform {

namespace {

using detail::Deadline;

SOCKET sock(NativeSocket s) { return static_cast<SOCKET>(s); }

/// Send small writes at once (no Nagle): the library's TCP traffic is requests and replies, where a held write waits for a delayed ACK.
void setNoDelay(SOCKET s)
{
    const BOOL on = TRUE;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
}

/// WSAPoll on one socket. 1 ready, 0 timed out, -1 failed.
int waitOne(SOCKET s, short events, const Deadline& deadline)
{
    WSAPOLLFD p{s, events, 0};
    const int r = ::WSAPoll(&p, 1, static_cast<int>(deadline.msLeft()));
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

/// Connect `s` while blocking (no deadline: as the POSIX side), then make it non-blocking again. 0 or the Winsock error.
int connectBlocking(SOCKET s, const sockaddr* addr, int len)
{
    u_long off = 0, on = 1;
    ::ioctlsocket(s, FIONBIO, &off);
    const int rc = ::connect(s, addr, len);
    const int err = rc == 0 ? 0 : ::WSAGetLastError();
    ::ioctlsocket(s, FIONBIO, &on);
    return err;
}

}  // namespace

void closeSocket(NativeSocket s)
{
    if (s != -1) ::closesocket(sock(s));
}

SocketStream& SocketStream::operator=(SocketStream&& o) noexcept
{
    if (this != &o) {
        closeSocket(socket_);
        socket_ = o.socket_;
        o.socket_ = -1;
    }
    return *this;
}

SocketStream::~SocketStream() { closeSocket(socket_); }

std::expected<void, PlatformError> SocketStream::writeAll(std::string_view data)
{
    if (socket_ == -1) return std::unexpected(PlatformError{"write", WSAENOTSOCK, "cannot write: the socket is not open"});
    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        const int chunk = static_cast<int>(std::min<std::size_t>(left, 1 << 30));
        const int n = ::send(sock(socket_), p, chunk, 0);
        if (n > 0) { p += n; left -= static_cast<std::size_t>(n); continue; }
        const int err = n == 0 ? WSAECONNRESET : ::WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            if (waitOne(sock(socket_), POLLWRNORM, Deadline{std::nullopt}) < 0) return std::unexpected(win32::socketError("wait to write"));
            continue;
        }
        return std::unexpected(win32::socketError("write to the socket", err));
    }
    return {};
}

std::expected<std::size_t, PlatformError> SocketStream::writeSome(std::string_view data)
{
    if (socket_ == -1) return std::unexpected(PlatformError{"write", WSAENOTSOCK, "cannot write: the socket is not open"});
    const int n = ::send(sock(socket_), data.data(), static_cast<int>(std::min<std::size_t>(data.size(), 1 << 30)), 0);
    if (n >= 0) return static_cast<std::size_t>(n);
    const int err = ::WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return std::size_t{0};
    return std::unexpected(win32::socketError("write to the socket", err));
}

std::expected<std::optional<std::size_t>, PlatformError> SocketStream::readSome(char* buffer, std::size_t size, std::optional<std::chrono::milliseconds> timeout)
{
    if (socket_ == -1) return std::unexpected(PlatformError{"read", WSAENOTSOCK, "cannot read: the socket is not open"});
    const Deadline deadline{timeout};
    for (;;) {
        const int n = ::recv(sock(socket_), buffer, static_cast<int>(std::min<std::size_t>(size, 1 << 30)), 0);
        if (n >= 0) return std::optional<std::size_t>{static_cast<std::size_t>(n)};
        const int err = ::WSAGetLastError();
        if (err == WSAECONNRESET || err == WSAECONNABORTED) return std::optional<std::size_t>{0};   // the peer is gone: end of stream, as on POSIX
        if (err != WSAEWOULDBLOCK) return std::unexpected(win32::socketError("read from the socket", err));
        const int r = waitOne(sock(socket_), POLLRDNORM, deadline);
        if (r < 0) return std::unexpected(win32::socketError("wait to read"));
        if (r == 0) return std::optional<std::size_t>{};
    }
}

SocketListener& SocketListener::operator=(SocketListener&& o) noexcept
{
    if (this != &o) {
        closeSocket(socket_);
        socket_ = o.socket_;
        port_ = o.port_;
        o.socket_ = -1;
    }
    return *this;
}

SocketListener::~SocketListener() { closeSocket(socket_); }

std::expected<std::optional<SocketStream>, PlatformError> SocketListener::accept(std::optional<std::chrono::milliseconds> timeout)
{
    if (socket_ == -1) return std::unexpected(PlatformError{"accept", WSAENOTSOCK, "cannot accept: the listener is not open"});
    const Deadline deadline{timeout};
    for (;;) {
        const SOCKET c = ::accept(sock(socket_), nullptr, nullptr);
        if (c != INVALID_SOCKET) {
            u_long on = 1;
            ::ioctlsocket(c, FIONBIO, &on);   // inherited from the listener already; said again so it never depends on that
            ::SetHandleInformation(reinterpret_cast<HANDLE>(c), HANDLE_FLAG_INHERIT, 0);
            setNoDelay(c);
            return std::optional<SocketStream>{SocketStream{static_cast<NativeSocket>(c)}};
        }
        const int err = ::WSAGetLastError();
        if (err == WSAECONNRESET) continue;   // the client gave up while it waited
        if (err != WSAEWOULDBLOCK) return std::unexpected(win32::socketError("accept a connection", err));
        const int r = waitOne(sock(socket_), POLLRDNORM, deadline);
        if (r < 0) return std::unexpected(win32::socketError("wait for a connection"));
        if (r == 0) return std::optional<SocketStream>{};
    }
}

std::expected<SocketStream, PlatformError> connectTcp(const std::string& host, std::uint16_t port)
{
    const std::string where = host + ":" + std::to_string(port);
    if (!win32::winsockReady()) return std::unexpected(win32::socketError("start Winsock"));
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (const int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res); rc != 0 || !res)
        return std::unexpected(PlatformError{"resolve " + host, rc, "cannot resolve " + host + ": " + win32::errorText(static_cast<DWORD>(rc))});
    int err = WSAECONNREFUSED;
    for (const addrinfo* a = res; a; a = a->ai_next) {
        const SOCKET s = win32::newSocket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) { err = ::WSAGetLastError(); continue; }
        err = connectBlocking(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
        if (err == 0) {
            ::freeaddrinfo(res);
            setNoDelay(s);
            return SocketStream{static_cast<NativeSocket>(s)};
        }
        ::closesocket(s);
    }
    ::freeaddrinfo(res);
    return std::unexpected(win32::socketError("connect to " + where, err));
}

std::expected<SocketListener, PlatformError> listenLoopback(std::uint16_t port)
{
    const SOCKET s = win32::newSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return std::unexpected(win32::socketError("create a socket"));
    const BOOL on = TRUE;
    ::setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof on);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    const std::string where = "127.0.0.1:" + std::to_string(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(s, 64) != 0) {
        const int err = ::WSAGetLastError();
        ::closesocket(s);
        return std::unexpected(win32::socketError("listen on " + where, err));
    }
    int len = sizeof a;
    ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    return SocketListener{static_cast<NativeSocket>(s), ntohs(a.sin_port)};
}

std::optional<std::size_t> waitForConnection(std::span<const SocketListener* const> listeners, std::optional<std::chrono::milliseconds> timeout)
{
    std::vector<WSAPOLLFD> fds;
    for (const auto* l : listeners) fds.push_back(WSAPOLLFD{sock(l->native()), POLLRDNORM, 0});
    const int r = ::WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), static_cast<int>(Deadline{timeout}.msLeft()));
    if (r <= 0) return std::nullopt;
    for (std::size_t i = 0; i < fds.size(); ++i)
        if (fds[i].revents & POLLRDNORM) return i;
    return std::nullopt;
}

bool loopbackPortInUse(std::uint16_t port) { return connectTcp("127.0.0.1", port).has_value(); }

}  // namespace sub0squad::platform
