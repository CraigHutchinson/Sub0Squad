// The one launcher (platform_process.hpp, docs/PLATFORM.md): exit codes and signals, capture with and without stderr, the tail kept when the output
// is long, timeouts, a missing program, the working directory, output files, group kill and no leaked descriptors. Children are tests/platform_child.cpp,
// the same program on every system.
#include "check.hpp"
#include "sub0squad/platform_proc.hpp"
#include "sub0squad/platform_process.hpp"

#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif

#include <fstream>
#include <sstream>

using namespace sub0squad::platform;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
const std::string kChild = SUB0SQUAD_PLATFORM_CHILD;
constexpr int kKill = 9;   // SIGKILL; Windows has no such constant, but Child::kill reports the same signal there
#if defined(_WIN32)
constexpr int kCrash = 11;   // an access violation is reported as SIGSEGV
#else
constexpr int kCrash = kKill;
#endif
ProcessSpec child(std::vector<std::string> args)
{
    args.insert(args.begin(), kChild);
    return ProcessSpec{.argv = std::move(args)};
}
std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
}  // namespace

int main()
{
    {   // the exit code, and a crash as a signal (with the shell's 128 + n)
        auto r = runCaptured(child({"exit", "3"}));
        CHECK(r.has_value() && r->status.exited() && r->status.code == 3 && !r->timedOut);
        auto k = runCaptured(child({"crash"}));
        CHECK(k.has_value() && !k->status.exited() && k->status.signal == kCrash && k->status.shellCode() == 128 + kCrash);
    }
    {   // stdout and stderr together by default; stderr left out on request
        auto both = runCaptured(child({"outerr", "out", "err"}));
        CHECK(both.has_value() && both->output == "out\nerr\n");
        auto only = runCaptured(child({"outerr", "out", "err (expected on the test log: stderr is inherited here)"}), {.includeStderr = false});
        CHECK(only.has_value() && only->output.find("out\n") == 0 && only->output.find("err") == std::string::npos);
    }
    {   // a long output keeps its END (where a compiler or a test runner reports the failure)
        auto r = runCaptured(child({"lines", "3000"}), {.maxBytes = 1000});
        CHECK(r.has_value() && r->truncated && r->output.size() == 1000 && r->output.ends_with("LAST\n") && r->status.code == 0);
    }
    {   // a timeout kills it, promptly
        const auto began = std::chrono::steady_clock::now();
        auto r = runCaptured(child({"say", "started", "30000"}), {.timeout = 300ms});
        CHECK(r.has_value() && r->timedOut && r->status.signal == kKill && r->output == "started\n");
        CHECK(std::chrono::steady_clock::now() - began < 10s);
    }
    {   // a program that does not exist: an error that names it, not a silent exit code
        auto r = runCaptured(ProcessSpec{.argv = {"s0q-no-such-program"}});
        CHECK(!r.has_value() && r.error().message.find("s0q-no-such-program") != std::string::npos && r.error().code != 0);
        CHECK(!spawn(ProcessSpec{}).has_value());
        auto badDir = runCaptured(ProcessSpec{.argv = {kChild, "exit", "0"}, .cwd = "/s0q/no/such/dir"});
        CHECK(!badDir.has_value() && badDir.error().message.find("/s0q/no/such/dir") != std::string::npos);
    }
    const fs::path dir = makeTempDir("platform");
    {   // the working directory
        auto r = runCaptured(ProcessSpec{.argv = {kChild, "pwd"}, .cwd = dir});
        std::error_code ec;
        CHECK(r.has_value() && fs::equivalent(fs::path(r->output), dir, ec));
    }
    {   // an output file: truncated, then appended; stdout and stderr both land there
        auto a = spawn(ProcessSpec{.argv = {kChild, "outerr", "one", "two"}, .outputFile = dir / "log.txt"});
        CHECK(a.has_value() && a->wait().has_value());
        auto b = spawn(ProcessSpec{.argv = {kChild, "out", "three"}, .outputFile = dir / "log.txt", .appendOutput = true});
        CHECK(b.has_value() && b->wait().value_or(ExitStatus{}).code == 0);
        CHECK_EQ(slurp(dir / "log.txt"), std::string{"one\ntwo\nthree\n"});
        auto c = spawn(ProcessSpec{.argv = {kChild, "out", "fresh"}, .outputFile = dir / "log.txt"});
        CHECK(c.has_value() && c->wait().has_value());
        CHECK_EQ(slurp(dir / "log.txt"), std::string{"fresh\n"});
    }
    {   // wait with a timeout returns nothing while it runs; kill ends it; a second wait gives the same status
        auto c = spawn(child({"sleep", "30000"}));
        CHECK(c.has_value());
        if (c) {
            CHECK(!c->wait(50ms).has_value());
            CHECK(c->kill());
            const auto s = c->wait(5s);
            CHECK(s.has_value() && s->signal == kKill);
            CHECK(c->wait().has_value() && c->wait()->signal == kKill);
            CHECK(!c->kill());   // already gone
        }
    }
    {   // a new session: terminate reaches the whole group (a job object on Windows), including a grandchild
        auto c = spawn(ProcessSpec{.argv = {kChild, "tree", "bg.pid", kChild}, .cwd = dir, .newSession = true});
        CHECK(c.has_value());
        if (c) {
            ProcessId bg = 0;
            for (int i = 0; i < 250 && bg == 0; ++i) { std::this_thread::sleep_for(20ms); std::ifstream(dir / "bg.pid") >> bg; }
            CHECK(bg > 1);
            const auto stamp = processStart(bg);
            CHECK(stamp.has_value());
            CHECK(c->terminate());
            CHECK(c->wait(5s).has_value());
            bool gone = !stamp;
            for (int i = 0; i < 100 && !gone; ++i) { gone = !isAlive(bg, *stamp); if (!gone) std::this_thread::sleep_for(20ms); }
            CHECK(gone);
            if (!gone) (void)terminateProcess(bg);
        }
    }
    {   // only the standard streams cross: a descriptor this program holds (inheritable) is not open in the child
        const fs::path held = dir / "held.txt";
#if defined(_WIN32)
        int fd = -1;
        (void)::_wsopen_s(&fd, held.c_str(), _O_WRONLY | _O_CREAT, _SH_DENYNO, _S_IREAD | _S_IWRITE);   // the C runtime's default: inheritable
        const std::string value = std::to_string(static_cast<long long>(::_get_osfhandle(fd)));
#else
        const int fd = ::open(held.c_str(), O_WRONLY | O_CREAT, 0600);   // not close-on-exec
        const std::string value = std::to_string(fd);
#endif
        CHECK(fd > 2);
        auto r = runCaptured(child({"held", value, held.string()}));
        CHECK(r.has_value() && r->output == "closed");
#if defined(_WIN32)
        ::_close(fd);
#else
        ::close(fd);
#endif
    }
    {   // a spec with an output file cannot also be captured
        CHECK(!runCaptured(ProcessSpec{.argv = {kChild, "exit", "0"}, .outputFile = dir / "x.txt"}).has_value());
    }
    removeTree(dir);
    return finish();
}
