"""Run fixed prompt exercises serially; each request and result is kept as it completes."""
import argparse
import json
import pathlib

from t0_contract import CASES, HOLDOUT_CASES, SYSTEM, score
from t0_self_format import request


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True)
    p.add_argument("--model", required=True)
    p.add_argument("--url", default="http://127.0.0.1:8092/v1/chat/completions")
    p.add_argument("--prompt", help="UTF-8 system prompt; omit for the fixed baseline")
    p.add_argument("--seed", type=int, default=9201)
    p.add_argument("--samples", type=int, default=3)
    p.add_argument("--max-tokens", type=int, default=1024)
    p.add_argument("--temperature", type=float, default=0.7)
    p.add_argument("--timeout", type=int, default=300)
    p.add_argument("--heldout", action="store_true", help="use the separately specified cases, without changing the scorer")
    args = p.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=False)
    system = pathlib.Path(args.prompt).read_text(encoding="utf-8") if args.prompt else SYSTEM
    run = {"schema": "tool-contract-exercise-1", "settings": vars(args), "system": system,
           "kpi": "all five exact host-checked calls per seed; report individual cases as well", "results": []}

    def save():
        (out / "manifest.json").write_text(json.dumps(run, indent=2, ensure_ascii=False), encoding="utf-8")

    try:
        for sample in range(args.samples):
            for name, (task, expected) in (HOLDOUT_CASES if args.heldout else CASES).items():
                messages = [{"role": "system", "content": system}, {"role": "user", "content": task}]
                row = {"seed": args.seed + sample, "case": name, "messages": messages, "expected": expected}
                run["results"].append(row)
                save()
                row["response"] = request(args.url, args.model, messages, args.seed + sample, args.temperature, args.max_tokens, args.timeout)
                row["score"] = score(row["response"]["content"], expected)
                if row["response"]["finish_reason"] != "stop":
                    row["score"] = {"parse": False, "operation": False, "exact": False}
                save()
        run["status"] = "complete"
    except Exception as exc:
        run["status"] = "incomplete"
        run["error"] = str(exc)
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
