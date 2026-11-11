#include "sub0squad/harness_common.hpp"

#include "sub0squad/event_channel.hpp"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

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
std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
bool writeFile(const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << text;
    return static_cast<bool>(o);
}


Cmd run(const std::vector<std::string>& argv, const fs::path& cwd, int timeoutSec)
{
    Cmd r;
    int fds[2];
    if (::pipe(fds) != 0) return r;
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(fds[1], 1); ::dup2(fds[1], 2); ::close(fds[0]); ::close(fds[1]);
        if (::chdir(cwd.c_str()) != 0) _exit(126);
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
        if (::poll(&p, 1, static_cast<int>(left)) <= 0) continue;
        const ssize_t n = ::read(fds[0], buf, sizeof buf);
        if (n <= 0) break;
        r.output.append(buf, static_cast<std::size_t>(n));
        if (r.output.size() > 512 * 1024) r.output.erase(0, 256 * 1024);
    }
    ::close(fds[0]);
    int st = 0;
    ::waitpid(pid, &st, 0);
    r.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
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
    const auto fail = [&](const std::string& why) {
        const std::string msg = "GATE FAIL: " + why + "\n";
        writeFile(attemptDir / "gate.txt", msg);
        std::fputs(msg.c_str(), stdout);
        return 1;
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
    for (const auto& f : hooks.scaffold()) writeFile(ws / f.path, f.content);
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
            if (!now.empty() && now == before && tap.tests >= 5 && tap.fail * 100 <= tap.tests * 40) return 5;  // the same few tests fail the same way: the tests are suspect
        }
        return tap.tests >= 10 && tap.fail * 100 <= tap.tests * 15 ? 3 : code;  // 3: a near pass, repaired rather than divided
    }
    const bool publish = one(a, "no-publish", "").empty();  // the optimiser judges attempts without touching the accepted outputs
    if (publish) {
        fs::remove_all(dir / "items" / itemId / "accepted", ec);
        if (coordinator) fs::create_directories(dir / "items" / itemId / "accepted", ec); else copyTree(out, dir / "items" / itemId / "accepted");
        writeFile(dir / "items" / itemId / "accepted.json", "{\"item\":\"" + itemId + "\",\"attempt\":" + std::to_string(attempt) + "}");
    }
    writeFile(attemptDir / "gate.txt", "GATE PASS\n");
    events::progress(4, 4, "accepted");
    std::printf("GATE PASS: %s attempt %d (%d tests, %d failing against stubs where expected)\n", itemId.c_str(), attempt, tap.tests, tap.fail);
    return 0;
}

}  // namespace sub0squad::harnessutil
