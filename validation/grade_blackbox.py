#!/usr/bin/env python3
"""Validation-suite grader for CLEAN-SLATE runs: judges a finished run that was planned by a model, with evidence the harness never sees.

This is NOT part of the sub0squad harness. A planned project has its own API, so nothing here links against it: the product is judged through its
command line only, and the product's own tests are audited by an independent evaluation of the literals they contain.

  validation/grade_blackbox.py WORKFLOW_DIR [--json]

  product    the built program (project/build/calc_cli) is run on cases any "write a calculator" request implies: four operations, precedence, parentheses,
             unary minus, a decimal, and a division by zero that must end with a non-zero status. The expression is passed as ONE argument and, when that
             fails, as separate words, because the request does not say which.
  literals   every `evaluate("EXPR") ... NUMBER` pair found in the tests the squad wrote is evaluated here (Python, + - * / and parentheses only); a test
             whose expected number differs is a WRONG TEST, whatever the product does.
"""
import argparse, json, os, re, subprocess, sys

CASES = [("2 + 3", 5), ("10 - 4", 6), ("6 * 7", 42), ("9 / 3", 3), ("2 + 3 * 4", 14), ("(2 + 3) * 4", 20), ("-5 + 8", 3), ("1.5 + 2.25", 3.75),
         ("10 / 4", 2.5), ("2 * (3 + 4) - 5", 9), ("100 / 5 / 2", 10)]
ERROR_CASES = ["1 / 0"]


def run(cmd, cwd, timeout=20):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout, errors="replace")
        return p.returncode, (p.stdout + p.stderr).strip()
    except Exception as e:  # noqa: BLE001
        return 99, str(e)


def number(text):
    for tok in re.findall(r"-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?", text):
        return float(tok)
    return None


def judge_product(app, cwd):
    out = []
    for expr, want in CASES:
        code, text = run([app, expr], cwd)
        got = number(text) if code == 0 else None
        if got is None or abs(got - want) > 1e-6:
            code, text = run([app] + expr.split(), cwd)
            got = number(text) if code == 0 else None
        out.append((expr, want, text, got is not None and abs(got - want) <= 1e-6))
    for expr in ERROR_CASES:
        code, text = run([app, expr], cwd)
        ok = code != 0
        if not ok:
            code, text = run([app] + expr.split(), cwd)
            ok = code != 0
        out.append((expr, "non-zero status", text, ok))
    return out


SAFE = re.compile(r"^[0-9+\-*/(). eE]+$")


def audit_literals(project):
    wrong, checked = [], 0
    tests = os.path.join(project, "tests")
    if not os.path.isdir(tests):
        return wrong, checked
    for name in sorted(os.listdir(tests)):
        if not name.endswith(".test.cpp"):
            continue
        src = open(os.path.join(tests, name), errors="replace").read()
        for expr, lit in re.findall(r'evaluate\(\s*"([^"]+)"\s*\)\s*,\s*(-?[0-9][0-9.eE+-]*)\s*[,)]', src):
            if not SAFE.match(expr):
                continue
            try:
                want = eval(expr, {"__builtins__": {}})  # noqa: S307 (the text is restricted to arithmetic above)
            except Exception:  # noqa: BLE001
                continue
            checked += 1
            if abs(float(lit) - want) > 1e-6:
                wrong.append((name, expr, lit, want))
    return wrong, checked


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("workflow")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    project = os.path.abspath(os.path.join(a.workflow, "project"))
    app = os.path.join(project, "build", "calc_cli")
    result = {"product": None, "literals": None}
    if os.path.exists(app):
        rows = judge_product(app, project)
        result["product"] = {"passed": sum(1 for r in rows if r[3]), "total": len(rows), "failed": [{"case": r[0], "want": r[1], "got": r[2]} for r in rows if not r[3]]}
    wrong, checked = audit_literals(project)
    result["literals"] = {"checked": checked, "wrong": [{"file": w[0], "expr": w[1], "test says": w[2], "actually": w[3]} for w in wrong]}
    if a.json:
        print(json.dumps(result, indent=1))
    else:
        p = result["product"]
        print("product   " + ("no built program" if p is None else f"{p['passed']} of {p['total']} cases pass" + "".join(f"\n  FAIL {f['case']!r}: want {f['want']}, got {f['got']!r}" for f in p["failed"])))
        l = result["literals"]
        print(f"literals  {checked} audited, {len(wrong)} wrong" + "".join(f"\n  WRONG {w[0]}: {w[1]} = {w[3]}, the test says {w[2]}" for w in wrong))
    ok = result["product"] is not None and result["product"]["passed"] == result["product"]["total"] and not wrong
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
