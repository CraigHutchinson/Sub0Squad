// The real-run blockers of run-v11 (catalogue rows 70 to 73) through the DAEMON, with no model: a real daemon, the real command line, real `llm-fork` and
// gate jobs (real C++ builds) and a fake engine that answers from the reference calculator the way the 35B did.
//   near pass  (rows 70, 72, 73) `term` is first written with a defect that fails 1 of 16 tests: a near pass. A repair round then makes it WORSE (attempt 1)
//              and another BREAKS THE FILE outside every section (attempt 2, run-v11's `expected unqualified-id before 'if'`). Each round is undone, every
//              attempt ends on the best state, every gate exits 3, the ladder only ever says `retry` (never `sections`: the item already forked), the
//              third attempt starts from the best canvas with no skeleton call and fixes the one function. Every failed attempt says why.
//   members    (row 71) the skeleton declares a class whose member functions have no section, as attempts 3, 7 and 8 of run-v11 did.
//     heal     prevention off: the link fails with the real `undefined reference to calc::Parser::...`, the gate reports the mend
//              `undefined-declared-member`, ONE decision `mend`, attempt 2 makes every declared member a section and the gate passes.
//     prevent  the standard process: the harness makes every declared member a section in attempt 1; nothing to mend.
#include "calculator_fake.hpp"

using namespace calcfake;

/// The skeleton as run-v11's model wrote it: the class declares its member functions as prototypes, and no `@@` line stands for any of them.
static std::string membersWithoutSections(const std::string& declared, bool keepEntryPoint)
{
    std::string out;
    for (const auto& l : bench::lines(declared)) {
        if (l == "namespace {" || l == "}  // namespace") continue;                     // the class in the named namespace, as the model had it
        const auto at = l.find("@@ ");
        if (at == std::string::npos) { out += l + "\n"; continue; }
        if (at > 0) out += l.substr(0, at) + l.substr(at + 3) + ";\n";                   // a member: a prototype in the class, nothing else
        else if (keepEntryPoint) out += l + "\n";
    }
    return out;
}

