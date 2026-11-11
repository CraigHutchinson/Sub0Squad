#include "sub0squad/harness_web.hpp"
#include "sub0squad/harness_common.hpp"
#include "sub0squad/qa.hpp"

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
using namespace harnessutil;

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

}  // namespace

std::string webFailureExcerpt(const std::string& tapOutput) { return parseTap(tapOutput).excerpt; }

std::string blueprintJson(const std::string& name) { return name == "calculator-web" ? std::string{kCalculatorWeb} : std::string{}; }

// ---- gate: the shared flow with web hooks ----------------------------------------------------------------------------------------
namespace {

std::string webPrecheck(const fs::path& out, const fs::path& ws)
{
    // The cheapest check first: does every file the attempt wrote even parse? `node --check` names the file, the line and the token, where a test run only says
    // "1 test ran" (the first real failures were truncated and mixed-format files that no test could load).
    std::string syntax;
    for (auto& e : fs::recursive_directory_iterator(out)) {
        const std::string ext = e.path().extension().string();
        if (!e.is_regular_file() || (ext != ".js" && ext != ".mjs")) continue;
        const std::string rel = fs::relative(e.path(), out).generic_string();
        const Cmd c = run({"node", "--check", (ws / rel).string()}, ws, 15);
        if (c.exitCode == 0) continue;
        std::istringstream in(c.output);
        std::string line, shown;
        int n = 0;
        while (std::getline(in, line) && n < 6) { if (line.find("node:internal") != std::string::npos || line.empty()) continue; shown += "  " + line.substr(0, 200) + "\n"; ++n; }
        syntax += "syntax error in " + rel + ":\n" + shown;
    }
    return syntax.empty() ? syntax : syntax + "The code does not parse, so no test could run. Fix the syntax first.";
}

std::string webBuildAndRun(const fs::path& ws, const std::vector<std::string>& tests, Tap& tap, bool& timedOut)
{
    std::vector<std::string> cmd = {"node", "--test", "--test-reporter=tap"};
    for (const auto& t : tests) cmd.push_back(t);
    const Cmd r = run(cmd, ws, 90);
    timedOut = r.timedOut;
    tap = parseTap(r.output);
    if (r.timedOut) return {};
    if (tap.tests < 0) return "the test runner produced no result:\n" + r.output.substr(0, 700);
    // A test file that crashes while loading (a read at the top of the file, an import that does not exist) counts as one failed test with the message
    // "test failed": say what the runner actually reported, or the model cannot know what to fix.
    int shown = 0;
    std::istringstream in(r.output);
    for (std::string line; std::getline(in, line) && shown < 4;)
        if (line.find("Error") != std::string::npos || line.find("ENOENT") != std::string::npos || line.find("Cannot find") != std::string::npos) { tap.note += "  " + line.substr(0, 200) + "\n"; ++shown; }
    // A passing near-miss is judged by the shared flow; a run that exited non-zero with no failing test is still a failure.
    if (r.exitCode != 0 && tap.fail == 0) tap.fail = 1;
    return {};
}

std::string webOracle(const GateContext& c, const Tap& tap)
{
    // Test oracle (off unless SUB0SQUAD_ORACLE_DIR names a reference project): the same tests against an independent implementation. Tests the
    // reference fails are suspect (the first real runs lost whole ladders to tests that expected positions off by one). Advisory by default,
    // a gate with SUB0SQUAD_ORACLE_MODE=gate; the verdict is written to oracle.txt for the history either way.
    (void)tap;
    const char* od = std::getenv("SUB0SQUAD_ORACLE_DIR");
    if (!od || !*od || !fs::exists(fs::path(od) / "src")) return {};
    std::error_code ec;
    const fs::path ows = c.attemptDir / "oracle-ws";
    fs::remove_all(ows, ec);
    for (const auto& f : webScaffold()) writeFile(ows / f.path, f.content);
    copyTree(fs::path(od) / "src", ows / "src");
    copyTree(c.out, ows);
    for (auto& e : fs::recursive_directory_iterator(c.out)) {  // keep the tester's own files, not the reference's (the tests are what is being judged)
        if (!e.is_regular_file()) continue;
        const std::string rel = fs::relative(e.path(), c.out).generic_string();
        if (rel.rfind("src/", 0) == 0) fs::copy_file(fs::path(od) / rel, ows / rel, fs::copy_options::overwrite_existing, ec);
    }
    std::vector<std::string> ocmd = {"node", "--test", "--test-reporter=tap"};
    for (const auto& t : c.item->gate.tests) ocmd.push_back(t);
    const Cmd o = run(ocmd, ows, 90);
    const Tap ot = parseTap(o.output);
    std::string verdict;
    if (ot.tests > 0 && ot.fail > 0)
        verdict = "the reference implementation fails " + std::to_string(ot.fail) + " of " + std::to_string(ot.tests) + " of these tests, so they may be wrong:\n" + ot.excerpt;
    writeFile(c.attemptDir / "oracle.txt", verdict.empty() ? "ORACLE AGREES\n" : "ORACLE DISAGREES: " + verdict);
    if (!verdict.empty() && ot.fail * 100 > ot.tests * 15) {
        const char* mode = std::getenv("SUB0SQUAD_ORACLE_MODE");
        if (mode && std::string(mode) == "gate") return "test oracle: " + verdict + "Correct the expectations the reference contradicts, keep those it confirms.";
        std::printf("ORACLE NOTE: %s", verdict.c_str());
    }
    return {};
}

}  // namespace

