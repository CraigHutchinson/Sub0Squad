#pragma once
/// @file process.hpp
/// A process is a workflow as DATA (docs/PROCESS_DESIGN.md): the parameters of the engine's three stages (plan, build, integrate) that used to be
/// constants and switches in `advance()`, so a new way of working is a file, not a fork of the engine. The engine is one function of
/// (workflow, jobs, process); this header is the process.
///
/// What a process can change, and nothing else (a knob that has one legal value is not a knob):
///   planner    how long a planner job may run and how many times it is retried;
///   leaf       the EXECUTOR of a developer's item and of a tester's item (session | canvas | fork: docs/CANVAS.md), which rungs of the escalation ladder
///              exist (divide into modules, write by sections, send the tests back, rewrite the card, escalate to the frontier tier), and the wall time of
///              each kind of job;
///   ascent     how many times a coordinator may send a failed integration down to a part;
///   integrate  how long the final integration may run.
/// The ladder's ORDER and the engine's rules (a failed branch stops only itself, progress is never discarded) are not parameters: they are the engine.
///
/// A process is registered by name: built in (`standardProcess`, `sectionsProcess`, each with a file under `registry/processes/` that a test holds equal to
/// the code) or a file `NAME.json` in the user's processes directory (`<state>/processes/`). A workflow is submitted with the resolved process INSIDE its
/// spec, so the journal says what ran and a later edit of a file changes nothing already running.

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

struct Process
{
    std::string name = "standard";
    int version = 1;
    std::string description;

    struct Planner
    {
        int wallSeconds = 60;
        int retries = 1;                       ///< a planner job that failed is run again this many times before the workflow fails
        bool operator==(const Planner&) const = default;
    } planner;

    struct Leaf
    {
        std::string developerExecutor = "session";   ///< session | canvas | fork
        std::string testerExecutor = "session";      ///< session | canvas (a test file has no sections to fork)
        bool allowDecompose = true;                  ///< a failing developer item may be divided into modules by the architect
        bool allowSections = true;                   ///< where the platform divides by sections, the next attempt may be written as forked sections
        bool allowReviseTests = true;                ///< a failure that points at the tests sends them back to the tester
        bool allowRearchitect = true;                ///< a dedicated job may rewrite the card
        bool allowEscalate = true;                   ///< the frontier tier may be asked (still subject to the workflow's frontier mode)
        int agentWallLlmSeconds = 10800;             ///< a local model-driven attempt
        int agentWallOtherSeconds = 120;             ///< a mock or stub attempt
        int gateWallSeconds = 120;
        int architectWallLlmSeconds = 5400;
        int architectWallOtherSeconds = 120;
        bool operator==(const Leaf&) const = default;
    } leaf;

    struct Ascent
    {
        int maxRoundTrips = 3;                 ///< integration failures a coordinator sends down before its item fails
        bool operator==(const Ascent&) const = default;
    } ascent;

    struct Integrate
    {
        int wallSeconds = 300;
        bool operator==(const Integrate&) const = default;
    } integrate;

    bool operator==(const Process&) const = default;

    /// "" when the process is usable, else the first problem (a name, an executor, a limit out of range).
    [[nodiscard]] std::string validate() const;
    /// `process-1` JSON. Every parameter is written, so a file says what runs.
    [[nodiscard]] std::string toJson() const;
    /// Parse and validate. An unknown key is an error that names it (a typo in a user's file must not silently mean the default).
    [[nodiscard]] static std::expected<Process, std::string> fromJson(std::string_view text);
    /// Set one parameter by its dotted path (`leaf.executor.developer=fork`, `leaf.allow.decompose=false`, `ascent.maxRoundTrips=5`); the value is checked
    /// against the parameter's type and then the whole process is validated. Used by `process add --set` and by tests.
    [[nodiscard]] std::expected<void, std::string> set(std::string_view path, std::string_view value);
    /// The dotted paths of every parameter, in file order.
    [[nodiscard]] static std::vector<std::string> parameterPaths();
};

/// Today's behaviour: the values the engine used before processes were data. The goldens (tests/data/actions) are the proof.
[[nodiscard]] const Process& standardProcess();
/// Developers' items are written as forked sections from their first attempt, testers' on the canvas, and a failing item is never divided into modules:
/// the process for compiled platforms and for work that is long rather than wide.
[[nodiscard]] const Process& sectionsProcess();
/// Every built-in process, in registry order.
[[nodiscard]] const std::vector<const Process*>& builtinProcesses();

/// The process called `name`: a built-in, else `<userDir>/<name>.json`. `userDir` may be empty or missing (only built-ins are then found).
[[nodiscard]] std::expected<Process, std::string> findProcess(std::string_view name, const std::filesystem::path& userDir);
/// Names of every process that `findProcess` finds, built-ins first then the user's files, sorted within each.
[[nodiscard]] std::vector<std::string> processNames(const std::filesystem::path& userDir);

}  // namespace sub0squad
