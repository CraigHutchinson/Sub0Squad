"""Tool-use and tool-need probes for the tier 0 canvas worker.

    python -B validation/tiers/t0_tools.py list                       # the scenarios and the tools each offers
    python -B validation/tiers/t0_tools.py selftest                   # no model: the checker against its own controls
    python -B validation/tiers/t0_tools.py run --out NEW_DIR --url http://127.0.0.1:8092/v1/chat/completions --model qwen35 [--arm need] [--samples 3]

"Tools" are the transaction forms a worker may reply with (FILE, SYMBOL, SEARCH/REPLACE, AFTER, READ), plus FIND TOOL in the `need` arm.
A scenario offers some of them, expects the right ones for its task, and gives the source the reply must fit. Each reply is judged in
tiers, stopping at the first failure:

  T1 invoked        the reply makes a transaction, and every transaction is a form this arm offers (no prose-only reply, no invented form)
  T2 selected       the transactions are the forms the task calls for (a whole function is SYMBOL, a few lines SEARCH/REPLACE, ...)
  T3 well-formed    the reply is only transactions; each header, terminator, path, symbol, SEARCH text (exactly once), AFTER position and
                    READ range fits the source the worker was shown; FILE never replaces a file that is not a stub

Tool need. A task can need a capability no offered form supplies. The right reply is one `=== FIND TOOL: <capability> ===` line that names
it. A scenario with `need` set expects that request; a scenario without it is a control, where a request is a false need. Every FIND TOOL
request is captured to `tool-needs.jsonl` with its verdict, for harness review. The probe never adds a tool or runs one: that stays with the
owner (docs/T0_ITERATIVE_SANDBOX.md, "Tool-use probe").

The live runner uses its own tool block, not the production canvas prompt (src/canvas_agent.cpp), so its rates describe the forms, not the
production prompt. It refuses a non-empty output directory and records engine failures apart from tool verdicts.
"""
from __future__ import annotations

import argparse
import collections
import dataclasses
import hashlib
import json
import math
import pathlib
import re
import sys
import time
import urllib.request
from typing import Mapping

FORMS = {
    "file": "=== FILE: <path> ===\n<the complete new file>\n=== END FILE ===\n"
            "  creates a file that does not exist, or replaces a stub",
    "symbol": "=== EDIT: <path> SYMBOL <function name> ===\n<the whole function, from its signature to its last line>\n=== END EDIT ===\n"
              "  replaces the function of that name, or adds it when there is none",
    "search": "=== EDIT: <path> ===\n<<<<<<< SEARCH\n<a few lines copied exactly from the file>\n=======\n<what replaces them>\n>>>>>>> REPLACE\n=== END EDIT ===\n"
              "  changes a few lines; the SEARCH text must occur exactly once",
    "after": "=== EDIT: <path> AFTER <n> ===\n<the lines to insert>\n=== END EDIT ===\n  inserts after line n (0 puts them first)",
    "read-symbol": "=== READ: <path> SYMBOL <function name> ===\n  shows a folded function; a reply of reads is only reads",
    "read-lines": "=== READ: <path> LINES <a-b> ===\n  shows lines a to b; a reply of reads is only reads",
    "find-tool": "=== FIND TOOL: <the capability the task needs> ===\n  asks for a tool that the task needs and none of the forms above can do; reply with that one line",
}

BASE = frozenset({"file", "symbol", "search", "read-symbol", "read-lines"})
ARM_TOOLS = {"plain": frozenset(), "need": frozenset({"find-tool"})}
END = {"EDIT": "=== END EDIT ===", "FILE": "=== END FILE ==="}
HEADER = re.compile(r"^=== ([A-Z][A-Z ]*?): (.*) ===$")
BARE = re.compile(r"^(READ|EDIT|FILE|FIND TOOL): (\S.*)$")
IDENT = re.compile(r"[A-Za-z_]\w*")


@dataclasses.dataclass(frozen=True)
class Scenario:
    id: str
    task: str
    files: Mapping[str, str]                 # the full source, as the checker knows it
    offered: frozenset[str]                  # the forms this scenario offers (FIND TOOL comes from the arm)
    expect: frozenset[str] = frozenset()     # the forms that correctly serve the task (any mix of them passes T2)
    need: tuple[str, ...] = ()               # a missing capability: keywords a FIND TOOL request must name (any one)
    folded: frozenset[str] = frozenset()     # functions the worker is not shown until it reads them
    stubs: frozenset[str] = frozenset()      # files that may be replaced whole by FILE
    view: str | None = None                  # what the worker is shown, when it is not the whole numbered source


def numbered(text: str) -> str:
    return "\n".join(f"{number}| {line}" for number, line in enumerate(text.splitlines(), 1))


def shown(scenario: Scenario) -> str:
    if scenario.view is not None:
        return scenario.view
    return "\n".join(f"=== {path} ===\n{numbered(text)}" for path, text in scenario.files.items()) or "(no files yet)"


