#include "sub0squad/harness_common.hpp"

#include "sub0squad/attempt_failure.hpp"

#include "sub0squad/contract.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/platform_process.hpp"
#include "sub0squad/platform_file.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

namespace sub0squad {

std::vector<ContractFunction> contractOf(const Harness* harness, const Plan* plan, const std::string& path)
{
    // The scaffold the workspace really starts from: the harness's own files, overlaid by the files the plan supplies (the plan's interface wins).
    std::map<std::string, std::string> files;
    if (harness && harness->scaffoldPaths && harness->scaffoldFile)
        for (const auto& p : harness->scaffoldPaths()) files[p] = harness->scaffoldFile(p);
    if (plan) for (const auto& f : plan->scaffold) files[f.path] = f.content;
    const auto stubIt = files.find(path);
    if (stubIt == files.end() || stubIt->second.empty()) return {};
    std::vector<std::string> headers;
    for (const auto& [p, content] : files) {
        const auto dot = p.rfind('.');
        const std::string ext = dot == std::string::npos ? std::string{} : p.substr(dot);
        if ((ext == ".hpp" || ext == ".h") && p.rfind("tests/", 0) != 0) headers.push_back(content);
    }
    return contractFunctions(path, stubIt->second, headers);
}

void writeScaffold(const Plan& plan, const std::vector<HarnessFile>& harnessFiles, const std::filesystem::path& ws)
{
    // A plan that carries its own scaffold owns the project's interface: of the harness's files only the instruments stay (the build file and the test
    // helpers); its calculator-shaped header and stubs would clash with the plan's own. Only the C++ harness separates the two today.
    const bool selfScaffolded = !plan.scaffold.empty() && plan.harness == "cpp-cmake";
    for (const auto& f : harnessFiles) {
        if (selfScaffolded && f.path != "CMakeLists.txt" && f.path.rfind("tests/helpers/", 0) != 0) continue;
        std::error_code ec;
        std::filesystem::create_directories((ws / f.path).parent_path(), ec);
        std::ofstream(ws / f.path, std::ios::binary | std::ios::trunc) << f.content;
    }
    for (const auto& f : plan.scaffold) {
        std::error_code ec;
        std::filesystem::create_directories((ws / f.path).parent_path(), ec);
        std::ofstream(ws / f.path, std::ios::binary | std::ios::trunc) << f.content;
    }
}

}  // namespace sub0squad

namespace sub0squad::harnessutil {

namespace fs = std::filesystem;

Args parseArgs(const std::vector<std::string>& args)
{
    std::map<std::string, std::vector<std::string>> m;
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i].rfind("--", 0) == 0) { m[args[i].substr(2)].push_back(args[i + 1]); ++i; }
    return m;
}
std::string one(const Args& m, const std::string& k, const std::string& d)
{
    auto it = m.find(k);
    return it == m.end() || it->second.empty() ? d : it->second.back();
}
std::string slurp(const fs::path& p) { return platform::readFileText(p); }
bool writeFile(const fs::path& p, const std::string& text) { return platform::writeFile(p, text).has_value(); }


Cmd run(const std::vector<std::string>& argv, const fs::path& cwd, int timeoutSec)
{
    Cmd r;
    const auto ran = platform::runCaptured({.argv = argv, .cwd = cwd}, {.timeout = std::chrono::seconds(timeoutSec), .maxBytes = 512 * 1024});
    if (!ran) { r.exitCode = 127; r.output = ran.error().message + "\n"; return r; }   // said, so a gate shows "cannot start 'g++'" rather than nothing
    r.timedOut = ran->timedOut;
    r.exitCode = ran->status.exited() ? ran->status.code : -1;
    r.output = ran->output;
    return r;
}


