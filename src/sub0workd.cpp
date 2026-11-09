// sub0workd (WORKING TITLE): the native agent worker daemon described in docs/DESIGN.md.
// Today it is only a dependency smoke test: it proves the pinned Sub0Pipeline and Sub0Pub link and cooperate here:
// a DAG with a job that completes from another thread (deferred completion) and typed events delivered to a subscriber.
#include "sub0pipeline/sub0pipeline.hpp"
#include "sub0pub/sub0pub.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
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
    std::puts("selftest ok: 3 jobs ran in dependency order, one completed from another thread, 3 typed events delivered");
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return selftest();
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) { std::puts("sub0workd 0.0.0 (working title, design stage)"); return 0; }
    std::puts("usage: sub0workd --selftest | --version   (the daemon itself is not implemented yet; see docs/ROADMAP.md)");
    return argc > 1 ? 2 : 0;
}