def defined_names(source: str) -> set[str]:
    """Function names at the start of a line (the fixtures keep their definitions unindented)."""
    return set(re.findall(r"^[^\s#/][^\n(]*?\b(\w+)\s*\(", source, re.M))


# -- fixtures ------------------------------------------------------------------------------------------------------------------------------

CALC = "double evaluate(const char* expression);\n\ndouble evaluate(const char* expression) {\n    return std::atof(expression);\n}\n"
PRECEDENCE = (
    "int precedence(char op) {\n    switch (op) {\n    case '+': return 1;\n    case '-': return 1;\n"
    "    case '*': return 2;\n    case '/': return 2;\n    case '^': return 2;\n    }\n    return 0;\n}\n"
)
TOTAL = "double total(const double* xs, int n) {\n    double sum = 0;\n    for (int i = 0; i < n; ++i) sum += xs[i];\n    return sum;\n}\n"
POWER = (
    "#include <cmath>\n\ndouble parse_power(double base, double exponent) {\n    return std::pow(base, exponent - 1);\n}\n\n"
    "double evaluate(double base, double exponent) {\n    return parse_power(base, exponent);\n}\n"
)
POWER_VIEW = (
    "=== src/calc.cpp ===\n1| #include <cmath>\n2|\n... (3 lines folded: 3-5; parse_power)\n6|\n"
    "7| double evaluate(double base, double exponent) {\n8|     return parse_power(base, exponent);\n9| }\n"
)
CUT = "#include <string>\n\nstd::string format_result(double value) {\n    std::string text = std::to_string(value);\n"

SCENARIOS = (
    Scenario("file-missing", "Create src/fact.cpp with double fact(int n) returning n!, and fact(0) = 1.", {},
             BASE, expect=frozenset({"file"})),
    Scenario("function-missing", "Add double factorial(int n) to src/calc.cpp (n!, with 0! = 1). The rest of the file is correct.",
             {"src/calc.cpp": CALC}, BASE, expect=frozenset({"symbol"})),
    Scenario("few-lines-wrong", "'^' must bind tighter than '*': its precedence in src/calc.cpp is 3, not 2. Nothing else changes.",
             {"src/calc.cpp": PRECEDENCE}, BASE, expect=frozenset({"search"})),
    Scenario("whole-function-wrong", "total() in src/calc.cpp must use Kahan compensated summation. Its whole body changes.",
             {"src/calc.cpp": TOTAL}, BASE, expect=frozenset({"symbol"})),
    Scenario("folded-function", "The exponent in parse_power in src/calc.cpp is off by one: it must return base to the power exponent.",
             {"src/calc.cpp": POWER}, BASE, expect=frozenset({"read-symbol", "read-lines"}), folded=frozenset({"parse_power"}),
             view=POWER_VIEW),
    Scenario("cut-off-tail", "src/calc.cpp ends in the middle of format_result. Finish the function.", {"src/calc.cpp": CUT},
             BASE | {"after"}, expect=frozenset({"after"})),
    Scenario("need-format", "Format src/calc.cpp to the project's .clang-format style. Change no code.", {"src/calc.cpp": TOTAL},
             BASE, need=("format",)),
    Scenario("need-run-tests", "Run the unit tests for src/calc.cpp and tell me which one fails.", {"src/calc.cpp": TOTAL},
             BASE, need=("run",)),
    Scenario("control-read-lines", "Show lines 2 to 4 of src/calc.cpp.", {"src/calc.cpp": TOTAL}, BASE,
             expect=frozenset({"read-lines"})),
)
SCENARIO = {scenario.id: scenario for scenario in SCENARIOS}


def reply(*lines: str) -> str:
    return "\n".join(lines) + "\n"


REFERENCE = {
    "file-missing": reply("=== FILE: src/fact.cpp ===", "double fact(int n) {", "    double r = 1;", "    for (int i = 2; i <= n; ++i) r *= i;",
                          "    return r;", "}", "=== END FILE ==="),
    "function-missing": reply("=== EDIT: src/calc.cpp SYMBOL factorial ===", "double factorial(int n) {", "    double r = 1;",
                              "    for (int i = 2; i <= n; ++i) r *= i;", "    return r;", "}", "=== END EDIT ==="),
    "few-lines-wrong": reply("=== EDIT: src/calc.cpp ===", "<<<<<<< SEARCH", "    case '^': return 2;", "=======", "    case '^': return 3;",
                             ">>>>>>> REPLACE", "=== END EDIT ==="),
    "whole-function-wrong": reply("=== EDIT: src/calc.cpp SYMBOL total ===", "double total(const double* xs, int n) {",
                                  "    double sum = 0, c = 0;", "    for (int i = 0; i < n; ++i) {", "        double y = xs[i] - c;",
                                  "        double t = sum + y;", "        c = (t - sum) - y;", "        sum = t;", "    }", "    return sum;", "}",
                                  "=== END EDIT ==="),
    "folded-function": reply("=== READ: src/calc.cpp SYMBOL parse_power ==="),
    "cut-off-tail": reply("=== EDIT: src/calc.cpp AFTER 4 ===", "    return text;", "}", "=== END EDIT ==="),
    "need-format": reply("=== FIND TOOL: apply clang-format with the project's .clang-format to src/calc.cpp ==="),
    "need-run-tests": reply("=== FIND TOOL: run the unit tests and report which one fails ==="),
    "control-read-lines": reply("=== READ: src/calc.cpp LINES 2-4 ==="),
}

