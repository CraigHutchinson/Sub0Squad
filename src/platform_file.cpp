// platform_file.hpp, the part every operating system shares: whole files through std::fstream in binary mode, atomic replacement by rename,
// and the one-record append. The descriptors, appends and modification time are in platform_file_posix.cpp and platform_file_win32.cpp.
#include "sub0squad/platform_file.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sub0squad::platform {

namespace fs = std::filesystem;

namespace {

PlatformError fromErrno(std::string operation, int code = errno) { return PlatformError{std::move(operation), code, std::strerror(code)}; }

PlatformError fromCode(std::string operation, const std::error_code& ec) { return PlatformError{std::move(operation), ec.value(), ec.message()}; }

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
    fs::rename(tmp, path, ec);   // replaces an existing file on both systems (MoveFileExW with MOVEFILE_REPLACE_EXISTING on Windows)
    if (ec) { fs::remove(tmp, ec); return std::unexpected(fromCode("rename to " + path.string(), ec)); }
    return {};
}

std::expected<void, PlatformError> appendToFile(const fs::path& path, std::string_view data, fs::perms perms)
{
    auto f = AppendFile::open(path, perms);
    if (!f) return std::unexpected(f.error());
    return f->append(data);
}

}  // namespace sub0squad::platform
