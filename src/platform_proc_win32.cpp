// Windows backend of platform_proc.hpp: process handles for identity, a Toolhelp32 snapshot for the tree, the process's own command line (read
// through ntdll, which every process has loaded) for its arguments. The parsers of /proc text are in platform_proc.cpp.
#include "sub0squad/platform_proc.hpp"

#include "platform_win32_util.hpp"

#include <fcntl.h>
#include <io.h>
#include <tlhelp32.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace sub0squad::platform {

namespace {

using win32::Handle;

Handle openProcess(ProcessId id, DWORD access = PROCESS_QUERY_LIMITED_INFORMATION)
{
    if (id <= 0) return Handle{};
    return Handle{::OpenProcess(access, FALSE, static_cast<DWORD>(id))};
}

/// Creation time of an open process, in 100 ns ticks since 1601.
std::optional<std::uint64_t> created(HANDLE h)
{
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!::GetProcessTimes(h, &creation, &exit, &kernel, &user)) return std::nullopt;
    return win32::ticks(creation);
}

bool running(HANDLE h)
{
    return ::WaitForSingleObject(h, 0) == WAIT_TIMEOUT;   // signalled once it has exited, whatever its exit code
}

}  // namespace

std::filesystem::path selfExecutable()
{
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); return std::filesystem::path(buf); }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
}

ProcessId currentProcess() { return static_cast<ProcessId>(::GetCurrentProcessId()); }

std::optional<std::uint64_t> processStart(ProcessId id)
{
    const Handle h = openProcess(id);
    if (!h.valid()) return std::nullopt;
    return created(h.get());
}

bool isAlive(ProcessId id, std::uint64_t expectedStart)
{
    const Handle h = openProcess(id, PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE);
    if (!h.valid()) return false;
    const auto start = created(h.get());
    return start && *start == expectedStart && running(h.get());
}

std::optional<ProcessUsage> processUsage(ProcessId id)
{
    const Handle h = openProcess(id);
    if (!h.valid()) return std::nullopt;
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!::GetProcessTimes(h.get(), &creation, &exit, &kernel, &user)) return std::nullopt;
    return ProcessUsage{static_cast<double>(win32::ticks(kernel) + win32::ticks(user)) / 1e7, std::nullopt};
}

