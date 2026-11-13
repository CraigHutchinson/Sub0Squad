// The executors through the DAEMON (catalogue rows 37 to 52): a real daemon, the real CLI and real agent jobs (`llm-agent` on the canvas, `llm-fork`), the model
// replaced by a fake engine that answers from the reference deliverables. The calculator in Python is built end to end with the frontier OFF, so every item
// must be accepted on what the executor itself produced (a stub could rescue nothing), and the integrated project must run. This is the test that was missing:
// the executors had only been driven by the agents' own unit tests, never as a workflow's jobs.
#include "agent_bench.hpp"
#include "e2e.hpp"

#include <thread>

using namespace e2e;
namespace fs = std::filesystem;

static std::string field(const std::string& s, const std::string& key)
{
    const std::string pat = "\"" + key + "\":";
    auto p = s.find(pat);
    if (p == std::string::npos) return {};
    p += pat.size();
    if (s[p] == '"') { auto e = s.find('"', p + 1); return s.substr(p + 1, e - p - 1); }
    return s.substr(p, s.find_first_of(",}", p) - p);
}
static std::string between(const std::string& s, const std::string& from, const std::string& to)
{
    const auto a = s.find(from);
    if (a == std::string::npos) return {};
    const auto b = s.find(to, a + from.size());
    return s.substr(a + from.size(), b == std::string::npos ? std::string::npos : b - a - from.size());
}
static std::vector<std::string> paths(const std::string& list)
{
    std::vector<std::string> v;
    std::size_t at = 0;
    while (at < list.size()) {
        const auto e = list.find(", ", at);
        v.push_back(list.substr(at, e == std::string::npos ? std::string::npos : e - at));
        if (e == std::string::npos) break;
        at = e + 2;
    }
    return v;
}

/// The fake model: answers a file request with the reference file, a skeleton request with the declared reference, a section request with that function.
struct Calculator
{
    struct File { std::string text, declared; std::map<std::string, std::string> functions; };
    std::map<std::string, File> files;
    const File& of(const std::string& path)
    {
        auto it = files.find(path);
        if (it != files.end()) return it->second;
        File f;
        f.text = sub0squad::findHarness("py-unittest")->mockFile(path, false);
        const auto names = bench::sectionNames(f.text);
        f.declared = names.empty() ? f.text : bench::skeletonOf(f.text, names, f.functions);
        return files.emplace(path, std::move(f)).first->second;
    }
    fake::Reply answer(const fake::Request& rq)
    {
        const std::string& ask = rq.last();
        const auto list = paths(between(ask, "Files to write: ", "\n"));
        if (ask.find("Declare the STRUCTURE") != std::string::npos) {
            std::string reply;
            for (const auto& p : list) reply += bench::skeletonBlock(p, of(p).declared);
            return {reply};
        }
        if (const auto own = ask.rfind("You own `"); own != std::string::npos) {
            const std::string name = ask.substr(own + 9, ask.find('`', own + 9) - own - 9);
            std::string file = ask.substr(ask.find(" in ", own) + 4);
            file = file.substr(0, file.find_first_of(" \n"));
            if (!file.empty() && file.back() == '.') file.pop_back();
            const auto& f = of(file);
            const auto it = f.functions.find(name);
            if (it == f.functions.end()) return {"I do not know that function."};
            return {"=== EDIT: " + file + " SYMBOL " + name + " ===\n" + it->second + "=== END EDIT ===\n"};
        }
        std::string reply;
        for (const auto& p : list) reply += "=== FILE: " + p + " ===\n" + of(p).text + "=== END FILE ===\n";
        return {reply};
    }
};

static void runWorkflow(const std::string& exe, const char* executor, const char* tag)
{
    Squad sq(exe, tag);
    sq.startDaemon(4, {"--frontier", "off"});
    Calculator calc;
    std::mutex mu;
    fake::Engine engine([&](const fake::Request& rq, int) { std::lock_guard<std::mutex> l(mu); return calc.answer(rq); });
    const std::string profile = engine.profile(8192) + ",slots=4";
    const Result sub = sq.cli({"project", "submit", "--task", "calculator-py", "--agent", "llm", "--engine", profile, "--executor", executor, "--max-tokens", "3000", "--repair-turns", "2", "--size-budget", "0"});
    CHECK_EQ(sub.exitCode, 0);
    const std::string wf = sub.out.substr(0, sub.out.find('\n'));
    CHECK_EQ(wf, std::string("w-000001"));
    std::string status;
    for (int i = 0; i < 1500; ++i) {   // five minutes
        status = sq.cli({"project", "status", wf, "--json"}).out;
        const std::string state = field(status, "state");
        if (state != "running" && !state.empty()) break;
        sleepMs(200);
    }
    if (field(status, "state") != "succeeded") std::fprintf(stderr, "[%s] workflow ended %s: %s\n", executor, field(status, "state").c_str(), field(status, "note").c_str());
    CHECK_EQ(field(status, "state"), std::string("succeeded"));
    CHECK(status.find("\"status\":\"failed\"") == std::string::npos && status.find("frontier-stub") == std::string::npos);   // nothing was rescued by the stub tier
    const fs::path dir = sq.home() / "state" / "workflows" / wf;
    CHECK(fs::exists(dir / "report.md"));
    for (const char* item : {"test-engine", "test-app", "dev-engine", "dev-app"}) CHECK(fs::exists(dir / "items" / item / "accepted"));
    const fs::path project = dir / "project";
    CHECK_EQ(sq.cli({"submit", "--cwd", project.string(), "--wall", "30", "--", "python3", "-m", "calc", "2^3^2"}).exitCode, 0);
    // The executor really ran: what each leaves in the attempt directory.
    const fs::path dev = dir / "items" / "dev-engine" / "attempt-1";
    if (std::string(executor) == "fork") {
        const std::string stats = slurp(dev / "fork.json");
        CHECK(stats.find("\"passed\":true") != std::string::npos && stats.find("\"failure\":\"\"") != std::string::npos && stats.find("\"lostEdits\":0") != std::string::npos);
        CHECK(stats.find("\"skeletonTokens\":") != std::string::npos && fs::exists(dev / "conversation-skeleton.md"));
        CHECK(slurp(dev / "canvas" / "calc_engine.py.r1").find("not implemented:") != std::string::npos);   // revision 1 is the skeleton the harness completed
        CHECK(fs::exists(dir / "items" / "test-engine" / "attempt-1" / "canvas.json"));                  // a tester's item runs on the canvas, not as a fork
        CHECK(!fs::exists(dir / "items" / "test-engine" / "attempt-1" / "fork.json"));
    } else {
        CHECK(fs::exists(dev / "canvas.json") && !fs::exists(dev / "fork.json"));
        CHECK(slurp(dev / "canvas.json").find("\"calc/engine.py\":1") != std::string::npos);            // written whole once: revision 1
    }
    CHECK_GE(engine.requestCount(), static_cast<std::size_t>(4));
}

int main(int, char** argv)
{
    const std::string exe = argv[1];
    runWorkflow(exe, "canvas", "exec-canvas");
    runWorkflow(exe, "fork", "exec-fork");
    return finish();
}
