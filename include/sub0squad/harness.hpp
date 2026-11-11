#pragma once
/// @file harness.hpp
/// The harness registry (docs/DESIGN_AUDIT.md section 3.1): a project harness is described by one entry, so the workflow, the agents and the plan
/// validator ask the registry instead of comparing harness names. Entries are plain function pointers and data; a new harness is one new entry.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// What a project harness provides. Functions may be null where a harness has no such part.
struct Harness
{
    const char* name = "";                                   ///< the plan's `harness` value
    const char* gateCommand = "";                            ///< CLI command of the per-item gate job (`<exe> <gateCommand> --workflow-dir D --item I --attempt N`)
    const char* integrateCommand = "";                       ///< CLI command of the integrator job
    bool coordinators = false;                               ///< divided items are kept as integrating coordinators (the V)
    std::string (*brief)() = nullptr;                        ///< short description for the fixed system block
    std::string (*facts)() = nullptr;                        ///< what an expert needs to know about how the harness behaves
    std::size_t (*scaffoldBytes)() = nullptr;                ///< bytes of scaffold and stubs the harness supplies itself
    int (*gate)(const std::vector<std::string>& args) = nullptr;       ///< in-process gate (the agent's self-check); same arguments as the gate command
    std::string (*mockFile)(const std::string& path, bool buggy) = nullptr;  ///< mock deliverable (empty if unknown)
    std::string (*mockBugTarget)(const std::string& item) = nullptr;
};

/// The harness named `name`, or nullptr (an empty or unknown name; the empty name is the legacy header-only calculator sandbox, which has no entry).
[[nodiscard]] const Harness* findHarness(std::string_view name);
/// Names of all registered harnesses, for error messages and help.
[[nodiscard]] std::vector<std::string> harnessNames();

}  // namespace sub0squad
