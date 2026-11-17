/// @file daemon.cpp
/// Daemon: reactor and IPC (a platform_wait loop, clients, request dispatch), construction and run.

#include "daemon_impl.hpp"
#include "sub0squad/platform_file.hpp"

namespace sub0squad {

void Daemon::Impl::post(std::move_only_function<void()> fn)
{
    { std::lock_guard lock(postMutex); posted.push_back(std::move(fn)); }
    waiter.wake();
}

bool Daemon::Impl::watchShim(const std::string& id, platform::ProcessId shim)
{
    const char* forcePoll = std::getenv("SUB0SQUAD_TEST_POLL_SHIMS");   // tests take the fallback on purpose
    if ((forcePoll && *forcePoll) || !waiter.watchExit(shim, tag(kShimExit, shim))) {
        const auto start = platform::processStart(shim);
        if (!start || !platform::isAlive(shim, *start)) return false;
        polledShims[shim] = *start;
        logf("job %s: shim %lld is watched by polling (the waiter could not take it)", id.c_str(), static_cast<long long>(shim));
    }
    shimToJob[shim] = id;
    return true;
}

void Daemon::Impl::checkPolledShims()
{
    std::vector<platform::ProcessId> gone;
    for (const auto& [shim, start] : polledShims)
        if (!platform::isAlive(shim, start)) gone.push_back(shim);
    for (const auto shim : gone) {
        polledShims.erase(shim);
        onShimExit(shim);
    }
}

void Daemon::Impl::notifyWaiters(const std::string& id)
{
    const JobRecord* job = store.find(id);
    if (!job) return;
    for (auto& [fd, c] : clients) {
        if (!c.waits.erase(id)) continue;
        json::Writer w;
        w.beginObject().kvBool("ok", true).kvRaw("job", jobJson(opts.paths, *job, reconcile(opts.paths, *job))).endObject();
        send(c, w.take());
    }
}

void Daemon::Impl::send(Client& c, const std::string& payload)
{
    c.out += encodeFrame(payload);
    flush(c);
}

void Daemon::Impl::flush(Client& c)
{
    while (!c.out.empty()) {
        auto n = c.stream.writeSome(c.out);
        if (!n) return;   // the peer is gone; the hang-up closes it
        if (*n == 0) break;
        c.out.erase(0, *n);
    }
    const bool wantWrite = !c.out.empty();   // ask to hear about room to write only while a reply waits
    if (wantWrite != c.watchingWrite) {
        c.watchingWrite = wantWrite;
        (void)waiter.watchSocket(c.stream.native(), true, wantWrite, tag(kClient, c.stream.native()));
    }
}

void Daemon::Impl::handle(Client& c, const std::string& text)
{
    auto req = parseRequest(text);
    if (!req) { send(c, errorReply(req.error())); return; }
    if (req->version != 1) { send(c, errorReply("protocol version " + std::to_string(req->version) + " not supported (daemon speaks 1)")); return; }
    const std::string& cmd = req->cmd;
    if (cmd == "ping") {
        // Pure: a ping reads and answers, it never changes anything (the watchers are run by `check`).
        json::Writer w;
        w.beginObject().kvBool("ok", true).kv("v", 1).kv("pid", platform::currentProcess()).kv("maxJobs", opts.maxJobs)
            .kvBool("journalOk", journalOk).kv("build", buildId()).endObject();
        send(c, w.take());
    } else if (cmd == "check") {
        // Run the watchers NOW (what the timer does every 1 to 5 seconds: the memory watcher's shed decision, then admission) and answer after them, so
        // their decisions can be observed without waiting on a clock (catalogue row 22: the shed test waited up to 12 s for a 5 s timer on a loaded host).
        const int before = activeCount();
        shedIfPressured();
        scheduleQueued();
        json::Writer w;
        w.beginObject().kvBool("ok", true).kv("activeBefore", before).kv("active", activeCount()).endObject();
        send(c, w.take());
    } else if ((cmd == "submit" || cmd == "workflowSubmit") && !req->build.empty() && req->build != buildId() && !req->allowBuildMismatch) {
        // Row 20: the daemon is still the binary it was started from. Work submitted by a newer (or older) command line would run on this one and be read as that one's.
        send(c, errorReply("build mismatch: this command is build '" + req->build + "' and the running daemon is build '" + buildId() +
                           "'. Restart the daemon so both are the same program (`sub0squad down`, then `sub0squad up`), or pass --allow-build-mismatch to submit anyway."));
    } else if (cmd == "submit") {
        if (!journalOk) { send(c, errorReply("journal is failing: not accepting new work")); return; }
        auto spec = JobSpec::fromJson(req->specJson);
        if (!spec) { send(c, errorReply(spec.error())); return; }
        spec->owner = c.owner;
        spec->workflow.clear(); spec->item.clear(); spec->role.clear(); spec->attempt = 0;  // clients cannot forge workflow labels
        if (spec->memoryMiB > 0) {  // the explicit memory guard: refuse now, with the numbers, rather than start and thrash
            if (auto ok = requireMemory(spec->memoryMiB); !ok) { send(c, errorReply(ok.error())); return; }
        }
        if (spec->tier == "frontier-stub") { send(c, errorReply("tier frontier-stub is reserved for workflows")); return; }
        const std::string id = store.nextId();
        record("jobSubmitted", journalbody::submitted(id, *spec));
        scheduleQueued();
        json::Writer w;
        w.beginObject().kvBool("ok", true).kv("id", id).endObject();
        send(c, w.take());
    } else if (cmd == "status") {
        send(c, statusJson(opts.paths, store, req->id, "live"));
    } else if (cmd == "cancel") {
        send(c, doCancel(req->id));
    } else if (cmd == "resume") {
        send(c, doResume(req->id));
    } else if (cmd == "wait") {
        const JobRecord* job = store.find(req->id);
        if (!job) { send(c, errorReply("no such job: " + req->id)); return; }
        const Effective eff = reconcile(opts.paths, *job);
        if (isTerminal(eff.state)) {
            json::Writer w;
            w.beginObject().kvBool("ok", true).kvRaw("job", jobJson(opts.paths, *job, eff)).endObject();
            send(c, w.take());
        } else {
            c.waits.insert(req->id);
        }
    } else if (cmd == "workflowSubmit") {
        send(c, doWorkflowSubmit(req->specJson));
    } else if (cmd == "workflowStatus") {
        send(c, workflowStatusJson(opts.paths, wfStore, store, req->id, "live"));
    } else if (cmd == "workflowPlan") {
        send(c, doWorkflowPlan(req->id, req->specJson));
    } else if (cmd == "requestRaise") {
        send(c, doRequestRaise(req->id, req->specJson));
    } else if (cmd == "requestAnswer") {
        send(c, doRequestAnswer(req->id, req->specJson));
    } else if (cmd == "workflowCancel") {
        send(c, doWorkflowCancel(req->id));
    } else if (cmd == "shutdown") {
        json::Writer w;
        w.beginObject().kvBool("ok", true).kv("note", "daemon stopping; running jobs keep running").endObject();
        send(c, w.take());
        stopping = true;
    } else {
        send(c, errorReply("unknown command '" + cmd + "'"));
    }
}

void Daemon::Impl::closeClient(platform::NativeSocket socket)
{
    waiter.unwatchSocket(socket);
    clients.erase(socket);   // closes the socket
}

void Daemon::Impl::onClient(const platform::WaitEvent& event)
{
    const auto socket = static_cast<platform::NativeSocket>(event.tag & kTagValueMask);
    auto it = clients.find(socket);
    if (it == clients.end()) return;
    Client& c = it->second;
    if (event.writable) flush(c);
    if (event.readable) {
        char buf[16384];
        for (;;) {
            auto n = c.stream.readSome(buf, sizeof buf, std::chrono::milliseconds(0));
            if (!n || (*n && **n == 0)) { closeClient(socket); return; }   // failed, or the peer closed its end
            if (!*n) break;                                                 // nothing more for now
            c.in.append(buf, **n);
        }
        std::string frame;
        for (;;) {
            auto got = c.in.next(frame);
            if (!got) { logf("client %lld: %s; closing", static_cast<long long>(socket), got.error().c_str()); closeClient(socket); return; }
            if (!*got) break;
            handle(c, frame);
            if (clients.find(socket) == clients.end()) return;
        }
    }
    if (event.hangup) closeClient(socket);
}

void Daemon::Impl::onAccept()
{
    for (;;) {
        auto accepted = listener.accept(std::chrono::milliseconds(0));
        if (!accepted || !*accepted) return;
        platform::SocketStream& s = **accepted;
        if (!platform::peerIsSameUser(s)) { logf("rejected a client from another user"); continue; }
        const platform::NativeSocket socket = s.native();
        if (!waiter.watchSocket(socket, true, false, tag(kClient, socket))) { logf("cannot watch a new client; closing it"); continue; }
        Client& c = clients[socket];
        c.owner = platform::peerUser(s);
        c.stream = std::move(s);
    }
}

void Daemon::Impl::drainPosts()
{
    for (;;) {
        std::move_only_function<void()> fn;
        {
            std::lock_guard lock(postMutex);
            if (posted.empty()) return;
            fn = std::move(posted.front());
            posted.pop_front();
        }
        fn();
    }
}

int Daemon::Impl::loop()
{
    while (!stopping) {
        // Blocks when there is nothing to watch (the idle state: no wake-ups); with jobs running or queued it wakes every 5 s (1 s near the reserve) to check memory.
        std::optional<std::chrono::milliseconds> timeout;
        if (hasWorkToWatch()) timeout = std::chrono::milliseconds(watchMs);
        if (!polledShims.empty() || opts.exitWith != 0) timeout = std::min(timeout.value_or(std::chrono::milliseconds(1000)), std::chrono::milliseconds(1000));
        auto events = waiter.wait(timeout);
        if (!events) { logf("%s", events.error().what().c_str()); return 1; }
        if (opts.exitWith != 0 && !platform::isAlive(opts.exitWith, exitWithStart)) {
            logf("process %lld, which this daemon serves (--exit-with), has ended: stopping (jobs keep running)", static_cast<long long>(opts.exitWith));
            stopping = true;
            break;
        }
        if (!polledShims.empty()) checkPolledShims();
        if (const auto now = std::chrono::steady_clock::now(); now - lastMemCheck >= std::chrono::milliseconds(watchMs) && hasWorkToWatch()) {
            lastMemCheck = now;
            shedIfPressured();
            scheduleQueued();  // memory recovered: admit what was held back
        }
        for (const platform::WaitEvent& e : *events) {
            if (e.kind == platform::WaitEvent::Kind::woken) { drainPosts(); continue; }
            switch (static_cast<Kind>(e.tag >> 56)) {
                case kListen: onAccept(); break;
                case kClient: onClient(e); break;
                case kShimExit: onShimExit(static_cast<platform::ProcessId>(e.tag & kTagValueMask)); break;
                case kStop: logf("asked to stop: stopping (jobs keep running)"); stopping = true; break;
            }
        }
    }
    return 0;
}


Daemon::Daemon(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Daemon::~Daemon()
{
    // The socket's file goes while the instance lock is still held (the Impl and its lock go after this body): a moved name (a long path on
    // Windows) would otherwise stay behind in the user's profile.
    impl_->listener = {};
    platform::removeLocal(impl_->opts.paths.controlSocket());
}

std::expected<std::unique_ptr<Daemon>, std::string> Daemon::create(DaemonOptions options)
{
    if (auto err = options.paths.ensureDirectories(); !err.empty()) return std::unexpected(err);
    auto lock = platform::FileLock::tryExclusive(options.paths.instanceLock());
    if (!lock) return std::unexpected(lock.error().message);
    if (!*lock) return std::unexpected("another sub0squad daemon already runs for this state directory");
    // The waiter takes the stop requests BEFORE the Impl starts its executor threads, so on POSIX those threads inherit SIGTERM and SIGINT blocked
    // and the signals reach the loop instead of ending the process.
    auto waiter = platform::Waiter::create();
    if (!waiter) return std::unexpected(waiter.error().message);
    if (auto ok = waiter->watchStopRequests(tag(kStop, 0)); !ok) return std::unexpected(ok.error().message);
    platform::ignoreBrokenPipes();
    auto impl = std::make_unique<Impl>();
    impl->opts = std::move(options);
    impl->instanceLock = std::move(**lock);
    impl->waiter = std::move(*waiter);
    auto journal = Journal::open(impl->opts.paths.journalDir());
    if (!journal) return std::unexpected(journal.error());
    impl->journal = std::move(*journal);
    for (const auto& rec : impl->journal.records()) { impl->store.apply(rec); impl->wfStore.apply(rec); }

    if (impl->opts.exitWith != 0) {
        const auto start = platform::processStart(impl->opts.exitWith);
        if (!start) return std::unexpected("--exit-with " + std::to_string(impl->opts.exitWith) + ": no such process");
        impl->exitWithStart = *start;
    }
    (void)platform::clearStaleLocalSockets();   // names moved for daemons that were killed
    auto listener = platform::listenLocal(impl->opts.paths.controlSocket());
    if (!listener) return std::unexpected(listener.error().message);
    impl->listener = std::move(*listener);
    if (auto ok = impl->waiter.watchSocket(impl->listener.native(), true, false, tag(kListen, 0)); !ok) return std::unexpected(ok.error().message);
    impl->reconcileAtStart();
    {   // run-info.json: which program is serving this state directory, since when and with which limits (catalogue row 20). Rewritten at every start.
        using namespace std::chrono;
        json::Writer w;
        w.beginObject().kv("schema", "run-info-1").kv("build", buildId()).kv("pid", platform::currentProcess()).kv("exe", impl->opts.exe.string())
            .kv("startedAt", duration<double>(system_clock::now().time_since_epoch()).count()).kv("maxJobs", impl->opts.maxJobs).kv("memShedMiB", impl->opts.memShedMiB)
            .kv("frontier", impl->opts.frontier == FrontierMode::off ? "off" : "stub").endObject();
        (void)platform::writeFileAtomic(impl->opts.paths.stateDir / "run-info.json", w.take() + "\n");
    }
    return std::unique_ptr<Daemon>(new Daemon(std::move(impl)));
}

int Daemon::run()
{
    logf("serving %s (max %d parallel jobs, %zu journal records)", impl_->opts.paths.controlSocket().string().c_str(), impl_->opts.maxJobs,
         impl_->journal.records().size());
    impl_->advanceWorkflows();
    impl_->scheduleQueued();
    return impl_->loop();
}

}  // namespace sub0squad
