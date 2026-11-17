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
#include <string_view>
#include <vector>

namespace sub0squad::platform {

/// A process id as the system numbers it (a pid on POSIX), widened so the type is the same on every platform.
using ProcessId = std::int64_t;

/// The read end of a pipe from a child: its stdout, its stderr or its event channel (ProcessSpec::pipeOutput, eventPipeVariable). Move-only; closes
/// on destruction. One reader at a time: reads block the calling thread, so a pipe is read on a thread of its own or with a timeout.
class Pipe
{
public:
    Pipe() = default;
    /// For the implementation: wraps a native read end (a descriptor on POSIX, a HANDLE on Windows).
    explicit Pipe(std::intptr_t native) : native_(native) {}
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    Pipe(Pipe&& o) noexcept : native_(o.native_) { o.native_ = -1; }
    Pipe& operator=(Pipe&& o) noexcept;
    ~Pipe();

    [[nodiscard]] bool isOpen() const { return native_ != -1; }
    /// Read what is available, waiting up to `timeout` (none: as long as it takes). nullopt when nothing came in time; 0 when every writer has
    /// closed it (the child and whatever inherited it).
    [[nodiscard]] std::expected<std::optional<std::size_t>, PlatformError> readSome(char* buffer, std::size_t size,
                                                                                    std::optional<std::chrono::milliseconds> timeout = std::nullopt);

private:
    std::intptr_t native_ = -1;
};

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
    std::optional<std::vector<std::string>> environment{};   ///< "NAME=value" entries that REPLACE the caller's environment; none: inherited
    bool pipeOutput = false;                     ///< stdout and stderr each go to a pipe of their own (Child::takeStdout, takeStderr);
                                                 ///< outputFile must be empty
    std::string eventPipeVariable{};             ///< non-empty: one more pipe, whose write end the child inherits and finds named by this
                                                 ///< environment variable (writeToInherited writes to it; Child::takeEvents reads it)
    int memoryVictimScore = 0;                   ///< POSIX: the child's oom_score_adj (1 to 1000: killed before others under memory pressure;
                                                 ///< 0 leaves it as inherited). Windows: no such preference, ignored
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
    /// With the native handles a backend keeps (Windows: the process handle and, for a new session, the job object that holds its tree).
    Child(ProcessId id, bool leadsGroup, std::intptr_t handle, std::intptr_t group) : id_(id), leadsGroup_(leadsGroup), handle_(handle), group_(group) {}
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    Child(Child&& o) noexcept;
    Child& operator=(Child&& o) noexcept;
    ~Child();   ///< closes what it holds; never waits and never kills

    /// Its process id (0 for an empty handle).
    [[nodiscard]] ProcessId id() const { return id_; }
    /// Waits until it ends, or until `timeout` passes (then nullopt and it keeps running). No timeout: waits as long as it takes. A second call after
    /// it ended returns the same status.
    [[nodiscard]] std::optional<ExitStatus> wait(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    /// Asks it to stop (SIGTERM on POSIX); its whole group when it leads one. False when it is already gone.
    bool terminate();
    /// Ends it at once (SIGKILL on POSIX); its whole group when it leads one. False when it is already gone.
    bool kill();

    /// The read ends of its pipes (ProcessSpec::pipeOutput, eventPipeVariable), each handed out once; an unopened Pipe when there is none.
    [[nodiscard]] Pipe takeStdout() { return std::move(stdout_); }
    [[nodiscard]] Pipe takeStderr() { return std::move(stderr_); }
    [[nodiscard]] Pipe takeEvents() { return std::move(events_); }
    /// For the implementation: hands it its pipes.
    void attachPipes(Pipe out, Pipe err, Pipe events);

    /// Reap every ended child of this process without waiting (on POSIX a subreaper, becomeSubreaper, inherits the orphans of its tree, and they stay
    /// as zombies until reaped), keeping this child's status when it is among them. No effect on Windows, which keeps no zombies.
    void reapOrphans();

private:
    ProcessId id_ = 0;
    bool leadsGroup_ = false;
    std::optional<ExitStatus> status_;
    std::intptr_t handle_ = 0;   ///< native process handle (Windows), 0 when none
    std::intptr_t group_ = 0;    ///< native group handle (a Windows job object), 0 when none
    int endedBy_ = 0;            ///< the signal-like cause this side ended it with (15 terminate, 9 kill), where the system keeps none
    Pipe stdout_, stderr_, events_;
};

/// Write all of `data` to the write end of an event pipe this process inherited, named by `handleText` (the value of the variable
/// ProcessSpec::eventPipeVariable named). An error when the text names nothing open.
[[nodiscard]] std::expected<void, PlatformError> writeToInherited(std::string_view handleText, std::string_view data);

/// Starts a child. The error names the program and the system's reason (a missing program, a working directory that does not exist, an output file
/// that cannot be opened).
[[nodiscard]] std::expected<Child, PlatformError> spawn(const ProcessSpec& spec);

/// How runCaptured collects the output.
struct CaptureOptions
{
    std::optional<std::chrono::milliseconds> timeout{}; ///< killed when it runs longer (its group when spec.newSession); none: no limit
    std::size_t maxBytes = 512 * 1024;                  ///< at most this much is kept: the END of the output, where a failure is reported
    bool includeStderr = true;                          ///< stderr is captured with stdout, interleaved; false: stderr is inherited
    bool discardStderr = false;                         ///< with includeStderr false: stderr goes to the null device instead
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
