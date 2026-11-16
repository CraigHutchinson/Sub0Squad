// The one launcher (platform_process.hpp, docs/PLATFORM.md): exit codes and signals, capture with and without stderr, the tail kept when the output
// is long, timeouts, a missing program, the working directory, output files, group kill and no leaked descriptors. Children are /bin/sh one-liners.
#include "check.hpp"
#include "sub0squad/platform_process.hpp"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

using namespace sub0squad::platform;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
ProcessSpec sh(const std::string& script) { return ProcessSpec{.argv = {"sh", "-c", script}}; }
std::string slurp(const fs::path& p) { std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
}  // namespace

int main()
{
    {   // the exit code, and a signal as a signal (with the shell's 128 + n)
        auto r = runCaptured(sh("exit 3"));
        CHECK(r.has_value() && r->status.exited() && r->status.code == 3 && !r->timedOut);
        auto k = runCaptured(sh("kill -9 $$"));
        CHECK(k.has_value() && !k->status.exited() && k->status.signal == 9 && k->status.shellCode() == 137);
    }
    {   // stdout and stderr together by default; stderr left out on request
        auto both = runCaptured(sh("echo out; echo err >&2"));
        CHECK(both.has_value() && both->output == "out\nerr\n");
        auto only = runCaptured(sh("echo out; echo 'err (expected on the test log: stderr is inherited here)' >&2"), {.includeStderr = false});
        CHECK(only.has_value() && only->output.find("out\n") == 0 && only->output.find("err") == std::string::npos);
    }
    {   // a long output keeps its END (where a compiler or a test runner reports the failure)
        auto r = runCaptured(sh("i=0; while [ $i -lt 3000 ]; do echo line-$i; i=$((i+1)); done; echo LAST"), {.maxBytes = 1000});
        CHECK(r.has_value() && r->truncated && r->output.size() == 1000 && r->output.ends_with("LAST\n") && r->status.code == 0);
    }
    {   // a timeout kills it, promptly
        const auto began = std::chrono::steady_clock::now();
        auto r = runCaptured(sh("echo started; exec sleep 30"), {.timeout = 300ms});   // exec: the kill reaches the sleep itself, no orphan is left
        CHECK(r.has_value() && r->timedOut && r->status.signal == SIGKILL && r->output == "started\n");
        CHECK(std::chrono::steady_clock::now() - began < 10s);
    }
    {   // a program that does not exist: an error that names it, not a silent exit code
        auto r = runCaptured(ProcessSpec{.argv = {"s0q-no-such-program"}});
        CHECK(!r.has_value() && r.error().message.find("s0q-no-such-program") != std::string::npos && r.error().code != 0);
        CHECK(!spawn(ProcessSpec{}).has_value());
        auto badDir = runCaptured(ProcessSpec{.argv = {"true"}, .cwd = "/s0q/no/such/dir"});
        CHECK(!badDir.has_value() && badDir.error().message.find("/s0q/no/such/dir") != std::string::npos);
    }
    const fs::path dir = makeTempDir("platform");
    {   // the working directory
        auto r = runCaptured(ProcessSpec{.argv = {"pwd", "-P"}, .cwd = dir});
        CHECK(r.has_value() && r->output == fs::canonical(dir).string() + "\n");
    }
    {   // an output file: truncated, then appended; stdout and stderr both land there
        auto a = spawn(ProcessSpec{.argv = {"sh", "-c", "echo one; echo two >&2"}, .outputFile = dir / "log.txt"});
        CHECK(a.has_value() && a->wait().has_value());
        auto b = spawn(ProcessSpec{.argv = {"sh", "-c", "echo three"}, .outputFile = dir / "log.txt", .appendOutput = true});
        CHECK(b.has_value() && b->wait().value_or(ExitStatus{}).code == 0);
        CHECK_EQ(slurp(dir / "log.txt"), std::string{"one\ntwo\nthree\n"});
        auto c = spawn(ProcessSpec{.argv = {"sh", "-c", "echo fresh"}, .outputFile = dir / "log.txt"});
        CHECK(c.has_value() && c->wait().has_value());
        CHECK_EQ(slurp(dir / "log.txt"), std::string{"fresh\n"});
    }
    {   // wait with a timeout returns nothing while it runs; kill ends it; a second wait gives the same status
        auto c = spawn(sh("exec sleep 30"));
        CHECK(c.has_value());
        if (c) {
            CHECK(!c->wait(50ms).has_value());
            CHECK(c->kill());
            const auto s = c->wait(5s);
            CHECK(s.has_value() && s->signal == SIGKILL);
            CHECK(c->wait().has_value() && c->wait()->signal == SIGKILL);
            CHECK(!c->kill());   // already gone
        }
    }
    {   // a new session: kill reaches the whole group, including a background child the shell started
        auto c = spawn(ProcessSpec{.argv = {"sh", "-c", "sleep 30 & echo $! > bg.pid; wait"}, .cwd = dir, .newSession = true});
        CHECK(c.has_value());
        if (c) {
            long bg = 0;
            for (int i = 0; i < 100 && bg == 0; ++i) { std::this_thread::sleep_for(20ms); std::ifstream(dir / "bg.pid") >> bg; }
            CHECK(bg > 1);
            CHECK(c->terminate());
            CHECK(c->wait(5s).has_value());
            bool gone = false;
            for (int i = 0; i < 100 && !gone && bg > 1; ++i) { gone = ::kill(static_cast<pid_t>(bg), 0) != 0; if (!gone) std::this_thread::sleep_for(20ms); }
            CHECK(gone);
            if (!gone && bg > 1) ::kill(static_cast<pid_t>(bg), SIGKILL);
        }
    }
    {   // only the standard streams cross: a descriptor this program holds (not close-on-exec) is not open in the child
        const int fd = ::open((dir / "held.txt").c_str(), O_WRONLY | O_CREAT, 0600);
        CHECK(fd > 2);
        auto r = runCaptured(sh("ls /proc/$$/fd"));
        CHECK(r.has_value());
        if (r) {
            std::istringstream in(r->output);
            bool leaked = false;
            for (std::string n; in >> n;) if (n == std::to_string(fd)) leaked = true;
            CHECK(!leaked);
        }
        ::close(fd);
    }
    {   // a spec with an output file cannot also be captured
        CHECK(!runCaptured(ProcessSpec{.argv = {"true"}, .outputFile = dir / "x.txt"}).has_value());
    }
    removeTree(dir);
    return finish();
}