# (scenario, arm, reply, tier, kind): the tier and kind a correct checker gives. "pass" means every tier passes.
CONTROLS = (
    ("file-missing", "plain", REFERENCE["file-missing"], "pass", "pass"),
    ("file-missing", "plain", reply("=== EDIT: src/fact.cpp SYMBOL fact ===", "double fact(int n) { return 1; }", "=== END EDIT ==="),
     "T2", "wrong-tool"),
    ("function-missing", "plain", REFERENCE["function-missing"], "pass", "pass"),
    ("function-missing", "plain", reply("=== FILE: src/calc.cpp ===", "double evaluate(const char* e) { return 0; }", "=== END FILE ==="),
     "T2", "wrong-tool"),
    ("function-missing", "plain", reply("=== EDIT: src/calc.cpp SYMBOL factorial ===", "int helper() { return 1; }", "=== END EDIT ==="),
     "T3", "malformed"),
    ("few-lines-wrong", "plain", REFERENCE["few-lines-wrong"], "pass", "pass"),
    ("few-lines-wrong", "plain", reply("Here is the fix:", *REFERENCE["few-lines-wrong"].splitlines()), "T3", "malformed"),
    ("few-lines-wrong", "plain", reply("=== EDIT: src/calc.cpp ===", "<<<<<<< SEARCH", "    case '%': return 2;", "=======",
                                       "    case '%': return 3;", ">>>>>>> REPLACE", "=== END EDIT ==="), "T3", "malformed"),
    ("few-lines-wrong", "plain", reply("=== EDIT: src/calc.cpp SYMBOL precedence ===", "int precedence(char op) { return 3; }",
                                       "=== END EDIT ==="), "T2", "wrong-tool"),
    ("whole-function-wrong", "plain", REFERENCE["whole-function-wrong"], "pass", "pass"),
    ("whole-function-wrong", "plain", reply("=== EDIT: src/calc.cpp ===", "<<<<<<< SEARCH", "    double sum = 0;", "=======",
                                            "    double sum = 0.0;", ">>>>>>> REPLACE", "=== END EDIT ==="), "T2", "wrong-tool"),
    ("folded-function", "plain", REFERENCE["folded-function"], "pass", "pass"),
    ("folded-function", "plain", reply("=== READ: src/calc.cpp LINES 3-5 ==="), "pass", "pass"),
    ("folded-function", "plain", reply("=== EDIT: src/calc.cpp SYMBOL parse_power ===",
                                       "double parse_power(double base, double exponent) { return std::pow(base, exponent); }",
                                       "=== END EDIT ==="), "T2", "unseen-edit"),
    ("cut-off-tail", "plain", REFERENCE["cut-off-tail"], "pass", "pass"),
    ("cut-off-tail", "plain", reply("=== EDIT: src/calc.cpp AFTER 40 ===", "    return text;", "=== END EDIT ==="), "T3", "malformed"),
    ("cut-off-tail", "plain", reply("=== FILE: src/calc.cpp ===", "#include <string>", "=== END FILE ==="), "T2", "wrong-tool"),
    ("need-format", "need", REFERENCE["need-format"], "pass", "pass"),
    ("need-format", "need", reply("=== EDIT: src/calc.cpp ===", "<<<<<<< SEARCH", "    double sum = 0;", "=======", "    double sum = 0;",
                                  ">>>>>>> REPLACE", "=== END EDIT ==="), "T2", "substitute"),
    ("need-format", "plain", REFERENCE["need-format"], "T1", "not-offered"),
    ("need-run-tests", "need", REFERENCE["need-run-tests"], "pass", "pass"),
    ("need-run-tests", "need", reply("=== FIND TOOL: read the test output ==="), "T2", "wrong-need"),
    ("control-read-lines", "plain", REFERENCE["control-read-lines"], "pass", "pass"),
    ("control-read-lines", "plain", reply("=== READ: src/calc.cpp LINES 2-40 ==="), "T3", "malformed"),
    ("control-read-lines", "plain", "Lines 2 to 4 are the sum function.", "T1", "no-request"),
    ("control-read-lines", "need", reply("=== FIND TOOL: read lines 2 to 4 of a file ==="), "T2", "false-need"),
    ("control-read-lines", "plain", "READ: src/calc.cpp LINES 2-4", "T3", "unframed"),
    ("need-run-tests", "need", "FIND TOOL: run the unit tests and report which one fails", "T3", "unframed"),
)


# -- parsing and scoring -------------------------------------------------------------------------------------------------------------------

