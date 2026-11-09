#include "sub0squad/sandbox.hpp"

#include "json_util.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/plan.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

std::map<std::string, std::vector<std::string>> parseArgs(const std::vector<std::string>& args)
{
    std::map<std::string, std::vector<std::string>> m;
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i].rfind("--", 0) == 0) { m[args[i].substr(2)].push_back(args[i + 1]); ++i; }
    return m;
}
std::string one(const std::map<std::string, std::vector<std::string>>& m, const std::string& k, const std::string& dflt = {})
{
    auto it = m.find(k);
    return it == m.end() || it->second.empty() ? dflt : it->second.back();
}

std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFileAtomic(const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    { std::ofstream o(tmp, std::ios::binary | std::ios::trunc); if (!o) return false; o << text; if (!o) return false; }
    fs::rename(tmp, p, ec);
    return !ec;
}

std::optional<Plan> loadPlan(const fs::path& dir, std::string& error)
{
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) { error = "cannot load plan.json: " + plan.error(); return std::nullopt; }
    return std::move(*plan);
}

// ---- running a command with a timeout, capturing its combined output -----------------------------------------------
struct CmdResult { int exitCode = -1; bool timedOut = false; std::string output; };

CmdResult runCommand(const std::vector<std::string>& argv, const fs::path& cwd, int timeoutSec)
{
    CmdResult r;
    int fds[2];
    if (::pipe(fds) != 0) return r;
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(fds[1], 1);
        ::dup2(fds[1], 2);
        ::close(fds[0]); ::close(fds[1]);
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) _exit(126);
        std::vector<char*> a;
        for (const auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        ::execvp(a[0], a.data());
        _exit(127);
    }
    ::close(fds[1]);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
    char buf[4096];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) { r.timedOut = true; ::kill(pid, SIGKILL); break; }
        pollfd p{fds[0], POLLIN, 0};
        const int n = ::poll(&p, 1, static_cast<int>(left));
        if (n <= 0) continue;
        const ssize_t got = ::read(fds[0], buf, sizeof buf);
        if (got <= 0) break;
        r.output.append(buf, static_cast<std::size_t>(got));
        if (r.output.size() > 256 * 1024) r.output.erase(0, r.output.size() - 128 * 1024);
    }
    ::close(fds[0]);
    int st = 0;
    ::waitpid(pid, &st, 0);
    r.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

std::string compiler()
{
    if (const char* cxx = std::getenv("CXX"); cxx && *cxx) return cxx;
    const char* path = std::getenv("PATH");
    std::stringstream ss(path ? path : "");
    std::string dir;
    for (const char* name : {"clang++", "g++", "c++"}) {
        ss.clear(); ss.seekg(0);
        while (std::getline(ss, dir, ':')) if (!dir.empty() && ::access((dir + "/" + name).c_str(), X_OK) == 0) return name;
    }
    return "c++";
}

std::string tail(const std::string& s, std::size_t n = 1500) { return s.size() <= n ? s : "..." + s.substr(s.size() - n); }

// Transitive dependency closure of an item (excluding the item itself).
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

}  // namespace

