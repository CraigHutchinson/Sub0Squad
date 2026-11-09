// M0 open question (docs/DESIGN.md section 11): can a Sub0Pipeline job add jobs to the graph WHILE it runs?
// The probe runs in a forked child so a crash or hang is an answer, not a test failure. The fallback (staged pipelines:
// a plan stage whose output builds the next Pipeline, and a job that runs sub-pipelines) is asserted to work.
#include "sub0pipeline/sub0pipeline.hpp"

#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

using namespace sub0pipeline;

namespace {

enum ProbeExit { kRanInSameRun = 10, kNotRunInSameRun = 11, kRunFailed = 12 };

// Child: a job emplaces another job (and an edge) while the pipeline runs.
int probeMidRunEmplace()
{
    std::atomic<int> lateRan{0};
    Pipeline pipe;
    auto grower = pipe.emplace([&] {
        auto late = pipe.emplace([&] { lateRan.store(1); });  // mutation during run(): documented as build-phase only
        (void)late;
    });
    (void)grower;
    PriorityExecutor executor{{.threadCount = 2}};
    const auto result = pipe.run(executor);
    pipe.joinOrphans();
    if (!result.has_value()) return kRunFailed;
    return lateRan.load() ? kRanInSameRun : kNotRunInSameRun;
}

// Fallback A: staged pipelines. Stage 1 plans N sub-tasks (data); stage 2 is built from that data and run.
bool stagedPipelines()
{
    std::vector<int> plan;
    {
        Pipeline planStage;
        auto planner = planStage.emplace([&] { for (int i = 1; i <= 5; ++i) plan.push_back(i); });
        (void)planner;
        PriorityExecutor executor{{.threadCount = 2}};
        if (!planStage.run(executor).has_value()) return false;
    }
    std::atomic<int> sum{0};
    Pipeline fanOut;
    auto integrate = fanOut.emplace([&] { sum.fetch_add(100); });
    for (int n : plan) {
        auto sub = fanOut.emplace([&sum, n] { sum.fetch_add(n); });
        integrate.succeed(sub);
    }
    PriorityExecutor executor{{.threadCount = 4}};
    if (!fanOut.run(executor).has_value()) return false;
    return sum.load() == 115;
}

// Fallback B: a running job builds and runs a sub-pipeline on its own executor (a sub-task per job).
bool nestedSubPipelines()
{
    std::atomic<int> sum{0};
    Pipeline outer;
    auto parent = outer.emplace([&] {
        Pipeline inner;
        auto a = inner.emplace([&] { sum.fetch_add(1); });
        auto b = inner.emplace([&] { sum.fetch_add(2); });
        auto c = inner.emplace([&] { sum.fetch_add(4); });
        c.succeed(a);
        c.succeed(b);
        PriorityExecutor innerExec{{.threadCount = 2}};
        (void)inner.run(innerExec);
    });
    (void)parent;
    PriorityExecutor executor{{.threadCount = 2}};
    if (!outer.run(executor).has_value()) return false;
    return sum.load() == 7;
}

}  // namespace

int main()
{
    int failures = 0;
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) _exit(probeMidRunEmplace());
    // Bounded wait: a hang is an answer too.
    int status = 0;
    bool hung = true;
    for (int i = 0; i < 100; ++i) {
        if (waitpid(pid, &status, WNOHANG) == pid) { hung = false; break; }
        usleep(50'000);
    }
    std::string answer;
    if (hung) { kill(pid, SIGKILL); waitpid(pid, &status, 0); answer = "hang"; }
    else if (WIFSIGNALED(status)) answer = "crash (signal " + std::to_string(WTERMSIG(status)) + ")";
    else switch (WEXITSTATUS(status)) {
        case kRanInSameRun: answer = "late job ran in the same run"; break;
        case kNotRunInSameRun: answer = "late job accepted but NOT run in the same run"; break;
        case kRunFailed: answer = "run failed"; break;
        default: answer = "unexpected exit " + std::to_string(WEXITSTATUS(status));
    }
    std::printf("m0 probe (emplace during run): %s\n", answer.c_str());
    // If a future Sub0Pipeline makes this work, this assertion fails on purpose: update DESIGN section 11 and the M4 plan.
    if (!hung && !WIFSIGNALED(status) && WEXITSTATUS(status) == kRanInSameRun) {
        std::puts("FAIL: mid-run growth now works; revisit docs/DESIGN.md section 11");
        ++failures;
    }
    if (!stagedPipelines()) { std::puts("FAIL: staged pipelines"); ++failures; } else std::puts("staged pipelines: ok");
    if (!nestedSubPipelines()) { std::puts("FAIL: nested sub-pipelines"); ++failures; } else std::puts("nested sub-pipelines: ok");
    return failures ? 1 : 0;
}
