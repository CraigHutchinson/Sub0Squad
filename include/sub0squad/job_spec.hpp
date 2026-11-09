#pragma once
/// @file job_spec.hpp
/// `job-spec-1`: everything needed to run a process job. Plain data, parsed and written as JSON.

#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// Optional adapter that derives progress events from a file the tool already writes (the optimisation harness's state.json).
struct ProgressAdapter
{
    std::string type;    ///< "state-file" or empty for none
    std::string path;    ///< file to read (absolute, or relative to the job's cwd)
    std::string schema;  ///< e.g. "optimization-run-1"
};

/// A process job. The environment is allow-listed: only PATH, HOME, LANG plus `env` reach the child.
struct JobSpec
{
    std::vector<std::string> argv;           ///< command and arguments (required, non-empty)
    std::string cwd;                         ///< working directory (empty: the submitter's)
    std::map<std::string, std::string> env;  ///< extra environment
    std::string tool = "process";            ///< manifest name (M3); free text in M1
    std::string tier = "local";              ///< "stub" or "local" in M1; never a frontier tier
    std::string owner;                       ///< submitting user or agent identity
    std::string dataClass = "private";       ///< what may leave the machine (enforced from M3b)
    std::int64_t wallSeconds = 0;            ///< 0 = unlimited; enforced by the shim
    std::int64_t graceSeconds = 5;           ///< SIGTERM to SIGKILL grace on cancel or timeout
    std::int64_t expectedEventIntervalSec = 0;  ///< work-liveness hint (0 = none)
    std::vector<std::string> resumeArgv;     ///< command used by `resume` (empty: not resumable)
    ProgressAdapter adapter;                 ///< optional progress adapter

    /// Serialise as a `job-spec-1` JSON object.
    [[nodiscard]] std::string toJson() const;
    /// Parse and validate. The error text names the first problem.
    [[nodiscard]] static std::expected<JobSpec, std::string> fromJson(std::string_view text);
};

}  // namespace sub0squad