// ---- planner --------------------------------------------------------------------------------------------------------
int runMockPlanner(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const std::string task = one(a, "task"), out = one(a, "out");
    if (task.empty() || out.empty()) { std::fprintf(stderr, "mock-planner: --task and --out are required\n"); return 64; }
    events::progress(0, 1, "planning");
    Plan plan;
    plan.task = task;
    const std::vector<std::string> allFiles = {"tokenizer.hpp", "test_tokenizer.cpp", "parser.hpp", "test_parser.cpp", "functions.hpp", "test_functions.cpp",
                                               "evaluator.hpp", "test_evaluator.cpp", "cli.hpp", "main.cpp", "test_cli.cpp", "calc_tests.cpp"};
    if (task == "calculator-basic") {
        plan.reasons = {"small task: one self-contained deliverable, splitting would cost more than it saves", "decision: single item"};
        plan.items = {{"calculator", "Calculator (all modules)", "Write a calculator with + - * / % ^ !, parentheses, constants and a CLI", allFiles, {}, 2}};
    } else if (task == "calculator-scientific") {
        plan.reasons = {"task needs a tokenizer, a parser, a function library, an evaluator, a command line and tests",
                        "modules have clear interfaces and disjoint files: independent items can run in parallel",
                        "decision: split into 6 items"};
        plan.items = {
            {"tokenizer", "Tokenizer", "Split an expression into numbers, names, operators, parentheses and commas", {"tokenizer.hpp", "test_tokenizer.cpp"}, {}, 1},
            {"parser", "Parser", "Parse tokens into an AST: precedence, right-associative ^, unary minus, postfix !, calls", {"parser.hpp", "test_parser.cpp"}, {"tokenizer"}, 3},
            {"functions", "Function library", "Trigonometric (radians or degrees), hyperbolic, logs, roots, rounding, pow, min, max, factorial, constants", {"functions.hpp", "test_functions.cpp"}, {}, 2},
            {"evaluator", "Evaluator", "Evaluate the AST using the function library; reject division by zero and unknown names", {"evaluator.hpp", "test_evaluator.cpp"}, {"parser", "functions"}, 2},
            {"cli", "Command line", "A calc program: expressions as arguments or lines on stdin, --deg, non-zero exit on errors", {"cli.hpp", "main.cpp", "test_cli.cpp"}, {"evaluator"}, 1},
            {"tests", "Combined tests", "A table of expressions with expected values and a list of expressions that must be rejected", {"calc_tests.cpp"}, {"evaluator"}, 2},
        };
    } else {
        std::fprintf(stderr, "mock-planner: no plan for task '%s' (known: calculator-basic, calculator-scientific)\n", task.c_str());
        return 3;
    }
    events::metric("plan.items", static_cast<double>(plan.items.size()));
    if (!writeFileAtomic(out, plan.toJson())) { std::fprintf(stderr, "mock-planner: cannot write %s\n", out.c_str()); return 1; }
    events::progress(1, 1, "planned");
    std::printf("planned %zu item(s) for %s\n", plan.items.size(), task.c_str());
    return 0;
}

// ---- agent ----------------------------------------------------------------------------------------------------------
int runMockAgent(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir");
    const std::string itemId = one(a, "item"), tier = one(a, "tier", "local");
    const int attempt = std::atoi(one(a, "attempt", "1").c_str());
    const int delayMs = std::atoi(one(a, "delay-ms", "120").c_str());
    std::string error;
    auto plan = loadPlan(dir, error);
    if (!plan) { std::fprintf(stderr, "mock-agent: %s\n", error.c_str()); return 1; }
    const WorkItem* item = plan->find(itemId);
    if (!item) { std::fprintf(stderr, "mock-agent: no item '%s' in the plan\n", itemId.c_str()); return 1; }

    // Failure injection: "item" fails every local attempt, "item:N" the first N attempts; the frontier-stub tier is never broken.
    bool buggy = false;
    if (tier != "frontier-stub")
        if (auto inj = a.find("inject"); inj != a.end())
            for (const auto& spec : inj->second) {
                const auto colon = spec.find(':');
                const std::string name = spec.substr(0, colon);
                const int limit = colon == std::string::npos ? 1 << 30 : std::atoi(spec.c_str() + colon + 1);
                if (name == itemId && attempt <= limit) buggy = true;
            }
    const std::string bugFile = calculatorBugTarget(itemId);
    const fs::path out = dir / "items" / itemId / ("attempt-" + std::to_string(attempt)) / "out";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);

    double tokensIn = 200.0 + static_cast<double>(item->goal.size()) / 4.0, tokensOut = 0;
    events::metric("tokens.in", tokensIn);
    long done = 0;
    const long total = static_cast<long>(item->files.size());
    events::progress(0, total, "writing");
    for (const auto& f : item->files) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        const bool useBug = buggy && f == bugFile;
        std::string text = calculatorFile(f, useBug);
        if (text.empty()) { std::fprintf(stderr, "mock-agent: no template for '%s' (or its defect no longer matches)\n", f.c_str()); return 1; }
        if (!writeFileAtomic(out / f, text)) { std::fprintf(stderr, "mock-agent: cannot write %s\n", f.c_str()); return 1; }
        tokensOut += static_cast<double>(text.size()) / 4.0;
        events::metric("tokens.out", tokensOut);
        events::progress(++done, total, "writing");
        std::printf("wrote %s (%zu bytes)%s\n", f.c_str(), text.size(), useBug ? " [injected defect]" : "");
    }
    json::Writer w;
    w.beginObject().kv("item", itemId).kv("tier", tier).kv("attempt", attempt).kvBool("injectedDefect", buggy).endObject();
    writeFileAtomic(out.parent_path() / "agent.json", w.take());
    return 0;
}

