"""Stage or run the bounded T3–T5 calculator workflows and grade the shipped CLI."""

import argparse
import json
import math
import os
import re
from pathlib import Path
import subprocess
import sys
import time
from urllib.parse import urlsplit
import uuid

from t0_sandbox import claim, digest, require, run, save

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "out/runs/product-tiers"
sys.path.insert(0, str(ROOT / "validation"))
from grade_blackbox import audit_literals
REQUEST = (
    "Build a small scientific calculator command-line program in C++. Build an executable named calc_cli. "
    "Accept one expression argument with decimal numbers, unary minus, addition, subtraction, multiplication, "
    "division, parentheses and sin(x). Use radians by default and accept --degrees before the expression. "
    "Print only the numeric result on success and exit zero. Invalid input, including division by zero, "
    "must exit nonzero. Supply automated tests for the evaluator and command line."
)
POWER = " Also support ^ with right associativity and tighter binding than unary minus."
CASES = (
    (["1.25+2.5"], 3.75), (["(3+4)*2"], 14), (["2+3*4"], 14), (["-5-2"], -7), (["9/2"], 4.5),
    (["sin(0.5)"], math.sin(0.5)), (["--degrees", "sin(30)"], 0.5),
    (["2+sin(0.5)"], 2 + math.sin(0.5)), (["1/0"], None), (["(2+"], None),
)
POWER_CASES = ((["2^3^2"], 512), (["-2^2"], -4), (["2^-1"], 0.5))


def supplied_plan():
    engine = ("double calc::evaluate(const std::string& expr, bool degrees = false), declared in "
              "include/calc/calc.hpp. Evaluate decimal numbers, unary minus, + - * /, parentheses "
              "and sin(x). Honor radians and degrees. Throw std::runtime_error on invalid input.")
    cli = "Accept one expression argument; --degrees before it selects degree mode. Print only the numeric result. Exit nonzero on invalid input."
    eval_test, cli_test = "tests/eval.test.cpp", "tests/cli.test.cpp"

    def item(name, role, path, contract, test, dependencies, goal, examples):
        return {"id": name, "title": name.replace("-", " "), "goal": goal, "persona": role,
                "complexity": 1, "files": [path], "deps": dependencies,
                "card": {"context": "Scientific calculator vertical slice.", "contract": contract,
                         "acceptance": ["C++20, standard library only, no warnings"], "examples": examples,
                         "include": ["include/calc/calc.hpp", test] if role == "developer" else ["include/calc/calc.hpp"], "notes": [f"Output only {path}"]},
                "gate": {"kind": "ctest", "tests": [test], "minTests": 3, "mustFailOnStub": role == "tester"}}

    eval_examples = ["2+3*4 gives 14", "(2+3)*4 gives 20", "sin(30) in degrees gives 0.5", "division by zero throws"]
    cli_examples = ["2+2 prints 4", "--degrees sin(30) prints 0.5", "1/0 exits nonzero"]
    return {"schema": "plan-2", "task": REQUEST, "harness": "cpp-cmake", "project": "scientific-slice",
            "reasons": ["Prove the tester-to-developer and evaluator-to-CLI handoffs on one small scientific slice."],
            "scaffold": [
                {"path": "include/calc/calc.hpp", "content": "#pragma once\n#include <string>\nnamespace calc { double evaluate(const std::string& expr, bool degrees = false); }\n"},
                {"path": "src/calc.cpp", "content": '#include "calc/calc.hpp"\n#include <limits>\nnamespace calc { double evaluate(const std::string&, bool) { return std::numeric_limits<double>::quiet_NaN(); } }\n'},
                {"path": "src/main.cpp", "content": "int main() { return 0; }\n"},
            ],
            "items": [
                item("test-engine", "tester", eval_test, engine, eval_test, [],
                     "Test the evaluator. Use tests/helpers/check.hpp and the public header.", eval_examples),
                item("dev-engine", "developer", "src/calc.cpp", engine, eval_test, ["test-engine"],
                     "Implement the evaluator behind the fixed header.", eval_examples),
                item("test-app", "tester", cli_test, cli, cli_test, ["dev-engine"],
                     "Test the CLI with helpers/app.hpp. Use app::run for the arguments.", cli_examples),
                item("dev-app", "developer", "src/main.cpp", cli + " Use the evaluator's public interface.",
                     cli_test, ["dev-engine", "test-app"], "Implement the command line against the evaluator.", cli_examples),
            ]}


