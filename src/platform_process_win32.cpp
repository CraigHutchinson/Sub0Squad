// Windows backend of platform_process.hpp: CreateProcessW with an explicit list of inherited handles (the Windows form of closefrom), a job object
// per new session so the whole tree can be ended, and a polled pipe for capture. Arguments are quoted by the rules of CommandLineToArgvW and the C
// runtime. A batch file (.cmd, .bat) is parsed by cmd.exe by other rules; do not pass untrusted arguments to one.
#include "sub0squad/platform_process.hpp"

#include "platform_win32_util.hpp"

#include <algorithm>
#include <thread>

namespace sub0squad::platform {

namespace {

using Clock = std::chrono::steady_clock;
using win32::Handle;

constexpr DWORD kTerminateCode = 128 + 15;   // what a shell reports for SIGTERM and SIGKILL, so callers see the same codes on both systems
constexpr DWORD kKillCode = 128 + 9;

/// One argument, quoted so CommandLineToArgvW and the C runtime read it back unchanged.
std::wstring quoted(const std::wstring& a)
{
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (auto it = a.begin();; ++it) {
        std::size_t slashes = 0;
        while (it != a.end() && *it == L'\\') { ++it; ++slashes; }
        if (it == a.end()) { out.append(slashes * 2, L'\\'); break; }
        if (*it == L'"') { out.append(slashes * 2 + 1, L'\\'); out.push_back(L'"'); }
        else { out.append(slashes, L'\\'); out.push_back(*it); }
    }
    out.push_back(L'"');
    return out;
}

/// How a process ended, from its exit code: a crash status of the system becomes the nearest signal, so callers that classify crashes keep working.
ExitStatus statusOf(DWORD code, int endedBy)
{
    if (endedBy != 0) return ExitStatus{-1, endedBy};
    if (code < 0xC0000000u) return ExitStatus{static_cast<int>(code), 0};
    switch (code) {
    case 0xC0000005u: return ExitStatus{-1, 11};   // access violation: SIGSEGV
    case 0xC000008Eu: case 0xC0000090u: case 0xC0000094u: return ExitStatus{-1, 8};   // divide by zero, float: SIGFPE
    case 0xC000013Au: return ExitStatus{-1, 2};    // Ctrl+C: SIGINT
    default: return ExitStatus{-1, 6};             // stack overrun, abort and the rest: SIGABRT
    }
}

/// An inheritable duplicate of `h` for the child, or none when `h` is none.
Handle inheritable(HANDLE h)
{
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return Handle{};
    HANDLE dup = nullptr;
    if (!::DuplicateHandle(::GetCurrentProcess(), h, ::GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) return Handle{};
    return Handle{dup};
}

std::expected<Child, PlatformError> start(const ProcessSpec& spec, HANDLE captureWrite, bool captureStderr)
{
    if (spec.argv.empty()) return std::unexpected(PlatformError{"start a process", ERROR_INVALID_PARAMETER, "cannot start a process: no program was given"});
    const std::string what = "start '" + spec.argv.front() + "'";
    std::error_code ec;
    if (!spec.cwd.empty() && !std::filesystem::is_directory(spec.cwd, ec))
        return std::unexpected(PlatformError{what, ERROR_DIRECTORY, "cannot " + what + ": the working directory " + spec.cwd.string() + " does not exist"});
    if (!spec.outputFile.empty() && captureWrite) return std::unexpected(PlatformError{what, ERROR_INVALID_PARAMETER, "cannot " + what + ": both an output file and capture"});

    std::wstring command;
    for (const auto& a : spec.argv) command += (command.empty() ? L"" : L" ") + quoted(win32::widen(a));

    Handle nul, file;
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE), out = ::GetStdHandle(STD_OUTPUT_HANDLE), err = ::GetStdHandle(STD_ERROR_HANDLE);
    if (spec.nullInput) {
        nul.reset(::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        in = nul.get();
    }
    if (!spec.outputFile.empty()) {
        file.reset(::CreateFileW(spec.outputFile.c_str(), spec.appendOutput ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, spec.appendOutput ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file.valid()) return std::unexpected(win32::lastError("open " + spec.outputFile.string() + " for " + what));
        out = err = file.get();
    } else if (captureWrite) {
        out = captureWrite;
        if (captureStderr) err = captureWrite;
    }

    // Exactly these handles cross into the child, each once: stdin, stdout and stderr (which may be one handle).
    Handle cin = inheritable(in), cout = inheritable(out), cerr = err == out ? Handle{} : inheritable(err);
    std::vector<HANDLE> inherit;
    for (const Handle* h : {&cin, &cout, &cerr})
        if (h->valid()) inherit.push_back(h->get());

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = cin.get();
    si.StartupInfo.hStdOutput = cout.get();
    si.StartupInfo.hStdError = err == out ? cout.get() : cerr.get();
    std::vector<char> attributeBuffer;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (!inherit.empty()) {
        SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attributeBuffer.resize(size);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
        if (!::InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size) ||
            !::UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(), inherit.size() * sizeof(HANDLE), nullptr, nullptr))
            return std::unexpected(win32::lastError("prepare the handles for " + what));
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    if (spec.newSession) flags |= CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW | CREATE_SUSPENDED;   // suspended until it is in its job

    PROCESS_INFORMATION pi{};
    const wchar_t* cwd = spec.cwd.empty() ? nullptr : spec.cwd.c_str();
    // A new session tries to leave the caller's job first, so it outlives a caller whose job ends its members on close.
    BOOL ok = FALSE;
    DWORD failure = 0;
    for (const DWORD extra : spec.newSession ? std::vector<DWORD>{CREATE_BREAKAWAY_FROM_JOB, 0} : std::vector<DWORD>{0}) {
        ok = ::CreateProcessW(nullptr, command.data(), nullptr, nullptr, inherit.empty() ? FALSE : TRUE, flags | extra, nullptr, cwd, &si.StartupInfo, &pi);
        failure = ok ? 0 : ::GetLastError();
        if (ok || failure != ERROR_ACCESS_DENIED) break;
    }
    if (si.lpAttributeList) ::DeleteProcThreadAttributeList(si.lpAttributeList);
    if (!ok) return std::unexpected(win32::lastError(what, failure));
    Handle thread{pi.hThread};
    Handle job;
    if (spec.newSession) {
        job.reset(::CreateJobObjectW(nullptr, nullptr));
        if (job.valid() && !::AssignProcessToJobObject(job.get(), pi.hProcess)) job.reset();   // no job: terminate() then ends the process alone
        ::ResumeThread(pi.hThread);
    }
    return Child{static_cast<ProcessId>(pi.dwProcessId), spec.newSession, reinterpret_cast<std::intptr_t>(pi.hProcess), reinterpret_cast<std::intptr_t>(job.release())};
}

void closeNative(std::intptr_t& h)
{
    if (h != 0) ::CloseHandle(reinterpret_cast<HANDLE>(h));
    h = 0;
}

}  // namespace

Child::Child(Child&& o) noexcept : id_(o.id_), leadsGroup_(o.leadsGroup_), status_(o.status_), handle_(o.handle_), group_(o.group_), endedBy_(o.endedBy_)
{
    o.id_ = 0;
    o.handle_ = 0;
    o.group_ = 0;
}

Child& Child::operator=(Child&& o) noexcept
{
    if (this != &o) {
        closeNative(handle_);
        closeNative(group_);
        id_ = o.id_;
        leadsGroup_ = o.leadsGroup_;
        status_ = o.status_;
        handle_ = o.handle_;
        group_ = o.group_;
        endedBy_ = o.endedBy_;
        o.id_ = 0;
        o.handle_ = 0;
        o.group_ = 0;
    }
    return *this;
}

Child::~Child()
{
    closeNative(handle_);
    closeNative(group_);   // no kill-on-close limit: the tree of a new session lives on, as on POSIX
}

std::optional<ExitStatus> Child::wait(std::optional<std::chrono::milliseconds> timeout)
{
    if (status_ || handle_ == 0) return status_;
    const DWORD ms = timeout ? static_cast<DWORD>(std::clamp<long long>(timeout->count(), 0, INFINITE - 1)) : INFINITE;
    const DWORD r = ::WaitForSingleObject(reinterpret_cast<HANDLE>(handle_), ms);
    if (r == WAIT_TIMEOUT) return std::nullopt;
    DWORD code = 0;
    if (r != WAIT_OBJECT_0 || !::GetExitCodeProcess(reinterpret_cast<HANDLE>(handle_), &code)) return status_ = ExitStatus{};
    return status_ = statusOf(code, endedBy_);
}

bool Child::terminate()
{
    // Not graceful: Windows has no SIGTERM for a process without a console of ours. A graceful stop is a request over the daemon's channel
    // (docs/PLATFORM.md section 6, decided with the shim in slice 5).
    if (status_ || handle_ == 0) return false;
    const bool ok = group_ != 0 ? ::TerminateJobObject(reinterpret_cast<HANDLE>(group_), kTerminateCode) : ::TerminateProcess(reinterpret_cast<HANDLE>(handle_), kTerminateCode);
    if (ok) endedBy_ = 15;
    return ok;
}

bool Child::kill()
{
    if (status_ || handle_ == 0) return false;
    const bool ok = group_ != 0 ? ::TerminateJobObject(reinterpret_cast<HANDLE>(group_), kKillCode) : ::TerminateProcess(reinterpret_cast<HANDLE>(handle_), kKillCode);
    if (ok) endedBy_ = 9;
    return ok;
}

std::expected<Child, PlatformError> spawn(const ProcessSpec& spec) { return start(spec, nullptr, false); }

std::expected<Captured, PlatformError> runCaptured(const ProcessSpec& spec, const CaptureOptions& options)
{
    if (!spec.outputFile.empty()) return std::unexpected(PlatformError{"capture output", ERROR_INVALID_PARAMETER, "cannot capture output: the process spec also names an output file"});
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!::CreatePipe(&readEnd, &writeEnd, nullptr, 0)) return std::unexpected(win32::lastError("create a pipe"));
    Handle reader{readEnd};
    auto child = start(spec, writeEnd, options.includeStderr);
    ::CloseHandle(writeEnd);   // the child holds its own copy: the pipe breaks when every writer is gone
    if (!child) return std::unexpected(child.error());

