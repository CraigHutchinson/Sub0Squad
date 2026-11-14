// The real-run blockers of run-v12 (catalogue rows 74 and 75) through the DAEMON, with no model: a real daemon, the real command line, real `llm-fork`
// and gate jobs (real C++ builds) and a fake engine that answers from the reference calculator the way the 35B did.
//   known diagnostic (row 74)  the entry point `evaluate` is written, every time it is asked, with run-v12's mistake: `auto pos = text.begin();` passed to a
//              `const_iterator&` parameter. The compiler's refusal matches the diagnostics table; the harness applies the table's mechanical edit
//              (`.begin()` to `.cbegin()`, `.end()` to `.cend()`) to the reply and the function passes: ONE call for the section, no refusal turn, no
//              decision of the ladder, and `fork.json` says which diagnostic was mended.
//   thrash and first cause (rows 74 b, c and 75)  `evaluate` is written with an error the table does NOT know, and the model sends the IDENTICAL reply
//              every time. The second turn is told its reply was identical (not the same refusal again); after three refusals the section gets one turn
//              under `think`; the attempt ends with the section unaccepted. The gate names it as the FIRST CAUSE and exits 8; the ladder's decision is
//              `retry` for that reason (no mend, no sections, no rearchitect); attempt 2 resumes the canvas and asks ONLY for `evaluate`. Every refused
//              turn is recorded as thrash (the first error repeated).
#include "calculator_fake.hpp"
#include "sub0squad/harness_registry.hpp"

using namespace calcfake;

/// `evaluate` as run-v12's model wrote it, in the reference's shape: an `iterator` passed where a `const_iterator&` is taken.
static const char* const kIteratorMistake =
    "double evaluate(const std::string& expr, bool degrees)\n"
    "{\n"
    "    struct Skip { static void blanks(std::vector<char>::const_iterator& p, const std::vector<char>::const_iterator& e) { while (p != e && *p == ' ') ++p; } };\n"
    "    std::vector<char> text(expr.begin(), expr.end());\n"
    "    auto pos = text.begin();\n"
    "    auto end = text.end();\n"
    "    Skip::blanks(pos, end);\n"
    "    return Parser(std::string(pos, end), degrees).run();\n"
    "}\n";

/// `evaluate` with an error no table knows.
static const char* const kUnknownMistake =
    "double evaluate(const std::string& expr, bool degrees)\n"
    "{\n"
    "    return Parser(expr, degrees).run() + missingTerm;\n"
    "}\n";

