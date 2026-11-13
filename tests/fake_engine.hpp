#pragma once
// A fake engine for agent tests (docs/COMPONENTS.md cleanup step 6, the fixture harness for agents): an OpenAI-compatible server on an ephemeral loopback
// port that answers every chat completion from a script of RECORDED REPLIES, streaming them as server-sent events in small pieces. No model, no process
// spawned: one accept thread and one thread per connection, all inside the test. It records every request (parsed messages) so a test can assert what the
// agent sent, and how many characters of a reply had been sent when the client closed the connection (an early abort).
#include <simdjson.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fake {

/// One scripted reply. `finish` is the finish reason ("stop", or "length" for a reply cut off at the token limit).
struct Reply
{
    std::string content;
    std::string finish = "stop";
    int pieceChars = 48;   ///< characters per streamed event
    int pauseMs = 0;       ///< pause between events (an abort test needs the stream to be slow enough to cancel)
    int delayMs = 0;       ///< wait this long before answering at all (streamed or not): a slow engine
    int status = 200;      ///< another status is sent as an error body
};

/// One request as the agent sent it.
struct Request
{
    std::vector<std::pair<std::string, std::string>> messages;   ///< role, content
    long maxTokens = 0;
    bool stream = false;
    long idSlot = -1;      ///< the `id_slot` the request pinned itself to (-1: none, the server chooses)
    /// The content of the last message (the newest user text).
    [[nodiscard]] const std::string& last() const { static const std::string none; return messages.empty() ? none : messages.back().second; }
    /// Every message's content, joined: what the model would have in its window.
    [[nodiscard]] std::string all() const { std::string s; for (const auto& m : messages) s += m.second + "\n"; return s; }
};

inline std::string jsonEscape(const std::string& s)
{
    std::string o;
    for (const unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o += static_cast<char>(c);
        }
    }
    return o;
}

class Engine
{
public:
    using Script = std::function<Reply(const Request&, int index)>;

    /// Replies are taken in order, one per chat request; past the end the last one repeats.
    explicit Engine(std::vector<Reply> replies) : Engine([r = std::move(replies)](const Request&, int i) { return r.empty() ? Reply{} : r[static_cast<std::size_t>(i) < r.size() ? static_cast<std::size_t>(i) : r.size() - 1]; }) {}
    /// The script chooses the reply from the request (forked sessions arrive in any order).
    explicit Engine(Script script) : script_(std::move(script))
    {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { std::perror("bind"); std::exit(2); }
        socklen_t len = sizeof a;
        ::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&a), &len);
        port_ = ntohs(a.sin_port);
        ::listen(listenFd_, 16);
        acceptor_ = std::thread([this] { acceptLoop(); });
    }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    ~Engine()
    {
        stop_ = true;
        if (acceptor_.joinable()) acceptor_.join();
        for (auto& t : workers_) if (t.joinable()) t.join();
        ::close(listenFd_);
    }

    /// The `--engine` argument that points an agent at this server.
    [[nodiscard]] std::string profile(int contextTokens = 8192) const { return "fake=http://127.0.0.1:" + std::to_string(port_) + "/v1,model=m,ctx=" + std::to_string(contextTokens); }
    [[nodiscard]] int port() const { return port_; }
    /// The chat requests received so far, in order of arrival.
    [[nodiscard]] std::vector<Request> requests() const { std::lock_guard<std::mutex> l(mu_); return requests_; }
    [[nodiscard]] std::size_t requestCount() const { std::lock_guard<std::mutex> l(mu_); return requests_.size(); }
    /// For request `i`: the characters of the reply that had been sent when the client closed the connection, or -1 when the reply was sent whole.
    [[nodiscard]] long abortedAfter(std::size_t i) const { std::lock_guard<std::mutex> l(mu_); return i < aborted_.size() ? aborted_[i] : -1; }

