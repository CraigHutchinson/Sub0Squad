#!/usr/bin/env python3
"""The tier 0 validation gate: every tier 0 case must pass on the proving engine, on THIS build, in every one of at least N runs, before tier 1 and
above are run (docs/BLOCK_PROVING_GROUNDS.md, "Model tiers and the tier 0 gate").

    python validation/tiers/gate_t0.py <repo> --engine qwen35 [--build VERSION] [--min-runs 3] [--set smoke] [--json]

It judges the run directories themselves (out/runs/t0/<case>/run-N, written by run_t0.sh), never a summary that could drift:
  * the build is the one the run's daemon recorded (state/run-info.json); by default the build of out/build/mingw/sub0squad.exe;
  * the engine is the one the attempts used (agent.json);
  * a run counts only if its plan is the case as make_t0.py defines it NOW (a run of an older definition is stale: listed, not counted);
  * a pass is an accepted item that a MODEL built (report.md "built by"), never the frontier stub or the mock.
Writes out/runs/t0/gate.json and gate.md. Exit 0: PASS; 1: FAIL (a counted run failed); 2: INCOMPLETE (a case has fewer than --min-runs counted runs).
--set smoke judges only the smoke set (make_t0.py SMOKE) and writes smoke.json and smoke.md instead: a quick check while iterating, never the gate.
"""
import argparse, hashlib, json, os, pathlib, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))   # the sibling modules, also under python -I
import quality_t0  # noqa: E402


def case_key(plan):
    """What makes two plans the same case: the seeded files, the owner's tests, the item's files, persona and gate."""
    it = plan["items"][0]
    core = {"scaffold": sorted((f["path"], f["content"]) for f in plan.get("scaffold", [])), "given": sorted((f["path"], f["content"]) for f in plan.get("given", [])),
            "files": it["files"], "persona": it.get("persona"), "tests": it["gate"]["tests"], "minTests": it["gate"]["minTests"], "context": it["card"]["context"]}
    return hashlib.sha256(json.dumps(core, sort_keys=True).encode()).hexdigest()


