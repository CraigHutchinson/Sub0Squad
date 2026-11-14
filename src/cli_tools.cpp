// Commands that are tools around a run: serve (the daemon), portal, replay, qa, recall, plan-lint.
#include "cli_impl.hpp"

#include "sub0squad/client.hpp"
#include "cmd_archive.hpp"
#include "cmd_process.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/build_id.hpp"
#include "json_util.hpp"
#include "sub0squad/daemon.hpp"
#include "sub0squad/frame.hpp"
#include "sub0squad/plan_lint.hpp"
#include "sub0squad/portal.hpp"
#include "sub0squad/qa.hpp"
#include "sub0squad/recall.hpp"
#include "sub0squad/replay.hpp"
#include "sub0squad/job_store.hpp"
#include "sub0squad/journal.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/status.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/workflow.hpp"
#include "sub0squad/workflow_status.hpp"
#include "sub0squad/unix_socket.hpp"
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sub0squad::cli {

int cmdServe(const RuntimePaths& paths, int argc, char** argv)
{
    DaemonOptions opts;
    opts.paths = paths;
    char self[4096];
    const ssize_t n = ::readlink("/proc/self/exe", self, sizeof self - 1);
    opts.exe = n > 0 ? std::string(self, static_cast<std::size_t>(n)) : std::string{argv[0]};
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::string_view{argv[i]} == "--mem-shed-mib") opts.memShedMiB = std::max(0, std::atoi(argv[i + 1]));
        if (std::string_view{argv[i]} == "--max-jobs") opts.maxJobs = std::max(1, std::atoi(argv[i + 1]));
        if (std::string_view{argv[i]} == "--frontier") {
            const std::string_view m = argv[i + 1];
            if (m == "off") opts.frontier = FrontierMode::off;
            else if (m == "stub") opts.frontier = FrontierMode::stub;
            else { std::fprintf(stderr, "serve: --frontier takes off or stub (there is no live mode in this build)\n"); return 2; }
        }
    }
    auto daemon = Daemon::create(std::move(opts));
    if (!daemon) { std::fprintf(stderr, "sub0squad serve: %s\n", daemon.error().c_str()); return 1; }
    return (*daemon)->run();
}

/// audit a plan's parallelism before running it: sub0squad plan-lint FILE [--tps N]
int cmdPlanLint(const RuntimePaths& /*paths*/, int argc, char** argv)
{
    if (argc < 3) { std::fputs(kUsage, stderr); return 2; }
    double tps = 5.0;
    for (int i = 3; i + 1 < argc; ++i) if (std::string_view{argv[i]} == "--tps") tps = std::atof(argv[i + 1]);
    const std::string text = slurpFile(argv[2]);
    auto plan = Plan::fromJson(text);
    if (!plan) { std::fprintf(stderr, "plan rejected: %s\n", plan.error().c_str()); return 1; }
    std::fputs(lintPlan(*plan, tps).toText().c_str(), stdout);
    return 0;
}

int cmdPortal(const RuntimePaths& paths, int argc, char** argv)
{
    int port = 8780;
    std::vector<std::string> engines;
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::string_view{argv[i]} == "--port") port = std::atoi(argv[i + 1]);
        else if (std::string_view{argv[i]} == "--engine-url") engines.push_back(argv[i + 1]);
    }
    return servePortal(port, [&paths] {
        bool offline = false;
        const std::string wf = workflowStatusAnyway(paths, "", offline);
        auto jobs = statusAnyway(paths, "", offline);
        return "{\"workflows\":" + (wf.empty() ? std::string{"{\"workflows\":[]}"} : wf) + ",\"jobs\":" + (jobs ? *jobs : std::string{"{\"jobs\":[]}"}) + "}";
   }, engines, [&paths](const std::string& command, const std::string& workflow, const std::string& spec) -> std::string {
        auto client = ControlClient::connect(paths);
        if (!client) return "{\"ok\":false,\"error\":\"the daemon is not running\"}";
        auto reply = client->request(command, "\"id\":\"" + workflow + "\",\"spec\":" + spec);
        return reply ? *reply : "{\"ok\":false,\"error\":\"" + reply.error() + "\"}";
    });
}

