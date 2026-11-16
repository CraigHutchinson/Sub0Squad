// Windows backend of platform_file.hpp: the C runtime's descriptors (_wopen in binary, append and no-inherit mode, _write, _commit, _chsize_s)
// for appends and descriptors, GetFileAttributesExW for the modification time. The whole-file functions are in platform_file.cpp.
#include "sub0squad/platform_file.hpp"

#include "platform_win32_util.hpp"

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>

namespace sub0squad::platform {

namespace fs = std::filesystem;

namespace {

PlatformError fromErrno(std::string operation, int code = errno) { return PlatformError{std::move(operation), code, std::strerror(code)}; }

constexpr std::uint64_t kEpochTicks = 116444736000000000ULL;   // 1970-01-01 in 100 ns ticks since 1601

}  // namespace

std::optional<double> modifiedSeconds(const fs::path& path)
{
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return std::nullopt;
    const std::uint64_t t = win32::ticks(a.ftLastWriteTime);
    return t < kEpochTicks ? 0.0 : static_cast<double>(t - kEpochTicks) / 1e7;
}

AppendFile::AppendFile(AppendFile&& other) noexcept : handle_(other.handle_) { other.handle_ = -1; }

AppendFile& AppendFile::operator=(AppendFile&& other) noexcept
{
    if (this != &other) {
        if (handle_ >= 0) ::_close(handle_);
        handle_ = other.handle_;
        other.handle_ = -1;
    }
    return *this;
}

AppendFile::~AppendFile()
{
    if (handle_ >= 0) ::_close(handle_);
}

std::expected<AppendFile, PlatformError> AppendFile::open(const fs::path& path, fs::perms perms)
{
    // Windows keeps only "read-only or not": a file without owner write is created read-only, the rest of `perms` has no meaning here.
    const int mode = (perms & fs::perms::owner_write) != fs::perms::none ? (_S_IREAD | _S_IWRITE) : _S_IREAD;
    int fd = -1;
    const errno_t e = ::_wsopen_s(&fd, path.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY | _O_NOINHERIT, _SH_DENYNO, mode);
    if (e != 0 || fd < 0) return std::unexpected(fromErrno("open " + path.string(), e));
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
    if (::_commit(handle_) != 0) return std::unexpected(fromErrno("flush to disk"));   // FlushFileBuffers underneath
    return {};
}

std::expected<void, PlatformError> AppendFile::truncate(std::uint64_t size)
{
    if (handle_ < 0) return std::unexpected(PlatformError{"truncate", EBADF, "the file is not open"});
    if (const errno_t e = ::_chsize_s(handle_, static_cast<__int64>(size)); e != 0) return std::unexpected(fromErrno("truncate", e));
    return {};
}

std::expected<void, PlatformError> writeAll(int descriptor, std::string_view data)
{
    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        const unsigned chunk = static_cast<unsigned>(std::min<std::size_t>(left, INT_MAX));
        const int n = ::_write(descriptor, p, chunk);
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
        const int n = ::_read(descriptor, buffer, static_cast<unsigned>(std::min<std::size_t>(size, INT_MAX)));
        if (n >= 0) return static_cast<std::size_t>(n);
        if (errno != EINTR) return std::unexpected(fromErrno("read"));
    }
}

}  // namespace sub0squad::platform
