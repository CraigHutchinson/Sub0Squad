// Canvas slice 0 (docs/CANVAS.md) and turn control (docs/INTERVENTIONS.md rows 34 to 36), end to end with recorded replies: the worker agent in canvas mode
// against a fake streaming engine and the real Python gate. No model. Each block is one behaviour the design promises:
// a reply is a transaction; the cheap check gates the commit; the conversation never carries files; a worse revision is undone; reads are bounded and a
// path-only read gives the outline; a runaway reply is cancelled while it streams; a cut-off edit is continued by appending with the overlap trimmed.
#include "agent_bench.hpp"
#include "sub0squad/harness_registry.hpp"
#include "sub0squad/outline.hpp"

using namespace sub0squad;
using namespace bench;

/// The text of the function `name` in `text` (signature to last body line).
static std::string functionText(const std::string& text, const std::string& name)
{
    for (const auto& r : foldRegions(text, true, true)) {
        if (r.name != name) continue;
        const auto v = lines(text);
        std::string out;
        for (int k = r.first; k <= r.bodyTo; ++k) out += v[static_cast<std::size_t>(k) - 1] + "\n";
        return out;
    }
    return {};
}
static std::vector<std::string> canvasArgs(const fs::path& d, const fake::Engine& e, int attempt, int repairTurns) { return agentArgs(d, e, attempt, repairTurns, {"--executor", "canvas"}); }
static std::string between(const std::string& s, const std::string& from, const std::string& to)
{
    const auto a = s.find(from);
    if (a == std::string::npos) return {};
    const auto b = s.find(to, a);
    return s.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

int main()
{
    sub0squad::registerBuiltinHarnesses();   // the registry starts empty: this program is its own composition root
    const std::string ref = reference(), bad = defective();
    const int bugLine = lineOf(bad, "v = v / d");
    const std::string fixedLine = lines(ref)[static_cast<std::size_t>(bugLine) - 1];
    const std::string term = functionText(ref, "term");
    CHECK(bugLine > 0 && !term.empty() && term.find("math.fmod") != std::string::npos);
    const std::string fixByLine = std::string{"=== EDIT: "} + kFile + " LINES " + std::to_string(bugLine) + " ===\n" + fixedLine + "\n=== END EDIT ===\n";
    const std::string fixBySymbol = std::string{"=== EDIT: "} + kFile + " SYMBOL term ===\n" + term + "=== END EDIT ===\n";

    {   // A reply is a transaction: one function replaced by name. The conversation is the card and the folded view of revision 1, nothing else.
        const fs::path d = workflow("canvas-symbol");
        failedAttempt(d, 1, bad);
        fake::Engine engine({{fixBySymbol}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(1));
        if (!rq.empty()) {
            CHECK_EQ(rq[0].messages.size(), static_cast<std::size_t>(2));
            CHECK(rq[0].messages[0].second.find("=== EDIT: <path> SYMBOL <function name> ===") != std::string::npos);   // the transaction forms
            CHECK(rq[0].messages[0].second.find("=== EDIT: src/calc.cpp ===\n<<<<<<< SEARCH\n    if (d == 0) return 0;") != std::string::npos);   // the worked snippet example
            CHECK(rq[0].last().find("=== CANVAS: calc/engine.py (revision 1,") != std::string::npos);
            CHECK(rq[0].last().find("modulo") != std::string::npos);                                                     // why the draft failed
        }
        const std::string log = slurp(attemptDir(d, 2) / "canvas.json");
        CHECK(log.find("\"calc/engine.py\":2") != std::string::npos && log.find("\"by\":\"dev-engine\"") != std::string::npos);
        CHECK_EQ(slurp(attemptDir(d, 2) / "canvas" / "calc_engine.py.r1"), bad);    // every revision is kept
        CHECK_EQ(slurp(attemptDir(d, 2) / "canvas" / "calc_engine.py.r2"), ref);
        const std::string stats = slurp(attemptDir(d, 2) / "agent.json");
        CHECK(stats.find("\"selfcheck\":true") != std::string::npos && stats.find("\"commits\":1") != std::string::npos && stats.find("\"refused\":0") != std::string::npos);
    }
    {   // An edit that does not parse is KEPT as a staged revision, not thrown away: the errors come back in the numbering of that revision (no repair turn
        // used), the tests do not run on it, and the next edit fixes it in place.
        const fs::path d = workflow("canvas-precheck");
        failedAttempt(d, 1, bad);
        fake::Engine engine({{std::string{"=== EDIT: "} + kFile + " LINES " + std::to_string(bugLine) + " ===\n                v = math.fmod(v, d\n=== END EDIT ===\n"}, {fixByLine}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            CHECK(rq[1].last().find("KEPT") != std::string::npos && rq[1].last().find("nothing was thrown away") != std::string::npos);
            CHECK(rq[1].last().find("syntax error") != std::string::npos);
            CHECK(rq[1].last().find("=== CANVAS: calc/engine.py (revision 2,") != std::string::npos);   // the staged revision
            CHECK(rq[1].last().find("math.fmod(v, d\n") != std::string::npos);                          // the broken line, in view to be fixed
            CHECK_EQ(rq[1].messages.size(), static_cast<std::size_t>(2));                                // the reply itself is not carried along
        }
        const std::string log = slurp(attemptDir(d, 2) / "canvas.json");
        CHECK(log.find("\"calc/engine.py\":3") != std::string::npos);                                    // staged r2, fixed r3
        const std::string stats = slurp(attemptDir(d, 2) / "agent.json");
        CHECK(stats.find("\"staged\":1") != std::string::npos && stats.find("\"refused\":0") != std::string::npos);
    }
    {   // The same compile error three times running ends the attempt (catalogue row 72: run-v11's tester spent six turns an attempt on one compile error),
        // and says why. The work is still not thrown away: with no tested revision, the last draft is handed on, so the gate names its error.
        const fs::path d = workflow("canvas-same-refusal");
        int n = 0;
        fake::Engine engine([&](const fake::Request&, int) { ++n; return fake::Reply{std::string{"=== FILE: "} + kFile + " ===\n" + std::string(static_cast<std::size_t>(n), '\n') + "def evaluate(expr, degrees=False:\n    return 0\n=== END FILE ===\n"}; });
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 1, 5)), 0);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(3));                                    // not the seven calls the turns allowed (the error's line number differs each time)
        const std::string trace = slurp(attemptDir(d, 1) / "conversation.md");
        CHECK(trace.find("the same compile error three times running") != std::string::npos && trace.find("the draft is handed on") != std::string::npos);
        CHECK(slurp(attemptDir(d, 1) / "out" / kFile).find("degrees=False:") != std::string::npos);
        CHECK(slurp(attemptDir(d, 1) / "agent.json").find("\"staged\":3") != std::string::npos);
    }
    {   // A commit that still fails the tests: the next turn is rebuilt from the card and revision 2; the earlier reply and the old file are not in the window.
        const fs::path d = workflow("canvas-flat");
        failedAttempt(d, 1, bad);
        const std::string wrong = std::string{"=== EDIT: "} + kFile + " LINES " + std::to_string(bugLine) + " ===\n                v = v * d  # still wrong\n=== END EDIT ===\n";
        fake::Engine engine({{wrong}, {fixByLine}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 1)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            CHECK_EQ(rq[1].messages.size(), static_cast<std::size_t>(2));
            CHECK(rq[1].last().find("(revision 2,") != std::string::npos);
            CHECK(rq[1].last().find("v = v * d  # still wrong") != std::string::npos);
            CHECK(rq[1].last().find("v = v / d") == std::string::npos);            // revision 1's line is gone from the window
            CHECK(rq[1].all().find("=== END EDIT ===\n=== END EDIT") == std::string::npos);
            CHECK(rq[1].last().find("failed") != std::string::npos && rq[1].last().find("modulo") != std::string::npos);
        }
    }
    {   // A commit that makes MORE tests fail is undone (revert as a new revision) and the worker is told; the fix then applies to the restored text.
        const fs::path d = workflow("canvas-revert");
        failedAttempt(d, 1, bad);
        const std::string worse = std::string{"=== EDIT: "} + kFile + " SYMBOL factorial ===\n    def factorial(v):\n        return 0.0\n=== END EDIT ===\n";
        fake::Engine engine({{worse}, {fixByLine}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 1)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            CHECK(rq[1].last().find("back at the better revision (yours stays in the history as calc/engine.py r2)") != std::string::npos);
            CHECK(rq[1].last().find("(revision 3,") != std::string::npos);          // 1 draft, 2 the worse edit, 3 the revert
            CHECK(rq[1].last().find("return 0.0") == std::string::npos);
        }
        CHECK_EQ(slurp(attemptDir(d, 2) / "canvas" / "calc_engine.py.r3"), bad);
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"reverted\":1") != std::string::npos);
        CHECK(slurp(attemptDir(d, 2) / "canvas.json").find("\"by\":\"harness\"") != std::string::npos);   // the log says who changed what
    }
    {   // Reads are bounded (row 36): two read-only replies are answered free; the third is REFUSED (not answered), costs a turn, and keeps the last
        // answer in view; a path-only read gives the outline.
        const fs::path d = workflow("canvas-reads");
        failedAttempt(d, 1, bad);
        const std::string read1 = std::string{"=== READ: "} + kFile + " ===\n", read2 = std::string{"=== READ: "} + kFile + " SYMBOL term ===\n";
        fake::Engine engine({{read1}, {read2}, {read2}, {fixByLine}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 1)), 0);      // one repair turn: the refused third read spends the first turn, the edit the second
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(4));
        if (rq.size() == 4) {
            const std::string outline = between(rq[1].last(), "The lines you asked to read", "=== CANVAS:");
            CHECK(outline.find("outline (") != std::string::npos && outline.find("def term(self):") != std::string::npos);
            CHECK(outline.find("v = v / d") == std::string::npos);              // bodies are folded: the whole file is never returned to a read
            CHECK(rq[1].last().find("Now reply with your edit") != std::string::npos);
            CHECK(between(rq[2].last(), "The lines you asked to read", "=== CANVAS:").find("v = v / d") != std::string::npos);   // SYMBOL term shows the body
            CHECK(rq[2].last().find("MUST be an edit") != std::string::npos);
            CHECK_EQ(rq[3].messages.size(), static_cast<std::size_t>(2));
            CHECK(rq[3].last().find("this read was not answered") != std::string::npos);
            CHECK(between(rq[3].last(), "The lines you read before", "=== CANVAS:").find("v = v / d") != std::string::npos);   // the last answer stays in view
        }
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"reads\":3") != std::string::npos);
    }
    {   // A read answer is what the worker asked to see: it is never clipped like a gate's output (2,400 characters). Clipped, the worker read the same
        // lines again until its turns ran out (tier 1 of 2026-11-17).
        const fs::path d = workflow("canvas-longread");
        std::string big = bad;
        for (int i = 1; i <= 150; ++i) big += "# filler line " + std::to_string(i) + ": long enough that the read answer runs well past the gate cap\n";
        failedAttempt(d, 1, big);
        const std::string line150 = lines(big)[149];   // a read returns at most 160 lines per region (its header says which): 150 fit
        fake::Engine engine({{std::string{"=== READ: "} + kFile + " LINES 1-150 ===\n"}, {fixByLine}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 0)), 0);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            const std::string answer = between(rq[1].last(), "The lines you asked to read", "=== CANVAS:");
            CHECK(answer.size() > 2400 && answer.find(" 150| " + line150) != std::string::npos && answer.find("(truncated)") == std::string::npos);
        }
        CHECK(slurp(attemptDir(d, 2) / "out" / kFile).find(fixedLine) != std::string::npos);
    }
    {   // With no turns left a third read ends the attempt: reads cannot loop.
        const fs::path d = workflow("canvas-readloop");
        failedAttempt(d, 1, bad);
        fake::Engine engine({{std::string{"=== READ: "} + kFile + " SYMBOL term ===\n"}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 0)), 0);      // the earlier draft is handed on (progress is never thrown away); the gate job will fail it
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(3));
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), bad);
    }
    {   // Early abort (row 34): a SEARCH block that copies the file is cancelled WHILE IT STREAMS and the worker is re-prompted for a LINES edit.
        const fs::path d = workflow("canvas-abort");
        failedAttempt(d, 1, bad);
        std::string runaway = std::string{"=== EDIT: "} + kFile + " ===\n<<<<<<< SEARCH\n" + bad + "=======\n" + ref + ">>>>>>> REPLACE\n=== END EDIT ===\n";
        fake::Reply slow{runaway};
        slow.pieceChars = 64;
        slow.pauseMs = 3;
        fake::Engine engine({slow, {fixByLine}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        CHECK(engine.abortedAfter(0) >= 0 && engine.abortedAfter(0) < static_cast<long>(runaway.size() / 2));   // the server was stopped well before the end
        if (rq.size() == 2) CHECK(rq[1].last().find("SEARCH block copied more than 40 lines") != std::string::npos);
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"aborted\":1") != std::string::npos);
    }
    {   // A reply that repeats one line is cancelled too.
        const fs::path d = workflow("canvas-repeat");
        failedAttempt(d, 1, bad);
        std::string loop = std::string{"=== EDIT: "} + kFile + " AFTER 3 ===\n";
        for (int i = 0; i < 400; ++i) loop += "    # the parser handles this case\n";
        fake::Reply slow{loop};
        slow.pauseMs = 2;
        fake::Engine engine({slow, {fixByLine}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 0)), 0);
        CHECK(engine.abortedAfter(0) >= 0);
        const auto rq = engine.requests();
        if (rq.size() == 2) CHECK(rq[1].last().find("repeated the line") != std::string::npos);
    }
    {   // A cut-off EDIT is continued by appending (row 34), and the overlap the continuation repeats is trimmed (row 35).
        const fs::path d = workflow("canvas-cut");
        failedAttempt(d, 1, bad);
        const auto cut = fixBySymbol.find("modulo by zero") + 6;                    // in the middle of a string literal
        const std::string first = fixBySymbol.substr(0, cut), rest = fixBySymbol.substr(cut - 14);   // the continuation starts 14 characters early
        fake::Engine engine({{first, "length"}, {rest}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);                    // no duplicated text: the file is exactly the reference
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) CHECK(rq[1].last().find("Continue from exactly that point") != std::string::npos);
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"continued\":1") != std::string::npos);
    }
    {   // A first attempt starts from the workspace's stub (revision 1), not from nothing; a file written whole becomes revision 2. The snippet form works too.
        const fs::path d = workflow("canvas-first");
        fake::Engine engine({{fileBlock(ref)}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 1, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK(!rq.empty() && rq[0].last().find("These files start as the workspace's stubs (calc/engine.py)") != std::string::npos);
        CHECK(!rq.empty() && rq[0].last().find("=== CANVAS: calc/engine.py (revision 1,") != std::string::npos);
        CHECK(slurp(attemptDir(d, 1) / "canvas.json").find("\"calc/engine.py\":2") != std::string::npos);

        const fs::path d2 = workflow("canvas-search");
        failedAttempt(d2, 1, bad);
        const std::string badLine = lines(bad)[static_cast<std::size_t>(bugLine) - 1];
        fake::Engine engine2({{std::string{"=== EDIT: "} + kFile + " ===\n<<<<<<< SEARCH\n" + badLine + "\n=======\n" + fixedLine + "\n>>>>>>> REPLACE\n=== END EDIT ===\n"}});
        CHECK_EQ(runLlmAgent(canvasArgs(d2, engine2, 2, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d2, 2) / "out" / kFile), ref);
    }
    {   // A part on a file the item does not own is skipped and named; the valid part beside it is kept (one bad part never costs the good ones).
        const fs::path d = workflow("canvas-allow");
        failedAttempt(d, 1, bad);
        const std::string stillWrong = std::string{"=== EDIT: "} + kFile + " LINES " + std::to_string(bugLine) + " ===\n                v = v * d  # still wrong\n=== END EDIT ===\n";
        fake::Engine engine({{stillWrong + "=== FILE: calc/other.py ===\nx = 1\n=== END FILE ===\n"}, {fixByLine}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 1)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            CHECK(rq[1].last().find("did not apply (the rest did)") != std::string::npos && rq[1].last().find("'calc/other.py' is not one of the files you may write") != std::string::npos);
            CHECK(rq[1].last().find("(revision 2,") != std::string::npos && rq[1].last().find("# still wrong") != std::string::npos);   // the valid part was kept
        }
        CHECK(!fs::exists(attemptDir(d, 2) / "out" / "calc" / "other.py"));
    }
    {   // Thinking that passes its share of the budget with no edit begun is stopped, and its end is carried into the next turn, which runs with thinking off.
        const fs::path d = workflow("canvas-thinking");
        failedAttempt(d, 1, bad);
        // 3000 max tokens: thinking may use 0.6 x 3000 x 3.5 = 6300 characters. The reasoning goes on well past that; its end must not be reached.
        fake::Reply thinker{"", "stop", std::string(5000, 'x') + "\nso the modulo branch divides: replace it with math.fmod\n" + std::string(20000, 'y')};
        thinker.pieceChars = 200;
        thinker.pauseMs = 1;
        fake::Engine engine({thinker, {fixByLine}});
        std::vector<std::string> args = canvasArgs(d, engine, 2, 0);
        for (std::size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == "--engine") args[i + 1] += ",thinking=1";
        CHECK_EQ(runLlmAgent(args), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            CHECK(rq[1].last().find("Your reasoning from the last turn is kept") != std::string::npos && rq[1].last().find("replace it with math.fmod") != std::string::npos);
            CHECK(rq[0].thinking == 1 && rq[1].thinking == 0);   // the turn that writes it down runs with thinking off
        }
        CHECK(engine.abortedAfter(0) >= 0);                      // stopped while it streamed
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"thinkingStops\":1") != std::string::npos);
    }
    return finish();
}
