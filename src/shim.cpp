#include "sub0squad/shim.hpp"

#include "json_util.hpp"
#include "sub0squad/job_spec.hpp"
#include "sub0squad/platform_file.hpp"
#include "sub0squad/platform_proc.hpp"
#include "sub0squad/platform_wait.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace sub0squad {

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t kLogCapBytes = 64u * 1024u * 1024u;  // per stream; M1 caps, rotation comes later
constexpr std::size_t kMaxEventLine = 64u * 1024u;
constexpr std::uint64_t kChildExit = 1, kStop = 2;

/// The supervisor of one job run (docs/PLATFORM.md section 6): starts the job in a group of its own with its stdout, stderr and event channel piped,
/// copies each stream to its file on a thread of its own, waits for the job's end, a stop request (a cancel) or the wall time, then tears the whole
/// tree down and writes exit.json.
class Shim
{
public:
    explicit Shim(std::filesystem::path dir) : dir_(std::move(dir)) {}
    int run();

private:
    /// One piped stream and the file it is copied to. Written by its reader thread only.
    struct Stream
    {
        platform::Pipe pipe;
        platform::AppendFile file;
        bool events = false;
        std::size_t written = 0;
        bool capped = false;
        std::string partial;  // events only: an incomplete line
    };

    bool start();
    void loop();
    void readStream(Stream& s);
    void copy(Stream& s, const char* data, std::size_t n);
    void joinReaders();
    void teardown(std::int64_t graceSec);
    void writeExit();

    std::filesystem::path dir_;
    JobSpec spec_;
    platform::Waiter waiter_;
    platform::Child child_;
    Stream out_, err_, events_;
    std::vector<std::jthread> readers_;
    std::atomic<int> readersActive_{0};
    std::atomic<bool> stopReaders_{false};
    std::atomic<std::size_t> droppedEventLines_{0};
    std::optional<platform::ExitStatus> status_;
    std::string outcome_ = "exited";  // exited | cancelled | timeout | startFailed
    std::string teardown_ = "verified";
    std::vector<platform::ProcessId> survivors_;
    Clock::time_point began_ = Clock::now();
};

