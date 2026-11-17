// The POSIX implementation of platform_process.hpp: posix_spawnp for every child (glibc reports a failed exec as the spawn's own error), a pipe
// and poll for capture, waitpid for the end. A Win32 sibling (CreateProcessW and a Job object per group) is the plan in docs/PLATFORM.md.
#include "sub0squad/platform_process.hpp"
#include "sub0squad/platform_file.hpp"

#include "platform_posix_util.hpp"
#include "platform_util.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <thread>

extern char** environ;

namespace sub0squad::platform {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

PlatformError errorOf(std::string operation, int code) { return posix::sysError(std::move(operation), code); }
ExitStatus statusOf(int raw) { return posix::exitStatusOf(raw); }

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

/// One pipe, both ends close-on-exec, the write end lifted to 10 or above so a dup2 onto 1, 2 or 3 never meets itself (a dup2 onto the same
/// number would keep close-on-exec). Closes what it holds.
struct ChildPipe
{
    int read = -1, write = -1;
    ChildPipe() = default;
    ChildPipe(const ChildPipe&) = delete;
    ChildPipe& operator=(const ChildPipe&) = delete;
    ~ChildPipe() { closeWrite(); if (read >= 0) ::close(read); }
    bool open()
    {
        int fds[2];
        if (::pipe2(fds, O_CLOEXEC) != 0) return false;
        read = fds[0];
        write = ::fcntl(fds[1], F_DUPFD_CLOEXEC, 10);
        ::close(fds[1]);
        return write >= 0;
    }
    void closeWrite() { if (write >= 0) ::close(write); write = -1; }
    Pipe takeRead() { const int r = read; read = -1; return r >= 0 ? Pipe{r} : Pipe{}; }
};

/// Starts `spec`; when `captureFd` is a descriptor, stdout (and stderr when `captureStderr`) are connected to it instead of spec.outputFile.
std::expected<Child, PlatformError> start(const ProcessSpec& spec, int captureFd, bool captureStderr, bool discardStderr = false)
{
    if (spec.argv.empty() || spec.argv.front().empty()) return std::unexpected(PlatformError{"start a process", EINVAL, "cannot start a process: no program was named"});
    const std::string program = spec.argv.front();
    const std::string what = "start '" + program + "'" + (spec.cwd.empty() ? std::string{} : " in " + spec.cwd.string());
    if (spec.pipeOutput && (captureFd >= 0 || !spec.outputFile.empty()))
        return std::unexpected(PlatformError{what, EINVAL, "cannot " + what + ": piped output with an output file or capture"});
    ChildPipe outPipe, errPipe, eventPipe;
    if (spec.pipeOutput && (!outPipe.open() || !errPipe.open())) return std::unexpected(errorOf("create the output pipes for " + what, errno));
    if (!spec.eventPipeVariable.empty() && !eventPipe.open()) return std::unexpected(errorOf("create the event pipe for " + what, errno));
    SpawnSetup s;
    if (!spec.cwd.empty() && posix_spawn_file_actions_addchdir_np(&s.actions, spec.cwd.c_str()) != 0) return std::unexpected(errorOf(what, errno));
    if (spec.nullInput) posix_spawn_file_actions_addopen(&s.actions, 0, "/dev/null", O_RDONLY, 0);
    if (captureFd >= 0) {
        posix_spawn_file_actions_adddup2(&s.actions, captureFd, 1);
        if (captureStderr) posix_spawn_file_actions_adddup2(&s.actions, captureFd, 2);
        else if (discardStderr) posix_spawn_file_actions_addopen(&s.actions, 2, "/dev/null", O_WRONLY, 0);
    }
    else if (spec.pipeOutput) {
        posix_spawn_file_actions_adddup2(&s.actions, outPipe.write, 1);
        posix_spawn_file_actions_adddup2(&s.actions, errPipe.write, 2);
    }
    else if (!spec.outputFile.empty()) {
        const int flags = O_WRONLY | O_CREAT | (spec.appendOutput ? O_APPEND : O_TRUNC);
        posix_spawn_file_actions_addopen(&s.actions, 1, spec.outputFile.c_str(), flags, static_cast<mode_t>(spec.outputPerms));
        posix_spawn_file_actions_adddup2(&s.actions, 1, 2);
    }
    // Only the standard streams cross (and the event pipe, as descriptor 3): no pipe, socket or lock of ours leaks into a child.
    if (eventPipe.write >= 0) posix_spawn_file_actions_adddup2(&s.actions, eventPipe.write, 3);
    posix_spawn_file_actions_addclosefrom_np(&s.actions, eventPipe.write >= 0 ? 4 : 3);

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
    std::vector<std::string> envStrings;
    if (spec.environment) envStrings = *spec.environment;
    else for (char** e = environ; *e; ++e) envStrings.emplace_back(*e);
    if (!spec.eventPipeVariable.empty()) {
        std::erase_if(envStrings, [&](const std::string& e) { return e.starts_with(spec.eventPipeVariable + "="); });
        envStrings.push_back(spec.eventPipeVariable + "=3");
    }
    std::vector<char*> envp;
    envp.reserve(envStrings.size() + 1);
    for (auto& e : envStrings) envp.push_back(e.data());
    envp.push_back(nullptr);
    pid_t pid = 0;
    if (const int rc = posix_spawnp(&pid, program.c_str(), &s.actions, &s.attr, argv.data(), envp.data()); rc != 0) return std::unexpected(errorOf(what, rc));
    // Raising oom_score_adj needs no privilege. Set from here, just after the start: posix_spawn runs no code of ours in the child.
    if (spec.memoryVictimScore > 0)
        if (const int fd = ::open(("/proc/" + std::to_string(pid) + "/oom_score_adj").c_str(), O_WRONLY | O_CLOEXEC); fd >= 0) {
            const std::string v = std::to_string(std::min(spec.memoryVictimScore, 1000));
            (void)!::write(fd, v.data(), v.size());
            ::close(fd);
        }
    Child child{pid, spec.newSession};
    outPipe.closeWrite();   // the child holds its own copies: each pipe ends when every writer is gone
    errPipe.closeWrite();
    eventPipe.closeWrite();
    child.attachPipes(outPipe.takeRead(), errPipe.takeRead(), eventPipe.takeRead());
    return child;
}

}  // namespace

