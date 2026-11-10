#include "sub0squad/daemon.hpp"

#include "json_util.hpp"
#include "sub0squad/frame.hpp"
#include "sub0squad/job_store.hpp"
#include "sub0squad/host_guard.hpp"
#include "sub0squad/journal.hpp"
#include "sub0squad/ledger.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/progress.hpp"
#include "sub0squad/workflow.hpp"
#include "sub0squad/workflow_status.hpp"
#include "sub0squad/process_tree.hpp"
#include "sub0squad/status.hpp"
#include "sub0squad/supervisor.hpp"
#include "sub0squad/unix_socket.hpp"
#include "sub0pipeline/sub0pipeline.hpp"

#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace sub0squad {

namespace {

enum Kind : std::uint64_t { kListen = 1, kClient, kPidfd, kPost, kSignals };
constexpr std::uint64_t tag(Kind k, int fd) { return (static_cast<std::uint64_t>(k) << 32) | static_cast<std::uint32_t>(fd); }

void logf(const char* fmt, ...)
{
    std::va_list ap;
    va_start(ap, fmt);
    std::fputs("sub0squad: ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

std::string errorReply(std::string_view message)
{
    json::Writer w;
    w.beginObject().kvBool("ok", false).kv("error", message).endObject();
    return w.take();
}

struct Request
{
    int version = 0;
    std::string cmd, id, specJson;
};

std::expected<Request, std::string> parseRequest(std::string_view text)
{
    Request r;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "v") r.version = static_cast<int>(v.get_int64());
                else if (k == "cmd") r.cmd = json::toString(v);
                else if (k == "id") r.id = json::toString(v);
                else if (k == "spec") { std::string_view raw = v.raw_json(); r.specJson.assign(raw); }
            }
        });
    } catch (const simdjson::simdjson_error& e) {
        return std::unexpected(std::string{"malformed request: "} + e.what());
    }
    return r;
}

}  // namespace

struct Daemon::Impl
{
    struct Client
    {
        int fd = -1;
        FrameBuffer in;
        std::string out;
        std::set<std::string> waits;
    };

    struct Running
    {
        std::string id;
        int run = 1;
        int pidfd = -1;
        pid_t shimPid = 0;
        bool finalized = false;
        bool hasPipeline = false;
        int leaseIndex = -1;           ///< slot of the job's lease (0..cap-1), passed to the job as SUB0SQUAD_LEASE_INDEX; -1 = none or adopted
        std::string leaseResource;
        sub0pipeline::Completion done;
        std::jthread runner;
    };

    DaemonOptions opts;
    int lockFd = -1, listenFd = -1, epfd = -1, wakeFd = -1, sigFd = -1;
    Journal journal;
    JobStore store;
    WorkflowStore wfStore;
    RulePolicy policy;
    bool inAdvance = false;
    bool journalOk = true;
    bool stopping = false;
    std::map<int, Client> clients;
    std::map<std::string, std::unique_ptr<Running>> running;
    std::map<int, std::string> pidfdToJob;
    std::mutex postMutex;
    std::deque<std::move_only_function<void()>> posted;
    sub0pipeline::PriorityExecutor executor{{.threadCount = 2}};

    ~Impl()
    {
        for (auto& [id, r] : running) { r->done.succeed(); }  // release any pipeline still waiting; the shims keep running
        running.clear();                                       // joins the runner threads
        for (auto& [fd, c] : clients) ::close(fd);
        for (int fd : {listenFd, wakeFd, sigFd, epfd, lockFd}) if (fd >= 0) ::close(fd);
        for (auto& [fd, id] : pidfdToJob) ::close(fd);
    }

    // ---- reactor plumbing ------------------------------------------------------------------------------------------
    void post(std::move_only_function<void()> fn)
    {
        { std::lock_guard lock(postMutex); posted.push_back(std::move(fn)); }
        const std::uint64_t one = 1;
        (void)!::write(wakeFd, &one, sizeof one);
    }

    void epollAdd(int fd, std::uint32_t events, std::uint64_t data)
    {
        epoll_event ev{};
        ev.events = events;
        ev.data.u64 = data;
        ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    }

    // ---- journal ---------------------------------------------------------------------------------------------------
    bool record(std::string_view type, const std::string& body)
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

