"""Run isolated T0 repair or reply-continuation trials against a local llama.cpp server."""

import argparse
import copy
import hashlib
import http.client
import json
import math
import os
import pathlib
import random
import re
import secrets
import sqlite3
import subprocess
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

import gate_t0


ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "out/runs/t0-iterative"
CASE = "half-written"
SOURCE = "src/calc.cpp"
OPTIONS = ("decompose", "sections", "rearchitect", "thinkFirst", "escalate", "strategies")
DRIVER = r'''
#include "calc/calc.hpp"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
int main() {
    std::string line;
    std::cout << std::setprecision(17);
    while (std::getline(std::cin, line)) {
        try {
            const auto value = calc::evaluate(line.substr(2), line[0] == '1');
            std::cout << "value " << value << '\n';
        } catch (const std::runtime_error&) {
            std::cout << "runtime_error\n";
        } catch (...) {
            std::cout << "other_exception\n";
        }
    }
}
'''


def encode(obj):
    return json.dumps(obj, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()


def digest(data):
    return hashlib.sha256(data if isinstance(data, bytes) else encode(data)).hexdigest()


def claim(ledger, key, run_id):
    """Atomically claim effective inputs. Return an existing trial ID when already claimed."""
    with sqlite3.connect(ledger) as conn:
        conn.execute("CREATE TABLE IF NOT EXISTS trials (key TEXT PRIMARY KEY, id TEXT NOT NULL)")
        inserted = conn.execute("INSERT OR IGNORE INTO trials VALUES (?, ?)", (key, run_id))
        if not inserted.rowcount:
            return conn.execute("SELECT id FROM trials WHERE key = ?", (key,)).fetchone()[0]
    return None


def save(path, obj):
    pending = path.with_suffix(path.suffix + ".tmp")
    pending.write_bytes(encode(obj) + b"\n")
    pending.replace(path)


def run(argv, cwd, log, *, env=None, timeout=120, input_text=None):
    p = subprocess.run([str(a) for a in argv], cwd=cwd, env=env, timeout=timeout,
                       input=input_text, capture_output=True, text=True, encoding="utf-8", errors="replace")
    with log.open("a", encoding="utf-8") as f:
        f.write(f"{argv!r} => {p.returncode}\n{p.stdout}\n{p.stderr}\n")
    return p


def require(p):
    if p.returncode:
        raise RuntimeError(f"command exited {p.returncode}: {(p.stderr or p.stdout)[-500:]}")
    return p


def source(plan):
    return next(f["content"] for f in plan["scaffold"] if f["path"] == SOURCE)


def materialize(plan, path, candidate):
    path.mkdir(parents=True)
    save(path / "plan.json", plan)
    output = path / "items/app/attempt-1/out" / SOURCE
    output.parent.mkdir(parents=True)
    output.write_bytes(candidate)


def gate(exe, folder, log):
    result = run([exe, "cpp-gate", "--workflow-dir", folder, "--item", "app", "--attempt", "1", "--no-publish", "1"],
                 folder, log, timeout=210)
    return result, (folder / "items/app/attempt-1/gate.txt").read_text(encoding="utf-8")


def preflight(exe, plan, reference, private):
    log = private / "preflight.log"
    for name, data in (("seed", source(plan).encode()), ("known-good", reference.encode())):
        folder = private / name
        materialize(plan, folder, data)
        result, message = gate(exe, folder, log)
        if name == "seed" and (not result.returncode or not re.search(r"end of (input|file)", message, re.I)):
            raise RuntimeError("seed did not fail at end of input; inspect private preflight log")
        if name == "known-good" and (result.returncode or "(16 tests," not in message):
            raise RuntimeError("known-good completion failed the unchanged 16-test gate")
    folder = private / "out-of-scope"
    materialize(plan, folder, reference.encode())
    attempt = folder / "items/app/attempt-1/out"
    (attempt / "outside.txt").write_text("must be refused", encoding="utf-8")
    result, message = gate(exe, folder, log)
    if not result.returncode or "outside the item's editable files" not in message:
        raise RuntimeError("gate failed to refuse an out-of-scope path")


def probes(seed):
    rng = random.Random(seed)
    cases = []
    for _ in range(8):
        n, m = rng.sample(range(3, 40), 2)
        angle = rng.randint(11, 79)
        cases.extend(((False, f"-{n}^2+{m}", -(n ** 2) + m),
                      (True, f"sin({angle})+cos({90-angle})", 2 * math.sin(math.radians(angle))),
                      (False, f"ln(exp({n}/10))+min({n},{m})", n / 10 + min(n, m)),
                      (False, f"sqrt({n*n})+{m}%{n}", n + m % n),
                      (False, f"max({n},sqrt(-{m}))", None),
                      (False, f"cos({n},{m})", None)))
    return cases


def owner_test_count(plan):
    test_paths = set(plan["items"][0]["gate"]["tests"])
    return sum(len(re.findall(r"^TEST\(", f["content"], re.MULTILINE))
               for f in plan["given"] if f["path"] in test_paths)


def check_t1_seed(exe, plan, private):
    folder = private / "seed"
    materialize(plan, folder, source(plan).encode())
    result, message = gate(exe, folder, private / "seed.log")
    expected = owner_test_count(plan)
    match = re.match(r"GATE FAIL: ([1-9]\d*) of (\d+) tests failed", message)
    if not result.returncode or not match or int(match[2]) != expected:
        raise RuntimeError(f"T1 seed must compile and fail behavior tests: {message.splitlines()[0]}")
    return int(match[1])


def private_check(exe, plan, ref, trial, private, seed, prompt, before, manifest):
    """Check exact accepted bytes in a fresh workspace, separate from the agent packet."""
    if manifest() != before:
        return "REJECT", "pinned harness inputs changed"
    workflow = trial / "home/state/workflows/w-000001"
    received = json.loads((workflow / "plan.json").read_text(encoding="utf-8"))
    if received != plan:
        return "REJECT", "effective workflow plan differs from the frozen plan"
    accepted = workflow / "items/app/accepted"
    found = sorted(f.relative_to(accepted).as_posix() for f in accepted.rglob("*") if f.is_file())
    if found != [SOURCE]:
        return "REJECT", "accepted output is outside the one-file contract"
    artifact = (accepted / SOURCE).read_bytes()
    if artifact != (workflow / "project" / SOURCE).read_bytes():
        return "REJECT", "accepted and published bytes differ"
    if artifact != (workflow / "items/app/attempt-1/out" / SOURCE).read_bytes():
        return "REJECT", "accepted bytes differ from the worker output"
    # Screen verbatim reference leakage in actual worker requests. Only the proposed system
    # addition is screened for semantic leakage by the separately owned prompt review.
    seed_source = source(plan)
    suffix = ref[len(seed_source):].splitlines() if ref.startswith(seed_source) else []
    request_files = sorted((trial / "requests").glob("*.json"))
    if not request_files:
        return "INDETERMINATE", "no captured worker request"
    initial = json.loads(request_files[0].read_text(encoding="utf-8"))
    for message in initial["messages"]:
        if any("\n".join(suffix[i:i + 5]) in message.get("content", "")
               for i in range(len(suffix) - 4)):
            return "REJECT", "initial request contains a block of missing reference code"
    if prompt and not initial["messages"][0]["content"].endswith(prompt):
        return "INDETERMINATE", "effective prompt addition was not delivered"
    for request_file in request_files[1:]:
        request = json.loads(request_file.read_text(encoding="utf-8"))
        if request["messages"][0] != initial["messages"][0]:
            return "REJECT", "system message changed inside the trial"
    folder = private / "accepted"
    materialize(plan, folder, artifact)
    log = private / "checker.log"
    result, msg = gate(exe, folder, log)
    if result.returncode or f"({owner_test_count(plan)} tests," not in msg:
        return "REJECT", "fresh owner gate did not reproduce acceptance"
    workspace = folder / "items/app/attempt-1/ws"
    (workspace / "checker.cpp").write_text(DRIVER, encoding="utf-8")
    driver = workspace / ("checker.exe" if os.name == "nt" else "checker")
    require(run(["g++", "-std=c++20", "-D_USE_MATH_DEFINES", "-Iinclude", SOURCE, "checker.cpp", "-o", driver],
                workspace, log, timeout=60))
    cases = probes(seed)
    result = run([driver], workspace, log, timeout=15,
                 input_text="".join(f"{int(degrees)} {expr}\n" for degrees, expr, _ in cases))
    lines = result.stdout.splitlines()
    if result.returncode or len(lines) != len(cases):
        return "REJECT", "independent calculator probe did not complete cleanly"
    for (_, _, expected), line in zip(cases, lines):
        if expected is None and line == "runtime_error":
            continue
        try:
            if expected is not None and line.startswith("value ") and math.isclose(float(line[6:]), expected, rel_tol=1e-10, abs_tol=1e-10):
                continue
        except ValueError:
            pass
        return "REJECT", "independent calculator probe failed"
    save(private / "artifact.json", {"sha256": digest(artifact), "probes": len(cases)})
    return "PASS", "fresh owner gate and independent calculator probes passed"


def reveal_t1_region(request, plan, symbol):
    """Expand one folded region using the seed's existing lines and numbering."""
    user = request["messages"][-1]
    marker = re.compile(r"(?m)^ +\.\.\. \(\d+ lines folded: (\d+)-(\d+); " + re.escape(symbol) + r"\)$")
    match = marker.search(user["content"]) if user["role"] == "user" else None
    if not match:
        raise ValueError(f"initial view did not contain a fold for {symbol}")
    first, last = map(int, match.groups())
    lines = source(plan).splitlines()
    if not 1 <= first <= last <= len(lines):
        raise ValueError("fold range does not match the seed")
    revealed = "\n".join(f"{n:5}| {lines[n-1]}" for n in range(first, last + 1))
    user["content"] = user["content"][:match.start()] + revealed + user["content"][match.end():]


def proxy(trial, target, prompt, scripted_reply=None, first_reply_tokens=None, expose_initial=None):
    requests = trial / "requests"
    requests.mkdir()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"
        counter = 0
        lock = threading.Lock()

        def log_message(self, fmt, *args):
            pass

        def do_POST(self):
            if self.path != "/v1/chat/completions":
                self.send_error(404)
                return
            connection = http.client.HTTPConnection(target.hostname, target.port, timeout=240)
            headers_sent = False
            try:
                request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                message = request["messages"][0]
                if message["role"] != "system" or not isinstance(message["content"], str):
                    raise ValueError("expected a leading system message")
                if prompt:
                    message["content"] += "\n\n" + prompt
                request["cache_prompt"] = False
                with self.lock:
                    index = Handler.counter
                    Handler.counter += 1
                if index == 0 and expose_initial is not None:
                    expose_initial(request)
                if index == 0 and first_reply_tokens is not None:
                    request["max_tokens"] = first_reply_tokens
                save(requests / f"{index:04}.json", request)
                if scripted_reply is not None:
                    self.send_response(200)
                    self.send_header("Content-Type", "text/event-stream")
                    self.end_headers()
                    headers_sent = True
                    events = ({"choices": [{"delta": {"content": scripted_reply}, "finish_reason": None}]},
                              {"choices": [{"delta": {}, "finish_reason": "stop"}]})
                    for event in events:
                        self.wfile.write(b"data: " + encode(event) + b"\n\n")
                    self.wfile.write(b"data: [DONE]\n\n")
                    return
                connection.request("POST", target.path + "/chat/completions", encode(request),
                                   {"Content-Type": "application/json", "Connection": "close"})
                response = connection.getresponse()
                if response.status >= 400:
                    raise RuntimeError(f"upstream returned HTTP {response.status}")
                self.send_response(response.status)
                self.send_header("Content-Type", response.getheader("Content-Type", "application/json"))
                self.end_headers()
                headers_sent = True
                while chunk := response.read1(8192):
                    try:
                        self.wfile.write(chunk)
                        self.wfile.flush()
                    except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                        # The canvas guard closes the client when it stops a runaway reply.
                        break
            except (OSError, ValueError, KeyError, RuntimeError, http.client.HTTPException) as e:
                with (trial / "proxy-errors.log").open("a", encoding="utf-8") as f:
                    f.write(f"{type(e).__name__}: {e}\n")
                if not headers_sent:
                    self.send_error(502)
            finally:
                connection.close()

    return ThreadingHTTPServer(("127.0.0.1", 0), Handler)


def workflow(args, plan, trial, engine):
    home = trial / "home"
    home.mkdir()
    log = trial / "runner.log"
    plan_file = trial / "plan.json"
    save(plan_file, plan)
    env = dict(os.environ, SUB0SQUAD_HOME=str(home))
    for knob in ("SUB0SQUAD_CUTOFF", "SUB0SQUAD_ISSUE_LAYER", "SUB0SQUAD_ISSUE_INLINE"):
        env.pop(knob, None)

    def cli(*argv):
        return run([args.exe, *argv], home, log, env=env, timeout=35)

    require(cli("up", "--frontier", "off", "--no-portal", "--max-jobs", "1"))
    started = False
    try:
        settings = [option for name in OPTIONS for option in ("--set", f"build.leaf.allow.{name}=false")]
        require(cli("process", "add", "t0", *settings))
        task = plan["project"].removeprefix("t0-")
        require(cli("do", f"tier zero spike: {task}", "--agent", "llm", "--engine", engine,
                    "--executor", "canvas", "--process", "t0", "--max-local", "1",
                    "--max-tokens", str(args.max_tokens), "--repair-turns", str(args.repair_turns)))
        started = True
        require(cli("project", "plan", "w-000001", "--file", plan_file))
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            status = cli("project", "status", "w-000001", "--json")
            if status.returncode == 0:
                data = json.loads(status.stdout)["workflows"][0]
                if data["state"] in ("succeeded", "failed", "cancelled"):
                    save(trial / "workflow.json", data)
                    started = False
                    return data["state"]
            time.sleep(3)
        raise TimeoutError("workflow limit exceeded")
    finally:
        try:
            if started:
                cli("project", "cancel", "w-000001")
        finally:
            cli("down")


def execute(args, plan, trial, target, prompt, scripted_reply=None):
    first_reply_tokens = args.first_reply_tokens if args.mode == "continuation" else None
    expose_initial = None
    if args.t1_visible:
        def expose_initial(request):
            reveal_t1_region(request, plan, args.t1_visible)

    with proxy(trial, target, prompt, scripted_reply, first_reply_tokens, expose_initial) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        engine = f"{args.model}=http://127.0.0.1:{server.server_port}/v1,model={args.model},ctx={args.ctx},slots=1,seed={args.seed},cache=0"
        try:
            return workflow(args, plan, trial, engine)
        finally:
            server.shutdown()
            thread.join()


def prepare(args):
    import sys

    original = sys.argv
    try:
        sys.argv = [original[0], str(ROOT)]
        import make_t0
    finally:
        sys.argv = original
    if args.t1_source:
        plan = json.loads((ROOT / "validation/tiers/t1-plan.json").read_text(encoding="utf-8"))
        plan["project"] = "t1-repair"
        item = plan["items"][0]
        item["files"] = [SOURCE]
        item["card"]["context"] = "Repair the existing scientific calculator. The command-line frontend is fixed."
        item["card"]["notes"] = ["Output only src/calc.cpp."]
        updated = {f"src/{name}": (args.t1_source / name).read_text(encoding="utf-8")
                   for name in ("calc.cpp", "main.cpp")}
        scaffold = {"include/calc/calc.hpp": make_t0.HEADER}
        scaffold.update((f["path"], f["content"]) for f in plan.get("scaffold", []))
        scaffold.update(updated)
        plan["scaffold"] = [{"path": name, "content": text} for name, text in scaffold.items()]
        return plan, make_t0.CALC, make_t0.REF
    case = "fix-op" if args.mode == "continuation" else CASE
    plan = make_t0.plan_for(case)
    ref = make_t0.CALC
    if args.cut is not None:
        lines = ref.splitlines(keepends=True)
        if not 1 <= args.cut < len(lines):
            raise ValueError("cut must be within the reference before its last line")
        plan = copy.deepcopy(plan)
        next(f for f in plan["scaffold"] if f["path"] == SOURCE)["content"] = "".join(lines[:args.cut])
        plan["items"][0]["card"]["context"] += " Resume from the provided editable file."
        if args.handoff:
            plan["items"][0]["card"]["context"] += " " + args.handoff.read_text(encoding="utf-8").strip()
    return plan, ref, make_t0.REF


def backend_profile(target):
    connection = http.client.HTTPConnection(target.hostname, target.port, timeout=10)
    try:
        connection.request("GET", "/props")
        response = connection.getresponse()
        if response.status != 200:
            raise RuntimeError(f"engine properties returned HTTP {response.status}")
        props = json.loads(response.read())
    finally:
        connection.close()
    return {"modelPath": props.get("model_path"), "slots": props["total_slots"],
            "slotContext": props["default_generation_settings"]["n_ctx"],
            "build": props.get("build_info")}


def continuation_evidence(trial):
    """Require the first real reply to be truncated and the next request to resume it."""
    attempt = trial / "home/state/workflows/w-000001/items/app/attempt-1"
    try:
        trace = (attempt / "conversation.md").read_text(encoding="utf-8")
        second = json.loads((trial / "requests/0001.json").read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None
    if not re.search(r"^### Reply, turn 0 .*finish `length`", trace, re.MULTILINE):
        return None
    if not re.search(r"^cut off at the token limit .*asked to continue \(continuation 1\)", trace, re.MULTILINE):
        return None
    last = second["messages"][-1]
    if last["role"] == "assistant":
        return "assistant-prefill"
    if last["role"] == "user" and last["content"].startswith("Your reply was cut off at the token limit."):
        return "tail-fallback"
    return None


def write_review(trial, identity, result):
    attempt = "home/state/workflows/w-000001/items/app/attempt-1"
    public_evidence = [f"{attempt}/{name}" for name in ("conversation.md", "gate.txt", "failures.txt", "agent.json")]
    save(trial / "review.json", {"trial": identity["trial"], "parent": identity.get("parent"),
                                "public": result["public"], "systemAddition": identity["prompt"],
                                "evidence": [name for name in public_evidence if (trial / name).is_file()]})


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("mode", choices=("preflight", "run", "continuation", "review"))
    ap.add_argument("--exe", type=pathlib.Path, default=ROOT / "out/build/mingw/sub0squad.exe")
    ap.add_argument("--endpoint", default="http://127.0.0.1:8092/v1")
    ap.add_argument("--model", default="qwen35")
    ap.add_argument("--ctx", type=int, default=65536)
    ap.add_argument("--seed", type=int, default=4101)
    ap.add_argument("--t1-visible", help="expand one named fold in the initial T1 worker view")
    ap.add_argument("--t1-source", type=pathlib.Path, help="run a one-file T1 repair diagnostic from a directory with calc.cpp and main.cpp")
    ap.add_argument("--cut", type=int, help="prepare a companion resume context; omit for canonical T0")
    ap.add_argument("--handoff", type=pathlib.Path, help="owner-authored resume note for a companion cut")
    ap.add_argument("--prompt", type=pathlib.Path, help="UTF-8 text to append to the worker system prompt")
    ap.add_argument("--parent", help="previous trial ID for a Codex-authored generation")
    ap.add_argument("--first-reply-tokens", type=int, help="cap the first reply only in continuation mode (default: 64)")
    ap.add_argument("--max-tokens", type=int, default=3000)
    ap.add_argument("--repair-turns", type=int, default=2)
    ap.add_argument("--timeout", type=int, default=600)
    args = ap.parse_args()
    if args.mode == "review":
        if not args.parent or not re.fullmatch(r"\d{8}T\d{6}Z-[0-9a-f]{12}", args.parent):
            ap.error("review requires --parent with a finished trial ID")
        trial = OUT / args.parent
        try:
            identity = json.loads((trial / "manifest.json").read_text(encoding="utf-8"))
            result = json.loads((trial / "verdict.json").read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            ap.error(f"could not read the trial: {e}")
        if identity["mode"] not in ("run", "continuation"):
            ap.error("review expects a model trial")
        write_review(trial, identity, result)
        print(trial / "review.json")
        return 0
    if args.mode == "continuation":
        args.first_reply_tokens = 64 if args.first_reply_tokens is None else args.first_reply_tokens
        if not 0 < args.first_reply_tokens < args.max_tokens:
            ap.error("first reply cap must be positive and below the normal reply budget")
        if args.cut is not None or args.handoff:
            ap.error("continuation uses the unchanged fix-op fixture")
    elif args.first_reply_tokens is not None:
        ap.error("--first-reply-tokens is only for continuation")
    args.exe = args.exe.resolve(strict=True)
    target = urlsplit(args.endpoint)
    if target.scheme != "http" or target.hostname != "127.0.0.1" or not target.port or target.path != "/v1" or target.query:
        ap.error("endpoint must be http://127.0.0.1:PORT/v1")
    if not re.fullmatch(r"[A-Za-z0-9._-]+", args.model) or min(args.ctx - args.max_tokens, args.timeout, args.seed + 1) <= 0:
        ap.error("check model alias, context, generation limit, timeout and seed")
    if args.t1_visible and (not args.t1_source or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:]*", args.t1_visible)):
        ap.error("--t1-visible requires a T1 source and a C++ symbol")
    if args.t1_source and (args.mode != "run" or args.cut is not None or args.handoff):
        ap.error("--t1-source is only for a separate run without a resume cut or handoff")
    if args.handoff and args.cut is None:
        ap.error("the canonical fixture has a fixed handoff; use --handoff with --cut")
    try:
        plan, ref, ref_dir = prepare(args)
    except (OSError, ValueError) as e:
        ap.error(str(e))
    if args.parent and (not re.fullmatch(r"\d{8}T\d{6}Z-[0-9a-f]{12}", args.parent)
                        or not (OUT / args.parent / "verdict.json").is_file()):
        ap.error("parent must identify a finished sandbox trial")
    prompt = args.prompt.read_text(encoding="utf-8").strip() if args.prompt else ""
    if len(prompt) > 2400 or "```" in prompt or re.search(r"\b(?:return|throw)\s+.+;", prompt):
        ap.error("prompt addition must be short guidance without implementation code")
    inputs = [args.exe, *sorted(ROOT.glob("validation/tiers/*.py")),
              ROOT / "validation/tiers/t1-plan.json", ROOT / "validation/tiers/lib.sh",
              *[ROOT / f"src/{name}.cpp" for name in ("canvas_agent", "turn_control", "harness_cpp", "harness_common", "cpp_templates", "engine", "shim", "workflow")],
              *sorted(path for path in ref_dir.rglob("*") if path.is_file())]

    def manifest():
        return {os.path.relpath(f, ROOT): digest(f.read_bytes()) for f in inputs}

    before = manifest()
    backend = None
    if args.mode in ("run", "continuation"):
        try:
            backend = backend_profile(target)
        except (OSError, ValueError, KeyError, RuntimeError, http.client.HTTPException) as e:
            ap.error(f"could not verify the local server: {e}")
        if args.ctx > backend["slotContext"]:
            ap.error(f"requested context exceeds server slot context {backend['slotContext']}")
    identity = {"inputs": before, "plan": digest(plan), "prompt": prompt, "endpoint": args.endpoint,
                "model": args.model, "ctx": args.ctx, "seed": args.seed, "maxTokens": args.max_tokens,
                "repairTurns": args.repair_turns, "wallSeconds": args.timeout, "mode": args.mode, "backend": backend}
    if args.t1_visible:
        identity["visibleSymbol"] = args.t1_visible
    if args.mode == "continuation":
        identity["firstReplyTokens"] = args.first_reply_tokens
    key = digest(identity)
    run_id = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()) + "-" + uuid.uuid4().hex[:12]
    OUT.mkdir(parents=True, exist_ok=True)
    trial = OUT / run_id
    private = OUT / "checker" / run_id
    prior = claim(OUT / "ledger.sqlite3", key, run_id)
    if prior:
        print(f"already scheduled: {OUT / prior}", flush=True)
        return 3
    trial.mkdir()
    private.mkdir(parents=True)
    checker_seed = secrets.randbits(64)
    save(private / "seed.json", checker_seed)
    save(trial / "manifest.json", {"trial": key, "parent": args.parent, **identity})
    print(trial, flush=True)
    result = {"public": "INDETERMINATE", "tester": "INDETERMINATE"}
    try:
        if args.mode == "preflight":
            preflight(args.exe, plan, ref, private)
            if not ref.startswith(source(plan)):
                raise RuntimeError("prepared source is not a reference prefix")
            replay = private / "canvas-replay"
            replay.mkdir()
            n = len(source(plan).splitlines())
            text = ref[len(source(plan)):].rstrip("\n")
            reply = f"=== EDIT: {SOURCE} AFTER {n} ===\n{text}\n=== END EDIT ==="
            state = execute(args, plan, replay, target, "", reply)
            wf = replay / "home/state/workflows/w-000001"
            if state != "succeeded" or (wf / "project" / SOURCE).read_text(encoding="utf-8") != ref:
                raise RuntimeError("production canvas did not publish the exact scripted repair")
            tester, detail = private_check(args.exe, plan, ref, replay, private, checker_seed, "", before, manifest)
            if tester != "PASS":
                raise RuntimeError(f"known-good repair failed independent checks: {detail}")
            result = {"public": "PASS", "tester": "PASS", "detail": "seed, repair, allowlist and production canvas controls passed"}
        else:
            if args.t1_source:
                result["seedFailures"] = check_t1_seed(args.exe, plan, private)
            state = execute(args, plan, trial, target, prompt)
            info = gate_t0.judge_run(trial / "home", plan, ref_dir)
            result["gate"] = info
            if args.mode == "continuation":
                result["continuation"] = continuation_evidence(trial)
            if not list((trial / "requests").glob("*.json")) or (trial / "proxy-errors.log").exists():
                result["detail"] = "inference transport incomplete"
            elif args.mode == "continuation" and result["continuation"] is None:
                result["detail"] = "first reply did not exercise the required continuation path"
            elif state == "succeeded" and info["passed"] and info["key"] == gate_t0.case_key(plan):
                result["public"] = "PASS"
                tester, detail = private_check(args.exe, plan, ref, trial, private, checker_seed, prompt, before, manifest)
                result["tester"] = tester
                save(private / "verdict.json", {"verdict": tester, "detail": detail})
            elif state == "failed":
                result["public"] = "FAIL"
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.TimeoutExpired) as e:
        result["detail"] = str(e)
    save(trial / "verdict.json", result)
    if args.mode in ("run", "continuation"):
        write_review(trial, {"trial": key, "parent": args.parent, "prompt": prompt}, result)
    print(json.dumps(result, ensure_ascii=False), flush=True)
    return 0 if result["public"] == "PASS" and (args.mode == "preflight" or result["tester"] == "PASS") else 1


if __name__ == "__main__":
    raise SystemExit(main())