    Captured c;
    const auto deadline = options.timeout ? std::optional{Clock::now() + *options.timeout} : std::nullopt;
    const auto keep = [&] {
        if (c.output.size() <= options.maxBytes) return;
        c.output.erase(0, c.output.size() - options.maxBytes);
        c.truncated = true;
    };
    char buf[8192];
    auto pause = std::chrono::milliseconds(1);
    bool childDone = false;
    for (;;) {
        DWORD available = 0;
        if (!::PeekNamedPipe(reader.get(), nullptr, 0, nullptr, &available, nullptr)) break;   // broken: every writer closed it
        if (available > 0) {
            DWORD n = 0;
            if (!::ReadFile(reader.get(), buf, std::min<DWORD>(available, sizeof buf), &n, nullptr) || n == 0) break;
            c.output.append(buf, n);
            if (c.output.size() > 2 * options.maxBytes) keep();   // trim in large steps, not on every read
            pause = std::chrono::milliseconds(1);
            continue;
        }
        // Nothing to read. Unlike a POSIX pipe, a grandchild that inherited the pipe would hold it open after the child ends; the child's end is the end.
        if (childDone) break;
        if (deadline && Clock::now() >= *deadline) { c.timedOut = true; break; }
        if (child->wait(std::chrono::milliseconds(0))) { childDone = true; continue; }   // one more look for what it wrote last
        std::this_thread::sleep_for(pause);
        pause = std::min(pause * 2, std::chrono::milliseconds(20));
    }
    keep();
    if (!c.timedOut && deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now());
        if (auto s = child->wait(std::max(left, std::chrono::milliseconds(0)))) { c.status = *s; return c; }
        c.timedOut = true;
    }
    if (c.timedOut) child->kill();
    c.status = child->wait().value_or(ExitStatus{});
    return c;
}

}  // namespace sub0squad::platform