def grade(app, power):
    outcomes = []
    for args, expected in (*CASES, *(POWER_CASES if power else ())):
        try:
            result = subprocess.run([str(app), *args], cwd=app.parent, capture_output=True,
                                    text=True, errors="replace", timeout=10)
            if expected is None:
                passed = result.returncode != 0
            else:
                try:
                    value = float(result.stdout.strip())
                except ValueError:
                    value = math.nan
                passed = result.returncode == 0 and math.isclose(value, expected, rel_tol=1e-8, abs_tol=1e-8)
            outcomes.append({"args": args, "expected": expected, "passed": passed, "exitCode": result.returncode,
                             "output": (result.stdout + result.stderr)[:500]})
        except (OSError, subprocess.TimeoutExpired) as exc:
            outcomes.append({"args": args, "expected": expected, "passed": False, "infrastructure": str(exc)})
    return {"passed": sum(row["passed"] for row in outcomes), "total": len(outcomes), "cases": outcomes}


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def workflow_state(exe, home, env, log):
    output = require(run([exe, "project", "status", "w-000001", "--json"], home, log, env=env, timeout=30))
    return json.loads(output.stdout)["workflows"][0]


def admission(tier, prior, exe, build, engine, log):
    if prior is None:
        raise ValueError("a received predecessor is required; see --prior")
    if tier == 3:
        gate = read_json(prior / "t0-gate.json")
        wf = prior / "state/workflows/w-000001"
        run_info = read_json(prior / "state/run-info.json")
        engine_alias = engine.split("=", 1)[0]
        if ((prior / "forced.txt").exists() or gate.get("verdict") != "PASS" or gate.get("set") != "all"
                or gate.get("build") != build or gate.get("engine") != engine_alias
                or run_info.get("build", "").split("+", 1)[0] != build):
            raise ValueError("T3 requires a matching, unforced T1 run with its T0 PASS")
        report = (wf / "report.md").read_text(encoding="utf-8")
        if "| app | model |" not in report:
            raise ValueError("the T1 predecessor does not establish model provenance")
        if not (wf / "items/app/accepted/src/main.cpp").is_file():
            raise ValueError("the T1 predecessor has no accepted CLI artifact")
        results = sorted((wf / "items/app").glob("attempt-*/gate.txt"))
        if not any("GATE PASS:" in path.read_text(encoding="utf-8") and "(22 tests," in path.read_text(encoding="utf-8")
                   for path in results):
            raise ValueError("the T1 predecessor has no recorded 22-test pass")
        state_env = dict(os.environ, SUB0SQUAD_HOME=str(prior))
        if workflow_state(exe, prior, state_env, log)["state"] != "succeeded":
            raise ValueError("the T1 workflow has not succeeded")
        return {"tier": 1, "evidence": str(prior), "gate": str(prior / "t0-gate.json")}
    previous = read_json(prior / "verdict.json")
    if (previous.get("schema") != "product-tier-verdict-1" or previous.get("tier") != tier - 1
            or previous.get("verdict") != "PASS" or previous.get("build") != build
            or previous.get("engineSpec") != engine):
        raise ValueError(f"T{tier} requires a received T{tier - 1} run with the same build and engine profile")
    return {"tier": tier - 1, "evidence": str(prior / "verdict.json")}


def execute(args, trial, request):
    home, workspace, log = trial / "home", trial / "workflow", trial / "runner.log"
    home.mkdir()
    workspace.mkdir()
    if args.tier == 3:
        save(workspace / "plan.json", supplied_plan())
        (workspace / "plan.supplied").touch()
    env = dict(os.environ, SUB0SQUAD_HOME=str(home))

    def cli(*argv):
        return run([args.exe, *argv], home, log, env=env, timeout=35)

    require(cli("up", "--frontier", "off", "--no-portal", "--max-jobs", "1"))
    started = False
    try:
        require(cli("do", request, "--sandbox", workspace, "--agent", "llm", "--engine", args.engine,
                    "--executor", "canvas", "--defaults"))
        started = True
        print(f'watch: python validation/tiers/watch_run.py "{trial}" --exe "{args.exe}" --wait 30',
              file=sys.stderr, flush=True)
        until = time.monotonic() + args.timeout
        while time.monotonic() < until:
            status = workflow_state(args.exe, home, env, log)
            if status["state"] in ("succeeded", "failed", "cancelled"):
                save(trial / "workflow.json", status)
                started = False
                return status, workspace
            time.sleep(3)
        raise TimeoutError("workflow limit exceeded")
    finally:
        try:
            if started:
                cli("project", "cancel", "w-000001")
        finally:
            cli("down")


