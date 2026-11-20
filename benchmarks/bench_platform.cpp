// Platform-layer micro-benchmarks (docs/PLATFORM.md), run through Sub0Math's sub0perf tooling (docs/DEV_SETUP.md, "Tests and benchmarks").
// Each primitive is paired with a hand-written twin: the fewest direct system calls that do the same job on this OS, the fastest version we know.
// Naming follows sub0perf: the twin and the library call share the text before the comma and form a pair group, judged as a ratio.
// Part of Sub0Squad_Bench (one executable, so one baseline per host): bench_squad.cpp's main calls runPlatformBenchmarks.
#include "bench_platform.hpp"

#include "sub0platform/file.hpp"
#include "sub0platform/net.hpp"
#include "sub0platform/process.hpp"
#include "sub0platform/wait.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <poll.h>
#include <spawn.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <fcntl.h>
#endif

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>

#if !defined(_WIN32)
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace {

/// Read exactly `size` bytes from a raw socket (a loopback read can return part of a small write). The library's sockets are non-blocking, so
/// an empty read waits for data in poll, as any real reader must (giving up on EAGAIN would let the twin skip the wait it is measured against).
void recvAll(sub0platform::NativeSocket s, char* buf, std::size_t size)
{
    std::size_t got = 0;
#if defined(_WIN32)
    const auto sock = static_cast<SOCKET>(s);
    while (got < size) {
        const int n = ::recv(sock, buf + got, static_cast<int>(size - got), 0);
        if (n > 0) { got += static_cast<std::size_t>(n); continue; }
        if (n == 0 || ::WSAGetLastError() != WSAEWOULDBLOCK) return;
        WSAPOLLFD p{sock, POLLRDNORM, 0};
        (void)::WSAPoll(&p, 1, -1);
    }
#else
    const int fd = static_cast<int>(s);
    while (got < size) {
        const ssize_t n = ::recv(fd, buf + got, size - got, 0);
        if (n > 0) { got += static_cast<std::size_t>(n); continue; }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) return;
        if (errno == EINTR) continue;
        pollfd p{fd, POLLIN, 0};
        (void)::poll(&p, 1, -1);
    }
#endif
}

void sendAll(sub0platform::NativeSocket s, const char* buf, std::size_t size)
{
#if defined(_WIN32)
    (void)::send(static_cast<SOCKET>(s), buf, static_cast<int>(size), 0);
#else
    (void)::send(static_cast<int>(s), buf, size, MSG_NOSIGNAL);
#endif
}

}  // namespace

