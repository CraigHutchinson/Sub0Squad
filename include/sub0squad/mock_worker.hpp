#pragma once
/// @file mock_worker.hpp
/// `sub0squad mock-worker`: a scripted stand-in for real work (docs/DESIGN.md section 13). It emits `sub0squad-event-1` progress on
/// the inherited event channel and can be told to misbehave in the ways the gates need: fail, crash, stall, ignore SIGTERM,
/// leave a server child behind, or escape its process group with setsid and a double fork.
///
/// Options: --steps N --step-ms T --fail-at K --crash-at K --stall-at K --ignore-term --server --escape --tag NAME
///          --mem-mib M --state-file PATH --metric NAME=VALUE

namespace sub0squad {

/// Entry point; `argv[0]` is the first option. Returns the exit code.
[[nodiscard]] int runMockWorker(int argc, char** argv);

}  // namespace sub0squad
