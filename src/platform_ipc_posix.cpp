// POSIX backend of platform_ipc.hpp: AF_UNIX sockets named relative to a directory descriptor through /proc/self/fd, which keeps
// sockaddr_un's 107-byte limit out of the design; created under umask 0177 (owner-only); the peer checked by SO_PEERCRED.
#include "sub0squad/platform_ipc.hpp"

#include "platform_posix_util.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>

namespace sub0squad::platform {

namespace {

using posix::sysError;

/// The address of `path`, through a descriptor of its directory that the caller closes after bind or connect.
struct Address
{
    sockaddr_un sa{};
    socklen_t len = 0;
    int dirFd = -1;
};

std::expected<Address, PlatformError> addressOf(const std::filesystem::path& path)
{
    Address a;
    a.dirFd = ::open(path.parent_path().c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (a.dirFd < 0) return std::unexpected(sysError("open " + path.parent_path().string()));
    const std::string rel = "/proc/self/fd/" + std::to_string(a.dirFd) + "/" + path.filename().string();
    if (rel.size() >= sizeof a.sa.sun_path) {
        ::close(a.dirFd);
        return std::unexpected(PlatformError{"name a socket", ENAMETOOLONG, "cannot name the socket " + path.string() + ": its file name is too long"});
    }
    a.sa.sun_family = AF_UNIX;
    std::memcpy(a.sa.sun_path, rel.c_str(), rel.size() + 1);
    a.len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + rel.size() + 1);
    return a;
}

std::optional<ucred> credentials(const SocketStream& s)
{
    ucred cred{};
    socklen_t len = sizeof cred;
    if (::getsockopt(static_cast<int>(s.native()), SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) return std::nullopt;
    return cred;
}

}  // namespace

std::expected<SocketListener, PlatformError> listenLocal(const std::filesystem::path& path)
{
    auto a = addressOf(path);
    if (!a) return std::unexpected(a.error());
    std::error_code ec;
    std::filesystem::remove(path, ec);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) { ::close(a->dirFd); return std::unexpected(sysError("create a socket")); }
    const mode_t old = ::umask(0177);
    const int rc = ::bind(fd, reinterpret_cast<sockaddr*>(&a->sa), a->len);
    ::umask(old);
    const int err = errno;
    ::close(a->dirFd);
    if (rc != 0) { ::close(fd); return std::unexpected(sysError("bind " + path.string(), err)); }
    if (::listen(fd, 64) != 0) { const int e = errno; ::close(fd); return std::unexpected(sysError("listen on " + path.string(), e)); }
    return SocketListener{fd, 0};
}

std::expected<SocketStream, LocalConnectError> connectLocal(const std::filesystem::path& path)
{
    auto a = addressOf(path);
    if (!a) return std::unexpected(LocalConnectError{a.error(), a.error().code != ENAMETOOLONG});   // no state directory: nothing can be listening
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { ::close(a->dirFd); return std::unexpected(LocalConnectError{sysError("create a socket"), false}); }
    int rc;
    do rc = ::connect(fd, reinterpret_cast<sockaddr*>(&a->sa), a->len); while (rc != 0 && errno == EINTR);
    const int err = errno;
    ::close(a->dirFd);
    if (rc != 0) {
        ::close(fd);
        return std::unexpected(LocalConnectError{sysError("connect to " + path.string(), err), err == ENOENT || err == ECONNREFUSED});
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return SocketStream{fd};
}

bool peerIsSameUser(const SocketStream& stream)
{
    const auto c = credentials(stream);
    return c && c->uid == ::getuid();
}

std::string peerUser(const SocketStream& stream)
{
    const auto c = credentials(stream);
    return "uid:" + std::to_string(c ? c->uid : 0);
}

}  // namespace sub0squad::platform
