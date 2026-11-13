#pragma once
/// @file cmd_process.hpp
/// `sub0squad process list | show | add` (private to the executable's command layer): the first command split out of client.cpp, which is one function per
/// command family on its way to being one unit per family (docs/COMPONENTS.md, cleanup step 4).

#include <filesystem>
#include <string>
#include <vector>

namespace sub0squad {

/// `args` are the words after `process`; `stateDir` is the runtime state directory (the user's processes live in `<stateDir>/processes/`).
[[nodiscard]] int runProcessCommand(const std::vector<std::string>& args, const std::filesystem::path& stateDir);

}  // namespace sub0squad