def judge_run(run, plan, ref_dir):
    info = {"run": run.name}
    try: info["build"] = json.loads((run / "state/run-info.json").read_text())["build"]
    except Exception: info["build"] = ""
    wf = run / "state/workflows/w-000001"
    try: info["key"] = case_key(json.loads((wf / "plan.json").read_text()))
    except Exception: info["key"] = ""
    item = wf / "items/app"
    attempts = sorted(item.glob("attempt-*"))
    info["attempts"] = len(attempts)
    engines, turns, tokens = set(), 0, 0
    for a in attempts:
        try:
            j = json.loads((a / "agent.json").read_text())
            engines.add(j.get("engine", "")); turns += j.get("turns", 0); tokens += j.get("tokensOut", 0)
        except Exception: pass
    info.update(engine=",".join(sorted(e for e in engines if e)), turns=turns, tokensOut=round(tokens))
    report = (wf / "report.md").read_text(encoding="utf-8", errors="replace") if (wf / "report.md").exists() else ""
    info["builtByModel"] = "| app | model |" in report
    info["accepted"] = (item / "accepted").exists()
    # Quality of what was handed on (quality_t0.py): judged on every accepted run; a leftover (a stub, a conflict marker) fails it.
    info["quality"] = {}
    if info["accepted"]:
        seeds = {f["path"]: f["content"] for f in plan.get("scaffold", [])}
        given = [f["content"] for f in plan.get("given", [])]
        for path in plan["items"][0]["files"]:
            final = wf / "project" / path
            if not final.exists(): continue
            ref = ref_dir / path
            info["quality"][path] = quality_t0.judge(seeds.get(path, ""), final.read_text(encoding="utf-8", errors="replace"),
                                                     ref.read_text(encoding="utf-8") if ref.exists() else None, given if path.startswith("tests/") else ())
    info["leftovers"] = sum(q.get("leftovers", 0) for q in info["quality"].values())
    info["passed"] = info["accepted"] and info["builtByModel"] and info["leftovers"] == 0
    info["firstAttempt"] = info["passed"] and info["attempts"] == 1
    return info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo"); ap.add_argument("--engine", required=True); ap.add_argument("--build"); ap.add_argument("--min-runs", type=int, default=3)
    ap.add_argument("--json", action="store_true"); ap.add_argument("--set", default="all")
    a = ap.parse_args()
    repo = pathlib.Path(a.repo).resolve()
    sys.argv = ["make_t0.py", str(repo)]
    import make_t0
    build = a.build or subprocess.run([str(repo / "out/build/mingw/sub0squad.exe"), "--version"], capture_output=True, text=True).stdout.split()[1]
    out = pathlib.Path(os.environ.get("T0_OUT", repo / "out/runs/t0"))
    cases, verdict = {}, "PASS"
    smoke = a.set != "all"   # a named set (make_t0.SETS): a quick check, never the gate
    for name in (make_t0.SETS[a.set] if smoke else make_t0.GATE):
        plan = make_t0.plan_for(name)
        key = case_key(plan)
        runs = [judge_run(r, plan, make_t0.REF) for r in sorted((out / name).glob("run-*"))]
        counted = [r for r in runs if r["key"] == key and r["build"].split("+")[0] == build and r["engine"] == a.engine]
        passed = sum(r["passed"] for r in counted)
        state = "FAIL" if passed < len(counted) else "INCOMPLETE" if len(counted) < a.min_runs else "PASS"
        if state == "FAIL" or (state == "INCOMPLETE" and verdict == "PASS"): verdict = state
        cases[name] = {"state": state, "runs": len(counted), "passed": passed, "firstAttempt": sum(r["firstAttempt"] for r in counted),
                       "turns": sum(r["turns"] for r in counted), "tokensOut": sum(r["tokensOut"] for r in counted),
                       "notCounted": [f"{r['run']} ({'stale case' if r['key'] != key else 'other build' if r['build'].split('+')[0] != build else 'unfinished' if not r['engine'] else 'other engine'})" for r in runs if r not in counted],
                       "quality": {r["run"]: r["quality"] for r in counted},
                       # Scaling: defects fixed per turn over the passing runs (1.0 is one defect a turn; a case of n defects done in one turn is n).
                       "defectsPerTurn": round(make_t0.CASES[name]["defects"] * passed / max(1, sum(r["turns"] for r in counted if r["passed"])), 2)
                                         if make_t0.CASES[name].get("defects") and passed else None,
                       "exercises": make_t0.CASES[name]["exercises"]}
    gate = {"schema": "t0-gate-1", "set": a.set, "verdict": verdict, "build": build, "engine": a.engine, "minRuns": a.min_runs, "cases": cases}
    def tree_node(name):
        evidence = cases.get(name)
        children = [tree_node(child) for child in make_t0.REQUIRES.get(name, ())]
        state = evidence["state"] if evidence else "NOT RUN" if name in make_t0.GATE else "NOT IN GATE"
        blocked = [node["case"] for node in children if node["state"] == "FAIL"] if state == "FAIL" else []
        return {"case": name, "state": state, "requires": children, "blockedBy": blocked}

    gate["tree"] = [tree_node(name) for name in make_t0.TOPS]
    out.mkdir(parents=True, exist_ok=True)
    stem = a.set if smoke else "gate"
    pending = out / f"{stem}.json.tmp"
    pending.write_text(json.dumps(gate, indent=1), encoding="utf-8")
    pending.replace(out / f"{stem}.json")
    md = [f"# Tier 0 {a.set}: {verdict} (the {a.set} set only; not the gate)" if smoke else f"# Tier 0 gate: {verdict}", "", f"Build `{build}`, engine `{a.engine}`, at least {a.min_runs} counted runs per case, every one passed by a model "
          "with nothing left unfinished (quality_t0.py: a stub or conflict marker left behind fails the run; the other quality measures are reported).", "",
          "| case | state | passed | first attempt | turns | out tokens | quality per run (changed/ideal lines, then what deserves a look) | not counted |",
          "|---|---|---|---|---|---|---|---|"]
    for n, c in cases.items():
        qual = "; ".join(f"{run}: " + " / ".join(quality_t0.summary(q) for q in files.values()) for run, files in c["quality"].items() if files)
        turns = f"{c['turns']}" + (f" ({c['defectsPerTurn']} defects/turn)" if c.get("defectsPerTurn") else "")
        md.append(f"| {n} | {c['state']} | {c['passed']}/{c['runs']} | {c['firstAttempt']}/{c['runs']} | {turns} | {c['tokensOut']} | {qual} | {', '.join(c['notCounted'])} |")
    def add_tree(nodes, level=0):
        for node in nodes:
            note = " · blocked by " + ", ".join(node["blockedBy"]) if node["blockedBy"] else ""
            md.append(f"{'  ' * level}- {node['case']} · {node['state']}{note}")
            add_tree(node["requires"], level + 1)

    md.extend(["", "## Case tree", ""])
    add_tree(gate["tree"])
    (out / f"{stem}.md").write_text("\n".join(md) + "\n")
    print(json.dumps(gate, indent=1) if a.json else "\n".join(md))
    return {"PASS": 0, "FAIL": 1, "INCOMPLETE": 2}[verdict]


if __name__ == "__main__":
    sys.exit(main())
