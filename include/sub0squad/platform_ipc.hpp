#pragma once
/// @file platform_ipc.hpp
/// The local control socket (docs/PLATFORM.md, module platform_ipc): a stream socket named by a path in the state directory, made of the same
/// SocketStream and SocketListener as TCP (platform_net.hpp). POSIX: AF_UNIX, opened through /proc/self/fd so the path may be longer than
/// sockaddr_un allows, created owner-only, the peer checked by SO_PEERCRED. Windows: AF_UNIX as well (Windows 10 1803 and later).

#include "sub0squad/platform_net.hpp"

#include <chrono>
#include <filesystem>
#include <string>

namespace sub0squad::platform {

/// Why a local connection failed; `noServer` when nothing listens there (no socket file, or nobody accepting): "start the daemon", not a fault.
struct LocalConnectError
{
    PlatformError error;
    bool noServer = false;
};

/// The file the socket named `path` really has. POSIX: `path` itself, at any length. Windows: `path` while it fits sockaddr_un (107 bytes of
/// UTF-8); a longer one (a deep worktree, a test home under a long temp directory) maps to a short stable name, a hash of the full path, under
/// %USERPROFILE%\.sub0squad\sockets (the user's temp directory when that is unset), so a daemon and its clients agree on it. An error when even
/// that name is too long.
[[nodiscard]] std::expected<std::filesystem::path, PlatformError> localSocketFile(const std::filesystem::path& path);

/// Remove the file of the socket named `path`, wherever localSocketFile put it: a daemon calls this as it stops, while it still holds the instance
/// lock. Nothing there is not an error.
void removeLocal(const std::filesystem::path& path);

/// Remove moved socket names (Windows: under %USERPROFILE%\.sub0squad\sockets) that nobody listens on any more: left by a daemon that was
/// killed. A name younger than `minAge` is left alone, as its daemon may be between bind and listen. POSIX moves no names: nothing to do.
/// Returns how many went.
int clearStaleLocalSockets(std::chrono::seconds minAge = std::chrono::minutes(1));

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