    // ---- startup reconciliation ------------------------------------------------------------------------------------
    void reconcileAtStart()
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

    void adopt(const JobRecord& job)
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

    // ---- scheduling: one one-node Sub0Pipeline graph per job -------------------------------------------------------
    int activeCount() const
    {
        int n = 0;
        for (const auto& [id, r] : running) {
            if (r->finalized) continue;
            const JobRecord* jr = store.find(id);
            if (!jr || !jr->spec.service) ++n;  // services (engines) hold memory, not a job slot
        }
        return n;
    }

    // ---- memory pressure: shed work before the kernel kills something that matters ---------------------------------------------------
    // Jobs and servers run with a high oom_score_adj (shim), so the kernel's last resort picks them before the owner's session; this is the
    // controlled first line: cancel the newest job when available memory falls below the reserve, servers only after every job, and admit nothing new.
    std::int64_t availableMiB() const
    {
        if (const char* f = std::getenv("SUB0SQUAD_FAKE_MEMAVAILABLE_FILE"); f && *f) {  // tests inject the number
            std::ifstream in(f);
            std::int64_t v = 0;
            if (in >> v) return v;
        }
        return readHostMemory().availableMiB;
    }
    bool underPressure() const { return opts.memShedMiB > 0 && availableMiB() < opts.memShedMiB + 512; }  // admission stops 512 MiB before shedding starts
    bool hasWorkToWatch() const
    {
        if (opts.memShedMiB <= 0) return false;
        if (activeCount() > 0) return true;
        for (const auto& j : store.jobs()) if (j.state == JobState::queued) return true;
        return false;
    }
    void shedIfPressured()
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
    std::map<std::string, std::string> shedNote;  ///< jobs cancelled by the memory guard: the reason recorded when they finish
    std::chrono::steady_clock::time_point lastMemCheck{};
    int watchMs = 5000;

