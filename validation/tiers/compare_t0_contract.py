"""Compare two complete runs on identical exercises and seeds, without inventing a significance claim."""
import json
import pathlib
import sys


def compare(left, right):
    if left.get("status") != "complete" or right.get("status") != "complete":
        raise ValueError("both runs must be complete")
    for field in ("model", "seed", "samples", "temperature", "max_tokens", "heldout"):
        if left["settings"].get(field) != right["settings"].get(field):
            raise ValueError(f"different {field}")
    def rows(run):
        result = {}
        for row in run["results"]:
            key = (row["seed"], row["case"])
            if key in result:
                raise ValueError("duplicate seed/case")
            result[key] = row
        return result
    a, b = rows(left), rows(right)
    if not a or a.keys() != b.keys():
        raise ValueError("different exercises")
    for key in a:
        if a[key]["expected"] != b[key]["expected"] or a[key]["messages"][-1] != b[key]["messages"][-1]:
            raise ValueError("different task or expected result")
    outcomes = [(a[k]["score"]["exact"], b[k]["score"]["exact"]) for k in a]
    seeds = {seed for seed, _ in a}
    return {"cases": len(a), "baseline_exact": sum(x for x, _ in outcomes), "candidate_exact": sum(y for _, y in outcomes),
            "improved_cases": sum(not x and y for x, y in outcomes), "regressed_cases": sum(x and not y for x, y in outcomes),
            "baseline_all_cases": sum(all(a[k]["score"]["exact"] for k in a if k[0] == seed) for seed in seeds),
            "candidate_all_cases": sum(all(b[k]["score"]["exact"] for k in b if k[0] == seed) for seed in seeds), "seeds": len(seeds)}


if __name__ == "__main__":
    runs = [json.loads(pathlib.Path(path).read_text(encoding="utf-8")) for path in sys.argv[1:3]]
    print(json.dumps(compare(*runs), indent=2))
