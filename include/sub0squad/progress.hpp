#pragma once
/// @file progress.hpp
/// Progress read from the event channel file (`sub0squad-event-1`), never from log text. Works with the daemon down.

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
};

/// Scan the tail (up to 256 KiB) of `<runDir>/events.ndjson`.
[[nodiscard]] Progress readProgress(const std::filesystem::path& runDir);

}  // namespace sub0squad
