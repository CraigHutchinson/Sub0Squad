#include "sub0squad/shim.hpp"

#include "json_util.hpp"
#include "sub0squad/job_spec.hpp"
#include "sub0squad/process_tree.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

extern char** environ;

namespace sub0squad {

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t kLogCapBytes = 64u * 1024u * 1024u;  // per stream; M1 caps, rotation comes later
constexpr std::size_t kMaxEventLine = 64u * 1024u;

enum Tag : std::uint64_t { kEvents = 1, kOut, kErr, kSignals, kTimer };

class Shim
{
public:
    explicit Shim(std::string dir) : dir_(std::move(dir)) {}
    int run();

private:
    struct Stream
    {
        int fd = -1;       // read end
        int outFd = -1;    // destination file
        std::size_t written = 0;
        bool capped = false;
        std::string partial;  // events only: incomplete line
    };

    bool start();
    void loop();
    void drain(Stream& s, bool isEvents);
    void drainAll();
    void reapAll();
    void teardown(std::int64_t graceSec);
    void writeExit();
    void writeFileAtomic(const std::string& name, const std::string& text);
    void closeStream(Stream& s);

    std::string dir_;
    JobSpec spec_;
    int epfd_ = -1, sigfd_ = -1, timerfd_ = -1, eventsOut_ = -1;
    Stream events_, out_, err_;
    pid_t child_ = 0;
    bool childExited_ = false;
    int childStatus_ = 0;
    std::string outcome_ = "exited";  // exited | cancelled | timeout | startFailed
    std::string teardown_ = "verified";
    std::vector<pid_t> survivors_;
    std::size_t droppedEventLines_ = 0;
    Clock::time_point began_ = Clock::now();
    sigset_t origMask_{};
};

void Shim::writeFileAtomic(const std::string& name, const std::string& text)
{
    const std::string tmp = dir_ + "/" + name + ".tmp";
    const std::string dst = dir_ + "/" + name;
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return;
    const char* p = text.data();
    std::size_t left = text.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n < 0) { if (errno == EINTR) continue; break; }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    ::fsync(fd);
    ::close(fd);
    ::rename(tmp.c_str(), dst.c_str());
}