Tap parseTap(const std::string& out)
{
    // Counts come from the summary lines. The excerpt lists LEAF failures (nested subtests too) with their first error lines; a parent whose
    // only message is "N subtests failed" adds nothing for a model that has to fix the code.
    Tap t;
    struct Failure { std::string name, error, expected, actual, where; };
    std::vector<Failure> failures;
    std::istringstream in(out);
    std::string line;
    bool inError = false;
    int errLines = 0;
    auto trimmed = [](const std::string& l) { const auto p = l.find_first_not_of(' '); return p == std::string::npos ? std::string{} : l.substr(p); };
    while (std::getline(in, line)) {
        if (line.rfind("# tests ", 0) == 0) { t.tests = std::atoi(line.c_str() + 8); continue; }
        if (line.rfind("# pass ", 0) == 0) { t.pass = std::atoi(line.c_str() + 7); continue; }
        if (line.rfind("# fail ", 0) == 0) { t.fail = std::atoi(line.c_str() + 7); continue; }
        const std::string tl = trimmed(line);
        if (tl.rfind("ok ", 0) == 0 || tl == "...") { inError = false; continue; }  // a passing test, or the end of a diagnostic block, ends the error message
        if (tl.rfind("not ok ", 0) == 0) { failures.push_back({tl.substr(7), "", "", "", ""}); inError = false; errLines = 0; continue; }
        if (failures.empty()) continue;
        // The runner's own observed values: what the test wanted, what the code gave, and the line of the assertion.
        if (tl.rfind("expected: ", 0) == 0 && failures.back().expected.empty()) failures.back().expected = tl.substr(10, 120);
        else if (tl.rfind("actual: ", 0) == 0 && failures.back().actual.empty()) failures.back().actual = tl.substr(8, 120);
        else if (tl.rfind("location: ", 0) == 0 && failures.back().where.empty()) {
            std::string w = tl.substr(10); w.erase(std::remove(w.begin(), w.end(), '\''), w.end());
            const auto slash = w.rfind('/'); failures.back().where = slash == std::string::npos ? w : w.substr(slash + 1);
        }
        if (tl.rfind("error: ", 0) == 0) {
            inError = true; errLines = 0;
            std::string e = tl.substr(7);
            if (e == "|-" || e == "|") e.clear();
            failures.back().error = e;
        } else if (inError && !tl.empty() && errLines < 4 && tl.rfind("code:", 0) != 0 && tl.rfind("name:", 0) != 0 && tl.rfind("stack:", 0) != 0 && tl.rfind("location:", 0) != 0 && tl.rfind("expected:", 0) != 0 && tl.rfind("actual:", 0) != 0 && tl.rfind("operator:", 0) != 0 && tl.rfind("duration_ms", 0) != 0 && tl.rfind("type:", 0) != 0 && tl.rfind("failureType", 0) != 0 && tl.rfind("...", 0) != 0 && tl.rfind("---", 0) != 0) {
            failures.back().error += (failures.back().error.empty() ? "" : " | ") + tl;
            ++errLines;
        } else if (tl.rfind("code:", 0) == 0 || tl.rfind("stack:", 0) == 0 || tl.rfind("name:", 0) == 0) inError = false;
    }
    int shown = 0;
    for (const auto& f : failures) {
        if (f.error.find("subtest") != std::string::npos && f.error.find("failed") != std::string::npos) continue;  // a parent summary
        if (shown++ >= 6 || t.excerpt.size() > 1300) break;
        t.excerpt += "FAIL " + f.name + (f.error.empty() ? "" : ": " + f.error.substr(0, 300)) + "\n";
        if (!f.expected.empty() || !f.actual.empty())
            t.excerpt += "  the test expected " + f.expected + " but the code gave " + f.actual + (f.where.empty() ? "" : " (" + f.where + ")") + "\n";
    }
    if (t.excerpt.empty() && !failures.empty()) t.excerpt = "FAIL " + failures.front().name + "\n";
    return t;
}

void copyTree(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    if (!fs::exists(from)) return;
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
}

std::string builtBy(const fs::path& dir, const std::string& itemId)
{
    const std::string accepted = slurp(dir / "items" / itemId / "accepted.json");
    if (accepted.empty()) return {};
    const auto at = accepted.find("\"attempt\":");
    const int attempt = at == std::string::npos ? 0 : std::atoi(accepted.c_str() + at + 10);
    const std::string agent = slurp(dir / "items" / itemId / ("attempt-" + std::to_string(attempt)) / "agent.json");
    if (agent.empty()) return fs::exists(dir / "items" / itemId / ("attempt-" + std::to_string(attempt)) / "fork.json") ? "model" : "parts";   // forked sections write fork.json; a coordinator has no agent step
    if (agent.find("\"tier\":\"frontier-stub\"") != std::string::npos) return "stub";
    if (agent.find("\"engine\":") != std::string::npos) return "model";
    return "mock";
}

