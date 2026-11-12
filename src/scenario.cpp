#include "sub0squad/scenario.hpp"

#include "json_util.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_common.hpp"
#include "sub0squad/plan.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace sub0squad {

namespace fs = std::filesystem;
using namespace harnessutil;

namespace {

struct PlatformDef { const char* harness; const char* blueprint; const char* tester; const char* dev; const char* appTester; const char* appDev; };
const std::vector<PlatformDef>& platforms()
{
    static const std::vector<PlatformDef> p = {
        {"cpp-cmake", "calculator-cpp", "test-engine", "dev-engine", "test-app", "dev-app"},
        {"py-unittest", "calculator-py", "test-engine", "dev-engine", "test-app", "dev-app"},
        {"web-static", "calculator-web", "test-engine", "dev-engine", "test-ui", "dev-ui"},
    };
    return p;
}

enum class Start { empty, draftSyntax, draftSemantic };
/// A scenario: which item the agent works on, which items are already accepted (with the reference outputs), and the starting draft of the item (if any).
struct Def { const char* element; const char* itemKey; std::vector<const char*> acceptedKeys; Start start; bool oracleJudge; };
const std::vector<Def>& defs()
{
    static const std::vector<Def> d = {
        {"tester", "tester", {}, Start::empty, true},
        {"developer-first", "dev", {"tester"}, Start::empty, false},
        {"developer-repair-syntax", "dev", {"tester"}, Start::draftSyntax, false},
        {"developer-repair-semantic", "dev", {"tester"}, Start::draftSemantic, false},
        {"app-first", "appDev", {"appTester", "dev"}, Start::empty, false},
        {"app-repair", "appDev", {"appTester", "dev"}, Start::draftSemantic, false},
    };
    return d;
}

std::string itemId(const PlatformDef& p, const std::string& key)
{
    return key == "tester" ? p.tester : key == "dev" ? p.dev : key == "appTester" ? p.appTester : p.appDev;
}

std::string selfPath()
{
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{"sub0squad"};
}

/// The sub0squad binary the scenarios drive (its gate and agent commands). Set by the caller: a test binary linking this unit must NOT use its own path (the first
/// version read /proc/self/exe, so the test re-ran itself for every gate call: a fork bomb, 33,000 processes, the host's memory and 107,000 temp directories).
std::string& exeRef() { static std::string e; return e; }
std::string exePath() { return exeRef(); }

/// Accept `exe` only if it really is the sub0squad binary (`--version` says so); also refuse to run nested inside a scenario run.
bool adoptExe(const std::string& exe, std::string& error)
{
    if (std::getenv("SUB0SQUAD_SCENARIO_ACTIVE")) { error = "refusing to run a scenario inside a scenario run"; return false; }
    ::setenv("SUB0SQUAD_SCENARIO_ACTIVE", "1", 1);   // set BEFORE anything is executed: children inherit it, so a scenario started from inside one is refused
    if (exe.empty() || !fs::exists(exe)) { ::unsetenv("SUB0SQUAD_SCENARIO_ACTIVE"); error = "the sub0squad binary '" + exe + "' does not exist"; return false; }
    const Cmd v = run({fs::absolute(exe).string(), "--version"}, ".", 10);
    if (v.exitCode != 0 || v.output.rfind("sub0squad", 0) != 0) { ::unsetenv("SUB0SQUAD_SCENARIO_ACTIVE"); error = "'" + exe + "' is not the sub0squad binary (--version said: " + v.output.substr(0, 60) + ")"; return false; }
    exeRef() = fs::absolute(exe).string();   // absolute: every command changes directory before it starts
    return true;
}

/// A stray token line appended to a source file: the compile or parse slip small models make (the real 35B run: a stray `();`).
std::string breakSyntax(const std::string& text, const std::string& harness)
{
    return text + (harness == "py-unittest" ? "\ndef (:\n" : "\n();\n");
}

struct Fixture { fs::path dir; Plan plan; const Harness* harness = nullptr; const PlatformDef* platform = nullptr; };

bool makeFixture(const PlatformDef& pd, const Def& def, const fs::path& dir, Fixture& fx, std::string& err)
{
    fx.platform = &pd;
    fx.harness = findHarness(pd.harness);
    if (!fx.harness || !fx.harness->blueprint) { err = "no harness or blueprint"; return false; }
    auto plan = Plan::fromJson(fx.harness->blueprint(pd.blueprint));
    if (!plan) { err = "blueprint: " + plan.error(); return false; }
    fx.plan = *plan;
    fx.dir = dir;
    std::error_code ec;
    fs::remove_all(dir, ec);
    writeFile(dir / "plan.json", fx.plan.toJson());
    for (const char* key : def.acceptedKeys) {   // the reference outputs of the items this one depends on, as if already accepted
        const WorkItem* it = fx.plan.find(itemId(pd, key));
        if (!it) { err = std::string("no item ") + itemId(pd, key); return false; }
        for (const auto& f : it->files) {
            const std::string text = fx.harness->mockFile(f, false);
            if (text.empty()) { err = "no reference file for " + f; return false; }
            writeFile(dir / "items" / it->id / "accepted" / f, text);
        }
        writeFile(dir / "items" / it->id / "accepted.json", "{\"item\":\"" + it->id + "\",\"attempt\":1}");
    }
    return true;
}

/// Run the harness gate on an attempt without publishing; true when it passes.
bool gatePasses(const Fixture& fx, const std::string& item, int attempt)
{
    const Cmd c = run({exePath(), fx.harness->gateCommand, "--workflow-dir", fx.dir.string(), "--item", item, "--attempt", std::to_string(attempt), "--no-publish", "1"}, fx.dir, 300);
    return c.exitCode == 0;
}

void writeAttempt(const Fixture& fx, const std::string& item, int attempt, const std::map<std::string, std::string>& files)
{
    for (const auto& [path, text] : files) writeFile(fx.dir / "items" / item / ("attempt-" + std::to_string(attempt)) / "out" / path, text);
}

std::map<std::string, std::string> referenceFiles(const Fixture& fx, const WorkItem& it, bool buggy)
{
    std::map<std::string, std::string> m;
    for (const auto& f : it.files) {
        const bool useBug = buggy && f == fx.harness->mockBugTarget(it.id);
        m[f] = fx.harness->mockFile(f, useBug);
    }
    return m;
}

/// The judge. A tester's tests must pass the gate AND be correct: the same tests against the reference implementation must pass. Anything else: the gate.
bool judge(const Fixture& fx, const Def& def, const std::string& item, int attempt)
{
    if (!gatePasses(fx, item, attempt)) return false;
    if (!def.oracleJudge) return true;
    // Oracle: accept the candidate's tests, put the reference engine in an attempt of the engine item, and run that item's gate over both.
    const WorkItem* tester = fx.plan.find(item);
    const WorkItem* dev = fx.plan.find(fx.platform->dev);
    if (!tester || !dev) return false;
    std::error_code ec;
    fs::remove_all(fx.dir / "items" / item / "accepted", ec);
    copyTree(fx.dir / "items" / item / ("attempt-" + std::to_string(attempt)) / "out", fx.dir / "items" / item / "accepted");
    writeAttempt(fx, dev->id, 900, referenceFiles(fx, *dev, false));
    return gatePasses(fx, dev->id, 900);
}

/// How much of the final files differs from the draft, as a share of lines (0 = identical, 1 = everything new): the patching skill, not only the pass.
double changedShare(const std::map<std::string, std::string>& draft, const fs::path& outDir, const WorkItem& it)
{
    long total = 0, changed = 0;
    for (const auto& f : it.files) {
        std::multiset<std::string> had;
        std::istringstream d(draft.count(f) ? draft.at(f) : std::string{});
        for (std::string l; std::getline(d, l);) had.insert(l);
        std::istringstream n(slurp(outDir / f));
        for (std::string l; std::getline(n, l);) {
            ++total;
            const auto h = had.find(l);
            if (h == had.end()) ++changed; else had.erase(h);
        }
    }
    return total ? static_cast<double>(changed) / static_cast<double>(total) : 1.0;
}

struct Trial { bool generated = false, passed = false; double seconds = 0, tokensIn = 0, tokensOut = 0, changed = -1; };

double numberAfter(const std::string& text, const char* key) { const auto p = text.find(key); return p == std::string::npos ? 0.0 : std::atof(text.c_str() + p + std::string{key}.size()); }

}  // namespace

