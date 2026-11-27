"""Conservatively inspect captured single calls; this does not execute them or prove semantic validity."""
import ast
import json
import pathlib
import sys

MARKER = "<|content_invoke_tool_json|>"


def inspect(text):
    text = text.strip()
    if MARKER in text:
        prefix, payload = text.split(MARKER, 1)
        try:
            call = json.loads(payload)
        except ValueError:
            return {"format": "native-marker", "parsed": False}
        if not isinstance(call, dict) or set(call) != {"name", "args"} or not isinstance(call["name"], str) or not isinstance(call["args"], dict) or prefix != call["name"]:
            return {"format": "native-marker", "parsed": False}
        return {"format": "native-marker", "parsed": True, "name": call["name"], "args": call["args"]}
    try:
        node = ast.parse(text, mode="eval").body
        if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Name) or node.args or any(k.arg is None for k in node.keywords):
            raise ValueError("not a keyword call")
        args = {k.arg: ast.literal_eval(k.value) for k in node.keywords}
        if len(args) != len(node.keywords):
            raise ValueError("duplicate argument")
        return {"format": "keyword-call", "parsed": True, "name": node.func.id, "args": args}
    except (SyntaxError, ValueError, TypeError):
        return {"format": "unknown", "parsed": False}


def main(path):
    manifest = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    rows = []
    for event in manifest["events"]:
        if event["label"].startswith("call-") and "response" in event:
            expected = event["label"].split("-")[1]
            result = inspect(event["response"]["content"])
            rows.append({"label": event["label"], "expected": expected, **result,
                         "name_matches_requested_task": result.get("name") == expected if result["parsed"] else None})
    print(json.dumps({"note": "Syntax and name only; no schema, guard, effect or task-completion verdict.", "results": rows}, indent=2))


if __name__ == "__main__":
    main(sys.argv[1])