def parse_reply(text: str):
    """Split a reply into transactions, prose lines outside them, and structural problems."""
    lines = text.split("\n")
    transactions, prose, problems = [], [], []
    index = 0
    while index < len(lines):
        line = lines[index]
        head = HEADER.match(line)
        if head is None:
            bare = BARE.match(line)
            if bare:   # a request written without the === framing: kept, and judged malformed (kind "unframed")
                transactions.append({"verb": bare[1], "arg": bare[2], "body": None, "bare": True})
                index += 1
                continue
            if line.strip():
                prose.append(line.strip())
            index += 1
            continue
        verb, arg = head.groups()
        if verb not in ("EDIT", "FILE", "READ", "FIND TOOL"):
            transactions.append({"verb": verb, "arg": arg, "body": None})
            index += 1
            continue
        if verb in ("READ", "FIND TOOL"):
            transactions.append({"verb": verb, "arg": arg, "body": None})
            index += 1
            continue
        end = index + 1
        while end < len(lines) and lines[end] != END[verb]:
            end += 1
        body = lines[index + 1:end]
        transactions.append({"verb": verb, "arg": arg, "body": body})
        if end == len(lines):
            problems.append(f"{verb} {arg!r} has no {END[verb]}")
            break
        index = end + 1
    return transactions, prose, problems


def fields(transaction):
    match = re.fullmatch(r"(\S+)(?: (SYMBOL|AFTER|LINES) (\S+))?", transaction["arg"])
    return (match[1], match[2], match[3]) if match else (None, None, None)


def tool_of(transaction):
    verb = transaction["verb"]
    if verb == "FIND TOOL":
        return "find-tool"
    path, mode, _ = fields(transaction)
    if path is None:
        return None
    if verb == "FILE":
        return "file" if mode is None else None
    if verb == "EDIT":
        return {"SYMBOL": "symbol", "AFTER": "after", None: "search"}.get(mode)
    if verb == "READ":
        return {"SYMBOL": "read-symbol", "LINES": "read-lines"}.get(mode)
    return None


def split_search(body):
    marks = {}
    for index, line in enumerate(body):
        for key, text in (("open", "<<<<<<< SEARCH"), ("sep", "======="), ("close", ">>>>>>> REPLACE")):
            if line.rstrip() == text:
                marks.setdefault(key, index)
    if not {"open", "sep", "close"} <= marks.keys() or not marks["open"] < marks["sep"] < marks["close"]:
        return None
    return body[marks["open"] + 1:marks["sep"]], body[marks["sep"] + 1:marks["close"]]


def check_transaction(transaction, tool, scenario: Scenario) -> list[str]:
    if tool == "find-tool":
        capability = transaction["arg"].strip()
        return [] if 0 < len(capability) <= 200 else ["FIND TOOL names no capability, or more than 200 characters"]
    path, mode, value = fields(transaction)
    if tool == "file":
        if path in scenario.files and path not in scenario.stubs:
            return [f"FILE would replace {path}, which exists and is not a stub"]
        return []
    if path not in scenario.files:
        return [f"{path} is not a file in the view"]
    source = scenario.files[path]
    lines = source.splitlines()
    body = transaction["body"] or []
    if tool == "symbol":
        if not IDENT.fullmatch(value or ""):
            return [f"{value!r} is not a function name"]
        if not any(line.strip() for line in body):
            return ["the function body is empty"]
        if not re.search(rf"\b{re.escape(value)}\s*\(", "\n".join(body)):
            return [f"the body does not define {value}"]
        return []
    if tool == "search":
        parts = split_search(body)
        if parts is None:
            return ["EDIT has no SEARCH/REPLACE pair in the right order"]
        search = parts[0]
        needle = [line.strip() for line in search if line.strip()]
        if not needle:
            return ["SEARCH is empty"]
        if len(search) > 15:
            return ["SEARCH is longer than about 15 lines"]
        stripped = [line.strip() for line in lines]
        count = sum(stripped[i:i + len(needle)] == needle for i in range(len(stripped) - len(needle) + 1))
        return [] if count == 1 else [f"SEARCH text occurs {count} times, not once"]
    if tool == "after":
        if not (value or "").isdigit() or not 0 <= int(value) <= len(lines):
            return [f"AFTER {value} is outside the file (0 to {len(lines)})"]
        return [] if any(line.strip() for line in body) else ["AFTER inserts nothing"]
    if tool == "read-symbol":
        return [] if value in defined_names(source) else [f"{value} is not a function in {path}"]
    if tool == "read-lines":
        match = re.fullmatch(r"(\d+)-(\d+)", value or "")
        if not match:
            return [f"LINES {value} is not a range a-b"]
        first, last = int(match[1]), int(match[2])
        return [] if 1 <= first <= last <= len(lines) else [f"LINES {value} is outside the file (1 to {len(lines)})"]
    return [f"no check for {tool}"]


