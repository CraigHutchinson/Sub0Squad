#pragma once
/// @file build_id.hpp
/// The identity of this build of the program (catalogue row 20). A daemon keeps running the binary it was started from, so after a rebuild the command line
/// and the daemon can be two different programs: a run then mixes the behaviour of both and its evidence describes neither. The daemon reports its build id
/// (`ping`, `run-info.json`) and refuses a submit from a command line whose id differs.

#include <string>

namespace sub0squad {

/// Set once by `main` from the generated version (semantic version and commit). Not set: the id is "unknown" (a test program linked without a version).
void setBuildId(std::string id);
/// This program's build id: the version, `+`, and a fingerprint of the program file's bytes (the version is generated when the build is configured, so two
/// links of one checkout share it; the fingerprint tells them apart). Computed once, on first use. `SUB0SQUAD_BUILD_ID` in the environment replaces it: for tests only, so a mismatch can be produced with one binary.
[[nodiscard]] std::string buildId();

}  // namespace sub0squad
