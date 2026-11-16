// The stance texts and the CONCERN parser (include/sub0squad/stance.hpp). No model, no files except one temporary directory.
#include "check.hpp"
#include "sub0squad/stance.hpp"
#include "sub0squad/workflow.hpp"

#include <fstream>
#include <sstream>

using namespace sub0squad;

int main()
{
    {   // `baseline` is exactly the wording every run before stances used (an A/B arm must not drift).
        CHECK_EQ(stanceText("baseline", "tester", WorkerShape::files),
                 std::string{"You are the tester on a small software squad: you own one small, focused piece of a whole solution. Be the expert and do it as well as it can be done, "
                             "keep to your contract and the exact interfaces you are given so the other pieces fit, and write only the files you are asked to write. "
                             "Write tests for the contract; do not write the implementation.\n"});
        CHECK_EQ(stanceText("baseline", "developer", WorkerShape::canvas),
                 std::string{"You are the developer on a small software squad: you own one small, focused piece of a whole solution. Be the expert and do it as well as it can be done, "
                             "keep to your contract and the exact interfaces you are given so the other pieces fit, and change only the files you are asked to write. "
                             "Implement the contract so that the provided tests pass.\n"});
        CHECK_EQ(stanceText("baseline", "developer", WorkerShape::section), std::string{"You are the developer on a small software squad.\n"});
        CHECK(concernSyntax("baseline").empty());
    }
    {   // `specialist`: a dedicated assignment, the standing to contest and delegate, the concern form, the persona last.
        const std::string dev = stanceText("specialist", "developer", WorkerShape::files);
        CHECK(dev.find("dedicated, specialised assignment") != std::string::npos);
        CHECK(dev.find("claims made by other specialists, not orders") != std::string::npos);
        CHECK(dev.find("Never bend correct work") != std::string::npos);
        CHECK(dev.find("split off and delegated") != std::string::npos);
        CHECK(dev.find("=== CONCERN: <kind> ===") != std::string::npos);
        CHECK(dev.find("write only the files") != std::string::npos);
        CHECK(dev.rfind("Your piece is the implementation") > dev.find("=== CONCERN"));   // persona last: the shared text comes first for the prompt cache
        CHECK(stanceText("specialist", "developer", WorkerShape::canvas).find("change only the files") != std::string::npos);
        CHECK(stanceText("specialist", "tester", WorkerShape::files).find("A test is right only if a correct implementation passes it") != std::string::npos);
        const std::string section = stanceText("specialist", "developer", WorkerShape::section);
        CHECK(section.find("ONE function") != std::string::npos);
        CHECK(section.find("before your edit block") != std::string::npos);
        for (const auto& k : concernKinds()) CHECK(concernSyntax("specialist").find(k + " (") != std::string::npos);
        CHECK(isKnownStance("baseline") && isKnownStance("specialist") && !isKnownStance("") && !isKnownStance("Specialist"));
    }
    {   // A concern with its END line, then the work: the concern comes out, the work is left exactly as it was.
        std::string reply = "=== CONCERN: Contradictory ===\ntests/a.py:3 expects 7.\nThe contract gives 14.\n=== END CONCERN ===\n=== FILE: a.py ===\nx = 1\n=== END FILE ===\n";
        const auto c = takeConcerns(reply);
        CHECK_EQ(c.size(), static_cast<std::size_t>(1));
        CHECK(c.size() == 1 && c[0].kind == "contradictory");
        CHECK(c.size() == 1 && c[0].text == "tests/a.py:3 expects 7.\nThe contract gives 14.");
        CHECK_EQ(reply, std::string{"=== FILE: a.py ===\nx = 1\n=== END FILE ===\n"});
    }
    {   // No END line: the concern ends at the next block; two concerns; a trailing one runs to the end of the reply.
        std::string reply = "=== CONCERN: wrong ===\nfirst\n=== EDIT: a.py LINES 2 ===\ny = 2\n=== END EDIT ===\n=== CONCERN: better-idea ===\nuse a table\n";
        const auto c = takeConcerns(reply);
        CHECK_EQ(c.size(), static_cast<std::size_t>(2));
        CHECK(c.size() == 2 && c[0].kind == "wrong" && c[0].text == "first");
        CHECK(c.size() == 2 && c[1].kind == "better-idea" && c[1].text == "use a table");
        CHECK_EQ(reply, std::string{"=== EDIT: a.py LINES 2 ===\ny = 2\n=== END EDIT ===\n"});
    }
    {   // Not at the start of a line (inside a file's text, a string, a comment): not a concern, and the reply is untouched.
        std::string reply = "=== FILE: a.py ===\ns = \"=== CONCERN: wrong ===\"\n=== END FILE ===\n";
        const std::string before = reply;
        CHECK(takeConcerns(reply).empty());
        CHECK_EQ(reply, before);
    }
    {   // A reply with only a concern leaves nothing behind; the acknowledgement names the kinds and says what to do now.
        std::string reply = "=== CONCERN: too-big ===\nThe CLI and the engine are two pieces.\n=== END CONCERN ===\n";
        const auto c = takeConcerns(reply);
        CHECK_EQ(c.size(), static_cast<std::size_t>(1));
        CHECK(reply.find("=== ") == std::string::npos);
        const std::string ack = concernAcknowledgement(c);
        CHECK(ack.find("(too-big)") != std::string::npos);
        CHECK(ack.find("keep code that is right") != std::string::npos);
    }
    {   // Recorded with the attempt: who raised it, on which turn, its kind and text; appended, never overwritten.
        const auto dir = makeTempDir("stance");
        CHECK(recordConcerns(dir, "dev-engine", 0, {{"wrong", "first"}}));
        CHECK(recordConcerns(dir, "dev-engine", 2, {{"missing", "second"}}));
        CHECK(recordConcerns(dir, "dev-engine", 3, {}));
        std::ifstream in(dir / "concerns.md");
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        CHECK(text.find("## CONCERN: wrong  (dev-engine, turn 0)\n\nfirst") != std::string::npos);
        CHECK(text.find("## CONCERN: missing  (dev-engine, turn 2)\n\nsecond") != std::string::npos);
        removeTree(dir);
    }
    {   // The workflow spec carries the stance: written only when it is not baseline (a baseline spec reads as before), read back, and checked.
        WorkflowSpec s;
        s.task = "calculator-cpp";
        CHECK(s.toJson().find("\"stance\"") == std::string::npos);
        s.stance = "specialist";
        const auto back = WorkflowSpec::fromJson(s.toJson());
        CHECK(back.has_value() && back->stance == "specialist");
        std::string bad = s.toJson();
        bad.replace(bad.find("\"specialist\""), 12, "\"bold\"");
        CHECK(!WorkflowSpec::fromJson(bad).has_value());
    }
    return finish();
}
