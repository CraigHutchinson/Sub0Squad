#!/usr/bin/env python3
"""The tier 0 validation gate: every tier 0 case must pass on the proving engine, on THIS build, in every one of at least N runs, before tier 1 and
above are run (docs/BLOCK_PROVING_GROUNDS.md, "Model tiers and the tier 0 gate").

    python validation/tiers/gate_t0.py <repo> --engine qwen35 [--build VERSION | --exe PATH] [--min-runs 3] [--set smoke] [--json]

It judges the run directories themselves (out/runs/t0/<case>/run-N, written by run_t0.sh), never a summary that could drift:
  * the build is the one the run's daemon recorded (state/run-info.json), checked against --build or --exe (default: out/build/mingw/sub0squad.exe);
  * the engine is the one the attempts used (agent.json);
  * a run counts only if its plan is the case as make_t0.py defines it NOW (a run of an older definition is stale: listed, not counted);
  * a pass is an accepted item that a MODEL built (report.md "built by"), never the frontier stub or the mock;
  * configured token ceilings apply per run. The warning threshold is advisory. A hard overrun fails; missing usage cannot certify the hard limit.
Writes out/runs/t0/gate.json and gate.md. Exit 0: PASS; 1: FAIL (a counted run failed); 2: INCOMPLETE (a case has fewer than --min-runs counted runs).
--set smoke judges only the smoke set (make_t0.py SMOKE) and writes smoke.json and smoke.md instead: a quick check while iterating, never the gate.
"""
import argparse, hashlib, json, os, pathlib, re, subprocess, sys, time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))   # the sibling modules, also under python -I
import quality_t0  # noqa: E402


def case_key(plan):
    """What makes two plans the same case: seeded files, owner tests, item scope, card contract and gate."""
    it = plan["items"][0]
    core = {"scaffold": sorted((f["path"], f["content"]) for f in plan.get("scaffold", [])), "given": sorted((f["path"], f["content"]) for f in plan.get("given", [])),
            "files": it["files"], "persona": it.get("persona"), "tests": it["gate"]["tests"], "minTests": it["gate"]["minTests"],
            "context": it["card"]["context"], "contract": it["card"].get("contract", "")}
    return hashlib.sha256(json.dumps(core, sort_keys=True).encode()).hexdigest()


# One verdict per applied edit that reached the compiler or behavior gate.
# Rechecks of starting files and rollbacks do not correspond to a new edit.
CANDIDATE_CHECK = re.compile(
    r"^### Verdict, turn \d+ \([^\n]*\)\n\n`{3,}\n"
    r"(staged \(does not compile yet\):[^\n]*|stopped: the same compile error three times running[^\n]*"
    r"|committed(?:; missing [^\n]*|; GATE FAIL:[^\n]*| \(revision log in canvas\.json\); self-check passed:[^\n]*))",
    re.MULTILINE,
)


def candidate_checks(attempts):
    for attempt in attempts:
        trace = attempt / "conversation.md"
        if trace.is_file():
            text = trace.read_text(encoding="utf-8", errors="replace")
            yield from ((attempt.name, verdict) for verdict in CANDIDATE_CHECK.findall(text))


def passed_self_check(verdict):
    return verdict.startswith("committed (revision log in canvas.json); self-check passed:")


def behavior_check(verdict):
    return verdict.startswith("committed; GATE FAIL:") or passed_self_check(verdict)


def edit_kpis(attempts, accepted_attempt):
    candidates = list(candidate_checks(attempts))
    behavior = [(attempt, verdict) for attempt, verdict in candidates if behavior_check(verdict)]

    def pass_ordinal(checks):
        return next((i for i, (attempt, verdict) in enumerate(checks, 1)
                     if attempt == accepted_attempt and passed_self_check(verdict)), None)

    return dict(candidateEdits=len(candidates), passingCandidate=pass_ordinal(candidates),
                candidateEditVerdicts=[verdict for _, verdict in candidates],
                testedEdits=len(behavior), passingEdit=pass_ordinal(behavior),
                gateEditVerdicts=[verdict for _, verdict in behavior])


