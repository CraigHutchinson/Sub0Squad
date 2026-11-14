// The command line: the usage text and the dispatch of every command to its unit (cli_impl.hpp).
#include "cli_impl.hpp"

#include "sub0squad/client.hpp"
#include "cmd_archive.hpp"
#include "cmd_process.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/build_id.hpp"
#include "json_util.hpp"
#include "sub0squad/daemon.hpp"
#include "sub0squad/frame.hpp"
#include "sub0squad/plan_lint.hpp"
#include "sub0squad/portal.hpp"
#include "sub0squad/qa.hpp"
#include "sub0squad/recall.hpp"
#include "sub0squad/replay.hpp"
#include "sub0squad/job_store.hpp"
#include "sub0squad/journal.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/status.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/workflow.hpp"
#include "sub0squad/workflow_status.hpp"
#include "sub0squad/unix_socket.hpp"
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sub0squad {

namespace cli {

const char* const kUsage =
    "usage: sub0squad <command> [options]\n"
    "  FIRST USE:  doctor  |  up  |  do \"write a calculator in C++\"  |  project wait w-000001  |  down      (README.md, \"First use\")\n"
    "  doctor [--engine NAME=URL]           check what a first run needs (memory, tools, ports, the engine) and say what to fix\n"
    "  up [--max-jobs N] [--frontier off|stub] [--portal-port N] [--no-portal]   start the daemon and the portal in the background; `down` stops them\n"
    "  process list | show NAME | tools | check FILE | add NAME [--from BASE] [--without STAGE]... [--set STAGE.PARAMETER=VALUE]... | add NAME --file FILE\n"
    "                                       the workflows you can run (`do ... --process NAME`): a process is a list of stages, built-in or a file in <state>/processes/\n"
    "  block list | run [--block N]         which building blocks are proven on this build (docs/BLOCK_PROVING_GROUNDS.md)\n"
    "  serve [--max-jobs N] [--mem-shed-mib N]   run the daemon (foreground); sheds jobs below N MiB available memory (default 1536, 0 = off)\n"
    "  submit [opts] -- <cmd> [args...]     submit a process job; prints its id\n"
    "         --cwd DIR  --wall SEC  --memory-mib N (refused at submit if the host lacks it)  --grace SEC  --tool NAME  --tier stub|local  --env K=V  --lease RESOURCE=CAP (at most CAP running jobs hold RESOURCE)  --service (a server: not counted against --max-jobs)\n"
    "         --resume-arg ARG (repeatable: the command `resume` runs)  --state-file PATH --state-schema NAME\n"
    "         --spec FILE (a job-spec-1 JSON file instead of a command)\n"
    "  status [ID] [--json]                 jobs and progress (works without the daemon, from the journal)\n"
    "  logs ID [--stderr] [--follow] [--run N]\n"
    "  cancel ID | resume ID | wait ID [--timeout SEC] | ping | stop\n"
    "  check                                make the daemon run its watchers now (memory pressure, then admission) instead of at its next timer; `ping` changes nothing\n"
    "  archive [--label TEXT]               set the finished runs aside (state -> archive/<UTC time>[-label]/state) and leave a fresh, empty state; the daemon must be stopped\n"
    "  every submit (submit, do, project submit) is refused when the running daemon is another build of this program: restart it, or pass --allow-build-mismatch\n"
    "  do \"<task in words>\" [project options]   triage first, then run: a known blueprint starts at once, otherwise a needs-plan.md handoff is written\n"
    "  project submit --task calculator-web|calculator-cpp|calculator-py (any blueprint of a registered harness starts at once) [--sandbox DIR] [--inject ITEM[:N]]... [--max-local N]\n"
    "         [--agent mock|llm] [--engine NAME=http://127.0.0.1:PORT/v1[,model=..][,ctx=N][,slots=N]]... [--engine-for PERSONA=NAME]... [--max-tokens N] [--treatment baseline|index|planfirst] [--executor session|canvas|fork] [--process NAME] [--mends FILE (a mends-1 table of this workflow's own, merged over the built-in one: `process mends`)] [--repair-turns N] [--size-budget TOKENS] [--fallback-engine NAME] [--ask]\n"
    "  project plan ID --file plan.json     supply the plan a workflow is awaiting (the deferred architect's answer)\n"
    "  request list [WF] | raise WF --kind K [--scope a,b] [--blocking] [--title T] [--detail D] [--option O]... | answer WF RID [--resolution retry|stop] [--answer TEXT]\n"
    "                                       the inbox: questions, clarifications, requirements, blockers, proposals, escalations\n"
    "  replay WF NODE [--seeds 1,2,3] [--think] [--decompose] [--fork [--parallel N | --sequential-reason TEXT]]   run one node again in isolation, several seeds side by side (minutes, not a whole workflow)\n"
    "  qa DIR|WORKFLOW                      quality findings over sources: duplicated code, the same helper declared twice, very long functions\n"
    "  recall WF words... [--item NODE] [--limit N]   search what a workflow already did: conversations, replies, gate outputs, briefings\n"
    "  plan-lint FILE [--tps N]             audit a plan: width, critical path, rough time, serial tails\n"
    "  portal [--port N] [--engine-url URL]  read-only status portal on 127.0.0.1 (default 8780): the work graph, tokens per node, daemon jobs\n"
    "  guard [--memory-mib N] [--reserve-mib R] [--disk-path P --disk-mib N]   explicit host guards (exit 1 when refused)\n"
    "  fetch-model --url https://huggingface.co/... --out FILE [--sha256 H]   guarded, resumable, verified download (run it as a job)\n"
    "  blueprint NAME                       print a built-in plan-2 (calculator-web, calculator-cpp, calculator-py)\n"
    "  project status [ID] [--json] | project cancel ID | project wait ID [--timeout SEC]\n"
    "  serve also takes --frontier off|stub (default stub: the escalation path runs, nothing is sent)\n"
    "  --selftest | --version\n";

}  // namespace cli

using namespace cli;

int runCli(int argc, char** argv)
{
    if (argc < 2) { std::fputs(kUsage, stderr); return 2; }
    const std::string cmd = argv[1];
    const RuntimePaths paths = RuntimePaths::fromEnvironment();
    if (cmd == "serve") return cmdServe(paths, argc, argv);
    if (cmd == "submit") return cmdSubmit(paths, argc - 2, argv + 2);
    if (cmd == "status") return cmdStatus(paths, argc, argv);
    if (cmd == "plan-lint") return cmdPlanLint(paths, argc, argv);
    if (cmd == "portal") return cmdPortal(paths, argc, argv);
    if (cmd == "do") return cmdDo(paths, argc - 2, argv + 2);
    if (cmd == "archive") return runArchiveCommand(std::vector<std::string>(argv + 2, argv + argc), paths);
    if (cmd == "process") return runProcessCommand(std::vector<std::string>(argv + 2, argv + argc), paths.stateDir);
    if (cmd == "blueprint") { if (argc < 3) { std::fputs(kUsage, stderr); return 2; } const Harness* bh = harnessWithBlueprint(argv[2]); const std::string b = bh ? bh->blueprint(argv[2]) : std::string{}; if (b.empty()) { std::fprintf(stderr, "no blueprint '%s'\n", argv[2]); return 1; } std::printf("%s", b.c_str()); return 0; }
    if (cmd == "project") return cmdProject(paths, argc - 2, argv + 2);
    if (cmd == "request") return cmdRequest(paths, argc - 2, argv + 2);
    if (cmd == "replay") return cmdReplay(paths, argc, argv);
    if (cmd == "qa") return cmdQa(paths, argc, argv);
    if (cmd == "recall") return cmdRecall(paths, argc, argv);
    if (cmd == "logs") return cmdLogs(paths, argc - 2, argv + 2);
    if (cmd == "wait") return cmdWait(paths, argc - 2, argv + 2);
    if (cmd == "cancel" || cmd == "resume") {
        if (argc < 3) { std::fputs(kUsage, stderr); return 2; }
        return simpleCommand(paths, cmd.c_str(), argv[2], true);
    }
    if (cmd == "ping") return simpleCommand(paths, "ping", "", true);
    if (cmd == "check") return simpleCommand(paths, "check", "", true);
    if (cmd == "stop") return simpleCommand(paths, "shutdown", "", false);
    std::fputs(kUsage, stderr);
    return 2;
}

}  // namespace sub0squad
