// Forked sections on the canvas (include/sub0squad/fork.hpp), over HTTP against the fake engine and judged by the real Python gate. No model.
// A primer writes the skeleton; each stubbed function is a section with a session of its own; sessions run side by side; each commits one transaction on
// its own function through the coordinator; the merged file must pass the item's tests. Pinned here: the width rule (throughput by default, a stated
// reason for sequential, forks shed under memory pressure), ownership (an edit outside the section is refused and the holder named), the precheck as the
// commit gate, the integration round, and that no edit is lost in the merge.
#include "agent_bench.hpp"
#include "sub0squad/fork.hpp"
#include "sub0squad/outline.hpp"

#include <map>
#include <mutex>

using namespace sub0squad;
using namespace bench;

/// The reference with the bodies of `names` replaced by stubs; `functions` receives each one's reference text.
static std::string skeletonOf(const std::string& ref, const std::vector<std::string>& names, std::map<std::string, std::string>& functions)
{
    auto v = lines(ref);
    const auto regions = foldRegions(ref, true, true);
    for (auto r = regions.rbegin(); r != regions.rend(); ++r) {   // bottom up, so earlier line numbers stay valid
        if (std::find(names.begin(), names.end(), r->name) == names.end() || functions.count(r->name)) continue;
        std::string text;
        for (int k = r->first; k <= r->bodyTo; ++k) text += v[static_cast<std::size_t>(k) - 1] + "\n";
        functions[r->name] = text;
        const std::string& first = v[static_cast<std::size_t>(r->bodyFrom) - 1];
        const std::string stub = first.substr(0, first.find_first_not_of(" \t")) + "raise NotImplementedError(\"not implemented: " + r->name + "\")";
        v.erase(v.begin() + (r->bodyFrom - 1), v.begin() + r->bodyTo);
        v.insert(v.begin() + (r->bodyFrom - 1), stub);
    }
    std::string out;
    for (const auto& l : v) out += l + "\n";
    return out;
}
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
    CHECK(skeleton.find("not implemented: term") != std::string::npos && skeleton.find("math.fmod") == std::string::npos);
    const auto primerOr = [&](const fake::Request& rq, const std::function<fake::Reply(const std::string&)>& section) -> fake::Reply {
        if (rq.last().find("Write the SKELETON first") != std::string::npos) return {fileBlock(skeleton)};
        return section(ownerOf(rq));
    };

    {   // Throughput by default: five sections, four slots, so four at once; every function lands, nothing is lost, the merged file is the reference.
        const fs::path d = workflow("fork-parallel");
        fake::Engine engine([&](const fake::Request& rq, int) { return primerOr(rq, [&](const std::string& name) { fake::Reply r{edit(name, fn[name])}; r.pauseMs = 5; return r; }); });
        CHECK_EQ(runLlmFork(forkArgs(d, engine)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(6));               // the primer and one session per section
        const std::string stats = slurp(attemptDir(d, 1) / "fork.json");
        CHECK(stats.find("\"passed\":true") != std::string::npos && stats.find("\"width\":4") != std::string::npos);
        CHECK(stats.find("\"conflicts\":0") != std::string::npos && stats.find("\"lostEdits\":0") != std::string::npos && stats.find("\"refused\":0") != std::string::npos);
        const auto rq = engine.requests();
        std::string prefix;
        for (const auto& r : rq) {
            if (r.last().find("Write the SKELETON first") != std::string::npos) continue;
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
            if (r.last().find("does not pass the check") != std::string::npos && r.last().find("syntax error") != std::string::npos) sawSyntax = true;
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
    {   // A primer that gives no usable skeleton twice ends the attempt with exit 2: nothing is delegated on a skeleton that does not parse.
        const fs::path d = workflow("fork-noskeleton");
        fake::Engine engine({{fileBlock("def broken(:\n")}});
        CHECK_EQ(runLlmFork(forkArgs(d, engine)), 2);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(2));
        CHECK(slurp(attemptDir(d, 1) / "fork.json").find("no usable skeleton") != std::string::npos);
    }
    return finish();
}
