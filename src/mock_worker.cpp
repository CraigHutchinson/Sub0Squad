#include "sub0squad/mock_worker.hpp"

#include "json_util.hpp"
#include "sub0squad/progress.hpp"

#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace sub0squad {

namespace {

int eventFd()
{
    const char* v = std::getenv("SUB0SQUAD_EVENT_FD");
    return v ? std::atoi(v) : -1;
}

void emit(const std::string& line)
{
    const int fd = eventFd();
    if (fd < 0) return;
    std::string out = line + "\n";
    (void)!::write(fd, out.data(), out.size());
}

void sleepMs(long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

[[noreturn]] void idleForever(bool ignoreTerm)
{
    if (ignoreTerm) ::signal(SIGTERM, SIG_IGN);
    for (;;) ::pause();
}

void nameSelf(const std::string& prefix, const std::string& tag)
{
    const std::string name = (prefix + tag).substr(0, 15);
    ::prctl(PR_SET_NAME, name.c_str());
}

// A "server": a child in the job's own group that ignores SIGTERM (like a model server that must be killed).
void spawnServer(const std::string& tag)
{
    if (::fork() == 0) { nameSelf("s0q-srv-", tag); idleForever(true); }
}

// An escapee: setsid, then double fork, so it is in no group of the job and its parent is gone.
void spawnEscapee(const std::string& tag)
{
    if (::fork() == 0) {
        ::setsid();
        if (::fork() == 0) { nameSelf("s0q-esc-", tag); idleForever(true); }
        _exit(0);
    }
}

void writeStateFile(const std::string& path, int attempts, int maxAttempts, const char* status)
{
    json::Writer w;
    w.beginObject().kv("schema", "optimization-run-1").kv("status", status).kv("no_progress", 0).kv("recoveries", 0);
    w.key("item").beginObject().kv("max_attempts", maxAttempts).endObject();
    w.key("attempts").beginArray();
    for (int i = 1; i <= attempts; ++i) w.beginObject().kv("attempt", i).kv("status", "no_proposal").endObject();
    w.endArray().endObject();
    const std::string tmp = path + ".tmp";
    if (FILE* f = std::fopen(tmp.c_str(), "w")) {
        const auto v = w.view();
        std::fwrite(v.data(), 1, v.size(), f);
        std::fclose(f);
        std::rename(tmp.c_str(), path.c_str());
    }
}

}  // namespace

int runMockWorker(int argc, char** argv)
{
    int steps = 5, failAt = -1, crashAt = -1, stallAt = -1;
    long stepMs = 50, memMiB = 0;
    bool ignoreTerm = false, server = false, escape = false, resume = false;
    std::string tag = "x", stateFile;
    std::vector<std::pair<std::string, double>> metrics;
    for (int i = 0; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--steps") steps = std::atoi(next());
        else if (a == "--step-ms") stepMs = std::atol(next());
        else if (a == "--fail-at") failAt = std::atoi(next());
        else if (a == "--crash-at") crashAt = std::atoi(next());
        else if (a == "--stall-at") stallAt = std::atoi(next());
        else if (a == "--ignore-term") ignoreTerm = true;
        else if (a == "--server") server = true;
        else if (a == "--escape") escape = true;
        else if (a == "--resume") resume = true;
        else if (a == "--tag") tag = next();
        else if (a == "--mem-mib") memMiB = std::atol(next());
        else if (a == "--state-file") stateFile = next();
        else if (a == "--metric") { std::string kv = next(); auto eq = kv.find('='); if (eq != std::string::npos) metrics.emplace_back(kv.substr(0, eq), std::atof(kv.c_str() + eq + 1)); }
        else { std::fprintf(stderr, "mock-worker: unknown option %s\n", a.c_str()); return 64; }
    }
    if (ignoreTerm) ::signal(SIGTERM, SIG_IGN);
    nameSelf("s0q-wrk-", tag);
    std::vector<char> ballast;
    if (memMiB > 0) { ballast.assign(static_cast<std::size_t>(memMiB) << 20, 1); }
    if (server) spawnServer(tag);
    if (escape) spawnEscapee(tag);
    std::printf("mock-worker %s: %d steps\n", tag.c_str(), steps);
    std::fflush(stdout);
    std::fprintf(stderr, "mock-worker %s: stderr line\n", tag.c_str());
    int firstStep = 1;
    if (resume && !stateFile.empty()) {  // continue after the attempts already recorded, like the harness's --resume
        std::FILE* f = std::fopen(stateFile.c_str(), "r");
        if (f) {
            std::string text;
            char b[4096];
            std::size_t n;
            while ((n = std::fread(b, 1, sizeof b, f)) > 0) text.append(b, n);
            std::fclose(f);
            firstStep = static_cast<int>(progressFromOptimizationState(text).done) + 1;
        }
    }
    for (int step = firstStep; step <= steps; ++step) {
        if (step == stallAt) { emit("{\"schema\":\"sub0squad-event-1\",\"type\":\"note\",\"text\":\"stalling\"}"); idleForever(ignoreTerm); }
        sleepMs(stepMs);
        if (step == crashAt) ::raise(SIGSEGV);
        if (step == failAt) { std::fprintf(stderr, "mock-worker: failing at step %d\n", step); return 3; }
        json::Writer w;
        w.beginObject().kv("schema", "sub0squad-event-1").kv("type", "progress").kv("done", step).kv("total", steps).kv("phase", "work").endObject();
        emit(std::string{w.view()});
        for (const auto& [name, value] : metrics) {
            json::Writer m;
            m.beginObject().kv("schema", "sub0squad-event-1").kv("type", "metric").kv("name", name).kv("value", value * step).endObject();
            emit(std::string{m.view()});
        }
        if (!stateFile.empty()) writeStateFile(stateFile, step, steps, step == steps ? "succeeded" : "running");
    }
    return 0;
}

}  // namespace sub0squad