// ---- gate -----------------------------------------------------------------------------------------------------------
int runSandboxGate(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir");
    const std::string itemId = one(a, "item");
    const int attempt = std::atoi(one(a, "attempt", "1").c_str());
    std::string error;
    auto plan = loadPlan(dir, error);
    if (!plan) { std::printf("GATE FAIL: %s\n", error.c_str()); return 1; }
    const WorkItem* item = plan->find(itemId);
    if (!item) { std::printf("GATE FAIL: no item '%s'\n", itemId.c_str()); return 1; }
    const fs::path attemptDir = dir / "items" / itemId / ("attempt-" + std::to_string(attempt));
    const fs::path out = attemptDir / "out";

    events::progress(0, 3, "allowlist");
    std::set<std::string> allowed(item->files.begin(), item->files.end());
    for (auto& e : fs::recursive_directory_iterator(out)) {
        if (!e.is_regular_file()) continue;
        const std::string rel = fs::relative(e.path(), out).generic_string();
        if (!allowed.count(rel)) { std::printf("GATE FAIL: '%s' is outside the item's editable files\n", rel.c_str()); return 1; }
    }
    for (const auto& f : item->files)
        if (!fs::exists(out / f)) { std::printf("GATE FAIL: expected file '%s' was not written\n", f.c_str()); return 1; }

    std::string testFile;
    if (allowed.count("test_" + itemId + ".cpp")) testFile = "test_" + itemId + ".cpp";
    else if (allowed.count("calc_tests.cpp")) testFile = "calc_tests.cpp";
    else { std::printf("GATE FAIL: the item has no test file\n"); return 1; }

    std::vector<std::string> cmd = {compiler(), "-std=c++23", "-O0", "-Wall", "-Werror", "-I", out.string()};
    for (const auto& dep : closure(*plan, itemId)) { cmd.push_back("-I"); cmd.push_back((dir / "items" / dep / "accepted").string()); }
    const fs::path bin = attemptDir / "gate.bin";
    cmd.insert(cmd.end(), {(out / testFile).string(), "-o", bin.string()});
    events::progress(1, 3, "compile");
    CmdResult c = runCommand(cmd, dir, 100);
    if (c.exitCode != 0) { std::printf("GATE FAIL: compile %s\n%s\n", c.timedOut ? "timed out" : "failed", tail(c.output).c_str()); return 1; }
    events::progress(2, 3, "run");
    CmdResult t = runCommand({bin.string()}, dir, 60);
    if (t.exitCode != 0) { std::printf("GATE FAIL: test %s (exit %d)\n%s\n", t.timedOut ? "timed out" : "failed", t.exitCode, tail(t.output).c_str()); return 1; }

    const fs::path acc = dir / "items" / itemId / "accepted";
    std::error_code ec;
    fs::remove_all(acc, ec);
    fs::create_directories(acc, ec);
    fs::copy(out, acc, fs::copy_options::recursive, ec);
    if (ec) { std::printf("GATE FAIL: cannot publish accepted files: %s\n", ec.message().c_str()); return 1; }
    json::Writer w;
    w.beginObject().kv("item", itemId).kv("attempt", attempt).endObject();
    writeFileAtomic(dir / "items" / itemId / "accepted.json", w.take());
    events::progress(3, 3, "accepted");
    std::printf("GATE PASS: %s attempt %d\n%s", itemId.c_str(), attempt, tail(t.output, 300).c_str());
    return 0;
}