def run_budget(run):
    for journal in sorted((run / "state/journal").glob("journal-*.ndjson")):
        for line in journal.read_text(encoding="utf-8", errors="replace").splitlines():
            if "workflowSubmitted" not in line:
                continue
            try:
                event = json.loads(line)
                if event["type"] == "workflowSubmitted" and event["d"]["id"] == "w-000001":
                    spec = event["d"]["spec"]
                    return spec.get("maxLocalAttempts"), spec.get("repairTurns")
            except (KeyError, json.JSONDecodeError):
                continue
    return None, None


REPLY_USAGE = re.compile(
    r"^### Reply, turn \d+ \([^\n]*\): (\d+) prompt tokens, (\d+) completion tokens, [^\n]*",
    re.MULTILINE,
)
PROMPT_TIME = re.compile(r", (\d+\.\d+) s prompt")
PROMPT_CACHE = re.compile(r", (\d+) of them already cached on the slot")
SENT_TURN = re.compile(r"^## Turn \d+ ", re.MULTILINE)
TRACE_BODY = re.compile(r"^(`{3,})$\n.*?^\1(?:\n|\Z)", re.MULTILINE | re.DOTALL)


def attempt_cost(attempt, agent):
    trace = attempt / "conversation.md"
    text = TRACE_BODY.sub("", trace.read_text(encoding="utf-8", errors="replace")) if trace.is_file() else ""
    replies = list(REPLY_USAGE.finditer(text))
    calls = max(agent.get("turns", 0), len(SENT_TURN.findall(text)), len(replies))
    timed = [float(time[1]) for reply in replies if (time := PROMPT_TIME.search(reply[0]))]
    return {
        "turns": calls,
        "tokensIn": round(max(agent.get("tokensIn", 0), sum(int(r[1]) for r in replies))),
        "tokensOut": round(max(agent.get("tokensOut", 0), sum(int(r[2]) for r in replies))),
        "cachedPromptTokensObserved": sum(int(cache[1]) for reply in replies if (cache := PROMPT_CACHE.search(reply[0]))),
        "prefillSeconds": sum(timed),
        "prefillTimedCalls": len(timed),
        "missingUsageCalls": calls - sum(int(r[1]) > 0 and int(r[2]) > 0 for r in replies),
    }


def load_cost_policy(engine):
    policy = json.loads(pathlib.Path(__file__).with_name("t0-token-budget.json").read_text(encoding="utf-8"))
    return policy if policy["engine"] == engine else None


