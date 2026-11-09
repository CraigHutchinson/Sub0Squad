#pragma once
/// @file supervisor.hpp
/// Starting a run's shim and reading what the shim recorded. Linux backend of IProcessSupervisor (docs/DESIGN.md section 20).

#include <sys/types.h>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace sub0squad {

/// Start `<exe> exec-shim <runDir>` as a new session (it outlives the caller). `exe` must be the multi-call `sub0squad` binary: the
/// daemon passes its own path, tests pass the built binary (never the test executable itself). The run directory must already hold
/// spec.json. Refuses to run from inside a shim (a recursion guard). Returns the shim's pid.
[[nodiscard]] std::expected<pid_t, std::string> spawnShim(const std::filesystem::path& exe, const std::filesystem::path& runDir);

/// Contents of `<runDir>/shim.json`.
struct ShimInfo
{
    pid_t pid = 0;
    std::uint64_t startTicks = 0;
};
[[nodiscard]] std::optional<ShimInfo> readShimInfo(const std::filesystem::path& runDir);

/// Parsed `<runDir>/exit.json` (`exit-1`), written atomically by the shim; absent while the run is alive or if the shim died.
struct ExitRecord
{
    std::string outcome;   ///< exited | cancelled | timeout | startFailed
    std::string teardown;  ///< verified | survivors
    int code = -1;         ///< exit status, -1 when the command was signalled
    int signal = 0;
    std::string raw;       ///< the file's JSON, stored in the journal
};
[[nodiscard]] std::optional<ExitRecord> readExitRecord(const std::filesystem::path& runDir);

}  // namespace sub0squad