std::string provenanceMarkdown(const Plan& plan, const fs::path& dir)
{
    std::string md = "\n## How each item was built\n\n| item | built by | attempt |\n|---|---|---|\n";
    int model = 0, stub = 0, mock = 0, leaves = 0;
    for (const auto& it : plan.items) {
        const std::string by = builtBy(dir, it.id);
        const std::string accepted = slurp(dir / "items" / it.id / "accepted.json");
        const auto at = accepted.find("\"attempt\":");
        md += "| " + it.id + " | " + (by.empty() ? std::string{"(not accepted)"} : by) + " | " + (at == std::string::npos ? std::string{"-"} : std::to_string(std::atoi(accepted.c_str() + at + 10))) + " |\n";
        if (by == "model") ++model; else if (by == "stub") ++stub; else if (by == "mock") ++mock;
        if (by == "model" || by == "stub" || by == "mock") ++leaves;
    }
    if (leaves == 0) md += "\nNo item was built by an agent.\n";
    else if (model == leaves) md += "\nEvery item was built by a model.\n";
    else md += "\n**" + std::to_string(model) + " of " + std::to_string(leaves) + " items were built by a model.** " + (stub ? std::to_string(stub) + " were written by the frontier STUB tier, which supplies the reference implementation: those passes say nothing about the model. " : std::string{}) +
               (mock ? std::to_string(mock) + " were written by the MOCK agent (reference templates, no model at all). " : std::string{}) + "\n";
    return md;
}

std::set<std::string> closure(const Plan& plan, const std::string& id)
{
    std::set<std::string> seen;
    std::vector<std::string> work{id};
    while (!work.empty()) {
        const WorkItem* it = plan.find(work.back());
        work.pop_back();
        if (!it) continue;
        for (const auto& d : it->deps) if (seen.insert(d).second) work.push_back(d);
    }
    return seen;
}



bool interfaceGap(const std::string& t)
{
    // Judge by the FIRST error only: after a syntax error the compiler reports undeclared names and missing members that are its own confusion, not a gap
    // between modules (seen on the first 35B run: an unterminated string literal followed by "was not declared in this scope").
    std::istringstream in(t);
    for (std::string line; std::getline(in, line);) {
        std::string low = line;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (low.find("error") == std::string::npos && low.find("undefined reference") == std::string::npos) continue;
        for (const char* sig : {"does not provide an export named", "cannot import name", "undefined reference to", "is not a member of", "no member named", "was not declared in this scope",
                                "has no attribute", "is not a function", "is not exported", "no module named"})
            if (low.find(sig) != std::string::npos) return true;
        return false;
    }
    return false;
}

std::string importedNames(const fs::path& ws, const std::vector<std::string>& testFiles)
{
    static const std::regex js(R"(import\s*\{([^}]*)\}\s*from\s*['"]([^'"]+)['"])");
    static const std::regex py(R"(from\s+([\w.]+)\s+import\s+([\w, ]+))");
    std::string out;
    for (const auto& t : testFiles) {
        const std::string text = slurp(ws / t);
        for (std::sregex_iterator it(text.begin(), text.end(), js), end; it != end; ++it) {
            std::string names = (*it)[1].str();
            names.erase(std::remove_if(names.begin(), names.end(), [](char c) { return c == '\n' || c == '\r'; }), names.end());
            out += t + " imports {" + names + " } from " + (*it)[2].str() + "\n";
        }
        for (std::sregex_iterator it(text.begin(), text.end(), py), end; it != end; ++it)
            if ((*it)[1].str().rfind("calc", 0) == 0) out += t + " imports " + (*it)[2].str() + " from " + (*it)[1].str() + "\n";
    }
    return out;
}

// ---- the shared gate ----------------------------------------------------------------------------------------------------------------
namespace {
std::set<std::string> failLines(const std::string& gateText)
{
    std::set<std::string> s;
    std::istringstream in(gateText);
    for (std::string l; std::getline(in, l);) if (l.rfind("FAIL ", 0) == 0) s.insert(l);
    return s;
}
}  // namespace

std::string runPrecheckFlow(const GateHooks& hooks, const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir", "");
    const std::string itemId = one(a, "item", "");
    const int attempt = std::atoi(one(a, "attempt", "1").c_str());
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) return "cannot load plan: " + plan.error();
    if (!plan->find(itemId)) return "no item '" + itemId + "'";
    if (!hooks.precheck) return {};
    const fs::path attemptDir = dir / "items" / itemId / ("attempt-" + std::to_string(attempt));
    const fs::path files = one(a, "files-dir", (attemptDir / "out").string());
    if (!fs::exists(files)) return "there are no files to check";
    const fs::path ws = attemptDir / "ws-pre";
    std::error_code ec;
    fs::remove_all(ws, ec);
    writeScaffold(*plan, hooks.scaffold(), ws);
    for (const auto& dep : closure(*plan, itemId)) copyTree(dir / "items" / dep / "accepted", ws);
    copyTree(files, ws);
    const bool partial = one(a, "partial", "") == "1";
    std::string bad = partial && hooks.precheckPartial ? hooks.precheckPartial(files, ws) : hooks.precheck(files, ws);
    fs::remove_all(ws, ec);
    return bad;
}

