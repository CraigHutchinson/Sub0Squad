#pragma once
/// @file progress.hpp
/// Progress read from the event channel file (`sub0squad-event-1`), never from log text. Works with the daemon down.

#include "sub0squad/job_spec.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace sub0squad {

struct Progress
{
    bool hasProgress = false;
    std::int64_t done = 0, total = 0;
    std::string phase;
    std::string lastNote;
    std::map<std::string, double> metrics;  ///< latest value per metric name
    double lastEventAt = 0;                 ///< wall-clock UTC seconds (file mtime), 0 if no event yet
    std::int64_t eventLines = 0;
    std::string adapterStatus;              ///< status reported by the tool's own record (e.g. "stalled"), empty if no adapter
    std::string adapterSchema;
};

/// Scan the tail (up to 256 KiB) of `<runDir>/events.ndjson`.
[[nodiscard]] Progress readProgress(const std::filesystem::path& runDir);

/// Events first; when the job declares a `state-file` adapter, fill what the events did not provide from that file
/// (`optimization-run-1`: attempts over max_attempts, status, no_progress, recoveries). A relative adapter path is taken
/// relative to `cwd`.
[[nodiscard]] Progress readProgress(const std::filesystem::path& runDir, const ProgressAdapter& adapter, const std::string& cwd);

/// The adapter half alone (exposed for tests): parse an `optimization-run-1` state file's text.
[[nodiscard]] Progress progressFromOptimizationState(std::string_view stateJson);

}  // namespace sub0squad
