// The POSIX implementation of platform_process.hpp: posix_spawnp for every child (glibc reports a failed exec as the spawn's own error), a pipe
// and poll for capture, waitpid for the end. A Win32 sibling (CreateProcessW and a Job object per group) is the plan in docs/PLATFORM.md.
#include "sub0squad/platform_process.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <thread>

extern char** environ;

namespace sub0squad::platform {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

PlatformError errorOf(std::string operation, int code)
{
    PlatformError e;
    e.code = code;
    e.message = "cannot " + operation + ": " + std::strerror(code);
    e.operation = std::move(operation);
    return e;
}

ExitStatus statusOf(int raw)
{
    ExitStatus s;
    if (WIFEXITED(raw)) s.code = WEXITSTATUS(raw);
    else if (WIFSIGNALED(raw)) s.signal = WTERMSIG(raw);
    return s;
}

/// Owns the posix_spawn attribute objects for one start.
struct SpawnSetup
{
    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attr{};
    SpawnSetup() { posix_spawn_file_actions_init(&actions); posix_spawnattr_init(&attr); }
    ~SpawnSetup() { posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attr); }
    SpawnSetup(const SpawnSetup&) = delete;
    SpawnSetup& operator=(const SpawnSetup&) = delete;
};

/// Starts `spec`; when `captureFd` is a descriptor, stdout (and stderr when `captureStderr`) are connected to it instead of spec.outputFile.
std::expected<Child, PlatformError> start(const ProcessSpec& spec, int captureFd, bool captureStderr)
{
    if (spec.argv.empty() || spec.argv.front().empty()) return std::unexpected(PlatformError{"start a process", EINVAL, "cannot start a process: no program was named"});
    const std::string program = spec.argv.front();
    const std::string what = "start '" + program + "'" + (spec.cwd.empty() ? std::string{} : " in " + spec.cwd.string());
    SpawnSetup s;
    if (!spec.cwd.empty() && posix_spawn_file_actions_addchdir_np(&s.actions, spec.cwd.c_str()) != 0) return std::unexpected(errorOf(what, errno));
    if (spec.nullInput) posix_spawn_file_actions_addopen(&s.actions, 0, "/dev/null", O_RDONLY, 0);
    if (captureFd >= 0) {
        posix_spawn_file_actions_adddup2(&s.actions, captureFd, 1);
        if (captureStderr) posix_spawn_file_actions_adddup2(&s.actions, captureFd, 2);
    }
    else if (!spec.outputFile.empty()) {
        const int flags = O_WRONLY | O_CREAT | (spec.appendOutput ? O_APPEND : O_TRUNC);
        posix_spawn_file_actions_addopen(&s.actions, 1, spec.outputFile.c_str(), flags, static_cast<mode_t>(spec.outputPerms));
        posix_spawn_file_actions_adddup2(&s.actions, 1, 2);
    }
    posix_spawn_file_actions_addclosefrom_np(&s.actions, 3);   // only the standard streams cross: no pipe, socket or lock of ours leaks into a child

    // A clean start: no blocked signals (the daemon blocks some for its own loop) and default SIGPIPE (an ignored one survives exec).
    sigset_t none, defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigmask(&s.attr, &none);
    posix_spawnattr_setsigdefault(&s.attr, &defaults);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    if (spec.newSession) flags |= POSIX_SPAWN_SETSID;
    posix_spawnattr_setflags(&s.attr, flags);

    std::vector<char*> argv;
    argv.reserve(spec.argv.size() + 1);
    for (const auto& a : spec.argv) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (const int rc = posix_spawnp(&pid, program.c_str(), &s.actions, &s.attr, argv.data(), environ); rc != 0) return std::unexpected(errorOf(what, rc));
    return Child{pid, spec.newSession};
}

}  // namespace

Child& Child::operator=(Child&& o) noexcept
{
    if (this != &o) {
        id_ = o.id_;
        leadsGroup_ = o.leadsGroup_;
        status_ = o.status_;
        o.id_ = 0;
    }
    return *this;
}

std::optional<ExitStatus> Child::wait(std::optional<std::chrono::milliseconds> timeout)
{
    if (status_ || id_ <= 0) return status_;
    const pid_t pid = static_cast<pid_t>(id_);
    int raw = 0;
    if (!timeout) {
        pid_t r;
        while ((r = ::waitpid(pid, &raw, 0)) < 0 && errno == EINTR) {}
        status_ = r == pid ? statusOf(raw) : ExitStatus{};   // ECHILD: not ours or already reaped; how it ended is unknown
        return status_;
    }
    const auto deadline = Clock::now() + *timeout;
    auto pause = std::chrono::milliseconds(1);
    for (;;) {
        const pid_t r = ::waitpid(pid, &raw, WNOHANG);
        if (r == pid) return status_ = statusOf(raw);
        if (r < 0 && errno != EINTR) return status_ = ExitStatus{};
        if (Clock::now() >= deadline) return std::nullopt;
        std::this_thread::sleep_for(pause);
        pause = std::min(pause * 2, std::chrono::milliseconds(20));
    }
}

bool Child::terminate()
{
    if (status_ || id_ <= 0) return false;
    return ::kill(static_cast<pid_t>(leadsGroup_ ? -id_ : id_), SIGTERM) == 0;
}

bool Child::kill()
{
    if (status_ || id_ <= 0) return false;
    return ::kill(static_cast<pid_t>(leadsGroup_ ? -id_ : id_), SIGKILL) == 0;
}

std::expected<Child, PlatformError> spawn(const ProcessSpec& spec)
{
    return start(spec, -1, false);
}

std::expected<Captured, PlatformError> runCaptured(const ProcessSpec& spec, const CaptureOptions& options)
{
    if (!spec.outputFile.empty()) return std::unexpected(PlatformError{"capture output", EINVAL, "cannot capture output: the process spec also names an output file"});
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) return std::unexpected(errorOf("create a pipe", errno));   // close-on-exec: only the dup onto stdout crosses
    auto child = start(spec, fds[1], options.includeStderr);
    ::close(fds[1]);
    if (!child) { ::close(fds[0]); return std::unexpected(child.error()); }

    Captured c;
    const auto deadline = options.timeout ? std::optional{Clock::now() + *options.timeout} : std::nullopt;
    const auto keep = [&] {
        if (c.output.size() <= options.maxBytes) return;
        c.output.erase(0, c.output.size() - options.maxBytes);
        c.truncated = true;
    };
    char buf[8192];
    for (;;) {
        int waitMs = -1;
        if (deadline) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now()).count();
            if (left <= 0) { c.timedOut = true; break; }
            waitMs = static_cast<int>(std::min<long long>(left, 1000 * 60));
        }
        pollfd p{fds[0], POLLIN, 0};
        const int ready = ::poll(&p, 1, waitMs);
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        const ssize_t n = ::read(fds[0], buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;   // every writer closed it: the child (and whatever it started) is done writing
        c.output.append(buf, static_cast<std::size_t>(n));
        if (c.output.size() > 2 * options.maxBytes) keep();   // trim in large steps, not on every read
    }
    ::close(fds[0]);
    keep();
    if (!c.timedOut && deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now());
        if (auto s = child->wait(std::max(left, std::chrono::milliseconds(0)))) { c.status = *s; return c; }
        c.timedOut = true;   // it closed its output but kept running past the deadline
    }
    if (c.timedOut) child->kill();
    c.status = child->wait().value_or(ExitStatus{});
    return c;
}

}  // namespace sub0squad::platform
