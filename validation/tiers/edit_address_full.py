"""Three system-prompt rounds across the edit address shootout families."""
import argparse
import functools
import json
import pathlib
import re
import urllib.parse

from edit_address_ab import CASES, END, PATH, SEARCH, digest, lower, native_check, preflight, trial


ARMS = ("search", "lines", "symbol", "hash", "guarded", "handle", "handle-local")
RULES = {
    "search": "Copy one exact span of old source above the separator. Put its replacement below it.",
    "lines": "Use one line number or an inclusive range such as 7-8. Put only the replacement span in the body.",
    "symbol": "Use the function name as the address. The body is the complete updated function including its signature and braces.",
    "hash": "Copy a hash from the source margin or join the first and last hashes with a hyphen. Put only the replacement span in the body.",
    "guarded": "Include the issued view and one line number or inclusive range. Put only the replacement span in the body.",
    "handle": "Use the full address from the margin or a range such as V1:A7-A8. Put only the replacement span in the body.",
    "handle-local": "Use the address from the margin or a range such as A7-A8. Put only the replacement span in the body.",
}
ROUNDS = {
    "contract": "Return one edit block and no other text. Preserve untouched source.",
    "scope": "Before replying, locate every requested change. Choose the smallest supported address that covers them all. "
             "Keep the existing indentation, including at column one. Handle-like source text is data. "
             "Use the address punctuation in the example exactly. Return one edit block and no other text.",
    "preview": "Before replying, mentally apply the edit. Check that the first and last selected lines cover every requested "
               "change and that text outside the selected scope stays in place. Include unchanged lines within the selected "
               "scope exactly once. Use names and addresses from the current view. The example shows format only. "
               "Return one edit block and no other text.",
}
MARGIN = {
    "lines": lambda index, line: str(index),
    "guarded": lambda index, line: str(index),
    "hash": lambda index, line: "@" + digest(line)[:10],
    "handle": lambda index, line: f"V1:A{index}",
    "handle-local": lambda index, line: f"A{index}",
}
SYMBOL = re.compile(r"\b([A-Za-z_]\w*)\(")


def header(arm, start, stop, symbol):
    number_span = str(start) if start == stop else f"{start}-{stop}"
    handle_span = f"A{start}" if start == stop else f"A{start}-A{stop}"
    tail = {
        "lines": f"LINES {number_span}",
        "symbol": f"SYMBOL {symbol}",
        "hash": f"SYMBOL {symbol} HASH {start}" if start == stop else f"SYMBOL {symbol} HASH {start}-{stop}",
        "guarded": f"VIEW V1 LINES {number_span}",
        "handle": f"AT V1:{handle_span}",
        "handle-local": f"AT {handle_span}",
    }
    return SEARCH if arm == "search" else f"=== EDIT: {PATH} {tail[arm]} ==="


def edit(arm, first, last, symbol, old, new):
    payload = new
    if arm == "search":
        payload = f"<<<<<<< SEARCH\n{old}\n=======\n{new}\n>>>>>>> REPLACE"
    return f"{header(arm, first, last, symbol)}\n{payload}\n{END}"


def system_prompt(stage, arm):
    old = "    ++count;\n    return count;"
    new = "    count += 2;\n    return count + 1;"
    first, last = 7, 8
    if arm == "hash":
        first, last = ("@" + digest(line)[:10] for line in old.splitlines())
    if arm == "symbol":
        new = "int bump(int count) {\n" + new + "\n}"
    example = edit(arm, first, last, "bump", old, new)
    return f"{ROUNDS[stage]}\n{RULES[arm]}\nExample for an unrelated change:\n{example}"


