#!/usr/bin/env python3
"""Campaign driver: run the same squad project many times with different seeds and record what happened (docs/CAMPAIGN.md).

    scripts/campaign.py run    --name baseline --runs 20            # real model (the engine must be running)
    scripts/campaign.py run    --name smoke --runs 2 --mock         # mock agent: no model, about a minute per run
    scripts/campaign.py report --name baseline                      # (re)write the report from the records

Standard library only. State lives in <home> (default out/campaign/<name>): a PINNED copy of the binary (a rebuild elsewhere cannot change a campaign), the
daemon's state, one archive per run, `campaign.jsonl` (one record per finished run: the campaign resumes from it) and `campaign-report.md`.
Send SIGINT or SIGTERM to stop after the current run (the current workflow is cancelled only on a second signal).
"""
import argparse
import json
import math
import os
import re
import shutil
import signal
import subprocess
import sys
import tarfile
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STOP = {"now": False, "hard": False}


def say(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def on_signal(signum, frame):
    if STOP["now"]:
        STOP["hard"] = True
        say("second signal: the current workflow will be cancelled")
    else:
        STOP["now"] = True
        say("stop requested: finishing the current run, then stopping")


def wilson(k, n, z=1.959964):
    """95 % Wilson score interval for k successes of n (the rule `scenario compare` uses)."""
    if n == 0:
        return (0.0, 0.0)
    p = k / n
    d = 1 + z * z / n
    centre = p + z * z / (2 * n)
    spread = z * math.sqrt((p * (1 - p) + z * z / (4 * n)) / n)
    return ((centre - spread) / d, (centre + spread) / d)


def run(cmd, env, timeout=None, check=False):
    p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout, errors="replace")
    if check and p.returncode != 0:
        raise RuntimeError("%s failed (%d): %s" % (" ".join(cmd[:3]), p.returncode, (p.stderr or p.stdout)[-400:]))
    return p


def http_text(url, timeout=5):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return r.read().decode("utf-8", "replace")
    except Exception:
        return None


def engine_root(engine_url):
    return engine_url[:-3] if engine_url.endswith("/v1") else engine_url


def engine_counters(root):
    """Engine tick counters (llama-server --metrics); None when the server was started without it."""
    text = http_text(root + "/metrics")
    if not text:
        return None
    out = {}
    for name in ("n_decode_total", "n_busy_slots_per_decode", "tokens_predicted_total", "prompt_tokens_total", "tokens_predicted_seconds_total", "prompt_seconds_total"):
        m = re.search(r"^llamacpp:%s\s+([0-9.eE+-]+)" % name, text, re.M)
        if m:
            out[name] = float(m.group(1))
    return out or None


def mem_available_mib():
    try:
        for line in open("/proc/meminfo"):
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) // 1024
    except OSError:
        pass
    return 1 << 30


SIGNATURES = [
    (r"undefined reference to", "link:undefined-reference"),
    (r"-Werror=unused|set but not used|defined but not used|unused parameter", "compile:unused"),
    (r"INTERFACE MISMATCH", "interface-mismatch"),
    (r"does not compile|error:", "compile:other"),
    (r"(\d+) of (\d+) tests failed", "tests:semantic"),
    (r"only \d+ test", "tests:too-few"),
    (r"sections never accepted", "assembly:section-unaccepted"),
    (r"timed out", "budget:timeout"),
]
# a heuristic first-cause class (docs/CAMPAIGN.md section 4); every class is a hypothesis to confirm by reading the run
CLASS_OF = {
    "link:undefined-reference": "A/P (assembly or contract)",
    "compile:unused": "A (assembly)",
    "interface-mismatch": "P (plan or contract)",
    "compile:other": "S (implementation)",
    "tests:semantic": "S (implementation)",
    "tests:too-few": "T (tests)",
    "assembly:section-unaccepted": "A (assembly)",
    "budget:timeout": "B (budget)",
}


def read(path, limit=4000):
    try:
        with open(path, errors="replace") as f:
            return f.read(limit)
    except OSError:
        return ""


def classify(status, sandbox):
    """First failure signature and class of a finished workflow, from its own records."""
    wf = status
    if wf.get("state") == "succeeded":
        return {"signature": "success", "class": "-", "failedItem": "", "detail": ""}
    item = wf.get("firstCause") or ""
    base = os.path.join(sandbox, "items", item)
    text = ""
    if os.path.isdir(base):
        attempts = sorted((d for d in os.listdir(base) if d.startswith("attempt-")), key=lambda d: int(d.split("-")[1]), reverse=True)
        for a in attempts:
            text = read(os.path.join(base, a, "gate.txt"))
            if text:
                break
            fail = read(os.path.join(base, a, "failure.json"))
            if fail:
                text = fail
                break
    sig = "unknown"
    for pat, name in SIGNATURES:
        if re.search(pat, text):
            sig = name
            break
    persona = next((i.get("persona") for i in wf.get("items", []) if i.get("id") == item), "")
    cls = CLASS_OF.get(sig, "?")
    if persona == "tester":
        cls = "T (tests)"
    if sig == "unknown" and not text:
        cls = "H? (no failure record)"
    detail = re.sub(r"\s+", " ", text.strip())[:200]
    return {"signature": sig, "class": cls, "failedItem": item, "detail": detail}


