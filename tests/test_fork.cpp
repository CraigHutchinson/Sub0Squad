// Forked sections on the canvas (include/sub0squad/fork.hpp), over HTTP against the fake engine and judged by the real Python gate. No model.
// A primer writes the skeleton; each stubbed function is a section with a session of its own; sessions run side by side; each commits one transaction on
// its own function through the coordinator; the merged file must pass the item's tests. Pinned here: the width rule (throughput by default, a stated
// reason for sequential, forks shed under memory pressure), ownership (an edit outside the section is refused and the holder named), the precheck as the
// commit gate, the integration round, and that no edit is lost in the merge.
#include "agent_bench.hpp"
#include "sub0squad/fork.hpp"
#include "sub0squad/harness_cpp.hpp"
#include "sub0squad/outline.hpp"

#include <cstdlib>
#include <map>
#include <mutex>

using namespace sub0squad;
using namespace bench;

/// The reference as a DECLARED skeleton (what a primer replies): each function in `names` becomes one `@@ signature` line; `functions` receives each
/// one's reference text. `python` selects the language of the fold detection.
static std::string skeletonOf(const std::string& ref, const std::vector<std::string>& names, std::map<std::string, std::string>& functions, bool python = true)
{
    auto v = lines(ref);
    const auto regions = foldRegions(ref, python, true);
    for (auto r = regions.rbegin(); r != regions.rend(); ++r) {   // bottom up, so earlier line numbers stay valid
        if (std::find(names.begin(), names.end(), r->name) == names.end() || functions.count(r->name)) continue;
        std::string text;
        for (int k = r->first; k <= r->bodyTo; ++k) text += v[static_cast<std::size_t>(k) - 1] + "\n";
        functions[r->name] = text;
        const std::string sig = v[static_cast<std::size_t>(r->first) - 1];
        const auto ind = sig.find_first_not_of(" \t");
        std::string decl = sig.substr(ind);
        if (python) decl.erase(decl.rfind(':'));
        v.erase(v.begin() + (r->first - 1), v.begin() + r->bodyTo);
        v.insert(v.begin() + (r->first - 1), sig.substr(0, ind) + "@@ " + decl);
    }
    std::string out;
    for (const auto& l : v) out += l + "\n";
    return out;
}
static std::string skeletonBlock(const std::string& path, const std::string& declared) { return "=== SKELETON: " + path + " ===\n" + declared + "=== END SKELETON ===\n"; }
static std::string ownerOf(const fake::Request& rq)
{
    const std::string& ask = rq.last();
    const auto own = ask.rfind("You own `");
    if (own != std::string::npos) return ask.substr(own + 9, ask.find('`', own + 9) - own - 9);
    const auto sym = ask.rfind(" SYMBOL ");   // a retry names the function in the block it asks for
    return sym == std::string::npos ? std::string{} : ask.substr(sym + 8, ask.find(' ', sym + 8) - sym - 8);
}
static std::string edit(const std::string& name, const std::string& text) { return std::string{"=== EDIT: "} + kFile + " SYMBOL " + name + " ===\n" + text + "=== END EDIT ===\n"; }
static std::vector<std::string> forkArgs(const fs::path& d, const fake::Engine& e, std::vector<std::string> extra = {})
{
    std::vector<std::string> a = {"--workflow-dir", d.string(), "--item", kItem, "--attempt", "1", "--engine", e.profile() + ",slots=4", "--max-tokens", "3000", "--timeout", "30", "--repair-turns", "2"};
    a.insert(a.end(), extra.begin(), extra.end());
    return a;
}

