// The README's "First use" section cannot rot: this test RUNS the commands printed there, in order, exactly as written (the word `sub0squad` becomes the
// built binary), against a private home and a free portal port, with the mock agent (no model). Every command must succeed, the project the README
// promises must exist where it says, and nothing may be left running. argv[1] is the binary, argv[2] the README.
#include "check.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern char** environ;
namespace fs = std::filesystem;

struct Result { int exitCode = -1; std::string out; };

static Result run(std::vector<std::string> argv)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) return {};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 2);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    posix_spawn_file_actions_addclose(&fa, pipefd[1]);
    std::vector<char*> cargv;
    for (auto& a : argv) cargv.push_back(a.data());
    cargv.push_back(nullptr);
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, argv[0].c_str(), &fa, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[1]);
    Result r;
    if (rc != 0) { close(pipefd[0]); return r; }
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof buf)) > 0) r.out.append(buf, static_cast<std::size_t>(n));
    close(pipefd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    r.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

/// Words of a shell-like line; double quotes group, a `#` outside quotes starts a comment.
static std::vector<std::string> words(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, have = false;
    for (const char c : line) {
        if (c == '"') { quoted = !quoted; have = true; continue; }
        if (!quoted && c == '#') break;
        if (!quoted && (c == ' ' || c == '\t')) { if (have) out.push_back(cur); cur.clear(); have = false; continue; }
        cur.push_back(c);
        have = true;
    }
    if (have) out.push_back(cur);
    return out;
}

static int freePort()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    socklen_t len = sizeof a;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    ::close(fd);
    return ntohs(a.sin_port);
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: test_readme <sub0squad> <README.md>\n"); return 2; }
    const std::string exe = fs::absolute(argv[1]).string();
    std::ifstream in(argv[2]);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string readme = ss.str();
    const auto begin = readme.find("<!-- first-use:begin -->"), end = readme.find("<!-- first-use:end -->");
    CHECK(begin != std::string::npos && end != std::string::npos && begin < end);
    if (gFailures) return finish();

    // The commands: every line of the section's code blocks that starts with `sub0squad`.
    std::vector<std::vector<std::string>> commands;
    {
        std::istringstream lines(readme.substr(begin, end - begin));
        bool code = false;
        for (std::string line; std::getline(lines, line);) {
            if (line.rfind("```", 0) == 0) { code = !code; continue; }
            if (!code) continue;
            auto w = words(line);
            if (w.empty() || w[0] != "sub0squad") continue;
            w[0] = exe;
            commands.push_back(std::move(w));
        }
    }
    CHECK(commands.size() >= 6);   // doctor, up, do, wait, status, down

    const fs::path home = makeTempDir("readme");
    ::setenv("SUB0SQUAD_HOME", home.c_str(), 1);
    ::setenv("SUB0SQUAD_PORTAL_PORT", std::to_string(freePort()).c_str(), 1);   // never the default port: a real portal may be using it
    bool sawDo = false, sawUp = false, sawDown = false, sawDoctor = false;
    for (const auto& c : commands) {
        const Result r = run(c);
        std::string shown;
        for (std::size_t i = 1; i < c.size(); ++i) shown += " " + c[i];
        if (r.exitCode != 0) std::fprintf(stderr, "README command failed (exit %d): sub0squad%s\n%s\n", r.exitCode, shown.c_str(), r.out.c_str());
        CHECK_EQ(r.exitCode, 0);
        if (c[1] == "doctor") { sawDoctor = true; CHECK(r.out.find("FIX") == std::string::npos); }
        if (c[1] == "up") { sawUp = true; CHECK(r.out.find("daemon: started") != std::string::npos && r.out.find("portal: http://127.0.0.1:") != std::string::npos); }
        if (c[1] == "do") { sawDo = true; CHECK(r.out.find("workflow w-000001") != std::string::npos && r.out.find("awaiting a plan") == std::string::npos); }   // the blueprint loaded itself
        if (c[1] == "down") { sawDown = true; CHECK(r.out.find("daemon: stopped") != std::string::npos && r.out.find("portal: stopped") != std::string::npos); }
        if (c[1] == "project" && c.size() > 2 && c[2] == "status") CHECK(r.out.find("succeeded") != std::string::npos);
    }
    CHECK(sawDoctor && sawUp && sawDo && sawDown);
    // What the README says is there, is there.
    const fs::path wf = home / "state" / "workflows" / "w-000001";
    CHECK(fs::exists(wf / "report.md"));
    CHECK(fs::exists(wf / "project" / "build" / "calc_cli"));
    CHECK(readme.find("state/workflows/w-000001/project") != std::string::npos && readme.find("report.md") != std::string::npos);
    // Nothing is left running, whatever happened above.
    (void)run({exe, "down"});
    CHECK(run({exe, "ping"}).exitCode != 0);
    return finish();
}
