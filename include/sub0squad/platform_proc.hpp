#pragma once
/// @file platform_proc.hpp
/// Process identity and the process tree (docs/PLATFORM.md, module platform_proc): this program's own path and id, whether a recorded process is
/// still the same one, its descendants and its command line. The POSIX backend reads /proc; the parsers are pure so they are tested on fixed text.

#include "sub0squad/platform_process.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad::platform {

/// The path of the running program's file; empty when it cannot be found. Never execute it from a test binary (catalogue row 32): a test
/// that links the library is not the `sub0squad` program.
[[nodiscard]] std::filesystem::path selfExecutable();

/// The id of the running process.
[[nodiscard]] ProcessId currentProcess();

/// An opaque start stamp of `id` (on Linux: clock ticks since boot, field 22 of /proc/PID/stat), or nullopt if no such process exists.
/// Together with the id it names one process even after the id is reused.
[[nodiscard]] std::optional<std::uint64_t> processStart(ProcessId id);

/// True if `id` exists, has not exited (a zombie counts as gone) and still has the start stamp `expectedStart`.
[[nodiscard]] bool isAlive(ProcessId id, std::uint64_t expectedStart);

/// Every live or zombie descendant of `root` (children, grandchildren, ...). A process that detached with setsid and a double fork is still a
/// descendant when `root` is a subreaper, because it was reparented to `root`.
[[nodiscard]] std::vector<ProcessId> descendants(ProcessId root);

/// Every process this user can see, live or zombie, in no particular order.
[[nodiscard]] std::vector<ProcessId> allProcesses();

/// The arguments `id` was started with, or nullopt when it does not exist or cannot be read.
[[nodiscard]] std::optional<std::vector<std::string>> commandLine(ProcessId id);

/// Ask `id` to stop. POSIX: SIGTERM. Windows: the stop event of a process that watches for stop requests (platform_wait), so it ends itself
/// in order; TerminateProcess when it has none. False when it could not be reached. Prefer `Child::terminate` for a process this program started.
[[nodiscard]] bool terminateProcess(ProcessId id);
/// End `id` at once (SIGKILL; TerminateProcess). False when it could not be reached.
[[nodiscard]] bool killProcess(ProcessId id);

/// Adopt the orphans of this process's tree (POSIX: PR_SET_CHILD_SUBREAPER, so a descendant that detaches stays a descendant and
/// `descendants` still finds it). Windows: no effect; a job object holds a tree there.
void becomeSubreaper();
/// A write to a pipe or socket whose reader is gone fails with an error instead of ending this process (POSIX: SIGPIPE ignored). Windows: always so.
void ignoreBrokenPipes();
/// Stop requests (terminateProcess, Ctrl+C) no longer end this process: for a test program that plays a process which will not stop.
void ignoreStopRequests();
/// The name the system shows for this process (POSIX: PR_SET_NAME, at most 15 bytes, what `ps -o comm` and /proc/PID/stat show). Windows: no effect.
void setProcessName(const std::string& name);
/// End this process as a crash does (POSIX: SIGSEGV; Windows: the access-violation status), for a test program that plays a crashing job.
[[noreturn]] void crash();

/// Whether `path` is a file this user may run (POSIX: execute permission; Windows: an existing file with an extension in PATHEXT).
[[nodiscard]] bool isExecutable(const std::filesystem::path& path);
/// The program `name` as the system would find it on PATH (Windows also tries each PATHEXT extension), or an empty path.
[[nodiscard]] std::filesystem::path findOnPath(const std::string& name);

/// Set or remove a variable in this process's environment (inherited by children started afterwards).
void setEnvironment(const std::string& name, const std::string& value);
void unsetEnvironment(const std::string& name);

/// The fields of /proc/PID/stat this module uses.
struct ProcStat
{
    ProcessId parent = 0;
    char state = '?';         ///< R, S, D, Z (zombie), X (dead), ...
    std::uint64_t start = 0;  ///< starttime, clock ticks since boot
};
/// Parse the text of /proc/PID/stat; the command name may hold spaces and parentheses, so fields are read after the LAST ')'.
[[nodiscard]] std::optional<ProcStat> parseProcStat(std::string_view text);
/// Split the NUL-separated text of /proc/PID/cmdline into arguments (a trailing NUL ends the last argument, it does not add an empty one).
[[nodiscard]] std::vector<std::string> parseCmdline(std::string_view text);

}  // namespace sub0squad::platform
