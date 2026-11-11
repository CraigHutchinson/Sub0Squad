#include "sub0squad/harness_cpp.hpp"

#include "json_util.hpp"
#include "sub0squad/event_channel.hpp"
#include "sub0squad/harness_common.hpp"
#include "sub0squad/plan.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;
using namespace harnessutil;

namespace {

const char* compiler()
{
    const char* cxx = std::getenv("CXX");
    return cxx && *cxx ? cxx : "g++";
}

/// The compiler's diagnostics, cheapest to read: each `error:` line with the source line and caret that follow it, at most `maxErrors`.
std::string diagnostics(const std::string& output, int maxErrors)
{
    std::istringstream in(output);
    std::string line, shown;
    int errors = 0, context = 0;
    while (std::getline(in, line)) {
        if (line.find("undefined reference to") != std::string::npos) {  // a link error: the symbol is the evidence ("collect2: error: ld returned 1" says nothing)
            if (errors++ >= maxErrors) break;
            shown += "  " + line.substr(0, 240) + "\n";
        } else if (line.find(" error: ") != std::string::npos || line.find(" error:") != std::string::npos) {
            if (errors++ >= maxErrors) break;
            context = 2;
            shown += "  " + line.substr(0, 240) + "\n";
        } else if (context > 0) {
            --context;
            shown += "  " + line.substr(0, 160) + "\n";
        }
    }
    if (shown.empty()) shown = "  " + output.substr(0, 600) + "\n";
    return shown;
}

/// `tests/eval.test.cpp` -> `test_eval`, the CMake target the harness's CMakeLists.txt gives that file.
std::string testTarget(const std::string& testFile)
{
    const std::string base = fs::path(testFile).filename().string();
    return "test_" + base.substr(0, base.find('.'));
}

struct Build { bool ok = false; std::string log; };

Build configureAndBuild(const fs::path& src, const fs::path& build, const std::vector<std::string>& targets, bool sanitize, int timeoutSec)
{
    Build b;
    std::vector<std::string> cfg = {"cmake", "-S", src.string(), "-B", build.string(), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug", std::string("-DCMAKE_CXX_COMPILER=") + compiler()};
    if (sanitize) cfg.push_back("-DSANITIZE=ON");
    const Cmd c = run(cfg, src, 60);
    if (c.exitCode != 0) { b.log = "cmake configure failed:\n" + c.output.substr(0, 800); return b; }
    std::vector<std::string> bld = {"cmake", "--build", build.string()};
    if (!targets.empty()) { bld.push_back("--target"); for (const auto& t : targets) bld.push_back(t); }
    const Cmd r = run(bld, src, timeoutSec);
    if (r.timedOut) { b.log = "the build timed out"; return b; }
    if (r.exitCode != 0) { b.log = diagnostics(r.output, 6); return b; }
    b.ok = true;
    return b;
}

}  // namespace

// ---- gate: the shared flow with C++ hooks -------------------------------------------------------------------------------------------
namespace {

std::string cppPrecheck(const fs::path& out, const fs::path& ws)
{
    // The cheapest check that names a file and a line: does every translation unit the attempt wrote compile on its own?
    std::string syntax;
    for (auto& e : fs::recursive_directory_iterator(out)) {
        if (!e.is_regular_file() || e.path().extension() != ".cpp") continue;
        const std::string rel = fs::relative(e.path(), out).generic_string();
        const Cmd c = run({compiler(), "-std=c++20", "-fsyntax-only", "-Wall", "-Wextra", "-Werror", "-Iinclude", "-Isrc", "-Itests", "-DCALC_CLI_PATH=\"x\"", rel}, ws, 60);
        if (c.exitCode != 0) syntax += "does not compile: " + rel + "\n" + diagnostics(c.output, 4);
    }
    return syntax.empty() ? syntax : syntax + "The code does not compile, so no test could run. Fix the errors first.";
}

std::string cppBuildAndRun(const fs::path& ws, const std::vector<std::string>& tests, Tap& total, bool& timedOut)
{
    std::vector<std::string> targets;
    for (const auto& t : tests) targets.push_back(testTarget(t));
    const Build build = configureAndBuild(ws, ws / "build", targets, false, 90);
    if (!build.ok) return "the build failed:\n" + build.log;
    total.tests = 0;
    bool ran = false;
    for (const auto& t : targets) {
        const Cmd r = run({(ws / "build" / t).string()}, ws, 60);
        timedOut = timedOut || r.timedOut;
        const Tap one1 = parseTap(r.output);
        if (one1.tests < 0) { total.excerpt += "the test program " + t + " produced no result (it crashed or hung):\n" + r.output.substr(0, 600) + "\n"; continue; }
        ran = true;
        total.tests += one1.tests; total.pass += one1.pass; total.fail += one1.fail;
        total.excerpt += one1.excerpt;
    }
    return ran ? std::string{} : total.excerpt;
}

}  // namespace

int runCppGate(const std::vector<std::string>& args)
{
    static const GateHooks hooks = {&cppScaffold, &cppPrecheck, &cppBuildAndRun};
    return runGateFlow(hooks, args);
}

// ---- integrator -------------------------------------------------------------------------------------------------------------------
int runCppIntegrate(const std::vector<std::string>& args)
{
    const auto a = parseArgs(args);
    const fs::path dir = one(a, "workflow-dir");
    auto plan = Plan::fromJson(slurp(dir / "plan.json"));
    if (!plan) { std::printf("INTEGRATION FAIL: %s\n", plan.error().c_str()); return 1; }
    const fs::path project = dir / "project";
    std::error_code ec;
    fs::remove_all(project, ec);
    events::progress(0, 4, "merge");
    for (const auto& f : cppScaffold()) writeFile(project / f.path, f.content);
    for (const auto& it : plan->items) {
        const fs::path acc = dir / "items" / it.id / "accepted";
        if (!fs::exists(acc)) { std::printf("INTEGRATION FAIL: item '%s' has no accepted output\n", it.id.c_str()); return 1; }
        copyTree(acc, project);
    }

    events::progress(1, 4, "build");
    const Build build = configureAndBuild(project, project / "build", {}, true, 240);  // sanitizers on: a leak or an out-of-bounds read fails the integration
    if (!build.ok) { std::printf("INTEGRATION FAIL: the merged project does not build:\n%s\n", build.log.c_str()); return 1; }

    events::progress(2, 4, "test");
    Tap all;
    all.tests = 0;
    for (auto& e : fs::directory_iterator(project / "tests")) {
        if (!e.is_regular_file() || e.path().filename().string().find(".test.cpp") == std::string::npos) continue;
        const std::string target = testTarget(e.path().filename().string());
        const Cmd r = run({(project / "build" / target).string()}, project, 120);
        const Tap t = parseTap(r.output);
        if (r.exitCode != 0 || t.tests < 1 || t.fail != 0) {
            std::printf("INTEGRATION FAIL: %s: %d of %d failed (exit %d)\n%s\n", target.c_str(), t.fail, t.tests, r.exitCode, t.tests < 1 ? r.output.substr(0, 800).c_str() : t.excerpt.c_str());
            return 1;
        }
        all.tests += t.tests; all.pass += t.pass;
    }

    events::progress(3, 4, "run");
    const fs::path app = project / "build" / "calc_cli";
    const Cmd smoke = run({app.string(), "2^3^2"}, project, 20);
    if (smoke.exitCode != 0 || smoke.output.find("512") == std::string::npos) { std::printf("INTEGRATION FAIL: the app gave '%s' for 2^3^2 (exit %d)\n", smoke.output.substr(0, 200).c_str(), smoke.exitCode); return 1; }
    const Cmd frame = run({app.string(), "--screen", "2^3^2"}, project, 20);
    if (frame.exitCode != 0 || frame.output.empty()) { std::printf("INTEGRATION FAIL: the app's --screen frame is empty (exit %d)\n", frame.exitCode); return 1; }
    writeFile(dir / "capture.txt", frame.output);

    events::progress(4, 4, "integrated");
    {
        std::vector<std::string> files;
        for (auto& e : fs::recursive_directory_iterator(project / "src")) if (e.is_regular_file()) files.push_back(fs::relative(e.path(), project).generic_string());
        for (auto& e : fs::recursive_directory_iterator(project / "tests")) if (e.is_regular_file()) files.push_back(fs::relative(e.path(), project).generic_string());
        std::sort(files.begin(), files.end());
        std::string md = "# " + (plan->project.empty() ? std::string{"Project"} : plan->project) + "\n\nRequest: " + plan->task + "\n\n## What was built\n\n| item | persona | files |\n|---|---|---|\n";
        for (const auto& it : plan->items) {
            md += "| " + it.id + " | " + it.persona + " | ";
            for (std::size_t i = 0; i < it.files.size(); ++i) md += (i ? ", " : "") + it.files[i];
            md += " |\n";
        }
        md += "\n## Verification\n\n" + std::to_string(all.pass) + " tests passed over the merged project, built with AddressSanitizer and UBSan; the app ran a smoke expression.\n\n## The app's screen (capture)\n\n```\n" + frame.output + "```\n\n## Run it\n\n```sh\ncd project\ncmake -S . -B build -G Ninja && cmake --build build\nbuild/calc_cli '2^3^2'\n```\n";
        if (!plan->reasons.empty()) { md += "\n## Why this split\n\n"; for (const auto& r : plan->reasons) md += "- " + r + "\n"; }
        writeFile(dir / "report.md", md);
        json::Writer w;
        w.beginObject().kv("schema", "deliverables-1").kv("summary", "all " + std::to_string(plan->items.size()) + " items accepted; " + std::to_string(all.pass) + " tests pass").key("items").beginArray();
        w.beginObject().kv("id", "app").kv("kind", "native-app").kv("title", "The command-line app").kv("path", "project/build/calc_cli").kv("open", "file").kv("run", "cd project && cmake -S . -B build -G Ninja && cmake --build build && build/calc_cli '2^3^2'").endObject();
        w.beginObject().kv("id", "capture").kv("kind", "screen-capture").kv("title", "The app's screen").kv("path", "capture.txt").kv("open", "file").endObject();
        w.beginObject().kv("id", "report").kv("kind", "report").kv("title", "Final report").kv("path", "report.md").kv("open", "file").endObject();
        w.beginObject().kv("id", "source").kv("kind", "source-tree").kv("title", "Project sources (" + std::to_string(files.size()) + " files)").kv("path", "project/").kv("open", "tree").endObject();
        w.endArray().endObject();
        writeFile(dir / "deliverables.json", w.take());
    }
    std::printf("INTEGRATION PASS: %zu items merged, %d tests passed under ASan+UBSan, app ran (try: cd %s && build/calc_cli '2^3^2')\n", plan->items.size(), all.pass, project.c_str());
    return 0;
}

}  // namespace sub0squad
