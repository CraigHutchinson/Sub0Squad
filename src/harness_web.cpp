#include "sub0squad/harness_web.hpp"

#include "json_util.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/plan.hpp"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

// ---- the blueprint: a plan-2 written by hand, standing in for a frontier architect -----------------------------------------------
const char* kCalculatorWeb = R"JSON({
  "schema": "plan-2",
  "task": "calculator-web",
  "harness": "web-static",
  "project": "sci-calc",
  "reasons": [
    "task is a web page with testable logic: the engine and the page are separate deliverables with a clear interface",
    "triage: complexity 3, known blueprint, no architect or project manager needed",
    "test-first: a tester item writes tests from the contract, the developer item is graded by tests it did not write",
    "decision: split into 4 items (2 testers, 2 developers)"
  ],
  "items": [
    {
      "id": "eval-tests", "title": "Engine tests", "persona": "tester", "complexity": 2,
      "goal": "Write tests for the calculator expression engine before it exists",
      "files": ["tests/eval.test.mjs"], "deps": [],
      "card": {
        "context": "A developer will implement the engine against your tests, so they must be correct and specific.",
        "contract": "src/calc.js exports evaluate(expr, options) returning a number. expr is a string; options.degrees (boolean, default false) makes sin cos tan take degrees and asin acos atan return degrees. Operators: + - * / % ^ (right associative; binds tighter than unary minus, so -2^2 is -4), postfix ! (factorial), parentheses, unary + and -. Numbers: 12, 1.5, .5, 1.5e2. Constants: pi e tau. One-argument functions: sin cos tan asin acos atan sinh cosh tanh ln log (base 10) log2 sqrt cbrt abs exp floor ceil round fact. Two-argument functions: pow min max. Invalid input must throw an Error: division or modulo by zero, unknown names or functions, wrong argument count, sqrt of a negative, unbalanced parentheses, a trailing operator, unexpected characters.",
        "acceptance": [
          "uses node:test and node:assert/strict and imports { evaluate } from '../src/calc.js'",
          "at least 10 test cases: operators, precedence, ^ associativity, factorial, decimals, constants, radians and degrees, logs, two-argument functions, error cases",
          "floating point results are compared with a tolerance of 1e-9, never with strict equality",
          "tests do not depend on each other"
        ],
        "include": [], "notes": ["Output only tests/eval.test.mjs"]
      },
      "gate": { "kind": "node-test", "tests": ["tests/eval.test.mjs"], "minTests": 8, "mustFailOnStub": true }
    },
    {
      "id": "engine", "title": "Expression engine", "persona": "developer", "complexity": 3,
      "goal": "Implement the expression engine so the provided tests pass",
      "files": ["src/calc.js"], "deps": ["eval-tests"],
      "card": {
        "context": "Replace the stub src/calc.js with a real implementation; the tests below are the specification.",
        "contract": "export function evaluate(expr, options = {}) returning a number; options.degrees switches trig functions to degrees. Support + - * / % ^ (right associative, tighter than unary minus), postfix !, parentheses, constants pi e tau, functions sin cos tan asin acos atan sinh cosh tanh ln log log2 sqrt cbrt abs exp floor ceil round fact, and pow min max. Throw an Error with a clear message on any invalid input.",
        "acceptance": ["every test in tests/eval.test.mjs passes", "plain ES module, no dependencies, no eval() and no new Function()"],
        "include": ["tests/eval.test.mjs"],
        "notes": ["A small recursive descent parser works well: expression -> term -> unary -> power -> postfix -> primary", "Output only src/calc.js"]
      },
      "gate": { "kind": "node-test", "tests": ["tests/eval.test.mjs"], "minTests": 8, "mustFailOnStub": false }
    },
    {
      "id": "ui-tests", "title": "Page tests", "persona": "tester", "complexity": 2,
      "goal": "Write tests for the calculator page's behaviour using the harness fake DOM",
      "files": ["tests/ui.test.mjs"], "deps": [],
      "card": {
        "context": "A developer will build the page against your tests. The fake DOM replaces a browser.",
        "contract": "src/ui.js exports mount(document). It uses #expr (input, .value), #result (output, .textContent), #degrees (checkbox, .checked) and every button[data-key] from document.querySelectorAll('button[data-key]'). Clicking a button appends its data-key to #expr.value, except: 'C' clears #expr and #result; 'DEL' removes the last character of #expr; '=' evaluates #expr.value with evaluate(value, { degrees: #degrees.checked }) from ./calc.js and sets #result.textContent to the number, or to 'Error: ' + message if it throws. A keydown event with key 'Enter' on #expr also evaluates. index.html contains elements with ids expr, result and degrees, a button with data-key=\"=\", and <script type=\"module\" src=\"src/ui.js\">. The harness file tests/helpers/dom-stub.mjs exports createDom() returning { document, expr, result, degrees, buttons, press(key), keydown(key) }.",
        "acceptance": [
          "imports createDom from './helpers/dom-stub.mjs', mount from '../src/ui.js', and uses node:test and node:assert/strict",
          "at least 6 tests: building an expression, equals, clear, delete, error display, the Enter key, degrees mode, and one that reads index.html with node:fs and checks the required ids",
          "each test creates its own dom with createDom() and calls mount(dom.document)"
        ],
        "include": [], "notes": ["Output only tests/ui.test.mjs"]
      },
      "gate": { "kind": "node-test", "tests": ["tests/ui.test.mjs"], "minTests": 6, "mustFailOnStub": true }
    },
    {
      "id": "ui", "title": "Calculator page", "persona": "developer", "complexity": 2,
      "goal": "Build the page, its script and its style so the provided tests pass and it works in a browser",
      "files": ["index.html", "src/ui.js", "style.css"], "deps": ["ui-tests", "engine"],
      "card": {
        "context": "Replace the stubs index.html and src/ui.js; add style.css. The tests below are the specification.",
        "contract": "src/ui.js: export function mount(document) as described in the tests; import { evaluate } from './calc.js'; at the end call mount(document) when typeof document !== 'undefined'. index.html: a page with <input id=\"expr\">, <output id=\"result\">, <input type=\"checkbox\" id=\"degrees\"> with a label, and a button for each key as <button data-key=\"KEY\">KEY</button>, then <script type=\"module\" src=\"src/ui.js\"></script>, and <link rel=\"stylesheet\" href=\"style.css\">. Keys: 0-9 . + - * / % ^ ( ) ! pi e sin( cos( tan( asin( acos( atan( ln( log( sqrt( C DEL =",
        "acceptance": ["every test in tests/ui.test.mjs passes", "the page works in a browser served by node serve.mjs", "style.css is simple and readable, no frameworks"],
        "include": ["tests/ui.test.mjs"],
        "notes": ["Format the result with Number(value.toPrecision(12)) so 0.1+0.2 shows 0.3", "Output index.html, src/ui.js and style.css"]
      },
      "gate": { "kind": "node-test", "tests": ["tests/ui.test.mjs"], "minTests": 6, "mustFailOnStub": false }
    }
  ]
}
)JSON";

