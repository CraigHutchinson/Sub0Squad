// Self-healing through the DAEMON (catalogue rows 59 and 60; mends.hpp), with no model: a real daemon, the real command line, real `llm-fork` and gate jobs
// (a real C++ build), and a fake engine that answers from the reference calculator. The fake's SKELETON LEAVES OUT `calc::evaluate`, the one function the
// contract header declares and the tests call: what the 35B did in run-v10.
//   heal     the process switches the prevention off, so the defect happens: attempt 1's gate fails with the linker's real `undefined reference`; the gate
//            reports the mend; the engine journals ONE decision `mend` and no `rearchitect`; attempt 2 adds the section, writes ONLY that section, and the
//            gate passes. The workflow succeeds and the status shows `healed`.
//   prevent  the standard process: the harness declares the contract function itself, so the SAME skeleton passes in attempt 1 with no mend at all.
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
static int occurrences(const std::string& s, const std::string& part) { int n = 0; for (auto at = s.find(part); at != std::string::npos; at = s.find(part, at + part.size())) ++n; return n; }
static std::vector<std::string> conversations(const fs::path& attempt)
{
    std::vector<std::string> v;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(attempt, ec)) if (e.path().filename().string().rfind("conversation-", 0) == 0) v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end());
    return v;
}

/// The fake model for the C++ calculator: a file request gets the reference file, a skeleton request the declared reference, a section request that
/// function. Its skeleton of src/calc.cpp has NO line for `evaluate`.
struct Calculator
{
    struct File { std::string text, declared; std::map<std::string, std::string> functions; };
    std::map<std::string, File> files;
    std::vector<std::string> sectionsAsked;
    const File& of(const std::string& path)
    {
        auto it = files.find(path);
        if (it != files.end()) return it->second;
        File f;
        f.text = sub0squad::findHarness("cpp-cmake")->mockFile(path, false);
        const auto names = bench::sectionNames(f.text, false);
        f.declared = names.empty() ? f.text : bench::skeletonOf(f.text, names, f.functions, false);
        if (path == "src/calc.cpp") {   // the defect of run-v10: the entry point is not in the section list
            std::string kept;
            for (const auto& l : bench::lines(f.declared)) if (!(l.find("@@") != std::string::npos && l.find("evaluate(") != std::string::npos)) kept += l + "\n";
            CHECK(kept != f.declared && f.functions.count("evaluate") == 1);
            f.declared = kept;
        }
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
            sectionsAsked.push_back(name);
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

/// Run the C++ calculator with the fork executor on `process`; returns the final status JSON. `dir` receives the workflow directory.
static std::string runWorkflow(Squad& sq, Calculator& calc, const std::string& process, fs::path& dir)
{
    std::mutex mu;
    fake::Engine engine([&](const fake::Request& rq, int) { std::lock_guard<std::mutex> l(mu); return calc.answer(rq); });
    const std::string profile = engine.profile(8192) + ",slots=4";
    const Result sub = sq.cli({"project", "submit", "--task", "calculator-cpp", "--agent", "llm", "--engine", profile, "--executor", "fork", "--process", process, "--max-tokens", "3000", "--repair-turns", "1", "--size-budget", "0"});
    CHECK_EQ(sub.exitCode, 0);
    const std::string wf = sub.out.substr(0, sub.out.find('\n'));
    std::string status;
    for (int i = 0; i < 2400; ++i) {   // eight minutes: every gate is a real C++ build
        status = sq.cli({"project", "status", wf, "--json"}).out;
        const std::string state = field(status, "state");
        if (state != "running" && !state.empty()) break;
        sleepMs(200);
    }
    if (field(status, "state") != "succeeded") std::fprintf(stderr, "[%s] workflow %s ended %s: %s\n", process.c_str(), wf.c_str(), field(status, "state").c_str(), field(status, "note").c_str());
    dir = sq.home() / "state" / "workflows" / wf;
    return status;
}

int main(int, char** argv)
{
    const std::string exe = argv[1];
    {   // ---- heal: the prevention is off, the defect happens, and the harness repairs it ----
        Squad sq(exe, "heal");
        sq.startDaemon(4, {"--frontier", "off"});
        CHECK_EQ(sq.cli({"process", "add", "no-prevention", "--set", "build.leaf.contractSections=false", "--describe", "the standard process without contract-first sections: reproduces run-v10"}).exitCode, 0);
        Calculator calc;
        fs::path dir;
        const std::string status = runWorkflow(sq, calc, "no-prevention", dir);
        CHECK_EQ(field(status, "state"), std::string("succeeded"));
        // The journal's decisions: exactly one, and it is the mend. No rearchitect, no retry: the ladder was never asked.
        CHECK_EQ(occurrences(status, "\"action\":\"mend\""), 1);
        CHECK_EQ(occurrences(status, "\"action\":\"rearchitect\""), 0);
        CHECK_EQ(occurrences(status, "\"action\":\"retry\""), 0);
        CHECK(status.find("\"signature\":\"undefined-contract-symbol\"") != std::string::npos && status.find("\"symbol\":\"calc::evaluate\"") != std::string::npos);
        CHECK(status.find("\"mends\":[\"undefined-contract-symbol\"],\"healed\":[\"undefined-contract-symbol\"]") != std::string::npos);   // the status says the item was healed, and of what
        CHECK(status.find("frontier-stub") == std::string::npos);
        const fs::path item = dir / "items" / "dev-engine";
        if (field(status, "state") != "succeeded") std::fprintf(stderr, "decisions: %.3000s\njobs: %.6000s\n", between(status, "\"decisions\":", "\"ledger\"").c_str(), sq.cli({"status", "--json"}).out.c_str());
        if (field(status, "state") != "succeeded" && std::getenv("SUB0SQUAD_HEAL_KEEP")) { std::error_code kec; fs::copy(sq.home() / "state", std::getenv("SUB0SQUAD_HEAL_KEEP"), fs::copy_options::recursive, kec); }
        if (field(status, "state") != "succeeded")   // the evidence, before the temporary home is removed
            for (int n = 1; n <= 4; ++n) std::fprintf(stderr, "--- attempt %d\ngate: %s\nfork: %.600s\nmend: %s\n", n, slurp(item / ("attempt-" + std::to_string(n)) / "gate.txt").substr(0, 900).c_str(),
                                                      slurp(item / ("attempt-" + std::to_string(n)) / "fork.json").c_str(), slurp(item / ("attempt-" + std::to_string(n)) / "mend.json").c_str());
        // Attempt 1: the real failure text, and the mend the gate reported for it.
        const std::string gate1 = slurp(item / "attempt-1" / "gate.txt");
        CHECK(gate1.find("GATE FAIL: INTERFACE MISMATCH") != std::string::npos && gate1.find("undefined reference to `calc::ev") != std::string::npos);   // the gate clips long lines: under this long test path the name itself is cut, and the mend still identifies it
        const std::string mend1 = slurp(item / "attempt-1" / "mend.json");
        CHECK(mend1.find("\"symbol\":\"calc::evaluate\"") != std::string::npos);                                             // ... as the one contract function that begins so
        CHECK(mend1.find("\"name\":\"undefined-contract-symbol\"") != std::string::npos && mend1.find("\"action\":\"add-section\"") != std::string::npos && mend1.find("\"file\":\"src/calc.cpp\"") != std::string::npos);
        CHECK(slurp(item / "attempt-1" / "out" / "src" / "calc.cpp").find("double evaluate(") == std::string::npos);           // the merged file really had no entry point
        // Attempt 2 is the mend: it continued attempt 1's file, added ONE section, and held ONE conversation (no skeleton, no other section).
        const std::string fork2 = slurp(item / "attempt-2" / "fork.json");
        CHECK(fork2.find("\"passed\":true") != std::string::npos && fork2.find("\"resumedFrom\":1") != std::string::npos && fork2.find("\"contractSections\":1") != std::string::npos && fork2.find("\"skeletonTries\":0") != std::string::npos);
        CHECK(conversations(item / "attempt-2") == std::vector<std::string>{"conversation-evaluate.md"});
        CHECK(slurp(item / "attempt-2" / "conversation-evaluate.md").find("This function was MISSING from the file") != std::string::npos);
        CHECK(slurp(item / "attempt-2" / "conversation-evaluate.md").find("undefined reference to") != std::string::npos);     // the failure text is the section's brief
        CHECK_EQ(static_cast<int>(std::count(calc.sectionsAsked.begin(), calc.sectionsAsked.end(), std::string("evaluate"))), 1);
        const std::string healed = slurp(item / "attempt-2" / "out" / "src" / "calc.cpp");
        CHECK(healed.find("double evaluate(const std::string& expr, bool degrees)") != std::string::npos && healed.find("not implemented") == std::string::npos);
        CHECK(slurp(item / "attempt-2" / "gate.txt").rfind("GATE PASS", 0) == 0 && !fs::exists(item / "attempt-2" / "mend.json") && !fs::exists(item / "attempt-3"));
        CHECK(fs::exists(item / "accepted") && fs::exists(dir / "report.md"));
        // The failed gate JOB carried the mend in its result, which is how it reached the journal (the engine reads no file).
        const std::string jobs = sq.cli({"status", "--json"}).out;
        CHECK(jobs.find("\"result\":{\"mend\":{\"name\":\"undefined-contract-symbol\"") != std::string::npos);
        // The integrated project runs.
        CHECK_EQ(sq.cli({"submit", "--cwd", (dir / "project").string(), "--wall", "30", "--", (dir / "project" / "build" / "calc_cli").string(), "2^3^2"}).exitCode, 0);
    }
    {   // ---- prevent: the standard process, the same skeleton: the harness declares the contract function, attempt 1 passes, nothing to mend ----
        Squad sq(exe, "prevent");
        sq.startDaemon(4, {"--frontier", "off"});
        Calculator calc;
        fs::path dir;
        const std::string status = runWorkflow(sq, calc, "standard", dir);
        CHECK_EQ(field(status, "state"), std::string("succeeded"));
        CHECK_EQ(occurrences(status, "\"action\":\"mend\""), 0);
        CHECK(status.find("\"decisions\":[]") != std::string::npos);
        const fs::path item = dir / "items" / "dev-engine";
        const std::string fork1 = slurp(item / "attempt-1" / "fork.json");
        CHECK(fork1.find("\"passed\":true") != std::string::npos && fork1.find("\"contractSections\":1") != std::string::npos);
        const std::string r1 = slurp(item / "attempt-1" / "canvas" / "src_calc.cpp.r1");
        CHECK(r1.find("double evaluate(const std::string& expr, bool degrees)") != std::string::npos && r1.find("not implemented: evaluate") != std::string::npos);   // revision 1 already has the stub the model did not declare
        CHECK(!fs::exists(item / "attempt-2"));
        CHECK_EQ(static_cast<int>(std::count(calc.sectionsAsked.begin(), calc.sectionsAsked.end(), std::string("evaluate"))), 1);
    }
    return finish();
}