int main()
{
    const std::string ref = reference();
    const std::vector<std::string> names = {"expr", "term", "unary", "postfix", "primary"};
    std::map<std::string, std::string> fn;
    const std::string skeleton = skeletonOf(ref, names, fn);
    CHECK_EQ(fn.size(), names.size());
    CHECK(skeleton.find("    @@ def term(self)\n") != std::string::npos && skeleton.find("math.fmod") == std::string::npos);
    const auto primerOr = [&](const fake::Request& rq, const std::function<fake::Reply(const std::string&)>& section) -> fake::Reply {
        if (rq.last().find("Declare the STRUCTURE") != std::string::npos) return {skeletonBlock(kFile, skeleton)};
        return section(ownerOf(rq));
    };

    {   // Throughput by default: five sections, four slots, so four at once; every function lands, nothing is lost, the merged file is the reference.
        const fs::path d = workflow("fork-parallel");
        fake::Engine engine([&](const fake::Request& rq, int) { return primerOr(rq, [&](const std::string& name) { fake::Reply r{edit(name, fn[name])}; r.pauseMs = 5; return r; }); });
        ::setenv("SUB0SQUAD_LEASE_INDEX", "1", 1);   // as inside a daemon job that holds lease slot 1
        CHECK_EQ(runLlmFork(forkArgs(d, engine)), 0);
        ::unsetenv("SUB0SQUAD_LEASE_INDEX");
        for (const auto& r : engine.requests()) CHECK_EQ(r.idSlot, -1L);   // several requests at once are never pinned to the job's one slot (row 47)
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(6));               // the primer and one session per section
        const std::string stats = slurp(attemptDir(d, 1) / "fork.json");
        CHECK(stats.find("\"passed\":true") != std::string::npos && stats.find("\"width\":4") != std::string::npos);
        CHECK(stats.find("\"conflicts\":0") != std::string::npos && stats.find("\"lostEdits\":0") != std::string::npos && stats.find("\"refused\":0") != std::string::npos);
        const auto rq = engine.requests();
        std::string prefix;
        for (const auto& r : rq) {
            if (r.last().find("Declare the STRUCTURE") != std::string::npos) continue;
            CHECK_EQ(r.messages.size(), static_cast<std::size_t>(2));               // a section's session is small: the system block and one message
            const std::string shared = r.last().substr(0, r.last().find("\nYou own `"));
            if (prefix.empty()) prefix = shared;
            CHECK(shared == prefix);                                                  // the same prefix for every section (one prefill on a caching server)
            CHECK(shared.find("=== CANVAS: calc/engine.py (revision 1) ===") != std::string::npos);
        }
        const std::string log = slurp(attemptDir(d, 1) / "canvas.json");
        for (const auto& n : names) CHECK(log.find("\"by\":\"" + n + "\"") != std::string::npos);   // the log says which section changed what
        CHECK(log.find("\"calc/engine.py\":6") != std::string::npos);                               // the skeleton and five commits
    }
    {   // One at a time needs a stated reason, and the reason is recorded; under memory pressure forks are shed.
        const fs::path d = workflow("fork-sequential");
        fake::Engine engine([&](const fake::Request& rq, int) { return primerOr(rq, [&](const std::string& name) { return fake::Reply{edit(name, fn[name])}; }); });
        CHECK_EQ(runLlmFork(forkArgs(d, engine, {"--sequential-reason", "the sections share a helper that does not exist yet"})), 0);
        const std::string stats = slurp(attemptDir(d, 1) / "fork.json");
        CHECK(stats.find("\"width\":1") != std::string::npos && stats.find("sequential: the sections share a helper that does not exist yet") != std::string::npos);

        const fs::path d2 = workflow("fork-shed");
        fake::Engine engine2([&](const fake::Request& rq, int) { return primerOr(rq, [&](const std::string& name) { return fake::Reply{edit(name, fn[name])}; }); });
        CHECK_EQ(runLlmFork(forkArgs(d2, engine2, {"--mem-floor-mib", "999999999"})), 0);
        const std::string stats2 = slurp(attemptDir(d2, 1) / "fork.json");
        CHECK(stats2.find("\"width\":1") != std::string::npos && stats2.find("\"shed\":true") != std::string::npos && stats2.find("forks are shed first") != std::string::npos);
        CHECK_EQ(slurp(attemptDir(d2, 1) / "out" / kFile), ref);                    // shed means slower, not different
    }
    {   // Ownership and the commit gate: a section that edits a neighbour's function is refused with the holder named; one that does not parse is refused
        // with the located error; neither changes the document, and the second reply of each lands.
        const fs::path d = workflow("fork-refuse");
        std::mutex mu;
        std::map<std::string, int> calls;
        fake::Engine engine([&](const fake::Request& rq, int) {
            return primerOr(rq, [&](const std::string& name) {
                std::lock_guard<std::mutex> l(mu);
                const int n = calls[name]++;
                if (name == "term" && n == 0) return fake::Reply{edit("unary", fn["unary"])};                               // not its own function
                if (name == "expr" && n == 0) return fake::Reply{edit("expr", "    def expr(self):\n        v = self.term(\n")};   // does not parse
                return fake::Reply{edit(name, fn[name])};
            });
        });
        CHECK_EQ(runLlmFork(forkArgs(d, engine)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        CHECK(slurp(attemptDir(d, 1) / "fork.json").find("\"refused\":2") != std::string::npos);
        bool sawHolder = false, sawSyntax = false;
        for (const auto& r : engine.requests()) {
            if (r.last().find("`unary` is held by the worker writing unary") != std::string::npos) sawHolder = true;
            if (r.last().find("does not pass the check") != std::string::npos && r.last().find("was never closed") != std::string::npos) sawSyntax = true;
        }
        CHECK(sawHolder);
        CHECK(sawSyntax);
    }
    {   // Integration: a function that parses but is wrong fails the merged file's tests; the sections go back, each in a small session that shows the
        // current revision with its own function open, and the corrected function makes the whole pass.
        const fs::path d = workflow("fork-round");
        std::mutex mu;
        std::map<std::string, int> calls;
        std::string wrong = fn["term"];
        wrong.replace(wrong.find("v = math.fmod(v, d)"), 19, "v = v / d");
        fake::Engine engine([&](const fake::Request& rq, int) {
            return primerOr(rq, [&](const std::string& name) {
                std::lock_guard<std::mutex> l(mu);
                return fake::Reply{edit(name, name == "term" && calls[name]++ == 0 ? wrong : fn[name])};
            });
        });
        CHECK_EQ(runLlmFork(forkArgs(d, engine)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        CHECK(slurp(attemptDir(d, 1) / "fork.json").find("\"rounds\":0") == std::string::npos);   // it took an integration round
        bool sawRound = false;
        for (const auto& r : engine.requests())
            if (r.last().find("The merged file was checked by running the tests and failed") != std::string::npos && r.last().find("You own `term`") != std::string::npos) {
                sawRound = true;
                CHECK(r.last().find("v = v / d") != std::string::npos);            // its own function is open
                CHECK(r.last().find("lines folded") != std::string::npos);          // a neighbour is folded
                CHECK(r.last().find("modulo") != std::string::npos);               // the failure
            }
        CHECK(sawRound);
    }
    {   // Row 46, as it happened: asked for the structure, the model writes the IMPLEMENTATION. The stream is cancelled past 120 lines (the server is
        // stopped, not waited for), the re-prompt says why, and a declared skeleton then runs as usual. The skeleton reply is also bounded at the server.
        const fs::path d = workflow("fork-ignores");
        std::string implementation = std::string{"=== SKELETON: "} + kFile + " ===\n" + ref + "\n" + ref + "=== END SKELETON ===\n";   // about 300 lines
        fake::Engine engine([&](const fake::Request& rq, int index) {
            if (index == 0) { fake::Reply slow{implementation}; slow.pieceChars = 80; slow.pauseMs = 2; return slow; }
            return primerOr(rq, [&](const std::string& name) { return fake::Reply{edit(name, fn[name])}; });
        });
        CHECK_EQ(runLlmFork(forkArgs(d, engine)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        CHECK(engine.abortedAfter(0) >= 0 && engine.abortedAfter(0) < static_cast<long>(implementation.size() * 3 / 4));
        const auto rq = engine.requests();
        CHECK(rq.size() >= 2 && rq[0].maxTokens == 1500);                                   // bounded at the server, whatever the item's budget
        CHECK(rq.size() >= 2 && rq[1].last().find("ran past 120 lines and was stopped") != std::string::npos && rq[1].last().find("NO function bodies") != std::string::npos);
        CHECK(slurp(attemptDir(d, 1) / "fork.json").find("\"skeletonTries\":2") != std::string::npos);
    }
    {   // The harness completes the skeleton: a `@@` line that is no signature is refused with its line, and the wall budget stops an attempt that has
        // none left before another session starts.
        const fs::path d = workflow("fork-badline");
        fake::Engine engine([&](const fake::Request& rq, int index) {
            if (index == 0) return fake::Reply{skeletonBlock(kFile, "import math\n\n@@ evaluate the expression\n")};
            return primerOr(rq, [&](const std::string& name) { return fake::Reply{edit(name, fn[name])}; });
        });
        CHECK_EQ(runLlmFork(forkArgs(d, engine)), 0);
        const auto rq = engine.requests();
        CHECK(rq.size() >= 2 && rq[1].last().find("line 3 is not a function signature") != std::string::npos);

        const fs::path d2 = workflow("fork-budget");
        fake::Reply slow{skeletonBlock(kFile, skeleton)};
        slow.pieceChars = 8;
        slow.pauseMs = 40;                                                                   // several seconds for the skeleton alone
        fake::Engine engine2([&](const fake::Request& rq, int index) { return index == 0 ? slow : primerOr(rq, [&](const std::string& name) { return fake::Reply{edit(name, fn[name])}; }); });
        CHECK_EQ(runLlmFork(forkArgs(d2, engine2, {"--attempt-wall", "1"})), 2);            // one second: the skeleton call times out
        CHECK_EQ(engine2.requestCount(), static_cast<std::size_t>(1));                      // and no second session starts on a spent budget
    }
    {   // A primer that gives no usable skeleton twice ends the attempt with exit 2: nothing is delegated on a skeleton that does not parse.
        const fs::path d = workflow("fork-noskeleton");
        fake::Engine engine({{skeletonBlock(kFile, "def broken(:\n")}});
        CHECK_EQ(runLlmFork(forkArgs(d, engine)), 2);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(2));
        CHECK(slurp(attemptDir(d, 1) / "fork.json").find("no usable skeleton") != std::string::npos);
    }
    {   // Rows 49 and 50: the wall budget stops an attempt between sections, says so, and the NEXT attempt carries on from that canvas: no new skeleton,
        // only the sections that were never written get a session, and the file is finished.
        const fs::path d = workflow("fork-resume");
        fake::Engine slowEngine([&](const fake::Request& rq, int) { return primerOr(rq, [&](const std::string& name) { fake::Reply r{edit(name, fn[name])}; r.delayMs = 700; return r; }); });
        CHECK_EQ(runLlmFork(forkArgs(d, slowEngine, {"--attempt-wall", "2", "--section-wall", "0", "--sequential-reason", "one at a time, so the budget ends between sections"})), 1);
        const std::string first = slurp(attemptDir(d, 1) / "fork.json");
        CHECK(first.find("\"failure\":\"budget\"") != std::string::npos && first.find("\"budgetSpent\":true") != std::string::npos && first.find("\"budgetSec\":2") != std::string::npos);
        const std::size_t written = slowEngine.requestCount() - 1;                           // sessions that ran before the budget ended (the primer is one request)
        CHECK(written >= 1 && written < names.size());
        CHECK(slurp(attemptDir(d, 1) / "out" / kFile).find("not implemented:") != std::string::npos);   // what stands is kept, stubs and all
        fake::Engine engine([&](const fake::Request& rq, int) { return primerOr(rq, [&](const std::string& name) { return fake::Reply{edit(name, fn[name])}; }); });
        std::vector<std::string> second = forkArgs(d, engine);
        second[5] = "2";                                                                    // --attempt 2
        CHECK_EQ(runLlmFork(second), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        for (const auto& r : engine.requests()) CHECK(r.last().find("Declare the STRUCTURE") == std::string::npos);   // no new skeleton
        CHECK(engine.requestCount() >= 1 && engine.requestCount() <= names.size() - written + 1);        // only what was missing (a session cut off mid-reply is asked again)
        const std::string stats = slurp(attemptDir(d, 2) / "fork.json");
        CHECK(stats.find("\"resumedFrom\":1") != std::string::npos && stats.find("\"passed\":true") != std::string::npos && stats.find("\"skeletonTries\":0") != std::string::npos);
        // The budget grows with the number of sections: 900 s and 150 s for each of the five.
        CHECK(stats.find("\"budgetSec\":1650") != std::string::npos);
    }
    {   // Rows 45 and 46 on the platform where they happened: the C++ calculator blueprint. The engine is ONE file; it is divided by SECTIONS under a
        // skeleton the harness completes (no new header, so no part can call a class nobody declared). The declared skeleton holds the class with one
        // `@@` line per method; the stubs leave methods uncalled and parameters unused, which the partial compile check allows; every section commits
        // through that check; the merged file passes the real C++ gate and is the reference, byte for byte.
        const Harness* cpp = findHarness("cpp-cmake");
        CHECK(cpp && cpp->divideBySections);
        const std::string file = "src/calc.cpp", cref = cpp->mockFile(file, false);
        const std::vector<std::string> methods = {"expr", "term", "unary", "postfix", "primary", "number", "name", "call"};
        std::map<std::string, std::string> cfn;
        const std::string cskel = skeletonOf(cref, methods, cfn, false);
        CHECK_EQ(cfn.size(), methods.size());
        const fs::path d = makeTempDir("fork-cpp");
        CHECK_EQ(runMockPlanner({"--task", "calculator-cpp", "--out", (d / "plan.json").string()}), 0);
        CHECK_EQ(runMockAgent({"--workflow-dir", d.string(), "--item", "test-engine", "--tier", "local", "--attempt", "1", "--delay-ms", "0"}), 0);
        CHECK_EQ(cpp->gate({"--workflow-dir", d.string(), "--item", "test-engine", "--attempt", "1"}), 0);
        // Assembly convergence (run-v9, rows 47 to 51), with replies shaped like that run's failures, all in one attempt:
        //   `spare` is a helper the skeleton declares and nobody calls: the strict check says "defined but not used"; the harness removes it and its
        //           prototype itself (no session is spent on it);
        //   `term`  is refused three times in the writing phase (it does not compile), so its stub is left: in assembly ONLY `term` gets another session;
        //   `unary` comes back with a path that returns nothing. The compiler cannot see it while `postfix`, which it calls, is still a stub that always
        //           throws, so it is committed; when `postfix` is written the error SURFACES IN ANOTHER SECTION'S COMMIT. That commit is not blocked
        //           (the error is not in its function), and in assembly the finding goes to `unary` alone, with only that error.
        std::string declared = cskel;
        declared.insert(declared.find("class Parser"), "@@ static double spare(double x)\n\n");
        std::string noReturn = cfn["unary"];
        noReturn.erase(noReturn.rfind("        return base;"), std::string("        return base;\n").size());
        std::mutex mu;
        std::map<std::string, int> calls;
        fake::Engine engine([&](const fake::Request& rq, int) {
            if (rq.last().find("Declare the STRUCTURE") != std::string::npos) return fake::Reply{skeletonBlock(file, declared)};
            const std::string name = ownerOf(rq);
            std::lock_guard<std::mutex> l(mu);
            const int n = calls[name]++;
            const auto reply = [&](const std::string& text) { return fake::Reply{"=== EDIT: " + file + " SYMBOL " + name + " ===\n" + text + "=== END EDIT ===\n"}; };
            if (name == "spare") return reply("static double spare(double x)\n{\n    return x * 2;\n}\n");
            if (name == "term" && n < 3) return reply("    double term()\n    {\n        return nothing_declared_anywhere;\n    }\n");
            if (name == "unary" && n == 0) return reply(noReturn);
            // The declaration is the harness's: a section that drops the indentation or renames a parameter still lands with the skeleton's declaration.
            if (name == "call") { std::string body = cfn[name]; return reply("double call(const std::string& fn, const std::vector<double>& args)" + body.substr(body.find("\n    {"))); }
            return reply(cfn[name]);
        });
        CHECK_EQ(runLlmFork({"--workflow-dir", d.string(), "--item", "dev-engine", "--attempt", "1", "--engine", engine.profile() + ",slots=4", "--max-tokens", "3000", "--timeout", "60", "--repair-turns", "2"}), 0);
        const fs::path attempt = d / "items" / "dev-engine" / "attempt-1";
        const auto squeeze = [](const std::string& s) { std::string o; for (const auto& l : lines(s)) if (l.find_first_not_of(" \t") != std::string::npos) o += l + "\n"; return o; };
        const std::string merged = slurp(attempt / "out" / file);
        CHECK_EQ(squeeze(merged), squeeze(cref));                                                                    // the reference, but for blank lines
        CHECK(merged.find("spare") == std::string::npos);                                                            // the uncalled helper and its prototype are gone
        const std::string r1 = slurp(attempt / "canvas" / "src_calc.cpp.r1");
        CHECK(r1.find("@@") == std::string::npos && r1.find("static double spare(double x);\n") != std::string::npos);  // revision 1 is the completed skeleton, prototype included
        CHECK(r1.find("    double call(const std::string& f, const std::vector<double>& a)\n    {\n        (void)f;\n        (void)a;\n        throw std::logic_error(\"not implemented: call\");\n    }\n") != std::string::npos);   // a warning-clean placeholder
        const std::string stats = slurp(attempt / "fork.json");
        CHECK(stats.find("\"passed\":true") != std::string::npos && stats.find("\"failure\":\"\"") != std::string::npos && stats.find("\"mechanicalMends\":1") != std::string::npos);
        CHECK(slurp(attempt / "canvas.json").find("\"by\":\"harness\"") != std::string::npos);                    // the log says the harness removed it
        // Accepted sections are never re-run: every section had ONE session, except the two that had to answer for their own defects.
        for (const auto& [name, n] : calls) {
            if (name == "term") CHECK_EQ(n, 4);            // three refusals, then its one session in assembly
            else if (name == "unary") CHECK_EQ(n, 2);      // written once, then one session for the finding that is its own
            else CHECK_EQ(n, 1);
        }
        CHECK_EQ(calls.size(), static_cast<std::size_t>(9));
        bool sawOwnText = false, sawRewrite = false, sawRouted = false;
        for (const auto& r : engine.requests()) {
            // a refusal shows the section its own text with the located error
            if (r.last().find("nothing_declared_anywhere") != std::string::npos && r.last().find("Your function as you wrote it, with the line numbers the errors use:") != std::string::npos && r.last().find("|         return nothing_declared_anywhere;") != std::string::npos) sawOwnText = true;
            if (r.last().find("You own `term`") != std::string::npos && r.last().find("Your earlier reply was refused") != std::string::npos) sawRewrite = true;
            // the late finding goes to its owner, and only to its owner, with only its own error
            if (r.last().find("these errors are inside YOUR function `unary`") != std::string::npos) {
                sawRouted = true;
                CHECK(r.last().find("control reaches end of non-void function") != std::string::npos || r.last().find("does not return a value") != std::string::npos);
                CHECK(r.last().find("defined but not used") == std::string::npos);                                  // the uncalled helper is not its business
                CHECK(r.last().find("lines folded") != std::string::npos);                                         // the others are folded
            }
        }
        CHECK(sawOwnText);
        CHECK(sawRewrite);
        CHECK(sawRouted);
        CHECK(stats.find("\"targeted\":2") != std::string::npos && stats.find("\"rounds\":2") != std::string::npos);   // two targeted sessions in assembly: term, unary
    }
    return finish();
}