bool Shim::start()
{
    auto spec = JobSpec::fromJson(platform::readFileText(dir_ / "spec.json"));
    if (!spec) { std::fprintf(stderr, "exec-shim: bad spec: %s\n", spec.error().c_str()); outcome_ = "startFailed"; return false; }
    spec_ = std::move(*spec);

    platform::setEnvironment("SUB0SQUAD_IN_SHIM", "1");  // recursion guard for spawnShim (the job's environment is rebuilt below, so it is not inherited)
    platform::becomeSubreaper();
    // Stop requests are taken before any thread starts, so on POSIX every thread inherits SIGTERM and SIGINT blocked and they reach the waiter.
    auto waiter = platform::Waiter::create();
    if (!waiter || !waiter->watchStopRequests(kStop)) { std::fprintf(stderr, "exec-shim: cannot wait for the job\n"); outcome_ = "startFailed"; return false; }
    waiter_ = std::move(*waiter);

    auto openOut = [&](const char* name) {
        auto f = platform::AppendFile::open(dir_ / name);
        return f ? std::move(*f) : platform::AppendFile{};
    };
    out_.file = openOut("stdout.log");
    err_.file = openOut("stderr.log");
    events_.file = openOut("events.ndjson");
    events_.events = true;

    platform::ProcessSpec ps;
    ps.argv = spec_.argv;
    ps.cwd = spec_.cwd;
    ps.newSession = true;   // the job leads a group of its own, so terminate and kill reach all of it
    ps.pipeOutput = true;
    ps.eventPipeVariable = "SUB0SQUAD_EVENT_FD";
    // Make the job (and what it spawns) the kernel's preferred victim under memory pressure, ahead of the owner's session and the daemon. Servers
    // (engines, the biggest holders) are 900, other jobs 600.
    ps.memoryVictimScore = spec_.service ? 900 : 600;
    std::vector<std::string> env;
    // A small fixed environment, then the job's own. The Windows names are what a program there needs to start at all; none is set on Linux.
    for (const char* key : {"PATH", "HOME", "LANG", "SYSTEMROOT", "WINDIR", "TEMP", "TMP", "USERPROFILE", "LOCALAPPDATA", "APPDATA", "PATHEXT", "COMSPEC"})
        if (const char* v = std::getenv(key)) env.push_back(std::string{key} + "=" + v);
    // Measurement switches, set on the squad's environment to compare one build both ways (docs/LAYERED_DOCUMENT.md): passed through, or the A/B
    // silently measures the default twice.
    for (const char* key : {"SUB0SQUAD_ISSUE_LAYER", "SUB0SQUAD_ISSUE_INLINE", "SUB0SQUAD_FIXSITE", "SUB0SQUAD_THINKPLAN", "SUB0SQUAD_FOLDS"})
        if (const char* v = std::getenv(key)) env.push_back(std::string{key} + "=" + v);
    env.push_back("SUB0SQUAD_RUN_DIR=" + dir_.string());
    for (const auto& [k, v] : spec_.env) env.push_back(k + "=" + v);
    ps.environment = std::move(env);

    auto child = platform::spawn(ps);
    if (!child) {
        // As a shell reports it: 127 when the program is not there, 126 when it cannot be run (or its working directory is missing). The reason goes
        // to stderr.log, where the job's own complaint would have been.
        const std::string reason = "sub0squad: " + child.error().message + "\n";
        if (err_.file.isOpen()) (void)err_.file.append(reason);
        std::error_code ec;
        const bool missing = spec_.argv.empty() || (std::filesystem::is_directory(spec_.cwd.empty() ? "." : spec_.cwd, ec) &&
                                                    platform::findOnPath(spec_.argv.front()).empty());
        status_ = platform::ExitStatus{missing ? 127 : 126, 0};
        return false;
    }
    child_ = std::move(*child);
    out_.pipe = child_.takeStdout();
    err_.pipe = child_.takeStderr();
    events_.pipe = child_.takeEvents();
    for (Stream* s : {&events_, &out_, &err_}) {
        ++readersActive_;
        readers_.emplace_back([this, s] { readStream(*s); --readersActive_; });
    }

    const auto ticks = platform::processStart(platform::currentProcess());
    json::Writer w;
    w.beginObject().kv("schema", "shim-1").kv("pid", platform::currentProcess()).kv("startTicks", static_cast<std::int64_t>(ticks.value_or(0)))
        .kv("child", child_.id()).endObject();
    (void)platform::writeFileAtomic(dir_ / "shim.json", w.take(), true);
    return true;
}

void Shim::copy(Stream& s, const char* data, std::size_t n)
{
    if (s.events) {
        s.partial.append(data, n);
        std::size_t nl;
        while ((nl = s.partial.find('\n')) != std::string::npos) {
            const std::string_view line{s.partial.data(), nl};
            if (!line.empty() && line.front() == '{' && line.size() <= kMaxEventLine) {
                std::string out{line};
                out.push_back('\n');
                (void)s.file.append(out);
            } else if (!line.empty()) {
                ++droppedEventLines_;
            }
            s.partial.erase(0, nl + 1);
        }
        if (s.partial.size() > kMaxEventLine) { s.partial.clear(); ++droppedEventLines_; }
        return;
    }
    std::size_t take = n;
    if (s.written + take > kLogCapBytes) {
        take = kLogCapBytes > s.written ? kLogCapBytes - s.written : 0;
        if (!s.capped) {
            (void)s.file.append("\n[sub0squad: log capped at 64 MiB; further output discarded]\n");
            s.capped = true;
        }
    }
    if (take) (void)s.file.append(std::string_view{data, take});
    s.written += take;
}

void Shim::readStream(Stream& s)
{
    // Until every writer has closed the pipe (the job and whatever inherited it), or the shim gives up on a survivor that still holds it.
    std::string buf(65536, '\0');
    while (s.pipe.isOpen()) {
        auto n = s.pipe.readSome(buf.data(), buf.size(), std::chrono::milliseconds(100));
        if (!n || (*n && **n == 0)) break;
        if (!*n) { if (stopReaders_) break; continue; }
        if (s.file.isOpen()) copy(s, buf.data(), **n);
    }
}

