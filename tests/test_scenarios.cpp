// The capability scenarios' own validity (docs/SCENARIOS.md), without a model: every fixture's reference solution passes its judge and every starting
// state does not, on all three platforms; and the statistic that decides "proven" is the 95 % Wilson lower bound.
#include "check.hpp"
#include "sub0squad/scenario.hpp"
#include "sub0squad/scenario_compare.hpp"

#include <fstream>
#include <sys/wait.h>

using namespace sub0squad;

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: test_scenarios <path of sub0squad>\n"); return 2; }
    CHECK_EQ(scenarioCatalogue().size(), static_cast<std::size_t>(18));
    CHECK(wilsonLowerBound(0, 0) == 0.0);
    CHECK(wilsonLowerBound(10, 10) > 0.72 && wilsonLowerBound(10, 10) < 0.73);   // all of ten is not certainty
    CHECK(wilsonLowerBound(9, 10) > 0.59 && wilsonLowerBound(9, 10) < 0.60);
    CHECK(wilsonLowerBound(5, 10) < 0.25);
    CHECK(wilsonLowerBound(30, 30) > wilsonLowerBound(10, 10));                  // more trials, tighter bound
    {   // `scenario compare`: two result files side by side. A difference is called only when the two 95 % intervals do not overlap; 9 of 10 against 8 of 10 is noise.
        const auto file = [](const char* build, const char* results) { return std::string{"{\"schema\":\"scenario-results-1\",\"build\":\""} + build + "\",\"engine\":\"e\",\"treatment\":\"baseline\",\"executor\":\"session\",\"trials\":10,\"minLower\":0.6,\"results\":[" + results + "]}"; };
        const auto row = [](const char* element, int passed, int trials, double seconds, double tokens) {
            return std::string{"{\"platform\":\"cpp\",\"element\":\""} + element + "\",\"passed\":" + std::to_string(passed) + ",\"trials\":" + std::to_string(trials) + ",\"lower95\":0,\"meanSeconds\":" + std::to_string(seconds) + ",\"meanTokens\":" + std::to_string(tokens) + ",\"meanChangedShare\":-1,\"reliable\":false}";
        };
        const std::string a = file("build-a", (row("tester", 9, 10, 100, 2000) + "," + row("developer-first", 10, 10, 200, 4000) + "," + row("app-repair", 1, 10, 50, 900) + "," + row("gone", 5, 10, 1, 1)).c_str());
        const std::string b = file("build-b", (row("tester", 8, 10, 50, 1000) + "," + row("developer-first", 2, 10, 300, 4000) + "," + row("app-repair", 10, 10, 50, 900) + "," + row("new", 3, 10, 1, 1)).c_str());
        const auto c = compareScenarioResults(a, b);
        CHECK(c.has_value());
        if (c) {
            CHECK_EQ(c->rows.size(), static_cast<std::size_t>(5));
            CHECK(c->regressions == 1 && c->improvements == 1 && c->buildA == "build-a" && c->buildB == "build-b");
            const auto verdict = [&](const char* element) { for (const auto& r : c->rows) if (r.element == element) return r.verdict; return std::string{"missing"}; };
            CHECK_EQ(verdict("tester"), std::string("no evidence of change"));        // 9/10 against 8/10
            CHECK_EQ(verdict("developer-first"), std::string("regression"));           // 10/10 against 2/10
            CHECK_EQ(verdict("app-repair"), std::string("improvement"));               // 1/10 against 10/10
            CHECK_EQ(verdict("gone"), std::string("only in A"));
            CHECK_EQ(verdict("new"), std::string("only in B"));
            CHECK(c->rows[0].lowerA > 0.59 && c->rows[0].lowerA < 0.60 && c->rows[0].upperA > 0.98);   // the interval of 9/10, the same statistic `scenario run` reports
            const std::string text = c->text();
            CHECK(text.find("1 regression(s), 1 improvement(s), 5 scenario(s) compared") != std::string::npos && text.find("-50%") != std::string::npos && text.find("+50%") != std::string::npos);
        }
        const auto same = compareScenarioResults(a, a);
        CHECK(same.has_value() && same->regressions == 0 && same->improvements == 0);
        CHECK(wilsonUpperBound(0, 0) == 1.0 && wilsonUpperBound(0, 10) > 0.27 && wilsonUpperBound(0, 10) < 0.28 && wilsonUpperBound(10, 10) > 0.999);
        // what is wrong with a file is said, with the side
        CHECK(compareScenarioResults("nonsense", b).error().find("A: not a scenario-results file") != std::string::npos);
        CHECK(compareScenarioResults(a, "{\"schema\":\"other\"}").error().find("B: the file's schema is not scenario-results-1") != std::string::npos);
        CHECK(compareScenarioResults(a, file("x", row("tester", 11, 10, 1, 1).c_str())).error().find("B: result cpp/tester: passed 11 of 10 trials is not possible") != std::string::npos);
        // the command: exit 1 on a regression (so a script can gate on it), 0 without one, 64 on a file it cannot use
        const std::filesystem::path dir = makeTempDir("scenario-compare");
        { std::ofstream(dir / "a.json") << a; std::ofstream(dir / "b.json") << b; }
        const auto command = [&](const std::string& x, const std::string& y) {
            const std::string line = "'" + std::string{argv[1]} + "' scenario compare '" + (dir / x).string() + "' '" + (dir / y).string() + "' > '" + (dir / "out.txt").string() + "' 2>&1";
            const int status = std::system(line.c_str());
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        };
        CHECK_EQ(command("a.json", "b.json"), 1);
        CHECK_EQ(command("a.json", "a.json"), 0);
        CHECK_EQ(command("b.json", "a.json"), 1);     // read the other way round, the improvement is the regression
        CHECK_EQ(command("a.json", "missing.json"), 64);
        removeTree(dir);
    }
    for (const auto& p : scenarioSelfTest({}, argv[1])) { std::fprintf(stderr, "%s\n", p.c_str()); CHECK(false); }
    return finish();
}
