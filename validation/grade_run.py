#!/usr/bin/env python3
"""Validation-suite grader: judges a finished (or failed) run with evidence the harness never sees.

This is NOT part of the sub0squad harness. The harness and its workers know nothing about the reference files below; they exist only so that WE
can tell, after the fact, whether a run's tests and product were right. A real project has no such reference, so no process may depend on it.

  validation/grade_run.py ARCHIVE.tar.gz|WORKFLOW_DIR [--json]

Two independent judgements of the sandbox project `calculator-cpp`:
  tests-vs-reference    the tester items' tests run against the reference implementation: a test that fails there is a WRONG test.
  product-vs-reference  the reference acceptance tests run against the product the developer items produced (the accepted files, else the best attempt).
"""
import argparse, json, os, shutil, subprocess, sys, tarfile, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, "reference", "calculator-cpp")


def sh(cmd, cwd, timeout=300):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout, errors="replace")
    return p.returncode, p.stdout + p.stderr


def tap_counts(out):
    tests = passed = failed = None
    for l in out.splitlines():
        if l.startswith("# tests "): tests = int(l.split()[2])
        elif l.startswith("# pass "): passed = int(l.split()[2])
        elif l.startswith("# fail "): failed = int(l.split()[2])
    return tests, passed, failed


def failing_lines(out, limit=8):
    keep = [l.strip() for l in out.splitlines() if l.startswith("not ok") or "error:" in l]
    return keep[:limit]


def run_target(ws, target):
    build = os.path.join(ws, "build")
    rc, out = sh(["cmake", "-S", ws, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug"], ws, 120)
    if rc: return {"built": False, "log": out[-800:]}
    rc, out = sh(["cmake", "--build", build, "--target", target], ws, 300)
    if rc: return {"built": False, "log": out[-1200:]}
    rc, out = sh([os.path.join(build, target)], ws, 120)
    t, p, f = tap_counts(out)
    return {"built": True, "tests": t, "pass": p, "fail": f, "failing": failing_lines(out)}


def find_scaffold(wf):
    """The scaffold the run itself used (CMakeLists, helpers): taken from any attempt workspace so the grader never depends on the harness binary."""
    for root, dirs, files in os.walk(os.path.join(wf, "items")):
        if root.endswith(os.sep + "ws") and os.path.exists(os.path.join(root, "CMakeLists.txt")):
            return root
    return None


def accepted_or_best(wf, item, rel):
    acc = os.path.join(wf, "items", item, "accepted", rel)
    if os.path.exists(acc): return acc, "accepted"
    best = None
    d = os.path.join(wf, "items", item)
    if os.path.isdir(d):
        for a in sorted(os.listdir(d)):
            f = os.path.join(d, a, "out", rel)
            if a.startswith("attempt-") and os.path.exists(f): best = f
    return best, "last attempt" if best else None


def grade(wf):
    scaffold = find_scaffold(wf)
    if not scaffold: return {"error": "no workspace scaffold found in the run"}
    res = {}
    for label, item, rel, target in (("engine", "test-engine", "tests/eval.test.cpp", "test_eval"), ("app", "test-app", "tests/cli.test.cpp", "test_cli")):
        f, how = accepted_or_best(wf, item, rel)
        if not f: res["tests-vs-reference:" + label] = {"skipped": "no tests were written"}; continue
        with tempfile.TemporaryDirectory() as tmp:
            ws = os.path.join(tmp, "ws"); shutil.copytree(scaffold, ws, ignore=shutil.ignore_patterns("build"))
            shutil.copy(os.path.join(REF, "src", "calc.cpp"), os.path.join(ws, "src", "calc.cpp"))
            shutil.copy(os.path.join(REF, "src", "main.cpp"), os.path.join(ws, "src", "main.cpp"))
            os.makedirs(os.path.join(ws, "tests"), exist_ok=True)
            for old in [x for x in os.listdir(os.path.join(ws, "tests")) if x.endswith(".test.cpp")]: os.remove(os.path.join(ws, "tests", old))
            shutil.copy(f, os.path.join(ws, rel))
            r = run_target(ws, target); r["source"] = how
            r["verdict"] = "WRONG TESTS" if r.get("built") and r.get("fail") else ("tests do not build against the reference" if not r.get("built") else "agree")
            res["tests-vs-reference:" + label] = r
    for label, item, rel, refTests, target in (("engine", "dev-engine", "src/calc.cpp", "eval.test.cpp", "test_eval"), ("app", "dev-app", "src/main.cpp", "cli.test.cpp", "test_cli")):
        f, how = accepted_or_best(wf, item, rel)
        if not f: res["product-vs-reference:" + label] = {"skipped": "no product was written"}; continue
        with tempfile.TemporaryDirectory() as tmp:
            ws = os.path.join(tmp, "ws"); shutil.copytree(scaffold, ws, ignore=shutil.ignore_patterns("build"))
            for rr in ("src/calc.cpp", "src/main.cpp"): shutil.copy(os.path.join(REF, rr), os.path.join(ws, rr))
            shutil.copy(f, os.path.join(ws, rel))  # the product replaces the reference's file
            for old in [x for x in os.listdir(os.path.join(ws, "tests")) if x.endswith(".test.cpp")]: os.remove(os.path.join(ws, "tests", old))
            shutil.copy(os.path.join(REF, "tests", refTests), os.path.join(ws, "tests", refTests))
            r = run_target(ws, target); r["source"] = how
            r["verdict"] = "PASSES reference acceptance" if r.get("built") and r.get("fail") == 0 else "FAILS reference acceptance"
            res["product-vs-reference:" + label] = r
    return res


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("run"); ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    tmp = None
    wf = a.run
    if os.path.isfile(a.run):
        tmp = tempfile.mkdtemp(prefix="grade-run-")
        with tarfile.open(a.run) as t: t.extractall(tmp, filter="data")
        subs = [d for d in os.listdir(tmp) if d.startswith("w-")]
        wf = os.path.join(tmp, subs[0]) if subs else tmp
    try:
        res = grade(wf)
    finally:
        if tmp: shutil.rmtree(tmp, ignore_errors=True)
    if a.json: print(json.dumps(res, indent=1)); return
    for k, v in res.items():
        print("%-34s %s" % (k, v.get("verdict") or v.get("skipped") or v.get("error")), "" if "tests" not in v else "(%s of %s fail, %s)" % (v.get("fail"), v.get("tests"), v.get("source")))
        for l in v.get("failing", [])[:4]: print("    " + l[:200])


if __name__ == "__main__":
    main()
