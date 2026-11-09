#include "sub0squad/unix_socket.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace sub0squad {

namespace {
struct Addr { sockaddr_un sa{}; socklen_t len = 0; int dirFd = -1; std::string error; };

Addr makeAddr(const std::filesystem::path& path)
{
    Addr a;
    a.dirFd = ::open(path.parent_path().c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (a.dirFd < 0) { a.error = std::string{"cannot open "} + path.parent_path().string() + ": " + std::strerror(errno); return a; }
    const std::string rel = "/proc/self/fd/" + std::to_string(a.dirFd) + "/" + path.filename().string();
    if (rel.size() >= sizeof a.sa.sun_path) { a.error = "socket path too long"; return a; }
    a.sa.sun_family = AF_UNIX;
    std::memcpy(a.sa.sun_path, rel.c_str(), rel.size() + 1);
    a.len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + rel.size() + 1);
    return a;
}
}  // namespace

std::expected<int, std::string> listenUnix(const std::filesystem::path& path)
{
    Addr a = makeAddr(path);
    if (!a.error.empty()) return std::unexpected(a.error);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) { ::close(a.dirFd); return std::unexpected(std::string{"socket: "} + std::strerror(errno)); }
    const mode_t old = ::umask(0177);
    const int rc = ::bind(fd, reinterpret_cast<sockaddr*>(&a.sa), a.len);
    ::umask(old);
    const int err = errno;
    ::close(a.dirFd);
    if (rc != 0) { ::close(fd); return std::unexpected(std::string{"bind "} + path.string() + ": " + std::strerror(err)); }
    if (::listen(fd, 64) != 0) { ::close(fd); return std::unexpected(std::string{"listen: "} + std::strerror(errno)); }
    return fd;
}

ConnectResult connectUnix(const std::filesystem::path& path)
{
    ConnectResult r;
    Addr a = makeAddr(path);
    if (!a.error.empty()) { r.error = a.error; r.noDaemon = a.dirFd < 0; return r; }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { ::close(a.dirFd); r.error = std::strerror(errno); return r; }
    const int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&a.sa), a.len);
    const int err = errno;
    ::close(a.dirFd);
    if (rc != 0) {
        ::close(fd);
        r.noDaemon = err == ENOENT || err == ECONNREFUSED;
        r.error = std::string{"cannot connect to "} + path.string() + ": " + std::strerror(err);
        return r;
    }
    r.fd = fd;
    return r;
}

std::expected<uid_t, std::string> peerUid(int fd)
{
    ucred cred{};
    socklen_t len = sizeof cred;
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) return std::unexpected(std::string{"SO_PEERCRED: "} + std::strerror(errno));
    return cred.uid;
}

std::expected<int, std::string> takeInstanceLock(const std::filesystem::path& path)
{
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return std::unexpected(std::string{"cannot open "} + path.string() + ": " + std::strerror(errno));
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return std::unexpected("another sub0squad daemon already runs for this state directory");
    }
    return fd;
}

}  // namespace sub0squad