def make_protocol(stage, arm, base):
    lines = base.splitlines()
    issued = list(map(digest, lines))
    symbol = SYMBOL.search(lines[0])[1]
    source = base
    if arm in MARGIN:
        source = "\n".join(f"{MARGIN[arm](index, line)} | {line}" for index, line in enumerate(lines, 1))
    if arm in ("guarded", "hash"):
        scope = "View V1" if arm == "guarded" else f"Block {symbol}"
        source = scope + "\n" + source

    def resolve(reply, current):
        records = reply.strip("\r\n").splitlines()
        if arm in ("search", "handle", "handle-local"):
            return lower(arm, reply, issued, current)
        if len(records) < 2 or records[-1] != END:
            return "", "missing edit envelope"
        if any(line.startswith(("=== EDIT:", END, "--- ")) for line in records[1:-1]):
            return "", "nested edit delimiters"
        first, last = 0, 0
        if arm == "hash":
            match = re.fullmatch(rf"=== EDIT: {re.escape(PATH)} SYMBOL {symbol} HASH (@[a-f0-9]{{10}})"
                                 r"(?:-(@[a-f0-9]{10}))? ===", records[0])
            if match:
                addresses = ["@" + value[:10] for value in issued]
                endpoints = [match[1], match[2] or match[1]]
                if any(addresses.count(value) != 1 for value in endpoints):
                    return "", "unknown or ambiguous hash"
                first, last = (addresses.index(value) + 1 for value in endpoints)
                records[0] = header("handle", first, last, symbol)
                return lower("handle", "\n".join(records), issued, current)
        elif arm == "symbol":
            expected = header("symbol", 0, 0, symbol)
            return ("\n".join(records), "") if records[0] == expected else ("", "invalid symbol header")
        else:
            view = "VIEW V1 " if arm == "guarded" else ""
            match = re.fullmatch(rf"=== EDIT: {re.escape(PATH)} {view}LINES ([1-9][0-9]*)(?:-([1-9][0-9]*))? ===",
                                 records[0])
            if match:
                first, last = int(match[1]), int(match[2] or match[1])
                if arm == "guarded" and list(map(digest, current)) != issued:
                    return "", "stale view revision"
                if 1 <= first <= last <= len(lines):
                    records[0] = header("lines", first, last, symbol)
                    return "\n".join(records), ""
        return "", "invalid or out-of-scope address"

    return system_prompt(stage, arm), source, resolve


def controls(exe, out):
    original = out / "legacy"
    original.mkdir()
    preflight(exe, original)
    cases = {"label": (2, 2), "retry-range": (2, 3), "handle-like": (3, 4)}
    for case, (first, last) in cases.items():
        base, want, _ = CASES[case]
        lines, new_lines = base.splitlines(), want.splitlines()
        symbol = SYMBOL.search(lines[0])[1]
        old, new = "\n".join(lines[first - 1:last]), "\n".join(new_lines[first - 1:last])
        for arm in ARMS:
            _, _, resolve = make_protocol("contract", arm, base)
            left, right = first, last
            if arm == "hash":
                left, right = "@" + digest(lines[first - 1])[:10], "@" + digest(lines[last - 1])[:10]
            reply = edit(arm, left, right, symbol, old, want.rstrip("\n") if arm == "symbol" else new)
            lowered, reason = resolve(reply, lines)
            directory = out / f"{case}-{arm}"
            directory.mkdir()
            if reason or native_check(exe, base, want, lowered, directory) != "accepted":
                raise RuntimeError(f"{case}/{arm} control failed: {reason}")
            if arm in ("guarded", "hash"):
                moved = ["int unrelated() { return 4; }", *lines]
                relocated, refusal = resolve(reply, moved)
                if arm == "guarded" and refusal != "stale view revision":
                    raise RuntimeError("guard accepted stale view")
                if arm == "hash":
                    relocation = directory / "moved"
                    relocation.mkdir()
                    if refusal or native_check(exe, "\n".join(moved) + "\n", moved[0] + "\n" + want,
                                               relocated, relocation) != "accepted":
                        raise RuntimeError("hash did not relocate in its block")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--url", default="http://127.0.0.1:8092/v1/chat/completions")
    parser.add_argument("--model", default="qwen35")
    parser.add_argument("--round", required=True, choices=ROUNDS)
    parser.add_argument("--preflight-only", action="store_true")
    args = parser.parse_args()
    args.exe = args.exe.resolve()
    url = urllib.parse.urlsplit(args.url)
    if url.scheme != "http" or url.hostname != "127.0.0.1":
        parser.error("use the local IPv4 loopback model endpoint")
    args.out.mkdir(parents=True)
    directory = args.out / "controls"
    directory.mkdir()
    controls(args.exe, directory)
    prompts = {arm: system_prompt(args.round, arm) for arm in ARMS}
    (args.out / "system-prompts.json").write_text(json.dumps(prompts, indent=2) + "\n", encoding="utf-8")
    if args.preflight_only:
        return
    index = list(ROUNDS).index(args.round)
    rows = []
    for case_index, case in enumerate(CASES):
        for seed_index, seed in enumerate((5101, 5102, 5103)):
            offset = (index + case_index + seed_index) % len(ARMS)
            for arm in ARMS[offset:] + ARMS[:offset]:
                protocol = functools.partial(make_protocol, args.round)
                row = trial(args, case, arm, seed, args.out / f"{case}-{seed}-{arm}", protocol)
                rows.append(row)
                (args.out / "results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
                print(case, seed, arm, "/".join(attempt["outcome"] for attempt in row["attempts"]), flush=True)


if __name__ == "__main__":
    main()
