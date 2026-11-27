"""Fixed, independently checked exercises for the proposed five-operation tool contract.

These are prompt experiments, not a production parser or authorization to execute calls.
"""
import json

SYSTEM = '''You work on a shared file canvas. Return exactly one JSON object and no other text.
The object has exactly two keys: "operation" and "arguments". Use these operations:
- view: arguments path (string), start (integer), end (integer); reads inclusive lines.
- search: arguments path (string), pattern (string), mode ("literal" or "regex"), scope ("content" or "paths").
- edit: arguments path (string), pattern (string), mode ("literal" or "regex"), expected_count (integer), replacement (string). The harness refuses the write if the match count differs.
- rename: arguments source (string), destination (string). The harness refuses an existing destination. A destination under .bin/ is a recoverable deletion.
- request: arguments capability (string). This records a need and changes nothing.
Use the operation the task explicitly asks to demonstrate. Do not invent arguments or call other operations.'''

# These exact exercises are visible to the model. Hold a separate set out before using the result to adopt a prompt.
CASES = {
    "view": ("Demonstrate viewing lines 2 through 4 of src/a.cpp.", {"operation": "view", "arguments": {"path": "src/a.cpp", "start": 2, "end": 4}}),
    "search": ("Demonstrate a literal content search for counter in src/a.cpp.", {"operation": "search", "arguments": {"path": "src/a.cpp", "pattern": "counter", "mode": "literal", "scope": "content"}}),
    "edit": ("Demonstrate a literal edit in src/a.cpp replacing old with new, expecting exactly one match.", {"operation": "edit", "arguments": {"path": "src/a.cpp", "pattern": "old", "mode": "literal", "expected_count": 1, "replacement": "new"}}),
    "rename": ("Demonstrate moving src/a.cpp to src/b.cpp, a path that does not exist.", {"operation": "rename", "arguments": {"source": "src/a.cpp", "destination": "src/b.cpp"}}),
    "request": ("Demonstrate requesting the capability run_tests.", {"operation": "request", "arguments": {"capability": "run_tests"}}),
}

HOLDOUT_CASES = {
    "view": ("Show lines 9 through 11 of lib/item.h using view.", {"operation": "view", "arguments": {"path": "lib/item.h", "start": 9, "end": 11}}),
    "search": ("Use search with regex mode and content scope to find ^TODO in notes.txt.", {"operation": "search", "arguments": {"path": "notes.txt", "pattern": "^TODO", "mode": "regex", "scope": "content"}}),
    "edit": ("Use a literal edit of x to y in lib/item.h; the expected match count is two.", {"operation": "edit", "arguments": {"path": "lib/item.h", "pattern": "x", "mode": "literal", "expected_count": 2, "replacement": "y"}}),
    "rename": ("Use rename to move notes.txt into the recoverable bin at .bin/notes.txt.", {"operation": "rename", "arguments": {"source": "notes.txt", "destination": ".bin/notes.txt"}}),
    "request": ("Use request to ask for the capability list_branches.", {"operation": "request", "arguments": {"capability": "list_branches"}}),
}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate key")
        result[key] = value
    return result


def score(text, expected):
    try:
        value = json.loads(text, object_pairs_hook=unique_object)
    except (ValueError, TypeError):
        return {"parse": False, "operation": False, "exact": False}
    if not isinstance(value, dict) or set(value) != {"operation", "arguments"} or not isinstance(value["arguments"], dict):
        return {"parse": False, "operation": False, "exact": False}
    return {"parse": True, "operation": value["operation"] == expected["operation"],
            "exact": json.dumps(value, sort_keys=True) == json.dumps(expected, sort_keys=True)}