def cost_limits(policy, name, key):
    case = policy["cases"].get(name)
    calibrated = case is not None and case["key"] == key
    baseline = case if calibrated else policy["fallback"]
    limits = {}
    for level, headroom in policy["headroom"].items():
        percent = headroom["percent"]
        limits[level] = {metric: baseline[metric] + max((baseline[metric] * percent + 99) // 100, floor)
                         for metric, floor in headroom["minimum"].items()}
    return limits, "case" if calibrated else "suite fallback"


def overrun(run, caps):
    return ", ".join(f"{metric} {run[metric]} > {cap}" for metric, cap in caps.items() if run[metric] > cap)


def judge_cost(run, limits):
    stopped = run.get("costStop")
    if stopped:
        return "FAIL", stopped["reason"]
    hard = overrun(run, limits["hard"])
    if hard:
        return "FAIL", f"hard token ceiling exceeded ({hard})"
    soft = overrun(run, limits["warning"])
    warning = f"soft token threshold exceeded ({soft})" if soft else ""
    if not run["turns"]:
        return "INCOMPLETE", "no measured model calls"
    if run["missingUsageCalls"]:
        return "INCOMPLETE", "; ".join(filter(None, (warning, f"token usage missing for {run['missingUsageCalls']} model call(s)")))
    return ("WARN", warning) if warning else ("PASS", "")


def measure_cost(attempts):
    engines = set()
    cost = dict(turns=0, tokensIn=0, tokensOut=0, cachedPromptTokensObserved=0, prefillSeconds=0.0, prefillTimedCalls=0, missingUsageCalls=0)
    for attempt in attempts:
        try:
            agent = json.loads((attempt / "agent.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            agent = {}
        if engine := agent.get("engine"):
            engines.add(engine)
        for metric, value in attempt_cost(attempt, agent).items():
            cost[metric] += value
    cost["prefillSeconds"] = round(cost["prefillSeconds"], 1) if cost["prefillTimedCalls"] else None
    return ",".join(sorted(engines)), cost


def cost_mode(out):
    marker = out / "cost-mode.txt"
    mode = marker.read_text(encoding="utf-8").strip() if marker.is_file() else "enforce"
    if mode not in {"enforce", "measure"}:
        raise ValueError(f"unknown T0 token policy mode: {mode}")
    return mode


def case_state(passed, counted, expected, cost_failures, cost_incomplete, mode):
    if passed < counted or (mode == "enforce" and cost_failures):
        return "FAIL"
    if counted < expected or (mode == "enforce" and cost_incomplete):
        return "INCOMPLETE"
    return "PASS"


TERMINAL_STATES = {"succeeded", "failed", "cancelled"}


def wait_run(exe, run, engine, policy, timeout=3600):
    workflow = run / "state/workflows/w-000001"
    limits = {}
    if policy:
        plan = json.loads((workflow / "plan.json").read_text(encoding="utf-8"))
        limits, _ = cost_limits(policy, plan["project"].removeprefix("t0-"), case_key(plan))
    env = dict(os.environ, SUB0SQUAD_HOME=str(run))

    def command(action, *args):
        return subprocess.check_output([str(exe.resolve()), "project", action, "w-000001", *args],
                                       text=True, env=env, timeout=35)

    def state():
        return json.loads(command("status", "--json"))["workflows"][0]["state"]

    deadline = time.monotonic() + timeout
    warned = False
    while (current := state()) not in TERMINAL_STATES:
        if limits:
            _, cost = measure_cost((workflow / "items/app").glob("attempt-[0-9]*"))
            hard = limits["hard"]
            if exceeded := overrun(cost, hard):
                mark = {"engine": engine, "reason": f"live hard token ceiling exceeded ({exceeded})",
                        "hardCaps": hard, "tokensIn": cost["tokensIn"], "tokensOut": cost["tokensOut"]}
                pending = run / "cost-stop.json.tmp"
                pending.write_text(json.dumps(mark, indent=2) + "\n", encoding="utf-8")
                pending.replace(run / "cost-stop.json")
                try:
                    command("cancel")
                except subprocess.CalledProcessError:
                    finished = state()
                    if finished in TERMINAL_STATES:
                        return finished
                    raise
                return "cancelled"
            if not warned and (exceeded := overrun(cost, limits["warning"])):
                print(f"{run.parent.name}/{run.name}: soft token threshold exceeded ({exceeded})", file=sys.stderr)
                warned = True
        if time.monotonic() >= deadline:
            command("cancel")
            return "cancelled"
        time.sleep(1)
    return current


def judge_run(run, plan, ref_dir):
    info = {"run": run.name}
    try: info["build"] = json.loads((run / "state/run-info.json").read_text())["build"]
    except Exception: info["build"] = ""
    wf = run / "state/workflows/w-000001"
    try: info["key"] = case_key(json.loads((wf / "plan.json").read_text()))
    except Exception: info["key"] = ""
    item = wf / "items/app"
    attempts = sorted(item.glob("attempt-[0-9]*"), key=lambda attempt: int(attempt.name.split("-")[1]))
    info["attempts"] = len(attempts)
    engine, cost = measure_cost(attempts)
    try:
        stopped = json.loads((run / "cost-stop.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        stopped = None
    if stopped:
        engine = engine or stopped["engine"]
        for metric in ("tokensIn", "tokensOut"):
            cost[metric] = max(cost[metric], stopped[metric])
    info.update(engine=engine, costStop=stopped, **cost)
    report = (wf / "report.md").read_text(encoding="utf-8", errors="replace") if (wf / "report.md").exists() else ""
    info["builtByModel"] = "| app | model |" in report
    info["accepted"] = (item / "accepted").exists()
    # Quality of what was handed on (quality_t0.py): judged on every accepted run; a leftover (a stub, a conflict marker) fails it.
    info["quality"] = {}
    missing_outputs = []
    if info["accepted"]:
        seeds = {f["path"]: f["content"] for f in plan.get("scaffold", [])}
        given = [f["content"] for f in plan.get("given", [])]
        for path in plan["items"][0]["files"]:
            final = wf / "project" / path
            if not final.is_file():
                missing_outputs.append(path)
                continue
            ref = ref_dir / path
            info["quality"][path] = quality_t0.judge(seeds.get(path, ""), final.read_text(encoding="utf-8", errors="replace"),
                                                     ref.read_text(encoding="utf-8") if ref.exists() else None, given if path.startswith("tests/") else ())
    info["leftovers"] = sum(q.get("leftovers", 0) for q in info["quality"].values())
    info["workflowPassed"] = info["accepted"] and info["builtByModel"] and not missing_outputs and info["leftovers"] == 0
    attempt_match = re.search(r"^\| app \| model \| (\d+) \|", report, re.MULTILINE)
    accepted_attempt = int(attempt_match[1]) if attempt_match and info["workflowPassed"] else None
    attempt_limit, repair_turns = run_budget(run)
    outside_budget = accepted_attempt is not None and attempt_limit is not None and accepted_attempt > attempt_limit
    info.update(acceptedAttempt=accepted_attempt, attemptLimit=attempt_limit, repairTurns=repair_turns, outsideAttemptBudget=outside_budget)
    info["passed"] = info["workflowPassed"] and not outside_budget
    info["firstAttempt"] = info["passed"] and info["attempts"] == 1
    info.update(edit_kpis(attempts, f"attempt-{accepted_attempt}"))
    if outside_budget:
        info["lastGate"] = [f"accepted on worker attempt {accepted_attempt}, above configured limit {attempt_limit}"]
        return info
    if missing_outputs:
        info["lastGate"] = ["accepted output missing: " + ", ".join(missing_outputs)]
        return info
    if not info["passed"]:
        for attempt in reversed(attempts):
            gate = attempt / "gate.txt"
            if gate.is_file():
                info["lastGate"] = gate.read_text(encoding="utf-8", errors="replace").splitlines()[:1]
                break
    return info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo"); ap.add_argument("--engine", required=True); ap.add_argument("--min-runs", type=int, default=3)
    build_source = ap.add_mutually_exclusive_group()
    build_source.add_argument("--build")
    build_source.add_argument("--exe", type=pathlib.Path)
    ap.add_argument("--json", action="store_true"); ap.add_argument("--set", default="all")
    run_mode = ap.add_mutually_exclusive_group()
    run_mode.add_argument("--report-run", type=pathlib.Path, help="report one saved canvas workflow using its recorded plan")
    run_mode.add_argument("--wait-run", type=pathlib.Path, help="watch a live T0 run and cancel it at the hard ceiling")
    a = ap.parse_args()
    repo = pathlib.Path(a.repo).resolve()
    cost_policy = load_cost_policy(a.engine)
    if a.wait_run is not None:
        if a.exe is None:
            ap.error("--wait-run requires --exe")
        run = a.wait_run.resolve()
        mode = cost_mode(run.parent.parent)
        print(wait_run(a.exe, run, a.engine, cost_policy if mode == "enforce" else None))
        return 0
    sys.argv = ["make_t0.py", str(repo)]
    import make_t0
    if a.report_run is not None:
        plan_path = a.report_run / "state/workflows/w-000001/plan.json"
        report = judge_run(a.report_run, json.loads(plan_path.read_text(encoding="utf-8")), make_t0.REF)
        print(json.dumps(report, indent=2))
        return 0 if report["passed"] and report["engine"] == a.engine else 1
    exe = a.exe or repo / "out/build/mingw/sub0squad.exe"
    build = a.build
    if build is None:
        version = subprocess.check_output([str(exe.resolve()), "--version"], text=True).split()
        if len(version) < 2:
            ap.error(f"could not read the build version from {exe}")
        build = version[1].split("+", 1)[0]
    out = pathlib.Path(os.environ.get("T0_OUT", repo / "out/runs/t0"))
    mode = cost_mode(out)
    cases, verdict = {}, "PASS"
    smoke = a.set != "all"   # a named set (make_t0.SETS): a quick check, never the gate
    for name in (make_t0.SETS[a.set] if smoke else make_t0.GATE):
        plan = make_t0.plan_for(name)
        key = case_key(plan)
        runs = [judge_run(r, plan, make_t0.REF) for r in sorted((out / name).glob("run-*"))]
        counted = [r for r in runs if r["key"] == key and r["build"].split("+")[0] == build and r["engine"] == a.engine]
        passed = sum(r["passed"] for r in counted)
        limits, limit_source = cost_limits(cost_policy, name, key) if cost_policy else ({}, "unconfigured engine")
        for run in counted:
            run["costState"], run["costReason"] = judge_cost(run, limits) if limits else ("UNCONFIGURED", "")
        cost_warnings = sum(r["costState"] == "WARN" for r in counted)
        cost_failures = sum(r["costState"] == "FAIL" for r in counted)
        cost_incomplete = sum(r["costState"] == "INCOMPLETE" for r in counted)
        state = case_state(passed, len(counted), a.min_runs, cost_failures, cost_incomplete, mode)
        if state == "FAIL" or (state == "INCOMPLETE" and verdict == "PASS"): verdict = state
        cases[name] = {"state": state, "runs": len(counted), "passed": passed,
                       "outsideAttemptBudget": sum(r["outsideAttemptBudget"] for r in counted), "firstAttempt": sum(r["firstAttempt"] for r in counted),
                       "firstPass": sum(r["passed"] and r["passingEdit"] == 1 for r in counted),
                       "secondPass": sum(r["passed"] and r["passingEdit"] == 2 for r in counted),
                       "firstCandidatePass": sum(r["passed"] and r["passingCandidate"] == 1 for r in counted),
                       "secondCandidatePass": sum(r["passed"] and r["passingCandidate"] == 2 for r in counted),
                       "editResults": {r["run"]: {field: r[field] for field in ("candidateEdits", "passingCandidate", "testedEdits", "passingEdit", "acceptedAttempt", "attemptLimit", "repairTurns")} for r in counted},
                       "passedWithoutEditKpi": sum(r["passed"] and r["passingEdit"] is None for r in counted),
                       "costLimits": limits, "costLimitSource": limit_source, "costWarnings": cost_warnings, "costFailures": cost_failures, "costIncomplete": cost_incomplete,
                       "costResults": {r["run"]: {field: r[field] for field in ("tokensIn", "tokensOut", "cachedPromptTokensObserved", "prefillSeconds", "prefillTimedCalls",
                                                                              "missingUsageCalls", "costState", "costReason", "costStop")} for r in counted},
                       "failures": [{"run": r["run"], "testedEdits": r["testedEdits"], "lastGate": r.get("lastGate", []),
                                     "costReason": r["costReason"],
                                     "gateEditVerdicts": r["gateEditVerdicts"], "candidateEditVerdicts": r["candidateEditVerdicts"]}
                                    for r in counted if not r["passed"] or r["costState"] in ("FAIL", "INCOMPLETE")],
                       "turns": sum(r["turns"] for r in counted),
                       "tokensIn": sum(r["tokensIn"] for r in counted), "tokensOut": sum(r["tokensOut"] for r in counted),
                       "cachedPromptTokensObserved": sum(r["cachedPromptTokensObserved"] for r in counted),
                       "missingUsageCalls": sum(r["missingUsageCalls"] for r in counted),
                       "notCounted": [f"{r['run']} ({'stale case' if r['key'] != key else 'other build' if r['build'].split('+')[0] != build else 'unfinished' if not r['engine'] else 'other engine'})" for r in runs if r not in counted],
                       "quality": {r["run"]: r["quality"] for r in counted},
                       # Scaling: defects fixed per turn over the passing runs (1.0 is one defect a turn; a case of n defects done in one turn is n).
                       "defectsPerTurn": round(make_t0.CASES[name]["defects"] * passed / max(1, sum(r["turns"] for r in counted if r["passed"])), 2)
                                         if make_t0.CASES[name].get("defects") and passed else None,
                       "exercises": make_t0.CASES[name]["exercises"]}
    gate = {"schema": "t0-gate-1", "set": a.set, "verdict": verdict, "build": build, "engine": a.engine, "minRuns": a.min_runs, "costMode": mode, "cases": cases}
    totals = {field: sum(case[field] for case in cases.values())
              for field in ("runs", "passed", "outsideAttemptBudget", "firstCandidatePass", "secondCandidatePass", "firstPass", "secondPass", "passedWithoutEditKpi",
                            "turns", "tokensIn", "tokensOut", "cachedPromptTokensObserved", "missingUsageCalls", "costWarnings", "costFailures", "costIncomplete")}
    arm = out / "edit-address.txt"
    if arm.is_file():
        gate["editAddress"] = arm.read_text(encoding="utf-8").strip()
    gate["costCalibration"] = cost_policy["calibration"] if cost_policy else None
    gate["costHeadroom"] = cost_policy["headroom"] if cost_policy else None
    gate["totals"] = totals

    def tree_node(name):
        evidence = cases.get(name)
        children = [tree_node(child) for child in make_t0.REQUIRES.get(name, ())]
        state = evidence["state"] if evidence else "NOT RUN" if name in make_t0.GATE else "NOT IN GATE"
        blocked = [node["case"] for node in children if node["state"] == "FAIL"] if state == "FAIL" else []
        return {"case": name, "state": state, "requires": children, "blockedBy": blocked}

    gate["tree"] = [tree_node(name) for name in make_t0.TOPS]
    out.mkdir(parents=True, exist_ok=True)
    stem = a.set if smoke else "gate"
    if mode == "measure":
        stem += "-measure"
    pending = out / f"{stem}.json.tmp"
    pending.write_text(json.dumps(gate, indent=1), encoding="utf-8")
    pending.replace(out / f"{stem}.json")
    md = [f"# Tier 0 {a.set}: {verdict} (the {a.set} set only; not the gate)" if smoke else f"# Tier 0 gate: {verdict}", "", f"Build `{build}`, engine `{a.engine}`, at least {a.min_runs} counted runs per case, every one passed by a model within its recorded worker attempt limit "
          "with nothing left unfinished (quality_t0.py: a stub or conflict marker left behind fails the run; the other quality measures are reported).", "",
          f"Behavior accepted within turn budget {totals['passed']}/{totals['runs']} runs. Accepted above budget {totals['outsideAttemptBudget']}. Candidate first pass {totals['firstCandidatePass']}, second pass {totals['secondCandidatePass']}. "
          f"Behavior first pass {totals['firstPass']}, second pass {totals['secondPass']}.", "",
          "Each pass column counts exact ordinals. Candidate checks include compiler failures. Behavior checks count edits that reached the item test gate. A gate pass without a recorded behavior ordinal is shown separately.", "",
          f"Observed PP (prompt tokens): {totals['tokensIn']:,}. TG (generated tokens): {totals['tokensOut']:,}. "
          f"Observed cached prompt tokens: {totals['cachedPromptTokensObserved']:,}. "
          f"Usage missing on {totals['missingUsageCalls']} of {totals['turns']} model calls. "
          f"Cost warnings: {totals['costWarnings']}. Hard cost failures: {totals['costFailures']}. Incomplete cost evidence: {totals['costIncomplete']}.", "",
          ("PP and TG warning and hard thresholds use the frozen calibration." if cost_policy else "Token ceilings are unconfigured for this engine.")
          + " Per-run timing is available in the JSON diagnostics.", "",
          "| case | state | passed | candidate first pass | candidate second pass | behavior first pass | behavior second pass | pass without ordinal | first attempt | calls | PP tokens | TG tokens | missing usage calls | PP / TG warning and hard caps per run | quality per run | not counted |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    if mode == "measure":
        md[0] = f"# Tier 0 behavior measurement: {verdict}"
        md[2:2] = ["Token limits were observed without enforcement. This report does not admit Tier 1.", ""]
        md = [line.replace("Hard cost failures:", "Hard ceiling overruns:") for line in md]
    for n, c in cases.items():
        qual = "; ".join(f"{run}: " + " / ".join(quality_t0.summary(q) for q in files.values()) for run, files in c["quality"].items() if files)
        turns = f"{c['turns']}" + (f" ({c['defectsPerTurn']} defects/turn)" if c.get("defectsPerTurn") else "")
        cap = c["costLimits"]
        budget = "; ".join(f"{level} {amounts['tokensIn']:,} / {amounts['tokensOut']:,}" for level, amounts in cap.items())
        budget = f"{budget} ({c['costLimitSource']})" if budget else "unconfigured"
        md.append(f"| {n} | {c['state']} | {c['passed']}/{c['runs']} | {c['firstCandidatePass']}/{c['runs']} | {c['secondCandidatePass']}/{c['runs']} | "
                  f"{c['firstPass']}/{c['runs']} | {c['secondPass']}/{c['runs']} | "
                  f"{c['passedWithoutEditKpi']} | {c['firstAttempt']}/{c['runs']} | {turns} | {c['tokensIn']:,} | {c['tokensOut']:,} | "
                  f"{c['missingUsageCalls']} | {budget} | {qual} | {', '.join(c['notCounted'])} |")
    def add_tree(nodes, level=0):
        for node in nodes:
            note = " · blocked by " + ", ".join(node["blockedBy"]) if node["blockedBy"] else ""
            md.append(f"{'  ' * level}- {node['case']} · {node['state']}{note}")
            add_tree(node["requires"], level + 1)

    md.extend(["", "## Cost warnings", ""])
    for name, case in cases.items():
        for run, cost in case["costResults"].items():
            if cost["costState"] == "WARN":
                path = f"{name}/{run}"
                md.append(f"- [{path}]({path}/): {cost['costReason']}")
    md.extend(["", "## Failed runs", ""])
    for name, case in cases.items():
        for failure in case["failures"]:
            path = f"{name}/{failure['run']}"
            reason = " / ".join(filter(None, (" ".join(failure["lastGate"]), failure["costReason"]))) or "no recorded gate verdict"
            md.append(f"- [{path}]({path}/): {reason} ({failure['testedEdits']} tested edits)")
    md.extend(["", "## Case tree", ""])
    add_tree(gate["tree"])
    (out / f"{stem}.md").write_text("\n".join(md) + "\n")
    print(json.dumps(gate, indent=1) if a.json else "\n".join(md))
    return {"PASS": 0, "FAIL": 1, "INCOMPLETE": 2}[verdict]


if __name__ == "__main__":
    sys.exit(main())
