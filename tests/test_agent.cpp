// The worker agent (`llm-agent`) driven by recorded replies: no model. A fake engine (tests/fake_engine.hpp) streams scripted replies; the real Python gate
// judges what the agent wrote. This pins the reply forms the agent has always accepted (a whole file, a LINES edit against a numbered draft, a read request
// answered in the same conversation, a file cut off at the token limit and continued), so the forms keep working while the loop is rebuilt.
#include "agent_bench.hpp"

using namespace sub0squad;
using namespace bench;

int main()
{
    const std::string ref = reference(), bad = defective();
    CHECK(!ref.empty() && !bad.empty() && ref != bad);
    const int bugLine = lineOf(bad, "v = v / d");
    CHECK(bugLine > 0);
    const std::string fixedLine = lines(ref)[static_cast<std::size_t>(bugLine) - 1];

    {   // A whole-file reply is written, self-checked by the gate and reported as verified. One call.
        const fs::path d = workflow("agent-file");
        fake::Engine engine({{fileBlock(ref)}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 1)), 0);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(1));
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        CHECK(slurp(attemptDir(d, 1) / "agent.json").find("\"selfcheck\":true") != std::string::npos);
        const auto rq = engine.requests();
        CHECK(!rq.empty() && rq[0].stream);                                        // the turn streams (live.txt)
        CHECK(!rq.empty() && rq[0].last().find("Files to write: calc/engine.py") != std::string::npos);
        CHECK(!fs::exists(attemptDir(d, 1) / "live.txt"));                         // removed when the turn ends
    }
    {   // A retry is shown the earlier draft with line numbers and repairs it with a LINES edit; nothing else changes.
        const fs::path d = workflow("agent-lines");
        failedAttempt(d, 1, bad);
        fake::Engine engine({{std::string{"=== EDIT: "} + kFile + " LINES " + std::to_string(bugLine) + " ===\n" + fixedLine + "\n=== END EDIT ===\n"}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 2)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(1));
        CHECK(!rq.empty() && rq[0].last().find("=== DRAFT: calc/engine.py") != std::string::npos);
        CHECK(!rq.empty() && rq[0].last().find("| " + lines(bad)[static_cast<std::size_t>(bugLine) - 1]) != std::string::npos);   // the defective line, numbered
        CHECK(!rq.empty() && rq[0].last().find("modulo") != std::string::npos);                                                     // the failure text
    }
    {   // A read request is answered in the same conversation and costs no repair turn; the edit that follows is applied.
        const fs::path d = workflow("agent-read");
        failedAttempt(d, 1, bad);
        fake::Engine engine({{std::string{"=== READ: "} + kFile + " SYMBOL term ===\n"},
                             {std::string{"=== EDIT: "} + kFile + " LINES " + std::to_string(bugLine) + " ===\n" + fixedLine + "\n=== END EDIT ===\n"}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 2, 0)), 0);   // no repair turns at all: the read must not use one
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        CHECK(rq.size() == 2 && rq[1].last().find("def term(self):") != std::string::npos);
    }
    {   // A file cut off at the token limit is continued, not started over: the two replies make one file.
        const fs::path d = workflow("agent-cut");
        const auto cut = ref.rfind('\n', ref.size() / 2) + 1;   // at a line boundary
        fake::Engine engine({{std::string{"=== FILE: "} + kFile + " ===\n" + ref.substr(0, cut), "length"}, {ref.substr(cut)}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 1, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK_EQ(rq.size(), static_cast<std::size_t>(2));
        CHECK(rq.size() == 2 && rq[1].last().find("Continue that file from exactly that point") != std::string::npos);
    }
    // ---- turn control in the session loop (docs/INTERVENTIONS.md rows 34 to 36) ----
    {   // Row 35, as it happened: a file cut in the middle of a string, and a continuation that starts a few characters early. The splice must not duplicate.
        const fs::path d = workflow("agent-overlap");
        const std::string whole = std::string{"=== FILE: "} + kFile + " ===\n" + ref;
        const auto cut = whole.find("modulo by zero") + 6;
        fake::Engine engine({{whole.substr(0, cut), "length"}, {whole.substr(cut - 17)}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 1, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 1) / "out" / kFile), ref);
    }
    {   // Row 34: an EDIT reply cut off at the token limit is continued by appending (not answered with "write the complete file").
        const fs::path d = workflow("agent-editcut");
        failedAttempt(d, 1, bad);
        const std::string editReply = std::string{"=== EDIT: "} + kFile + " LINES " + std::to_string(bugLine) + " ===\n" + fixedLine + "\n=== END EDIT ===\n";
        const auto cut = editReply.find("math.fmod") + 6;
        fake::Engine engine({{editReply.substr(0, cut), "length"}, {editReply.substr(cut - 10)}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 2, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        const auto rq = engine.requests();
        CHECK(rq.size() == 2 && rq[1].last().find("cut off at the token limit in the middle of an edit") != std::string::npos);
    }
    {   // Row 34: a SEARCH block that copies the file is cancelled while it streams; the re-prompt asks for LINES. A function is replaced by name (SYMBOL).
        const fs::path d = workflow("agent-abort");
        failedAttempt(d, 1, bad);
        fake::Reply slow{std::string{"=== EDIT: "} + kFile + " ===\n<<<<<<< SEARCH\n" + bad + "=======\n" + ref + ">>>>>>> REPLACE\n=== END EDIT ===\n"};
        slow.pieceChars = 64;
        slow.pauseMs = 3;
        std::string term;
        {
            const auto v = lines(ref);
            const int from = lineOf(ref, "def term(self):"), to = lineOf(ref, "# Unary minus binds looser") - 2;
            for (int k = from; k <= to; ++k) term += v[static_cast<std::size_t>(k) - 1] + "\n";
        }
        fake::Engine engine({slow, {std::string{"=== EDIT: "} + kFile + " SYMBOL term ===\n" + term + "=== END EDIT ===\n"}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 2, 0)), 0);
        CHECK_EQ(slurp(attemptDir(d, 2) / "out" / kFile), ref);
        CHECK(engine.abortedAfter(0) >= 0);
        const auto rq = engine.requests();
        CHECK(rq.size() == 2 && rq[1].last().find("SEARCH block copied more than 40 lines") != std::string::npos);
        CHECK(!rq.empty() && rq[0].last().find("=== EDIT: src/calc.cpp LINES 41 ===") != std::string::npos);   // the worked LINES example is in the edit prompt
    }
    {   // Row 36: reads cannot loop. Two are free; the third spends the only turn, so the attempt ends after three calls with the earlier draft's failure standing.
        const fs::path d = workflow("agent-readloop");
        failedAttempt(d, 1, bad);
        fake::Engine engine({{std::string{"=== READ: "} + kFile + " SYMBOL term ===\n"}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 2, 0)), 3);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(3));
        const auto rq = engine.requests();
        CHECK(rq.size() == 3 && rq[2].last().find("MUST be an edit") != std::string::npos);
    }
    {   // A reply with no usable file fails the attempt with exit 3 (nothing was written), after the turns it was given.
        const fs::path d = workflow("agent-prose");
        fake::Engine engine({{"I think the answer is to use a recursive descent parser."}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 1, 1)), 3);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(2));
    }
    return finish();
}
