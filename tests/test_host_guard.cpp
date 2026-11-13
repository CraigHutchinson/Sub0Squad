// The explicit resource guards: memory and disk are checked before work is admitted, with the numbers in the refusal.
#include "check.hpp"
#include "sub0squad/host_guard.hpp"

using namespace sub0squad;

int main()
{
    const HostMemory m = readHostMemory();
    CHECK(m.totalMiB > 0 && m.availableMiB > 0 && m.availableMiB <= m.totalMiB);
    CHECK(requireMemory(1, 0).has_value());
    auto refused = requireMemory(m.totalMiB * 2, 0);                       // more than the whole machine
    CHECK(!refused.has_value());
    if (!refused) CHECK(refused.error().find("not enough free memory") != std::string::npos && refused.error().find(std::to_string(m.totalMiB)) != std::string::npos);
    CHECK(!requireMemory(m.availableMiB, 1024).has_value());                // the reserve counts
    const auto dir = makeTempDir("guard");
    CHECK(freeDiskMiB(dir) > 0);
    CHECK(freeDiskMiB(dir / "not" / "yet" / "created") > 0);                // the nearest existing parent decides
    CHECK(requireDisk(dir, 1, 0).has_value());
    auto full = requireDisk(dir, freeDiskMiB(dir) * 2, 0);
    CHECK(!full.has_value());
    if (!full) CHECK(full.error().find("not enough free disk space") != std::string::npos);
    CHECK_EQ(runGuard({"--memory-mib", "1", "--reserve-mib", "0"}), 0);
    CHECK_EQ(runGuard({"--memory-mib", std::to_string(m.totalMiB * 2)}), 1);  // exit status 1 when a guard refuses
    removeTree(dir);
    return finish();
}