int runHarnessGate(const std::vector<std::string>& args)
{
    static const GateHooks hooks = {&webScaffold, &webPrecheck, &webBuildAndRun, &webOracle};
    return runGateFlow(hooks, args);
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
    // The deliverables manifest and the final report: what the run produced and how to open it. The web harness decides by rule (the page,
    // the source tree, the report); other harnesses will have a reporter step decide (docs/WORK_GRAPH.md section 12).
    {
        std::vector<std::string> files;
        for (auto& e : fs::recursive_directory_iterator(project)) if (e.is_regular_file()) files.push_back(fs::relative(e.path(), project).generic_string());
        std::sort(files.begin(), files.end());
        std::string md = "# " + (plan->project.empty() ? std::string{"Project"} : plan->project) + "\n\nRequest: " + plan->task + "\n\n## What was built\n\n| item | persona | files |\n|---|---|---|\n";
        for (const auto& it : plan->items) {
            md += "| " + it.id + " | " + it.persona + " | ";
            for (std::size_t i = 0; i < it.files.size(); ++i) md += (i ? ", " : "") + it.files[i];
            md += " |\n";
        }
        md += "\n## Verification\n\n" + std::to_string(tap.pass) + " tests passed over the merged project; the page and its modules were fetched from the static server.\n\n## Run it\n\n```sh\ncd project\nnode --test tests/\nnode serve.mjs 8080   # then open http://127.0.0.1:8080\n```\n";
        {   // Quality (advisory): duplicated blocks, helpers declared twice, long functions. Never a gate; findings go into the report and qa.txt for the next iteration.
            const std::string q = qaText(qaScan(project / "src"));
            writeFile(dir / "qa.txt", q);
            md += "\n## Quality findings (advisory)\n\n" + (q.empty() ? std::string{"none\n"} : "```\n" + q + "```\n");
        }
        if (!plan->reasons.empty()) { md += "\n## Why this split\n\n"; for (const auto& r : plan->reasons) md += "- " + r + "\n"; }
        writeFile(dir / "report.md", md);
        json::Writer w;
        w.beginObject().kv("schema", "deliverables-1").kv("summary", "all " + std::to_string(plan->items.size()) + " items accepted; " + std::to_string(tap.pass) + " tests pass").key("items").beginArray();
        w.beginObject().kv("id", "page").kv("kind", "web-page").kv("title", "The page").kv("path", "project/index.html").kv("open", "site").kv("run", "cd project && node serve.mjs 8080").endObject();
        w.beginObject().kv("id", "report").kv("kind", "report").kv("title", "Final report").kv("path", "report.md").kv("open", "file").endObject();
        w.beginObject().kv("id", "source").kv("kind", "source-tree").kv("title", "Project sources (" + std::to_string(files.size()) + " files)").kv("path", "project/").kv("open", "tree").endObject();
        w.endArray().endObject();
        writeFile(dir / "deliverables.json", w.take());
    }
    std::printf("INTEGRATION PASS: %zu items merged, %d tests passed, page served (try: cd %s && node serve.mjs 8080)\n", plan->items.size(), tap.pass, project.c_str());
    return 0;
}

}  // namespace sub0squad
