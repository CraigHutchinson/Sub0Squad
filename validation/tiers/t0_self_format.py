"""Record a model-authored tool protocol and fresh-context checks. Experimental, serial only."""
import argparse
import json
import pathlib
import urllib.request

from t0_tools import OPERATIONS, SELF_PROMPT

DEFINITIONS = "\n".join(f"- {name}: {description}" for name, description in OPERATIONS.items())
INTEGRATE = ("Revise your system prompt to include the following required operations. Preserve your preferred style. "
             "Define a callable form and required arguments for each; return the complete revised system prompt only.\n" + DEFINITIONS)
CHECK = ("Check the system prompt you were given for each required operation below. Return JSON only: "
         '{"view":true,"search":true,"edit":true,"rename":true,"request":true}, using false for any missing or undefined operation.\n'
         + DEFINITIONS)
EXAMPLES = {
    "view": "Demonstrate a view call reading lines 2 through 4 of src/a.cpp.",
    "search": "Demonstrate a search call finding the literal text 'counter' in src/a.cpp.",
    "edit": "Demonstrate an edit call replacing 'old' with 'new' in src/a.cpp, expecting one match. The file was inspected at base version v1 and the user authorized the change.",
    "rename": "Demonstrate a rename call moving src/a.cpp at inspected base version v1 to src/b.cpp, which does not exist. The user authorized the move.",
    "request": "Demonstrate a request call for a capability to run the test suite; do not modify files.",
}


def check_report(text):
    """Accept only an explicit complete boolean report; never infer missing fields."""
    try:
        value = json.loads(text)
    except (ValueError, TypeError):
        return None
    if not isinstance(value, dict) or set(value) != set(OPERATIONS) or any(type(v) is not bool for v in value.values()):
        return None
    return value


def request(url, model, messages, seed, temperature, tokens, timeout):
    body = json.dumps({"model": model, "messages": messages, "seed": seed, "temperature": temperature,
                       "max_tokens": tokens, "stream": False, "cache_prompt": False}).encode()
    req = urllib.request.Request(url, body, {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        result = json.load(response)
    choice = result["choices"][0]
    return {"content": choice["message"].get("content") or "", "finish_reason": choice.get("finish_reason"),
            "usage": result.get("usage"), "message": choice["message"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True)
    parser.add_argument("--url", default="http://127.0.0.1:8092/v1/chat/completions")
    parser.add_argument("--model", required=True)
    parser.add_argument("--seed", type=int, default=8101)
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--max-revisions", type=int, default=2)
    parser.add_argument("--max-tokens", type=int, default=2048)
    parser.add_argument("--temperature", type=float, default=0.7)
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--prompt-from", help="replay a captured complete candidate from a prior manifest; skips generation and self-check")
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=False)
    manifest = {"schema": "self-format-1", "settings": vars(args), "events": []}

    def call(label, messages, seed):
        event = {"label": label, "seed": seed, "messages": messages}
        manifest["events"].append(event)
        save()
        event["response"] = request(args.url, args.model, messages, seed, args.temperature, args.max_tokens, args.timeout)
        save()
        if event["response"]["finish_reason"] != "stop":
            raise RuntimeError(f"{label}: incomplete response; see manifest")
        return event["response"]["content"]

    def save():
        (out / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")

    def pair(system, user):
        return [{"role": "system", "content": system}, {"role": "user", "content": user}]

    try:
        if args.prompt_from:
            previous = json.loads(pathlib.Path(args.prompt_from).read_text(encoding="utf-8"))
            completed = [e for e in previous["events"] if e["label"].startswith(("integrate", "revision-"))
                         and e.get("response", {}).get("finish_reason") == "stop"]
            if not completed:
                raise ValueError("no complete candidate in the source manifest")
            prompt = completed[-1]["response"]["content"]
            manifest["replayed_prompt"] = prompt
            save()
        else:
            draft = call("draft", pair("You are a helpful assistant.", SELF_PROMPT), args.seed)
            prompt = call("integrate", [{"role": "system", "content": "You are a helpful assistant."},
                                    {"role": "user", "content": SELF_PROMPT}, {"role": "assistant", "content": draft},
                                    {"role": "user", "content": INTEGRATE}], args.seed)
            for revision in range(args.max_revisions + 1):
                report = call(f"check-{revision}", pair(prompt, CHECK), args.seed)
                parsed = check_report(report)
                manifest["events"][-1]["parsed_report"] = parsed
                save()
                if parsed and all(parsed.values()):
                    break
                if revision == args.max_revisions:
                    raise RuntimeError("model did not confirm every operation in a valid report")
                prompt = call(f"revision-{revision + 1}", pair(prompt, "Produce a complete corrected system prompt. "
                              "Address this validation report and include every required operation:\n" + report + "\n" + DEFINITIONS), args.seed)
        for sample in range(args.samples):
            for operation, task in EXAMPLES.items():
                call(f"call-{operation}-{args.seed + sample}", pair(prompt, task + " Respond with one tool call only, in the form specified by your system prompt."), args.seed + sample)
        manifest["status"] = "captured; independent protocol and argument review required"
    except Exception as exc:
        manifest["status"] = "incomplete"
        manifest["error"] = str(exc)
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
