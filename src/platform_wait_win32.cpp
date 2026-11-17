// Windows backend of platform_wait.hpp. Every watched socket signals one shared event (WSAEventSelect); each wait first resets it and asks WSAPoll
// for the sockets' state as it is now (WSAEventSelect alone reports a change once, epoll reports a state for as long as it lasts), then waits
// in WaitForMultipleObjects on that event, the wake-up event, the stop event and the watched processes. The stop event is named after this
// process (win32::stopEventName), so terminateProcess in another process can set it; Ctrl+C and Ctrl+Break on a console set it too.
#include "sub0squad/platform_wait.hpp"

#include "platform_win32_util.hpp"
#include "platform_util.hpp"

#include <algorithm>
#include <atomic>
#include <map>

namespace sub0squad::platform {

namespace {

using win32::Handle;

constexpr std::size_t kMaxProcesses = MAXIMUM_WAIT_OBJECTS - 4;   // the socket, wake and stop events, and one spare

std::atomic<HANDLE> gConsoleStop{nullptr};   // the stop event the console handler sets

BOOL WINAPI onConsoleControl(DWORD type)
{
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT && type != CTRL_CLOSE_EVENT) return FALSE;
    if (const HANDLE h = gConsoleStop.load()) ::SetEvent(h);
    return TRUE;
}

}  // namespace

struct Waiter::State
{
    struct Watched
    {
        bool read = false, write = false;
        std::uint64_t tag = 0;
    };
    struct Process
    {
        ProcessId id = 0;
        Handle handle;
        std::uint64_t tag = 0;
    };
    Handle socketEvent;   ///< manual reset: any watched socket's network event
    Handle wakeEvent;     ///< auto reset
    Handle stopEvent;     ///< auto reset, named; none until watchStopRequests
    std::uint64_t stopTag = 0;
    std::map<SOCKET, Watched> sockets;
    std::vector<Process> processes;

    ~State()
    {
        for (const auto& [s, w] : sockets) ::WSAEventSelect(s, nullptr, 0);
        if (stopEvent.valid()) {
            HANDLE expected = stopEvent.get();
            gConsoleStop.compare_exchange_strong(expected, nullptr);
        }
    }
};

Waiter::Waiter() = default;
Waiter::Waiter(std::unique_ptr<State> state) : state_(std::move(state)) {}
Waiter::Waiter(Waiter&&) noexcept = default;
Waiter& Waiter::operator=(Waiter&&) noexcept = default;
Waiter::~Waiter() = default;

std::expected<Waiter, PlatformError> Waiter::create()
{
    if (!win32::winsockReady()) return std::unexpected(win32::socketError("start Winsock"));
    auto s = std::make_unique<State>();
    s->socketEvent.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    s->wakeEvent.reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!s->socketEvent.valid() || !s->wakeEvent.valid()) return std::unexpected(win32::lastError("create the events to wait on"));
    return Waiter{std::move(s)};
}

std::expected<void, PlatformError> Waiter::watchSocket(NativeSocket socket, bool read, bool write, std::uint64_t tag)
{
    const auto s = static_cast<SOCKET>(socket);
    const long events = FD_CLOSE | (read ? FD_READ | FD_ACCEPT : 0) | (write ? FD_WRITE : 0);
    if (::WSAEventSelect(s, state_->socketEvent.get(), events) != 0) return std::unexpected(win32::socketError("watch a socket"));
    state_->sockets[s] = State::Watched{read, write, tag};
    return {};
}

void Waiter::unwatchSocket(NativeSocket socket)
{
    const auto s = static_cast<SOCKET>(socket);
    if (state_->sockets.erase(s)) ::WSAEventSelect(s, nullptr, 0);   // the socket stays non-blocking, as every socket of the library is
}

bool Waiter::watchExit(ProcessId id, std::uint64_t tag)
{
    if (id <= 0 || state_->processes.size() >= kMaxProcesses) return false;
    Handle h{::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(id))};
    if (!h.valid()) return false;
    state_->processes.push_back(State::Process{id, std::move(h), tag});
    return true;
}

void Waiter::unwatchExit(ProcessId id)
{
    std::erase_if(state_->processes, [&](const State::Process& p) { return p.id == id; });
}

std::expected<void, PlatformError> Waiter::watchStopRequests(std::uint64_t tag)
{
    state_->stopTag = tag;
    if (state_->stopEvent.valid()) return {};
    state_->stopEvent.reset(::CreateEventW(nullptr, FALSE, FALSE, win32::stopEventName(static_cast<std::int64_t>(::GetCurrentProcessId())).c_str()));
    if (!state_->stopEvent.valid()) return std::unexpected(win32::lastError("create the stop event"));
    gConsoleStop.store(state_->stopEvent.get());
    ::SetConsoleCtrlHandler(onConsoleControl, TRUE);   // no console: no effect, and harmless
    return {};
}

void Waiter::wake() { ::SetEvent(state_->wakeEvent.get()); }

