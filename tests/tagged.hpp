#pragma once
// The mock worker's processes, found by their command line on every system: the worker (`mock-worker --tag TAG ...`), its server
// (`mock-worker --idle s0q-srv-TAG`) and its escapee (`mock-worker --idle s0q-esc-TAG`). The tag must match exactly, so "k1" never finds "k10".
// A zombie has no command line any more, so only live processes are found.
#include "sub0squad/platform_proc.hpp"

#include <string>
#include <vector>

namespace tagged {

inline bool isTagged(const std::vector<std::string>& argv, const std::string& tag)
{
    if (argv.size() < 3 || argv[1] != "mock-worker") return false;
    for (std::size_t i = 2; i < argv.size(); ++i) {
        if (argv[i] == "--tag" && i + 1 < argv.size() && argv[i + 1] == tag) return true;
        if (argv[i] == "--idle" && i + 1 < argv.size() && (argv[i + 1] == "s0q-srv-" + tag || argv[i + 1] == "s0q-esc-" + tag)) return true;
    }
    return false;
}

/// Live processes of the mock worker run tagged `tag`.
inline std::vector<sub0squad::platform::ProcessId> processes(const std::string& tag)
{
    std::vector<sub0squad::platform::ProcessId> out;
    for (const auto id : sub0squad::platform::allProcesses())
        if (auto argv = sub0squad::platform::commandLine(id); argv && isTagged(*argv, tag)) out.push_back(id);
    return out;
}

inline int count(const std::string& tag) { return static_cast<int>(processes(tag).size()); }
inline void killAll(const std::string& tag) { for (const auto id : processes(tag)) (void)sub0squad::platform::killProcess(id); }

}  // namespace tagged
