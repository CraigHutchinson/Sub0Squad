#include "sub0squad/harness_py.hpp"

#include "json_util.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/plan.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;
using namespace harnessutil;

namespace {

std::string precheck(const fs::path& out, const fs::path& ws)
{
    // The cheapest check that names a file and a line: does every Python file the attempt wrote compile?
    std::string syntax;
    for (auto& e : fs::recursive_directory_iterator(out)) {
        if (!e.is_regular_file() || e.path().extension() != ".py") continue;
        const std::string rel = fs::relative(e.path(), out).generic_string();
        const char* script = "import ast, sys\n"
                             "f = sys.argv[1]\n"
                             "try:\n"
                             "    ast.parse(open(f).read(), f)\n"
                             "except SyntaxError as e:\n"
                             "    print('%s:%s: %s\\n    %s' % (f, e.lineno, e.msg, (e.text or '').strip()))\n"
                             "    sys.exit(1)\n";
        const Cmd c = run({"python3", "-B", "-c", script, rel}, ws, 30);
        if (c.exitCode != 0) syntax += "syntax error in " + c.output.substr(0, 400);
    }
    return syntax.empty() ? syntax : syntax + "The code does not parse, so no test could run. Fix the syntax first.";
}

std::string buildAndRun(const fs::path& ws, const std::vector<std::string>& tests, Tap& total, bool& timedOut)
{
    total.tests = 0;
    bool ran = false;
    for (const auto& t : tests) {
        const Cmd r = run({"python3", "-B", "tests/helpers/tap.py", t}, ws, 60);
        timedOut = timedOut || r.timedOut;
        const Tap one1 = parseTap(r.output);
        if (one1.tests < 0) { total.excerpt += "the test file " + t + " produced no result (it failed while loading or crashed):\n" + r.output.substr(std::min<std::size_t>(r.output.size() > 700 ? r.output.size() - 700 : 0, r.output.size())) + "\n"; continue; }
        ran = true;
        total.tests += one1.tests; total.pass += one1.pass; total.fail += one1.fail;
        total.excerpt += one1.excerpt;
    }
    return ran ? std::string{} : total.excerpt;
}

}  // namespace

namespace {
const GateHooks& pyHooks() { static const GateHooks hooks = {&pyScaffold, &precheck, &buildAndRun}; return hooks; }
}  // namespace

int runPyGate(const std::vector<std::string>& args) { return runGateFlow(pyHooks(), args); }
std::string runPyPrecheck(const std::vector<std::string>& args) { return runPrecheckFlow(pyHooks(), args); }

