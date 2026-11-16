// Host facts (platform_host.hpp, docs/PLATFORM.md): the pure parsers on fixed /proc text, then the live calls for sanity (not exact values).
#include "check.hpp"
#include "sub0squad/platform_host.hpp"

using namespace sub0squad::platform;

int main()
{
    {   // /proc/meminfo, kB to MiB; the commit pair gives the headroom
        const auto m = parseMeminfo("MemTotal:       32768000 kB\nMemFree:         1024000 kB\nMemAvailable:   16384000 kB\n"
                                    "SwapTotal:       8192000 kB\nSwapFree:        4096000 kB\nCommitLimit:    24576000 kB\nCommitted_AS:   20480000 kB\n");
        CHECK_EQ(m.totalMiB, std::int64_t{32000});
        CHECK_EQ(m.availableMiB, std::int64_t{16000});
        CHECK_EQ(m.swapTotalMiB, std::int64_t{8000});
        CHECK_EQ(m.swapFreeMiB, std::int64_t{4000});
        CHECK(m.commitHeadroomMiB.has_value() && *m.commitHeadroomMiB == 4000);
        const auto partial = parseMeminfo("MemTotal: 2048 kB\n");   // missing keys stay 0; no commit pair, no headroom
        CHECK_EQ(partial.totalMiB, std::int64_t{2});
        CHECK_EQ(partial.availableMiB, std::int64_t{0});
        CHECK(!partial.commitHeadroomMiB.has_value());
        CHECK_EQ(parseMeminfo("").totalMiB, std::int64_t{0});
    }
    {   // /proc/stat: busy is everything except idle and iowait
        const auto t = parseCpuTimes("cpu  100 10 50 800 20 5 5 10 0 0\ncpu0 50 5 25 400 10 2 3 5 0 0\n");
        CHECK(t.has_value());
        if (t) { CHECK_EQ(t->total, 1000.0); CHECK_EQ(t->busy, 180.0); }
        CHECK(!parseCpuTimes("intr 1 2 3").has_value());
        CHECK(!parseCpuTimes("").has_value());
    }
    {   // the live host: plausible, not exact
        const auto m = memory();
        CHECK(m.totalMiB > 0 && m.availableMiB > 0 && m.availableMiB <= m.totalMiB);
        const auto t = cpuTimes();
        CHECK(t.has_value() && t->total > 0 && t->busy <= t->total);
        const auto load = loadAverage();
#if defined(_WIN32)
        CHECK(!load.has_value());   // Windows keeps no load average
#else
        CHECK(load.has_value() && *load >= 0);
#endif
        const auto dir = makeTempDir("platform-host");
        const auto free = freeDiskMiB(dir);
        CHECK(free.has_value() && *free > 0);
        CHECK(freeDiskMiB(dir / "not" / "yet" / "created").has_value());   // the nearest existing parent decides
        removeTree(dir);
        if (const auto g = gpu()) CHECK(g->vramTotalMiB >= 0);   // optional: most build hosts have none
    }
    return finish();
}
