// Sub0Squad micro-benchmarks, run through Sub0Math's sub0perf tooling (docs/M1_PLAN.md, "Verification with sub0perf").
// Naming follows sub0perf: a "hand-written" reference and its library twin share the text before the comma and form a pair group.
#define ANKERL_NANOBENCH_IMPLEMENT
#include "bench_runner.hpp"

#include "sub0squad/frame.hpp"
#include "sub0squad/job_spec.hpp"
#include "sub0squad/journal.hpp"
#include "sub0squad/progress.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sub0squad;
namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    sub0bench::BenchRunner runner(argc, argv, "sub0squad");
    const fs::path root = fs::path{argc > 0 ? argv[0] : "."}.parent_path() / "bench-tmp";
    fs::remove_all(root);
    fs::create_directories(root);

    // Frame encode: a hand-written fixed-size memcpy twin versus encodeFrame().
    const std::string payload(256, 'x');
    runner.run("frame encode 256 B, hand-written", [&] {
        char buf[4 + 256];
        const std::uint32_t n = 256;
        std::memcpy(buf, &n, 4);
        std::memcpy(buf + 4, payload.data(), 256);
        ankerl::nanobench::doNotOptimizeAway(buf);
    });
    runner.run("frame encode 256 B, encodeFrame()", [&] { auto f = encodeFrame(payload); ankerl::nanobench::doNotOptimizeAway(f); });

    // Journal append without fsync: a plain O_APPEND write of the same line versus Journal::append.
    const std::string body = "{\"id\":\"j-000001\",\"run\":1,\"state\":\"running\"}";
    {
        const fs::path d = root / "plain";
        fs::create_directories(d);
        const int fd = ::open((d / "plain.ndjson").c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        const std::string line = "{\"seq\":1,\"ts\":1.0,\"type\":\"jobStarted\",\"d\":" + body + "}\n";
        runner.run("journal append (no fsync), hand-written write()", [&] { (void)!::write(fd, line.data(), line.size()); });
        ::close(fd);
    }
    {
        auto j = Journal::open(root / "journal-nosync");
        runner.run("journal append (no fsync), Journal::append()", [&] { auto r = j->append("jobStarted", body, false); ankerl::nanobench::doNotOptimizeAway(r); });
    }
    {
        auto j = Journal::open(root / "journal-sync");
        runner.bench().epochs(5).minEpochIterations(5);
        runner.run("journal append (fdatasync per record)", [&] { auto r = j->append("jobStarted", body, true); ankerl::nanobench::doNotOptimizeAway(r); });
        runner.bench().epochs(11).minEpochIterations(10);
    }

    // Parsing: job spec and a progress tail.
    JobSpec spec;
    spec.argv = {"python3", "iterate.py", "--item", "item.json", "--out", "out", "--resume"};
    spec.env["A"] = "b";
    spec.resumeArgv = {"python3", "iterate.py", "--resume"};
    const std::string specText = spec.toJson();
    runner.run("job-spec parse, JobSpec::fromJson()", [&] { auto s = JobSpec::fromJson(specText); ankerl::nanobench::doNotOptimizeAway(s); });
    runner.run("job-spec serialise, JobSpec::toJson()", [&] { auto s = spec.toJson(); ankerl::nanobench::doNotOptimizeAway(s); });
    {
        const fs::path d = root / "events";
        fs::create_directories(d);
        std::ofstream o(d / "events.ndjson");
        for (int i = 1; i <= 200; ++i)
            o << "{\"schema\":\"sub0squad-event-1\",\"type\":\"progress\",\"done\":" << i << ",\"total\":200,\"phase\":\"work\"}\n"
              << "{\"schema\":\"sub0squad-event-1\",\"type\":\"metric\",\"name\":\"tokens\",\"value\":" << i * 10 << "}\n";
        o.close();
        runner.run("progress tail 400 events, readProgress()", [&] { auto p = readProgress(d); ankerl::nanobench::doNotOptimizeAway(p); });
    }
    const int rc = runner.finish();
    fs::remove_all(root);
    return rc;
}
