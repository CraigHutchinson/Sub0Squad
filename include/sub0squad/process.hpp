#pragma once
/// @file process.hpp
/// A process is a workflow as DATA (docs/PROCESS_DESIGN.md, section 13): an ordered list of STAGES. The engine (`advance()`) has no stage of its own: every
/// pass it walks the process's stages, derives each one's state from the journal (never stores it), and does what the stage's KIND says. There are two kinds,
/// because there are two things the engine can do:
///
///   job     run ONE item-less job (a registered tool with arguments) and retry it; optionally accept its output as the workflow's plan. The planner and the
///           final integration of the standard process are both this kind; so is anything a new workflow adds before, between or after them.
///   items   run the plan's items: each item's ROLE picks its executor (a coordinator ascends: integration rounds and send-downs; anything else is a leaf:
///           attempt, gate, escalation ladder), and the stage holds the parameters of both.
///
/// When a stage is entered and when it is done are predicates in a closed language (predicate.hpp) over journal facts, checked when the process is loaded. A
/// stage may read only the stages before it, so a process cannot describe a cycle. The workflow succeeds when every stage is done.
///
/// So a new way of working is a FILE: another stage (a review after integration), a stage removed (no integration: the items are the result), a different
/// order or condition, a different executor or ladder. Nothing in the engine names a stage, a step or a tool of the standard process.
///
/// A process is registered by name: built in (each with a file under `registry/processes/` that a test holds equal to the code) or a file `NAME.json` in the
/// user's processes directory (`<state>/processes/`). A workflow is submitted with the resolved process INSIDE its spec, so the journal says what ran and a
/// later edit of a file changes nothing already running.

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// What a `job` stage runs.
struct JobParams
{
    std::string step;                      ///< the job's role in the journal, the ledger and the portal (`planner`, `integrator`, ...); unique in the process
    std::string label;                     ///< how the step is named in the workflow's closing note ("integration failed: ...")
    /// A registered stage tool (`stageTools()`): a sub-command of this program, or `harness:integrate` (the integration command of the plan's harness).
    std::string tool;
    /// The tool's arguments. `${task}`, `${sandbox}` and `${workflow}` are replaced by the workflow's task name, sandbox directory and id; nothing else is.
    std::vector<std::string> args;
    std::string tier = "stub";             ///< the job's tier label
    int wallSeconds = 120;
    int retries = 0;                       ///< a failed job is run again this many times before the workflow fails
    std::string accept;                    ///< "" or "plan": on success the daemon loads `<sandbox>/plan.json` as the workflow's plan
    bool operator==(const JobParams&) const = default;
};

/// How an `items` stage runs an item that has an agent (a developer's, a tester's).
struct LeafParams
{
    std::string developerExecutor = "session";   ///< session | canvas | fork (docs/CANVAS.md)
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
    bool operator==(const LeafParams&) const = default;
};

/// How an `items` stage runs a coordinator (the V's ascent).
struct AscentParams
{
    int maxRoundTrips = 3;                 ///< integration failures a coordinator sends down before its item fails
    bool operator==(const AscentParams&) const = default;
};

enum class StageKind { job, items };

struct Stage
{
    std::string name;                      ///< unique in the process; what `stage.NAME.done` reads
    StageKind kind = StageKind::job;
    std::string entry = "always";          ///< predicate: the stage does nothing until this holds
    std::string exit;                      ///< predicate: the stage is done when it is entered and this holds
    JobParams job;                         ///< kind == job
    LeafParams leaf;                       ///< kind == items
    AscentParams ascent;                   ///< kind == items
    bool operator==(const Stage&) const = default;
};

struct Process
{
    std::string name;
    int version = 1;
    std::string description;
    std::vector<Stage> stages;

    bool operator==(const Process&) const = default;

    /// The stage called `name`, or nullptr.
    [[nodiscard]] const Stage* stage(std::string_view name) const;
    /// The `items` stage, or nullptr (a process has at most one: a plan's items do not yet say which stage they belong to).
    [[nodiscard]] const Stage* itemsStage() const;
    /// True when a stage accepts its job's output as the plan: without one, a workflow whose task has no blueprint waits for a plan to be supplied.
    [[nodiscard]] bool producesPlan() const;

    /// "" when the process is usable, else the first problem, naming the stage and the field (a name, a tool, a predicate with its column, a limit).
    [[nodiscard]] std::string validate() const;
    /// `process-1` JSON. Every parameter of every stage is written, so a file says what runs.
    [[nodiscard]] std::string toJson() const;
    /// Parse and validate. An unknown key is an error that names it (a typo in a user's file must not silently mean the default).
    [[nodiscard]] static std::expected<Process, std::string> fromJson(std::string_view text);
    /// Set one parameter by its path, `STAGE.PARAMETER` (`build.leaf.executor.developer=fork`, `plan.job.retries=0`, `integrate.entry=stage.build.done`),
    /// or `description`. The value is checked against the parameter's type and then the whole process is validated: all or nothing.
    [[nodiscard]] std::expected<void, std::string> set(std::string_view path, std::string_view value);
    /// Remove the stage called `name`; refused (and nothing changes) when a later stage reads it.
    [[nodiscard]] std::expected<void, std::string> removeStage(std::string_view name);
    /// The path of every settable parameter, in file order.
    [[nodiscard]] std::vector<std::string> parameterPaths() const;
};

/// One registered stage tool.
struct StageTool
{
    const char* name;          ///< what a job stage's `tool` says
    const char* what;          ///< one line for `process tools`
    bool needsPlan;            ///< the tool is resolved from the plan (its harness), so the stage cannot run before a plan exists
};
/// Every tool a job stage may run. Closed: a process file cannot name an arbitrary program.
[[nodiscard]] const std::vector<StageTool>& stageTools();

/// Plan, build every item under the escalation ladder, integrate. What the engine did before processes were data; the goldens (tests/data/actions) prove it.
[[nodiscard]] const Process& standardProcess();
/// The standard stages, with developers' items written as forked sections from their first attempt, testers' on the canvas, and no division into modules:
/// for compiled platforms and for work that is long rather than wide.
[[nodiscard]] const Process& sectionsProcess();
/// The standard stages and one more: after integration a `review` stage runs the quality scan over the integrated project. The smallest process that has a
/// stage the engine never had.
[[nodiscard]] const Process& reviewedProcess();
/// Every built-in process, in registry order.
[[nodiscard]] const std::vector<const Process*>& builtinProcesses();

/// The process called `name`: a built-in, else `<userDir>/<name>.json`. `userDir` may be empty or missing (only built-ins are then found).
[[nodiscard]] std::expected<Process, std::string> findProcess(std::string_view name, const std::filesystem::path& userDir);
/// Names of every process that `findProcess` finds, built-ins first then the user's files, sorted within each.
[[nodiscard]] std::vector<std::string> processNames(const std::filesystem::path& userDir);

}  // namespace sub0squad
