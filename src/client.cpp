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

namespace sub0squad {

ControlClient::~ControlClient() { if (fd_ >= 0) ::close(fd_); }

std::expected<ControlClient, ControlClient::ConnectError> ControlClient::connect(const RuntimePaths& paths)
{
    auto r = connectUnix(paths.controlSocket());
    if (r.fd < 0) return std::unexpected(ConnectError{r.error, r.noDaemon});
    ControlClient c;
    c.fd_ = r.fd;
    return c;
}

void ControlClient::setReceiveTimeout(int seconds)
{
    timeval tv{seconds, 0};
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

std::expected<std::string, std::string> ControlClient::request(std::string_view cmd, std::string_view extra)
{
    std::string body = "{\"v\":1,\"cmd\":\"" + std::string{cmd} + "\"";
    if (!extra.empty()) { body += ","; body += extra; }
    body += "}";
    if (auto w = writeFrameBlocking(fd_, body); !w) return std::unexpected(w.error());
    return readFrameBlocking(fd_);
}

}  // namespace sub0squad
