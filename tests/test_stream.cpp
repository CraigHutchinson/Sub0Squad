// Streaming completions (docs/PORTAL transcript follow): a fake engine sends server-sent events in chunked pieces, one event split across two chunks, with pauses.
// The sink must see the pieces as they arrive (before the server has finished), the result must equal the non-streaming one, and an error status must be reported.
#include "check.hpp"
#include "sub0squad/engine.hpp"

#include "sub0platform/net.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using namespace sub0squad;
using Clock = std::chrono::steady_clock;

static void sendAll(sub0platform::SocketStream& fd, const std::string& s) { (void)fd.writeAll(s); }
static std::string chunk(const std::string& s) { char h[32]; std::snprintf(h, sizeof h, "%zx\r\n", s.size()); return std::string(h) + s + "\r\n"; }

/// A one-connection fake engine on an ephemeral port. `mode`: 0 streaming 200, 1 an error status.
struct Fake
{
    sub0platform::SocketListener listener;
    int port = 0;
    std::atomic<long long> finishedAtMs{0};
    std::thread th;
    Clock::time_point t0 = Clock::now();
    explicit Fake(int mode)
    {
        auto l = sub0platform::listenLoopback(0);
        CHECK(l.has_value());
        if (l) { listener = std::move(*l); port = listener.port(); }
        th = std::thread([this, mode] {
            auto accepted = listener.accept(std::chrono::seconds(30));
            if (!accepted || !*accepted) return;
            sub0platform::SocketStream fd = std::move(**accepted);
            char buf[16384];
            std::string req;
            while (req.find("\r\n\r\n") == std::string::npos) { const auto n = fd.readSome(buf, sizeof buf, std::chrono::seconds(10)); if (!n || !*n || **n == 0) break; req.append(buf, **n); }
            // the request body is not needed; but a streaming request must say stream:true
            streamRequested = req.find("\"stream\":true") != std::string::npos;
            if (mode == 1) { sendAll(fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 17\r\nConnection: close\r\n\r\n{\"error\":\"boom\"}\n"); return; }
            sendAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
            const auto pause = [] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); };
            sendAll(fd, chunk("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"think \"},\"finish_reason\":null}]}\n\n")); pause();
            sendAll(fd, chunk("data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"},\"finish_reason\":null}]}\n\ndata: {\"choices\":[{\"delta\":{\"con")); pause();   // an event split across two chunks
            sendAll(fd, chunk("tent\":\"lo\"},\"finish_reason\":null}]}\n\n")); pause();
            sendAll(fd, chunk("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":11,\"completion_tokens\":3},\"timings\":{\"prompt_ms\":12.5,\"predicted_ms\":300.0,\"cache_n\":4}}\n\ndata: [DONE]\n\n"));
            finishedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
            sendAll(fd, "0\r\n\r\n");
        });
    }
    std::atomic<bool> streamRequested{false};
    ~Fake() { if (th.joinable()) th.join(); }
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
    return finish();
}
