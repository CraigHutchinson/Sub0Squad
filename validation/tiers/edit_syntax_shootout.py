"""Compare deterministic edit replies with the production canvas parser."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import urllib.request


PATH = "src/labels.cpp"
OLD = '    return "draft";'
NEW = '    return "ready";'


def function(name, label):
    return f'const char* {name}(bool paused)\n{{\n    if (paused)\n        return "paused";\n    return "{label}";\n}}\n'


def make_fixture(name, base, reply, want, refusal):
    expect = f"# expect-error: {refusal}\n" if refusal else ""
    result = "" if refusal else f"--- want {PATH}\n{want}"
    return f"# name: {name}\n{expect}--- base {PATH}\n{base}--- reply\n{reply}\n{result}"


def token_count(url, reply):
    request = urllib.request.Request(
        url, data=json.dumps({"content": reply, "add_special": False}).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=5) as response:
        return len(json.load(response)["tokens"])


def line_digest(line):
    return hashlib.sha256(line.encode()).hexdigest()


def resolve_sample_anchor(base, digest):
    # The fixture has one named function with a known brace layout. This is a sandbox resolver.
    lines = base.splitlines()
    first = lines.index("const char* statusLabel(bool paused)")
    last = lines.index("}", first)
    matches = [index + 1 for index in range(first, last) if line_digest(lines[index]) == digest]
    return matches[0] if len(matches) == 1 else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--tokenizer", help="Optional local llama-server tokenize endpoint. Does not generate text.")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True)
    fixtures = out / "fixtures"
    fixtures.mkdir()

    target = function("statusLabel", "draft")
    changed = function("statusLabel", "ready")
    other = function("queueLabel", "queued")
    line = target.splitlines().index(OLD) + 1
    scenarios = {
        "original": (target + other, changed + other),
        "moved-block": (other + target, other + changed),
        "duplicate-text": (target + function("queueLabel", "draft"), changed + function("queueLabel", "draft")),
        "copy-typo": (target + other, changed + other),
    }
    measured_tokens = {}
    results = []
    for scenario, (base, goal) in scenarios.items():
        search = OLD.replace("draft", "Draft") if scenario == "copy-typo" else OLD
        edits = {
            "search": f"=== EDIT: {PATH} ===\n<<<<<<< SEARCH\n{search}\n=======\n{NEW}\n>>>>>>> REPLACE\n=== END EDIT ===",
            "lines": f"=== EDIT: {PATH} LINES {line} ===\n{NEW}\n=== END EDIT ===",
            "symbol": f"=== EDIT: {PATH} SYMBOL statusLabel ===\n{changed}=== END EDIT ===",
        }
        for form, reply in edits.items():
            name = f"{scenario}-{form}"
            refusal = ""
            want = goal
            if form == "search":
                refusal = {"copy-typo": "does not appear", "duplicate-text": "appears more than once"}.get(scenario, "")
            if form == "lines" and scenario == "moved-block":
                # Replay the original coordinates after the two functions have exchanged places.
                want = other.replace('    return "queued";', NEW) + target
            fixture = make_fixture(name, base, reply, want, refusal)
            (fixtures / f"{name}.case").write_text(fixture, encoding="utf-8", newline="\n")
            outcome = "refused" if refusal else "correct"
            if not refusal and want != goal:
                outcome = "wrong target"
            row = {"scenario": scenario, "form": form, "bytes": len(reply.encode()), "outcome": outcome}
            if args.tokenizer:
                if reply not in measured_tokens:
                    measured_tokens[reply] = token_count(args.tokenizer, reply)
                row["tokens"] = measured_tokens[reply]
            results.append(row)

    digest = line_digest(OLD)
    hash_reply = f"=== EDIT: {PATH} SYMBOL statusLabel LINE @{digest[:10]} ===\n{NEW}\n=== END EDIT ==="
    hash_cases = dict(scenarios)
    hash_cases["changed-target"] = (function("statusLabel", "pending") + other, changed + other)
    repeated = target.replace(OLD, f"{OLD}\n{OLD}")
    hash_cases["duplicate-in-block"] = (repeated + other, changed + other)
    for scenario, (base, goal) in hash_cases.items():
        name = f"{scenario}-hash-line"
        address = resolve_sample_anchor(base, digest)
        must_refuse = scenario in ("changed-target", "duplicate-in-block")
        if (address is None) != must_refuse:
            raise RuntimeError(f"{name}: anchor result differs from the paired control")
        if address is not None:
            lowered = f"=== EDIT: {PATH} LINES {address} ===\n{NEW}\n=== END EDIT ==="
            fixture = make_fixture(name, base, lowered, goal, "")
            (fixtures / f"{name}.case").write_text(fixture, encoding="utf-8", newline="\n")
        row = {"scenario": scenario, "form": "hash prototype", "bytes": len(hash_reply.encode()),
               "outcome": "refused" if address is None else "correct"}
        if args.tokenizer:
            if hash_reply not in measured_tokens:
                measured_tokens[hash_reply] = token_count(args.tokenizer, hash_reply)
            row["tokens"] = measured_tokens[hash_reply]
        results.append(row)

    command = [str(args.exe.resolve()), "block", "run", "--block", "1", "--fixtures", str(fixtures), "--out", str(out)]
    subprocess.run(command, check=True, timeout=30)
    (out / "comparison.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print("Scenario / form / outcome / bytes / tokens")
    for row in results:
        print(f"{row['scenario']:15} {row['form']:14} {row['outcome']:12} {row['bytes']:4} {row.get('tokens', '-')}")


if __name__ == "__main__":
    main()
