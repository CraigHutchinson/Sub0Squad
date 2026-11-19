// Canvas slice 0 (docs/CANVAS.md) and turn control (docs/INTERVENTIONS.md rows 34 to 36), end to end with recorded replies: the worker agent in canvas mode
// against a fake streaming engine and the real Python gate. No model. Each block is one behaviour the design promises:
// a reply is a transaction; the cheap check gates the commit; the conversation never carries files; a worse revision is undone; reads are bounded and a
// path-only read gives the outline; a runaway reply is cancelled while it streams; a cut-off edit is continued by appending with the overlap trimmed.
#include "agent_bench.hpp"
#include "sub0squad/harness_registry.hpp"
#include "sub0squad/outline.hpp"
#include "../src/agent_impl.hpp"

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
static std::vector<std::string> canvasArgsCtx(const fs::path& d, const fake::Engine& e, int attempt, int repairTurns, int ctx)
{
    auto a = canvasArgs(d, e, attempt, repairTurns);
    for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == "--engine") a[i + 1] = e.profile(ctx);
    return a;
}
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
        // The first reply writes the file over the stub; the next two edit it (a FILE over the worker's own draft is refused: it is not a stub), each
        // moving the same error down a line.
        const fs::path d = workflow("canvas-same-refusal");
        int n = 0;
        fake::Engine engine([&](const fake::Request&, int) {
            ++n;
            if (n == 1) return fake::Reply{std::string{"=== FILE: "} + kFile + " ===\ndef evaluate(expr, degrees=False:\n    return 0\n=== END FILE ===\n"};
            return fake::Reply{std::string{"=== EDIT: "} + kFile + " LINES 1 ===\n\ndef evaluate(expr, degrees=False:\n=== END EDIT ===\n"};
        });
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
    {   // What was read stays open in the folded view on later turns, and a read of the same lines on an unchanged revision is named as a repeat (tier 1 of
        // 2026-11-18: only the latest answer stayed in view and the 35B read the same ranges at least six times).
        const auto viewOf = [](const std::string& m) { const auto at = m.find("=== CANVAS:"); return at == std::string::npos ? std::string{} : m.substr(at); };
        const fs::path d = workflow("canvas-readfocus");
        failedAttempt(d, 1, bad);
        const std::string readTerm = std::string{"=== READ: "} + kFile + " SYMBOL term ===\n";
        const auto firstLine = lines(bad)[0];
        fake::Engine engine({{readTerm}, {std::string{"=== EDIT: "} + kFile + " LINES 1 ===\n# a note\n" + firstLine + "\n=== END EDIT ===\n"}, {fixBySymbol}});
        CHECK_EQ(runLlmAgent(canvasArgsCtx(d, engine, 2, 2, 7168)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), "# a note\n" + ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(3));
        if (rq.size() == 3) {
            CHECK(viewOf(rq[0].last()).find("v = v / d") == std::string::npos);   // folded at first
            CHECK(viewOf(rq[2].last()).find("v = v / d") != std::string::npos);   // open after it was read, an edit later
            CHECK(rq[2].last().find("The lines you read earlier") == std::string::npos);   // the view holds it: nothing carried
        }
        {   // A short window (ctx 6144) has no room to keep it open: it is answered again from the file as it now stands and shown above the view.
            const fs::path ds = workflow("canvas-readfocus-short");
            failedAttempt(ds, 1, bad);
            fake::Engine es({{readTerm}, {std::string{"=== EDIT: "} + kFile + " LINES 1 ===\n# a note\n" + firstLine + "\n=== END EDIT ===\n"}, {fixBySymbol}});
            CHECK_EQ(runLlmAgent(canvasArgsCtx(ds, es, 2, 2, 6144)), 0);
            const auto rs = es.requests();
            CHECK_EQ(rs.size(), static_cast<std::size_t>(3));
            if (rs.size() == 3) {
                const std::string& m = rs[2].last();
                const auto carried = m.find("The lines you read earlier in this attempt");
                CHECK(carried != std::string::npos && carried < m.find("=== CANVAS:"));
                CHECK(between(m, "The lines you read earlier", "=== CANVAS:").find(" 51|                 v = v / d") != std::string::npos);   // renumbered after the edit
            }
        }

        const fs::path d2 = workflow("canvas-readagain");
        failedAttempt(d2, 1, bad);
        fake::Engine engine2({{readTerm}, {readTerm}, {fixBySymbol}});
        CHECK_EQ(runLlmAgent(canvasArgsCtx(d2, engine2, 2, 1, 6144)), 0);
        CHECK_EQ(slurp(attemptDir(d2, 2) / "out" / kFile), ref);
        const auto rq2 = engine2.requests();
        if (rq2.size() == 3) {
            CHECK(rq2[1].last().find("You read all of these lines before") == std::string::npos);
            CHECK(rq2[2].last().find("You read all of these lines before (turn 0)") != std::string::npos);
        }
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
        if (rq.size() == 2) CHECK(rq[1].last().find("got stuck in a loop (the line") != std::string::npos && rq[1].last().find("different angle") != std::string::npos);
        if (rq.size() == 2) CHECK(rq[1].last().find("What is written once is done") != std::string::npos);
        if (rq.size() == 2) CHECK(rq[0].presencePenalty == 0 && rq[1].presencePenalty == 0);   // no repetition penalty: in code, repetition is often right
    }
    // A loop is caught after the fact and rolled back (2026-11-18): the reply up to where the loop began, its first copy included, goes back as the
    // assistant's message with the interception after it, and the reply that follows is joined to it. A second loop in a row is not rolled back.
    const auto refLines = lines(ref);
    std::string head = std::string{"=== FILE: "} + kFile + " ===\n", tail;
    for (std::size_t i = 0; i < refLines.size(); ++i) (i + 10 < refLines.size() ? head : tail) += refLines[i] + "\n";
    {
        const fs::path d = workflow("canvas-loop-rollback");
        fake::Engine engine({{head + tail + tail + tail}, {"=== END FILE ===\n"}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 1, 1)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            CHECK_EQ(rq[1].messages.size(), static_cast<std::size_t>(4));
            if (rq[1].messages.size() == 4) CHECK(rq[1].messages[2].first == "assistant" && rq[1].messages[2].second == head + tail);
            CHECK(rq[1].last().find("got stuck in a loop (the same 10 lines 3 times over)") != std::string::npos);
            CHECK(rq[1].last().find("rolled back to where the loop began") != std::string::npos);
        }
    }
    {
        const fs::path d = workflow("canvas-loop-twice");
        fake::Engine engine({{head + tail + tail + tail}, {tail + tail + tail}, {fileBlock(ref)}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 1, 1)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(3));
        if (rq.size() == 3) {
            CHECK_EQ(rq[2].messages.size(), static_cast<std::size_t>(2));   // nothing kept: started afresh
            CHECK(rq[2].last().find("second loop in a row") != std::string::npos);
        }
    }
    {   // The retry moves on to another block instead of continuing the kept FILE (35B, 2026-11-18): the kept FILE is closed and made whole from the
        // current revision (the functions it had not reached), not thrown away, and the new block applies after it.
        const fs::path d = workflow("canvas-loop-moveon");
        failedAttempt(d, 1, bad + "# not implemented: the marker makes this draft a stub, so a FILE may replace it\n");
        const std::string termHead = term.substr(0, term.find('\n') + 1);
        const auto termAt = ref.find(termHead);
        CHECK(termAt != std::string::npos);
        std::string cycle;
        for (int i = 0; i < 5; ++i) cycle += "        # step back: the divisor\n        # is checked first\n        # then the modulo\n        # then the result\n";
        fake::Engine engine({{std::string{"=== FILE: "} + kFile + " ===\n" + ref.substr(0, termAt) + termHead + cycle}, {fixBySymbol}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 1)), 0);
        const std::string out = slurp(attemptDir(d, 2) / "out" / kFile);
        CHECK(out.rfind(ref.substr(0, termAt), 0) == 0);                               // the kept part, as written
        CHECK(out.find(term) != std::string::npos && out.find("step back") == std::string::npos);
        const auto defs = [](const std::string& s) { std::size_t n = 0; for (auto at = s.find("def "); at != std::string::npos; at = s.find("def ", at + 1)) ++n; return n; };
        CHECK_EQ(defs(out), defs(ref));                                                // every function, once: the unreached ones from the draft
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"aborted\":1") != std::string::npos);
    }
    {   // A cut-off reply is continued as itself (2026-11-18): its text up to the last complete line goes back as the assistant's unfinished message (a
        // prefill), and the model carries on from there, the cut line written whole; nothing is shown as a tail, nothing asked in a user message.
        const fs::path d = workflow("canvas-cut");
        failedAttempt(d, 1, bad);
        const auto cut = fixBySymbol.find("modulo by zero") + 6;                    // in the middle of a string literal
        const auto lineStart = fixBySymbol.rfind('\n', cut) + 1;
        fake::Engine engine({{fixBySymbol.substr(0, cut), "length"}, {fixBySymbol.substr(lineStart)}});
        CHECK_EQ(runLlmAgent(canvasArgsCtx(d, engine, 2, 0, 32768)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);                    // no duplicated text: the file is exactly the reference
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            CHECK(rq[1].messages.back().first == "assistant" && rq[1].last() == fixBySymbol.substr(0, lineStart));
        }
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"continued\":1,\"prefilled\":1") != std::string::npos);
    }
    {   // llama.cpp's reply repeats the prefill before the continuation: the repeat is taken off.
        const fs::path d = workflow("canvas-cut-echo");
        failedAttempt(d, 1, bad);
        const auto cut = fixBySymbol.find("modulo by zero") + 6;
        fake::Engine engine({{fixBySymbol.substr(0, cut), "length"}, {fixBySymbol}});
        CHECK_EQ(runLlmAgent(canvasArgsCtx(d, engine, 2, 0, 32768)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
    }
    {   // No room for the prefill in the context: the fallback shows the tail and asks for the cut line again in full, joined at the line boundary.
        const fs::path d = workflow("canvas-cut-line");
        failedAttempt(d, 1, bad);
        const auto cut = fixBySymbol.find("modulo by zero") + 6;
        const auto lineStart = fixBySymbol.rfind('\n', cut) + 1;
        fake::Engine engine({{fixBySymbol.substr(0, cut), "length"}, {"```\n" + fixBySymbol.substr(lineStart)}});
        CHECK_EQ(runLlmAgent(canvasArgsCtx(d, engine, 2, 0, 1024)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        if (rq.size() == 2) CHECK(rq[1].last().find("Continue from the start of that unfinished line: write it again in full") != std::string::npos);
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"prefilled\":0") != std::string::npos);
    }
    {   // A first attempt starts from the workspace's stub (revision 1), not from nothing; a file written whole becomes revision 2. The snippet form works too.
        const fs::path d = workflow("canvas-first");
        fake::Engine engine({{fileBlock(ref)}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 1, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK(!rq.empty() && rq[0].last().find("These files are what the workspace starts with (calc/engine.py)") != std::string::npos);
        CHECK(!rq.empty() && rq[0].last().find("The tests were run on them as they stand and failed") != std::string::npos);   // judged before the first turn
        CHECK(!rq.empty() && rq[0].last().find("=== CANVAS: calc/engine.py (revision 1,") != std::string::npos);
        CHECK(slurp(attemptDir(d, 1) / "canvas.json").find("\"calc/engine.py\":2") != std::string::npos);

        const fs::path d2 = workflow("canvas-search");
        failedAttempt(d2, 1, bad);
        const std::string badLine = lines(bad)[static_cast<std::size_t>(bugLine) - 1];
        fake::Engine engine2({{std::string{"=== EDIT: "} + kFile + " ===\n<<<<<<< SEARCH\n" + badLine + "\n=======\n" + fixedLine + "\n>>>>>>> REPLACE\n=== END EDIT ===\n"}});
        CHECK_EQ(runLlmAgent(canvasArgs(d2, engine2, 2, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d2, 2) / "out" / kFile), ref);
    }
    {   // Edits first: a FILE over a file that exists and is not a stub is refused (nothing applied) until edits stop making progress; the edit that
        // follows applies (tier 0 wrong-approach of 2026-11-18: whole rewrites on turn 0 ran past the token limit and did not compile).
        const fs::path d = workflow("canvas-scope");
        failedAttempt(d, 1, bad);
        fake::Engine engine({{fileBlock(ref)}, {fixBySymbol}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 1)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        CHECK(!rq.empty() && rq[0].messages[0].second.find("is only a stub: FILE") != std::string::npos);
        CHECK(!rq.empty() && rq[0].last().find("SYMBOL (as many as it takes; FILE only for a file that is new or a stub)") != std::string::npos);
        if (rq.size() == 2) CHECK(rq[1].last().find("Your reply was stopped: FILE would replace calc/engine.py, which exists and is not a stub") != std::string::npos);
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"restructuredAt\":-1") != std::string::npos);
    }
    {   // Escalation on repeat failure of the second kind: two replies in a row stopped for a rewrite widen the scope, and the third rewrite is accepted
        // (35B, 2026-11-18: with stops alone, every turn of three runs was stopped).
        const fs::path d = workflow("canvas-scope-again");
        failedAttempt(d, 1, bad);
        fake::Engine engine({{fileBlock(ref)}, {fileBlock(ref)}, {fileBlock(ref)}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 2)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(3));
        if (rq.size() == 3) {
            CHECK(rq[1].last().find("RESTRUCTURE ALLOWED") == std::string::npos);
            CHECK(rq[2].last().find("RESTRUCTURE ALLOWED: 2 replies in a row were stopped for growing into a rewrite") != std::string::npos);
        }
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"restructuredAt\":1") != std::string::npos);
    }
    {   // Escalation: two tested revisions in a row without fewer failing tests widen the scope; the worker is told, and a FILE is then accepted.
        const fs::path d = workflow("canvas-restructure");
        failedAttempt(d, 1, bad);
        const auto still = [&](const std::string& note) { return std::string{"=== EDIT: "} + kFile + " LINES " + std::to_string(bugLine) + " ===\n                v = v * d  # " + note + "\n=== END EDIT ===\n"; };
        const std::string rewritten = "# rewritten whole: a new line 1 moves every line, so this is no restatement\n" + ref;
        fake::Engine engine({{still("still wrong")}, {still("still wrong again")}, {fileBlock(rewritten)}});
        CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 2)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), rewritten);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(3));
        if (rq.size() == 3) {
            CHECK(rq[1].last().find("RESTRUCTURE ALLOWED") == std::string::npos);   // one revision without progress is not yet a pattern
            CHECK(rq[2].last().find("RESTRUCTURE ALLOWED: 2 tested revisions in a row have not reduced the failing tests") != std::string::npos);
            CHECK(rq[2].last().find("the approach wrong, the complete new file") != std::string::npos);
        }
        CHECK(slurp(attemptDir(d, 2) / "agent.json").find("\"restructuredAt\":1") != std::string::npos);
        CHECK(slurp(attemptDir(d, 2) / "conversation.md").find("scope widened: restructure allowed: 2 tested revisions in a row have not reduced the failing tests") != std::string::npos);
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
    {   // Thinking that goes round in a loop is stopped where the loop began, well before the cap: what came before it (its first copy included) is carried,
        // named as a loop, and the next turn decides with thinking off.
        const fs::path d = workflow("canvas-thinking-loop");
        failedAttempt(d, 1, bad);
        std::string loop;
        for (int i = 0; i < 12; ++i) loop += "Wait, let me reconsider the modulo branch.\nMaybe the divisor check is wrong here.\nNo, the divisor check is fine as it is.\n";
        fake::Reply thinker{"", "stop", "so the modulo branch divides: replace it with math.fmod\n" + loop};
        thinker.pieceChars = 60;
        thinker.pauseMs = 1;
        fake::Engine engine({thinker, {fixByLine}});
        std::vector<std::string> args = canvasArgs(d, engine, 2, 0);
        for (std::size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == "--engine") args[i + 1] += ",thinking=1";
        CHECK_EQ(runLlmAgent(args), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        if (rq.size() == 2) {
            const std::string& m = rq[1].last();
            CHECK(m.find("went round in a loop (the same 3 lines") != std::string::npos && m.find("replace it with math.fmod") != std::string::npos);
            const auto first = m.find("Maybe the divisor check is wrong here.");
            CHECK(first != std::string::npos && m.find("Maybe the divisor check is wrong here.", first + 1) == std::string::npos);   // its first copy only
            CHECK(rq[1].thinking == 0);
        }
        CHECK(engine.abortedAfter(0) >= 0);
    }
    {   // Think, plan, act (SUB0SQUAD_THINKPLAN=steps): a thinking turn is asked for the plan only; its plan is carried into the next turn, which runs
        // with thinking off and writes the transaction. The plan turn uses no turn of the budget.
        const auto setPlan = [](const char* v) {
#ifdef _WIN32
            _putenv_s("SUB0SQUAD_THINKPLAN", v);
#else
            setenv("SUB0SQUAD_THINKPLAN", v, 1);
#endif
        };
        for (const char* mode : {"steps", "options"}) {
            setPlan(mode);
            const bool options = std::string_view{mode} == "options";
            const fs::path d = workflow((std::string{"canvas-thinkplan-"} + mode).c_str());
            failedAttempt(d, 1, bad);
            fake::Engine engine({{"=== PLAN ===\nThe modulo branch divides: replace it with math.fmod.\n=== END PLAN ===\n", "stop", "the cause is the modulo branch"}, {fixByLine}});
            std::vector<std::string> args = canvasArgs(d, engine, 2, 0);
            for (std::size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == "--engine") args[i + 1] += ",thinking=1";
            CHECK_EQ(runLlmAgent(args), 0);
            setPlan("");
            CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
            const auto rq = engine.requests();
            CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
            if (rq.size() == 2) {
                CHECK(rq[0].thinking == 1 && rq[0].last().find("reply with your plan only") != std::string::npos);
                CHECK(rq[0].last().find(options ? "three different options" : "do not write the code in it") != std::string::npos);
                if (options) CHECK(rq[0].last().find("leave the code for the act turn") != std::string::npos);
                const std::string& m = rq[1].last();
                CHECK(m.find("Your plan from the last turn:\nThe modulo branch divides: replace it with math.fmod.") != std::string::npos);
                CHECK(m.find("reply with your plan only") == std::string::npos);
                CHECK(rq[1].thinking == 0);
            }
            CHECK(slurp(attemptDir(d, 2) / "conversation.md").find("plan written") != std::string::npos);
        }
    }
    {   // A failing test named by the gate opens its definition in the folded view of a test file (tier 0 write-tests: no line number to focus on).
        std::string tests = "#include \"helpers/check.hpp\"\n";
        for (int k = 0; k < 20; ++k) {
            tests += "TEST(t" + std::to_string(k) + ") {\n";
            for (int j = 0; j < 6; ++j) tests += "    CHECK_EQ(f(" + std::to_string(k) + "), 0);\n";
            tests += "}\n";
        }
        const std::string gate = "GATE FAIL: 1 of 20 tests failed\nFAIL 13 - t13: CHECK_EQ failed: f(13) is 1 but 0 is 0\n";
        const std::string view = agent::draftView("tests/more.test.cpp", tests, gate, 2500);
        CHECK(view.find("lines folded") != std::string::npos && view.find("CHECK_EQ(f(13), 0);") != std::string::npos && view.find("CHECK_EQ(f(12), 0);") == std::string::npos);
    }
    {   // The issues layer (docs/LAYERED_DOCUMENT.md): a failing commit's verdict is shown as a numbered summary, each issue with the function it is likely
        // in; `=== READ: ISSUE n ===` answers with that issue in full and its function's current lines (no turn used); the layers are written per turn
        // for a viewer. With SUB0SQUAD_ISSUE_LAYER=0 the same verdict is the gate's text as before.
        const auto setLayer = [](const char* v) {
#ifdef _WIN32
            _putenv_s("SUB0SQUAD_ISSUE_LAYER", v);
#else
            setenv("SUB0SQUAD_ISSUE_LAYER", v, 1);
#endif
        };
        const std::string wrong = std::string{"=== EDIT: "} + kFile + " LINES " + std::to_string(bugLine) + " ===\n                v = v * d  # still wrong\n=== END EDIT ===\n";
        // Slice 2: mode 1 also shows the first issues in full with their function's code; mode 2 (adaptive, the default) sends a verdict that fits
        // unchanged (this one: one short failure), while still writing the layers for a viewer.
        for (const char* mode : {"1", "0", "2"}) {
            const bool on = std::string_view{mode} == "1", adaptive = std::string_view{mode} == "2";
            setLayer(mode);
            const fs::path d = workflow((std::string{"canvas-issues-"} + mode).c_str());
            failedAttempt(d, 1, bad);
            std::vector<fake::Reply> replies{fake::Reply{wrong}};
            if (on) replies.push_back(fake::Reply{"=== READ: ISSUE 1 ===\n"});
            replies.push_back(fake::Reply{fixByLine});
            fake::Engine engine(std::move(replies));
            CHECK_EQ(runLlmAgent(canvasArgs(d, engine, 2, 1)), 0);
            CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
            const auto rq = engine.requests();
            CHECK_EQ(rq.size(), static_cast<std::size_t>(on ? 3 : 2));
            if (rq.size() != (on ? 3u : 2u)) continue;
            const std::string stats = slurp(attemptDir(d, 2) / "agent.json");
            const std::string layers = slurp(attemptDir(d, 2) / "layers" / "turn-1.json");
            if (on) {
                CHECK(rq[1].last().find("1 issue (`=== READ: ISSUE n ===`") != std::string::npos);
                CHECK(rq[1].last().find("likely in term()") != std::string::npos);
                CHECK(rq[1].last().find("In full:\n=== ISSUE 1 (test) ===") != std::string::npos && rq[1].last().find("v = v * d  # still wrong") != std::string::npos);
                CHECK(rq[2].last().find("=== ISSUE 1 (test) ===") != std::string::npos && rq[2].last().find("v = v * d  # still wrong") != std::string::npos);
                CHECK(stats.find("\"issueLayer\":true") != std::string::npos && stats.find("\"issueReads\":1") != std::string::npos);
                CHECK(stats.find("\"issueMode\":1") != std::string::npos && stats.find("\"issueInline\":3") != std::string::npos && stats.find("\"layerShown\":1") != std::string::npos);
                CHECK(layers.find("\"schema\":\"layered-doc-1\"") != std::string::npos && layers.find("\"issues\":[{") != std::string::npos);
            } else if (adaptive) {
                CHECK(rq[1].last().find("=== READ: ISSUE") == std::string::npos && rq[1].last().find("=== ISSUE 1") == std::string::npos);
                CHECK(rq[1].last().find("FAIL") != std::string::npos);   // the gate's own text
                CHECK(stats.find("\"issueMode\":2") != std::string::npos && stats.find("\"layerShown\":0") != std::string::npos && stats.find("\"layerPassed\":1") != std::string::npos);
                CHECK(layers.find("\"issues\":[{") != std::string::npos);
            } else {
                CHECK(rq[1].last().find("=== READ: ISSUE") == std::string::npos);
                CHECK(stats.find("\"issueLayer\":false") != std::string::npos && stats.find("\"issueMode\":0") != std::string::npos);
                CHECK(!fs::exists(attemptDir(d, 2) / "layers"));
            }
        }
        setLayer("2");
    }
    return finish();
}
