#pragma once
/// @file unix_socket.hpp
/// Local control socket plumbing (Linux backend of IIpcEndpoint). Long paths work because the socket is opened relative to a
/// directory descriptor through /proc/self/fd, which keeps sockaddr_un's 107-byte limit out of the design.

#include <sys/types.h>
#include <expected>
#include <filesystem>
#include <string>

namespace sub0squad {

/// Bind and listen on `path` (mode 0600). A stale socket file is removed first: the caller must already hold the instance lock.
[[nodiscard]] std::expected<int, std::string> listenUnix(const std::filesystem::path& path);
/// Connect to `path`. Error text distinguishes "no daemon" (ENOENT/ECONNREFUSED) from other failures via the `noDaemon` flag.
struct ConnectResult { int fd = -1; bool noDaemon = false; std::string error; };
[[nodiscard]] ConnectResult connectUnix(const std::filesystem::path& path);
/// uid of the process at the other end.
[[nodiscard]] std::expected<uid_t, std::string> peerUid(int fd);
/// Take the exclusive single-instance lock at `path` (returns the fd to keep open), or an error if another daemon holds it.
[[nodiscard]] std::expected<int, std::string> takeInstanceLock(const std::filesystem::path& path);

}  // namespace sub0squad