int runGateFlow(const GateHooks& hooks, const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir", "");
    const std::string itemId = one(a, "item", "");
    const int attempt = std::atoi(one(a, "attempt", "1").c_str());
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) { std::printf("GATE FAIL: cannot load plan: %s\n", plan.error().c_str()); return 1; }
    const WorkItem* item = plan->find(itemId);
    if (!item) { std::printf("GATE FAIL: no item '%s'\n", itemId.c_str()); return 1; }
    const fs::path attemptDir = dir / "items" / itemId / ("attempt-" + std::to_string(attempt));
    const fs::path out = attemptDir / "out";
    const fs::path mendFile = attemptDir / "mend.json";
    std::error_code mendEc;
    fs::remove(mendFile, mendEc);
    // A failure the mends table knows (mends.hpp) is reported with the failure: `attempt-N/mend.json`, and as this job's result (which the journal carries),
    // so the engine can have the harness repair the cause instead of asking a model or the ladder. The table is the built-in one plus the workflow's own
    // `<workflow>/mends.json` (a mend of the same name replaces the built-in one). A requirement that does not hold means no mend.
    const auto reportMend = [&](const std::string& msg) {
        if (item->persona != "developer") return;
        std::vector<Mend> table = builtinMends();
        if (const std::string own = slurp(dir / "mends.json"); !own.empty()) { if (auto extra = parseMends(own)) table = mergedMends(std::move(table), *extra); else std::printf("note: %s is ignored: %s\n", (dir / "mends.json").c_str(), extra.error().c_str()); }
        const auto leafOf = [](const std::string& n) { const auto c = n.find_last_of(":."); return c == std::string::npos ? n : n.substr(c + 1); };
        // Table order; the first mend whose pattern matches the first error AND whose requirement holds (two mends may share a pattern: an undefined
        // reference is a contract function, or a member the file's own class declares).
        std::optional<MendMatch> m;
        for (const Mend& candidate : table) {
            m = matchMend({candidate}, msg);
            if (!m) continue;
            bool holds = false;
            // The gate clips long output lines, and a long workspace path can cut the line INSIDE the name (`calc::ev`): a name that ends where the line
            // ends is a prefix, and it holds when exactly one candidate begins with it.
            const bool cut = m->error.size() >= m->symbol.size() && m->error.compare(m->error.size() - m->symbol.size(), m->symbol.size(), m->symbol) == 0;
            if (m->requires_ == "contract") {
                int candidates = 0;
                std::string full, file;
                for (const auto& f : item->files) for (const auto& c : undefinedContract(slurp(attemptDir / "out" / f), f, contractOf(findHarness(plan->harness), &*plan, f))) {   // only one the file does NOT define can be the missing one
                    const std::string qualified = c.scope.empty() ? c.name : c.scope + "::" + c.name;
                    if (m->symbol == qualified || m->symbol == c.name || (cut && (qualified.rfind(m->symbol, 0) == 0 || c.name.rfind(m->symbol, 0) == 0))) { ++candidates; full = qualified; file = f; }
                }
                if (candidates == 1) { holds = true; m->file = file; m->symbol = full; }
            } else if (m->requires_ == "declared") {
                const auto endsWith = [](const std::string& s, const std::string& tail) { return s == tail || (s.size() > tail.size() + 2 && s.compare(s.size() - tail.size() - 2, 2, "::") == 0 && s.compare(s.size() - tail.size(), tail.size(), tail) == 0); };
                for (const auto& f : item->files) for (const auto& member : undefinedMembers(slurp(attemptDir / "out" / f), f)) {
                    const std::string full = (member.scope.empty() ? std::string{} : member.scope + "::") + member.qualified();
                    if (holds || !(endsWith(full, m->symbol) || endsWith(m->symbol, member.qualified()) || (cut && full.rfind(m->symbol, 0) == 0))) continue;
                    holds = true; m->file = f; m->symbol = full;
                }
            } else if (m->requires_ == "prototype") {
                for (const auto& f : item->files) if (!prototypeOf(slurp(attemptDir / "out" / f), leafOf(m->symbol)).empty()) { holds = true; m->file = f; }
            } else if (m->requires_ == "location" && m->line > 0) {
                for (const auto& f : item->files) if (m->file == f || (m->file.size() > f.size() && m->file.compare(m->file.size() - f.size() - 1, f.size() + 1, "/" + f) == 0)) { holds = true; m->file = f; }
            }
            if (holds) break;
            m.reset();
        }
        if (!m) return;
        const std::string result = "{\"mend\":" + m->toJson() + "}\n";
        writeFile(mendFile, result);
        if (const char* runDir = std::getenv("SUB0SQUAD_RUN_DIR"); runDir && *runDir && one(a, "no-publish", "").empty()) writeFile(fs::path{runDir} / "result.json", result);   // only the gate JOB reports; an agent's self-check does not
        std::printf("mend available: %s (%s) for %s\n", m->name.c_str(), m->action.c_str(), m->symbol.c_str());
    };
    // A fork that left a section unaccepted (catalogue row 75): its stub stands in the file, so that section is the FIRST cause of whatever fails here.
    // The gate says so first, answers exit 8, and reports no mend for an error about unused code (a consequence: the only caller is the missing section).
    const std::string unaccepted = item->persona == "developer" ? unacceptedSections(slurp(attemptDir / "fork.json")) : std::string{};
    const auto fail = [&](const std::string& why) {
        const std::string msg = "GATE FAIL: " + (unaccepted.empty() ? std::string{} : "FIRST CAUSE: never accepted: " + unaccepted + " (a stub stands in its place; errors about unused code below are consequences)\n") + why + "\n";
        writeFile(attemptDir / "gate.txt", msg);
        std::fputs(msg.c_str(), stdout);
        if (unaccepted.empty() || !isUnusedConsequence(firstErrorLine(msg))) reportMend(msg);
        else std::printf("no mend: the error is a consequence of the unaccepted section(s) %s\n", unaccepted.c_str());
        if (one(a, "no-publish", "").empty()) {   // the gate JOB: why this attempt failed, in a few lines (an agent's self-check leaves that to the agent)
            const std::string first = firstErrorLine(msg);
            recordAttemptFailure(attemptDir, "gate", "gate", msg.substr(0, msg.find('\n')) + (first.empty() || msg.rfind(first, 0) == 0 ? std::string{} : "\n" + first));
        }
        return unaccepted.empty() ? 1 : 8;
    };

    const bool coordinator = item->persona == "integrator";  // writes nothing: its tests run over the merged outputs of its parts
    events::progress(0, 4, "allowlist");
    std::set<std::string> allowed(item->files.begin(), item->files.end());
    if (!coordinator && !fs::exists(out)) return fail("the agent wrote nothing");
    if (!coordinator) for (auto& e : fs::recursive_directory_iterator(out)) {
        if (!e.is_regular_file()) continue;
        const std::string rel = fs::relative(e.path(), out).generic_string();
        if (!allowed.count(rel)) return fail("'" + rel + "' is outside the item's editable files (" + item->files.front() + "...)");
    }
    if (!coordinator) for (const auto& f : item->files) if (!fs::exists(out / f) || fs::file_size(out / f) == 0) return fail("expected file '" + f + "' is missing or empty");

    const fs::path ws = attemptDir / "ws";
    std::error_code ec;
    fs::remove_all(ws, ec);
    writeScaffold(*plan, hooks.scaffold(), ws);
    for (const auto& dep : closure(*plan, itemId)) copyTree(dir / "items" / dep / "accepted", ws);
    if (!coordinator) copyTree(out, ws);

    const auto failWidely = [&](const std::string& why) {
        // An interface mismatch is not a bug in this one file: say which names the tests need, and exit 6 so the policy goes up a scope.
        if (!interfaceGap(why)) return fail(why);
        const std::string names = importedNames(ws, item->gate.tests);
        fail("INTERFACE MISMATCH: " + why + (names.empty() ? std::string{} : "The tests need these names from the module (provide exactly these, with these spellings):\n" + names));
        return 6;
    };
    events::progress(1, 4, "check");
    if (!coordinator && hooks.precheck) if (const std::string bad = hooks.precheck(out, ws); !bad.empty()) return failWidely(bad);
    for (const auto& t : item->gate.tests) if (!fs::exists(ws / t)) return fail("gate test file '" + t + "' does not exist in the workspace");

    events::progress(2, 4, "test");
    Tap tap;
    bool timedOut = false;
    if (const std::string bad = hooks.buildAndRun(ws, item->gate.tests, tap, timedOut); !bad.empty()) return failWidely(bad);
    if (timedOut) return fail("tests timed out");
    events::progress(3, 4, "judge");
    if (tap.tests < 0) return fail("the test runner produced no result");
    if (tap.tests < item->gate.minTests)
        return failWidely("only " + std::to_string(tap.tests) + " test(s) ran, at least " + std::to_string(item->gate.minTests) + " are required\n" + tap.excerpt +
                    (tap.note.empty() ? std::string{} : "The runner reported (a test file that fails while loading counts as one failed test):\n" + tap.note));
    if (item->gate.mustFailOnStub) {
        // A tester's tests must be about something real: against the stubs most of them must fail.
        if (tap.fail * 10 < tap.tests * 7) return fail("only " + std::to_string(tap.fail) + " of " + std::to_string(tap.tests) + " tests fail against the stub: they do not exercise the contract");
        if (hooks.afterTesterJudged) if (const std::string bad = hooks.afterTesterJudged(GateContext{item, dir, attemptDir, out, ws, attempt}, tap); !bad.empty()) return fail(bad);
    } else if (tap.fail != 0) {
        const std::string failedText = std::to_string(tap.fail) + " of " + std::to_string(tap.tests) + " tests failed\n" + tap.excerpt + tap.note;
        if (interfaceGap(failedText)) return failWidely(failedText);
        const int code = fail(std::to_string(tap.fail) + " of " + std::to_string(tap.tests) + " tests failed\n" + tap.excerpt);
        if (attempt > 1) {
            const auto now = failLines(slurp(attemptDir / "gate.txt"));
            const auto before = failLines(slurp(dir / "items" / itemId / ("attempt-" + std::to_string(attempt - 1)) / "gate.txt"));
            // The same few tests fail the same way after a repair: the tests are suspect. Not when the FILES are the same too (an attempt whose rounds were
            // undone ends on the best earlier state, catalogue row 70): the same failure of the same code says nothing new about the tests.
            bool sameFiles = true;
            for (const auto& f : item->files) if (slurp(out / f) != slurp(dir / "items" / itemId / ("attempt-" + std::to_string(attempt - 1)) / "out" / f)) sameFiles = false;
            if (!sameFiles && !now.empty() && now == before && tap.tests >= 5 && tap.fail * 100 <= tap.tests * 40) return 5;
            // 7: a STUCK near pass. The files are the previous attempt's, so its near pass is this one's: the repair changed nothing that stands.
            if (sameFiles && tap.tests >= 10 && tap.fail * 100 <= tap.tests * 15) return 7;
        }
        return tap.tests >= 10 && tap.fail * 100 <= tap.tests * 15 ? 3 : code;  // 3: a near pass, repaired rather than divided
    }
    const bool publish = one(a, "no-publish", "").empty();  // the optimiser judges attempts without touching the accepted outputs
    if (publish) {
        fs::remove_all(dir / "items" / itemId / "accepted", ec);
        if (coordinator) fs::create_directories(dir / "items" / itemId / "accepted", ec); else copyTree(out, dir / "items" / itemId / "accepted");
        writeFile(dir / "items" / itemId / "accepted.json", "{\"item\":\"" + itemId + "\",\"attempt\":" + std::to_string(attempt) + "}");
    }
    char passLine[200];
    std::snprintf(passLine, sizeof passLine, "GATE PASS: %s attempt %d (%d tests, %d failing against stubs where expected)\n", itemId.c_str(), attempt, tap.tests, tap.fail);
    writeFile(attemptDir / "gate.txt", passLine);
    if (publish) clearAttemptFailure(attemptDir);
    events::progress(4, 4, "accepted");
    std::fputs(passLine, stdout);
    return 0;
}

}  // namespace sub0squad::harnessutil
