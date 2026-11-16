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
#include "sub0squad/platform_ipc.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sub0squad {

std::expected<ControlClient, ControlClient::ConnectError> ControlClient::connect(const RuntimePaths& paths)
{
    auto s = platform::connectLocal(paths.controlSocket());
    if (!s) return std::unexpected(ConnectError{s.error().error.message, s.error().noServer});
    ControlClient c;
    c.stream_ = std::move(*s);
    return c;
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
    return readFrameBlocking(stream_, receiveTimeout_);
}

}  // namespace sub0squad