    void scheduleQueued()
    {
        if (stopping || !journalOk) return;
        if (underPressure()) return;  // admit nothing while memory is short; the periodic check retries
        for (;;) {
            if (activeCount() >= opts.maxJobs) return;
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
                if (!next || j.submittedAt < next->submittedAt) next = &j;
            }
            if (!next) return;
            int idx = -1;
            if (!next->spec.leaseResource.empty()) { idx = 0; while (usedIndex[next->spec.leaseResource].count(idx)) ++idx; }  // the lowest free slot of the lease
            startRun(*next, idx);
        }
    }

    void startRun(const JobRecord& job, int leaseIndex = -1)
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

    void onShimStart(const std::string& id, int run, sub0pipeline::Completion done)
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

    void onRunnerDone(const std::string& id) { running.erase(id); }

    void onPidfd(int fd)
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

    void finalizeWith(const std::string& id, JobState state, const std::string& exitJson, const std::string& note)
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

    // ---- workflows and the ledger ---------------------------------------------------------------------------------
    void ledgerFor(const std::string& id)
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

    void executeWorkflowAction(const std::string& wfId, const WorkflowAction& a)
    {
        switch (a.kind) {
            case WorkflowAction::Kind::startJob: {
                const std::string id = store.nextId();
                record("jobSubmitted", journalbody::submitted(id, a.job));
                logf("workflow %s: queued %s job %s (item '%s', attempt %d, tier %s)", wfId.c_str(), a.job.role.c_str(), id.c_str(), a.job.item.c_str(), static_cast<int>(a.job.attempt), a.job.tier.c_str());
                break;
            }
            case WorkflowAction::Kind::recordDecision:
                record("workflowDecision", wfbody::decision(wfId, a.item, a.attempt, a.decisionAction, a.decisionTier, a.decisionJson));
                logf("workflow %s: decision for '%s' attempt %d: %s %s", wfId.c_str(), a.item.c_str(), a.attempt, a.decisionAction.c_str(), a.decisionTier.c_str());
                break;
            case WorkflowAction::Kind::cancelJob: (void)doCancel(a.jobId); break;
            case WorkflowAction::Kind::loadPlan: {
                const WorkflowRecord* wf = wfStore.find(wfId);
                if (!wf) break;
                std::ifstream in(std::filesystem::path{wf->spec.sandboxDir} / "plan.json");
                std::stringstream ss;
                ss << in.rdbuf();
                auto plan = Plan::fromJson(ss.str());
                if (plan) {
                    record("workflowPlanned", wfbody::planned(wfId, *plan));
                    logf("workflow %s: plan accepted, %zu item(s)", wfId.c_str(), plan->items.size());
                } else {
                    record("workflowFinished", wfbody::finished(wfId, WorkflowState::failed, "plan rejected: " + plan.error()));
                    logf("workflow %s: plan rejected: %s", wfId.c_str(), plan.error().c_str());
                }
                break;
            }
            case WorkflowAction::Kind::amendPlan: {
                const WorkflowRecord* wf = wfStore.find(wfId);
                if (!wf || !wf->plan) break;
                const std::filesystem::path sandbox{wf->spec.sandboxDir};
                const std::filesystem::path frag = sandbox / "items" / a.item / ("attempt-" + std::to_string(a.attempt)) / "fragment.json";
                std::ifstream in(frag);
                std::stringstream ss;
                ss << in.rdbuf();
                std::string reason;
                auto fragment = Plan::fromJson(ss.str());
                if (!fragment) reason = "fragment invalid: " + fragment.error();
                else if (auto merged = Plan::amended(*wf->plan, a.item, *fragment)) {
                    { std::ofstream o(sandbox / "plan.json", std::ios::trunc); o << merged->toJson(); }
                    const std::size_t before = wf->plan->items.size();
                    record("workflowPlanAmended", wfbody::planAmended(wfId, a.item, *merged));
                    logf("workflow %s: item '%s' divided into %zu items", wfId.c_str(), a.item.c_str(), merged->items.size() - before + 1);
                    break;
                } else reason = merged.error();
                record("workflowAmendRejected", wfbody::amendRejected(wfId, a.item, a.attempt, reason));
                logf("workflow %s: division of '%s' rejected: %s", wfId.c_str(), a.item.c_str(), reason.c_str());
                break;
            }
            case WorkflowAction::Kind::writeDispute: {
                const WorkflowRecord* wf = wfStore.find(wfId);
                if (!wf) break;
                const std::filesystem::path sandbox{wf->spec.sandboxDir};
                std::ifstream in(sandbox / "items" / a.fromItem / ("attempt-" + std::to_string(a.attempt)) / "gate.txt");
                std::stringstream ss;
                ss << in.rdbuf();
                std::ofstream o(sandbox / "items" / a.item / "dispute.txt", std::ios::trunc);
                o << "Item '" << a.fromItem << "' is graded by your tests and failed them even after several attempts. Some of your tests may contradict the contract. Failures reported:\n"
                  << ss.str() << "Check each failing test against the contract and fix the tests that are wrong; keep the ones that are right.\n";
                break;
            }
            case WorkflowAction::Kind::finish:
                record("workflowFinished", wfbody::finished(wfId, a.finalState, a.note));
                logf("workflow %s finished: %s (%s)", wfId.c_str(), toString(a.finalState), a.note.c_str());
                break;
        }
    }

    void advanceWorkflows()
    {
        if (inAdvance || stopping) return;
        inAdvance = true;
        WorkflowSettings settings;
        settings.exe = opts.exe.string();
        settings.frontier = opts.frontier;
        for (int round = 0; round < 16; ++round) {
            bool acted = false;
            std::vector<std::string> ids;
            for (const auto& wf : wfStore.workflows()) if (wf.state == WorkflowState::running) ids.push_back(wf.id);
            for (const auto& id : ids) {
                const WorkflowRecord* wf = wfStore.find(id);
                if (!wf || wf->state != WorkflowState::running) continue;
                const auto actions = advance(*wf, store, settings, policy);  // copies what it needs: the store is mutated below
                for (const auto& a : actions) { executeWorkflowAction(id, a); acted = true; }
            }
            if (!acted) break;
        }
        inAdvance = false;
        scheduleQueued();
    }

    std::string doWorkflowSubmit(const std::string& specJson)
    {
        if (!journalOk) return errorReply("journal is failing: not accepting new work");
        auto spec = WorkflowSpec::fromJson(specJson);
        if (!spec) return errorReply(spec.error());
        const std::string id = wfStore.nextId();
        if (spec->sandboxDir.empty()) spec->sandboxDir = std::filesystem::absolute(opts.paths.workflowsDir() / id).string();
        std::error_code ec;
        std::filesystem::create_directories(spec->sandboxDir, ec);
        if (ec) return errorReply("cannot create the sandbox " + spec->sandboxDir + ": " + ec.message());
        record("workflowSubmitted", wfbody::submitted(id, *spec));
        logf("workflow %s submitted: task '%s', sandbox %s", id.c_str(), spec->task.c_str(), spec->sandboxDir.c_str());
        if (!isKnownTask(spec->task)) {  // no blueprint: the architect role is deferred as a file handoff
            TriageResult t = triage(spec->taskText.empty() ? spec->task : spec->taskText);
            const std::filesystem::path packet = std::filesystem::path{spec->sandboxDir} / "needs-plan.md";
            std::ofstream(packet) << needsPlanPacket(spec->taskText.empty() ? spec->task : spec->taskText, t, id);
            record("workflowAwaitingPlan", wfbody::awaitingPlan(id, packet.string()));
            logf("workflow %s awaits a plan: %s", id.c_str(), packet.c_str());
        }
        advanceWorkflows();
        json::Writer w;
        w.beginObject().kvBool("ok", true).kv("id", id).kv("sandbox", spec->sandboxDir).endObject();
        return w.take();
    }

    std::string doWorkflowPlan(const std::string& id, const std::string& planJson)
    {
        const WorkflowRecord* wf = wfStore.find(id);
        if (!wf) return errorReply("no such workflow: " + id);
        if (wf->state != WorkflowState::awaitingPlan) return errorReply("workflow " + id + " is " + toString(wf->state) + ", not awaiting a plan");
        auto plan = Plan::fromJson(planJson);
        if (!plan) return errorReply("plan rejected: " + plan.error());
        { std::ofstream o(std::filesystem::path{wf->spec.sandboxDir} / "plan.json", std::ios::trunc); o << plan->toJson(); }
        record("workflowPlanned", wfbody::planned(id, *plan));
        logf("workflow %s: plan supplied, %zu item(s)", id.c_str(), plan->items.size());
        advanceWorkflows();
        json::Writer w;
        w.beginObject().kvBool("ok", true).kv("id", id).kv("items", static_cast<std::int64_t>(plan->items.size())).endObject();
        return w.take();
    }

    std::string doWorkflowCancel(const std::string& id)
    {
        const WorkflowRecord* wf = wfStore.find(id);
        if (!wf) return errorReply("no such workflow: " + id);
        if (wf->state != WorkflowState::running && wf->state != WorkflowState::awaitingPlan) return errorReply("workflow " + id + " is already " + toString(wf->state));
        if (wf->state == WorkflowState::awaitingPlan) {
            record("workflowFinished", wfbody::finished(id, WorkflowState::cancelled, "cancelled while awaiting a plan"));
            json::Writer w;
            w.beginObject().kvBool("ok", true).kv("id", id).endObject();
            return w.take();
        }
        record("workflowCancelRequested", wfbody::cancelRequested(id));
        advanceWorkflows();
        json::Writer w;
        w.beginObject().kvBool("ok", true).kv("id", id).endObject();
        return w.take();
    }

    void notifyWaiters(const std::string& id)
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

    // ---- requests --------------------------------------------------------------------------------------------------
    void send(Client& c, const std::string& payload)
    {
        c.out += encodeFrame(payload);
        flush(c);
    }

    void flush(Client& c)
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

    std::string doCancel(const std::string& id)
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

    std::string doResume(const std::string& id)
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

    void handle(Client& c, const std::string& text)
    {
        auto req = parseRequest(text);
        if (!req) { send(c, errorReply(req.error())); return; }
        if (req->version != 1) { send(c, errorReply("protocol version " + std::to_string(req->version) + " not supported (daemon speaks 1)")); return; }
        const std::string& cmd = req->cmd;
        if (cmd == "ping") {
            json::Writer w;
            w.beginObject().kvBool("ok", true).kv("v", 1).kv("pid", static_cast<std::int64_t>(::getpid())).kv("maxJobs", opts.maxJobs)
                .kvBool("journalOk", journalOk).endObject();
            send(c, w.take());
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

    void closeClient(int fd)
    {
        ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
        clients.erase(fd);
    }

    void onClient(int fd, std::uint32_t events)
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

    void onAccept()
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

    void drainPosts()
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

    int loop()
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
};

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
