#pragma once
/// @file daemon_impl.hpp
/// Private definition of the daemon's implementation struct, shared by the daemon_*.cpp units: reactor and IPC (daemon.cpp), job lifecycle and
/// scheduling (daemon_jobs.cpp), workflow execution (daemon_workflow.cpp) and governance (daemon_governance.cpp). Not part of the public API.

#include "sub0squad/daemon.hpp"

#include "json_util.hpp"
#include "sub0squad/frame.hpp"
#include "sub0squad/job_store.hpp"
#include "sub0squad/host_guard.hpp"
#include "sub0squad/journal.hpp"
#include "sub0squad/ledger.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/briefing.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/overseer.hpp"
#include "sub0squad/recall.hpp"
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

namespace daemon_detail {

enum Kind : std::uint64_t { kListen = 1, kClient, kPidfd, kPost, kSignals };
constexpr std::uint64_t tag(Kind k, int fd) { return (static_cast<std::uint64_t>(k) << 32) | static_cast<std::uint32_t>(fd); }

inline void logf(const char* fmt, ...)
{
    std::va_list ap;
    va_start(ap, fmt);
    std::fputs("sub0squad: ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

inline std::string errorReply(std::string_view message)
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

inline std::expected<Request, std::string> parseRequest(std::string_view text)
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

}  // namespace daemon_detail

using namespace daemon_detail;

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
    void post(std::move_only_function<void()> fn);
    void epollAdd(int fd, std::uint32_t events, std::uint64_t data);
    // ---- journal ---------------------------------------------------------------------------------------------------
    bool record(std::string_view type, const std::string& body);
    // ---- startup reconciliation ------------------------------------------------------------------------------------
    void reconcileAtStart();
    void adopt(const JobRecord& job);
    // ---- scheduling: one one-node Sub0Pipeline graph per job -------------------------------------------------------
    int activeCount() const;
    // ---- memory pressure: shed work before the kernel kills something that matters ---------------------------------------------------
    // Jobs and servers run with a high oom_score_adj (shim), so the kernel's last resort picks them before the owner's session; this is the
    // controlled first line: cancel the newest job when available memory falls below the reserve, servers only after every job, and admit nothing new.
    std::int64_t availableMiB() const;
    bool underPressure() const { return opts.memShedMiB > 0 && availableMiB() < opts.memShedMiB + 512; }  // admission stops 512 MiB before shedding starts
    bool hasWorkToWatch() const;
    void shedIfPressured();
    std::map<std::string, int> lastSlot;  ///< "workflow/item" -> the lease slot its latest job took (slot affinity for the server-side prompt cache)
    std::map<std::string, std::string> shedNote;  ///< jobs cancelled by the memory guard: the reason recorded when they finish
    std::chrono::steady_clock::time_point lastMemCheck{};
    int watchMs = 5000;
    void scheduleQueued();
    void startRun(const JobRecord& job, int leaseIndex = -1);
    void onShimStart(const std::string& id, int run, sub0pipeline::Completion done);
    void onRunnerDone(const std::string& id) { running.erase(id); }
    void onPidfd(int fd);
    void finalizeWith(const std::string& id, JobState state, const std::string& exitJson, const std::string& note);
    // ---- workflows and the ledger ---------------------------------------------------------------------------------
    void ledgerFor(const std::string& id);
    void executeWorkflowAction(const std::string& wfId, const WorkflowAction& a);
    void advanceWorkflows();
    std::string doWorkflowSubmit(const std::string& specJson);
    std::string doWorkflowPlan(const std::string& id, const std::string& planJson);
    // ---- requests: what nodes, the engine or the owner raise, and the owner's or architect's answers ------------------------------------
    // Guidance reaches the nodes in scope as the feedback of their next attempt; for a requirement or clarification the nodes that were already
    // accepted are voided too, so they redo their work under the new words (impact on dependents follows the scope the owner chose).
    void applyGuidance(const WorkflowRecord& wf, const std::vector<std::string>& scope, const std::string& text, bool voidAccepted, const std::string& source);
    // The first responder: analyse an escalation's evidence before the owner is asked. Guidance is answered at once (at most twice per item, so
    // a loop cannot form); a fatal fault or a real decision stays open for the owner, with the analysis attached so the question is already framed.
    void firstResponder(const std::string& wfId, const std::string& rid);
    // Record an answer and hand its text to the nodes in scope as guidance. Returns an error text, empty when it took.
    std::string answerRequest(const std::string& wfId, const std::string& rid, const std::string& answer, const std::string& resolution, const std::string& by);
    std::string raiseRequest(const std::string& wfId, RequestRecord r);
    std::string doRequestRaise(const std::string& wfId, const std::string& specJson);
    std::string doRequestAnswer(const std::string& wfId, const std::string& specJson);
    std::string doWorkflowCancel(const std::string& id);
    void notifyWaiters(const std::string& id);
    // ---- requests --------------------------------------------------------------------------------------------------
    void send(Client& c, const std::string& payload);
    void flush(Client& c);
    std::string doCancel(const std::string& id);
    std::string doResume(const std::string& id);
    void handle(Client& c, const std::string& text);
    void closeClient(int fd);
    void onClient(int fd, std::uint32_t events);
    void onAccept();
    void drainPosts();
    int loop();
};

}  // namespace sub0squad
