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
    {   // A reply with no usable file fails the attempt with exit 3 (nothing was written), after the turns it was given.
        const fs::path d = workflow("agent-prose");
        fake::Engine engine({{"I think the answer is to use a recursive descent parser."}});
        CHECK_EQ(runLlmAgent(agentArgs(d, engine, 1, 1)), 3);
        CHECK_EQ(engine.requestCount(), static_cast<std::size_t>(2));
    }
    return finish();
}
