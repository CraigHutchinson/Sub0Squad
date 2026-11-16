#pragma once
/// @file platform_net.hpp
/// Sockets (docs/PLATFORM.md, module platform_net): a connected stream and a listener, both move-only and closing themselves, and the TCP ways to
/// make them (a client to host:port, a listener on loopback). The local control socket (platform_ipc.hpp) makes the same two types. Every read and
/// accept can wait with a deadline; writes never raise SIGPIPE. Sockets are non-blocking underneath and never inherited by a child.

#include "sub0squad/platform_error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sub0squad::platform {

/// The native socket (a descriptor on POSIX, a SOCKET on Windows) widened to one type; -1 when none.
using NativeSocket = std::intptr_t;

/// A connected stream socket.
class SocketStream
{
public:
    SocketStream() = default;
    /// For the implementation: takes ownership of a connected socket.
    explicit SocketStream(NativeSocket s) : socket_(s) {}
    SocketStream(const SocketStream&) = delete;
    SocketStream& operator=(const SocketStream&) = delete;
    SocketStream(SocketStream&& o) noexcept : socket_(o.socket_) { o.socket_ = -1; }
    SocketStream& operator=(SocketStream&& o) noexcept;
    ~SocketStream();

    [[nodiscard]] bool isOpen() const { return socket_ != -1; }
    /// Write all of `data`, waiting while the peer's buffer is full. An error when the peer has gone.
    [[nodiscard]] std::expected<void, PlatformError> writeAll(std::string_view data);
    /// Read what is available, waiting up to `timeout` (none: as long as it takes; zero: not at all). nullopt when nothing came in time;
    /// 0 when the peer closed its end.
    [[nodiscard]] std::expected<std::optional<std::size_t>, PlatformError> readSome(char* buffer, std::size_t size,
                                                                                    std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    /// The native socket, still owned here (for a loop that waits on native handles: the daemon's, until slice 5).
    [[nodiscard]] NativeSocket native() const { return socket_; }
    /// Give up ownership: the caller closes the returned socket (with closeSocket).
    [[nodiscard]] NativeSocket release() { const NativeSocket s = socket_; socket_ = -1; return s; }

private:
    NativeSocket socket_ = -1;
};

/// A listening socket. accept() hands out streams.
class SocketListener
{
public:
    SocketListener() = default;
    /// For the implementation: takes ownership of a listening socket; `port` is its TCP port (0 for a local socket).
    SocketListener(NativeSocket s, std::uint16_t port) : socket_(s), port_(port) {}
    SocketListener(const SocketListener&) = delete;
    SocketListener& operator=(const SocketListener&) = delete;
    SocketListener(SocketListener&& o) noexcept : socket_(o.socket_), port_(o.port_) { o.socket_ = -1; }
    SocketListener& operator=(SocketListener&& o) noexcept;
    ~SocketListener();

    [[nodiscard]] bool isOpen() const { return socket_ != -1; }
    /// The next waiting connection, waiting up to `timeout` (none: as long as it takes; zero: not at all); nullopt when none came in time.
    [[nodiscard]] std::expected<std::optional<SocketStream>, PlatformError> accept(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    /// The TCP port it listens on (the one the system chose when 0 was asked for); 0 for a local socket.
    [[nodiscard]] std::uint16_t port() const { return port_; }
    [[nodiscard]] NativeSocket native() const { return socket_; }

private:
    NativeSocket socket_ = -1;
    std::uint16_t port_ = 0;
};

/// Close a socket taken with SocketStream::release.
void closeSocket(NativeSocket s);

/// Connect to `host`:`port` (a name or an address; IPv4 or IPv6).
[[nodiscard]] std::expected<SocketStream, PlatformError> connectTcp(const std::string& host, std::uint16_t port);

/// Listen on 127.0.0.1:`port` only (never reachable from another machine); 0 lets the system choose (see port()). A port another listener holds
/// is an error, on every system.
[[nodiscard]] std::expected<SocketListener, PlatformError> listenLoopback(std::uint16_t port);

/// Wait until one of `listeners` has a connection waiting: its index, or nullopt when `timeout` passed first.
[[nodiscard]] std::optional<std::size_t> waitForConnection(std::span<const SocketListener* const> listeners,
                                                           std::optional<std::chrono::milliseconds> timeout = std::nullopt);

/// True when something on this host accepts connections on 127.0.0.1:`port`.
[[nodiscard]] bool loopbackPortInUse(std::uint16_t port);

}  // namespace sub0squad::platform