std::expected<std::vector<WaitEvent>, PlatformError> Waiter::wait(std::optional<std::chrono::milliseconds> timeout)
{
    const detail::Deadline deadline{timeout};
    std::vector<WaitEvent> out;
    for (;;) {
        // The sockets as they are now. The event is reset first, so a change after the look still ends the wait below.
        // Two looks, merged: WSAPoll (the state now) and the network events Winsock recorded for each socket since the last look. WSAPoll alone
        // missed a connection pending on an AF_UNIX listener on Windows Server 2022 (the GitHub runners): one that arrived before the first wait
        // had its only signal cleared by the reset, and Winsock records FD_ACCEPT again only after an accept, so the daemon never answered.
        if (!state_->sockets.empty()) {
            ::ResetEvent(state_->socketEvent.get());
            std::vector<WSAPOLLFD> fds;
            fds.reserve(state_->sockets.size());
            for (const auto& [s, w] : state_->sockets)
                fds.push_back(WSAPOLLFD{s, static_cast<SHORT>((w.read ? POLLRDNORM : 0) | (w.write ? POLLWRNORM : 0)), 0});
            if (::WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), 0) == SOCKET_ERROR) return std::unexpected(win32::socketError("look at the sockets"));
            for (const auto& f : fds) {
                const auto& w = state_->sockets[f.fd];
                WSANETWORKEVENTS recorded{};
                const long seen = ::WSAEnumNetworkEvents(f.fd, nullptr, &recorded) == 0 ? recorded.lNetworkEvents : 0;
                const bool readable = (f.revents & POLLRDNORM) != 0 || (w.read && (seen & (FD_READ | FD_ACCEPT | FD_CLOSE)) != 0);
                const bool writable = (f.revents & POLLWRNORM) != 0 || (w.write && (seen & FD_WRITE) != 0);
                const bool hangup = (f.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0 || (seen & FD_CLOSE) != 0;
                if (readable || writable || hangup)
                    out.push_back(WaitEvent{.kind = WaitEvent::Kind::socket, .tag = w.tag, .readable = readable, .writable = writable, .hangup = hangup});
            }
        }

        // The rare stop event first, then the socket and wake events, then the processes. WaitForMultipleObjects names the lowest signalled
        // index, so every handle before it was quiet at that moment: only it and the ones after it need a look (no heap, no probe per wake).
        // The socket event only while a socket is watched: each handle costs the wait, and nothing else sets it.
        HANDLE handles[MAXIMUM_WAIT_OBJECTS];
        DWORD count = 0;
        const DWORD stopAt = state_->stopEvent.valid() ? count++ : MAXIMUM_WAIT_OBJECTS;
        if (stopAt != MAXIMUM_WAIT_OBJECTS) handles[stopAt] = state_->stopEvent.get();
        if (!state_->sockets.empty()) handles[count++] = state_->socketEvent.get();
        const DWORD wakeAt = count;
        handles[count++] = state_->wakeEvent.get();
        const DWORD firstProcess = count;
        for (const auto& p : state_->processes) handles[count++] = p.handle.get();
        const DWORD ms = out.empty() ? static_cast<DWORD>(deadline.msLeft(INFINITE, INFINITE - 1)) : 0;
        const DWORD r = ::WaitForMultipleObjects(count, handles, FALSE, ms);
        if (r == WAIT_FAILED) return std::unexpected(win32::lastError("wait for events"));
        if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + count) {
            const DWORD first = r - WAIT_OBJECT_0;
            // Signalled or not, by the look (auto-reset events: the look consumes the signal, as the wait did for `first`).
            const auto signalled = [&](DWORD i) { return i == first || (i > first && ::WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0); };
            if (stopAt != MAXIMUM_WAIT_OBJECTS && signalled(stopAt)) out.push_back(WaitEvent{.kind = WaitEvent::Kind::stopRequested, .tag = state_->stopTag});
            if (signalled(wakeAt)) out.push_back(WaitEvent{.kind = WaitEvent::Kind::woken});
            std::size_t kept = 0;
            for (std::size_t i = 0; i < state_->processes.size(); ++i) {
                auto& proc = state_->processes[i];
                if (!signalled(firstProcess + static_cast<DWORD>(i))) { if (kept != i) state_->processes[kept] = std::move(proc); ++kept; continue; }
                WaitEvent ev{.kind = WaitEvent::Kind::exited, .tag = proc.tag};
                DWORD code = 0;
                if (::GetExitCodeProcess(proc.handle.get(), &code)) ev.status = win32::exitStatusOf(code);
                out.push_back(ev);
            }
            state_->processes.resize(kept);
        }
        if (!out.empty()) return out;
        // A timed-out wait is not proof the time passed: Windows rounds waits to its timer tick (about 15.6 ms) and can return a few
        // milliseconds early, so the deadline, not WAIT_TIMEOUT, decides, and the rest is waited for on the next turn.
        if (deadline.passed()) return out;
        // Only the socket event fired and no socket is ready now (its event was for a state already gone): look again.
    }
}

}  // namespace sub0squad::platform
