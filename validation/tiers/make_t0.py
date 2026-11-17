"""Write a tier 0 plan for the C++ calculator (python validation/tiers/make_t0.py <repo> a|b): a = fix one injected bug, b = write one stubbed function.
Both seed the reference engine (validation/reference/calculator-cpp) and give its eval tests; the item is src/calc.cpp alone. Built on validation/tiers/t1-plan.json
(the tier 1 plan); written to out/runs/single/. Run with the "single" process on the canvas executor."""
import json, pathlib, re, sys

root = pathlib.Path(sys.argv[1])
variant = sys.argv[2] if len(sys.argv) > 2 else "a"
ref = root / "validation/reference/calculator-cpp"
calc = (ref / "src/calc.cpp").read_text(encoding="utf-8")
if variant == "a":   # one wrong line
    good = "v = std::fmod(v, d); }"
    assert calc.count(good) == 1, "the defect site is not unique"
    defective = calc.replace(good, "v = v / d; }")
else:                # one function emptied to a stub: written into a skeleton that otherwise works
    head = "    double call(const std::string& f, const std::vector<double>& a)\n    {\n"
    start = calc.index(head)
    end = calc.index("\n    }\n", start) + len("\n    }\n")
    defective = calc[:start] + head + "        (void)a;\n        fail(\"not implemented: \" + f);\n    }\n" + calc[end:]
templates = (root / "src/cpp_templates.cpp").read_text(encoding="utf-8")
header = re.search(r'const char\* kHeader = R"CPP\((.*?)\)CPP";', templates, re.S).group(1)

base = json.loads((root / "validation/tiers/t1-plan.json").read_text(encoding="utf-8"))
item = base["items"][0]
item.update({
    "id": "app",
    "title": "Calculator engine: fix the failing test",
    "goal": "Make every test in tests/eval.test.cpp pass by fixing src/calc.cpp",
    "complexity": 1,
    "files": ["src/calc.cpp"],
})
item["card"].update({
    "context": "src/calc.cpp is a complete calculator engine that nearly works: run against the provided tests, some fail. Find the defect and fix it with the smallest edit. Do not rewrite the file." if variant == "a" else
               "src/calc.cpp is a complete calculator engine except for one function, call(), which is still a stub: it must apply a named function (sin, pow, ...) to its arguments. Write that one function. Do not rewrite the file.",
    "contract": item["card"]["contract"].split("\nAPP:")[0],
    "acceptance": ["every test in tests/eval.test.cpp passes", "C++20, standard library only, no warnings"],
    "include": ["tests/eval.test.cpp"],
    "notes": ["Output only src/calc.cpp"],
})
item["gate"].update({"tests": ["tests/eval.test.cpp"], "minTests": 15})
plan = dict(base)
plan.update({
    "task": "tier 0a: fix one bug in a nearly working calculator engine" if variant == "a" else "tier 0b: write one function into a working calculator engine",
    "project": "t0a-fix-one-bug" if variant == "a" else "t0b-one-function",
    "reasons": ["tier 0a spike: one worker on the canvas fixes one injected defect (modulo divides instead of fmod) in the reference engine" if variant == "a" else
                "tier 0b spike: one worker on the canvas writes call() (the function dispatch, about 30 lines) into the reference engine where it is a stub"],
    "scaffold": [
        {"path": "include/calc/calc.hpp", "content": header},
        {"path": "src/calc.cpp", "content": defective},
        {"path": "src/main.cpp", "content": (ref / "src/main.cpp").read_text(encoding="utf-8")},
    ],
    "given": [{"path": "tests/eval.test.cpp", "content": (ref / "tests/eval.test.cpp").read_text(encoding="utf-8")}],
    "items": [item],
})
out = root / f"out/runs/single/t0{variant}-plan.json"
if variant != "a": item["title"] = "Calculator engine: write call()"; item["goal"] = "Write call() in src/calc.cpp so every test in tests/eval.test.cpp passes"
out.write_text(json.dumps(plan, indent=1), encoding="utf-8")
print(out, [f["path"] for f in plan["scaffold"]], item["files"])
