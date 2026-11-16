// POSIX backend of platform_net.hpp: BSD sockets, non-blocking and close-on-exec, waited on with poll; MSG_NOSIGNAL so a gone peer is an error
// and never a SIGPIPE.
#include "sub0squad/platform_net.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

namespace sub0squad::platform {

namespace {

using Clock = std::chrono::steady_clock;

PlatformError sysError(const std::string& operation, int code = errno)
{
    return PlatformError{operation, code, "cannot " + operation + ": " + std::strerror(code)};
}

void setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/// poll() on one descriptor until `events`, the deadline or an error; EINTR restarts with the time left. 1 ready, 0 timed out, -1 failed.
int waitOne(int fd, short events, std::optional<Clock::time_point> deadline)
{
    for (;;) {
        int ms = -1;
        if (deadline) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now()).count();
            ms = static_cast<int>(std::clamp<long long>(left, 0, 1 << 30));
        }
        pollfd p{fd, events, 0};
        const int r = ::poll(&p, 1, ms);
        if (r >= 0) return r > 0 ? 1 : 0;
        if (errno != EINTR) return -1;
    }
}

std::optional<Clock::time_point> deadlineOf(std::optional<std::chrono::milliseconds> timeout)
{
    return timeout ? std::optional{Clock::now() + *timeout} : std::nullopt;
}

}  // namespace

void closeSocket(NativeSocket s)
{
    if (s != -1) ::close(static_cast<int>(s));
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
    if (socket_ == -1) return std::unexpected(PlatformError{"write", EBADF, "cannot write: the socket is not open"});
    const int fd = static_cast<int>(socket_);
    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        const ssize_t n = ::send(fd, p, left, MSG_NOSIGNAL);
        if (n > 0) { p += n; left -= static_cast<std::size_t>(n); continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (waitOne(fd, POLLOUT, std::nullopt) < 0) return std::unexpected(sysError("wait to write"));
            continue;
        }
        return std::unexpected(sysError("write to the socket", n == 0 ? EPIPE : errno));
    }
    return {};
}

std::expected<std::optional<std::size_t>, PlatformError> SocketStream::readSome(char* buffer, std::size_t size, std::optional<std::chrono::milliseconds> timeout)
{
    if (socket_ == -1) return std::unexpected(PlatformError{"read", EBADF, "cannot read: the socket is not open"});
    const int fd = static_cast<int>(socket_);
    const auto deadline = deadlineOf(timeout);
    for (;;) {
        const ssize_t n = ::recv(fd, buffer, size, 0);
        if (n >= 0) return std::optional<std::size_t>{static_cast<std::size_t>(n)};
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return std::unexpected(sysError("read from the socket"));
        const int r = waitOne(fd, POLLIN, deadline);
        if (r < 0) return std::unexpected(sysError("wait to read"));
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
    if (socket_ == -1) return std::unexpected(PlatformError{"accept", EBADF, "cannot accept: the listener is not open"});
    const int fd = static_cast<int>(socket_);
    const auto deadline = deadlineOf(timeout);
    for (;;) {
        const int c = ::accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (c >= 0) return std::optional<SocketStream>{SocketStream{c}};
        if (errno == EINTR || errno == ECONNABORTED) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return std::unexpected(sysError("accept a connection"));
        const int r = waitOne(fd, POLLIN, deadline);
        if (r < 0) return std::unexpected(sysError("wait for a connection"));
        if (r == 0) return std::optional<SocketStream>{};
    }
}

std::expected<SocketStream, PlatformError> connectTcp(const std::string& host, std::uint16_t port)
{
    const std::string where = host + ":" + std::to_string(port);
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (const int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res); rc != 0 || !res)
        return std::unexpected(PlatformError{"resolve " + host, rc, "cannot resolve " + host + ": " + ::gai_strerror(rc)});
    int err = ECONNREFUSED;
    for (const addrinfo* a = res; a; a = a->ai_next) {
        const int fd = ::socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (fd < 0) { err = errno; continue; }
        int rc;
        do rc = ::connect(fd, a->ai_addr, a->ai_addrlen); while (rc != 0 && errno == EINTR);
        if (rc == 0) {
            ::freeaddrinfo(res);
            setNonBlocking(fd);
            return SocketStream{fd};
        }
        err = errno;
        ::close(fd);
    }
    ::freeaddrinfo(res);
    return std::unexpected(PlatformError{"connect to " + where, err, "cannot connect to " + where + ": " + std::strerror(err)});
}

std::expected<SocketListener, PlatformError> listenLoopback(std::uint16_t port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return std::unexpected(sysError("create a socket"));
    const int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);   // reuse a port in TIME_WAIT; a live listener still refuses a second bind
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    const std::string where = "127.0.0.1:" + std::to_string(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 64) != 0) {
        const int err = errno;
        ::close(fd);
        return std::unexpected(sysError("listen on " + where, err));
    }
    socklen_t len = sizeof a;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    return SocketListener{fd, ntohs(a.sin_port)};
}

std::optional<std::size_t> waitForConnection(std::span<const SocketListener* const> listeners, std::optional<std::chrono::milliseconds> timeout)
{
    std::vector<pollfd> fds;
    for (const auto* l : listeners) fds.push_back(pollfd{static_cast<int>(l->native()), POLLIN, 0});
    const auto deadline = deadlineOf(timeout);
    for (;;) {
        int ms = -1;
        if (deadline) ms = static_cast<int>(std::clamp<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now()).count(), 0, 1 << 30));
        const int r = ::poll(fds.data(), fds.size(), ms);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return std::nullopt;
        for (std::size_t i = 0; i < fds.size(); ++i)
            if (fds[i].revents & POLLIN) return i;
        return std::nullopt;
    }
}

bool loopbackPortInUse(std::uint16_t port) { return connectTcp("127.0.0.1", port).has_value(); }

}  // namespace sub0squad::platform
