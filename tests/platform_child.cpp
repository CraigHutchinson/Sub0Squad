// The child the platform tests start (test_platform_process, test_platform_proc): one program that behaves the same on every system, in place of
// /bin/sh one-liners. Its output is binary (no CRLF on Windows) and flushed at each step, so the order of stdout and stderr is the order written.
//
//   exit N                 exit with code N
//   crash                  end as a crash does (POSIX: SIGKILL; Windows: an access-violation status)
//   out TEXT...            each TEXT on its own line, to stdout
//   outerr OUT ERR         OUT to stdout, then ERR to stderr
//   lines N                line-0 .. line-(N-1), then LAST
//   say TEXT MS            TEXT to stdout, then sleep MS milliseconds
//   sleep MS               sleep MS milliseconds
//   pwd                    the working directory, no line break
//   env NAME               the variable's value, no line break
//   held HANDLE PATH       "leaked" if the descriptor (POSIX) or handle (Windows) HANDLE is open here and is the file PATH, else "closed"
//   tree PIDFILE SELF      start SELF sleep 30000 (a grandchild), write its id to PIDFILE, wait for it
//   event VAR TEXT         TEXT and a line break to the inherited event pipe the variable VAR names
//   stoppable MS           watch for stop requests, say "ready", then exit 0 on a request ("stopped" to stdout) or 1 after MS milliseconds
#include "sub0squad/platform_process.hpp"
#include "sub0squad/platform_wait.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

void put(std::FILE* f, std::string_view s)
{
    std::fwrite(s.data(), 1, s.size(), f);
    std::fflush(f);
}

void sleepMs(const char* ms) { std::this_thread::sleep_for(std::chrono::milliseconds(std::atoll(ms))); }

bool heldOpen(const char* value, const fs::path& path)
{
    std::error_code ec;
#if defined(_WIN32)
    const auto h = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(std::atoll(value)));
    wchar_t buf[1024];
    const DWORD n = ::GetFinalPathNameByHandleW(h, buf, 1024, FILE_NAME_NORMALIZED);
    if (n == 0 || n >= 1024) return false;
    std::wstring name(buf, n);
    if (name.starts_with(L"\\\\?\\")) name.erase(0, 4);
    return fs::equivalent(fs::path(name), path, ec);
#else
    const int fd = std::atoi(value);
    if (::fcntl(fd, F_GETFD) == -1) return false;
    return fs::equivalent(fs::read_symlink("/proc/self/fd/" + std::to_string(fd), ec), path, ec);
#endif
}

}  // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    ::_setmode(::_fileno(stdout), _O_BINARY);
    ::_setmode(::_fileno(stderr), _O_BINARY);
#endif
    if (argc < 2) return 64;
    const std::string_view mode = argv[1];
    if (mode == "exit" && argc == 3) return std::atoi(argv[2]);
    if (mode == "crash") {
#if defined(_WIN32)
        std::fflush(nullptr);
        ::TerminateProcess(::GetCurrentProcess(), 0xC0000005u);   // what an access violation leaves as the exit code
#else
        std::raise(SIGKILL);
#endif
        return 70;
    }
    if (mode == "out") { for (int i = 2; i < argc; ++i) put(stdout, std::string(argv[i]) + "\n"); return 0; }
    if (mode == "outerr" && argc == 4) { put(stdout, std::string(argv[2]) + "\n"); put(stderr, std::string(argv[3]) + "\n"); return 0; }
    if (mode == "lines" && argc == 3) {
        std::string all;
        for (int i = 0, n = std::atoi(argv[2]); i < n; ++i) all += "line-" + std::to_string(i) + "\n";
        put(stdout, all + "LAST\n");
        return 0;
    }
    if (mode == "say" && argc == 4) { put(stdout, std::string(argv[2]) + "\n"); sleepMs(argv[3]); return 0; }
    if (mode == "sleep" && argc == 3) { sleepMs(argv[2]); return 0; }
    if (mode == "pwd") { put(stdout, fs::current_path().string()); return 0; }
    if (mode == "env" && argc == 3) { const char* v = std::getenv(argv[2]); put(stdout, v ? v : ""); return 0; }
    if (mode == "held" && argc == 4) { put(stdout, heldOpen(argv[2], argv[3]) ? "leaked" : "closed"); return 0; }
    if (mode == "tree" && argc == 4) {
        auto c = sub0squad::platform::spawn(sub0squad::platform::ProcessSpec{.argv = {argv[3], "sleep", "30000"}});
        if (!c) { put(stderr, c.error().message + "\n"); return 71; }
        {
            std::ofstream(fs::path(argv[2]).concat(".part"), std::ios::binary) << c->id();
        }
        fs::rename(fs::path(argv[2]).concat(".part"), argv[2]);   // the reader never sees half a number
        (void)c->wait();
        return 0;
    }
    if (mode == "event" && argc == 4) {
        const char* v = std::getenv(argv[2]);
        if (!v) return 72;
        return sub0squad::platform::writeToInherited(v, std::string(argv[3]) + "\n") ? 0 : 73;
    }
    if (mode == "stoppable" && argc == 3) {
        auto w = sub0squad::platform::Waiter::create();
        if (!w || !w->watchStopRequests(7)) return 74;
        put(stdout, "ready\n");
        auto events = w->wait(std::chrono::milliseconds(std::atoll(argv[2])));
        if (events && !events->empty() && events->front().kind == sub0squad::platform::WaitEvent::Kind::stopRequested && events->front().tag == 7) {
            put(stdout, "stopped\n");
            return 0;
        }
        return 1;
    }
    put(stderr, "platform_child: unknown mode\n");
    return 64;
}
