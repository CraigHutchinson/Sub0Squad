// Sockets and the instance lock (platform_net.hpp, platform_ipc.hpp, platform_file.hpp FileLock; docs/PLATFORM.md): a loopback round trip, reads
// and accepts that wait with a deadline, the end of a stream, waiting on two listeners, a port already held, the local control socket with "no
// server" told apart, the peer's user, and a lock that a second taker cannot get. Everything runs inside this process on ephemeral ports.
#include "check.hpp"
#include "sub0squad/platform_file.hpp"
#include "sub0squad/platform_ipc.hpp"
#include "sub0squad/platform_net.hpp"

#include <string>

using namespace sub0squad::platform;
using namespace std::chrono_literals;

namespace {

/// Read until `want` bytes arrived, the peer closed, or 5 s passed.
std::string readAll(SocketStream& s, std::size_t want)
{
    std::string got;
    char buf[4096];
    while (got.size() < want) {
        auto n = s.readSome(buf, sizeof buf, 5s);
        if (!n || !*n || **n == 0) break;
        got.append(buf, **n);
    }
    return got;
}

}  // namespace

int main()
{
    {   // loopback: a round trip, a read that times out, the end of the stream
        auto l = listenLoopback(0);
        CHECK(l.has_value() && l->port() != 0);
        if (l) {
            auto none = l->accept(0ms);   // nobody is connecting
            CHECK(none.has_value() && !none->has_value());
            auto c = connectTcp("127.0.0.1", l->port());
            CHECK(c.has_value());
            auto a = l->accept(5s);
            CHECK(a.has_value() && a->has_value());
            if (c && a && *a) {
                SocketStream& server = **a;
                const std::string big(300000, 'x');   // more than a socket buffer: writeAll must wait and carry on
                std::thread writer([&] { CHECK(c->writeAll("hello " + big).has_value()); });
                CHECK(readAll(server, 6 + big.size()) == "hello " + big);
                writer.join();
                char b[16];
                const auto began = std::chrono::steady_clock::now();
                auto quiet = server.readSome(b, sizeof b, 100ms);
                CHECK(quiet.has_value() && !quiet->has_value());   // nothing came: nullopt, not an error and not the end
                CHECK(std::chrono::steady_clock::now() - began >= 90ms);
                CHECK(server.writeAll("back").has_value());
                CHECK(readAll(*c, 4) == "back");
                *c = SocketStream{};   // the client closes
                auto end = server.readSome(b, sizeof b, 5s);
                CHECK(end.has_value() && end->has_value() && **end == 0);
            }
            CHECK(loopbackPortInUse(l->port()));
            auto again = listenLoopback(l->port());   // the port is held: refused on every system
            CHECK(!again.has_value());
        }
        CHECK(!connectTcp("127.0.0.1", 1).has_value());   // nothing listens on port 1 on a build host
        auto bad = connectTcp("s0q-no-such-host.invalid", 80);
        CHECK(!bad.has_value() && bad.error().message.find("s0q-no-such-host.invalid") != std::string::npos);
    }
    {   // waiting on two listeners: the one with a connection is named; none within the timeout is nullopt
        auto a = listenLoopback(0), b = listenLoopback(0);
        CHECK(a.has_value() && b.has_value());
        if (a && b) {
            const SocketListener* both[] = {&*a, &*b};
            CHECK(!waitForConnection(both, 50ms).has_value());
            auto c = connectTcp("127.0.0.1", b->port());
            CHECK(c.has_value());
            const auto which = waitForConnection(both, 5s);
            CHECK(which.has_value() && *which == 1);
        }
    }
    const auto dir = makeTempDir("platform-net");
    {   // the local control socket
        const auto path = dir / "control.sock";
        auto missing = connectLocal(path);
        CHECK(!missing.has_value() && missing.error().noServer);   // no socket file: "start the daemon", not a fault
        auto l = listenLocal(path);
        CHECK(l.has_value());
        if (l) {
            auto c = connectLocal(path);
            CHECK(c.has_value());
            auto a = l->accept(5s);
            CHECK(a.has_value() && a->has_value());
            if (c && a && *a) {
                CHECK(peerIsSameUser(**a));
                CHECK(!peerUser(**a).empty());
                CHECK(c->writeAll("ping").has_value());
                CHECK(readAll(**a, 4) == "ping");
            }
        }
    }
    {   // a socket path longer than sockaddr_un's 107 bytes (a deep worktree, a long temp directory): POSIX reaches it through /proc/self/fd, Windows
        // moves it to a short stable name; either way a daemon and its client meet
        const auto deep = dir / std::string(120, 'd');
        std::filesystem::create_directories(deep);
        const auto path = deep / "control.sock";
        CHECK(path.string().size() > 107);
        const auto file = localSocketFile(path);
        CHECK(file.has_value() && localSocketFile(path).value_or(std::filesystem::path{}) == *file);   // stable: the client computes the same name as the daemon
        if (file) CHECK(*file == path || file->string().size() <= 107);
        auto missing = connectLocal(path);
        CHECK(!missing.has_value() && missing.error().noServer);
        {   // in its own scope: the listener is closed before the moved name is removed
            auto l = listenLocal(path);
            CHECK(l.has_value());
            if (l) {
                auto c = connectLocal(path);
                CHECK(c.has_value());
                if (!c) std::fprintf(stderr, "connect to the moved name %s: %s\n", file ? file->string().c_str() : "?", c.error().error.message.c_str());
                auto a = l->accept(5s);
                CHECK(a.has_value() && a->has_value());
                if (c && a && *a) {
                    CHECK(c->writeAll("deep").has_value());
                    CHECK(readAll(**a, 4) == "deep");
                }
            }
        }
        if (file && *file != path) CHECK(sub0squad::platform::removeTree(*file).has_value());   // the moved name is outside `dir`
    }
    {   // the instance lock: the first taker has it, a second does not, and it is free again once released
        const auto path = dir / "instance.lock";
        auto first = FileLock::tryExclusive(path);
        CHECK(first.has_value() && first->has_value() && (*first)->isHeld());
        auto second = FileLock::tryExclusive(path);
        CHECK(second.has_value() && !second->has_value());
        if (first) first->reset();
        auto third = FileLock::tryExclusive(path);
        CHECK(third.has_value() && third->has_value());
        CHECK(!FileLock::tryExclusive(dir / "no" / "such" / "dir" / "lock").has_value());
    }
    {   // the tree still holds the local socket's file: removing it opens no file (std::filesystem::remove_all blocked on one for ever on
        // Windows Server 2022), and a tree that is already gone is success
        CHECK(sub0squad::platform::removeTree(dir).has_value() && !std::filesystem::exists(dir));
        CHECK(sub0squad::platform::removeTree(dir).has_value());
    }
    return finish();
}