/// sub0squad replay WF NODE [--seeds 1,2,3] [--think] [--decompose] [--engine PROFILE] [--max-tokens N] [--repair-turns N]
int cmdReplay(const RuntimePaths& paths, int argc, char** argv)
{
    if (argc < 4) { std::fputs(kUsage, stderr); return 2; }
    sub0squad::ReplayOptions ro;
    ro.node = argv[3];
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--seeds") { ro.seeds.clear(); std::stringstream ss(val()); for (std::string x; std::getline(ss, x, ',');) if (!x.empty()) ro.seeds.push_back(std::atol(x.c_str())); }
        else if (a == "--think") ro.think = true;
        else if (a == "--decompose") ro.decompose = true;
        else if (a == "--fork") ro.fork = true;
        else if (a == "--parallel") ro.parallel = std::atoi(val().c_str());
        else if (a == "--sequential-reason") ro.sequentialReason = val();
        else if (a == "--engine") ro.engineProfile = val();
        else if (a == "--max-tokens") ro.maxTokens = std::atoi(val().c_str());
        else if (a == "--repair-turns") ro.repairTurns = std::atoi(val().c_str());
        else if (a == "--treatment") ro.treatment = val();
    }
    if (ro.seeds.empty()) ro.seeds = {1};
    bool offline = false;
    const std::string text = workflowStatusAnyway(paths, argv[2], offline);
    std::string firstEngine;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                if (json::keyOf(f) != "workflows") continue;
                for (auto w : f.value().get_array()) for (auto wf : w.get_object()) {
                    const auto k = json::keyOf(wf);
                    if (k == "sandbox") ro.sandbox = json::toString(wf.value());
                    else if (k == "engines") { for (auto e : wf.value().get_array()) if (firstEngine.empty()) firstEngine = json::toString(e.value()); }
                }
            }
        });
    } catch (const simdjson::simdjson_error&) {}
    if (ro.sandbox.empty()) { std::fprintf(stderr, "no such workflow: %s\n", argv[2]); return 2; }
    if (ro.engineProfile.empty()) ro.engineProfile = firstEngine;
    {
        char self[4096];
        const ssize_t n = ::readlink("/proc/self/exe", self, sizeof self - 1);
        ro.exe = n > 0 ? std::string(self, static_cast<std::size_t>(n)) : std::string{argv[0]};
    }
    return sub0squad::runReplay(ro);
}

/// sub0squad qa DIR | WORKFLOW: duplication and size findings over a source tree or a workflow's accepted outputs
int cmdQa(const RuntimePaths& paths, int argc, char** argv)
{
    if (argc < 3) { std::fputs(kUsage, stderr); return 2; }
    std::filesystem::path root = argv[2];
    std::error_code qec;
    if (!std::filesystem::is_directory(root, qec)) {
        bool offline = false;
        const std::string text = workflowStatusAnyway(paths, argv[2], offline);
        std::string sandbox;
        try { json::withObject(text, [&](simdjson::ondemand::object& obj) { for (auto f : obj) { if (json::keyOf(f) != "workflows") continue; for (auto w : f.value().get_array()) for (auto wf : w.get_object()) if (json::keyOf(wf) == "sandbox") sandbox = json::toString(wf.value()); } }); } catch (const simdjson::simdjson_error&) {}
        if (sandbox.empty()) { std::fprintf(stderr, "no such directory or workflow: %s\n", argv[2]); return 2; }
        root = qaCollect(sandbox, std::filesystem::path(sandbox) / "qa-tree");
    }
    const auto findings = qaScan(root);
    std::fputs(qaText(findings).c_str(), stdout);
    return 0;
}

/// sub0squad recall WF words... [--item NODE] [--limit N]: what the workflow already did (conversations, gates, briefings)
int cmdRecall(const RuntimePaths& paths, int argc, char** argv)
{
    if (argc < 4) { std::fputs(kUsage, stderr); return 2; }
    std::string query, item;
    int limit = 20;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--item" && i + 1 < argc) item = argv[++i];
        else if (a == "--limit" && i + 1 < argc) limit = std::atoi(argv[++i]);
        else query += (query.empty() ? "" : " ") + a;
    }
    bool offline = false;
    const std::string text = workflowStatusAnyway(paths, argv[2], offline);
    std::string sandbox;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                if (json::keyOf(f) != "workflows") continue;
                for (auto w : f.value().get_array()) for (auto wf : w.get_object()) if (json::keyOf(wf) == "sandbox") sandbox = json::toString(wf.value());
            }
        });
    } catch (const simdjson::simdjson_error&) {}
    if (sandbox.empty()) { std::fprintf(stderr, "no such workflow: %s\n", argv[2]); return 1; }
    const auto hits = recallSearch(sandbox, query, limit, item);
    for (const auto& h : hits) std::printf("%-12s %s:%d  %s\n", h.kind.c_str(), h.path.c_str(), h.line, h.text.c_str());
    if (hits.empty()) std::printf("nothing matched\n");
    return 0;
}

}  // namespace sub0squad::cli