Pipe& Pipe::operator=(Pipe&& o) noexcept
{
    if (this != &o) {
        if (native_ != -1) ::close(static_cast<int>(native_));
        native_ = o.native_;
        o.native_ = -1;
    }
    return *this;
}

Pipe::~Pipe()
{
    if (native_ != -1) ::close(static_cast<int>(native_));
}

std::expected<std::optional<std::size_t>, PlatformError> Pipe::readSome(char* buffer, std::size_t size, std::optional<std::chrono::milliseconds> timeout)
{
    if (native_ == -1) return std::unexpected(PlatformError{"read", EBADF, "cannot read: the pipe is not open"});
    const int fd = static_cast<int>(native_);
    const detail::Deadline deadline{timeout};
    for (;;) {
        pollfd p{fd, POLLIN, 0};
        const int ready = ::poll(&p, 1, static_cast<int>(deadline.msLeft()));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return std::unexpected(errorOf("wait on a pipe", errno));
        if (ready == 0) return std::optional<std::size_t>{};
        const ssize_t n = ::read(fd, buffer, size);
        if (n >= 0) return std::optional<std::size_t>{static_cast<std::size_t>(n)};
        if (errno != EINTR && errno != EAGAIN) return std::unexpected(errorOf("read a pipe", errno));
    }
}

std::expected<void, PlatformError> writeToInherited(std::string_view handleText, std::string_view data)
{
    int fd = -1;
    const auto [end, ec] = std::from_chars(handleText.data(), handleText.data() + handleText.size(), fd);
    if (ec != std::errc{} || end != handleText.data() + handleText.size() || fd < 0)
        return std::unexpected(PlatformError{"write to an inherited pipe", EBADF, "cannot write to an inherited pipe: '" + std::string{handleText} + "' names none"});
    return writeAll(fd, data);   // platform_file: the one loop that writes all of it, retrying interrupted writes
}

Child::Child(Child&& o) noexcept
    : id_(o.id_), leadsGroup_(o.leadsGroup_), status_(o.status_), handle_(o.handle_), group_(o.group_), endedBy_(o.endedBy_), stdout_(std::move(o.stdout_)),
      stderr_(std::move(o.stderr_)), events_(std::move(o.events_))
{
    o.id_ = 0;
}

Child::~Child() = default;

Child& Child::operator=(Child&& o) noexcept
{
    if (this != &o) {
        id_ = o.id_;
        leadsGroup_ = o.leadsGroup_;
        status_ = o.status_;
        handle_ = o.handle_;   // unused on POSIX: a pid is the whole handle
        group_ = o.group_;
        endedBy_ = o.endedBy_;
        stdout_ = std::move(o.stdout_);
        stderr_ = std::move(o.stderr_);
        events_ = std::move(o.events_);
        o.id_ = 0;
    }
    return *this;
}

void Child::attachPipes(Pipe out, Pipe err, Pipe events)
{
    stdout_ = std::move(out);
    stderr_ = std::move(err);
    events_ = std::move(events);
}

void Child::reapOrphans()
{
    int raw = 0;
    pid_t p;
    while ((p = ::waitpid(-1, &raw, WNOHANG)) > 0 || (p < 0 && errno == EINTR))
        if (p == static_cast<pid_t>(id_) && !status_) status_ = statusOf(raw);
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
    if (id_ <= 0 || (status_ && !leadsGroup_)) return false;   // a group may outlive its reaped leader
    return ::kill(static_cast<pid_t>(leadsGroup_ ? -id_ : id_), SIGTERM) == 0;
}

bool Child::kill()
{
    if (id_ <= 0 || (status_ && !leadsGroup_)) return false;   // a group may outlive its reaped leader
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
    auto child = start(spec, fds[1], options.includeStderr, options.discardStderr);
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
