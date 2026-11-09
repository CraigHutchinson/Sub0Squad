#include "sub0squad/engine.hpp"

#include "json_util.hpp"

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstring>

namespace sub0squad {

std::string EngineProfile::toJson() const
{
    json::Writer w;
    w.beginObject().kv("name", name).kv("kind", kind).kv("endpoint", endpoint).kv("model", model).kv("maxConcurrent", maxConcurrent).kv("contextTokens", contextTokens).kvBool("thinking", thinking).endObject();
    return w.take();
}

std::string EngineProfile::toArg() const
{
    return name + "=" + endpoint + ",model=" + model + ",kind=" + kind + ",ctx=" + std::to_string(contextTokens) + ",slots=" + std::to_string(maxConcurrent) + (thinking ? ",thinking=1" : "") + (temperature >= 0 ? ",temp=" + std::to_string(temperature) : "") + (topP >= 0 ? ",top_p=" + std::to_string(topP) : "") + (topK >= 0 ? ",top_k=" + std::to_string(topK) : "");
}

std::expected<EngineProfile, std::string> EngineProfile::parse(const std::string& text)
{
    EngineProfile p;
    const auto eq = text.find('=');
    if (eq == std::string::npos || eq == 0) return std::unexpected("engine must be name=http://host:port/v1[,model=..][,kind=..][,ctx=N][,slots=N]");
    p.name = text.substr(0, eq);
    std::string rest = text.substr(eq + 1);
    std::size_t pos = 0;
    bool first = true;
    while (pos <= rest.size()) {
        const auto comma = rest.find(',', pos);
        const std::string part = rest.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (first) p.endpoint = part;
        else {
            const auto e = part.find('=');
            const std::string k = part.substr(0, e), v = e == std::string::npos ? "" : part.substr(e + 1);
            if (k == "model") p.model = v; else if (k == "kind") p.kind = v; else if (k == "ctx") p.contextTokens = std::atoi(v.c_str());
            else if (k == "slots") p.maxConcurrent = std::atoi(v.c_str()); else if (k == "thinking") p.thinking = v == "1" || v == "true";
            else if (k == "temp") p.temperature = std::atof(v.c_str()); else if (k == "top_p") p.topP = std::atof(v.c_str()); else if (k == "top_k") p.topK = std::atoi(v.c_str());
            else return std::unexpected("unknown engine option '" + k + "'");
        }
        first = false;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (p.endpoint.rfind("http://", 0) != 0) return std::unexpected("only http:// loopback endpoints are supported");
    if (p.model.empty()) p.model = p.name;
    if (p.maxConcurrent < 1) p.maxConcurrent = 1;
    return p;
}

namespace {

struct Url { std::string host, path; int port = 80; };

std::expected<Url, std::string> parseUrl(const std::string& url)
{
    if (url.rfind("http://", 0) != 0) return std::unexpected("only http:// URLs are supported: " + url);
    Url u;
    const std::string rest = url.substr(7);
    const auto slash = rest.find('/');
    std::string hostport = rest.substr(0, slash);
    u.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const auto colon = hostport.find(':');
    u.host = hostport.substr(0, colon);
    if (colon != std::string::npos) u.port = std::atoi(hostport.c_str() + colon + 1);
    if (u.host != "127.0.0.1" && u.host != "localhost" && u.host != "::1") return std::unexpected("engine endpoints must be on loopback (nothing leaves this machine): " + u.host);
    return u;
}

std::string dechunk(const std::string& s)
{
    std::string out;
    std::size_t pos = 0;
    while (pos < s.size()) {
        const auto eol = s.find("\r\n", pos);
        if (eol == std::string::npos) break;
        const std::size_t n = std::strtoul(s.substr(pos, eol - pos).c_str(), nullptr, 16);
        if (n == 0) break;
        out.append(s, eol + 2, n);
        pos = eol + 2 + n + 2;
    }
    return out;
}

}  // namespace

std::expected<HttpResponse, std::string> httpRequest(const std::string& method, const std::string& url, const std::string& body, int timeoutSec)
{
    auto u = parseUrl(url);
    if (!u) return std::unexpected(u.error());
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (::getaddrinfo(u->host.c_str(), std::to_string(u->port).c_str(), &hints, &res) != 0 || !res) return std::unexpected("cannot resolve " + u->host);
    const int fd = ::socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0 || ::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        const std::string err = std::strerror(errno);
        if (fd >= 0) ::close(fd);
        ::freeaddrinfo(res);
        return std::unexpected("cannot connect to " + u->host + ":" + std::to_string(u->port) + ": " + err);
    }
    ::freeaddrinfo(res);
    std::string req = method + " " + u->path + " HTTP/1.1\r\nHost: " + u->host + "\r\nConnection: close\r\nAccept: application/json\r\n";
    if (!body.empty() || method == "POST") req += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;
    const char* p = req.data();
    std::size_t left = req.size();
    while (left > 0) {
        const ssize_t n = ::send(fd, p, left, MSG_NOSIGNAL);
        if (n <= 0) { ::close(fd); return std::unexpected("send failed"); }
        p += n; left -= static_cast<std::size_t>(n);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
    std::string raw;
    char buf[8192];
    for (;;) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (ms <= 0) { ::close(fd); return std::unexpected("timed out after " + std::to_string(timeoutSec) + " s"); }
        pollfd pf{fd, POLLIN, 0};
        const int r = ::poll(&pf, 1, static_cast<int>(ms));
        if (r <= 0) continue;
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    const auto hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos || raw.rfind("HTTP/1.", 0) != 0) return std::unexpected("malformed HTTP response");
    HttpResponse out;
    out.status = std::atoi(raw.c_str() + 9);
    std::string headers = raw.substr(0, hdrEnd);
    for (auto& c : headers) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    out.body = raw.substr(hdrEnd + 4);
    if (headers.find("transfer-encoding: chunked") != std::string::npos) out.body = dechunk(out.body);
    return out;
}

std::expected<ChatResult, std::string> chatComplete(const EngineProfile& e, const std::string& system, const std::string& user, int maxTokens, double temperature, int timeoutSec)
{
    EngineProfile p = e;
    if (temperature >= 0) p.temperature = temperature;
    return chatCompleteMessages(p, {{"system", system}, {"user", user}}, maxTokens, timeoutSec);
}

std::expected<ChatResult, std::string> chatCompleteMessages(const EngineProfile& e, const std::vector<ChatMessage>& messages, int maxTokens, int timeoutSec)
{
    json::Writer w;
    w.beginObject().kv("model", e.model).key("messages").beginArray();
    for (const auto& m : messages) w.beginObject().kv("role", m.role).kv("content", m.content).endObject();
    w.endArray().kv("max_tokens", maxTokens).kv("temperature", e.effectiveTemperature()).kv("top_p", e.effectiveTopP()).kv("top_k", e.effectiveTopK()).kvBool("stream", false).kvBool("cache_prompt", true)
        .key("chat_template_kwargs").beginObject().kvBool("enable_thinking", e.thinking).endObject().endObject();
    auto resp = httpRequest("POST", e.endpoint + "/chat/completions", w.take(), timeoutSec);
    if (!resp) return std::unexpected(resp.error());
    if (resp->status != 200) return std::unexpected("engine returned HTTP " + std::to_string(resp->status) + ": " + resp->body.substr(0, 300));
    ChatResult r;
    try {
        json::withObject(resp->body, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "choices") {
                    for (auto ch : v.get_array()) {
                        for (auto cf : ch.get_object()) {
                            const std::string_view ck = cf.unescaped_key();
                            if (ck == "finish_reason") r.finishReason = json::toString(cf.value());
                            else if (ck == "message") for (auto mf : cf.value().get_object()) if (json::keyOf(mf) == "content") r.content = json::toString(mf.value());
                        }
                        break;
                    }
                } else if (k == "usage") {
                    for (auto uf : v.get_object()) {
                        const std::string_view uk = uf.unescaped_key();
                        if (uk == "prompt_tokens") r.promptTokens = uf.value().get_int64();
                        else if (uk == "completion_tokens") r.completionTokens = uf.value().get_int64();
                    }
                } else if (k == "timings") {
                    for (auto tf : v.get_object()) {
                        const std::string_view tk = tf.unescaped_key();
                        if (tk == "prompt_ms") r.promptMs = tf.value().get_double();
                        else if (tk == "predicted_ms") r.predictedMs = tf.value().get_double();
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error& ex) { return std::unexpected(std::string{"unreadable engine reply: "} + ex.what()); }
    return r;
}

long countTokens(const EngineProfile& e, const std::string& text, bool& estimated)
{
    estimated = false;
    const auto v1 = e.endpoint.rfind("/v1");
    const std::string root = v1 == std::string::npos ? e.endpoint : e.endpoint.substr(0, v1);
    json::Writer w;
    w.beginObject().kv("content", text).endObject();
    if (auto resp = httpRequest("POST", root + "/tokenize", w.take(), 30); resp && resp->status == 200) {
        try {
            long n = -1;
            json::withObject(resp->body, [&](simdjson::ondemand::object& obj) {
                for (auto f : obj) if (json::keyOf(f) == "tokens") { n = 0; for (auto t : f.value().get_array()) { (void)t; ++n; } }
            });
            if (n >= 0) return n;
        } catch (const simdjson::simdjson_error&) {}
    }
    estimated = true;
    return static_cast<long>(static_cast<double>(text.size()) / 3.5);
}

}  // namespace sub0squad