void Shim::joinReaders()
{
    const auto deadline = Clock::now() + std::chrono::seconds(2);   // what the job wrote last, once its writers are gone
    while (readersActive_ > 0 && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stopReaders_ = true;
    readers_.clear();   // joins
}

void Shim::teardown(std::int64_t graceSec)
{
    const platform::ProcessId self = platform::currentProcess();
    auto live = [&] { child_.reapOrphans(); return platform::descendants(self); };
    auto sendAll = [&](bool hard) {
        (void)(hard ? child_.kill() : child_.terminate());   // the job's own group first
        for (const platform::ProcessId d : platform::descendants(self))   // escapees that left the group
            (void)(hard ? platform::killProcess(d) : platform::terminateProcess(d));
    };
    if (!live().empty()) {
        sendAll(false);
        const auto deadline = Clock::now() + std::chrono::seconds(graceSec);
        while (Clock::now() < deadline && !live().empty()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto hard = Clock::now() + std::chrono::seconds(10);
    while (Clock::now() < hard) {
        if (live().empty()) break;
        sendAll(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    child_.reapOrphans();
    survivors_ = platform::descendants(self);
    std::erase_if(survivors_, [](platform::ProcessId p) { return !platform::processStart(p); });
    teardown_ = survivors_.empty() ? "verified" : "survivors";
}

void Shim::loop()
{
    const bool watched = waiter_.watchExit(child_.id(), kChildExit);
    const auto wall = spec_.wallSeconds > 0 ? std::optional{began_ + std::chrono::seconds(spec_.wallSeconds)} : std::nullopt;
    bool ended = !watched;   // not watched: it is already gone
    while (!ended) {
        std::optional<std::chrono::milliseconds> left;
        if (wall) left = std::max(std::chrono::milliseconds(0), std::chrono::duration_cast<std::chrono::milliseconds>(*wall - Clock::now()));
        auto events = waiter_.wait(left);
        if (!events) break;
        if (events->empty() && wall && Clock::now() >= *wall) { outcome_ = "timeout"; break; }
        bool cancel = false;
        for (const auto& e : *events) {
            if (e.kind == platform::WaitEvent::Kind::exited) { ended = true; status_ = e.status; }
            else if (e.kind == platform::WaitEvent::Kind::stopRequested) cancel = true;
        }
        if (ended) break;  // the main command ended first: the outcome stays "exited"
        if (cancel) { outcome_ = "cancelled"; break; }
    }
    teardown(outcome_ == "exited" ? std::min<std::int64_t>(spec_.graceSeconds, 2) : spec_.graceSeconds);
    if (!status_) status_ = child_.wait(std::chrono::milliseconds(0));
    joinReaders();
}

void Shim::writeExit()
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began_).count();
    json::Writer w;
    w.beginObject().kv("schema", "exit-1").kv("outcome", outcome_);
    if (status_ && status_->exited()) w.kv("code", status_->code);
    if (status_ && status_->signal != 0) w.kv("signal", status_->signal);
    w.kv("teardown", teardown_);
    w.key("survivors").beginArray();
    for (const platform::ProcessId p : survivors_) w.num(static_cast<std::int64_t>(p));
    w.endArray();
    w.kv("durationMs", static_cast<std::int64_t>(ms)).kv("eventsDropped", static_cast<std::int64_t>(droppedEventLines_.load()));
    // What the job itself reports about its outcome (`result.json` in its run directory: a small JSON object, e.g. a gate's mend): carried in the exit
    // record, so it reaches the journal with the job's end and the workflow engine can read it without touching a file.
    if (auto text = platform::readFile(dir_ / "result.json")) {
        std::string result = std::move(*text);
        while (!result.empty() && (result.back() == '\n' || result.back() == ' ')) result.pop_back();
        bool object = false;
        if (result.size() <= 8192) { try { json::withObject(result, [&](simdjson::ondemand::object& obj) { for (auto f : obj) (void)f.value().raw_json(); object = true; }); } catch (const simdjson::simdjson_error&) { object = false; } }
        if (object) w.kvRaw("result", result);
    }
    w.endObject();
    (void)platform::writeFileAtomic(dir_ / "exit.json", w.take(), true);
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
