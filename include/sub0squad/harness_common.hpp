#pragma once
/// @file harness_common.hpp
/// What every project harness needs and none should copy: argument parsing, file helpers, running a command with a timeout, reading a TAP report
/// into counts and a failure excerpt, copying trees and walking a plan's dependency closure.

#include "sub0squad/plan.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sub0squad {

/// One file the harness provides itself.
struct HarnessFile { std::string path; std::string content; };

}  // namespace sub0squad

namespace sub0squad::harnessutil {

using Args = std::map<std::string, std::vector<std::string>>;

/// `--key value` pairs of a job command line (a key may repeat).
[[nodiscard]] Args parseArgs(const std::vector<std::string>& args);
/// The last value of `k`, or `d`.
[[nodiscard]] std::string one(const Args& m, const std::string& k, const std::string& d = {});
[[nodiscard]] std::string slurp(const std::filesystem::path& p);
/// Write `text` to `p`, creating parent directories; false on failure.
bool writeFile(const std::filesystem::path& p, const std::string& text);

/// Result of running a command: exit code (-1 if it did not exit), whether the timeout killed it, and its merged stdout and stderr (tail-limited).
struct Cmd { int exitCode = -1; bool timedOut = false; std::string output; };
/// Run `argv` in `cwd` with a wall-clock timeout.
[[nodiscard]] Cmd run(const std::vector<std::string>& argv, const std::filesystem::path& cwd, int timeoutSec);

/// A TAP report in brief: counts (-1 tests when none were found) and an excerpt of the first failures with expected and actual values.
struct Tap { int tests = -1, pass = 0, fail = 0; std::string excerpt; std::string note; /* extra runner output shown when too few tests ran */ };
[[nodiscard]] Tap parseTap(const std::string& out);

/// Recursive copy that overwrites; a missing source is not an error.
void copyTree(const std::filesystem::path& from, const std::filesystem::path& to);
/// Ids of every item `id` depends on, directly or not.
[[nodiscard]] std::set<std::string> closure(const Plan& plan, const std::string& id);


/// Whether a gate failure is about the INTERFACE between modules (a name a test or caller needs that the module does not provide) rather than a wrong result:
/// a missing export, import, member or symbol. Such a failure cannot be fixed by retrying the one file; the contract that should name it is the next scope up.
[[nodiscard]] bool interfaceGap(const std::string& gateText);
/// The names the given test files import from the project, one line per test file ("tests/a.test.mjs imports { x, y } from ../src/f.js"), for JavaScript
/// and Python tests; empty when nothing is recognised. Handed to the developer so the names to provide are exact, not guessed.
[[nodiscard]] std::string importedNames(const std::filesystem::path& ws, const std::vector<std::string>& testFiles);

/// What a gate hook may need to know about the attempt being judged.
struct GateContext
{
    const WorkItem* item = nullptr;
    std::filesystem::path dir, attemptDir, out, ws;
    int attempt = 1;
};

/// The parts of a gate that differ between toolchains; everything else (allowlist, workspace, judging, publishing) is shared by `runGateFlow`.
struct GateHooks
{
    const std::vector<HarnessFile>& (*scaffold)() = nullptr;
    /// The cheapest check of the files the attempt wrote (a syntax or compile check that names file and line), run over the workspace; "" when fine.
    std::string (*precheck)(const std::filesystem::path& out, const std::filesystem::path& ws) = nullptr;
    /// Build what the gate's test files need and run them, filling `tap`; returns a failure text (a build error, a crash) or "". Sets `timedOut`.
    std::string (*buildAndRun)(const std::filesystem::path& ws, const std::vector<std::string>& tests, Tap& tap, bool& timedOut) = nullptr;
    /// Optional extra judgement of a tester's tests that already fail against the stubs (the test oracle); returns a failure text or "".
    std::string (*afterTesterJudged)(const GateContext& ctx, const Tap& tap) = nullptr;
    /// The same check for a file that is still being written in sections (`--partial 1`): a skeleton's stubs leave helpers uncalled and parameters unused,
    /// which is not an error until the file is whole (row 46). Null: the ordinary precheck serves.
    std::string (*precheckPartial)(const std::filesystem::path& out, const std::filesystem::path& ws) = nullptr;
};

/// The precheck alone, over candidate files in `--files-dir` (default: the attempt's `out`): a scratch workspace (`attempt-N/ws-pre`: scaffold, the accepted
/// outputs of the item's dependencies, the candidate files) is built and `hooks.precheck` run over it. Returns "" when the files pass, else the located
/// diagnostic. Nothing is judged, published or written to `gate.txt`: it is the commit gate of a canvas transaction, cheap enough to run on every edit.
[[nodiscard]] std::string runPrecheckFlow(const GateHooks& hooks, const std::vector<std::string>& args);

/// The common gate: allowlist, workspace (scaffold, dependencies' accepted outputs, the attempt), precheck, build and run, then judge (minimum tests,
/// tests must fail against stubs for testers, near pass exit 3, same-failures exit 5) and publish. `args` are those of the gate command.
[[nodiscard]] int runGateFlow(const GateHooks& hooks, const std::vector<std::string>& args);

}  // namespace sub0squad::harnessutil
