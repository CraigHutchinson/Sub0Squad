/// @file daemon_jobs.cpp
/// Daemon: job lifecycle and scheduling (journal records, adoption, memory shedding, leases, shim start, finalisation, cancel, resume).

#include "daemon_impl.hpp"

namespace sub0squad {

bool Daemon::Impl::record(std::string_view type, const std::string& body)
{
    auto r = journal.append(type, body, true);
    if (!r) {
        if (journalOk) logf("JOURNAL FAILURE (%s): refusing new work, still supervising", r.error().c_str());
        journalOk = false;
        return false;
    }
    store.apply(journal.records().back());
    wfStore.apply(journal.records().back());
    return true;
}

void Daemon::Impl::reconcileAtStart()
{
    for (const auto& snapshot : std::vector<JobRecord>(store.jobs())) {
        if (snapshot.state != JobState::running && snapshot.state != JobState::cancelling) continue;
        const Effective eff = reconcile(opts.paths, snapshot);
        if (eff.state == JobState::running || eff.state == JobState::cancelling) {
            adopt(snapshot);
        } else {
            record("jobFinished", journalbody::finished(snapshot.id, snapshot.run, eff.state, eff.exitJson, eff.note));
            ledgerFor(snapshot.id);
            logf("job %s reconciled at start: %s (%s)", snapshot.id.c_str(), std::string{toString(eff.state)}.c_str(), eff.note.c_str());
        }
    }
}

void Daemon::Impl::adopt(const JobRecord& job)
{
    const int pidfd = static_cast<int>(::syscall(SYS_pidfd_open, static_cast<pid_t>(job.shimPid), 0));
    if (pidfd < 0) { record("jobFinished", journalbody::finished(job.id, job.run, JobState::lost, "", "supervisor vanished while adopting")); return; }
    auto r = std::make_unique<Running>();
    r->id = job.id;
    r->run = job.run;
    r->pidfd = pidfd;
    r->shimPid = static_cast<pid_t>(job.shimPid);
    pidfdToJob[pidfd] = job.id;
    epollAdd(pidfd, EPOLLIN, tag(kPidfd, pidfd));
    running[job.id] = std::move(r);
    logf("adopted running job %s (shim %d)", job.id.c_str(), static_cast<int>(job.shimPid));
}

int Daemon::Impl::activeCount() const
{
    int n = 0;
    for (const auto& [id, r] : running) {
        if (r->finalized) continue;
        const JobRecord* jr = store.find(id);
        if (!jr || !jr->spec.service) ++n;  // services (engines) hold memory, not a job slot
    }
    return n;
}

std::int64_t Daemon::Impl::availableMiB() const
{
    if (const char* f = std::getenv("SUB0SQUAD_FAKE_MEMAVAILABLE_FILE"); f && *f) {  // tests inject the number
        std::ifstream in(f);
        std::int64_t v = 0;
        if (in >> v) return v;
    }
    return readHostMemory().availableMiB;
}

bool Daemon::Impl::hasWorkToWatch() const
{
    if (opts.memShedMiB <= 0) return false;
    if (activeCount() > 0) return true;
    for (const auto& j : store.jobs()) if (j.state == JobState::queued) return true;
    return false;
}

void Daemon::Impl::shedIfPressured()
{
    if (opts.memShedMiB <= 0 || stopping) return;
    const std::int64_t avail = availableMiB();
    watchMs = avail < 2 * static_cast<std::int64_t>(opts.memShedMiB) ? 1000 : 5000;  // quiet: a check every 5 s (idle stays near zero wake-ups); close to the reserve: every second
    if (avail >= opts.memShedMiB) return;
    const JobRecord* victim = nullptr;
    for (bool services : {false, true}) {  // every plain job before any server
        for (const auto& [id, r] : running) {
            const JobRecord* jr = store.find(id);
            if (r->finalized || !jr || jr->state == JobState::cancelling || jr->spec.service != services) continue;
            if (services && avail >= opts.memShedMiB / 2) continue;  // servers are only shed when memory is critically low
            if (!victim || jr->startedAt > victim->startedAt) victim = jr;
        }
        if (victim) break;
    }
    if (!victim) return;
    shedNote[victim->id] = "shed: memory pressure (" + std::to_string(avail) + " MiB available, reserve " + std::to_string(opts.memShedMiB) + " MiB)";
    logf("memory pressure: %lld MiB available, shedding %s%s", static_cast<long long>(avail), victim->id.c_str(), victim->spec.service ? " (a server)" : "");
    (void)doCancel(victim->id);  // one per tick: give the kernel time to reclaim before shedding more
}

void Daemon::Impl::scheduleQueued()
{
    if (stopping || !journalOk) return;
    if (underPressure()) return;  // admit nothing while memory is short; the periodic check retries
    for (;;) {
        // Capacity classes (row 19, policy.hpp): structural jobs without an engine lease have a small reserve of their own beyond --max-jobs.
        const auto reservedClass = [](const JobSpec& s) { return isStructuralJob(s.workflow, s.role) && s.leaseResource.empty(); };
        int activeReserved = 0;
        for (const auto& [id, r] : running) if (!r->finalized) if (const JobRecord* jr = store.find(id); jr && !jr->spec.service && reservedClass(jr->spec)) ++activeReserved;
        const int activeNormal = activeCount() - activeReserved;
        // Lease occupancy of the jobs that are running now: a queued job whose shared resource (an engine's slots) is full waits
        // without blocking later jobs that need nothing or a different resource.
        std::map<std::string, std::int64_t> held;
        std::map<std::string, std::set<int>> usedIndex;
        for (const auto& [id, r] : running)
            if (!r->finalized) if (const JobRecord* jr = store.find(id); jr && !jr->spec.leaseResource.empty()) {
                ++held[jr->spec.leaseResource];
                if (r->leaseIndex >= 0) usedIndex[jr->spec.leaseResource].insert(r->leaseIndex);
            }
        const JobRecord* next = nullptr;
        for (const auto& j : store.jobs()) {
            if (j.state != JobState::queued || running.count(j.id)) continue;
            if (!j.spec.leaseResource.empty() && held[j.spec.leaseResource] >= std::max<std::int64_t>(1, j.spec.leaseCap)) continue;
            if (!j.spec.service && !admitsJob(reservedClass(j.spec), activeNormal, activeReserved, opts.maxJobs)) continue;
            const bool structural = isStructuralJob(j.spec.workflow, j.spec.role), nextStructural = next && isStructuralJob(next->spec.workflow, next->spec.role);
            if (!next || (structural && !nextStructural) || (structural == nextStructural && j.submittedAt < next->submittedAt)) next = &j;
        }
        if (!next) return;
        int idx = -1;
        if (!next->spec.leaseResource.empty()) {
            const std::string key = next->spec.workflow + "/" + next->spec.item;
            const auto prev = lastSlot.find(key);
            idx = chooseLeaseSlot(static_cast<int>(std::max<std::int64_t>(1, next->spec.leaseCap)), usedIndex[next->spec.leaseResource], prev == lastSlot.end() ? -1 : prev->second);
            if (!next->spec.item.empty()) lastSlot[key] = idx;  // a node's next attempt goes back to the slot that already holds its prompt
        }
        startRun(*next, idx);
    }
}

void Daemon::Impl::startRun(const JobRecord& job, int leaseIndex)
{
    const auto runDir = opts.paths.runDir(job.id, job.run);
    std::error_code ec;
    std::filesystem::create_directories(runDir, ec);
    JobSpec spec = job.spec;
    if (leaseIndex >= 0) spec.env["SUB0SQUAD_LEASE_INDEX"] = std::to_string(leaseIndex);
    if (job.run > 1 && !spec.resumeArgv.empty()) spec.argv = spec.resumeArgv;
    { std::ofstream o(runDir / "spec.json"); o << spec.toJson(); }
    auto entry = std::make_unique<Running>();
    entry->id = job.id;
    entry->run = job.run;
    entry->hasPipeline = true;
    entry->leaseIndex = leaseIndex;
    entry->leaseResource = job.spec.leaseResource;
    auto pipe = std::make_shared<sub0pipeline::Pipeline>();
    auto node = pipe->emplaceDeferred([this, id = job.id, run = job.run](sub0pipeline::Completion done) {
        post([this, id, run, d = std::move(done)]() mutable { onShimStart(id, run, std::move(d)); });
    });
    (void)node;
    entry->runner = std::jthread([this, pipe, id = job.id] {
        (void)pipe->run(executor);
        pipe->joinOrphans();
        post([this, id] { onRunnerDone(id); });
    });
    running[job.id] = std::move(entry);
}

void Daemon::Impl::onShimStart(const std::string& id, int run, sub0pipeline::Completion done)
{
    auto it = running.find(id);
    if (it == running.end()) { done.succeed(); return; }
    Running& r = *it->second;
    r.done = std::move(done);
    const auto runDir = opts.paths.runDir(id, run);
    auto pid = spawnShim(opts.exe, runDir);
    if (!pid) {
        logf("job %s: %s", id.c_str(), pid.error().c_str());
        finalizeWith(id, JobState::failed, "", pid.error());
        return;
    }
    r.shimPid = *pid;
    const auto ticks = proc::startTicks(*pid).value_or(0);
    r.pidfd = static_cast<int>(::syscall(SYS_pidfd_open, *pid, 0));
    record("jobStarted", journalbody::started(id, run, *pid, ticks));
    if (r.pidfd >= 0) { pidfdToJob[r.pidfd] = id; epollAdd(r.pidfd, EPOLLIN, tag(kPidfd, r.pidfd)); }
    logf("job %s run %d started (shim %d)", id.c_str(), run, static_cast<int>(*pid));
}

void Daemon::Impl::onPidfd(int fd)
{
    auto it = pidfdToJob.find(fd);
    if (it == pidfdToJob.end()) return;
    const std::string id = it->second;
    pidfdToJob.erase(it);
    ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    auto rit = running.find(id);
    if (rit == running.end()) return;
    rit->second->pidfd = -1;
    int status = 0;
    (void)::waitpid(rit->second->shimPid, &status, WNOHANG);  // reap when it is our child; harmless otherwise
    const JobRecord* job = store.find(id);
    if (!job) return;
    const Effective eff = reconcile(opts.paths, *job);
    // The shim is gone, so reconcile returns either its recorded outcome or `lost`.
    finalizeWith(id, eff.state == JobState::running || eff.state == JobState::cancelling ? JobState::lost : eff.state, eff.exitJson, eff.note);
}

void Daemon::Impl::finalizeWith(const std::string& id, JobState state, const std::string& exitJson, const std::string& note)
{
    const JobRecord* job = store.find(id);
    std::string finalNote = note;
    if (auto sn = shedNote.find(id); sn != shedNote.end()) { finalNote = sn->second; shedNote.erase(sn); }
    if (job) record("jobFinished", journalbody::finished(id, job->run, state, exitJson, finalNote));
    logf("job %s finished: %s (%s)", id.c_str(), std::string{toString(state)}.c_str(), note.c_str());
    auto it = running.find(id);
    if (it != running.end()) {
        it->second->finalized = true;
        if (it->second->hasPipeline) it->second->done.succeed();  // ends the one-node graph; onRunnerDone erases the entry
        else running.erase(it);
    }
    notifyWaiters(id);
    if (job) ledgerFor(id);
    advanceWorkflows();
    scheduleQueued();
}

void Daemon::Impl::ledgerFor(const std::string& id)
{
    const JobRecord* job = store.find(id);
    if (!job || job->spec.workflow.empty()) return;
    LedgerRecord r;
    r.ts = job->finishedAt;
    r.workflow = job->spec.workflow; r.item = job->spec.item; r.role = job->spec.role; r.job = job->id;
    r.tier = job->spec.tier; r.attempt = static_cast<int>(job->spec.attempt); r.run = job->run;
    r.stub = true;  // no live provider exists in this build
    r.outcome = std::string{toString(job->state)};
    r.durationSec = job->startedAt > 0 ? job->finishedAt - job->startedAt : 0.0;
    const Progress pr = readProgress(opts.paths.runDir(job->id, job->run), job->spec.adapter, job->spec.cwd);
    if (auto it = pr.metrics.find("tokens.in"); it != pr.metrics.end()) r.tokensIn = it->second;
    if (auto it = pr.metrics.find("tokens.out"); it != pr.metrics.end()) r.tokensOut = it->second;
    r.costUnits = notionalCost(r.tier, r.tokensIn, r.tokensOut);
    auto metric = [&](const char* name) { auto it = pr.metrics.find(name); return it == pr.metrics.end() ? 0.0 : it->second; };
    r.tokensSystem = metric("tokens.system"); r.tokensCard = metric("tokens.card"); r.tokensFeedback = metric("tokens.feedback");
    r.tokensWork = metric("tokens.work"); r.harnessBytes = metric("harness.bytes"); r.decodeTps = metric("engine.decode_tps");
    for (std::size_t i = 0; i + 1 < job->spec.argv.size(); ++i)
        if (job->spec.argv[i] == "--engine") r.engine = job->spec.argv[i + 1].substr(0, job->spec.argv[i + 1].find('='));
    if (!appendLedger(opts.paths.ledgerFile(), r)) logf("cannot append to the ledger %s", opts.paths.ledgerFile().c_str());
}

std::string Daemon::Impl::doCancel(const std::string& id)
{
    const JobRecord* job = store.find(id);
    if (!job) return errorReply("no such job: " + id);
    if (isTerminal(job->state)) return errorReply("job " + id + " is already " + std::string{toString(job->state)});
    const int run = job->run;
    const bool wasQueued = job->state == JobState::queued;
    const pid_t shim = static_cast<pid_t>(job->shimPid);
    const std::uint64_t ticks = job->shimStartTicks;
    record("jobCancelRequested", journalbody::cancelRequested(id, run));
    if (!wasQueued && shim > 0 && proc::isAlive(shim, ticks)) ::kill(shim, SIGTERM);
    json::Writer w;
    w.beginObject().kvBool("ok", true).kv("id", id).kv("state", toString(store.find(id)->state)).endObject();
    if (wasQueued) { notifyWaiters(id); }
    return w.take();
}

std::string Daemon::Impl::doResume(const std::string& id)
{
    const JobRecord* job = store.find(id);
    if (!job) return errorReply("no such job: " + id);
    if (job->state != JobState::cancelled && job->state != JobState::lost && job->state != JobState::failed)
        return errorReply("job " + id + " is " + std::string{toString(job->state)} + "; only cancelled, lost or failed jobs can be resumed");
    if (job->spec.resumeArgv.empty()) return errorReply("job " + id + " declares no resume command");
    if (!journalOk) return errorReply("journal is failing: not accepting work");
    record("jobResumed", journalbody::resumed(id, job->run + 1));
    scheduleQueued();
    json::Writer w;
    w.beginObject().kvBool("ok", true).kv("id", id).kv("run", store.find(id)->run).endObject();
    return w.take();
}

}  // namespace sub0squad
