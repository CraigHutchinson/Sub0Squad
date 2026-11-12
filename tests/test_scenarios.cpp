// The capability scenarios' own validity (docs/SCENARIOS.md), without a model: every fixture's reference solution passes its judge and every starting
// state does not, on all three platforms; and the statistic that decides "proven" is the 95 % Wilson lower bound.
#include "check.hpp"
#include "sub0squad/scenario.hpp"

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
    for (const auto& p : scenarioSelfTest({}, argv[1])) { std::fprintf(stderr, "%s\n", p.c_str()); CHECK(false); }
    return 0;
}
