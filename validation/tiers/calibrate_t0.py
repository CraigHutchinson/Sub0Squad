"""Propose a profile-specific token policy from a complete T0 measurement cohort."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
from gate_t0 import case_key, cost_limits, overrun

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cohort", type=Path, help="completed measurement directory with a manifest and frozen policy")
    args = parser.parse_args()
    root = args.cohort.resolve()
    if not (root / "gate-measure.json").is_file() or not (root / "runner.exit").is_file():
        parser.error("the cohort has no complete T0 measurement report")
    report_path = root / "gate-measure.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8-sig"))
    old = json.loads((root / "token-policy.json").read_text(encoding="utf-8"))
    if not manifest.get("seeds") or not manifest.get("context") or not manifest.get("maxTokens"):
        parser.error("calibration requires explicit seeds, context, and reply token budget")
    if report.get("set") != "all" or any(not case["runs"] for case in report["cases"].values()):
        parser.error("calibration requires every release fixture")
    counted = sum(case["runs"] for case in report["cases"].values())
    if (counted != manifest["releaseCases"] * len(manifest["seeds"])
            or any(case["runs"] != len(manifest["seeds"]) for case in report["cases"].values())):
        parser.error("the report is missing one or more planned trials")
    if (report["costMode"] != "measure" or report["workflowProfile"] not in {"legacy", "simple"}
            or report["editAddress"] != manifest["arm"] or report["engine"] != old["engine"]
            or report["build"] not in manifest["version"]):
        parser.error("the report, manifest, and frozen policy do not describe the same measurement cohort")
    if (root / "runner.exit").read_text(encoding="utf-8").strip() not in {"0", "1"}:
        parser.error("the runner did not finish with a terminal behavior verdict")
    metrics = ("tokensIn", "tokensOut")
    candidate = {
        "engine": report["engine"],
        "calibration": {
            "build": report["build"], "seeds": manifest["seeds"], "contextTokens": manifest["context"],
            "maxReplyTokens": manifest["maxTokens"], "cache": manifest["promptCache"], "thinking": manifest["thinking"],
            "workflowProfile": report["workflowProfile"], "editAddress": report["editAddress"],
            "sourceReportSha256": hashlib.sha256(report_path.read_bytes()).hexdigest(),
            "passingRuns": report["totals"]["passed"], "completePassingRuns": 0,
        },
        "headroom": old["headroom"],
        "fallback": dict(old["fallback"]),
        "cases": {},
    }
    checks = {}
    rollup = Counter()
    for name, case in report["cases"].items():
        plan = json.loads((root / name / "plan.json").read_text(encoding="utf-8"))
        key = case_key(plan)
        recorded = old["cases"].get(name)
        legacy = recorded if recorded and recorded["key"] == key else None
        passing, failed, partial = [], [], 0
        for run, cost in case["costResults"].items():
            attempt = case["editResults"][run]
            accepted = attempt["acceptedAttempt"]
            if accepted and (attempt["attemptLimit"] is None or accepted <= attempt["attemptLimit"]):
                if cost["missingUsageCalls"] or any(cost[m] <= 0 for m in metrics):
                    partial += 1
                else:
                    passing.append((run, cost))
            else:
                failed.append((run, cost))
        candidate["calibration"]["completePassingRuns"] += len(passing)
        if passing:
            max_use = {m: max(cost[m] for _, cost in passing) for m in metrics}
            sample = dict(key=key, passingRuns=len(passing)+partial, partialUsageRuns=partial, **max_use)
            if len(passing) < len(manifest["seeds"]) and legacy:
                for m in metrics:
                    sample[m] = max(sample[m], legacy[m])
            candidate["cases"][name] = sample
            for m in metrics:
                candidate["fallback"][m] = max(candidate["fallback"][m], max_use[m])
        elif legacy:
            candidate["cases"][name] = legacy
        checks[name] = {"key": key, "passing": passing, "failed": failed, "partial": partial}

    evaluation = {}
    for name, observed in checks.items():
        previous, _ = cost_limits(old, name, observed["key"])
        proposed, source = cost_limits(candidate, name, observed["key"])
        passing, failed = observed["passing"], observed["failed"]
        assert not any(overrun(cost, proposed["hard"]) for _, cost in passing)
        rollup.update(
            completePassing=len(passing), partialPassing=observed["partial"], failed=len(failed),
            pastPolicyHardOverrunOnPass=sum(bool(overrun(cost, previous["hard"])) for _, cost in passing),
            pastPolicyHardOverrunOnFailure=sum(bool(overrun(cost, previous["hard"])) for _, cost in failed),
            proposedHardOverrunOnFailure=sum(bool(overrun(cost, proposed["hard"])) for _, cost in failed),
            uncalibrated=int(not passing),
        )
        evaluation[name] = {"completePassing": len(passing), "failed": len(failed),
                            "priorLimits": previous, "recalibratedLimits": proposed, "source": source,
                            "baselines": candidate["cases"].get(name), "partialPassing": observed["partial"]}
    target = root / "proposed-token-policy.json"
    target.write_bytes((json.dumps(candidate, indent=2) + "\n").encode())
    (root / "calibration-comparison.json").write_bytes((json.dumps({"summary": dict(rollup), "cases": evaluation}, indent=2) + "\n").encode())
    print(json.dumps(rollup, indent=2))


if __name__ == "__main__":
    main()
