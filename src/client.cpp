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
#include "sub0platform/ipc.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sub0squad {

std::expected<ControlClient, ControlClient::ConnectError> ControlClient::connect(const RuntimePaths& paths)
{
    auto s = sub0platform::connectLocal(paths.controlSocket());
    if (!s) return std::unexpected(ConnectError{s.error().error.message, s.error().noServer});
    ControlClient c;
    c.stream_ = std::move(*s);
    c.setReceiveTimeout(defaultReceiveSeconds());
    return c;
}

int ControlClient::defaultReceiveSeconds()
{
    const char* env = std::getenv("SUB0SQUAD_REPLY_TIMEOUT");
    const int seconds = env && *env ? std::atoi(env) : 0;
    return seconds > 0 ? seconds : 30;
}

void ControlClient::setReceiveTimeout(int seconds)
{
    receiveTimeout_ = seconds > 0 ? std::optional{std::chrono::milliseconds(seconds * 1000LL)} : std::nullopt;
}

std::expected<std::string, std::string> ControlClient::request(std::string_view cmd, std::string_view extra)
{
    std::string body = "{\"v\":1,\"cmd\":\"" + std::string{cmd} + "\"";
    if (!extra.empty()) { body += ","; body += extra; }
    body += "}";
    if (auto w = writeFrameBlocking(stream_, body); !w) return std::unexpected(w.error());
    auto reply = readFrameBlocking(stream_, receiveTimeout_);
    if (!reply && reply.error() == "timed out" && receiveTimeout_)
        return std::unexpected("timed out: the daemon did not answer '" + std::string{cmd} + "' within " + std::to_string(receiveTimeout_->count() / 1000) +
                               " s; it may be hung (see daemon.log in its state directory; `sub0squad down`, then `sub0squad up`, restarts it)");
    return reply;
}

}  // namespace sub0squad