def score(scenario: Scenario, text: str, arm: str = "plain") -> dict:
    offered = scenario.offered | ARM_TOOLS[arm]
    transactions, prose, problems = parse_reply(text)
    tools = [tool_of(transaction) for transaction in transactions]
    result = {"scenario": scenario.id, "arm": arm, "tools": tools, "tier": "pass", "kind": "pass", "problems": []}

    def fail(tier, kind, *why):
        result.update(tier=tier, kind=kind, problems=list(why))
        return result

    if not transactions:
        return fail("T1", "no-request", "the reply makes no transaction" + (f" (prose: {prose[0]!r})" if prose else ""))
    for transaction, tool in zip(transactions, tools):
        if tool is None and transaction.get("bare"):   # run together or cut short: a request the checker cannot read as any form
            return fail("T3", "unframed", f"unframed and unreadable: {transaction['verb']}: {transaction['arg']!r}")
        if tool is None:
            return fail("T1", "invented-form", f"{transaction['verb']}: {transaction['arg']!r} is not a form the worker has")
        if tool not in offered:
            return fail("T1", "not-offered", f"{tool} is not offered in the {arm} arm")

    expect = {"find-tool"} if scenario.need else set(scenario.expect)
    if not set(tools) <= expect:
        unseen = any(tool == "symbol" and fields(t)[2] in scenario.folded for t, tool in zip(transactions, tools))
        if "find-tool" in tools and not scenario.need:
            kind = "false-need"
        elif scenario.need:
            kind = "substitute"
        elif unseen:
            kind = "unseen-edit"
        else:
            kind = "wrong-tool"
        return fail("T2", kind, f"the task calls for {sorted(expect)}, the reply used {sorted(set(tools))}")
    if scenario.need:
        capabilities = " ".join(t["arg"].lower() for t, tool in zip(transactions, tools) if tool == "find-tool")
        if not any(keyword in capabilities for keyword in scenario.need):
            return fail("T2", "wrong-need", f"the request names no {'/'.join(scenario.need)} capability")

    bad = list(problems)
    if prose:
        bad.append("prose outside the transactions: " + prose[0])
    if len(transactions) > 1 and any(tool in ("read-symbol", "read-lines", "find-tool") for tool in tools):
        bad.append("a read or find-tool request must be the whole reply")
    for transaction, tool in zip(transactions, tools):
        bad += check_transaction(transaction, tool, scenario)
    if any(transaction.get("bare") for transaction in transactions):
        bad.insert(0, "a request is not framed: each form is written between === and ===")
        return fail("T3", "unframed", *bad)
    if bad:
        return fail("T3", "malformed", *bad)
    return result


def wilson(passed: int, total: int, z: float = 1.959963984540054) -> tuple[float, float]:
    if total == 0:
        return 0.0, 1.0
    p = passed / total
    denominator = 1 + z * z / total
    centre = p + z * z / (2 * total)
    margin = z * math.sqrt(p * (1 - p) / total + z * z / (4 * total * total))
    return (centre - margin) / denominator, (centre + margin) / denominator


def selftest() -> list[str]:
    failures = []
    for scenario_id, arm, text, tier, kind in CONTROLS:
        got = score(SCENARIO[scenario_id], text, arm)
        want = ("pass", "pass") if tier == "pass" else (tier, kind)
        if (got["tier"], got["kind"]) != want:
            failures.append(f"{scenario_id}/{arm}: want {want}, got {(got['tier'], got['kind'])}: {got['problems']}")
    for scenario in SCENARIOS:
        outcomes = {score(scenario, REFERENCE[scenario.id], "need" if scenario.need else "plain")["tier"]}
        if outcomes != {"pass"}:
            failures.append(f"{scenario.id}: the reference reply does not pass")
        if not any(c[0] == scenario.id and c[3] != "pass" for c in CONTROLS):
            failures.append(f"{scenario.id}: no failing control")
    return failures


# -- live runner (opt-in: it calls an engine, so run it only with the owner's go-ahead) --------------------------------------------------

SYSTEM_HEAD = ("You work on files in a shared canvas. Reply with transactions in these forms and nothing else: no prose, no markdown fences.\n\n")


def system_for(scenario: Scenario, arm: str) -> str:
    offered = sorted(scenario.offered | ARM_TOOLS[arm], key=list(FORMS).index)
    return SYSTEM_HEAD + "\n".join(FORMS[tool] for tool in offered)


def user_for(scenario: Scenario) -> str:
    return f"Task: {scenario.task}\n\nFiles as they stand (the line numbers are not part of the file):\n\n{shown(scenario)}"


def chat(url, model, system, user, seed, temperature, max_tokens, timeout):
    body = json.dumps({
        "model": model,
        "messages": [{"role": "system", "content": system}, {"role": "user", "content": user}],
        "temperature": temperature, "max_tokens": max_tokens, "seed": seed, "stream": False,
        "cache_prompt": False,   # one fresh prefill per request, as the Squad profile does
    }).encode("utf-8")
    request = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(request, timeout=timeout) as response:
        payload = json.load(response)
    return payload["choices"][0]["message"]["content"] or ""


