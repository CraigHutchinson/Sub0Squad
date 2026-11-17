// POSIX backend of platform_wait.hpp: one epoll set holding the watched sockets, a pidfd per watched process, an eventfd for wake() and a
// signalfd for SIGTERM and SIGINT (blocked in the calling thread, and in every thread it starts afterwards).
#include "sub0squad/platform_wait.hpp"

#include "platform_posix_util.hpp"
#include "platform_util.hpp"

#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>

namespace sub0squad::platform {

namespace {

using posix::sysError;

}  // namespace

struct Waiter::State
{
    int epoll = -1;
    int wakeFd = -1;
    int signalFd = -1;
    std::uint64_t stopTag = 0;
    std::map<int, std::uint64_t> sockets;                         ///< fd -> tag
    std::map<int, std::pair<ProcessId, std::uint64_t>> exits;     ///< pidfd -> (pid, tag)

    ~State()
    {
        for (const auto& [fd, watched] : exits) ::close(fd);
        for (int fd : {signalFd, wakeFd, epoll}) if (fd >= 0) ::close(fd);
    }
    bool add(int fd, std::uint32_t events)
    {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        return ::epoll_ctl(epoll, EPOLL_CTL_ADD, fd, &ev) == 0;
    }
};

Waiter::Waiter() = default;
Waiter::Waiter(std::unique_ptr<State> state) : state_(std::move(state)) {}
Waiter::Waiter(Waiter&&) noexcept = default;
Waiter& Waiter::operator=(Waiter&&) noexcept = default;
Waiter::~Waiter() = default;

std::expected<Waiter, PlatformError> Waiter::create()
{
    auto s = std::make_unique<State>();
    s->epoll = ::epoll_create1(EPOLL_CLOEXEC);
    if (s->epoll < 0) return std::unexpected(sysError("create an epoll set"));
    s->wakeFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (s->wakeFd < 0 || !s->add(s->wakeFd, EPOLLIN)) return std::unexpected(sysError("create the wake-up event"));
    return Waiter{std::move(s)};
}

std::expected<void, PlatformError> Waiter::watchSocket(NativeSocket socket, bool read, bool write, std::uint64_t tag)
{
    const int fd = static_cast<int>(socket);
    epoll_event ev{};
    ev.events = (read ? EPOLLIN : 0u) | (write ? EPOLLOUT : 0u);
    ev.data.fd = fd;
    const bool known = state_->sockets.contains(fd);
    if (::epoll_ctl(state_->epoll, known ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, fd, &ev) != 0) return std::unexpected(sysError("watch a socket"));
    state_->sockets[fd] = tag;
    return {};
}

void Waiter::unwatchSocket(NativeSocket socket)
{
    const int fd = static_cast<int>(socket);
    if (state_->sockets.erase(fd)) ::epoll_ctl(state_->epoll, EPOLL_CTL_DEL, fd, nullptr);
}

bool Waiter::watchExit(ProcessId id, std::uint64_t tag)
{
    const int fd = static_cast<int>(::syscall(SYS_pidfd_open, static_cast<pid_t>(id), 0));
    if (fd < 0) return false;
    if (!state_->add(fd, EPOLLIN)) { ::close(fd); return false; }
    state_->exits[fd] = {id, tag};
    return true;
}

void Waiter::unwatchExit(ProcessId id)
{
    for (auto it = state_->exits.begin(); it != state_->exits.end(); ++it)
        if (it->second.first == id) {
            ::epoll_ctl(state_->epoll, EPOLL_CTL_DEL, it->first, nullptr);
            ::close(it->first);
            state_->exits.erase(it);
            return;
        }
}

std::expected<void, PlatformError> Waiter::watchStopRequests(std::uint64_t tag)
{
    state_->stopTag = tag;
    if (state_->signalFd >= 0) return {};
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (const int rc = ::pthread_sigmask(SIG_BLOCK, &mask, nullptr); rc != 0) return std::unexpected(sysError("block the stop signals", rc));
    state_->signalFd = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (state_->signalFd < 0 || !state_->add(state_->signalFd, EPOLLIN)) return std::unexpected(sysError("watch the stop signals"));
    return {};
}

void Waiter::wake()
{
    const std::uint64_t one = 1;
    (void)!::write(state_->wakeFd, &one, sizeof one);
}

std::expected<std::vector<WaitEvent>, PlatformError> Waiter::wait(std::optional<std::chrono::milliseconds> timeout)
{
    const detail::Deadline deadline{timeout};
    epoll_event evs[64];
    int n = 0;
    for (;;) {
        n = ::epoll_wait(state_->epoll, evs, 64, static_cast<int>(deadline.msLeft()));
        if (n >= 0) break;
        if (errno != EINTR) return std::unexpected(sysError("wait for events"));
    }
    std::vector<WaitEvent> out;
    for (int i = 0; i < n; ++i) {
        const int fd = evs[i].data.fd;
        const std::uint32_t e = evs[i].events;
        if (fd == state_->wakeFd) {
            std::uint64_t count;
            (void)!::read(fd, &count, sizeof count);
            out.push_back(WaitEvent{.kind = WaitEvent::Kind::woken});
        } else if (fd == state_->signalFd) {
            signalfd_siginfo si;
            while (::read(fd, &si, sizeof si) == static_cast<ssize_t>(sizeof si)) {}
            out.push_back(WaitEvent{.kind = WaitEvent::Kind::stopRequested, .tag = state_->stopTag});
        } else if (auto s = state_->sockets.find(fd); s != state_->sockets.end()) {
            out.push_back(WaitEvent{.kind = WaitEvent::Kind::socket, .tag = s->second, .readable = (e & EPOLLIN) != 0, .writable = (e & EPOLLOUT) != 0,
                                    .hangup = (e & (EPOLLHUP | EPOLLERR)) != 0});
        } else if (auto x = state_->exits.find(fd); x != state_->exits.end()) {
            const auto [pid, tag] = x->second;
            ::epoll_ctl(state_->epoll, EPOLL_CTL_DEL, fd, nullptr);
            ::close(fd);
            state_->exits.erase(x);
            WaitEvent ev{.kind = WaitEvent::Kind::exited, .tag = tag};
            int raw = 0;
            if (::waitpid(static_cast<pid_t>(pid), &raw, WNOHANG) == static_cast<pid_t>(pid))   // our child: reaped, and how it ended is known
                ev.status = posix::exitStatusOf(raw);
            out.push_back(ev);
        }
    }
    return out;
}

}  // namespace sub0squad::platform
