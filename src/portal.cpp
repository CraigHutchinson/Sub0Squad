// The portal's HTTP server and API routes (portal_impl.hpp names the other units).
#include "portal_impl.hpp"

namespace sub0squad {

using namespace portal;

namespace {

// ---- HTTP -----------------------------------------------------------------------------------------------------------------------
void sendAll(int fd, const std::string& s)
{
    std::size_t off = 0;
    while (off < s.size()) {
        const ssize_t n = ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;
        off += static_cast<std::size_t>(n);
    }
}

void respond(int fd, int code, const char* type, const std::string& body, const char* extraHeaders = "")
{
    const char* text = code == 200 ? "OK" : code == 404 ? "Not Found" : "Method Not Allowed";
    sendAll(fd, "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n" + extraHeaders + "Connection: close\r\n\r\n" + body);
}

}  // namespace

namespace {
std::string randomToken()
{
    unsigned char b[16] = {};
    if (std::FILE* f = std::fopen("/dev/urandom", "rb")) { (void)!std::fread(b, 1, sizeof b, f); std::fclose(f); }
    static const char* hex = "0123456789abcdef";
    std::string t;
    for (unsigned char c : b) { t += hex[c >> 4]; t += hex[c & 15]; }
    return t;
}
std::string headerValue(const std::string& req, const std::string& name)  // case-insensitive, first occurrence
{
    std::string lower = req;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    const std::string key = "\r\n" + name + ":";
    const auto p = lower.find(key);
    if (p == std::string::npos) return {};
    auto s = p + key.size();
    while (s < req.size() && req[s] == ' ') ++s;
    return req.substr(s, req.find("\r\n", s) - s);
}
}  // namespace

int servePortal(int port, const std::function<std::string()>& state, const std::vector<std::string>& extraEngines, const PortalAct& act)
{
    const std::string token = randomToken();
    const int srv = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (srv < 0) { std::perror("portal: socket"); return 1; }
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);  // loopback only: the portal is never reachable from another machine
    if (::bind(srv, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(srv, 16) != 0) { std::perror("portal: bind/listen"); ::close(srv); return 1; }

    // Generated sites are served from a second loopback port, a different origin from the portal: a page the models wrote cannot read the
    // portal's API (no CORS headers there), and its own responses forbid any network access (CSP connect-src 'none', default-src to itself).
    const int siteSrv = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    {
        int one2 = 1;
        ::setsockopt(siteSrv, SOL_SOCKET, SO_REUSEADDR, &one2, sizeof one2);
        sockaddr_in b{};
        b.sin_family = AF_INET;
        b.sin_port = htons(static_cast<std::uint16_t>(port + 1));
        ::inet_pton(AF_INET, "127.0.0.1", &b.sin_addr);
        if (siteSrv < 0 || ::bind(siteSrv, reinterpret_cast<sockaddr*>(&b), sizeof b) != 0 || ::listen(siteSrv, 16) != 0) { std::perror("portal: site port"); return 1; }
    }
    Live live;
    for (const auto& b : extraEngines) live.bases.insert(b);
    std::jthread sampler([&live, &state](std::stop_token st) {
        double prevBusy = 0, prevTotal = 0;
        int tick = 0;
        while (!st.stop_requested()) {
            const auto next = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            sampleSystem(live, prevBusy, prevTotal);
            if (tick++ % 2 == 0) {
                std::string fresh = state();  // may take a while when the host is thrashing: only this thread waits for it
                discover(live, fresh);
                std::lock_guard lock(live.m);
                live.stateJson = std::move(fresh);
            }
            std::set<std::string> bases;
            { std::lock_guard lock(live.m); bases = live.bases; }
            for (const auto& b : bases) {
                bool need = false;
                { std::lock_guard lock(live.m); const auto& m = live.engines[b].model; need = !m.have || nowSec() - m.fetchedAt > 60; }
                if (need) fetchModel(live, b);
                sampleEngine(live, b);
            }
            std::this_thread::sleep_until(next);
        }
    });
    std::fprintf(stderr, "portal: http://127.0.0.1:%d/  (read-only, loopback only; Ctrl-C to stop)\n", port);
    for (;;) {
        pollfd pf[2] = {{srv, POLLIN, 0}, {siteSrv, POLLIN, 0}};
        if (::poll(pf, 2, -1) <= 0) continue;
        const bool onSite = !(pf[0].revents & POLLIN);
        const int c = ::accept4(onSite ? siteSrv : srv, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0) continue;
        timeval tv{2, 0};
        ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        std::string req;
        char buf[2048];
        while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) break;
            req.append(buf, static_cast<std::size_t>(n));
        }
        if (req.rfind("POST ", 0) == 0) {  // the owner's inbox: same-origin, tokened, small
            const std::string target = req.substr(5, req.find(' ', 5) - 5);
            const auto headEnd = req.find("\r\n\r\n");
            const std::size_t want = static_cast<std::size_t>(std::clamp<long>(std::atol(headerValue(req, "content-length").c_str()), 0, 65536));
            while (headEnd != std::string::npos && req.size() < headEnd + 4 + want) {
                const ssize_t n = ::recv(c, buf, sizeof buf, 0);
                if (n <= 0) break;
                req.append(buf, static_cast<std::size_t>(n));
            }
            const std::string origin = headerValue(req, "origin"), host = headerValue(req, "host");
            const std::string self1 = "127.0.0.1:" + std::to_string(port), self2 = "localhost:" + std::to_string(port);
            const bool sameOrigin = (origin.empty() || origin == "http://" + self1 || origin == "http://" + self2) && (host == self1 || host == self2);
            if (!act || onSite || (target != "/api/raise" && target != "/api/answer")) respond(c, 404, "text/plain", "not found\n");
            else if (!sameOrigin || headerValue(req, "x-portal-token") != token) respond(c, 403, "text/plain", "forbidden\n");
            else {
                std::string wf, spec;
                try {
                    simdjson::ondemand::parser parser;
                    simdjson::padded_string body{req.substr(headEnd + 4)};
                    auto doc = parser.iterate(body);
                    for (auto f : doc.get_object()) {
                        const std::string_view k = f.unescaped_key();
                        if (k == "wf") wf = std::string{std::string_view{f.value().get_string()}};
                        else if (k == "spec") spec = std::string{f.value().raw_json().value()};
                    }
                } catch (const simdjson::simdjson_error&) {}
                const bool okId = !wf.empty() && wf.size() < 20 && std::all_of(wf.begin(), wf.end(), [](unsigned char ch) { return std::isalnum(ch) || ch == '-'; });
                if (!okId || spec.empty()) respond(c, 400, "application/json", "{\"ok\":false,\"error\":\"bad request\"}");
                else respond(c, 200, "application/json", act(target == "/api/raise" ? "requestRaise" : "requestAnswer", wf, spec));
            }
        }
        else if (req.rfind("GET ", 0) != 0) respond(c, 405, "text/plain", "read-only portal: GET and the two inbox POSTs only\n");
        else {
            const std::string target = req.substr(4, req.find(' ', 4) - 4);
            const std::string path = target.substr(0, target.find('?'));
            if (onSite && path.rfind("/site/", 0) != 0) respond(c, 404, "text/plain", "this port serves /site/<workflow>/ only\n");
            else if (path == "/" || path == "/index.html") {
                std::string page = portalPage();
                const std::string ph = "__PORTAL_TOKEN__";
                for (auto at = page.find(ph); at != std::string::npos; at = page.find(ph, at + token.size())) page.replace(at, ph.size(), token);
                respond(c, 200, "text/html; charset=utf-8", page);
            }
            else if (path == "/api/state") { std::string cached; { std::lock_guard lock(live.m); cached = live.stateJson; } respond(c, 200, "application/json", cached); }
            else if (path == "/api/live") {
                std::size_t n = 120;
                if (const auto q = target.find("n="); q != std::string::npos) n = std::clamp<long>(std::atol(target.c_str() + q + 2), 10, static_cast<long>(kKeep));
                respond(c, 200, "application/json", liveJson(live, n));
            }
            else if (path == "/api/files") {  // the packages of one workflow: its files and its deliverables manifest
                const std::string root = sandboxOf(live, queryParam(target, "wf"));
                if (root.empty()) respond(c, 404, "text/plain", "unknown workflow\n"); else respond(c, 200, "application/json", filesJson(root));
            }
            else if (path == "/api/search") {  // what the workflow already did: conversations, gate outputs, briefings, sizing; read only
                const std::string root = sandboxOf(live, queryParam(target, "wf"));
                if (root.empty()) respond(c, 404, "text/plain", "unknown workflow\n");
                else {
                    json::Writer w;
                    w.beginObject().key("hits").beginArray();
                    for (const auto& h : recallSearch(root, queryParam(target, "q"), 40, queryParam(target, "item")))
                        w.beginObject().kv("p", h.path).kv("kind", h.kind).kv("line", h.line).kv("score", h.score).kv("text", h.text).endObject();
                    w.endArray().endObject();
                    respond(c, 200, "application/json", w.take());
                }
            }
            else if (path == "/api/file") {  // one file as plain text (the request, a prompt, a reply, a gate output, an artifact), capped
                const std::string root = sandboxOf(live, queryParam(target, "wf"));
                fs::path full;
                if (root.empty() || !(jobFilePath(root, queryParam(target, "p"), full) || insideSandbox(root, queryParam(target, "p"), full)) || !fs::is_regular_file(full)) respond(c, 404, "text/plain", "not found\n");
                else {
                    std::string body = slurp(full);
                    const long tailBytes = std::atol(queryParam(target, "tail").c_str());   // a live transcript wants the END of a long file, not its first 400 KB
                    if (tailBytes > 0 && body.size() > static_cast<std::size_t>(tailBytes)) {
                        std::size_t from = body.size() - static_cast<std::size_t>(tailBytes);
                        const auto nl = body.find('\n', from);
                        if (nl != std::string::npos) from = nl + 1;
                        body = "[... earlier text omitted: showing the last " + std::to_string(body.size() - from) + " bytes]\n" + body.substr(from);
                    }
                    const bool cut = body.size() > 400000;
                    if (cut) body.resize(400000);
                    // A transcript shows the turn that is being generated too: the agent streams it into live.txt beside the file and removes it when the turn ends.
                    if (full.filename() == "conversation.md") {
                        const std::string live = slurp(full.parent_path() / "live.txt");
                        if (!live.empty()) body += "\n### In flight (streaming now, not yet part of the conversation)\n\n" + live;
                    }
                    respond(c, 200, "text/plain; charset=utf-8", body + (cut ? "\n[... cut at 400 KB]\n" : ""));
                }
            }
            else if (onSite && path.rfind("/site/", 0) != 0) respond(c, 404, "text/plain", "this port serves /site/<workflow>/ only\n");
            else if (onSite) {  // the integrated project served as a site, in an opaque sandboxed origin with no network
                const std::string rest = urlDecode(path.substr(6));
                const auto slash = rest.find('/');
                const std::string wf = rest.substr(0, slash);
                std::string rel = slash == std::string::npos ? "" : rest.substr(slash + 1);
                if (rel.empty() || rel.back() == '/') rel += "index.html";
                const std::string root = sandboxOf(live, wf);
                fs::path full;
                if (root.empty() || !insideSandbox(fs::path(root) / "project", rel, full) || !fs::is_regular_file(full)) respond(c, 404, "text/plain", "not found\n");
                else respond(c, 200, mimeOf(full.string()), slurp(full), ("Content-Security-Policy: default-src http://127.0.0.1:" + std::to_string(port + 1) + " 'unsafe-inline'; connect-src 'none'\r\n").c_str());
            }
            else if (path == "/healthz") respond(c, 200, "text/plain", "ok\n");
            else respond(c, 404, "text/plain", "not found\n");
        }
        ::close(c);
    }
}

}  // namespace sub0squad