int runPyIntegrate(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir", "");
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) { std::printf("INTEGRATION FAIL: %s\n", plan.error().c_str()); return 1; }
    const fs::path project = dir / "project";
    std::error_code ec;
    fs::remove_all(project, ec);
    events::progress(0, 3, "merge");
    for (const auto& f : pyScaffold()) writeFile(project / f.path, f.content);
    for (const auto& it : plan->items) {
        const fs::path acc = dir / "items" / it.id / "accepted";
        if (!fs::exists(acc)) { std::printf("INTEGRATION FAIL: item '%s' has no accepted output\n", it.id.c_str()); return 1; }
        copyTree(acc, project);
    }

    events::progress(1, 3, "test");
    Tap all;
    all.tests = 0;
    for (auto& e : fs::directory_iterator(project / "tests")) {
        const std::string name = e.path().filename().string();
        if (!e.is_regular_file() || name.size() < 8 || name.compare(name.size() - 8, 8, "_test.py") != 0) continue;
        // Development mode with warnings as errors: the Python counterpart of building under sanitizers.
        const Cmd r = run({"python3", "-X", "dev", "-W", "error", "-B", "tests/helpers/tap.py", "tests/" + name}, project, 120);
        const Tap t = parseTap(r.output);
        if (r.exitCode != 0 || t.tests < 1 || t.fail != 0) {
            std::printf("INTEGRATION FAIL: %s: %d of %d failed (exit %d)\n%s\n", name.c_str(), t.fail, t.tests, r.exitCode, t.tests < 1 ? r.output.substr(0, 800).c_str() : t.excerpt.c_str());
            return 1;
        }
        all.tests += t.tests; all.pass += t.pass;
    }

    events::progress(2, 3, "run");
    const Cmd smoke = run({"python3", "-B", "-m", "calc", "2^3^2"}, project, 20);
    if (smoke.exitCode != 0 || smoke.output.find("512") == std::string::npos) { std::printf("INTEGRATION FAIL: the app gave '%s' for 2^3^2 (exit %d)\n", smoke.output.substr(0, 200).c_str(), smoke.exitCode); return 1; }
    const Cmd frame = run({"python3", "-B", "-m", "calc", "--screen", "2^3^2"}, project, 20);
    if (frame.exitCode != 0 || frame.output.empty()) { std::printf("INTEGRATION FAIL: the app's --screen frame is empty (exit %d)\n", frame.exitCode); return 1; }
    writeFile(dir / "capture.txt", frame.output);

    events::progress(3, 3, "integrated");
    {
        std::vector<std::string> files;
        for (const char* sub : {"calc", "tests"})
            for (auto& e : fs::recursive_directory_iterator(project / sub)) if (e.is_regular_file() && e.path().extension() == ".py") files.push_back(fs::relative(e.path(), project).generic_string());
        std::sort(files.begin(), files.end());
        std::string md = "# " + (plan->project.empty() ? std::string{"Project"} : plan->project) + "\n\nRequest: " + plan->task + "\n\n## What was built\n\n| item | persona | files |\n|---|---|---|\n";
        for (const auto& it : plan->items) {
            md += "| " + it.id + " | " + it.persona + " | ";
            for (std::size_t i = 0; i < it.files.size(); ++i) md += (i ? ", " : "") + it.files[i];
            md += " |\n";
        }
        md += "\n## Verification\n\n" + std::to_string(all.pass) + " tests passed over the merged project in Python development mode with warnings as errors; the app ran a smoke expression.\n\n## The app's screen (capture)\n\n```\n" + frame.output + "```\n\n## Run it\n\n```sh\ncd project\npython3 -m calc '2^3^2'\n```\n";
        if (!plan->reasons.empty()) { md += "\n## Why this split\n\n"; for (const auto& r : plan->reasons) md += "- " + r + "\n"; }
        writeFile(dir / "report.md", md);
        json::Writer w;
        w.beginObject().kv("schema", "deliverables-1").kv("summary", "all " + std::to_string(plan->items.size()) + " items accepted; " + std::to_string(all.pass) + " tests pass").key("items").beginArray();
        w.beginObject().kv("id", "app").kv("kind", "native-app").kv("title", "The command-line app").kv("path", "project/calc/__main__.py").kv("open", "file").kv("run", "cd project && python3 -m calc '2^3^2'").endObject();
        w.beginObject().kv("id", "capture").kv("kind", "screen-capture").kv("title", "The app's screen").kv("path", "capture.txt").kv("open", "file").endObject();
        w.beginObject().kv("id", "report").kv("kind", "report").kv("title", "Final report").kv("path", "report.md").kv("open", "file").endObject();
        w.beginObject().kv("id", "source").kv("kind", "source-tree").kv("title", "Project sources (" + std::to_string(files.size()) + " files)").kv("path", "project/").kv("open", "tree").endObject();
        w.endArray().endObject();
        writeFile(dir / "deliverables.json", w.take());
    }
    std::printf("INTEGRATION PASS: %zu items merged, %d tests passed (dev mode, warnings as errors), app ran (try: cd %s && python3 -m calc '2^3^2')\n", plan->items.size(), all.pass, project.c_str());
    return 0;
}

}  // namespace sub0squad