def near_pass(sandbox, item):
    """Best test result of any fork attempt of an item: (passed, total) or None."""
    best = None
    base = os.path.join(sandbox, "items", item)
    if not os.path.isdir(base):
        return None
    for a in os.listdir(base):
        try:
            d = json.loads(read(os.path.join(base, a, "fork.json"), 1 << 20))
        except ValueError:
            continue
        t, f = d.get("testsTotal", -1), d.get("testsFailed", -1)
        if t and t > 0 and f is not None and f >= 0:
            ratio = (t - f) / t
            if best is None or ratio > best[0]:
                best = (ratio, t - f, t)
    return None if best is None else (best[1], best[2])


def fork_tokens(sandbox):
    """Tokens the fork attempts recorded (the ledger misses most of them: docs/METRICS.md section 1)."""
    total = 0
    items = os.path.join(sandbox, "items")
    if not os.path.isdir(items):
        return 0
    for it in os.listdir(items):
        base = os.path.join(items, it)
        for a in os.listdir(base) if os.path.isdir(base) else []:
            try:
                d = json.loads(read(os.path.join(base, a, "fork.json"), 1 << 20))
            except ValueError:
                continue
            total += int(d.get("skeletonTokens", 0) or 0) + int(d.get("sectionTokens", 0) or 0) + int(d.get("integrationTokens", 0) or 0)
    return total


