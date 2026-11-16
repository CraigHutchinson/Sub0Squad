#pragma once
/// @file platform_process.hpp
/// Starting and waiting for child processes: the one launcher of the program (docs/PLATFORM.md section 4). Arguments are a vector, never a shell
/// string. Every child gets only the standard streams: descriptors the caller holds are never inherited.

#include "sub0squad/platform_error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sub0squad::platform {

/// A process id as the system numbers it (a pid on POSIX), widened so the type is the same on every platform.
using ProcessId = std::int64_t;

/// What to start and how its standard streams are connected.
struct ProcessSpec
{
    std::vector<std::string> argv{};             ///< argv[0] is the program: a path, or a bare name searched on PATH
    std::filesystem::path cwd{};                 ///< the child's working directory; empty: the caller's
    std::filesystem::path outputFile{};          ///< stdout and stderr both go to this file; empty: inherited (or captured by runCaptured)
    bool appendOutput = false;                   ///< append to outputFile instead of truncating it
    std::filesystem::perms outputPerms = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::group_read |
                                         std::filesystem::perms::others_read;   ///< permissions of outputFile when it is created
    bool nullInput = true;                       ///< stdin reads from the null device; false: inherited
    bool newSession = false;                     ///< the child leads its own session and process group: detached from the caller's terminal and
                                                 ///< signals, and Child::kill and terminate reach its whole group
};

/// How a child ended.
struct ExitStatus
{
    int code = -1;           ///< the exit code when it exited, -1 when a signal ended it
    int signal = 0;          ///< the signal that ended it, 0 when it exited

    /// Whether it exited (rather than being ended by a signal).
    [[nodiscard]] bool exited() const { return signal == 0 && code >= 0; }
    /// The shell's convention: the exit code, or 128 plus the signal.
    [[nodiscard]] int shellCode() const { return signal != 0 ? 128 + signal : code; }
};

/// A started child. Move-only. Destroying it neither waits nor kills: a detached child may outlive this object and this program, and a child that
/// is never waited for is reaped by the system when this program ends.
class Child
{
public:
    Child() = default;
    /// For the implementation: wraps a started process.
    Child(ProcessId id, bool leadsGroup) : id_(id), leadsGroup_(leadsGroup) {}
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    Child(Child&& o) noexcept : id_(o.id_), leadsGroup_(o.leadsGroup_), status_(o.status_) { o.id_ = 0; }
    Child& operator=(Child&& o) noexcept;
    ~Child() = default;

    /// Its process id (0 for an empty handle).
    [[nodiscard]] ProcessId id() const { return id_; }
    /// Waits until it ends, or until `timeout` passes (then nullopt and it keeps running). No timeout: waits as long as it takes. A second call after
    /// it ended returns the same status.
    [[nodiscard]] std::optional<ExitStatus> wait(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    /// Asks it to stop (SIGTERM on POSIX); its whole group when it leads one. False when it is already gone.
    bool terminate();
    /// Ends it at once (SIGKILL on POSIX); its whole group when it leads one. False when it is already gone.
    bool kill();

private:
    ProcessId id_ = 0;
    bool leadsGroup_ = false;
    std::optional<ExitStatus> status_;
};

/// Starts a child. The error names the program and the system's reason (a missing program, a working directory that does not exist, an output file
/// that cannot be opened).
[[nodiscard]] std::expected<Child, PlatformError> spawn(const ProcessSpec& spec);

/// How runCaptured collects the output.
struct CaptureOptions
{
    std::optional<std::chrono::milliseconds> timeout{}; ///< killed when it runs longer (its group when spec.newSession); none: no limit
    std::size_t maxBytes = 512 * 1024;                  ///< at most this much is kept: the END of the output, where a failure is reported
    bool includeStderr = true;                          ///< stderr is captured with stdout, interleaved; false: stderr is inherited
};

/// What runCaptured saw.
struct Captured
{
    ExitStatus status{};        ///< how it ended (when it timed out: the kill)
    std::string output{};       ///< stdout (and stderr), the last maxBytes of it
    bool timedOut = false;      ///< the timeout passed and it was killed
    bool truncated = false;     ///< more than maxBytes were written; the start was dropped
};

/// Runs a child to its end with its output captured. `spec.outputFile` must be empty. The output is read until the child closes it, so a background
/// process the child leaves holding its output keeps this waiting until the timeout.
[[nodiscard]] std::expected<Captured, PlatformError> runCaptured(const ProcessSpec& spec, const CaptureOptions& options = {});

}  // namespace sub0squad::platform
