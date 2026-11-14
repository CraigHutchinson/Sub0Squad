/// @file daemon.cpp
/// Daemon: reactor and IPC (epoll loop, clients, request dispatch), construction and run.

#include "daemon_impl.hpp"

namespace sub0squad {

void Daemon::Impl::post(std::move_only_function<void()> fn)
{
    { std::lock_guard lock(postMutex); posted.push_back(std::move(fn)); }
    const std::uint64_t one = 1;
    (void)!::write(wakeFd, &one, sizeof one);
}

void Daemon::Impl::epollAdd(int fd, std::uint32_t events, std::uint64_t data)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.u64 = data;
    ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
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
        const ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (n > 0) { c.out.erase(0, static_cast<std::size_t>(n)); continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT;
            ev.data.u64 = tag(kClient, c.fd);
            ::epoll_ctl(epfd, EPOLL_CTL_MOD, c.fd, &ev);
            return;
        }
        return;  // peer gone; the EPOLLHUP path closes it
    }
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = tag(kClient, c.fd);
    ::epoll_ctl(epfd, EPOLL_CTL_MOD, c.fd, &ev);
}

void Daemon::Impl::handle(Client& c, const std::string& text)
{
    auto req = parseRequest(text);
    if (!req) { send(c, errorReply(req.error())); return; }
    if (req->version != 1) { send(c, errorReply("protocol version " + std::to_string(req->version) + " not supported (daemon speaks 1)")); return; }
    const std::string& cmd = req->cmd;
    if (cmd == "ping") {
        // A ping also runs the memory watcher's check NOW (what its timer does every 1 to 5 seconds), before it answers: the watcher's decision can then
        // be observed without waiting on a clock (catalogue row 22: the shed test waited up to 12 s for a 5 s timer and failed on a loaded host).
        shedIfPressured();
        scheduleQueued();
        json::Writer w;
        w.beginObject().kvBool("ok", true).kv("v", 1).kv("pid", static_cast<std::int64_t>(::getpid())).kv("maxJobs", opts.maxJobs)
            .kvBool("journalOk", journalOk).kv("build", buildId()).endObject();
        send(c, w.take());
    } else if ((cmd == "submit" || cmd == "workflowSubmit") && !req->build.empty() && req->build != buildId() && !req->allowBuildMismatch) {
        // Row 20: the daemon is still the binary it was started from. Work submitted by a newer (or older) command line would run on this one and be read as that one's.
        send(c, errorReply("build mismatch: this command is build '" + req->build + "' and the running daemon is build '" + buildId() +
                           "'. Restart the daemon so both are the same program (`sub0squad down`, then `sub0squad up`), or pass --allow-build-mismatch to submit anyway."));
    } else if (cmd == "submit") {
        if (!journalOk) { send(c, errorReply("journal is failing: not accepting new work")); return; }
        auto spec = JobSpec::fromJson(req->specJson);
        if (!spec) { send(c, errorReply(spec.error())); return; }
        auto uid = peerUid(c.fd);
        spec->owner = "uid:" + std::to_string(uid.value_or(0));
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

void Daemon::Impl::closeClient(int fd)
{
    ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    clients.erase(fd);
}

void Daemon::Impl::onClient(int fd, std::uint32_t events)
{
    auto it = clients.find(fd);
    if (it == clients.end()) return;
    Client& c = it->second;
    if (events & EPOLLOUT) flush(c);
    if (events & EPOLLIN) {
        char buf[16384];
        for (;;) {
            const ssize_t n = ::read(fd, buf, sizeof buf);
            if (n > 0) { c.in.append(buf, static_cast<std::size_t>(n)); continue; }
            if (n == 0) { closeClient(fd); return; }
            if (errno == EINTR) continue;
            break;
        }
        std::string frame;
        for (;;) {
            auto got = c.in.next(frame);
            if (!got) { logf("client %d: %s; closing", fd, got.error().c_str()); closeClient(fd); return; }
            if (!*got) break;
            handle(c, frame);
            if (clients.find(fd) == clients.end()) return;
        }
    }
    if (events & (EPOLLHUP | EPOLLERR)) closeClient(fd);
}

void Daemon::Impl::onAccept()
{
    for (;;) {
        const int fd = ::accept4(listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        auto uid = peerUid(fd);
        if (!uid || *uid != ::getuid()) { logf("rejected a client from another uid"); ::close(fd); continue; }
        clients[fd].fd = fd;
        epollAdd(fd, EPOLLIN, tag(kClient, fd));
    }
}

void Daemon::Impl::drainPosts()
{
    std::uint64_t n;
    (void)!::read(wakeFd, &n, sizeof n);
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
    epoll_event evs[32];
    while (!stopping) {
        // Blocks when there is nothing to watch (the idle state: no wake-ups); with jobs running or queued it wakes every 5 s (1 s near the reserve) to check memory.
        const int n = ::epoll_wait(epfd, evs, 32, hasWorkToWatch() ? watchMs : -1);
        if (n < 0) { if (errno == EINTR) continue; return 1; }
        if (const auto now = std::chrono::steady_clock::now(); now - lastMemCheck >= std::chrono::milliseconds(watchMs) && hasWorkToWatch()) {
            lastMemCheck = now;
            shedIfPressured();
            scheduleQueued();  // memory recovered: admit what was held back
        }
        for (int i = 0; i < n; ++i) {
            const auto kind = static_cast<Kind>(evs[i].data.u64 >> 32);
            const int fd = static_cast<int>(evs[i].data.u64 & 0xFFFFFFFFu);
            switch (kind) {
                case kListen: onAccept(); break;
                case kClient: onClient(fd, evs[i].events); break;
                case kPidfd: onPidfd(fd); break;
                case kPost: drainPosts(); break;
                case kSignals: { signalfd_siginfo si; (void)!::read(sigFd, &si, sizeof si); logf("signal %u: stopping (jobs keep running)", si.ssi_signo); stopping = true; break; }
            }
        }
    }
    return 0;
}


Daemon::Daemon(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Daemon::~Daemon() = default;

std::expected<std::unique_ptr<Daemon>, std::string> Daemon::create(DaemonOptions options)
{
    if (auto err = options.paths.ensureDirectories(); !err.empty()) return std::unexpected(err);
    auto lock = takeInstanceLock(options.paths.instanceLock());
    if (!lock) return std::unexpected(lock.error());
    auto impl = std::make_unique<Impl>();
    impl->opts = std::move(options);
    impl->lockFd = *lock;
    auto journal = Journal::open(impl->opts.paths.journalDir());
    if (!journal) return std::unexpected(journal.error());
    impl->journal = std::move(*journal);
    for (const auto& rec : impl->journal.records()) { impl->store.apply(rec); impl->wfStore.apply(rec); }

    impl->epfd = ::epoll_create1(EPOLL_CLOEXEC);
    impl->wakeFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    impl->sigFd = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    signal(SIGPIPE, SIG_IGN);
    auto listenFd = listenUnix(impl->opts.paths.controlSocket());
    if (!listenFd) return std::unexpected(listenFd.error());
    impl->listenFd = *listenFd;
    impl->epollAdd(impl->listenFd, EPOLLIN, tag(kListen, impl->listenFd));
    impl->epollAdd(impl->wakeFd, EPOLLIN, tag(kPost, impl->wakeFd));
    impl->epollAdd(impl->sigFd, EPOLLIN, tag(kSignals, impl->sigFd));
    impl->reconcileAtStart();
    {   // run-info.json: which program is serving this state directory, since when and with which limits (catalogue row 20). Rewritten at every start.
        using namespace std::chrono;
        json::Writer w;
        w.beginObject().kv("schema", "run-info-1").kv("build", buildId()).kv("pid", static_cast<std::int64_t>(::getpid())).kv("exe", impl->opts.exe.string())
            .kv("startedAt", duration<double>(system_clock::now().time_since_epoch()).count()).kv("maxJobs", impl->opts.maxJobs).kv("memShedMiB", impl->opts.memShedMiB)
            .kv("frontier", impl->opts.frontier == FrontierMode::off ? "off" : "stub").endObject();
        const std::filesystem::path file = impl->opts.paths.stateDir / "run-info.json", tmp = impl->opts.paths.stateDir / "run-info.json.tmp";
        { std::ofstream o(tmp, std::ios::trunc); o << w.take() << "\n"; }
        std::error_code ec;
        std::filesystem::rename(tmp, file, ec);
    }
    return std::unique_ptr<Daemon>(new Daemon(std::move(impl)));
}

int Daemon::run()
{
    logf("serving %s (max %d parallel jobs, %zu journal records)", impl_->opts.paths.controlSocket().c_str(), impl_->opts.maxJobs,
         impl_->journal.records().size());
    impl_->advanceWorkflows();
    impl_->scheduleQueued();
    return impl_->loop();
}

}  // namespace sub0squad
