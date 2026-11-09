#pragma once
/// @file process_tree.hpp
/// Linux process identity and tree enumeration (the `/proc` half of IProcessSupervisor, docs/DESIGN.md section 20).

#include <sys/types.h>
#include <cstdint>
#include <optional>
#include <vector>

namespace sub0squad::proc {

/// Start time of `pid` in clock ticks since boot (field 22 of /proc/<pid>/stat), or nullopt if the process does not exist.
/// Together with the pid it identifies a process even after pid reuse.
[[nodiscard]] std::optional<std::uint64_t> startTicks(pid_t pid);

/// True if `pid` exists, is not a zombie, and still has the given start time.
[[nodiscard]] bool isAlive(pid_t pid, std::uint64_t expectedStartTicks);

/// Every live or zombie descendant of `root` (children, grandchildren, ...), found by scanning /proc. A process that detached with
/// setsid and double fork is still a descendant when `root` is a subreaper, because it was reparented to `root`.
[[nodiscard]] std::vector<pid_t> descendantsOf(pid_t root);

}  // namespace sub0squad::proc
