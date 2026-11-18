"""Localize a failed T0 composite using the case tree and recorded gate evidence."""

import argparse
import json
import pathlib
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("repo", type=pathlib.Path)
    ap.add_argument("case")
    ap.add_argument("--report", type=pathlib.Path, help="gate, quick or calibration JSON")
    ap.add_argument("--plan", action="store_true", help="show the minimal next cases without running a model")
    args = ap.parse_args()
    repo = args.repo.resolve()
    sys.argv = [sys.argv[0], str(repo)]
    import make_t0

    if args.case not in make_t0.CASES:
        ap.error(f"unknown case {args.case}")
    report = args.report or (repo / "out/runs/t0/gate.json")
    try:
        evidence = json.loads(report.read_text(encoding="utf-8")) if report.is_file() else {}
    except (OSError, ValueError) as exc:
        ap.error(f"cannot read evidence: {exc}")
    cases = evidence.get("cases", {})
    failing, unknown, interactions = [], [], []
    seen = set()

    def inspect(name):
        if name in seen:
            return
        seen.add(name)
        state = cases.get(name, {}).get("state")
        children = make_t0.REQUIRES.get(name, ())
        if state == "PASS":
            return
        if state != "FAIL":
            unknown.append(name)
            return
        if not children:
            failing.append(name)
            return
        if all(cases.get(child, {}).get("state") == "PASS" for child in children):
            interactions.append(name)
            return
        for child in children:
            inspect(child)

    inspect(args.case)
    result = {"schema": "t0-dig-1", "root": args.case, "build": evidence.get("build"), "engine": evidence.get("engine"),
              "leavesFailed": failing, "interactions": interactions, "needsEvidence": unknown}
    if not args.plan:
        output = report.parent / f"dig-{args.case}.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
