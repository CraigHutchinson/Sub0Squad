"""Shadow retry diagnostics and fixture-specific golden path checks."""
from collections import Counter
import json
from pathlib import Path
import re


FENCE = re.compile(r"^(\x60{3,})$\n(.*?)^\1(?:\n|\Z)", re.MULTILINE | re.DOTALL)
VERDICT = re.compile(r"^### Verdict, turn (-?\d+) \([^\n]*\)\n\n$", re.MULTILINE)
DUPLICATE = re.compile(
    r"\bredefinition\b|\bredeclaration\b|cannot be overloaded with|has the same name as the class",
    re.IGNORECASE,
)


def classify(verdict):
    if verdict.startswith(("staged (does not compile", "stopped: the same compile error")):
        return "duplicateDeclaration" if DUPLICATE.search(verdict) else "compileFailure"
    if verdict.startswith("committed; GATE FAIL:"):
        if "INTERFACE MISMATCH" in verdict:
            return "interfaceFailure"
        if "crashed " in verdict and "before it reported" in verdict:
            return "testCrash"
        return "behaviorFailure"
    if verdict.startswith(("transaction refused:", "stopped: the same refusal", "stagnated: the transaction")):
        return "transactionRefusal"
    if verdict.startswith("no transaction"):
        return "noTransaction"
    if verdict.startswith("bounded plan cut off"):
        return "planCutoff"
    if verdict.startswith("cut off at the token limit"):
        return "replyContinuation"
    if verdict.startswith("read request refused"):
        return "readRefusal"
    if verdict.startswith("the engine call failed"):
        return "engineFailure"
    return None


def repair_diagnostics(attempts):
    """Count each observed verdict class once per attempt and model call."""
    counts = Counter()
    for attempt in attempts:
        path = attempt / "conversation.md"
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        seen = set()
        for fence in FENCE.finditer(text):
            # The server reply is itself fenced. Only a preceding harness verdict heading can classify a block.
            heading = VERDICT.search(text, max(0, fence.start() - 200), fence.start())
            if heading is None or int(heading[1]) < 0:
                continue
            if category := classify(fence[2]):
                seen.add((int(heading[1]), category))
        counts.update(category for _, category in seen)
    return dict(sorted(counts.items()))


TURN = re.compile(r"^## Turn (\d+) ", re.MULTILINE)
USAGE = re.compile(r"^### Reply, turn \d+ \([^\n]*\): (\d+) prompt tokens, (\d+) completion tokens", re.MULTILINE)


def stage_trace(attempt):
    path = attempt / "conversation.md"
    if not path.is_file():
        return []
    text = path.read_text(encoding="utf-8", errors="replace")
    calls = []
    current = None
    previous_end = 0
    for fence in FENCE.finditer(text):
        headings = text[previous_end:fence.start()]
        previous_end = fence.end()
        if turn := TURN.search(headings):
            current = {"call": int(turn[1]), "requested": "act", "outcomes": []}
            calls.append(current)
        if current is None:
            continue
        if "### Sent as system\n\n" in headings and "On a plan turn, follow the plan-only instruction" in fence[2]:
            current["requested"] = "plan" if current["call"] == 0 else "replan"
        if usage := USAGE.search(headings):
            current.update(pp=int(usage[1]), tg=int(usage[2]))
        if verdict := VERDICT.search(headings):
            if int(verdict[1]) != current["call"]:
                continue
            value = fence[2]
            outcome = classify(value)
            if value.startswith("plan written"):
                outcome = "plan"
            elif value.startswith("read request:"):
                outcome = "read"
            elif "self-check passed:" in value:
                outcome = "passed"
            if outcome and outcome not in current["outcomes"]:
                current["outcomes"].append(outcome)
    return calls


def workflow_jobs(run):
    jobs = []
    for journal in sorted((run / "state/journal").glob("journal-*.ndjson")):
        for row in journal.read_text(encoding="utf-8", errors="replace").splitlines():
            if '"jobSubmitted"' not in row:
                continue
            try:
                event = json.loads(row)
                if event["type"] != "jobSubmitted":
                    continue
                spec = event["d"]["spec"]
                if spec.get("workflow") != "w-000001":
                    continue
                jobs.append({key: spec[key] for key in ("role", "tool", "attempt") if key in spec})
            except (ValueError, KeyError):
                continue
    return jobs


def load_golden_paths():
    return json.loads(Path(__file__).with_name("t0-golden-paths.json").read_text(encoding="utf-8"))


def compare_golden_path(registry, name, key, engine, arm, profile, counts, observed):
    """Allowances are explicit fixture policy. These findings never vote on acceptance."""
    case = registry["cases"].get(name)
    if (not case or case["key"] != key or registry["engine"] != engine
            or registry["editAddress"] != arm or registry["workflowProfile"] != profile):
        return {"calibration": "uncalibrated", "counts": counts, "warnings": []}
    observed = {**counts, **observed}
    limits = {**case["golden"]["trajectory"], **case["allow"]}
    warnings = [{"kind": kind, "observed": actual, "allowed": limits.get(kind, 0)}
                for kind, actual in sorted(observed.items()) if actual > limits.get(kind, 0)]
    return {"calibration": "golden", "counts": counts, "warnings": warnings}
