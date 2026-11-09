#pragma once
/// @file shim.hpp
/// `sub0squad exec-shim <runDir>`: the per-run supervisor. It is a session leader and a child subreaper, runs the job's command in
/// its own process group, captures stdout, stderr and the event channel into files, enforces the wall budget, and on cancel,
/// timeout or exit tears down the WHOLE tree and verifies nothing is left. It records the outcome in `<runDir>/exit.json`
/// (written atomically), so the outcome survives a daemon restart and is written by the process that actually saw it.
///
/// Files in the run directory: spec.json (input), shim.json, stdout.log, stderr.log, events.ndjson, exit.json (outputs).

#include <string>

namespace sub0squad {

/// Entry point of the shim subcommand. Returns the process exit code (0 when an outcome was recorded, whatever it was).
[[nodiscard]] int runShim(const std::string& runDir);

}  // namespace sub0squad
