// sub0squad (WORKING TITLE): the native agent worker daemon described in docs/DESIGN.md.
// Today it is only a dependency smoke test: it proves the pinned Sub0Pipeline and Sub0Pub link and cooperate here:
// a DAG with a job that completes from another thread (deferred completion) and typed events delivered to a subscriber.
#include "sub0pipeline/sub0pipeline.hpp"
#include "sub0pub/sub0pub.hpp"
#include <simdjson.h>

#include <string_view>
#include <Version.hpp>
#include "sub0squad/build_id.hpp"
#include "sub0squad/client.hpp"
#include "sub0squad/mock_worker.hpp"
#include "sub0squad/harness_cpp.hpp"
#include "first_use.hpp"
#include "sub0squad/blocks.hpp"
#include "sub0squad/scenario.hpp"
#include "sub0squad/harness_py.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/host_guard.hpp"
#include "sub0squad/sandbox.hpp"
#include "sub0squad/shim.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

namespace {

struct JobFinished { int id; };

class EventSource : public sub0::Publish<JobFinished> {
public:
    void emit(int id) { sub0::publish(this, JobFinished{id}); }
};

class EventCounter : public sub0::Subscribe<JobFinished> {
public:
    void receive(const JobFinished& event) noexcept { sum += event.id; ++count; }
    int sum = 0;
    int count = 0;
};

int selftest()
{
    using namespace sub0pipeline;
    std::atomic<int> finishedMask{0};
    std::jthread helper;  // completes the deferred job from a thread that is not an executor thread

    Pipeline pipe;
    auto plan = pipe.emplace([&] { finishedMask.fetch_or(1); });
    auto wait = pipe.emplaceDeferred([&](Completion done) {
        helper = std::jthread([&finishedMask, done = std::move(done)]() mutable {
            finishedMask.fetch_or(2);
            done.succeed();
        });
    });
    auto finish = pipe.emplace([&] { finishedMask.fetch_or(4); });
    wait.succeed(plan);
    finish.succeed(wait);

    PriorityExecutor executor{{.threadCount = 2}};
    const auto result = pipe.run(executor);
    pipe.joinOrphans();
    if (!result.has_value()) {
        std::fprintf(stderr, "selftest: pipeline failed\n");
        return 1;
    }

    // Events are published from this one thread: the default Sub0Pub broker is unlocked.
    EventSource source;
    EventCounter counter;
    for (int bit = 0; bit < 3; ++bit)
        if (finishedMask.load() & (1 << bit)) source.emit(bit + 1);
    if (finishedMask.load() != 7 || counter.count != 3 || counter.sum != 6) {
        std::fprintf(stderr, "selftest: mask %d, events %d, sum %d\n", finishedMask.load(), counter.count, counter.sum);
        return 1;
    }

    // JSON round trip: write with the builder, read with the forward on-demand parser (the family's JSON convention).
    simdjson::builder::string_builder out;
    out.start_object();
    out.append_key_value<"schema">("sub0squad-event-1");
    out.append_comma();
    out.append_key_value<"done">(3);
    out.end_object();
    std::string text;
    if (out.move_to(text) != simdjson::SUCCESS) { std::fprintf(stderr, "selftest: json write failed\n"); return 1; }
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded{text};
    auto doc = parser.iterate(padded);
    int64_t done = 0;
    if (doc["done"].get(done) != simdjson::SUCCESS || done != 3) { std::fprintf(stderr, "selftest: json read failed: %s\n", text.c_str()); return 1; }
    std::puts("selftest ok: 3 jobs ran in dependency order, one completed from another thread, 3 typed events delivered, JSON written and read");
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    sub0squad::setBuildId(std::string(sub0squad::version::version_full));
    if (argc > 2 && std::strcmp(argv[1], "exec-shim") == 0) return sub0squad::runShim(argv[2]);
    if (argc > 1 && std::strcmp(argv[1], "mock-worker") == 0) return sub0squad::runMockWorker(argc - 2, argv + 2);
    if (argc > 1) {
        const std::vector<std::string> rest(argv + 2, argv + argc);
        if (std::strcmp(argv[1], "mock-planner") == 0) return sub0squad::runMockPlanner(rest);
        if (std::strcmp(argv[1], "mock-agent") == 0) return sub0squad::runMockAgent(rest);
        if (std::strcmp(argv[1], "guard") == 0) return sub0squad::runGuard(rest);
        if (std::strcmp(argv[1], "fetch-model") == 0) return sub0squad::runFetchModel(rest);
        if (std::strcmp(argv[1], "mock-architect") == 0) return sub0squad::runMockArchitect(rest);
        if (std::strcmp(argv[1], "llm-architect") == 0) return sub0squad::runLlmArchitect(rest);
        if (std::strcmp(argv[1], "scenario") == 0) return sub0squad::runScenarioCommand(rest);
        if (std::strcmp(argv[1], "block") == 0) return sub0squad::runBlockCommand(rest, std::string(sub0squad::version::version_full));
        if (std::strcmp(argv[1], "optimize-job") == 0) return sub0squad::runOptimizeJob(rest);
        if (std::strcmp(argv[1], "llm-agent") == 0) return sub0squad::runLlmAgent(rest);
        if (std::strcmp(argv[1], "llm-fork") == 0) return sub0squad::runLlmFork(rest);
        if (std::strcmp(argv[1], "mend-apply") == 0) return sub0squad::runMendApply(rest);
        if (std::strcmp(argv[1], "harness-gate") == 0) return sub0squad::runHarnessGate(rest);
        if (std::strcmp(argv[1], "harness-integrate") == 0) return sub0squad::runHarnessIntegrate(rest);
        if (std::strcmp(argv[1], "py-gate") == 0) return sub0squad::runPyGate(rest);
        if (std::strcmp(argv[1], "py-integrate") == 0) return sub0squad::runPyIntegrate(rest);
        if (std::strcmp(argv[1], "cpp-gate") == 0) return sub0squad::runCppGate(rest);
        if (std::strcmp(argv[1], "cpp-integrate") == 0) return sub0squad::runCppIntegrate(rest);
    }
    if (argc > 1 && (std::strcmp(argv[1], "up") == 0 || std::strcmp(argv[1], "down") == 0 || std::strcmp(argv[1], "doctor") == 0)) {
        const std::vector<std::string> rest(argv + 2, argv + argc);
        const std::string exe = sub0squad::resolveProgram(argv[0]);
        if (std::strcmp(argv[1], "up") == 0) return sub0squad::runUp(rest, exe);
        if (std::strcmp(argv[1], "down") == 0) return sub0squad::runDown(rest);
        return sub0squad::runDoctor(rest, exe, std::string(sub0squad::version::version_full));
    }
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return selftest();
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) { std::printf("sub0squad %s (working title)\n", std::string(sub0squad::version::version_full).c_str()); return 0; }
    return sub0squad::runCli(argc, argv);
}
