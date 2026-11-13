// Catalogue row 20, through a real daemon and the real command line in a temporary home: the daemon says which build it is (`ping`, `run-info.json`); a
// submit from another build is refused with what to do, and accepted with --allow-build-mismatch; `archive` refuses while the daemon runs, then moves the
// state aside, leaves a fresh one, and keeps the owner's registered processes. Also: a process with no plan and no items runs through the daemon (row 56).
#include "e2e.hpp"

#include <cstdlib>

using namespace e2e;
namespace fs = std::filesystem;

static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
static std::string field(const std::string& json, const std::string& key)
{
    const std::string pat = "\"" + key + "\":\"";
    const auto p = json.find(pat);
    return p == std::string::npos ? std::string{} : json.substr(p + pat.size(), json.find('"', p + pat.size()) - p - pat.size());
}
/// The CLI's stderr is discarded by the harness; this runs it through a shell-free spawn with stderr joined to stdout.
static Result cliAll(Squad& sq, std::vector<std::string> args)
{
    int pipefd[2];
    CHECK(pipe(pipefd) == 0);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 2);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    std::vector<char*> argv{const_cast<char*>(sq.exe().c_str())};
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid;
    CHECK(posix_spawn(&pid, sq.exe().c_str(), &fa, nullptr, argv.data(), environ) == 0);
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[1]);
    Result r;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof buf)) > 0) r.out.append(buf, static_cast<std::size_t>(n));
    close(pipefd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    r.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

int main(int, char** argv)
{
    ::unsetenv("SUB0SQUAD_BUILD_ID");
    Squad sq(argv[1], "archive");
    const fs::path state = sq.home() / "state";

    // The real build id: a version and a fingerprint of the binary; the daemon and the command line (one binary) agree, so a submit is accepted.
    sq.startDaemon();
    const std::string ping = sq.cli({"ping"}).out;
    const std::string build = field(ping, "build");
    CHECK(build.size() > 17 && build[build.size() - 17] == '+');   // VERSION+16 hex digits
    const std::string info = slurp(state / "run-info.json");
    CHECK(contains(info, "\"schema\":\"run-info-1\"") && field(info, "build") == build && contains(info, "\"pid\":" + std::to_string(sq.daemonPid())) && contains(info, "\"maxJobs\":2") && contains(info, "\"frontier\":\"stub\""));
    CHECK_EQ(sq.cli({"submit", "--", "true"}).exitCode, 0);

    // Another build on the command line (the test-only override): every kind of submit is refused, and the message says both builds and what to do.
    ::setenv("SUB0SQUAD_BUILD_ID", "newer-build", 1);
    for (const std::vector<std::string>& cmd : {std::vector<std::string>{"submit", "--", "true"}, std::vector<std::string>{"project", "submit", "--task", "calculator-py"}, std::vector<std::string>{"do", "write a calculator in Python"}}) {
        const Result r = cliAll(sq, cmd);
        CHECK(r.exitCode != 0);
        CHECK(contains(r.out, "build mismatch") && contains(r.out, "'newer-build'") && contains(r.out, "'" + build + "'") && contains(r.out, "sub0squad down") && contains(r.out, "--allow-build-mismatch"));
    }
    CHECK(!contains(sq.cli({"project", "status", "--json"}).out, "\"id\":\"w-"));    // nothing was started by the refused submits
    // reading is never refused: status and ping work across builds
    CHECK_EQ(sq.cli({"status"}).exitCode, 0);
    CHECK_EQ(sq.cli({"ping"}).exitCode, 0);
    // the override: the owner says the difference is known
    CHECK_EQ(cliAll(sq, {"submit", "--allow-build-mismatch", "--", "true"}).exitCode, 0);
    const Result forced = cliAll(sq, {"project", "submit", "--task", "calculator-py", "--allow-build-mismatch"});
    CHECK_EQ(forced.exitCode, 0);
    CHECK(contains(forced.out, "w-000001"));
    CHECK_EQ(sq.cli({"project", "wait", "w-000001", "--timeout", "120"}).exitCode, 0);
    ::unsetenv("SUB0SQUAD_BUILD_ID");

    // A process with no plan and no items (two job stages) runs through the daemon: no "awaiting a plan", both jobs run, the workflow succeeds (row 56).
    {
        const fs::path file = sq.home() / "two-jobs.json";
        { std::ofstream(file) << "{\"schema\":\"process-1\",\"name\":\"two-jobs\",\"stages\":["
                                   "{\"name\":\"scan\",\"kind\":\"job\",\"job\":{\"step\":\"scan\",\"tool\":\"qa\",\"args\":[\"${sandbox}\"]}},"
                                   "{\"name\":\"again\",\"kind\":\"job\",\"entry\":\"stage.scan.done\",\"job\":{\"step\":\"rescan\",\"label\":\"second scan\",\"tool\":\"qa\",\"args\":[\"${sandbox}\"]}}]}\n"; }
        CHECK_EQ(cliAll(sq, {"process", "check", file.string()}).exitCode, 0);
        CHECK_EQ(cliAll(sq, {"process", "add", "two-jobs", "--file", file.string()}).exitCode, 0);
        const Result sub = cliAll(sq, {"project", "submit", "--task", "custom", "--process", "two-jobs"});
        CHECK_EQ(sub.exitCode, 0);
        CHECK(contains(sub.out, "w-000002"));
        CHECK_EQ(sq.cli({"project", "wait", "w-000002", "--timeout", "60"}).exitCode, 0);
        const std::string st = sq.cli({"project", "status", "w-000002", "--json"}).out;
        CHECK(contains(st, "\"state\":\"succeeded\"") && contains(st, "\"note\":\"scan passed; second scan passed\"") && contains(st, "\"process\":\"two-jobs@1\""));
        CHECK(contains(st, "{\"name\":\"again\",\"kind\":\"job\",\"state\":\"done\",\"waitingOn\":\"\"}"));
        // a file with a mistake is refused by `check` and by `add`, with the stage and the reason
        const fs::path bad = sq.home() / "bad.json";
        { std::ofstream(bad) << "{\"schema\":\"process-1\",\"name\":\"bad\",\"stages\":[{\"name\":\"a\",\"kind\":\"job\",\"job\":{\"step\":\"s\",\"tool\":\"sh\"}}]}\n"; }
        const Result check = cliAll(sq, {"process", "check", bad.string()});
        CHECK(check.exitCode != 0 && contains(check.out, "stage 'a': job.tool 'sh' is not a registered stage tool"));
        CHECK(cliAll(sq, {"process", "add", "bad", "--file", bad.string()}).exitCode != 0);
        CHECK(!fs::exists(state / "processes" / "bad.json"));
        // a stage fewer, from the command line
        CHECK_EQ(cliAll(sq, {"process", "add", "no-int", "--without", "integrate"}).exitCode, 0);
        CHECK(!contains(sq.cli({"process", "show", "no-int"}).out, "\"integrate\""));
        const Result reader = cliAll(sq, {"process", "add", "broken", "--without", "build"});
        CHECK(reader.exitCode != 0 && contains(reader.out, "--without build") && contains(reader.out, "stage 'integrate': entry"));
        CHECK(contains(sq.cli({"process", "tools"}).out, "harness:integrate"));
    }

    // archive: refused while the daemon runs (its journal is open) ...
    const Result busy = cliAll(sq, {"archive"});
    CHECK(busy.exitCode != 0 && contains(busy.out, "the daemon is running"));
    CHECK(fs::exists(state / "journal"));
    // ... and a bad label is refused before anything is touched
    sq.stopDaemon();
    CHECK(cliAll(sq, {"archive", "--label", "../escape"}).exitCode == 2);
    CHECK(fs::exists(state / "journal"));
    // ... then it moves the whole state aside and leaves a new one with the owner's processes
    const Result done = cliAll(sq, {"archive", "--label", "run-v9"});
    CHECK_EQ(done.exitCode, 0);
    CHECK(contains(done.out, "archived to ") && contains(done.out, "your registered processes were kept"));
    fs::path archived;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(sq.home() / "archive", ec)) archived = e.path();
    CHECK(!archived.empty() && contains(archived.filename().string(), "-run-v9"));
    CHECK(fs::exists(archived / "state" / "journal") && fs::exists(archived / "state" / "run-info.json") && fs::exists(archived / "state" / "workflows" / "w-000001" / "report.md"));
    CHECK(fs::is_directory(state) && !fs::exists(state / "journal") && !fs::exists(state / "workflows") && !fs::exists(state / "run-info.json"));
    CHECK(fs::exists(state / "processes" / "two-jobs.json") && fs::exists(archived / "state" / "processes" / "two-jobs.json"));
    // the fresh home starts counting again, and still knows the owner's process
    sq.startDaemon();
    CHECK(!contains(sq.cli({"project", "status", "--json"}).out, "\"id\":\"w-"));
    const Result fresh = cliAll(sq, {"project", "submit", "--task", "custom", "--process", "two-jobs"});
    CHECK(fresh.exitCode == 0 && contains(fresh.out, "w-000001"));
    CHECK_EQ(sq.cli({"project", "wait", "w-000001", "--timeout", "60"}).exitCode, 0);
    sq.stopDaemon();
    // a second archive in the same home does not collide with the first; an empty state has nothing to archive
    CHECK_EQ(cliAll(sq, {"archive", "--label", "run-v9"}).exitCode, 0);
    int archives = 0;
    for (const auto& e : fs::directory_iterator(sq.home() / "archive", ec)) { (void)e; ++archives; }
    CHECK_EQ(archives, 2);
    fs::remove_all(state, ec);
    fs::create_directories(state, ec);
    const Result none = cliAll(sq, {"archive"});
    CHECK(none.exitCode == 0 && contains(none.out, "nothing to archive"));
    return finish();
}
