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
    const char* gateKind = "node-test";                      ///< the plan's declarative gate `kind` this harness judges
    const char* testSuffix = ".test.mjs";                    ///< a test file for `src/x.<ext>` is `tests/x<testSuffix>`
    const char* deliverableStyle = "plain ES module, no dependencies";  ///< what a developer's card says about the shape of the code
    std::string (*blueprint)(const std::string& name) = nullptr;       ///< a hand-written plan-2 for a known task (empty if unknown)
    bool (*claimsTask)(const std::string& lowercaseTask) = nullptr;     ///< whether the task text names this harness's kind of project
    /// The cheapest check alone (a syntax or compile check that names file and line), over candidate files that are NOT yet the attempt's output: the commit
    /// gate of a canvas transaction (docs/CANVAS.md). Arguments: `--workflow-dir D --item I --attempt N --files-dir DIR`. Returns "" when the files pass.
    std::string (*precheck)(const std::vector<std::string>& args) = nullptr;
    /// Work is divided by SECTIONS of a file under a skeleton (the fork executor), never into new modules: in a compiled language a new module needs a
    /// header that declares its interface and no item of a division owns one (docs/INTERVENTIONS.md row 45, docs/CANVAS.md "Division on compiled platforms").
    bool divideBySections = false;
};

/// The harness named `name`, or nullptr (an empty or unknown name; the empty name is the legacy header-only calculator sandbox, which has no entry).
[[nodiscard]] const Harness* findHarness(std::string_view name);
/// Whether any registered harness judges gates of this kind.
[[nodiscard]] bool knownGateKind(std::string_view kind);
/// The first harness whose blueprint knows `name`, or nullptr.
[[nodiscard]] const Harness* harnessWithBlueprint(const std::string& name);
/// Names of all registered harnesses, for error messages and help.
[[nodiscard]] std::vector<std::string> harnessNames();

}  // namespace sub0squad
