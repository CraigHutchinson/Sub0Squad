// Waiting for several things at once (platform_wait.hpp; docs/PLATFORM.md) and the process pieces the shim stands on (platform_process.hpp): a
// timeout, a wake from another thread, a socket that becomes readable and writable, a peer that hangs up, a child's exit with its status, a stop
// request to this process and to a child (terminateProcess), separate stdout and stderr pipes, the event pipe, and a replaced environment.
#include "check.hpp"
#include "sub0squad/platform_net.hpp"
#include "sub0squad/platform_proc.hpp"
#include "sub0squad/platform_process.hpp"
#include "sub0squad/platform_wait.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <thread>

using namespace sub0squad::platform;
using namespace std::chrono_literals;

namespace {

const std::string kChild = SUB0SQUAD_PLATFORM_CHILD;

/// Read a pipe until every writer closed it or 10 s passed.
std::string drain(Pipe& p)
{
    std::string got;
    char buf[4096];
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto n = p.readSome(buf, sizeof buf, 100ms);
        if (!n) break;
        if (!*n) continue;
        if (**n == 0) break;
        got.append(buf, **n);
    }
    return got;
}

/// Wait (up to 10 s) for an event of `kind`; the events seen on the way are kept in `seen`.
std::optional<WaitEvent> waitFor(Waiter& w, WaitEvent::Kind kind, std::vector<WaitEvent>* seen = nullptr)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto events = w.wait(500ms);
        if (!events) return std::nullopt;
        for (const auto& e : *events) {
            if (seen) seen->push_back(e);
            if (e.kind == kind) return e;
        }
    }
    return std::nullopt;
}

}  // namespace