def integrated_app(workspace):
    names = ["calc_cli.exe", "calc_cli"] if os.name == "nt" else ["calc_cli", "calc_cli.exe"]
    return next((workspace / "project/build" / name for name in names if (workspace / "project/build" / name).is_file()), None)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("mode", choices=("stage", "run", "grade"))
    ap.add_argument("tier", type=int, choices=(3, 4, 5))
    ap.add_argument("--exe", type=Path, default=ROOT / "out/build/mingw/sub0squad.exe")
    ap.add_argument("--engine", default="qwen35=http://127.0.0.1:8092/v1,model=qwen35,ctx=16384,slots=1,seed=5101,cache=0")
    ap.add_argument("--prior", type=Path, help="received predecessor home or tier artifact directory")
    ap.add_argument("--staged", help="run an existing prepared trial by its immutable directory ID")
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--app", type=Path, help="explicit built executable for an independent grade")
    args = ap.parse_args()
    if args.mode == "grade":
        if args.app is None or not args.app.is_file():
            ap.error("grade requires --app pointing to a built calculator")
        result = grade(args.app.resolve(), args.tier == 5)
        print(json.dumps(result, indent=2))
        return 0 if result["passed"] == result["total"] else 1
    if args.timeout <= 0:
        ap.error("--timeout must be positive")
    name, separator, profile = args.engine.partition("=")
    endpoint = urlsplit(profile.partition(",")[0])
    if not name or not separator or endpoint.scheme != "http" or endpoint.hostname != "127.0.0.1" or endpoint.username:
        ap.error("--engine must point at the local loopback inference server")
    args.exe = args.exe.resolve()
    if args.staged and (args.mode != "run" or not re.fullmatch(r"\d{8}T\d{6}Z-[a-f0-9]{12}", args.staged)):
        ap.error("--staged requires run mode and a valid trial ID")
    trial = RESULTS / (args.staged or (time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()) + "-" + uuid.uuid4().hex[:12]))
    if not args.staged:
        trial.mkdir(parents=True)
    print(trial, flush=True)
    request = REQUEST + (POWER if args.tier == 5 else "")
    manifest = {"schema": "product-tier-1", "tier": args.tier, "mode": "product-tier", "state": "staged",
                "planned": 1, "request": request, "engineSpec": args.engine, "budgetSeconds": args.timeout,
                "scope": "power" if args.tier == 5 else "scientific-slice", "assessment": "independent CLI grade"}
    if args.tier == 3:
        manifest["planSha256"] = digest(supplied_plan())
    if args.staged:
        try:
            prepared = read_json(trial / "manifest.json")
        except (OSError, ValueError) as exc:
            ap.error(f"cannot read staged trial: {exc}")
        if (trial / "verdict.json").exists() or any(prepared.get(key) != value for key, value in manifest.items()):
            ap.error("the staged trial is terminal or does not match the requested profile")
    else:
        save(trial / "manifest.json", manifest)
    if args.mode == "stage":
        if args.tier == 3:
            save(trial / "plan.json", supplied_plan())
        return 0
    result = {"schema": "product-tier-verdict-1", "tier": args.tier, "verdict": "INDETERMINATE", "engineSpec": args.engine}
    try:
        build = require(run([args.exe, "--version"], ROOT, trial / "runner.log", timeout=30)).stdout.split()[1]
        result["build"] = build
        manifest["admission"] = admission(args.tier, args.prior and args.prior.resolve(), args.exe, build, args.engine, trial / "runner.log")
        manifest["binarySha256"] = digest(args.exe.read_bytes())
        trial_key = digest({"tier": args.tier, "binary": manifest["binarySha256"], "engine": args.engine,
                            "wallSeconds": args.timeout, "request": request,
                            "plan": supplied_plan() if args.tier == 3 else None})
        prior = claim(RESULTS / "ledger.sqlite3", trial_key, trial.name)
        manifest["trial"] = trial_key
        if prior:
            raise ValueError(f"the same effective trial is already claimed at {RESULTS / prior}")
        manifest["state"] = "submitted"
        save(trial / "manifest.json", manifest)
        status, workspace = execute(args, trial, request)
        result["workflow"] = status["state"]
        built = [item for item in status["items"] if item.get("persona") in ("tester", "developer")]
        actual = integrated_app(workspace)
        if status["state"] == "succeeded" and built and all(item.get("builtBy") == "model" for item in built) and actual:
            result["grade"] = grade(actual, args.tier == 5)
            result["artifactSha256"] = digest(actual.read_bytes())
            wrong, checked = audit_literals(str(workspace / "project"))
            result["literalAudit"] = {"checked": checked, "wrong": wrong}
            if any("infrastructure" in case for case in result["grade"]["cases"]):
                result["detail"] = "external grading was interrupted by infrastructure"
            else:
                result["verdict"] = "PASS" if result["grade"]["passed"] == result["grade"]["total"] and not wrong else "FAIL"
        else:
            result["detail"] = "workflow did not receive a model-built CLI"
            result["verdict"] = "FAIL" if status["state"] == "failed" else "INDETERMINATE"
    except (OSError, ValueError, RuntimeError, KeyError, IndexError, subprocess.TimeoutExpired) as exc:
        result["detail"] = str(exc)
    if args.staged and manifest["state"] == "staged":
        save(trial / "admission.json", {"state": "waiting", "detail": result.get("detail", "admission unavailable")})
    else:
        save(trial / "verdict.json", result)
    print(json.dumps(result), flush=True)
    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