bool Shim::start()
{
    std::ifstream in(dir_ + "/spec.json");
    std::stringstream ss;
    ss << in.rdbuf();
    auto spec = JobSpec::fromJson(ss.str());
    if (!spec) { std::fprintf(stderr, "exec-shim: bad spec: %s\n", spec.error().c_str()); outcome_ = "startFailed"; return false; }
    spec_ = std::move(*spec);

    ::setenv("SUB0SQUAD_IN_SHIM", "1", 1);  // recursion guard for spawnShim (the job's environment is rebuilt below, so it is not inherited)
    ::prctl(PR_SET_CHILD_SUBREAPER, 1);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &origMask_);
    sigfd_ = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);

    auto openOut = [&](const char* name) { return ::open((dir_ + "/" + name).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600); };
    out_.outFd = openOut("stdout.log");
    err_.outFd = openOut("stderr.log");
    eventsOut_ = openOut("events.ndjson");

    int pe[2], po[2], pr[2];
    if (::pipe2(pe, O_CLOEXEC) || ::pipe2(po, O_CLOEXEC) || ::pipe2(pr, O_CLOEXEC)) { outcome_ = "startFailed"; return false; }

    std::vector<std::string> envStrings;
    for (const char* key : {"PATH", "HOME", "LANG"})
        if (const char* v = std::getenv(key)) envStrings.push_back(std::string{key} + "=" + v);
    envStrings.push_back("SUB0SQUAD_EVENT_FD=3");
    envStrings.push_back("SUB0SQUAD_RUN_DIR=" + dir_);
    for (const auto& [k, v] : spec_.env) envStrings.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& e : envStrings) envp.push_back(e.data());
    envp.push_back(nullptr);
    std::vector<char*> argv;
    for (auto& a : spec_.argv) argv.push_back(a.data());
    argv.push_back(nullptr);

    child_ = ::fork();
    if (child_ < 0) { outcome_ = "startFailed"; return false; }
    if (child_ == 0) {
        ::setpgid(0, 0);
        sigprocmask(SIG_SETMASK, &origMask_, nullptr);
        // Make the job (and what it spawns) the kernel's preferred victim under memory pressure, ahead of the owner's session and the daemon:
        // raising oom_score_adj needs no privilege. Servers (engines, the biggest holders) are 900, other jobs 600.
        if (const int fd = ::open("/proc/self/oom_score_adj", O_WRONLY); fd >= 0) { const char* v = spec_.service ? "900" : "600"; (void)!::write(fd, v, std::strlen(v)); ::close(fd); }
        const int devnull = ::open("/dev/null", O_RDONLY);
        // Move the three write ends to fixed numbers without clobbering one another: stdin=0, stdout=1, stderr=2, events=3.
        int ev = pe[1], so = po[1], se = pr[1];
        auto lift = [](int fd) { return ::fcntl(fd, F_DUPFD_CLOEXEC, 10); };
        ev = lift(ev); so = lift(so); se = lift(se);
        ::dup2(devnull, 0);
        ::dup2(so, 1);
        ::dup2(se, 2);
        ::dup2(ev, 3);  // dup2 clears CLOEXEC on the new descriptor
        for (int fd = 4; fd < 1024; ++fd) ::close(fd);
        if (!spec_.cwd.empty() && ::chdir(spec_.cwd.c_str()) != 0) _exit(126);
        environ = envp.data();
        ::execvp(argv[0], argv.data());
        _exit(127);
    }
    ::setpgid(child_, child_);  // also done in the child; whichever runs first wins
    ::close(pe[1]); ::close(po[1]); ::close(pr[1]);
    events_.fd = pe[0]; out_.fd = po[0]; err_.fd = pr[0];
    for (int fd : {events_.fd, out_.fd, err_.fd}) ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);

    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    auto add = [&](int fd, Tag t) { epoll_event ev{}; ev.events = EPOLLIN; ev.data.u64 = t; ::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev); };
    add(events_.fd, kEvents); add(out_.fd, kOut); add(err_.fd, kErr); add(sigfd_, kSignals);
    if (spec_.wallSeconds > 0) {
        timerfd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        itimerspec its{};
        its.it_value.tv_sec = spec_.wallSeconds;
        ::timerfd_settime(timerfd_, 0, &its, nullptr);
        add(timerfd_, kTimer);
    }

    const auto ticks = proc::startTicks(::getpid());
    json::Writer w;
    w.beginObject().kv("schema", "shim-1").kv("pid", static_cast<std::int64_t>(::getpid())).kv("startTicks", static_cast<std::int64_t>(ticks.value_or(0)))
        .kv("child", static_cast<std::int64_t>(child_)).endObject();
    writeFileAtomic("shim.json", w.take());
    return true;
}

void Shim::closeStream(Stream& s)
{
    if (s.fd >= 0) { ::epoll_ctl(epfd_, EPOLL_CTL_DEL, s.fd, nullptr); ::close(s.fd); s.fd = -1; }
}

void Shim::drain(Stream& s, bool isEvents)
{
    char buf[65536];
    while (s.fd >= 0) {
        const ssize_t n = ::read(s.fd, buf, sizeof buf);
        if (n > 0) {
            if (isEvents) {
                s.partial.append(buf, static_cast<std::size_t>(n));
                std::size_t nl;
                while ((nl = s.partial.find('\n')) != std::string::npos) {
                    const std::string_view line{s.partial.data(), nl};
                    if (!line.empty() && line.front() == '{' && line.size() <= kMaxEventLine) {
                        std::string out{line};
                        out.push_back('\n');
                        (void)!::write(eventsOut_, out.data(), out.size());
                    } else if (!line.empty()) {
                        ++droppedEventLines_;
                    }
                    s.partial.erase(0, nl + 1);
                }
                if (s.partial.size() > kMaxEventLine) { s.partial.clear(); ++droppedEventLines_; }
            } else {
                std::size_t take = static_cast<std::size_t>(n);
                if (s.written + take > kLogCapBytes) {
                    take = kLogCapBytes > s.written ? kLogCapBytes - s.written : 0;
                    if (!s.capped) {
                        static const char msg[] = "\n[sub0squad: log capped at 64 MiB; further output discarded]\n";
                        (void)!::write(s.outFd, msg, sizeof msg - 1);
                        s.capped = true;
                    }
                }
                if (take) (void)!::write(s.outFd, buf, take);
                s.written += take;
            }
        } else if (n == 0) {
            closeStream(s);
        } else {
            if (errno == EINTR) continue;
            break;  // EAGAIN
        }
    }
}

void Shim::drainAll() { drain(events_, true); drain(out_, false); drain(err_, false); }