int main()
{
    auto created = Waiter::create();
    CHECK(created.has_value());
    if (!created) return finish();
    Waiter& w = *created;

    {   // nothing watched: the timeout passes and the result is empty
        const auto t0 = std::chrono::steady_clock::now();
        auto events = w.wait(50ms);
        CHECK(events.has_value() && events->empty());
        CHECK(std::chrono::steady_clock::now() - t0 >= 40ms);
    }
    {   // a wake from another thread ends a wait with no timeout; a wake before the wait is not lost
        std::jthread waker([&] { std::this_thread::sleep_for(50ms); w.wake(); });
        auto events = w.wait();
        CHECK(events.has_value() && events->size() == 1 && events->front().kind == WaitEvent::Kind::woken);
        w.wake();
        auto again = w.wait(5s);
        CHECK(again.has_value() && again->size() == 1 && again->front().kind == WaitEvent::Kind::woken);
    }
    {   // sockets: a connection to accept, data to read, room to write, then the peer's hang-up
        auto l = listenLoopback(0);
        CHECK(l.has_value());
        if (l) {
            CHECK(w.watchSocket(l->native(), true, false, 11).has_value());
            auto c = connectTcp("127.0.0.1", l->port());
            CHECK(c.has_value());
            auto e = waitFor(w, WaitEvent::Kind::socket);
            CHECK(e && e->tag == 11 && e->readable);
            auto s = l->accept(1s);
            CHECK(s.has_value() && s->has_value());
            w.unwatchSocket(l->native());
            if (c && s && *s) {
                SocketStream& server = **s;
                CHECK(w.watchSocket(server.native(), true, false, 12).has_value());
                auto quiet = w.wait(50ms);   // nothing sent yet
                CHECK(quiet.has_value() && quiet->empty());
                CHECK(c->writeAll("hello").has_value());
                e = waitFor(w, WaitEvent::Kind::socket);
                CHECK(e && e->tag == 12 && e->readable && !e->writable);
                char buf[16];
                auto n = server.readSome(buf, sizeof buf, 0ms);
                CHECK(n && *n && **n == 5);
                CHECK(w.watchSocket(server.native(), true, true, 13).has_value());   // watching again changes what is watched
                e = waitFor(w, WaitEvent::Kind::socket);
                CHECK(e && e->tag == 13 && e->writable);
                auto wrote = server.writeSome("reply");
                CHECK(wrote.has_value() && *wrote == 5);
                CHECK(w.watchSocket(server.native(), true, false, 14).has_value());
                *c = SocketStream{};   // the peer closes
                e = waitFor(w, WaitEvent::Kind::socket);
                CHECK(e && e->tag == 14 && (e->readable || e->hangup));
                auto end = server.readSome(buf, sizeof buf, 0ms);
                CHECK(end && *end && **end == 0);
                w.unwatchSocket(server.native());
            }
        }
    }
    {   // a child's exit, with its status
        auto c = spawn(ProcessSpec{.argv = {kChild, "sleep", "100"}});
        CHECK(c.has_value());
        if (c) {
            CHECK(w.watchExit(c->id(), 21));
            auto e = waitFor(w, WaitEvent::Kind::exited);
            CHECK(e && e->tag == 21 && e->status && e->status->exited() && e->status->code == 0);
        }
        auto code = spawn(ProcessSpec{.argv = {kChild, "exit", "7"}});
        if (code) {
            if (w.watchExit(code->id(), 23)) {
                auto e = waitFor(w, WaitEvent::Kind::exited);
                CHECK(e && e->tag == 23 && e->status && e->status->code == 7);
            } else {
                CHECK(code->wait().value_or(ExitStatus{}).code == 7);   // it ended before it was watched
            }
        }
    }
    {   // a stop request to this process reaches the waiter instead of ending it
        CHECK(w.watchStopRequests(31).has_value());
        CHECK(terminateProcess(currentProcess()));
        auto e = waitFor(w, WaitEvent::Kind::stopRequested);
        CHECK(e && e->tag == 31);
    }
    {   // a stop request to a child that watches for them: it ends itself in order (Windows: its stop event, not TerminateProcess)
        auto c = spawn(ProcessSpec{.argv = {kChild, "stoppable", "10000"}, .pipeOutput = true});
        CHECK(c.has_value());
        if (c) {
            Pipe out = c->takeStdout();
            std::string said;
            char buf[64];
            for (int i = 0; i < 100 && said.find("ready") == std::string::npos; ++i)
                if (auto n = out.readSome(buf, sizeof buf, 100ms); n && *n) said.append(buf, **n);
            CHECK(said.find("ready") != std::string::npos);
            CHECK(terminateProcess(c->id()));
            said += drain(out);
            CHECK(said.find("stopped") != std::string::npos);
            CHECK(c->wait(10s).value_or(ExitStatus{}).code == 0);
        }
    }
    {   // separate stdout and stderr pipes
        auto c = spawn(ProcessSpec{.argv = {kChild, "outerr", "to-out", "to-err"}, .pipeOutput = true});
        CHECK(c.has_value());
        if (c) {
            Pipe out = c->takeStdout(), err = c->takeStderr();
            CHECK(out.isOpen() && err.isOpen() && !c->takeEvents().isOpen());
            CHECK(drain(out).find("to-out") != std::string::npos);
            CHECK(drain(err).find("to-err") != std::string::npos);
            CHECK(c->wait(10s).value_or(ExitStatus{}).code == 0);
        }
    }
    {   // the event pipe: the child finds it named by the variable and writes a line to it
        auto c = spawn(ProcessSpec{.argv = {kChild, "event", "S0Q_TEST_EVENTS", "{\"n\":1}"}, .eventPipeVariable = "S0Q_TEST_EVENTS"});
        CHECK(c.has_value());
        if (c) {
            Pipe events = c->takeEvents();
            CHECK(drain(events) == "{\"n\":1}\n");
            CHECK(c->wait(10s).value_or(ExitStatus{}).code == 0);
        }
    }
    {   // a replaced environment: only what the spec names (plus what a Windows program needs to start)
        setEnvironment("S0Q_TEST_INHERITED", "yes");
        std::vector<std::string> env{"S0Q_TEST_GIVEN=given"};
        for (const char* key : {"SYSTEMROOT", "PATH"})
            if (const char* v = std::getenv(key)) env.push_back(std::string{key} + "=" + v);
        auto given = spawn(ProcessSpec{.argv = {kChild, "env", "S0Q_TEST_GIVEN"}, .environment = env, .pipeOutput = true});
        auto inherited = spawn(ProcessSpec{.argv = {kChild, "env", "S0Q_TEST_INHERITED"}, .environment = env, .pipeOutput = true});
        CHECK(given.has_value() && inherited.has_value());
        if (given && inherited) {
            Pipe g = given->takeStdout(), i = inherited->takeStdout();
            CHECK_EQ(drain(g), std::string{"given"});
            CHECK_EQ(drain(i), std::string{});
            (void)given->wait(10s);
            (void)inherited->wait(10s);
        }
        unsetEnvironment("S0Q_TEST_INHERITED");
    }
    return finish();
}