class Campaign:
    def __init__(self, a):
        self.a = a
        self.home = os.path.abspath(a.home or os.path.join(ROOT, "out", "campaign", a.name))
        self.jsonl = os.path.join(self.home, "campaign.jsonl")
        self.binary = None
        self.env = dict(os.environ)
        self.env["SUB0SQUAD_HOME"] = os.path.join(self.home, "daemon")

    # ---- setup ---------------------------------------------------------------------------------------------------------
    def pin_binary(self):
        src = os.path.abspath(self.a.binary or os.path.join(ROOT, "out", "build", "dev", "sub0squad"))
        if not os.path.exists(src):
            raise SystemExit("binary not found: %s" % src)
        probe = run([src, "--version"], self.env)
        ver = probe.stdout.strip().split()[1] if probe.returncode == 0 and probe.stdout.strip() else "unknown"
        os.makedirs(os.path.join(self.home, "bin"), exist_ok=True)
        self.binary = os.path.join(self.home, "bin", "sub0squad-" + ver)
        if not os.path.exists(self.binary):
            shutil.copy2(src, self.binary)
            os.chmod(self.binary, 0o755)
        self.version = ver
        say("pinned binary %s (%s)" % (self.binary, ver))

    def cli(self, *args, timeout=None):
        return run([self.binary, *args], self.env, timeout=timeout)

    def ensure_daemon(self):
        if self.cli("ping").returncode == 0:
            return
        os.makedirs(self.env["SUB0SQUAD_HOME"], exist_ok=True)
        with open(os.path.join(self.env["SUB0SQUAD_HOME"], "run-info.json"), "w") as f:
            json.dump({"campaign": self.a.name, "version": self.version, "started": time.strftime("%F %T"), "frontier": "off"}, f)
        args = ["up", "--frontier", "off", "--max-jobs", str(self.a.max_jobs)]
        args += ["--no-portal"] if self.a.no_portal else ["--portal-port", str(self.a.portal_port)]
        p = self.cli(*args)
        if p.returncode != 0:
            raise SystemExit("could not start the daemon: %s" % (p.stderr or p.stdout)[-300:])
        for _ in range(20):
            if self.cli("ping").returncode == 0:
                return
            time.sleep(0.5)
        raise SystemExit("the daemon does not answer")

    def engine_ok(self):
        return self.a.mock or http_text(engine_root(self.a.engine_url) + "/health") is not None

    def done_seeds(self):
        done = {}
        if os.path.exists(self.jsonl):
            for line in open(self.jsonl):
                try:
                    r = json.loads(line)
                    done[r["seed"]] = r
                except (ValueError, KeyError):
                    pass
        return done

    # ---- one run -------------------------------------------------------------------------------------------------------
    def one(self, index, seed):
        a = self.a
        t0 = time.time()
        before = None if a.mock else engine_counters(engine_root(a.engine_url))
        args = ["do", a.task]
        if not a.mock:
            args += ["--agent", "llm", "--engine", "%s=%s,model=%s,ctx=%d,slots=%d,seed=%d" % (a.engine_name, a.engine_url, a.model, a.ctx, a.slots, seed),
                     "--executor", a.executor, "--max-tokens", str(a.max_tokens), "--repair-turns", str(a.repair_turns)]
        p = self.cli(*args, timeout=120)
        m = re.search(r"workflow (w-\d+)", p.stdout + p.stderr)
        if p.returncode != 0 or not m:
            return {"seed": seed, "index": index, "result": "harness-error", "detail": (p.stderr or p.stdout)[-300:], "version": self.version}
        wid = m.group(1)
        say("run %d seed %d: %s started" % (index, seed, wid))
        deadline = t0 + a.run_timeout_min * 60
        outcome = "running"
        while time.time() < deadline:
            w = self.cli("project", "wait", wid, "--timeout", "60", timeout=90)
            st = self.status(wid)
            if st and st.get("state") in ("succeeded", "failed", "cancelled"):
                outcome = st["state"]
                break
            if STOP["hard"]:
                self.cli("project", "cancel", wid)
                outcome = "cancelled"
                break
            if not self.engine_ok():
                say("the engine stopped answering: cancelling %s" % wid)
                self.cli("project", "cancel", wid)
                outcome = "engine-lost"
                break
        else:
            self.cli("project", "cancel", wid)
            outcome = "timeout"
        time.sleep(2)
        st = self.status(wid) or {}
        sandbox = os.path.join(self.env["SUB0SQUAD_HOME"], "state", "workflows", wid)
        after = None if a.mock else engine_counters(engine_root(a.engine_url))
        rec = {"seed": seed, "index": index, "workflow": wid, "version": self.version, "result": outcome, "state": st.get("state"),
               "wallSec": round(time.time() - t0, 1), "items": len(st.get("items", [])),
               "accepted": sum(1 for i in st.get("items", []) if i.get("status") == "accepted"),
               "attempts": sum(int(i.get("attempts", 0) or 0) for i in st.get("items", [])),
               "decisions": {}, "mends": sum(len(i.get("mends", []) if isinstance(i.get("mends"), list) else []) or int(i.get("mends") or 0) for i in st.get("items", [])),
               "ledgerTokens": (st.get("ledger") or {}).get("totalTokens"), "forkTokens": fork_tokens(sandbox)}
        for d in st.get("decisions", []):
            rec["decisions"][d.get("action", "?")] = rec["decisions"].get(d.get("action", "?"), 0) + 1
        rec.update(classify(st, sandbox))
        if rec.get("failedItem"):
            npz = near_pass(sandbox, rec["failedItem"])
            if npz:
                rec["bestTests"] = "%d/%d" % npz
        if before and after:
            ticks = after.get("n_decode_total", 0) - before.get("n_decode_total", 0)
            toks = after.get("tokens_predicted_total", 0) - before.get("tokens_predicted_total", 0)
            rec["engineTicks"] = int(ticks)
            rec["engineDecodedTokens"] = int(toks)
            rec["engineConcurrency"] = round(toks / ticks, 2) if ticks > 0 else None
            rec["enginePromptTokens"] = int(after.get("prompt_tokens_total", 0) - before.get("prompt_tokens_total", 0))
        # the archive: this workflow's directory
        os.makedirs(os.path.join(self.home, "archive"), exist_ok=True)
        arc = os.path.join(self.home, "archive", "run-%02d-seed-%d.tar.gz" % (index, seed))
        if os.path.isdir(sandbox):
            with tarfile.open(arc, "w:gz") as t:
                t.add(sandbox, arcname="%s" % wid)
            rec["archive"] = os.path.relpath(arc, self.home)
        return rec

    def status(self, wid):
        p = self.cli("project", "status", wid, "--json")
        try:
            return json.loads(p.stdout)["workflows"][0]
        except (ValueError, KeyError, IndexError):
            return None

    # ---- the loop and the report ---------------------------------------------------------------------------------------
    def loop(self):
        os.makedirs(self.home, exist_ok=True)
        self.pin_binary()
        self.ensure_daemon()
        done = self.done_seeds()
        say("campaign %s: %d runs, seeds %d..%d, %d already recorded" % (self.a.name, self.a.runs, self.a.seed_base, self.a.seed_base + self.a.runs - 1, len(done)))
        errors = 0
        for i in range(self.a.runs):
            seed = self.a.seed_base + i
            if seed in done:
                continue
            if STOP["now"]:
                say("stopped before run %d" % i)
                break
            for _ in range(30):
                if mem_available_mib() >= self.a.min_mem_mib:
                    break
                say("waiting for memory (%d MiB available, need %d)" % (mem_available_mib(), self.a.min_mem_mib))
                time.sleep(60)
            if not self.engine_ok():
                say("the engine does not answer at %s: stopping (start it, then run the same command again to resume)" % self.a.engine_url)
                break
            self.ensure_daemon()
            rec = self.one(i, seed)
            with open(self.jsonl, "a") as f:
                f.write(json.dumps(rec) + "\n")
            say("run %d seed %d: %s | %s | accepted %s/%s | best tests %s | %s s" % (i, seed, rec.get("result"), rec.get("signature", "-"), rec.get("accepted"), rec.get("items"), rec.get("bestTests", "-"), rec.get("wallSec")))
            self.report()
            errors = errors + 1 if rec.get("result") == "harness-error" else 0
            if errors >= self.a.max_harness_errors:
                say("%d harness errors in a row: stopping" % errors)
                break
        self.report()

    def report(self):
        recs = [json.loads(l) for l in open(self.jsonl)] if os.path.exists(self.jsonl) else []
        n = len(recs)
        ok = sum(1 for r in recs if r.get("result") == "succeeded")
        lo, hi = wilson(ok, n)
        lines = ["# Campaign %s" % self.a.name, "", "Runs: %d. Succeeded: %d (%.0f %%, 95 %% interval %.0f to %.0f %%)." % (n, ok, 100 * ok / n if n else 0, 100 * lo, 100 * hi), "",
                 "| run | seed | result | first failure | class | item | accepted | best tests | attempts | mends | wall min | ticks | concurrency |", "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for r in recs:
            lines.append("| %s | %s | %s | %s | %s | %s | %s/%s | %s | %s | %s | %.0f | %s | %s |" % (r.get("index"), r.get("seed"), r.get("result"), r.get("signature", "-"), r.get("class", "-"), r.get("failedItem", "-"), r.get("accepted", "-"), r.get("items", "-"),
                         r.get("bestTests", "-"), r.get("attempts", "-"), r.get("mends", "-"), (r.get("wallSec") or 0) / 60, r.get("engineTicks", "-"), r.get("engineConcurrency", "-")))
        hist = {}
        for r in recs:
            if r.get("result") != "succeeded":
                k = "%s / %s" % (r.get("signature", "?"), r.get("class", "?"))
                hist[k] = hist.get(k, 0) + 1
        lines += ["", "## Failure classes (first cause, heuristic: read the runs to confirm)", ""] + ["- %d x %s" % (v, k) for k, v in sorted(hist.items(), key=lambda kv: -kv[1])]
        walls = sorted(r.get("wallSec") or 0 for r in recs)
        if walls:
            lines += ["", "Wall time per run: median %.0f min, longest %.0f min (a load-dependent figure; engine ticks are the load-independent one)." % (walls[len(walls) // 2] / 60, walls[-1] / 60)]
        with open(os.path.join(self.home, "campaign-report.md"), "w") as f:
            f.write("\n".join(lines) + "\n")
        say("report: %s (%d runs, %d succeeded)" % (os.path.join(self.home, "campaign-report.md"), n, ok))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["run", "report"])
    ap.add_argument("--name", default="baseline")
    ap.add_argument("--home", help="campaign directory (default out/campaign/NAME)")
    ap.add_argument("--runs", type=int, default=20)
    ap.add_argument("--seed-base", type=int, default=1000)
    ap.add_argument("--task", default="write a calculator in C++")
    ap.add_argument("--executor", default="fork", choices=["session", "canvas", "fork"])
    ap.add_argument("--binary", help="the sub0squad binary to pin (default out/build/dev/sub0squad)")
    ap.add_argument("--engine-name", default="qwen35")
    ap.add_argument("--engine-url", default="http://127.0.0.1:8092/v1")
    ap.add_argument("--model", default="qwen35")
    ap.add_argument("--ctx", type=int, default=8192)
    ap.add_argument("--slots", type=int, default=4)
    ap.add_argument("--max-tokens", type=int, default=3500)
    ap.add_argument("--repair-turns", type=int, default=2)
    ap.add_argument("--max-jobs", type=int, default=4)
    ap.add_argument("--run-timeout-min", type=int, default=240)
    ap.add_argument("--min-mem-mib", type=int, default=3000)
    ap.add_argument("--max-harness-errors", type=int, default=3)
    ap.add_argument("--portal-port", type=int, default=8780)
    ap.add_argument("--no-portal", action="store_true")
    ap.add_argument("--mock", action="store_true", help="the mock agent: no model needed (a smoke test of the campaign machinery)")
    a = ap.parse_args()
    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)
    c = Campaign(a)
    if a.command == "report":
        c.report()
    else:
        c.loop()


if __name__ == "__main__":
    main()