double wilsonLowerBound(int k, int n)
{
    if (n <= 0) return 0.0;
    const double z = 1.959964, p = static_cast<double>(k) / n, z2 = z * z;
    return (p + z2 / (2 * n) - z * std::sqrt((p * (1 - p) + z2 / (4 * n)) / n)) / (1 + z2 / n);
}

std::vector<ScenarioInfo> scenarioCatalogue()
{
    std::vector<ScenarioInfo> out;
    for (const auto& pd : platforms()) for (const auto& d : defs()) out.push_back({d.element, d.element, pd.harness});
    return out;
}

std::vector<std::string> scenarioSelfTest(const std::string& only, const std::string& exe)
{
    std::vector<std::string> problems;
    if (std::string err; !adoptExe(exe, err)) return {err};
    for (const auto& pd : platforms()) {
        for (const auto& d : defs()) {
            const std::string name = std::string(pd.harness) + "/" + d.element;
            if (!only.empty() && only != d.element && only != pd.harness && only != name) continue;
            Fixture fx;
            std::string err;
            const fs::path dir = fs::temp_directory_path() / ("s0q-scenario-" + std::string(pd.harness) + "-" + d.element + "-" + std::to_string(::getpid()));
            if (!makeFixture(pd, d, dir, fx, err)) { problems.push_back(name + ": fixture: " + err); continue; }
            const std::string item = itemId(pd, d.itemKey);
            const WorkItem* it = fx.plan.find(item);
            if (!it) { problems.push_back(name + ": no item " + item); continue; }
            // 1. The starting state must not pass: a broken draft, or (a tester) a file that tests nothing.
            if (d.start == Start::empty && d.oracleJudge) {
                writeAttempt(fx, item, 1, {{it->files.front(), "// no tests\n"}});
                if (judge(fx, d, item, 1)) problems.push_back(name + ": a file with no tests passes the judge");
            } else if (d.start != Start::empty) {
                auto draft = referenceFiles(fx, *it, d.start == Start::draftSemantic);
                if (d.start == Start::draftSyntax) for (auto& [path, text] : draft) text = breakSyntax(text, pd.harness);
                writeAttempt(fx, item, 1, draft);
                if (gatePasses(fx, item, 1)) problems.push_back(name + ": the broken draft passes the gate (the scenario would pass with no agent)");
            }
            // 2. The reference solution must pass the judge.
            writeAttempt(fx, item, 2, referenceFiles(fx, *it, false));
            if (!judge(fx, d, item, 2)) problems.push_back(name + ": the reference solution does not pass the judge (the fixture or the reference is wrong)");
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    }
    return problems;
}

int runScenarioCommand(const std::vector<std::string>& args)
{
    if (args.empty() || args[0] == "list") {
        for (const auto& s : scenarioCatalogue()) std::printf("%-12s %s\n", s.platform.c_str(), s.element.c_str());
        return 0;
    }
    const auto a = parseArgs(std::vector<std::string>(args.begin() + 1, args.end()));
    const std::string only = one(a, "only", "");
    if (args[0] == "selftest") {
        const auto problems = scenarioSelfTest(only, selfPath());
        for (const auto& p : problems) std::printf("PROBLEM %s\n", p.c_str());
        std::printf(problems.empty() ? "every scenario fixture is valid: the reference passes and the starting state does not\n" : "%zu problem(s)\n", problems.size());
        return problems.empty() ? 0 : 1;
    }
    if (args[0] != "run") { std::fprintf(stderr, "scenario: list | selftest | run\n"); return 64; }
    const std::string engine = one(a, "engine", "");
    if (engine.empty()) { std::fprintf(stderr, "scenario run: --engine is required\n"); return 64; }
    const int trials = std::max(1, std::atoi(one(a, "trials", "10").c_str()));
    const int parallel = std::max(1, std::atoi(one(a, "parallel", "2").c_str()));
    const double minLower = std::atof(one(a, "min-lower", "0.6").c_str());
    const std::string treatment = one(a, "treatment", "baseline"), repairTurns = one(a, "repair-turns", "2"), maxTokens = one(a, "max-tokens", "3500");
    const fs::path outDir = one(a, "out", "out/scenarios");
    const long baseSeed = std::atol(one(a, "seed", "1000").c_str());
    if (std::string err; !adoptExe(selfPath(), err)) { std::fprintf(stderr, "scenario run: %s\n", err.c_str()); return 64; }
    const std::string exe = exePath();
    const Cmd ver = run({exe, "--version"}, ".", 10);
    std::string build = ver.output;
    while (!build.empty() && (build.back() == '\n' || build.back() == ' ')) build.pop_back();

    json::Writer w;
    w.beginObject().kv("schema", "scenario-results-1").kv("build", build).kv("engine", engine).kv("treatment", treatment).kv("trials", trials).kv("minLower", minLower).key("results").beginArray();
    std::printf("%-12s %-27s %8s %7s %7s %8s %9s %8s  %s\n", "platform", "element", "passed", "rate", "lower95", "seconds", "tokens", "changed", "verdict");
    int unreliable = 0;
    for (const auto& pd : platforms()) {
        for (const auto& d : defs()) {
            const std::string name = std::string(pd.harness) + "/" + d.element;
            if (!only.empty() && only != d.element && only != pd.harness && only != name) continue;
            const std::string item = itemId(pd, d.itemKey);
            std::vector<Trial> results(static_cast<std::size_t>(trials));
            std::atomic<int> next{0};
            const auto worker = [&]() {
                for (;;) {
                    const int i = next.fetch_add(1);
                    if (i >= trials) return;
                    Trial& t = results[static_cast<std::size_t>(i)];
                    Fixture fx;
                    std::string err;
                    const fs::path dir = outDir / "work" / (std::string(pd.harness) + "-" + d.element + "-" + std::to_string(i));
                    if (!makeFixture(pd, d, dir, fx, err)) continue;
                    const WorkItem* it = fx.plan.find(item);
                    int attempt = 1;
                    std::map<std::string, std::string> draft;
                    if (d.start != Start::empty) {   // an earlier attempt that failed: the agent's attempt 2 starts from it and the gate's own words
                        draft = referenceFiles(fx, *it, d.start == Start::draftSemantic);
                        if (d.start == Start::draftSyntax) for (auto& [path, text] : draft) text = breakSyntax(text, pd.harness);
                        writeAttempt(fx, item, 1, draft);
                        (void)gatePasses(fx, item, 1);   // writes the gate text the agent will be shown
                        attempt = 2;
                    }
                    const auto began = std::chrono::steady_clock::now();
                    const Cmd c = run({exe, "llm-agent", "--workflow-dir", dir.string(), "--item", item, "--tier", "local", "--attempt", std::to_string(attempt), "--engine", engine + ",seed=" + std::to_string(baseSeed + i),
                                       "--max-tokens", maxTokens, "--treatment", treatment, "--repair-turns", repairTurns, "--timeout", "1700"}, dir, 2400);
                    t.generated = c.exitCode == 0;
                    if (t.generated) t.passed = judge(fx, d, item, attempt);
                    t.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
                    const std::string metrics = slurp(dir / "items" / item / ("attempt-" + std::to_string(attempt)) / "agent.json");
                    t.tokensIn = numberAfter(metrics, "\"tokensIn\":");
                    t.tokensOut = numberAfter(metrics, "\"tokensOut\":");
                    if (t.generated && d.start != Start::empty) t.changed = changedShare(draft, dir / "items" / item / ("attempt-" + std::to_string(attempt)) / "out", *it);
                }
            };
            std::vector<std::thread> pool;
            for (int k = 0; k < std::min(parallel, trials); ++k) pool.emplace_back(worker);
            for (auto& th : pool) th.join();
            int ok = 0, changedN = 0;
            double secs = 0, tok = 0, changed = 0;
            for (const auto& t : results) { ok += t.passed; secs += t.seconds; tok += t.tokensIn + t.tokensOut; if (t.changed >= 0) { changed += t.changed; ++changedN; } }
            const double lower = wilsonLowerBound(ok, trials);
            const bool reliable = lower >= minLower;
            unreliable += reliable ? 0 : 1;
            std::printf("%-12s %-27s %5d/%-2d %6.0f%% %7.2f %8.0f %9.0f %8s  %s\n", pd.harness, d.element, ok, trials, 100.0 * ok / trials, lower, secs / trials, tok / trials,
                        changedN ? (std::to_string(static_cast<int>(100 * changed / changedN)) + "%").c_str() : "-", reliable ? "RELIABLE" : "not proven");
            std::fflush(stdout);
            w.beginObject().kv("platform", pd.harness).kv("element", d.element).kv("passed", ok).kv("trials", trials).kv("lower95", lower).kv("meanSeconds", secs / trials).kv("meanTokens", tok / trials)
                .kv("meanChangedShare", changedN ? changed / changedN : -1.0).kvBool("reliable", reliable).endObject();
        }
    }
    w.endArray().endObject();
    writeFile(outDir / "scenario-results.json", w.take());
    std::printf("\n%d scenario(s) not proven (95%% lower bound below %.2f); results in %s\n", unreliable, minLower, (outDir / "scenario-results.json").c_str());
    return unreliable == 0 ? 0 : 1;
}

}  // namespace sub0squad
