"""Bounded status and failure drill-down for Squad runs."""
import argparse
from collections import Counter
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from gate_t0 import candidate_checks, case_key, cost_limits, measure_cost, overrun


TERMINAL = {"succeeded", "failed", "cancelled"}
RUN_ID = re.compile(r"[\w-]+/run-\d+")
WORKFLOW_ID = re.compile(r"w-\d+")


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return {}


def read_text(path):
    try:
        return path.read_text(encoding="utf-8-sig")
    except OSError:
        return ""


def excerpt(text, limit=220):
    return " ".join(str(text or "").split())[:limit]


def published_lines(path):
    text = read_text(path)
    return text.splitlines() if text.endswith("\n") else text.splitlines()[:-1]


def t0_rows(root):
    rows = []
    for row in published_lines(root / "summary.md")[2:]:
        fields = [field.strip() for field in row.strip("|").split("|")]
        if len(fields) >= 3 and fields[1].isdigit() and fields[2] in TERMINAL:
            rows.append((f"{fields[0]}/run-{fields[1]}", fields[2]))
    return rows


@lru_cache(maxsize=256)
def saved_t0_trial(root, run_id):
    run = root / run_id
    stop = read_json(run / "cost-stop.json")
    attempts = sorted((run / "state/workflows/w-000001/items/app").glob("attempt-[0-9]*"),
                      key=lambda attempt: int(attempt.name.split("-")[-1]))
    _, usage = measure_cost(attempts)
    usage["tokensIn"] = max(usage["tokensIn"], stop.get("tokensIn", 0))
    usage["tokensOut"] = max(usage["tokensOut"], stop.get("tokensOut", 0))
    policy = read_json(root / "token-policy.json")
    plan = read_json(run / "state/workflows/w-000001/plan.json")
    ceiling = ""
    if policy and plan:
        limits, _ = cost_limits(policy, run.parent.name, case_key(plan))
        ceiling = overrun(usage, limits["hard"])
    evidence = attempts[-1] if attempts else run
    failure = read_text(evidence / "gate.txt").splitlines()
    edits = list(candidate_checks(attempts))
    cause = stop.get("reason") or excerpt(" ".join(failure[:3]))
    if not cause and edits:
        cause = excerpt(edits[-1][1])
    return {
        "pp": usage["tokensIn"],
        "tg": usage["tokensOut"],
        **({"hard_ceiling": ceiling} if ceiling else {}),
        "cached": usage["cachedPromptTokensObserved"],
        "missing_usage": usage["missingUsageCalls"],
        "cause": cause,
        "evidence": evidence.relative_to(root).as_posix(),
    }


def indexed_snapshot(rows, after, limit):
    try:
        cursor = int(after) if after is not None else max(0, len(rows) - limit)
    except ValueError as error:
        raise ValueError("this run uses a numeric cursor") from error
    if not 0 <= cursor <= len(rows):
        raise ValueError("cursor is beyond the published results")
    return cursor, rows[cursor:]


