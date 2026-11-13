#pragma once
/// @file cmd_archive.hpp
/// `sub0squad archive [--label TEXT]` (private to the executable's command layer; catalogue row 20): set the finished runs aside and leave a fresh home, so the
/// next run's evidence is not mixed with the last one's.

#include "sub0squad/paths.hpp"

#include <string>
#include <vector>

namespace sub0squad {

/// `args` are the words after `archive`. Refused while a daemon answers on the control socket (its journal is open). Moves `<state>` to
/// `<state>/../archive/<UTC time>[-label]/state` (a rename: nothing is copied or deleted), creates an empty `<state>`, and copies the owner's registered
/// processes (`<state>/processes/`) into it, since they are configuration and not evidence of a run. Prints where the archive is.
[[nodiscard]] int runArchiveCommand(const std::vector<std::string>& args, const RuntimePaths& paths);

}  // namespace sub0squad