// ---- helpers --------------------------------------------------------------------------------------------------------------------
std::map<std::string, std::vector<std::string>> parseArgs(const std::vector<std::string>& args)
{
    std::map<std::string, std::vector<std::string>> m;
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i].rfind("--", 0) == 0) { m[args[i].substr(2)].push_back(args[i + 1]); ++i; }
    return m;
}
std::string one(const std::map<std::string, std::vector<std::string>>& m, const std::string& k, const std::string& d = {})
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

struct Cmd { int exitCode = -1; bool timedOut = false; std::string output; };

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

struct Tap { int tests = -1, pass = 0, fail = 0; std::string excerpt; };

Tap parseTap(const std::string& out)
{
    // Counts come from the summary lines. The excerpt lists LEAF failures (nested subtests too) with their first error lines; a parent whose
    // only message is "N subtests failed" adds nothing for a model that has to fix the code.
    Tap t;
    struct Failure { std::string name, error; };
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
        if (tl.rfind("not ok ", 0) == 0) { failures.push_back({tl.substr(7), ""}); inError = false; errLines = 0; continue; }
        if (failures.empty()) continue;
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

}  // namespace

std::string blueprintJson(const std::string& name) { return name == "calculator-web" ? std::string{kCalculatorWeb} : std::string{}; }

