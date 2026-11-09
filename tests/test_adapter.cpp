// The state-file adapter against a REAL optimization-run-1 record (a copy of Sub0Llm's stalled iq1s-pass2-run-001), and the status
// path that uses it: progress and liveness come from the tool's own record, not from any log.
#include "check.hpp"
#include "sub0squad/progress.hpp"

#include <fstream>
#include <sstream>

using namespace sub0squad;

int main(int, char** argv)
{
    std::ifstream in(argv[1]);
    CHECK(static_cast<bool>(in));
    std::stringstream ss;
    ss << in.rdbuf();
    const Progress p = progressFromOptimizationState(ss.str());
    CHECK(p.hasProgress);
    CHECK_EQ(p.done, 6);
    CHECK_EQ(p.total, 6);
    CHECK_EQ(p.adapterStatus, std::string{"stalled"});
    CHECK_EQ(p.phase, std::string{"stalled"});
    CHECK_EQ(p.metrics.at("recoveries"), 3.0);
    CHECK_EQ(p.metrics.at("no_progress"), 0.0);
    CHECK(!progressFromOptimizationState("{ not json").hasProgress);
    CHECK(progressFromOptimizationState("[1]").adapterStatus == "unreadable");
    return finish();
}