def t0_snapshot(root, after, limit, details):
    rows = t0_rows(root)
    cursor, new = indexed_snapshot(rows, after, limit)
    mode = read_text(root / "cost-mode.txt").strip() or "enforce"
    manifest = read_json(root / "manifest.json")
    report = root / ("gate-measure.json" if mode == "measure" else "gate.json")
    final = read_json(report)
    state = "finished" if final else "stopped" if (root / "stop.json").is_file() else (
        "runner-exited" if (root / "runner.exit").is_file() else "receiving")
    outcomes = Counter(status for _, status in rows)
    indexed_events = []
    for index, (run_id, status) in enumerate(new, cursor):
        trial = saved_t0_trial(root, run_id)
        if status != "succeeded" or trial.get("hard_ceiling"):
            kind = "failure" if status != "succeeded" else "token-overrun"
            indexed_events.append((index, {"type": kind, "run": run_id, "state": status, **trial}))
    subtotal = Counter()
    for run_id, _ in rows:
        counts = saved_t0_trial(root, run_id)
        subtotal.update({key: counts[key] for key in ("pp", "tg", "cached", "missing_usage")})
    result = {
        "kind": "t0", "state": state, "mode": mode, "cursor": len(rows), "new": len(rows) - cursor,
        "outcomes": dict(outcomes), "tokens": dict(subtotal),
        "events": [event for _, event in indexed_events[:limit]],
    }
    if len(indexed_events) > limit:
        result.update(cursor=indexed_events[limit][0], more_events=len(indexed_events) - limit)
    if state == "receiving":
        recorded = {run_id for run_id, _ in rows}
        pending = sorted(p.relative_to(root).as_posix() for p in root.glob("*/run-*")
                         if p.is_dir() and p.relative_to(root).as_posix() not in recorded)
        if len(pending) == 1:
            result["active_trial"] = pending[0]
    if manifest.get("releaseCases") and manifest.get("seeds"):
        result["expected"] = manifest["releaseCases"] * len(manifest["seeds"])
    if final:
        result.update(verdict=final["verdict"], report=report.name)
        totals = final.get("totals", {})
        result["gate_kpis"] = {key: totals[key] for key in
                               ("firstCandidatePass", "secondCandidatePass", "firstPass", "secondPass",
                                "costWarnings", "costFailures", "costIncomplete") if key in totals}
    if details:
        if not RUN_ID.fullmatch(details) or not (root / details).is_dir():
            raise ValueError("use a trial such as fix-op/run-1 inside the selected T0 root")
        result["details"] = {"run": details, **saved_t0_trial(root, details)}
    return result


def campaign_snapshot(root, after, limit, details):
    rows = [json.loads(line) for line in published_lines(root / "campaign.jsonl") if line.strip()]
    cursor, new = indexed_snapshot(rows, after, limit)
    if details:
        new = [row for row in rows if str(row.get("seed")) == details]
        if not new:
            raise ValueError("provide a completed campaign seed")
    events = [{key: row.get(key) for key in
               ("seed", "workflow", "result", "failedItem", "bestTests", "archive") if row.get(key) is not None}
              for row in new if details or row.get("state") != "succeeded"]
    result = {"kind": "campaign", "state": "recording", "cursor": len(rows), "new": len(rows) - cursor,
              "outcomes": dict(Counter(row.get("state") or "unknown" for row in rows)), "events": events[:limit]}
    if len(events) > limit and not details:
        failures = [index for index, row in enumerate(new, cursor) if row.get("state") != "succeeded"]
        result.update(cursor=failures[limit], more_events=len(events) - limit)
    return result