// ---- gate -----------------------------------------------------------------------------------------------------------------------
int runHarnessGate(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir");
    const std::string itemId = one(a, "item");
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

    events::progress(0, 3, "allowlist");
    std::set<std::string> allowed(item->files.begin(), item->files.end());
    if (!fs::exists(out)) return fail("the agent wrote nothing");
    for (auto& e : fs::recursive_directory_iterator(out)) {
        if (!e.is_regular_file()) continue;
        const std::string rel = fs::relative(e.path(), out).generic_string();
        if (!allowed.count(rel)) return fail("'" + rel + "' is outside the item's editable files (" + item->files.front() + "...)");
    }
    for (const auto& f : item->files) if (!fs::exists(out / f) || fs::file_size(out / f) == 0) return fail("expected file '" + f + "' is missing or empty");

    // Workspace: harness scaffold and stubs, then accepted outputs of dependencies, then this attempt's files.
    const fs::path ws = attemptDir / "ws";
    std::error_code ec;
    fs::remove_all(ws, ec);
    for (const auto& f : webScaffold()) writeFile(ws / f.path, f.content);
    for (const auto& dep : closure(*plan, itemId)) copyTree(dir / "items" / dep / "accepted", ws);
    copyTree(out, ws);

    events::progress(1, 3, "test");
    std::vector<std::string> cmd = {"node", "--test", "--test-reporter=tap"};
    for (const auto& t : item->gate.tests) {
        if (!fs::exists(ws / t)) return fail("gate test file '" + t + "' does not exist in the workspace");
        cmd.push_back(t);
    }
    const Cmd r = run(cmd, ws, 90);
    const Tap tap = parseTap(r.output);
    if (r.timedOut) return fail("tests timed out");
    events::progress(2, 3, "judge");
    if (tap.tests < 0) return fail("the test runner produced no result:\n" + r.output.substr(0, 700));
    if (tap.tests < item->gate.minTests) return fail("only " + std::to_string(tap.tests) + " test(s) ran, at least " + std::to_string(item->gate.minTests) + " are required\n" + tap.excerpt);
    if (item->gate.mustFailOnStub) {
        // A tester's tests must be about something real: against the stubs most of them must fail.
        if (tap.fail * 10 < tap.tests * 7) return fail("only " + std::to_string(tap.fail) + " of " + std::to_string(tap.tests) + " tests fail against the stub: they do not exercise the contract");
    } else if (tap.fail != 0 || r.exitCode != 0) {
        return fail(std::to_string(tap.fail) + " of " + std::to_string(tap.tests) + " tests failed\n" + tap.excerpt);
    }
    const bool publish = one(a, "no-publish").empty();  // the optimiser judges attempts without touching the accepted outputs
    if (publish) { fs::remove_all(dir / "items" / itemId / "accepted", ec); copyTree(out, dir / "items" / itemId / "accepted"); }
    if (publish) writeFile(dir / "items" / itemId / "accepted.json", "{\"item\":\"" + itemId + "\",\"attempt\":" + std::to_string(attempt) + "}");
    writeFile(attemptDir / "gate.txt", "GATE PASS\n");
    events::progress(3, 3, "accepted");
    std::printf("GATE PASS: %s attempt %d (%d tests, %d failing against stubs where expected)\n", itemId.c_str(), attempt, tap.tests, tap.fail);
    return 0;
}

// ---- integrator -----------------------------------------------------------------------------------------------------------------
int runHarnessIntegrate(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir");
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) { std::printf("INTEGRATION FAIL: %s\n", plan.error().c_str()); return 1; }
    const fs::path project = dir / "project";
    std::error_code ec;
    fs::remove_all(project, ec);
    events::progress(0, 3, "merge");
    for (const auto& f : webScaffold()) writeFile(project / f.path, f.content);
    for (const auto& it : plan->items) {
        const fs::path acc = dir / "items" / it.id / "accepted";
        if (!fs::exists(acc)) { std::printf("INTEGRATION FAIL: item '%s' has no accepted output\n", it.id.c_str()); return 1; }
        copyTree(acc, project);
    }
    events::progress(1, 3, "test");
    const Cmd t = run({"node", "--test", "--test-reporter=tap", "tests/"}, project, 120);
    const Tap tap = parseTap(t.output);
    if (t.exitCode != 0 || tap.fail != 0 || tap.tests < 1) { std::printf("INTEGRATION FAIL: the whole suite: %d of %d failed\n%s\n", tap.fail, tap.tests, tap.excerpt.c_str()); return 1; }
    events::progress(2, 3, "serve");
    const char* script = "import { startServer } from './serve.mjs';"
                         "const s = await startServer(0);"
                         "const base = 'http://127.0.0.1:' + s.address().port;"
                         "let bad = [];"
                         "for (const p of ['/', '/src/calc.js', '/src/ui.js', '/style.css']) {"
                         "  const r = await fetch(base + p); if (r.status !== 200) bad.push(p + ' -> ' + r.status); }"
                         "const html = await (await fetch(base + '/')).text();"
                         "if (!html.includes('src/ui.js')) bad.push('index.html does not load src/ui.js');"
                         "const t = await fetch(base + '/tests/ui.test.mjs'); if (t.status !== 404) bad.push('tests are served');"
                         "s.close(); if (bad.length) { console.log(bad.join('; ')); process.exit(1); } console.log('served ok');";
    const Cmd s = run({"node", "--input-type=module", "-e", script}, project, 30);
    if (s.exitCode != 0) { std::printf("INTEGRATION FAIL: serve check: %s\n", s.output.substr(0, 500).c_str()); return 1; }
    events::progress(3, 3, "integrated");
    std::printf("INTEGRATION PASS: %zu items merged, %d tests passed, page served (try: cd %s && node serve.mjs 8080)\n", plan->items.size(), tap.pass, project.c_str());
    return 0;
}

}  // namespace sub0squad
