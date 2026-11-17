// Process identity and the tree (platform_proc.hpp, docs/PLATFORM.md): the pure parsers on fixed text, then the live calls against this
// process and short-lived children (tests/platform_child.cpp) started through platform_process.
#include "check.hpp"
#include "sub0squad/platform_proc.hpp"
#include "sub0squad/platform_process.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <thread>

using namespace sub0squad::platform;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

int main()
{
    {   // /proc/PID/stat: the command name may hold spaces and ')' — fields are read after the LAST ')'
        const std::string text = "4242 (odd ) name) S 17 4242 4242 0 -1 4194560 100 0 0 0 1 2 0 0 20 0 1 0 987654 1000 200 18446744073709551615";
        const auto st = parseProcStat(text);
        CHECK(st.has_value());
        if (st) { CHECK_EQ(st->state, 'S'); CHECK_EQ(st->parent, ProcessId{17}); CHECK_EQ(st->start, std::uint64_t{987654}); }
        CHECK(!parseProcStat("").has_value());
        CHECK(!parseProcStat("12 (name) S 1 2 3").has_value());   // cut short before starttime
        CHECK(!parseProcStat("12 no-parenthesis S 1").has_value());
    }
    {   // /proc/PID/cmdline: NUL-separated, a trailing NUL ends the last argument
        using namespace std::string_literals;
        CHECK((parseCmdline("sub0squad\0portal\0--port\0""8080\0"s) == std::vector<std::string>{"sub0squad", "portal", "--port", "8080"}));
        CHECK((parseCmdline("a\0\0b"s) == std::vector<std::string>{"a", "", "b"}));   // an empty argument in the middle is kept
        CHECK(parseCmdline("").empty());
    }
    {   // this process: its program, id, start stamp and command line
        const fs::path self = selfExecutable();
        CHECK(!self.empty() && fs::exists(self) && self.filename().string().find("test_platform_proc") != std::string::npos);
        const ProcessId me = currentProcess();
        CHECK(me > 0);
        const auto start = processStart(me);
        CHECK(start.has_value());
        if (start) { CHECK(isAlive(me, *start)); CHECK(!isAlive(me, *start + 1)); }   // a different stamp is a different process
        const auto args = commandLine(me);
        CHECK(args.has_value() && !args->empty() && args->front().find("test_platform_proc") != std::string::npos);
        CHECK(!processStart(0x7ffffff0).has_value());
        CHECK(!commandLine(0x7ffffff0).has_value());
    }
    {   // descendants: a child and its grandchild; once reaped, the child is gone and its stamp no longer answers
        const auto dir = makeTempDir("platform-proc");
        const std::string child = SUB0SQUAD_PLATFORM_CHILD;
        auto c = spawn(ProcessSpec{.argv = {child, "tree", "gc.pid", child}, .cwd = dir});
        CHECK(c.has_value());
        if (c) {
            ProcessId grandchild = 0;
            for (int i = 0; i < 250 && grandchild == 0; ++i) { std::this_thread::sleep_for(20ms); std::ifstream(dir / "gc.pid") >> grandchild; }
            const auto tree = descendants(currentProcess());
            CHECK(std::ranges::find(tree, c->id()) != tree.end());
            CHECK(grandchild > 0 && std::ranges::find(tree, grandchild) != tree.end());
            const auto stamp = processStart(c->id());
            CHECK(stamp.has_value());
            CHECK(terminateProcess(grandchild));
            CHECK(c->wait(5000ms).has_value());   // the child's wait returns once the grandchild is gone, then the child exits
            if (stamp) CHECK(!isAlive(c->id(), *stamp));
            CHECK(!terminateProcess(0));
        }
        ::removeTree(dir);   // check.hpp's, which retries; platform::removeTree is also in scope
    }
    {   // the environment is inherited by children started afterwards
        setEnvironment("SUB0SQUAD_PLATFORM_PROC_TEST", "yes");
        auto r = runCaptured(ProcessSpec{.argv = {SUB0SQUAD_PLATFORM_CHILD, "env", "SUB0SQUAD_PLATFORM_PROC_TEST"}});
        CHECK(r.has_value() && r->output == "yes");
        unsetEnvironment("SUB0SQUAD_PLATFORM_PROC_TEST");
        CHECK(std::getenv("SUB0SQUAD_PLATFORM_PROC_TEST") == nullptr);
    }
    return finish();
}
