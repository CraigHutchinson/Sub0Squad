#include "sub0squad/portal.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace sub0squad {

namespace {

void sendAll(int fd, const std::string& s)
{
    std::size_t off = 0;
    while (off < s.size()) {
        const ssize_t n = ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;
        off += static_cast<std::size_t>(n);
    }
}

void respond(int fd, int code, const char* type, const std::string& body)
{
    const char* text = code == 200 ? "OK" : code == 404 ? "Not Found" : "Method Not Allowed";
    sendAll(fd, "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n" + body);
}

}  // namespace

int servePortal(int port, const std::function<std::string()>& state)
{
    const int srv = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (srv < 0) { std::perror("portal: socket"); return 1; }
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);  // loopback only: the portal is never reachable from another machine
    if (::bind(srv, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(srv, 16) != 0) { std::perror("portal: bind/listen"); ::close(srv); return 1; }
    std::fprintf(stderr, "portal: http://127.0.0.1:%d/  (read-only, loopback only; Ctrl-C to stop)\n", port);
    for (;;) {
        const int c = ::accept4(srv, nullptr, nullptr, SOCK_CLOEXEC);
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
        if (req.rfind("GET ", 0) != 0) respond(c, 405, "text/plain", "read-only portal: GET only\n");
        else {
            const std::string path = req.substr(4, req.find(' ', 4) - 4);
            if (path == "/" || path == "/index.html") respond(c, 200, "text/html; charset=utf-8", portalPage());
            else if (path == "/api/state") respond(c, 200, "application/json", state());
            else if (path == "/healthz") respond(c, 200, "text/plain", "ok\n");
            else respond(c, 404, "text/plain", "not found\n");
        }
        ::close(c);
    }
}

}  // namespace sub0squad