void Shim::reapAll()
{
    int status = 0;
    pid_t p;
    while ((p = ::waitpid(-1, &status, WNOHANG)) > 0) {
        if (p == child_) { childExited_ = true; childStatus_ = status; }
    }
}

void Shim::teardown(std::int64_t graceSec)
{
    const pid_t self = ::getpid();
    auto live = [&] { reapAll(); return proc::descendantsOf(self); };
    auto sendAll = [&](int sig) {
        ::kill(-child_, sig);  // the job's own process group first
        for (pid_t d : proc::descendantsOf(self)) ::kill(d, sig);  // escapees that left the group
    };
    if (!live().empty()) {
        sendAll(SIGTERM);
        const auto deadline = Clock::now() + std::chrono::seconds(graceSec);
        while (Clock::now() < deadline && !live().empty()) { drainAll(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    }
    const auto hard = Clock::now() + std::chrono::seconds(10);
    while (Clock::now() < hard) {
        auto d = live();
        if (d.empty()) break;
        sendAll(SIGKILL);
        drainAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    reapAll();
    survivors_ = proc::descendantsOf(self);
    survivors_.erase(std::remove_if(survivors_.begin(), survivors_.end(), [](pid_t p) { return !proc::startTicks(p); }), survivors_.end());
    teardown_ = survivors_.empty() ? "verified" : "survivors";
}

void Shim::loop()
{
    while (!childExited_) {
        epoll_event evs[8];
        const int n = ::epoll_wait(epfd_, evs, 8, -1);
        if (n < 0) { if (errno == EINTR) continue; break; }
        bool cancel = false, timeout = false;
        for (int i = 0; i < n; ++i) {
            switch (evs[i].data.u64) {
                case kEvents: drain(events_, true); break;
                case kOut: drain(out_, false); break;
                case kErr: drain(err_, false); break;
                case kTimer: { std::uint64_t x; (void)!::read(timerfd_, &x, sizeof x); timeout = true; break; }
                case kSignals: {
                    signalfd_siginfo si;
                    while (::read(sigfd_, &si, sizeof si) == static_cast<ssize_t>(sizeof si)) {
                        if (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT) cancel = true;
                    }
                    reapAll();
                    break;
                }
            }
        }
        if (childExited_) break;  // the main command ended first: outcome stays "exited"
        if (cancel) { outcome_ = "cancelled"; break; }
        if (timeout) { outcome_ = "timeout"; break; }
    }
    drainAll();
    teardown(outcome_ == "exited" ? std::min<std::int64_t>(spec_.graceSeconds, 2) : spec_.graceSeconds);
    drainAll();
}

void Shim::writeExit()
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began_).count();
    json::Writer w;
    w.beginObject().kv("schema", "exit-1").kv("outcome", outcome_);
    if (childExited_ && WIFEXITED(childStatus_)) w.kv("code", WEXITSTATUS(childStatus_));
    if (childExited_ && WIFSIGNALED(childStatus_)) w.kv("signal", WTERMSIG(childStatus_));
    w.kv("teardown", teardown_);
    w.key("survivors").beginArray();
    for (pid_t p : survivors_) w.num(static_cast<std::int64_t>(p));
    w.endArray();
    w.kv("durationMs", static_cast<std::int64_t>(ms)).kv("eventsDropped", static_cast<std::int64_t>(droppedEventLines_));
    // What the job itself reports about its outcome (`result.json` in its run directory: a small JSON object, e.g. a gate's mend): carried in the exit
    // record, so it reaches the journal with the job's end and the workflow engine can read it without touching a file.
    {
        std::ifstream in(dir_ + "/result.json", std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string result = ss.str();
        while (!result.empty() && (result.back() == '\n' || result.back() == ' ')) result.pop_back();
        bool object = false;
        if (in && result.size() <= 8192) { try { json::withObject(result, [&](simdjson::ondemand::object& obj) { for (auto f : obj) (void)f.value().raw_json(); object = true; }); } catch (const simdjson::simdjson_error&) { object = false; } }
        if (object) w.kvRaw("result", result);
    }
    w.endObject();
    writeFileAtomic("exit.json", w.take());
}

int Shim::run()
{
    if (!start()) {
        teardown_ = "verified";
        writeExit();
        return 1;
    }
    loop();
    writeExit();
    return 0;
}

}  // namespace

int runShim(const std::string& runDir)
{
    Shim shim{runDir};
    return shim.run();
}

}  // namespace sub0squad