// ---- integrator -----------------------------------------------------------------------------------------------------
int runSandboxIntegrate(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir");
    std::string error;
    auto plan = loadPlan(dir, error);
    if (!plan) { std::printf("INTEGRATION FAIL: %s\n", error.c_str()); return 1; }
    const fs::path project = dir / "project";
    std::error_code ec;
    fs::remove_all(project, ec);
    fs::create_directories(project, ec);
    events::progress(0, 4, "merge");
    for (const auto& item : plan->items) {
        const fs::path acc = dir / "items" / item.id / "accepted";
        if (!fs::exists(acc)) { std::printf("INTEGRATION FAIL: item '%s' has no accepted output\n", item.id.c_str()); return 1; }
        for (const auto& f : item.files) {
            if (fs::exists(project / f)) { std::printf("INTEGRATION FAIL: '%s' provided twice\n", f.c_str()); return 1; }
            fs::create_directories((project / f).parent_path(), ec);
            fs::copy_file(acc / f, project / f, ec);
            if (ec) { std::printf("INTEGRATION FAIL: cannot merge '%s': %s\n", f.c_str(), ec.message().c_str()); return 1; }
        }
    }
    const std::string cxx = compiler();
    events::progress(1, 4, "build");
    CmdResult b1 = runCommand({cxx, "-std=c++23", "-O1", "-Wall", "-Werror", "-I", project.string(), (project / "main.cpp").string(), "-o", (project / "calc").string()}, dir, 200);
    if (b1.exitCode != 0) { std::printf("INTEGRATION FAIL: building calc\n%s\n", tail(b1.output).c_str()); return 1; }
    CmdResult b2 = runCommand({cxx, "-std=c++23", "-O1", "-Wall", "-Werror", "-I", project.string(), (project / "calc_tests.cpp").string(), "-o", (project / "calc_tests").string()}, dir, 200);
    if (b2.exitCode != 0) { std::printf("INTEGRATION FAIL: building calc_tests\n%s\n", tail(b2.output).c_str()); return 1; }
    events::progress(2, 4, "tests");
    CmdResult t = runCommand({(project / "calc_tests").string()}, dir, 60);
    if (t.exitCode != 0) { std::printf("INTEGRATION FAIL: combined tests\n%s\n", tail(t.output).c_str()); return 1; }
    events::progress(3, 4, "smoke");
    struct Smoke { std::vector<std::string> args; std::string expect; };
    const std::vector<Smoke> smoke = {{{"2+3*4"}, "14\n"}, {{"2^3^2"}, "512\n"}, {{"--deg", "sin(30)"}, "0.5\n"}, {{"5!"}, "120\n"}};
    for (const auto& s : smoke) {
        std::vector<std::string> cmd = {(project / "calc").string()};
        cmd.insert(cmd.end(), s.args.begin(), s.args.end());
        CmdResult r = runCommand(cmd, dir, 20);
        if (r.exitCode != 0 || r.output != s.expect) { std::printf("INTEGRATION FAIL: smoke '%s' printed '%s'\n", s.args.back().c_str(), r.output.c_str()); return 1; }
    }
    events::progress(4, 4, "integrated");
    std::printf("INTEGRATION PASS: %zu items merged, calc built, %s", plan->items.size(), t.output.c_str());
    return 0;
}

}  // namespace sub0squad