private:
    static bool sendAll(int fd, const std::string& s)
    {
        std::size_t off = 0;
        while (off < s.size()) {
            const ssize_t n = ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
            if (n <= 0) return false;
            off += static_cast<std::size_t>(n);
        }
        return true;
    }
    static std::string chunk(const std::string& s) { char h[32]; std::snprintf(h, sizeof h, "%zx\r\n", s.size()); return std::string(h) + s + "\r\n"; }
    /// Whether the peer has closed (it sends nothing after its request, so readable means closed).
    static bool peerClosed(int fd)
    {
        pollfd pf{fd, POLLIN, 0};
        if (::poll(&pf, 1, 0) <= 0) return false;
        char b[64];
        return ::recv(fd, b, sizeof b, MSG_DONTWAIT) <= 0;
    }

    void acceptLoop()
    {
        while (!stop_) {
            pollfd pf{listenFd_, POLLIN, 0};
            if (::poll(&pf, 1, 50) <= 0) continue;
            const int fd = ::accept(listenFd_, nullptr, nullptr);
            if (fd < 0) continue;
            workers_.emplace_back([this, fd] { serve(fd); ::close(fd); });
        }
    }

    void serve(int fd)
    {
        std::string req;
        char buf[16384];
        std::size_t headEnd = std::string::npos, need = 0;
        for (;;) {
            if (headEnd == std::string::npos) {
                headEnd = req.find("\r\n\r\n");
                if (headEnd != std::string::npos) {
                    std::string low = req.substr(0, headEnd);
                    for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    const auto cl = low.find("content-length:");
                    need = cl == std::string::npos ? 0 : static_cast<std::size_t>(std::atol(low.c_str() + cl + 15));
                }
            }
            if (headEnd != std::string::npos && req.size() >= headEnd + 4 + need) break;
            pollfd pf{fd, POLLIN, 0};
            if (::poll(&pf, 1, 5000) <= 0) return;
            const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
            if (n <= 0) return;
            req.append(buf, static_cast<std::size_t>(n));
        }
        const std::string line = req.substr(0, req.find("\r\n"));
        if (line.find("/chat/completions") == std::string::npos) {   // /tokenize, /slots: not offered; the client falls back to its own estimate
            sendAll(fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
            return;
        }
        Request r;
        try {
            simdjson::dom::parser parser;
            simdjson::dom::element doc = parser.parse(req.substr(headEnd + 4));
            for (auto m : doc["messages"].get_array()) r.messages.emplace_back(std::string(m["role"].get_string().value()), std::string(m["content"].get_string().value()));
            if (auto mt = doc["max_tokens"].get_int64(); !mt.error()) r.maxTokens = mt.value();
            if (auto st = doc["stream"].get_bool(); !st.error()) r.stream = st.value();
            if (auto sl = doc["id_slot"].get_int64(); !sl.error()) r.idSlot = sl.value();
        } catch (const simdjson::simdjson_error&) {}
        Reply reply;
        std::size_t index = 0;
        {
            std::lock_guard<std::mutex> l(mu_);
            index = requests_.size();
            requests_.push_back(r);
            aborted_.push_back(-1);
        }
        reply = script_(r, static_cast<int>(index));
        if (reply.delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(reply.delayMs));
        if (reply.status != 200) {
            const std::string body = "{\"error\":\"" + jsonEscape(reply.content) + "\"}";
            sendAll(fd, "HTTP/1.1 " + std::to_string(reply.status) + " Error\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            return;
        }
        const long completion = static_cast<long>(reply.content.size() / 4 + 1), prompt = static_cast<long>(r.all().size() / 4 + 1);
        const std::string usage = "\"usage\":{\"prompt_tokens\":" + std::to_string(prompt) + ",\"completion_tokens\":" + std::to_string(completion) + "}";
        if (!r.stream) {
            const std::string body = "{\"choices\":[{\"finish_reason\":\"" + reply.finish + "\",\"message\":{\"role\":\"assistant\",\"content\":\"" + jsonEscape(reply.content) + "\"}}]," + usage + "}";
            sendAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            return;
        }
        if (!sendAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n")) return;
        const std::size_t step = static_cast<std::size_t>(reply.pieceChars > 0 ? reply.pieceChars : 48);
        for (std::size_t off = 0; off < reply.content.size(); off += step) {
            const std::string piece = reply.content.substr(off, step);
            if (peerClosed(fd) || !sendAll(fd, chunk("data: {\"choices\":[{\"delta\":{\"content\":\"" + jsonEscape(piece) + "\"},\"finish_reason\":null}]}\n\n"))) {
                std::lock_guard<std::mutex> l(mu_);
                aborted_[index] = static_cast<long>(off);
                return;
            }
            if (reply.pauseMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(reply.pauseMs));
        }
        sendAll(fd, chunk("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"" + reply.finish + "\"}]," + usage + "}\n\ndata: [DONE]\n\n"));
        sendAll(fd, "0\r\n\r\n");
    }

    Script script_;
    int listenFd_ = -1, port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread acceptor_;
    std::vector<std::thread> workers_;
    mutable std::mutex mu_;
    std::vector<Request> requests_;
    std::vector<long> aborted_;
};

}  // namespace fake
