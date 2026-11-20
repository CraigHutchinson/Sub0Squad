"""Paired, local-model edit address spike using the production reply checker."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import urllib.parse
import urllib.request

from edit_syntax_shootout import PATH, make_fixture


END = "=== END EDIT ==="
SEARCH = f"=== EDIT: {PATH} ==="
HANDLE = re.compile(rf"=== EDIT: {re.escape(PATH)} AT V1:A([1-9][0-9]*)(?:-A([1-9][0-9]*))? ===")
COMPACT_HANDLE = re.compile(rf"=== EDIT: {re.escape(PATH)} AT A([1-9][0-9]*)(?:-A([1-9][0-9]*))? ===")
CASES = {
    "label": (
        'const char* statusLabel() {\n    return "draft";\n}\n',
        'const char* statusLabel() {\n    return "ready";\n}\n',
        "Change the string returned by statusLabel to ready.",
    ),
    "retry-range": (
        'int retryDelay(int attempt) {\n    const int initialDelay = 80;\n'
        '    const int maximumDelay = 640;\n    const int delay = initialDelay * attempt;\n'
        '    return delay < maximumDelay ? delay : maximumDelay;\n}\n',
        'int retryDelay(int attempt) {\n    const int initialDelay = 120;\n'
        '    const int maximumDelay = 900;\n    const int delay = initialDelay * attempt;\n'
        '    return delay < maximumDelay ? delay : maximumDelay;\n}\n',
        "Set retryDelay's initial delay to 120 and its maximum delay to 900.",
    ),
    "handle-like": (
        'const char* helpText() {\n    return R"help(edit example\n'
        'AT V1:A2\nmode: draft\noutput: V1:A4\n)help";\n}\n',
        'const char* helpText() {\n    return R"help(edit example\n'
        'AT V1:A9\nmode: ready\noutput: V1:A4\n)help";\n}\n',
        "In helpText's example, change AT V1:A2 to AT V1:A9 and mode: draft to mode: ready. Preserve the output line.",
    ),
}

FORMAT = {
    "search": (
        f"{SEARCH}\n<<<<<<< SEARCH\n<exact old lines>\n=======\n<replacement lines>\n"
        f">>>>>>> REPLACE\n{END}\nCopy the old source lines exactly."
    ),
    "handle-template": (
        f"=== EDIT: {PATH} AT V1:A2 ===\n<replacement lines>\n{END}\n"
        "Use one issued handle for one line, or an inclusive range like V1:A2-A4. "
        "The margin labels are addresses. Copy only source text into the body. "
        "Handle text inside the source or replacement body is literal."
    ),
    "handle-local": (
        "Use an address from this source margin. A7-A9 includes both endpoints. "
        "Write only replacement source in the body. Example:\n"
        f"=== EDIT: {PATH} AT A7 ===\n    ++count;\n{END}"
    ),
    "handle": (
        "Replace using an issued address or inclusive range (V1:A7-A9). "
        "Omit margin labels from the replacement. Source and body handle text is literal. Example:\n"
        f"=== EDIT: {PATH} AT V1:A7 ===\n    ++count;\n{END}"
    ),
}


def digest(line):
    return hashlib.sha256(line.encode()).hexdigest()


def lower(arm, reply, issued, current):
    """Resolve the single-editor experiment envelope to a native edit."""
    lines = reply.strip("\r\n").splitlines()
    if len(lines) < 2 or lines[-1] != END:
        return "", "missing edit envelope"
    if any(line.startswith(("=== EDIT:", END)) for line in lines[1:-1]):
        return "", "nested edit delimiters"
    if any(line.startswith("--- ") for line in lines):
        return "", "reserved fixture delimiter"
    if arm == "search":
        return ("\n".join(lines), "") if lines[0] == SEARCH else ("", "invalid SEARCH header")
    # The live request holds one immutable issued view. Only the local arm omits its ID on the wire.
    pattern = COMPACT_HANDLE if arm == "handle-local" else HANDLE
    match = pattern.fullmatch(lines[0])
    if not match:
        return "", "invalid handle header"
    first, last = int(match[1]), int(match[2] or match[1])
    if not 1 <= first <= last <= len(issued):
        return "", "unknown or reversed handle range"
    current_hashes = [digest(line) for line in current]
    starts = [index for index, value in enumerate(current_hashes) if value == issued[0]]
    if len(starts) != 1:
        return "", "stale or ambiguous target"
    start = starts[0]
    # The fixed fixtures use an unindented closing brace to delimit the visible function.
    if "}" not in current[start + 1:]:
        return "", "stale or ambiguous target"
    end = current.index("}", start + 1) + 1
    fragment = issued[first - 1:last]
    positions = [index + 1 for index in range(start, end - len(fragment) + 1)
                 if current_hashes[index:index + len(fragment)] == fragment]
    if len(positions) != 1:
        return "", "stale or ambiguous target"
    start = positions[0]
    stop = start + len(fragment) - 1
    address = str(start) if start == stop else f"{start}-{stop}"
    return "\n".join([f"=== EDIT: {PATH} LINES {address} ===", *lines[1:]]), ""


def native_check(exe, base, want, reply, out):
    fixtures = out / "fixtures"
    fixtures.mkdir()
    (fixtures / "edit.case").write_text(make_fixture("edit", base, reply, want, ""), encoding="utf-8", newline="\n")
    result = subprocess.run([str(exe), "block", "run", "--block", "1", "--fixtures", str(fixtures),
                             "--out", str(out)], capture_output=True, text=True, timeout=25)
    (out / "native.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    report = json.loads((out / "block-results.json").read_text(encoding="utf-8"))["blocks"][0]
    if result.returncode not in (0, 1):
        raise RuntimeError(f"native validator failed: {result.stdout} {result.stderr}")
    if result.returncode == 0 and report["evidence"].startswith("1 of 1 "):
        return "accepted"
    problem = result.stdout + result.stderr
    if "differs from the intended text" in problem:
        return "wrong output"
    if "refused:" in problem:
        return "native refusal"
    raise RuntimeError(problem)


def preflight(exe, out):
    base, want, _ = CASES["handle-like"]
    lines = base.splitlines()
    issued = list(map(digest, lines))
    search_reply = f"{SEARCH}\n<<<<<<< SEARCH\nAT V1:A2\nmode: draft\n=======\nAT V1:A9\nmode: ready\n>>>>>>> REPLACE\n{END}"
    handle_reply = f"=== EDIT: {PATH} AT V1:A3-A4 ===\nAT V1:A9\nmode: ready\n{END}"
    moved = 'int untouched() { return 5; }\n'
    for name, arm, reply, actual, expected in (
        ("search-literal", "search", search_reply, base, want),
        ("handle-literal", "handle", handle_reply, base, want),
        ("handle-local-literal", "handle-local", handle_reply.replace(" AT V1:", " AT ", 1), base, want),
        ("handle-moved", "handle", handle_reply, moved + base, moved + want),
    ):
        lowered, refusal = lower(arm, reply, issued, actual.splitlines())
        directory = out / name
        directory.mkdir()
        if refusal or native_check(exe, actual, expected, lowered, directory) != "accepted":
            raise RuntimeError(f"control failed: {name}")
    for candidate, source, reason in (
        (handle_reply.replace("AT V1:", "AT V2:", 1), base, "invalid handle header"),
        (handle_reply.replace("A3-A4", "A99"), base, "unknown or reversed handle range"),
        (handle_reply, base.replace("mode: draft", "mode: pending"), "stale or ambiguous target"),
        (handle_reply, base + base, "stale or ambiguous target"),
        (handle_reply, base.replace("mode: draft", "mode: pending") + base.replace("helpText", "otherText"),
         "stale or ambiguous target"),
        (handle_reply.replace("mode: ready", SEARCH), base, "nested edit delimiters"),
    ):
        if lower("handle", candidate, issued, source.splitlines()) != ("", reason):
            raise RuntimeError(f"expected refusal: {reason}")


def request(url, model, messages, seed):
    body = {"model": model, "messages": messages, "seed": seed, "temperature": 0.2,
            "max_tokens": 512, "cache_prompt": False,
            "chat_template_kwargs": {"enable_thinking": False}}
    query = urllib.request.Request(url, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(query, timeout=120) as response:
        return json.load(response)


def trial(args, case, arm, seed, out, protocol=None):
    base, want, task = CASES[case]
    lines = base.splitlines()
    issued = list(map(digest, lines))
    source = base if arm == "search" else "\n".join(f"A{i} | {line}" for i, line in enumerate(lines, 1))
    system = "Make one minimal contiguous edit. Return exactly one edit block with no commentary.\n"
    if protocol is None:
        system += FORMAT[arm]
        resolve = lambda reply, current: lower(arm, reply, issued, current)
    else:
        system, source, resolve = protocol(arm, base)
    messages = [
        {"role": "system", "content": system},
        {"role": "user", "content": f"{task}\nPreserve all other source text.\nFile: {PATH}\n{source}"},
    ]
    out.mkdir()
    attempts = []
    for ordinal in (1, 2):
        directory = out / str(ordinal)
        directory.mkdir()
        (directory / "messages.json").write_text(json.dumps(messages, indent=2), encoding="utf-8")
        response = request(args.url, args.model, messages, seed + ordinal - 1)
        (directory / "response.json").write_text(json.dumps(response, indent=2), encoding="utf-8")
        choice = response["choices"][0]
        reply = choice["message"].get("content") or ""
        native, refusal = resolve(reply, lines)
        outcome = refusal or native_check(args.exe, base, want, native, directory)
        usage = response.get("usage", {})
        attempts.append({"outcome": outcome, "pp": usage.get("prompt_tokens"), "tg": usage.get("completion_tokens"),
                         "finish": choice["finish_reason"]})
        if outcome == "accepted":
            break
        feedback = ("The edit did not make exactly the requested change."
                    if outcome == "wrong output" else "The edit was refused. Check the required format and target.")
        messages.extend([{"role": "assistant", "content": reply}, {"role": "user", "content": feedback}])
    return {"case": case, "arm": arm, "seed": seed, "attempts": attempts}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--url", default="http://127.0.0.1:8092/v1/chat/completions")
    parser.add_argument("--model", default="qwen35")
    parser.add_argument("--seeds", type=int, nargs="+", default=[5101, 5102, 5103])
    parser.add_argument("--preflight-only", action="store_true")
    parser.add_argument("--handle-arm", choices=["handle", "handle-template", "handle-local"], default="handle")
    args = parser.parse_args()
    args.exe = args.exe.resolve()
    url = urllib.parse.urlsplit(args.url)
    if url.scheme != "http" or url.hostname != "127.0.0.1":
        parser.error("use the local IPv4 loopback model endpoint")
    if len(set(args.seeds)) != len(args.seeds):
        parser.error("seeds must be unique")
    args.out.mkdir(parents=True)
    controls = args.out / "controls"
    controls.mkdir()
    preflight(args.exe, controls)
    if args.preflight_only:
        return
    rows = []
    for index, (case, seed) in enumerate((case, seed) for case in CASES for seed in args.seeds):
        arms = ("search", args.handle_arm) if index % 2 == 0 else (args.handle_arm, "search")
        for arm in arms:
            out = args.out / f"{case}-{seed}-{arm}"
            row = trial(args, case, arm, seed, out)
            rows.append(row)
            print(json.dumps(row), flush=True)
            (args.out / "results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
