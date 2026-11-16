#pragma once
/// @file platform_ipc.hpp
/// The local control socket (docs/PLATFORM.md, module platform_ipc): a stream socket named by a path in the state directory, made of the same
/// SocketStream and SocketListener as TCP (platform_net.hpp). POSIX: AF_UNIX, opened through /proc/self/fd so the path may be longer than
/// sockaddr_un allows, created owner-only, the peer checked by SO_PEERCRED. Windows: AF_UNIX as well (Windows 10 1803 and later).

#include "sub0squad/platform_net.hpp"

#include <filesystem>
#include <string>

namespace sub0squad::platform {

/// Why a local connection failed; `noServer` when nothing listens there (no socket file, or nobody accepting): "start the daemon", not a fault.
struct LocalConnectError
{
    PlatformError error;
    bool noServer = false;
};

/// Listen on `path`, creating it owner-only. A stale socket file is removed first: the caller must already hold the instance lock.
[[nodiscard]] std::expected<SocketListener, PlatformError> listenLocal(const std::filesystem::path& path);

/// Connect to the socket at `path`.
[[nodiscard]] std::expected<SocketStream, LocalConnectError> connectLocal(const std::filesystem::path& path);

/// Whether the process at the other end of a local stream runs as the same user as this one. POSIX: SO_PEERCRED. Windows keeps no peer
/// credentials on AF_UNIX: the owner-only state directory is the check, and this answers true.
[[nodiscard]] bool peerIsSameUser(const SocketStream& stream);

/// The user at the other end, for records ("uid:1000"); "uid:0" when it cannot be read. Windows: the name of this process's user.
[[nodiscard]] std::string peerUser(const SocketStream& stream);

}  // namespace sub0squad::platform