void runPlatformBenchmarks(sub0bench::BenchRunner& runner, const fs::path& self, const fs::path& root)
{
    fs::create_directories(root);

    // ---- wake one waiting thread: signal then wait, in one thread (the cost of a post to the daemon's reactor) ------------------------------
    {
#if defined(_WIN32)
        HANDLE ev = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        runner.run("waiter wake and wait, hand-written", [&] {
            ::SetEvent(ev);
            const DWORD r = ::WaitForMultipleObjects(1, &ev, FALSE, INFINITE);
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        ::CloseHandle(ev);
#else
        const int efd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        const int ep = ::epoll_create1(EPOLL_CLOEXEC);
        epoll_event reg{};
        reg.events = EPOLLIN;
        reg.data.fd = efd;
        ::epoll_ctl(ep, EPOLL_CTL_ADD, efd, &reg);
        runner.run("waiter wake and wait, hand-written", [&] {
            const std::uint64_t one = 1;
            (void)!::write(efd, &one, sizeof one);
            epoll_event ev[8];
            const int n = ::epoll_wait(ep, ev, 8, -1);
            std::uint64_t count;
            (void)!::read(efd, &count, sizeof count);
            ankerl::nanobench::doNotOptimizeAway(n);
        });
        ::close(ep);
        ::close(efd);
#endif
        auto waiter = sub0platform::Waiter::create();
        runner.run("waiter wake and wait, Waiter", [&] {
            waiter->wake();
            auto events = waiter->wait();
            ankerl::nanobench::doNotOptimizeAway(events);
        });
    }

    // ---- 64 bytes over a loopback TCP connection, written on one end and read on the other ------------------------------------------------
    {
        auto listener = sub0platform::listenLoopback(0);
        auto client = sub0platform::connectTcp("127.0.0.1", listener->port());
        auto accepted = listener->accept(std::chrono::seconds{5});
        sub0platform::SocketStream server = std::move(**accepted);
        char out[64];
        std::memset(out, 'x', sizeof out);
        char in[64];
        runner.bench().minEpochIterations(20000);
        runner.run("loopback 64 B write and read, hand-written", [&] {
            sendAll(client->native(), out, sizeof out);
            recvAll(server.native(), in, sizeof in);
            ankerl::nanobench::doNotOptimizeAway(in);
        });
        runner.run("loopback 64 B write and read, SocketStream", [&] {
            (void)client->writeAll(std::string_view{out, sizeof out});
            std::size_t got = 0;
            while (got < sizeof in) {
                auto n = server.readSome(in + got, sizeof in - got);
                if (!n || !*n || **n == 0) break;
                got += **n;
            }
            ankerl::nanobench::doNotOptimizeAway(in);
        });
        runner.bench().minEpochIterations(10);
    }

    // ---- replace a 4 KiB file atomically (temporary file and rename, not flushed to the device) ---------------------------------------------
    {
        const std::string text(4096, 'a');
        const fs::path target = root / "atomic.txt";
        const fs::path temp = root / "atomic.txt.tmp";
        runner.bench().minEpochIterations(200);
#if defined(_WIN32)
        runner.run("atomic replace 4 KiB, hand-written", [&] {
            HANDLE h = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD written = 0;
            ::WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            ::CloseHandle(h);
            ::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
        });
#else
        runner.run("atomic replace 4 KiB, hand-written", [&] {
            const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            (void)!::write(fd, text.data(), text.size());
            ::close(fd);
            ::rename(temp.c_str(), target.c_str());
        });
#endif
        runner.run("atomic replace 4 KiB, writeFileAtomic()", [&] { auto r = sub0platform::writeFileAtomic(target, text); ankerl::nanobench::doNotOptimizeAway(r); });
        runner.bench().minEpochIterations(10);
    }

    // ---- start a child that exits at once, and wait for it ---------------------------------------------------------------------------------
    {
        runner.bench().epochs(5).minEpochIterations(60);
#if defined(_WIN32)
        std::wstring commandLine = L"\"" + self.wstring() + L"\" " + fs::path{kBenchExitNow}.wstring();
        runner.run("spawn and wait, hand-written", [&] {
            STARTUPINFOW si{};
            si.cb = sizeof si;
            PROCESS_INFORMATION pi{};
            std::wstring line = commandLine;
            if (::CreateProcessW(self.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {   // the caller's console: CREATE_NO_WINDOW would start a console host per child
                ::WaitForSingleObject(pi.hProcess, INFINITE);
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
            }
        });
#else
        const std::string path = self.string();
        runner.run("spawn and wait, hand-written", [&] {
            char* args[] = {const_cast<char*>(path.c_str()), const_cast<char*>(kBenchExitNow), nullptr};
            pid_t pid = 0;
            if (::posix_spawn(&pid, path.c_str(), nullptr, nullptr, args, environ) == 0) {
                int status = 0;
                ::waitpid(pid, &status, 0);
            }
        });
#endif
        const sub0platform::ProcessSpec spec{.argv = {self.string(), kBenchExitNow}};
        runner.run("spawn and wait, sub0platform::spawn()", [&] {
            auto child = sub0platform::spawn(spec);
            if (child) { auto st = child->wait(); ankerl::nanobench::doNotOptimizeAway(st); }
        });
        runner.bench().epochs(11).minEpochIterations(10);
    }

}