std::vector<ProcessId> allProcesses()
{
    std::vector<ProcessId> out;
    const Handle snap{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (!snap.valid()) return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof e;
    for (BOOL ok = ::Process32FirstW(snap.get(), &e); ok; ok = ::Process32NextW(snap.get(), &e))
        if (e.th32ProcessID != 0) out.push_back(static_cast<ProcessId>(e.th32ProcessID));
    return out;
}

std::vector<ProcessId> descendants(ProcessId root)
{
    // Windows never reparents: an orphan keeps the id of its dead parent, which may since have been reused. A child is only counted when it
    // was created after the process that now holds its parent's id.
    std::map<ProcessId, std::vector<ProcessId>> children;
    const Handle snap{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (!snap.valid()) return {};
    PROCESSENTRY32W e{};
    e.dwSize = sizeof e;
    for (BOOL ok = ::Process32FirstW(snap.get(), &e); ok; ok = ::Process32NextW(snap.get(), &e))
        if (e.th32ProcessID != 0 && e.th32ProcessID != e.th32ParentProcessID)
            children[static_cast<ProcessId>(e.th32ParentProcessID)].push_back(static_cast<ProcessId>(e.th32ProcessID));

    std::map<ProcessId, std::optional<std::uint64_t>> starts;
    const auto startOf = [&](ProcessId p) -> std::optional<std::uint64_t> {
        auto [it, fresh] = starts.try_emplace(p);
        if (fresh) it->second = processStart(p);
        return it->second;
    };
    std::vector<ProcessId> out;
    std::set<ProcessId> seen{root};
    std::vector<ProcessId> work{root};
    while (!work.empty()) {
        const ProcessId p = work.back();
        work.pop_back();
        const auto it = children.find(p);
        if (it == children.end()) continue;
        const auto parentStart = startOf(p);
        for (ProcessId c : it->second) {
            const auto childStart = startOf(c);
            if (parentStart && childStart && *childStart < *parentStart) continue;   // the parent's id was reused: not its child
            if (!seen.insert(c).second) continue;
            out.push_back(c);
            work.push_back(c);
        }
    }
    return out;
}

std::optional<std::vector<std::string>> commandLine(ProcessId id)
{
    // NtQueryInformationProcess(ProcessCommandLineInformation = 60) answers a UNICODE_STRING followed by its text (Windows 8.1 and later).
    struct CountedText { USHORT length; USHORT maximum; PWSTR buffer; };
    using Query = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query = reinterpret_cast<Query>(reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    if (!query) return std::nullopt;
    const Handle h = openProcess(id);
    if (!h.valid()) return std::nullopt;
    ULONG size = 0;
    query(h.get(), 60, nullptr, 0, &size);
    if (size < sizeof(CountedText)) return std::nullopt;
    std::vector<unsigned char> buf(size);
    if (query(h.get(), 60, buf.data(), size, &size) < 0) return std::nullopt;
    const auto* text = reinterpret_cast<const CountedText*>(buf.data());
    const std::wstring line(text->buffer, text->length / sizeof(wchar_t));
    std::vector<std::string> args;
    if (line.empty()) return args;
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(line.c_str(), &argc);
    if (!argv) return std::nullopt;
    for (int i = 0; i < argc; ++i) args.push_back(win32::narrow(argv[i]));
    ::LocalFree(argv);
    return args;
}

bool terminateProcess(ProcessId id)
{
    // Windows has no SIGTERM. A process that watches for stop requests (platform_wait) made a named event: set it, and it ends itself in order.
    // Otherwise it ends at once, with the code a shell gives SIGTERM.
    if (id > 0) {
        const Handle stop{::OpenEventW(EVENT_MODIFY_STATE, FALSE, win32::stopEventName(id).c_str())};
        if (stop.valid() && ::SetEvent(stop.get())) return true;
    }
    const Handle h = openProcess(id, PROCESS_TERMINATE);
    return h.valid() && ::TerminateProcess(h.get(), 128 + 15);
}

bool killProcess(ProcessId id)
{
    const Handle h = openProcess(id, PROCESS_TERMINATE);
    return h.valid() && ::TerminateProcess(h.get(), 128 + 9);
}

void becomeSubreaper() {}      // a job object holds a tree on Windows (platform_process: a new session gets one)
void useBinaryStandardStreams()
{
    for (FILE* f : {stdin, stdout, stderr}) (void)::_setmode(::_fileno(f), _O_BINARY);
}

void ignoreBrokenPipes() {}    // a write to a broken pipe is already an error, never a signal
void ignoreStopRequests() { ::SetConsoleCtrlHandler(nullptr, TRUE); }   // Ctrl+C; terminateProcess without a stop event cannot be ignored
void setProcessName(const std::string&) {}
void crash()
{
    std::fflush(nullptr);
    ::TerminateProcess(::GetCurrentProcess(), 0xC0000005u);   // what an access violation leaves as the exit code
    std::abort();
}

namespace {

/// PATHEXT as lower-case extensions (".com;.exe;.bat;.cmd" when unset).
std::vector<std::string> pathExtensions()
{
    const char* v = std::getenv("PATHEXT");
    std::string all = v && *v ? v : ".COM;.EXE;.BAT;.CMD";
    for (auto& c : all) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= all.size()) {
        const auto semi = all.find(';', at);
        const std::string e = all.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
        if (!e.empty()) out.push_back(e);
        if (semi == std::string::npos) break;
        at = semi + 1;
    }
    return out;
}

}  // namespace

bool isExecutable(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    std::string ext = path.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto exts = pathExtensions();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::filesystem::path findOnPath(const std::string& name)
{
    const auto tryName = [](const std::filesystem::path& base) -> std::filesystem::path {
        if (isExecutable(base)) return base;
        if (base.has_extension()) return {};
        for (const auto& e : pathExtensions()) {
            std::filesystem::path p = base;
            p += e;
            if (isExecutable(p)) return p;
        }
        return {};
    };
    if (name.find_first_of("/\\") != std::string::npos) return tryName(std::filesystem::path(win32::widen(name)));
    const char* path = std::getenv("PATH");
    const std::string all = path ? path : "";
    std::size_t at = 0;
    while (at <= all.size()) {
        const auto semi = all.find(';', at);
        const std::string dir = all.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
        if (!dir.empty())
            if (auto p = tryName(std::filesystem::path(win32::widen(dir)) / win32::widen(name)); !p.empty()) return p;
        if (semi == std::string::npos) break;
        at = semi + 1;
    }
    return {};
}

void setEnvironment(const std::string& name, const std::string& value)
{
    // Both copies: the C runtime's (getenv) and the system's (inherited by CreateProcessW).
    ::_wputenv_s(win32::widen(name).c_str(), win32::widen(value).c_str());
    ::SetEnvironmentVariableW(win32::widen(name).c_str(), win32::widen(value).c_str());
}

void unsetEnvironment(const std::string& name)
{
    ::_wputenv_s(win32::widen(name).c_str(), L"");
    ::SetEnvironmentVariableW(win32::widen(name).c_str(), nullptr);
}

}  // namespace sub0squad::platform