int main(int, char** argv)
{
    sub0squad::registerBuiltinHarnesses();   // the registry starts empty: this program is its own composition root
    const std::string exe = argv[1];
    const auto complete = [](const std::string&, const std::string& declared) { return declared; };   // a complete skeleton: every function a section
    {   // ---- row 74: a known diagnostic is mended by the harness, in the section's own turn ----
        Squad sq(exe, "known-diagnostic");
        sq.startDaemon(4, {"--frontier", "off"});
        Calculator calc;
        calc.declare = complete;
        calc.section = [](const std::string& name, const std::string& file, const std::string& text, int) { return file == "src/calc.cpp" && name == "evaluate" ? std::string{kIteratorMistake} : text; };
        fs::path dir;
        const std::string status = runWorkflow(sq, calc, "standard", dir);
        CHECK_EQ(field(status, "state"), std::string("succeeded"));
        CHECK(status.find("\"decisions\":[]") != std::string::npos);                       // nothing reached the ladder
        const fs::path item = dir / "items" / "dev-engine";
        const std::string fork1 = slurp(item / "attempt-1" / "fork.json");
        if (field(status, "state") != "succeeded") std::fprintf(stderr, "gate: %.900s\nfork: %.1500s\n", slurp(item / "attempt-1" / "gate.txt").c_str(), fork1.c_str());
        const std::string section = between(fork1, "\"name\":\"evaluate\"", "}");
        CHECK(fork1.find("\"passed\":true") != std::string::npos && fork1.find("\"mechanicalMends\":1") != std::string::npos);
        CHECK(section.find("\"calls\":1") != std::string::npos && section.find("\"refused\":0") != std::string::npos);
        CHECK(section.find("\"mended\":\"iterator-for-const-iterator-ref\"") != std::string::npos && section.find("\"thrash\":0") != std::string::npos);
        CHECK_EQ(calc.asked["src/calc.cpp#evaluate"], 1);
        const std::string written = slurp(item / "attempt-1" / "out" / "src" / "calc.cpp");
        CHECK(written.find("auto pos = text.cbegin();") != std::string::npos && written.find("auto end = text.cend();") != std::string::npos && written.find("text.begin()") == std::string::npos);
        const std::string talk = slurp(item / "attempt-1" / "conversation-evaluate.md");
        CHECK(talk.find("after the harness applied the known fix `iterator-for-const-iterator-ref` (const-iterators)") != std::string::npos);
        CHECK(!fs::exists(item / "attempt-2"));
    }
    {   // ---- rows 74 (b, c) and 75: identical replies are thrash; the unaccepted section is the first cause ----
        Squad sq(exe, "thrash");
        sq.startDaemon(4, {"--frontier", "off"});
        Calculator calc;
        calc.declare = complete;
        // Attempt 1 asks 7 times (three ordinary turns and one under think in the writing phase, three more in the one assembly round): all the same
        // wrong text. The 8th request is attempt 2's.
        calc.section = [](const std::string& name, const std::string& file, const std::string& text, int nth) { return file == "src/calc.cpp" && name == "evaluate" && nth <= 7 ? std::string{kUnknownMistake} : text; };
        fs::path dir;
        const std::string status = runWorkflow(sq, calc, "standard", dir);
        CHECK_EQ(field(status, "state"), std::string("succeeded"));
        const fs::path item = dir / "items" / "dev-engine";
        const std::string fork1 = slurp(item / "attempt-1" / "fork.json"), gate1 = slurp(item / "attempt-1" / "gate.txt");
        if (field(status, "state") != "succeeded")
            for (int n = 1; n <= 3; ++n) std::fprintf(stderr, "--- attempt %d\ngate: %.600s\nfork: %.1800s\n", n, slurp(item / ("attempt-" + std::to_string(n)) / "gate.txt").c_str(), slurp(item / ("attempt-" + std::to_string(n)) / "fork.json").c_str());
        // Attempt 1: the section was never accepted; every refused turn after the first repeated the first error (thrash) with the identical text.
        CHECK(fork1.find("\"failure\":\"sections-missing\"") != std::string::npos && fork1.find("sections never accepted: evaluate") != std::string::npos);
        const std::string section = between(fork1, "\"name\":\"evaluate\"", "}");
        CHECK(section.find("\"calls\":7") != std::string::npos && section.find("\"refused\":7") != std::string::npos && section.find("\"committed\":false") != std::string::npos);
        CHECK(section.find("\"progress\":0") != std::string::npos && section.find("\"thrash\":5") != std::string::npos && section.find("\"identicalReplies\":5") != std::string::npos);   // turns 2 to 4 of the first session, 2 and 3 of the second
        CHECK(section.find("\"thought\":true") != std::string::npos);
        CHECK(fork1.find("\"thrashTurns\":5") != std::string::npos && fork1.find("\"progressTurns\":0") != std::string::npos);
        const std::string talk = slurp(item / "attempt-1" / "conversation-evaluate.md");
        CHECK(talk.find("Your reply is IDENTICAL to your previous one, so it fails in exactly the same way.") != std::string::npos);
        CHECK(talk.find("refused (thrash: the identical reply again)") != std::string::npos);
        CHECK_EQ(occurrences(talk, "Your reply was REFUSED and nothing changed"), 2);      // only the FIRST refusal of each session is the plain one
        // The gate: the first cause first, exit 8, no mend.
        CHECK(gate1.rfind("GATE FAIL: FIRST CAUSE: never accepted: evaluate", 0) == 0);
        CHECK(!fs::exists(item / "attempt-1" / "mend.json"));
        const std::string jobs = sq.cli({"status", "--json"}).out;
        CHECK_EQ(occurrences(jobs, "\"code\":8"), 1);
        // The ladder: one retry, for the first cause; nothing else.
        const std::string decisions = between(status, "\"decisions\":", "\"ledger\"");
        CHECK_EQ(occurrences(decisions, "\"action\":\"retry\""), 1);
        CHECK_EQ(occurrences(decisions, "\"action\":\"mend\"") + occurrences(decisions, "\"action\":\"sections\"") + occurrences(decisions, "\"action\":\"rearchitect\"") + occurrences(decisions, "\"action\":\"reviseTests\"") + occurrences(decisions, "\"action\":\"think\""), 0);
        CHECK(decisions.find("first cause: a section of the file was never accepted") != std::string::npos && decisions.find("\"unacceptedSection\":true,\"unacceptedBefore\":false") != std::string::npos);
        // Attempt 2: the canvas of attempt 1, no skeleton, ONE section asked, and it passes.
        const std::string fork2 = slurp(item / "attempt-2" / "fork.json");
        CHECK(fork2.find("\"passed\":true") != std::string::npos && fork2.find("\"resumedFrom\":1") != std::string::npos && fork2.find("\"skeletonTries\":0") != std::string::npos);
        CHECK_EQ(calc.asked["src/calc.cpp#evaluate"], 8);
        CHECK_EQ(static_cast<int>(std::count(calc.sectionsAsked.begin(), calc.sectionsAsked.end(), std::string("term"))), 1);   // written once, in attempt 1
        CHECK(!fs::exists(item / "attempt-3"));
    }
    return finish();
}
