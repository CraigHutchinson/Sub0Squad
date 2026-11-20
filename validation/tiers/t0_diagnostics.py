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


def load_golden_paths():
    return json.loads(Path(__file__).with_name("t0-golden-paths.json").read_text(encoding="utf-8"))


def compare_golden_path(registry, name, key, engine, arm, counts, observed):
    """Allowances are explicit fixture policy. These findings never vote on acceptance."""
    case = registry["cases"].get(name)
    if not case or case["key"] != key or registry["engine"] != engine or registry["editAddress"] != arm:
        return {"calibration": "uncalibrated", "counts": counts, "warnings": []}
    observed = {**counts, **observed}
    limits = {**case["golden"]["trajectory"], **case["allow"]}
    warnings = [{"kind": kind, "observed": actual, "allowed": limits.get(kind, 0)}
                for kind, actual in sorted(observed.items()) if actual > limits.get(kind, 0)]
    return {"calibration": "golden", "counts": counts, "warnings": warnings}
