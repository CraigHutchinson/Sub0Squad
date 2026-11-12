// Streaming completions (docs/PORTAL transcript follow): a fake engine sends server-sent events in chunked pieces, one event split across two chunks, with pauses.
// The sink must see the pieces as they arrive (before the server has finished), the result must equal the non-streaming one, and an error status must be reported.
#include "check.hpp"
#include "sub0squad/engine.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace sub0squad;
using Clock = std::chrono::steady_clock;

static void sendAll(int fd, const std::string& s) { std::size_t off = 0; while (off < s.size()) { const ssize_t n = ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL); if (n <= 0) return; off += static_cast<std::size_t>(n); } }
static std::string chunk(const std::string& s) { char h[32]; std::snprintf(h, sizeof h, "%zx\r\n", s.size()); return std::string(h) + s + "\r\n"; }

/// A one-connection fake engine on an ephemeral port. `mode`: 0 streaming 200, 1 an error status.
struct Fake
{
    int listenFd = -1, port = 0;
    std::atomic<long long> finishedAtMs{0};
    std::thread th;
    Clock::time_point t0 = Clock::now();
    explicit Fake(int mode)
    {
        listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        CHECK(::bind(listenFd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
        socklen_t len = sizeof a;
        ::getsockname(listenFd, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
        ::listen(listenFd, 1);
        th = std::thread([this, mode] {
            const int fd = ::accept(listenFd, nullptr, nullptr);
            char buf[16384];
            std::string req;
            while (req.find("\r\n\r\n") == std::string::npos) { const ssize_t n = ::recv(fd, buf, sizeof buf, 0); if (n <= 0) break; req.append(buf, static_cast<std::size_t>(n)); }
            // the request body is not needed; but a streaming request must say stream:true
            streamRequested = req.find("\"stream\":true") != std::string::npos;
            if (mode == 1) { sendAll(fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 17\r\nConnection: close\r\n\r\n{\"error\":\"boom\"}\n"); ::close(fd); return; }
            sendAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
            const auto pause = [] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); };
            sendAll(fd, chunk("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"think \"},\"finish_reason\":null}]}\n\n")); pause();
            sendAll(fd, chunk("data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"},\"finish_reason\":null}]}\n\ndata: {\"choices\":[{\"delta\":{\"con")); pause();   // an event split across two chunks
            sendAll(fd, chunk("tent\":\"lo\"},\"finish_reason\":null}]}\n\n")); pause();
            sendAll(fd, chunk("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":11,\"completion_tokens\":3},\"timings\":{\"prompt_ms\":12.5,\"predicted_ms\":300.0,\"cache_n\":4}}\n\ndata: [DONE]\n\n"));
            finishedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
            sendAll(fd, "0\r\n\r\n");
            ::close(fd);
        });
    }
    std::atomic<bool> streamRequested{false};
    ~Fake() { if (th.joinable()) th.join(); ::close(listenFd); }
};

int main()
{
    {   // A stream: pieces arrive while the server is still sending, and the result is the whole.
        Fake fake(0);
        auto e = EngineProfile::parse("fake=http://127.0.0.1:" + std::to_string(fake.port) + "/v1,model=m");
        CHECK(e.has_value());
        std::vector<long long> at;
        std::string content, reasoning;
        const DeltaSink sink = [&](std::string_view c, std::string_view r) { at.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - fake.t0).count()); content += c; reasoning += r; };
        auto r = chatCompleteMessages(*e, {{"user", "hi"}}, 100, 30, sink);
        CHECK(r.has_value());
        CHECK(fake.streamRequested);
        if (r) {
            CHECK_EQ(r->content, std::string("Hello"));
            CHECK_EQ(r->reasoning, std::string("think "));
            CHECK_EQ(r->finishReason, std::string("stop"));
            CHECK_EQ(r->promptTokens, 11L);
            CHECK_EQ(r->completionTokens, 3L);
            CHECK(r->predictedMs == 300.0 && r->promptMs == 12.5 && r->cachedTokens == 4);
        }
        CHECK_EQ(content, std::string("Hello"));
        CHECK_EQ(reasoning, std::string("think "));
        CHECK(at.size() == 3);                                              // reasoning, "Hel", "lo": the split event arrived whole
        CHECK(!at.empty() && at.front() + 400 < fake.finishedAtMs.load());  // the first piece was seen well before the server finished
    }
    {   // An error status is an error, not an empty answer.
        Fake fake(1);
        auto e = EngineProfile::parse("fake=http://127.0.0.1:" + std::to_string(fake.port) + "/v1,model=m");
        auto r = chatCompleteMessages(*e, {{"user", "hi"}}, 100, 30, [](std::string_view, std::string_view) {});
        CHECK(!r.has_value() && r.error().find("HTTP 500") != std::string::npos);
    }
    return 0;
}
