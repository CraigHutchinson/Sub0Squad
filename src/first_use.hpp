#pragma once
/// @file first_use.hpp
/// The first-use path (README "First use"): `up` starts the daemon and the portal in the background, `down` stops them, `doctor` checks what a first
/// run needs and says what to fix. Part of the sub0squad executable only (never the core library): `up` starts other processes of this binary, so the
/// binary's path is passed in by main and verified, and a nested `up` is refused (docs/INTERVENTIONS.md row 32).

#include <string>
#include <vector>

namespace sub0squad {

/// The path of this program from `argv[0]` (made absolute, or found on PATH); empty when it cannot be found. Never /proc/self/exe.
[[nodiscard]] std::string resolveProgram(const char* argv0);
/// `up [--max-jobs N] [--frontier off|stub] [--portal-port N] [--no-portal]`: start the daemon and the portal if they are not running; print what to do next.
[[nodiscard]] int runUp(const std::vector<std::string>& args, const std::string& exe);
/// `down`: stop the daemon and the portal that `up` started.
[[nodiscard]] int runDown(const std::vector<std::string>& args);
/// `doctor [--engine URL]`: the checks of a first run, one line each (`ok`, `note` or `FIX`); exit 1 when something must be fixed.
[[nodiscard]] int runDoctor(const std::vector<std::string>& args, const std::string& exe, const std::string& version);

}  // namespace sub0squad