# Aliases: the names a model uses for an operation it was not given (the 2026-11-25 self-written prompts). An alias maps the intent of a call to
# a canonical operation and its arguments. A mapping is recorded beside the strict result: "aliased" means the intent was recovered, and the
# strict pass is unchanged, so a model is not credited for using our names. None means the name has no canonical operation and is unmapped.
ALIASES = {
    "read_file": ("view", {"path": "path"}),
    "list_files": ("search", {"scope": "paths", "path": "pattern"}),
    "propose_edit": ("edit", {"path": "path", "text": "body"}),
    "apply_edit": ("edit", {"path": "path", "text": "body"}),
    "replace": ("edit", {"path": "path", "pattern": "match", "replacement": "body"}),
    "request_review": ("request", {"capability": "capability"}),
    "comment": (None, {}),
}


def canonical_call(name: str, arguments: dict) -> dict | None:
    """The canonical operation a call intends, or None when the name has no operation. Arguments are renamed by the alias table."""
    if name not in ALIASES:
        return None
    operation, renames = ALIASES[name]
    if operation is None:
        return None
    return {"operation": operation, "alias": name, "arguments": {renames[key]: value for key, value in arguments.items() if key in renames}}


KNOW_QUESTIONS = {
    "forms": "Which request forms could you use to read or change files? Give the exact header line of each one.",
    "read-symbol": "Explain how to read a folded function before you edit it. Give one exact example line.",
    "read-lines": "Explain how to read lines 10 to 20 of a file. Give one exact example line.",
    "edit-symbol": "Explain how to replace a whole function. Give the exact header line.",
    "edit-search": "Explain how to change three lines in the middle of a file. Give the exact header and markers.",
    "file": "When would you write a whole file rather than an edit? Give the exact header line.",
    "find-tool": "If a task needs a tool that none of your request forms provides, what do you send? Give the exact line.",
}
# The pattern a correct answer to each question must show; "forms" is judged on what it names.
KNOW_EXPECT = {"read-symbol": "read-symbol", "read-lines": "read-lines", "edit-symbol": "symbol", "edit-search": "search",
               "file": "file", "find-tool": "find-tool"}
KNOW_PATTERN = {"file": r"===\s*FILE:", "symbol": r"EDIT:[^\n]*SYMBOL", "search": r"SEARCH", "read-symbol": r"READ:[^\n]*SYMBOL",
                "read-lines": r"READ:[^\n]*LINES", "after": r"AFTER", "find-tool": r"FIND TOOL"}
FRAMED = re.compile(r"^=== (FILE|EDIT|READ|FIND TOOL):", re.M)
SELF_PROMPT = ("Write the system prompt you would give a model that must edit files in a shared canvas. "
               "Include the request forms yourself and say when to use each.")
KNOW_CONDITIONS = {
    "tools-shown": system_for(SCENARIO["need-format"], "need"),
    "no-tools": "You are a helpful assistant.",
}


def know_scores(answer: str) -> dict:
    found = sorted(name for name, pattern in KNOW_PATTERN.items() if re.search(pattern, answer))
    return {"forms": found, "framed": bool(FRAMED.search(answer))}


def know(arguments) -> int:
    """Ask the model what request forms it knows, with and without the forms in its system prompt. Measurement only."""
    out = pathlib.Path(arguments.out)
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f"{out} is not empty: give a new output directory")
    (out / "replies").mkdir(parents=True)
    results = []
    for condition, system in KNOW_CONDITIONS.items():
        for question, ask in KNOW_QUESTIONS.items():
            for sample in range(arguments.samples):
                seed = arguments.seed + sample
                answer = chat(arguments.url, arguments.model, system, ask, seed, arguments.temperature, arguments.max_tokens,
                              arguments.timeout)
                answer = re.sub(r"<think>.*?</think>", "", answer, flags=re.S).strip()
                name = f"{condition}-{question}-{seed}"
                (out / "replies" / f"{name}.txt").write_text(answer, encoding="utf-8")
                scores = know_scores(answer)
                expected = KNOW_EXPECT.get(question)
                results.append({"condition": condition, "question": question, "seed": seed, **scores,
                                "expectedForm": expected, "knowsExpected": expected is None or expected in scores["forms"]})
    for sample in range(arguments.samples):   # the model's own system prompt, unaided
        seed = arguments.seed + sample
        text = chat(arguments.url, arguments.model, "You are a helpful assistant.", SELF_PROMPT, seed, arguments.temperature,
                    arguments.max_tokens * 2, arguments.timeout)
        text = re.sub(r"<think>.*?</think>", "", text, flags=re.S).strip()
        (out / "replies" / f"self-prompt-{seed}.txt").write_text(text, encoding="utf-8")
        results.append({"condition": "self-prompt", "question": "self-prompt", "seed": seed, "forms": know_scores(text)["forms"],
                        "framed": bool(FRAMED.search(text)), "expectedForm": None, "knowsExpected": None})
    (out / "summary.json").write_text(json.dumps({"schema": "t0-tools-know-1", "model": arguments.model, "samples": arguments.samples,
                                                  "seed": arguments.seed, "temperature": arguments.temperature,
                                                  "maxTokens": arguments.max_tokens, "results": results}, indent=2), encoding="utf-8")
    print(f"{len(results)} answers written to {out}")
    for condition in list(KNOW_CONDITIONS) + ["self-prompt"]:
        rows = [r for r in results if r["condition"] == condition and r["knowsExpected"] is not None]
        if rows:
            print(f"{condition:12} expected form named in {sum(r['knowsExpected'] for r in rows)}/{len(rows)} answers")
    return 0


