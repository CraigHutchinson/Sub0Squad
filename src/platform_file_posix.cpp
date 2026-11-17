// POSIX backend of platform_file.hpp: open/write/fdatasync/ftruncate for appends and descriptors, stat for the modification time. The whole-file
// functions are in platform_file.cpp.
#include "sub0squad/platform_file.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace sub0squad::platform {

namespace fs = std::filesystem;

namespace {

PlatformError fromErrno(std::string operation, int code = errno) { return PlatformError{std::move(operation), code, std::strerror(code)}; }

mode_t modeOf(fs::perms p) { return static_cast<mode_t>(p & fs::perms::mask); }

}  // namespace

std::optional<double> modifiedSeconds(const fs::path& path)
{
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return std::nullopt;
    return static_cast<double>(st.st_mtim.tv_sec) + static_cast<double>(st.st_mtim.tv_nsec) / 1e9;
}

AppendFile::AppendFile(AppendFile&& other) noexcept : handle_(other.handle_) { other.handle_ = -1; }

AppendFile& AppendFile::operator=(AppendFile&& other) noexcept
{
    if (this != &other) {
        if (handle_ >= 0) ::close(handle_);
        handle_ = other.handle_;
        other.handle_ = -1;
    }
    return *this;
}

AppendFile::~AppendFile()
{
    if (handle_ >= 0) ::close(handle_);
}

std::expected<AppendFile, PlatformError> AppendFile::open(const fs::path& path, fs::perms perms)
{
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, modeOf(perms));
    if (fd < 0) return std::unexpected(fromErrno("open " + path.string()));
    return AppendFile{fd};
}

std::expected<void, PlatformError> AppendFile::append(std::string_view data)
{
    if (handle_ < 0) return std::unexpected(PlatformError{"append", EBADF, "the file is not open"});
    return writeAll(handle_, data);
}

std::expected<void, PlatformError> AppendFile::sync()
{
    if (handle_ < 0) return std::unexpected(PlatformError{"sync", EBADF, "the file is not open"});
    if (::fdatasync(handle_) != 0) return std::unexpected(fromErrno("fdatasync"));
    return {};
}

std::expected<void, PlatformError> AppendFile::truncate(std::uint64_t size)
{
    if (handle_ < 0) return std::unexpected(PlatformError{"truncate", EBADF, "the file is not open"});
    if (::ftruncate(handle_, static_cast<off_t>(size)) != 0) return std::unexpected(fromErrno("ftruncate"));
    return {};
}

FileLock& FileLock::operator=(FileLock&& other) noexcept
{
    if (this != &other) {
        if (handle_ != -1) ::close(static_cast<int>(handle_));
        handle_ = other.handle_;
        other.handle_ = -1;
    }
    return *this;
}

FileLock::~FileLock()
{
    if (handle_ != -1) ::close(static_cast<int>(handle_));   // closing the descriptor releases the flock
}

std::expected<std::optional<FileLock>, PlatformError> FileLock::tryExclusive(const fs::path& path)
{
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return std::unexpected(fromErrno("open " + path.string()));
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int err = errno;
        ::close(fd);
        if (err == EWOULDBLOCK) return std::optional<FileLock>{};
        return std::unexpected(fromErrno("lock " + path.string(), err));
    }
    return std::optional<FileLock>{FileLock{fd}};
}

std::expected<void, PlatformError> writeAll(int descriptor, std::string_view data)
{
    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        const ssize_t n = ::write(descriptor, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(fromErrno("write"));
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    return {};
}

std::expected<std::size_t, PlatformError> readSome(int descriptor, char* buffer, std::size_t size)
{
    for (;;) {
        const ssize_t n = ::read(descriptor, buffer, size);
        if (n >= 0) return static_cast<std::size_t>(n);
        if (errno != EINTR) return std::unexpected(fromErrno("read"));
    }
}

const char* nullDevice() { return "/dev/null"; }

}  // namespace sub0squad::platform
