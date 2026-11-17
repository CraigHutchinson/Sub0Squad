#pragma once
/// @file platform_file.hpp
/// Files (docs/PLATFORM.md, module platform_file): whole-file reads and writes in binary mode (never a text-mode CRLF), atomic replacement, durable
/// appends for the journal and the ledger, a file's modification time, and whole writes and reads on an inherited descriptor.

#include "sub0squad/platform_error.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sub0squad::platform {

/// The whole file, or nullopt when it cannot be opened.
[[nodiscard]] std::optional<std::string> readFile(const std::filesystem::path& path);
/// The whole file, or "" when it cannot be opened (for callers to whom a missing file and an empty one mean the same).
[[nodiscard]] std::string readFileText(const std::filesystem::path& path);

/// Replace `path` with `text`, creating its parent directories.
[[nodiscard]] std::expected<void, PlatformError> writeFile(const std::filesystem::path& path, std::string_view text);
/// Like writeFile, but through a sibling temporary file and a rename: a reader sees the old content or the new, never half of it. `durable`: the
/// temporary file is flushed to the device before the rename, so after a crash the file holds the old content or the whole new one.
[[nodiscard]] std::expected<void, PlatformError> writeFileAtomic(const std::filesystem::path& path, std::string_view text, bool durable = false);

/// Remove `path` and everything under it, opening no file: a local socket (an AF_UNIX reparse point on Windows) cannot be opened, and
/// std::filesystem::remove_all blocked on one for ever on Windows Server 2022. A missing `path` is success. The error names the first entry that
/// could not be removed; the rest is still attempted.
[[nodiscard]] std::expected<void, PlatformError> removeTree(const std::filesystem::path& path);

/// The path of the device that discards what is written to it ("/dev/null", "NUL"), for a tool's output argument.
[[nodiscard]] const char* nullDevice();

/// Seconds since the epoch of the last change to `path`; nullopt when it does not exist.
[[nodiscard]] std::optional<double> modifiedSeconds(const std::filesystem::path& path);

/// A file opened for appending only: every write goes to the end, even with other writers. Move-only; closes on destruction.
class AppendFile
{
public:
    AppendFile() = default;
    AppendFile(const AppendFile&) = delete;
    AppendFile& operator=(const AppendFile&) = delete;
    AppendFile(AppendFile&& other) noexcept;
    AppendFile& operator=(AppendFile&& other) noexcept;
    ~AppendFile();

    /// Open (creating it with `perms` if new) for appending.
    [[nodiscard]] static std::expected<AppendFile, PlatformError> open(const std::filesystem::path& path,
                                                                       std::filesystem::perms perms = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    /// Write all of `data` (one write call when it fits, so concurrent appenders do not interleave a line).
    [[nodiscard]] std::expected<void, PlatformError> append(std::string_view data);
    /// Flush the written data to the device (fdatasync; FlushFileBuffers on Windows).
    [[nodiscard]] std::expected<void, PlatformError> sync();
    /// Cut the file to `size` bytes (a torn tail).
    [[nodiscard]] std::expected<void, PlatformError> truncate(std::uint64_t size);
    [[nodiscard]] bool isOpen() const { return handle_ >= 0; }

private:
    explicit AppendFile(int handle) : handle_(handle) {}
    int handle_ = -1;
};

/// An exclusive lock on a file, held until the object is destroyed or this process ends (the system releases it even after a crash). Move-only.
class FileLock
{
public:
    FileLock() = default;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&& other) noexcept : handle_(other.handle_) { other.handle_ = -1; }
    FileLock& operator=(FileLock&& other) noexcept;
    ~FileLock();

    /// Take the lock on `path` (created owner-only if new) without waiting: nullopt when another process holds it, an error when the file
    /// cannot be opened. POSIX: flock; Windows: LockFileEx.
    [[nodiscard]] static std::expected<std::optional<FileLock>, PlatformError> tryExclusive(const std::filesystem::path& path);
    [[nodiscard]] bool isHeld() const { return handle_ != -1; }

private:
    explicit FileLock(std::intptr_t handle) : handle_(handle) {}
    std::intptr_t handle_ = -1;
};

/// Open, append and close: one record to a shared log (the ledger).
[[nodiscard]] std::expected<void, PlatformError> appendToFile(const std::filesystem::path& path, std::string_view data,
                                                              std::filesystem::perms perms = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);

/// Write all of `data` to an open descriptor (a pipe or socket this process inherited or holds), retrying interrupted writes.
[[nodiscard]] std::expected<void, PlatformError> writeAll(int descriptor, std::string_view data);
/// Read what is available from an open descriptor into `buffer`, blocking until something is; 0 means the other end closed.
[[nodiscard]] std::expected<std::size_t, PlatformError> readSome(int descriptor, char* buffer, std::size_t size);

}  // namespace sub0squad::platform