int main(int, char** argv)
{
    const std::string exe = argv[1];
    {   // ---- near pass: never thrown away, never made worse, repaired from the best state ----
        Squad sq(exe, "nearpass");
        sq.startDaemon(4, {"--frontier", "off"});
        Calculator calc;
        calc.declare = [](const std::string&, const std::string& declared) { return declared; };   // a complete skeleton: every function a section
        calc.section = [](const std::string& name, const std::string& file, const std::string& text, int nth) {
            if (file != "src/calc.cpp" || name != "term") return text;
            std::string t = text;
            if (nth == 1) { const auto at = t.find("v = std::fmod(v, d);"); CHECK(at != std::string::npos); t.replace(at, 20, "v = v / d;"); return t; }   // 1 of 16 tests fails
            if (nth == 2) return std::string{"    double term()\n    {\n        return 0.0;\n    }\n"};                                                      // worse: most tests fail
            if (nth == 3) return t + "    if (pos_ == 0) { }\n";                                                                                               // breaks the class, outside the function
            return t;                                                                                                                                         // the repair
        };
        fs::path dir;
        bool sawFailure = false;
        const std::string status = runWorkflow(sq, calc, "standard", dir, true, [&](const std::string& s) { if (s.find("\"lastFailure\":{\"schema\":\"attempt-failure-1\",\"role\":\"gate\"") != std::string::npos) sawFailure = true; });
        CHECK_EQ(field(status, "state"), std::string("succeeded"));
        const fs::path item = dir / "items" / "dev-engine";
        if (field(status, "state") != "succeeded")
            for (int n = 1; n <= 4; ++n) std::fprintf(stderr, "--- attempt %d\ngate: %.600s\nfork: %.500s\n", n, slurp(item / ("attempt-" + std::to_string(n)) / "gate.txt").c_str(), slurp(item / ("attempt-" + std::to_string(n)) / "fork.json").c_str());
        // The ladder: two retries and nothing else. No `sections` (the item already ran as a fork: row 73), no rearchitect, no reviseTests.
        const std::string decisions = between(status, "\"decisions\":", "\"ledger\"");
        CHECK_EQ(occurrences(decisions, "\"action\":\"retry\""), 2);
        CHECK_EQ(occurrences(decisions, "\"action\":\"sections\"") + occurrences(decisions, "\"action\":\"rearchitect\"") + occurrences(decisions, "\"action\":\"reviseTests\"") + occurrences(decisions, "\"action\":\"strategy\""), 0);
        CHECK(decisions.find("\"forked\":true") != std::string::npos);
        CHECK(decisions.find("near pass (at most 15 % of the tests fail)") != std::string::npos);   // the second retry is the near-pass allowance
        // Attempt 1: a near pass. The round that made it worse was undone; what stands fails ONE test, and the gate job said exit 3.
        const std::string fork1 = slurp(item / "attempt-1" / "fork.json");
        CHECK(fork1.find("\"exitCode\":3") != std::string::npos && fork1.find("\"nearPass\":true") != std::string::npos && fork1.find("\"testsTotal\":16,\"testsFailed\":1") != std::string::npos);
        CHECK(fork1.find("\"reverted\":1") != std::string::npos && fork1.find("\"failure\":\"tests\"") != std::string::npos);
        const std::string out1 = slurp(item / "attempt-1" / "out" / "src" / "calc.cpp");
        CHECK(out1.find("v = v / d;") != std::string::npos && out1.find("return 0.0;\n    }\n\n    double unary") == std::string::npos);
        CHECK(slurp(item / "attempt-1" / "gate.txt").rfind("GATE FAIL: 1 of 16 tests failed", 0) == 0);
        const std::string jobs = sq.cli({"status", "--json"}).out;
        CHECK(occurrences(jobs, "\"code\":3") >= 2);                                             // the gate jobs of attempts 1 and 2
        // Every failed attempt says why, in its directory and (while it was the latest) in the status (row 72).
        const std::string why1 = slurp(item / "attempt-1" / "failure.json");
        CHECK(why1.find("\"role\":\"gate\"") != std::string::npos && why1.find("1 of 16 tests failed") != std::string::npos);
        CHECK(sawFailure);
        // Attempt 2 started from attempt 1's canvas (no skeleton), a round broke the file OUTSIDE every section, the round was undone: the same near pass.
        const std::string fork2 = slurp(item / "attempt-2" / "fork.json");
        CHECK(fork2.find("\"resumedFrom\":1") != std::string::npos && fork2.find("\"skeletonTries\":0") != std::string::npos && fork2.find("\"reverted\":1") != std::string::npos);
        CHECK(fork2.find("\"exitCode\":3") != std::string::npos && fork2.find("\"testsTotal\":16,\"testsFailed\":1") != std::string::npos);
        CHECK_EQ(slurp(item / "attempt-2" / "out" / "src" / "calc.cpp"), out1);                 // never a worse state than the best before it
        CHECK(!fs::exists(item / "attempt-2" / "conversation-skeleton.md"));
        // The sections that went back were shown the failing test as it is written (rows 25/26).
        const std::string talk = slurp(item / "attempt-2" / "conversation-term.md");
        CHECK(talk.find("The failing tests as they are written:") != std::string::npos && talk.find("TEST(modulo) { CHECK_NEAR(ev(\"10%3\"), 1, 1e-9); }") != std::string::npos);
        // Attempt 3 started from the best state again and repaired the one function.
        const std::string fork3 = slurp(item / "attempt-3" / "fork.json");
        CHECK(fork3.find("\"passed\":true") != std::string::npos && fork3.find("\"resumedFrom\":2") != std::string::npos && fork3.find("\"skeletonTries\":0") != std::string::npos);
        CHECK(slurp(item / "attempt-3" / "out" / "src" / "calc.cpp").find("v = std::fmod(v, d);") != std::string::npos);
        CHECK(!fs::exists(item / "attempt-3" / "failure.json") && !fs::exists(item / "attempt-4"));
        CHECK_EQ(calc.asked["src/calc.cpp#term"], 4);
    }
    {   // ---- members, heal: prevention off, the link fails on a member the class declares, and the harness makes the members sections ----
        Squad sq(exe, "members-heal");
        sq.startDaemon(4, {"--frontier", "off"});
        CHECK_EQ(sq.cli({"process", "add", "no-prevention", "--set", "build.leaf.contractSections=false", "--describe", "the standard process without contract-first sections: reproduces run-v10 and run-v11"}).exitCode, 0);
        Calculator calc;
        calc.declare = [](const std::string& path, const std::string& declared) { return path == "src/calc.cpp" ? membersWithoutSections(declared, true) : declared; };
        fs::path dir;
        const std::string status = runWorkflow(sq, calc, "no-prevention", dir);
        CHECK_EQ(field(status, "state"), std::string("succeeded"));
        const fs::path item = dir / "items" / "dev-engine";
        if (field(status, "state") != "succeeded")
            for (int n = 1; n <= 3; ++n) std::fprintf(stderr, "--- attempt %d\ngate: %.900s\nfork: %.600s\nmend: %s\n", n, slurp(item / ("attempt-" + std::to_string(n)) / "gate.txt").c_str(), slurp(item / ("attempt-" + std::to_string(n)) / "fork.json").c_str(), slurp(item / ("attempt-" + std::to_string(n)) / "mend.json").c_str());
        CHECK_EQ(occurrences(status, "\"action\":\"mend\""), 1);
        CHECK_EQ(occurrences(status, "\"action\":\"rearchitect\"") + occurrences(status, "\"action\":\"retry\""), 0);
        CHECK(status.find("\"signature\":\"undefined-declared-member\"") != std::string::npos);
        CHECK(status.find("\"healed\":[\"undefined-declared-member\"]") != std::string::npos);
        const std::string gate1 = slurp(item / "attempt-1" / "gate.txt");
        CHECK(gate1.find("GATE FAIL: INTERFACE MISMATCH") != std::string::npos && gate1.find("undefined reference to `calc::") != std::string::npos);   // under this long test path the gate's clipping cuts the name itself; the mend still finds the member
        if (std::count(calc.sectionsAsked.begin(), calc.sectionsAsked.end(), std::string("evaluate")) != 1) { for (const auto& a : calc.sectionsAsked) std::fprintf(stderr, "%s ", a.c_str()); std::fprintf(stderr, "\n"); }
        const std::string mend1 = slurp(item / "attempt-1" / "mend.json");
        CHECK(mend1.find("\"name\":\"undefined-declared-member\"") != std::string::npos && mend1.find("\"requires\":\"declared\"") != std::string::npos && mend1.find("\"symbol\":\"calc::Parser::") != std::string::npos);
        const std::string fork2 = slurp(item / "attempt-2" / "fork.json");
        CHECK(fork2.find("\"passed\":true") != std::string::npos && fork2.find("\"resumedFrom\":1") != std::string::npos && fork2.find("\"skeletonTries\":0") != std::string::npos);
        CHECK(fork2.find("\"memberSections\":0") == std::string::npos && fork2.find("\"name\":\"Parser::term\"") != std::string::npos && fork2.find("\"symbol\":\"Parser::term\"") != std::string::npos);
        const std::string healed = slurp(item / "attempt-2" / "out" / "src" / "calc.cpp");
        CHECK(healed.find("double Parser::term()\n{") != std::string::npos && healed.find("not implemented") == std::string::npos);
        CHECK(std::find(calc.sectionsAsked.begin(), calc.sectionsAsked.end(), std::string("Parser::term")) != calc.sectionsAsked.end());
        CHECK_EQ(static_cast<int>(std::count(calc.sectionsAsked.begin(), calc.sectionsAsked.end(), std::string("evaluate"))), 1);   // written once, in attempt 1: the mend wrote only what was missing
    }
    {   // ---- members, prevent: the same kind of skeleton under the standard process ----
        Squad sq(exe, "members-prevent");
        sq.startDaemon(4, {"--frontier", "off"});
        Calculator calc;
        calc.declare = [](const std::string& path, const std::string& declared) { return path == "src/calc.cpp" ? membersWithoutSections(declared, false) : declared; };
        fs::path dir;
        const std::string status = runWorkflow(sq, calc, "standard", dir);
        CHECK_EQ(field(status, "state"), std::string("succeeded"));
        CHECK(status.find("\"decisions\":[]") != std::string::npos);
        const fs::path item = dir / "items" / "dev-engine";
        const std::string fork1 = slurp(item / "attempt-1" / "fork.json");
        CHECK(fork1.find("\"passed\":true") != std::string::npos && fork1.find("\"contractSections\":1") != std::string::npos && fork1.find("\"memberSections\":0") == std::string::npos);
        const std::string r1 = slurp(item / "attempt-1" / "canvas" / "src_calc.cpp.r1");
        CHECK(r1.find("double Parser::term()\n{\n    throw std::logic_error(\"not implemented: Parser::term\");\n}\n") != std::string::npos);   // revision 1 already defines every declared member as a stub
        CHECK(!fs::exists(item / "attempt-2"));
    }
    return finish();
}
