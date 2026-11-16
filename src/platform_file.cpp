// POSIX backend of platform_file.hpp: open/write/fdatasync/ftruncate for appends and descriptors, stat for the modification time, and
// std::fstream in binary mode for whole files.
#include "sub0squad/platform_file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sub0squad::platform {

namespace fs = std::filesystem;

namespace {

PlatformError fromErrno(std::string operation, int code = errno) { return PlatformError{std::move(operation), code, std::strerror(code)}; }

PlatformError fromCode(std::string operation, const std::error_code& ec) { return PlatformError{std::move(operation), ec.value(), ec.message()}; }

mode_t modeOf(fs::perms p) { return static_cast<mode_t>(p & fs::perms::mask); }

}  // namespace

std::optional<std::string> readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return std::move(ss).str();
}

std::string readFileText(const fs::path& path) { return readFile(path).value_or(std::string{}); }

std::expected<void, PlatformError> writeFile(const fs::path& path, std::string_view text)
{
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return std::unexpected(fromErrno("open " + path.string()));
    o.write(text.data(), static_cast<std::streamsize>(text.size()));
    o.close();
    if (!o) return std::unexpected(fromErrno("write " + path.string()));
    return {};
}

std::expected<void, PlatformError> writeFileAtomic(const fs::path& path, std::string_view text)
{
    const fs::path tmp = path.string() + ".tmp";
    if (auto w = writeFile(tmp, text); !w) return w;
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) { fs::remove(tmp, ec); return std::unexpected(fromCode("rename to " + path.string(), ec)); }
    return {};
}

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

std::expected<void, PlatformError> appendToFile(const fs::path& path, std::string_view data, fs::perms perms)
{
    auto f = AppendFile::open(path, perms);
    if (!f) return std::unexpected(f.error());
    return f->append(data);
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

}  // namespace sub0squad::platform
