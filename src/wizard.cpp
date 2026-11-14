#include "wizard.hpp"

#include "first_use.hpp"
#include "sub0squad/clarify.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/paths.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

extern char** environ;

namespace sub0squad {

namespace fs = std::filesystem;

namespace {

std::string option(const std::vector<std::string>& a, const std::string& key, const std::string& d = {})
{
    for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == "--" + key) return a[i + 1];
    return d;
}
bool flag(const std::vector<std::string>& a, const std::string& key) { for (const auto& x : a) if (x == "--" + key) return true; return false; }

std::string readFile(const fs::path& p)
{
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// One command of this program, run as a child with its output captured (stdout and stderr together). The program's path was passed in by main.
struct Ran { int code = -1; std::string out; };
Ran runCommand(const std::string& exe, std::vector<std::string> args)
{
    Ran r;
    int fd[2];
    if (::pipe2(fd, O_CLOEXEC) != 0) return r;   // close-on-exec: a daemon the command starts must not inherit the pipe, or this read would never end
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 2);
    posix_spawn_file_actions_addclose(&fa, fd[0]);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    std::vector<char*> argv{const_cast<char*>(exe.c_str())};
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int rc = ::posix_spawn(&pid, exe.c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(fd[1]);
    if (rc != 0) { ::close(fd[0]); return r; }
    char buf[4096];
    for (ssize_t n; (n = ::read(fd[0], buf, sizeof buf)) > 0;) r.out.append(buf, static_cast<std::size_t>(n));
    ::close(fd[0]);
    int st = 0;
    ::waitpid(pid, &st, 0);
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

/// One item of the checklist. The items and their order are data; a step of the flow sets an item's state.
struct Item
{
    const char* id;
    const char* title;
    std::string state, detail;
    Item(const char* i, const char* t) : id(i), title(t), state("todo") {}
};

struct Wizard
{
    std::string exe, version;
    bool yes = false;
    std::istream* answers = &std::cin;
    std::ifstream script;
    bool scripted = false;
    std::vector<Item> items = {
        {"binary", "this program"}, {"memory", "memory and disk"}, {"harness", "a project toolchain"}, {"ports", "the portal's port"},
        {"model", "a model (or the mock agent)"}, {"daemon", "the daemon"}, {"portal", "the portal"}, {"project", "a project"},
    };

    Item& item(const char* id) { for (auto& i : items) if (std::string_view{i.id} == id) return i; return items.front(); }
    void set(const char* id, const std::string& state, const std::string& detail) { item(id).state = state; item(id).detail = detail; }

    void show(const char* heading)
    {
        std::printf("\n%s\n", heading);
        bool nextShown = false;
        for (const auto& i : items) {
            const bool next = !nextShown && i.state == "todo";
            nextShown = nextShown || next;
            std::printf("  [%s] %-28s %s\n", i.state == "ok" ? "ok" : i.state == "fix" ? "!!" : i.state == "skipped" ? "--" : next ? ">>" : "  ", i.title, i.detail.c_str());
        }
        std::fflush(stdout);
    }

    /// One answer: nullopt when there is none left (a script that ended, a closed terminal). `--yes` answers with the default.
    std::optional<std::string> ask(const std::string& question, const std::string& def)
    {
        std::printf("\n? %s [%s] ", question.c_str(), def.c_str());
        std::fflush(stdout);
        if (yes) { std::printf("%s   (--yes: the default)\n", def.c_str()); return def; }
        std::string line;
        if (!std::getline(*answers, line)) { std::printf("\nno answer is left%s: the wizard stops here and changes nothing further.\n", scripted ? " in the script" : ""); return std::nullopt; }
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (scripted) std::printf("%s\n", line.c_str());   // a recorded keystroke is echoed, so the transcript reads as the session did
        return line.empty() ? def : line;
    }
    /// A yes/no question: nullopt as for `ask`.
    std::optional<bool> confirm(const std::string& question, bool def)
    {
        const auto a = ask(question, def ? "Y/n" : "y/N");
        if (!a) return std::nullopt;
        if (*a == "Y/n" || *a == "y/N") return def;
        return (*a)[0] == 'y' || (*a)[0] == 'Y';
    }
};

}  // namespace

bool isLoopbackEndpoint(const std::string& url)
{
    auto at = url.find("://");
    std::string host = at == std::string::npos ? url : url.substr(at + 3);
    host = host.substr(0, host.find('/'));
    if (const auto user = host.rfind('@'); user != std::string::npos) host = host.substr(user + 1);
    if (!host.empty() && host[0] == '[') host = host.substr(0, host.find(']') + 1);
    else host = host.substr(0, host.find(':'));
    return host == "localhost" || host == "[::1]" || host.rfind("127.", 0) == 0;
}

int runWizard(const std::vector<std::string>& args, const std::string& exe, const std::string& version)
{
    if (exe.empty()) { std::fprintf(stderr, "wizard: cannot find this program's own path; run it by its full path\n"); return 64; }
    Wizard w;
    w.exe = exe; w.version = version; w.yes = flag(args, "yes");
    if (const std::string file = option(args, "script"); !file.empty()) {
        w.script.open(file);
        if (!w.script) { std::fprintf(stderr, "wizard: cannot read the script %s\n", file.c_str()); return 64; }
        w.answers = &w.script; w.scripted = true;
    }
    const std::string portalPort = option(args, "portal-port", "8780");
    const bool noPortal = flag(args, "no-portal");
    std::string engine = option(args, "engine");
    const RuntimePaths paths = RuntimePaths::fromEnvironment();

    std::printf("sub0squad wizard: from here to a first finished project. Nothing is started, stopped or sent without your yes;\n"
                "nothing is downloaded or installed; the frontier tier stays off.\n");

    // ---- the checklist, from the doctor's checks ----
    // The engine is asked only once its address is accepted (below): an address that is not this machine is never contacted without a yes.
    const auto check = [&](bool withEngine) {
        std::vector<std::string> doctorArgs = {"--portal-port", portalPort};
        if (withEngine && !engine.empty()) { doctorArgs.push_back("--engine"); doctorArgs.push_back(engine); }
        return doctorChecks(doctorArgs, exe, version);
    };
    std::vector<std::string> notes;
    const auto apply = [&](const std::vector<DoctorCheck>& checks) {
        notes.clear();
        int fixes = 0;
        bool harnessOk = false;
        for (const auto& c : checks) {
            if (c.level != "ok") notes.push_back(c.text);
            if (c.id == "binary") w.set("binary", "ok", c.text);
            else if (c.id == "memory" || c.id == "disk" || c.id == "state") { if (c.level == "fix") { w.set("memory", "fix", c.text); ++fixes; } else if (c.id == "memory") w.set("memory", "ok", c.text); }
            else if (c.id == "harness") { if (c.level == "ok") { harnessOk = true; w.set("harness", "ok", c.text); } else if (c.level == "fix") { w.set("harness", "fix", c.text); ++fixes; } }
            else if (c.id == "ports") w.set("ports", c.level == "ok" ? "ok" : "todo", c.text);
            else if (c.id == "daemon") w.set("daemon", c.level == "ok" ? "ok" : "todo", c.text);
            else if (c.id == "engine" && c.level != "note") w.set("model", c.level == "ok" ? "ok" : "fix", c.text);
        }
        if (!harnessOk && w.item("harness").state != "fix") w.set("harness", "fix", "no project harness has its tools on PATH");
        return fixes;
    };
    if (apply(check(false)) > 0) {
        w.show("What a first project needs:");
        std::printf("\nFix the items marked [!!] first, then run the wizard again. Nothing was changed.\n");
        return 1;
    }
    w.show("What a first project needs:");

    // ---- a model, or none ----
    if (engine.empty()) {
        const auto a = w.ask("A model server that is ALREADY running, as NAME=http://127.0.0.1:PORT/v1, or Enter for the mock agent (no model; the wizard never starts, downloads or installs one)", "mock");
        if (!a) return 0;
        if (*a != "mock") engine = *a;
    }
    if (!engine.empty()) {
        const auto profile = EngineProfile::parse(engine.find('=') == std::string::npos ? "engine=" + engine : engine);
        if (!profile) { std::printf("that is not an engine (%s): using the mock agent.\n", profile.error().c_str()); engine.clear(); }
        else if (!isLoopbackEndpoint(profile->endpoint)) {
            const auto go = w.confirm(profile->endpoint + " is NOT this machine: prompts and code would be sent to it. Use it anyway?", false);
            if (!go) return 0;
            if (!*go) { std::printf("not used: nothing is sent off this machine. Using the mock agent.\n"); engine.clear(); }
        }
    }
    if (!engine.empty()) {
        apply(check(true));
        if (w.item("model").state != "ok") { std::printf("%s\nusing the mock agent instead.\n", w.item("model").detail.c_str()); engine.clear(); }
    }
    if (engine.empty()) w.set("model", "ok", "the mock agent: reference deliverables, no model, nothing leaves the machine");

    // ---- the model may explain, and only explain ----
    if (!engine.empty() && !notes.empty()) {
        const auto want = w.confirm("Ask the model to explain the notes of the checklist? It only explains: nothing it says is run", false);
        if (!want) return 0;
        if (*want) {
            std::string text;
            for (const auto& n : notes) text += "- " + n + "\n";
            const auto profile = EngineProfile::parse(engine.find('=') == std::string::npos ? "engine=" + engine : engine);
            const auto reply = chatCompleteMessages(*profile, {{"system", "You explain the notes of a setup checklist to its owner in at most four short sentences. You do not give commands to run."}, {"user", "Explain these notes:\n" + text}}, 300, 60);
            if (reply) std::printf("the model says (advice only; the wizard runs nothing from it):\n%s\n", reply->content.c_str());
            else std::printf("the model did not answer (%s); carrying on without its explanation.\n", reply.error().c_str());
        }
    }

    // ---- the daemon and the portal: `up` ----
    if (w.item("daemon").state != "ok") {
        const auto start = w.confirm(std::string{"Start the daemon"} + (noPortal ? "" : " and the portal") + " now (`sub0squad up`, frontier off)?", true);
        if (!start) return 0;
        if (!*start) { w.show("Stopped at your word; nothing was started:"); return 0; }
        std::vector<std::string> up = {"up", "--frontier", "off", "--portal-port", portalPort};
        if (noPortal) up.push_back("--no-portal");
        const Ran r = runCommand(exe, up);
        std::istringstream lines(r.out);
        for (std::string l; std::getline(lines, l);) { if (l.empty()) break; std::printf("  %s\n", l.c_str()); if (l.rfind("portal:", 0) == 0) w.set("portal", noPortal ? "skipped" : "ok", l.substr(8)); }
        if (r.code != 0) { w.set("daemon", "fix", "`up` failed"); w.show("The daemon did not start:"); return 1; }
        w.set("daemon", "ok", "started by the wizard, frontier off");
        w.set("ports", "ok", "the portal's port is the portal's");
    } else {
        w.set("daemon", "ok", "already running (its settings are its own: the wizard changes none)");
        w.set("portal", noPortal ? "skipped" : "ok", noPortal ? "--no-portal" : "http://127.0.0.1:" + portalPort + "/ if one is running there");
    }
    if (noPortal) w.set("portal", "skipped", "--no-portal");

    // ---- a project: `do`, under the process that asks ----
    const auto text = w.ask("What should be built?", "write a calculator");
    if (!text) return 0;
    std::vector<std::string> submit = {"do", *text, "--process", "clarified"};
    if (!engine.empty()) { submit.insert(submit.end(), {"--agent", "llm", "--engine", engine}); }
    const Ran d = runCommand(exe, submit);
    std::printf("%s", d.out.c_str());
    std::string wf;
    if (const auto at = d.out.find("workflow w-"); at != std::string::npos) wf = d.out.substr(at + 9, d.out.find('\n', at) - at - 9);
    if (d.code != 0 || wf.empty()) { w.set("project", "fix", "`do` failed"); w.show("The project was not started:"); return 1; }
    const fs::path sandbox = paths.workflowsDir() / wf;
    // The planning question of that process, shown and answered here (the same request `request list` and the portal's inbox show).
    bool settled = false;
    for (int i = 0; i < 200 && !settled; ++i) {
        if (fs::exists(sandbox / "brief.json")) { settled = true; break; }
        const Ran inbox = runCommand(exe, {"request", "list", wf});
        if (inbox.out.find("planner") != std::string::npos && inbox.out.find("open") != std::string::npos) {
            const auto questions = ClarifyPlan::fromJson(readFile(sandbox / "questions.json"));
            if (questions) std::printf("\n%s", questions->requestDetail().c_str());
            for (int tries = 0; tries < 3 && !settled; ++tries) {
                const auto answer = w.ask("Your answer", "go");
                if (!answer) return 0;
                const Ran a = runCommand(exe, {"request", "answer", wf, "r-1", "--answer", *answer});
                if (a.code == 0) settled = true; else std::printf("%s", a.out.c_str());
            }
            if (!settled) { w.set("project", "fix", wf + " waits for an answer: `sub0squad request answer " + wf + " r-1 --answer go`"); w.show("The question is still open:"); return 1; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (int i = 0; i < 100 && !fs::exists(sandbox / "brief.json"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (const auto brief = readFile(sandbox / "brief.json"); !brief.empty()) {
        const auto at = brief.find("\"task\":\"");
        std::printf("planned as: %s\n", at == std::string::npos ? "?" : brief.substr(at + 8, brief.find('"', at + 8) - at - 8).c_str());
    }
    w.set("project", "todo", wf + " is running");
    const auto wait = w.confirm("Wait here until " + wf + " ends?", true);
    if (!wait) return 0;
    if (*wait) {
        const Ran done = runCommand(exe, {"project", "wait", wf, "--timeout", "900"});
        std::printf("%s", done.out.c_str());
        w.set("project", done.code == 0 ? "ok" : "fix", done.code == 0 ? wf + " finished: " + (sandbox / "project").string() + " and report.md beside it" : wf + " did not succeed: `sub0squad project status " + wf + "`");
    } else w.set("project", "ok", wf + " runs on: `sub0squad project status " + wf + "`; results in " + (sandbox / "project").string());

    // ---- stop, if wanted ----
    const auto stop = w.confirm("Stop the daemon and the portal now (`sub0squad down`)?", false);
    if (stop && *stop) {
        const Ran down = runCommand(exe, {"down"});
        std::printf("%s", down.out.c_str());
        w.set("daemon", "skipped", "stopped by the wizard");
        if (!noPortal) w.set("portal", "skipped", "stopped by the wizard");
    }
    w.show("Where it stands:");
    return w.item("project").state == "fix" ? 1 : 0;
}

}  // namespace sub0squad
