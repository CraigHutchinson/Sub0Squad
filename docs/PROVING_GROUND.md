# Proving ground: a portfolio of sandbox projects

Status: design, 2026-11-11. Working titles only; no project name is chosen (that is the owner's call).

One sandbox proves one thing. The harness has to be shown to work across **different toolchains, different feedback signals and different sizes**, or
every mend we make fits only the web calculator. The proving ground is therefore a small ladder of projects, each chosen to exercise something the
previous one cannot.

## 1. The ladder

| rung | project | harness | size (items) | what it proves that the rung below cannot |
|---|---|---|---|---|
| 1 | calculator-web (exists) | `web-static` | 4 to 12 | the V, test-first, ladder, 9B workers, TAP feedback |
| 2 | calculator-cpp | `cpp-cmake` | 4 to 12 | a second toolchain: compile diagnostics as feedback, CMake, sanitizers, a CLI app, text-screen capture |
| 2b | calculator-py (third platform) | `py-unittest` | 4 to 12 | that the calculator contract is platform-independent: if a blueprint needs more than a surface, the abstraction is wrong |
| 3 | spreadsheet (web UI and native core) | both | 20 to 40 | reuse (the calculator engine becomes a part), cross-harness contracts, a dependency graph with cycles, UI capture |
| 4 | **knowledge store** (native core, web viewer; `docs/KNOWLEDGE_STORE.md`) | both | 50 to 60 | depth: layers with versioned contracts, property and crash tests, model-based oracle, sanitizers; related to, and comparable with, the squad's own register (still a sandbox project, not adopted) |
| side | CHIP-8 or 6502 emulator | `cpp-cmake` + capture | 15 to 30 | pixel/framebuffer capture as a gate (test ROMs show a known image) |

Each rung reuses the one below, so a regression in a lower rung is visible in every higher one.

## 2. Brainstorm: the larger project

Criteria: **complex** (many layers, real seams), **useful** (we would keep it), **decomposable** (the V has real work: contracts that change when a
leaf is drafted), **checkable without a person** (an oracle or exact spec), and **sized in stages** (a thin first slice, then growth, so the harness is
tested on rework, not just a first build).

| idea | oracle | layers | why it is good | why it may not be |
|---|---|---|---|---|
| **Spreadsheet** (formulas, dependency graph, recalculation, grid UI, CSV/JSON import) | formula semantics are small and exact; differential test against the calculator engine and a naive recalculator | parser, evaluator, dependency graph, recalc scheduler, storage, UI | grows out of rung 2 and reuses it; useful; web and native both make sense; cycles and incremental recalculation are real design decisions | no external reference engine |
| **Mini SQL database** (lexer, parser, planner, executor, B-tree pages, WAL) | SQLite: run the same statements, compare rows (differential testing) | five or six strict layers | the best V-model exercise: contracts between layers must be amended when storage is drafted; fuzzing and crash recovery give rich feedback | big; storage code is hard for a 9B; best as the stress test |
| **Content-addressed VCS** (object store, diff, three-way merge, log) | real `git`: same repo operations, compare object ids and output | objects, index, diff, merge, CLI | git is the oracle and the user is a developer; merge is subtle | git's format is fiddly to match |
| **Build tool** (a small make: dependency graph, staleness, parallel execution) | `make` and `ninja` on the same Makefiles | parser, graph, scheduler, executor | the squad could use it on itself | overlaps the daemon's own scheduling |
| **Markdown notes** (parser, index, full-text search, backlinks, CLI and web viewer) | CommonMark spec examples (a published test suite) | parser, index, search, render, UI | spec examples are a free oracle; very useful | little depth below the parser |
| **Emulator** (CHIP-8, then 6502) | published test ROMs with known final screens | CPU, memory, display, input, UI | exact spec; the framebuffer is a natural screen-capture gate | narrow; little reuse |

### Decision (owner, 2026-11-11)

The larger project is the **knowledge store** (section 19 of `docs/WORK_GRAPH.md`, designed in `docs/KNOWLEDGE_STORE.md`): useful, relevant to Sub0Squad itself, and it
has the depth of the database idea with a model-based oracle in place of SQLite. The spreadsheet and SQL database stay as later rungs. The earlier
recommendation is kept below for the record.

### Earlier recommendation

1. **Spreadsheet is the flagship** (rung 3): it grows out of the calculator, so the proving ground compounds instead of branching; it needs both
   harnesses (native core, web grid); and it has real structure for the V (a dependency graph whose design depends on how the evaluator is drafted).
2. **Mini SQL database is the stress test** (rung 4) once the process engine can do shadow runs: it is the only idea with an external differential oracle
   and enough layers for contract amendment up-transfer to matter.
3. **CHIP-8 emulator is the capture specialist**: build it only to prove the framebuffer gate.
4. Markdown notes is the cheap "useful" fallback if a rung needs to be added quickly (CommonMark's own examples are the oracle).

Each is built in stages (slice 1 tiny, slice 2 adds a layer, slice 3 changes a contract) so the suite measures **rework**, not only first builds.

## 3. What the suite measures (per run, per rung)

Pass/fail is not enough. Every run records: items, attempts per item, ladder steps used, first-pass rate by persona, tokens and wall time per item,
how many contract amendments, how many manual mends (a run that needed one is a failure of the harness, row in `docs/INTERVENTIONS.md`), the build sha,
and whether the final deliverable ran. A rung is "proven" when the same plan passes N times in a row on the 9B with zero manual mends.

## 4. What a second harness forces us to generalise

Reading the code for `cpp-cmake` found these `web-static` assumptions outside the harness itself; each is fixed by making it harness data:

| assumption | where | fix |
|---|---|---|
| gate kind is the literal `node-test` | `plan.cpp`, `division.cpp`, `architect.cpp`, `triage.cpp` | `Harness::gateKind`; validation and division ask the registry |
| job commands are dispatched by literal name | `sub0squad.cpp` | dispatch loops over the registry's gate and integrate commands |
| blueprints come from one function | `blueprintJson`, `client.cpp`, `sandbox.cpp` | `Harness::blueprint(name)` |
| TAP parsing, process running, file helpers live inside `harness_web.cpp` | `harness_web.cpp` | extract to `harness_common` (layer 3) shared by every harness |
| triage maps wording to `web-static` or the legacy header-only type | `triage.cpp` | triage asks each harness whether the task text is its project type |
| division and plan prompts speak of ES modules | `division.cpp`, `architect.cpp` | the brief and card boilerplate come from the harness |

## 5. The `cpp-cmake` harness

* **Workspace:** harness-provided `CMakeLists.txt` (C++20, `-Wall -Wextra -Werror`, one library from `src/*.cpp` except `main.cpp`, an app `calc_cli`,
  one test executable per `tests/*.test.cpp`), a tiny test framework `tests/helpers/check.hpp` that prints TAP in the same shape the web runner does
  (so one parser and one excerpt format serve both), the contract header `include/calc/calc.hpp`, and stubs that throw "not implemented".
* **Feedback signals, cheapest first:** allowlist; `-fsyntax-only` of each written file (file, line, message, the source line: the `node --check`
  analogue); build of only the gate's targets; the tests (TAP: expected, actual, location); then, in the integrator, the whole suite under
  AddressSanitizer and UBSan, and the app smoke run.
* **Capture (honest scope):** the app has a `--screen` mode that renders a fixed-size text frame; the integrator stores it as `capture.txt`, and a
  tester can assert on it (a golden frame). Pixel capture needs a display server (Xvfb is not installed here) and a GUI toolkit; it is a later
  harness stage and needs the owner's permission to install anything. Until then the text frame is the screen.
* **Debugger:** `gdb` is installed; the debugger worker designed in `docs/FEEDBACK_LOOPS.md` section 8 attaches to a failing test executable and
  returns the backtrace and values, which is the native analogue of a stack trace from the web runner.

## 6. The third platform: why Python, and what it proved (2026-11-11)

Two platforms can be fitted by accident: a harness that only ever saw JavaScript and C++ would still look general. A third that differs in *kind* tests the
abstraction. **Python 3 with `unittest`** was chosen because it needs no download (Python 3.14 is installed; pytest, Go and Rust are not, and nothing is
installed without the owner's permission), is interpreted like the web platform but has a different runner, error format and module system, and has no build
step and no sanitizers (its counterpart is development mode with warnings as errors). An embedded firmware variant (a HAL, a simulated display whose
framebuffer is a real image to capture) is the better fourth platform once a real capture path is wanted.

What the third platform forced, each now in the code:

* **One calculator contract.** `calculator_spec` states the expression language, the error behaviour and the app's output once; a platform supplies a
  `CalculatorSurface` (file names, how the engine is declared, how a test runs the app, style) and gets its plan-2. Both blueprints are generated, and
  `test_platforms` asserts they have the same items, personas, dependencies and thresholds.
* **One gate flow.** The allowlist, workspace, judging (minimum tests, tests must fail against stubs, near pass, same failures twice) and publishing are
  `runGateFlow` in `harness_common`; a platform supplies a scaffold, a precheck (a compile or syntax check that names file and line) and a build-and-run.
  `cpp-cmake` and `py-unittest` are about 60 lines each for their gates. The web gate still has its own copy (it has the oracle and the node precheck);
  moving it onto the flow, with the oracle as a hook, is the next step and the existing tests are its safety net.
* **One report format.** Each platform's test runner prints the same TAP shape (expected, actual, location), so one parser and one failure excerpt serve all.
* **A parser defect found by a new emitter** (catalogue row 24): the shared TAP reader let a failing test's error text run into the following passing
  tests' lines, because the web runner always closed a diagnostic with a line the reader happened to skip. Fixed in the reader, not in each runner.

Findings that are not yet fixed: the excerpt cuts expected and actual values at 120 characters, which hides the difference in long strings (the screen
frame); Python's `assertEqual` on a tuple prints a diff of the whole tuple, so a tester should compare the parts separately; the per-job timeout of 120 s
for a gate may be short for a cold C++ build on a loaded host.

## 7. The model bar: the 35B is the minimum (owner decision, 2026-11-11)

Qwen3.6-35B-A3B (Q4_K_M, a mixture of experts with about 3B active parameters) is the **minimum model the workflow is proven on**. The 9B is a harder
target than the workflow needs to show: on the first C++ run it spent over an hour without an accepted item, failing to compile test files that misused the
scaffold's own helpers (catalogue rows 25 to 27). Hardening the harness against a model that weak was useful (it found rows 24 to 27), but proving the
workflow, the V and the platforms needs a worker that is reliable enough that a failure points at the harness. The 9B stays as a stress target for
harness hardening, not as the bar.

Practice: one engine at a time (the 35B takes about 22 GB of the 30 GB host, so the 9B and the 35B cannot both be resident); the engine is a service job under
the `run-ornith1` daemon (now the 35B on :8092, 4 slots of 8192 tokens, alias `qwen35`); the first proof runs are the C++, Python and web calculators from their
blueprints, each recorded in `out/archive` with the build sha. A platform is proven when the same plan passes with zero manual mends.

## 8. Order of work

Done 2026-11-11: `harness_common` and the registry fields; `cpp-cmake` and `py-unittest` harnesses with reference files; triage routing; the shared
calculator spec; `test_platforms` (real compiler and interpreter, no model).

Next:

1. Move the web gate onto `runGateFlow` (oracle as a hook) and generate `calculator-web` from the same spec.
2. A real 9B run of rung 2 and 2b (after run `w-000002` finishes: one CPU-heavy job at a time); compare first-pass rates across the three platforms.
3. The knowledge store (`docs/KNOWLEDGE_STORE.md`) slice 1 plan, written by the architect step, then built.
4. Pixel capture (framebuffer to an image) when a fourth platform needs it.