def capture(scenario: Scenario, arm: str, seed: int, text: str, row: dict) -> list[dict]:
    """Every FIND TOOL request in a reply, true or false, bare or framed, with its verdict."""
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]
    found = []
    for transaction in parse_reply(text)[0]:
        if tool_of(transaction) == "find-tool":
            verdict = "named" if row["tier"] == "pass" and scenario.need else row["kind"]
            found.append({"scenario": scenario.id, "arm": arm, "seed": seed, "capability": transaction["arg"], "verdict": verdict,
                          "reply": digest, "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
    return found


def rescore(out: pathlib.Path) -> int:
    """Judge the saved replies again with the current checker. No model is called."""
    record = json.loads((out / "summary.json").read_text(encoding="utf-8"))
    arm, rows, needs = record["arm"], [], []
    for row in record["rows"]:
        if row["tier"] == "infra":
            rows.append(row)
            continue
        scenario = SCENARIO[row["scenario"]]
        text = (out / "replies" / f"{scenario.id}-{arm}-{row['seed']}.txt").read_text(encoding="utf-8")
        judged = score(scenario, text, arm) | {"seed": row["seed"]}
        rows.append(judged)
        needs.extend(capture(scenario, arm, row["seed"], text, judged))
    record["rows"] = rows
    (out / "summary.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    with (out / "tool-needs.jsonl").open("w", encoding="utf-8") as handle:
        for need in needs:
            handle.write(json.dumps(need) + "\n")
    return publish(out, len(needs))


def run_live(arguments) -> int:
    out = pathlib.Path(arguments.out)
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f"{out} is not empty: give a new output directory")
    (out / "replies").mkdir(parents=True, exist_ok=True)
    rows, needs = [], []
    for scenario in SCENARIOS:
        for sample in range(arguments.samples):
            seed = arguments.seed + sample
            try:
                text = chat(arguments.url, arguments.model, system_for(scenario, arguments.arm), user_for(scenario), seed,
                            arguments.temperature, arguments.max_tokens, arguments.timeout)
            except (OSError, ValueError, KeyError) as error:
                rows.append({"scenario": scenario.id, "arm": arguments.arm, "seed": seed, "tier": "infra", "kind": "engine-failed",
                             "problems": [str(error)], "tools": []})
                continue
            text = re.sub(r"<think>.*?</think>", "", text, flags=re.S).strip()
            (out / "replies" / f"{scenario.id}-{arguments.arm}-{seed}.txt").write_text(text, encoding="utf-8")
            row = score(scenario, text, arguments.arm) | {"seed": seed}
            rows.append(row)
            needs.extend(capture(scenario, arguments.arm, seed, text, row))
    with (out / "tool-needs.jsonl").open("w", encoding="utf-8") as handle:
        for need in needs:
            handle.write(json.dumps(need) + "\n")
    (out / "summary.json").write_text(json.dumps({"arm": arguments.arm, "model": arguments.model, "samples": arguments.samples,
                                                  "seed": arguments.seed, "temperature": arguments.temperature,
                                                  "maxTokens": arguments.max_tokens,
                                                  "rows": rows}, indent=2), encoding="utf-8")
    return publish(out, len(needs))


def transcript(out: pathlib.Path, record: dict) -> str:
    """Every trial as it was sent and answered: the system prompt, the user message, the reply and the verdict.

    The prompts are rebuilt from the scenario and arm with the same functions the run used. The reply is the text after any
    reasoning block was stripped, which is what the checker judged; the stripped reasoning is not kept.
    """
    arm = record["arm"]
    lines = [f"# Tool-use probe, arm {arm}", "",
             f"Model `{record['model']}`, {record['samples']} samples per scenario (seeds as in each heading), "
             f"temperature {record.get('temperature', 'not recorded')}, max tokens {record.get('maxTokens', 'not recorded')}, "
             "prompt caching off. Not the T0 gate.", ""]
    for row in record["rows"]:
        scenario = SCENARIO[row["scenario"]]
        label = f"{scenario.id} · seed {row['seed']} · " + ("engine failed" if row["tier"] == "infra" else
                                                             ("pass" if row["tier"] == "pass" else f"{row['tier']} {row['kind']}"))
        lines += [f"## {label}", "", f"Task: {scenario.task}", "", "### Input: system prompt", "",
                  "````", system_for(scenario, arm), "````", "", "### Input: user message", "", "````", user_for(scenario), "````", ""]
        if row["tier"] == "infra":
            lines += ["### Model output", "", "No reply: " + "; ".join(row["problems"]), ""]
        else:
            reply_text = (out / "replies" / f"{scenario.id}-{arm}-{row['seed']}.txt").read_text(encoding="utf-8")
            lines += ["### Model output", "", "````", reply_text.rstrip("\n"), "````", ""]
            if row["tier"] != "pass":
                lines += ["Verdict: " + row["tier"] + " " + row["kind"] + " · " + "; ".join(row["problems"]), ""]
    return "\n".join(lines)


def publish(out: pathlib.Path, needs_captured: int | None = None) -> int:
    """Write the portal fields into summary.json (a `cases` object in the shape the Work tab's gate adapter reads).

    The probe is a measurement, not the T0 gate: its verdict is "measured", and its detail says so.
    """
    record = json.loads((out / "summary.json").read_text(encoding="utf-8"))
    rows = record["rows"]
    summary = summarise(rows)
    if needs_captured is None:
        needs_captured = sum(1 for line in (out / "tool-needs.jsonl").read_text(encoding="utf-8").splitlines() if line.strip())
    cases = {scenario_id: {"state": "measured", "runs": item["counted"], "passed": item["passed"]}
             for scenario_id, item in summary.items()}
    counted = sum(item["counted"] for item in summary.values())
    passed = sum(item["passed"] for item in summary.values())
    infra = sum(item["infra"] for item in summary.values())
    record.update({
        "schema": "t0-tools-1",
        "verdict": "measured",
        "engine": record["model"],
        "build": "not recorded",
        "detail": (f"Tool-use probe, arm {record['arm']}: not the T0 gate. {passed} of {counted} counted replies pass all tiers"
                   f" ({infra} engine failures kept apart). Tool needs captured: {needs_captured}."),
        "cases": cases,
        "scenarios": summary,
    })
    (out / "summary.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    (out / "transcript.md").write_text(transcript(out, record), encoding="utf-8")
    for scenario_id, item in summary.items():
        low, high = wilson(item["passed"], item["counted"])
        print(f"{scenario_id:22} {item['passed']}/{item['counted']} pass  95% {low:.2f}-{high:.2f}  "
              f"fails {dict(item['tiers'])}  infra {item['infra']}")
    print(f"tool needs captured: {needs_captured} -> {out / 'tool-needs.jsonl'}")
    return 0


def summarise(rows) -> dict:
    result = {}
    for row in rows:
        item = result.setdefault(row["scenario"], {"passed": 0, "counted": 0, "infra": 0, "tiers": collections.Counter(),
                                                   "kinds": collections.Counter()})
        if row["tier"] == "infra":
            item["infra"] += 1
            continue
        item["counted"] += 1
        item["passed"] += row["tier"] == "pass"
        if row["tier"] != "pass":
            item["tiers"][row["tier"]] += 1
            item["kinds"][row["kind"]] += 1
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="the scenarios and the forms each offers")
    commands.add_parser("selftest", help="check the scorer against its scripted controls (no model)")
    republish = commands.add_parser("publish", help="add the portal fields to a finished run's summary.json")
    republish.add_argument("out")
    know_parser = commands.add_parser("know", help="ask the model which request forms it knows, with and without them shown (opt-in)")
    know_parser.add_argument("--out", required=True)
    know_parser.add_argument("--url", default="http://127.0.0.1:8092/v1/chat/completions")
    know_parser.add_argument("--model", required=True)
    know_parser.add_argument("--samples", type=int, default=3)
    know_parser.add_argument("--seed", type=int, default=7101)
    know_parser.add_argument("--temperature", type=float, default=0.7)
    know_parser.add_argument("--max-tokens", type=int, default=600)
    know_parser.add_argument("--timeout", type=float, default=240)
    rejudge = commands.add_parser("rescore", help="judge a finished run's saved replies again with this checker (no model)")
    rejudge.add_argument("out")
    live = commands.add_parser("run", help="call an engine on every scenario (opt-in)")
    live.add_argument("--out", required=True)
    live.add_argument("--url", default="http://127.0.0.1:8092/v1/chat/completions")
    live.add_argument("--model", required=True)
    live.add_argument("--arm", choices=sorted(ARM_TOOLS), default="plain")
    live.add_argument("--samples", type=int, default=3)
    live.add_argument("--seed", type=int, default=6101)
    live.add_argument("--temperature", type=float, default=0.7)
    live.add_argument("--max-tokens", type=int, default=800)
    live.add_argument("--timeout", type=float, default=240)
    arguments = parser.parse_args(argv)
    if arguments.command == "list":
        for scenario in SCENARIOS:
            expects = ["find-tool"] if scenario.need else sorted(scenario.expect)
            need = f"  needs {list(scenario.need)}" if scenario.need else ""
            print(f"{scenario.id:22} offers {sorted(scenario.offered)}  expects {expects}{need}")
        return 0
    if arguments.command == "selftest":
        failures = selftest()
        for failure in failures:
            print("FAIL", failure)
        print(f"selftest: {len(CONTROLS)} controls, {len(failures)} failures")
        return 1 if failures else 0
    if arguments.command == "publish":
        return publish(pathlib.Path(arguments.out))
    if arguments.command == "know":
        return know(arguments)
    if arguments.command == "rescore":
        return rescore(pathlib.Path(arguments.out))
    return run_live(arguments)


if __name__ == "__main__":
    sys.exit(main())