def workflow_snapshot(root, exe, workflow_id, details):
    if not WORKFLOW_ID.fullmatch(workflow_id):
        raise ValueError("provide a workflow ID such as w-000001")
    snapshot = read_json(root / "workflow.json")
    verdict = read_json(root / "verdict.json") or read_json(root / "gate-result.json")
    source = "archive"
    if exe and not (snapshot and verdict):
        home = root / "home" if (root / "home").is_dir() and (root / "manifest.json").is_file() else root
        process = subprocess.run([str(exe.resolve()), "project", "status", workflow_id, "--json"],
                                 env=dict(os.environ, SUB0SQUAD_HOME=str(home)), capture_output=True,
                                 text=True, check=True, timeout=10)
        response = json.loads(process.stdout)
        snapshot = response["workflows"][0]
        source = response.get("source", "cli")
    if not snapshot:
        if verdict:
            return {"kind": "workflow", "state": verdict.get("state", "finished"),
                    "verdict": verdict.get("verdict", verdict.get("passed")), "events": [],
                    "evidence": "verdict.json" if (root / "verdict.json").is_file() else "gate-result.json"}
        if (root / "manifest.json").is_file():
            return {"kind": "workflow", "state": "status-unavailable", "events": []}
        raise ValueError("use --exe for live status or point to an archived workflow")
    items = snapshot.get("items", [])
    requests = [r for r in snapshot.get("requests", []) if r.get("blocking") and r.get("status") == "open"]
    result = {
        "kind": "workflow", "source": source, "state": snapshot.get("state"), "workflow": snapshot.get("id", workflow_id),
        "active_jobs": snapshot.get("jobsActive"), "outcomes": dict(Counter(i.get("status") for i in items)),
        "stages": [{"name": s["name"], "state": s["state"], "waiting_on": excerpt(s.get("waitingOn"))}
                   for s in snapshot.get("stages", []) if s.get("state") in {"active", "waiting"}],
        "requests": [{"id": r["id"], "title": excerpt(r.get("title"))} for r in requests],
        "events": [{"item": i["id"], "attempt": i.get("lastFailureAttempt"),
                    "cause": excerpt(i.get("lastFailure", {}).get("reason"))}
                   for i in items if i.get("lastFailure")],
    }
    if verdict and verdict.get("schema") == "product-tier-verdict-1":
        result["product_verdict"] = verdict.get("verdict")
        result["evidence"] = "verdict.json"
    if snapshot.get("firstCause"):
        result["first_cause"] = excerpt(snapshot["firstCause"])
    accounting = snapshot.get("ledger", {}).get("byEngine", {})
    if accounting:
        result["reported_ledger_tokens"] = {key: round(sum(a.get(field, 0) for a in accounting.values()))
                                             for key, field in (("pp", "tokensIn"), ("tg", "tokensOut"))}
    if details:
        known = next((i for i in items if i.get("id") == details), None)
        if known is None:
            raise ValueError("provide an item ID from this workflow")
        result["details"] = {key: known[key] for key in
                             ("id", "status", "attempts", "deps", "lastFailure") if key in known}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="Tier output root, campaign directory, or Squad home")
    parser.add_argument("--kind", choices=("t0", "campaign", "workflow"))
    parser.add_argument("--exe", type=Path, help="Use this Squad binary for live workflow status")
    parser.add_argument("--workflow", default="w-000001")
    parser.add_argument("--after", help="Cursor emitted by the previous check on this run")
    parser.add_argument("--details", help="A T0 case/run-N, campaign seed, or workflow item ID")
    parser.add_argument("--wait", default=0, type=int, choices=range(61), metavar="0..60")
    parser.add_argument("--wake", choices=("change", "failure"), default="change")
    parser.add_argument("--limit", default=3, type=int, help="Max failure events per response")
    args = parser.parse_args()
    root = args.root.resolve()
    if not root.is_dir() or args.limit < 1:
        parser.error("provide an existing run directory and a positive limit")
    kind = args.kind or ("t0" if (root / "summary.md").is_file() or (root / "cost-mode.txt").is_file() else
                         "campaign" if (root / "campaign.jsonl").is_file() else "workflow")
    deadline = time.monotonic() + args.wait
    after = args.after
    try:
        while True:
            if kind == "t0":
                result = t0_snapshot(root, after, args.limit, args.details)
            elif kind == "campaign":
                result = campaign_snapshot(root, after, args.limit, args.details)
            else:
                result = workflow_snapshot(root, args.exe, args.workflow, args.details)
                fingerprint = json.dumps(result, sort_keys=True).encode()
                failures = json.dumps(result["events"], sort_keys=True).encode()
                failure_id = hashlib.sha256(failures).hexdigest()[:12]
                result["cursor"] = hashlib.sha256(fingerprint).hexdigest()[:12] + "." + failure_id
                result["new"] = int(after is not None and after != result["cursor"])
                if after is not None and after.split(".")[-1] == failure_id:
                    result["events"] = []
                if len(result["events"]) > args.limit:
                    result["more_events"] = len(result["events"]) - args.limit
                    result["events"] = result["events"][:args.limit]
            if not args.wait or args.details:
                break
            terminal = result["state"] in TERMINAL | {"finished", "stopped", "runner-exited"}
            failure = any(event.get("type") != "token-overrun" for event in result["events"])
            if after is None and (terminal or (result["events"] if args.wake == "change" else failure)):
                break
            if after is None:
                after = str(result["cursor"])
            changed = bool(result["new"] or result["events"])
            if terminal or (changed if args.wake == "change" else failure):
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                result["wait_expired"] = True
                break
            time.sleep(min(5, remaining))
        print(json.dumps(result, ensure_ascii=True, separators=(",", ":")))
    except (ValueError, OSError, IndexError, subprocess.SubprocessError) as error:
        parser.exit(2, f"watch_run: {error}\n")


if __name__ == "__main__":
    main()
