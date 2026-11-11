# Proving ground: a portfolio of sandbox projects

Status: design, 2026-11-11. Working titles only; no project name is chosen (that is the owner's call).

One sandbox proves one thing. The harness has to be shown to work across **different toolchains, different feedback signals and different sizes**, or
every mend we make fits only the web calculator. The proving ground is therefore a small ladder of projects, each chosen to exercise something the
previous one cannot.

## 1. The ladder

| rung | project | harness | size (items) | what it proves that the rung below cannot |
|---|---|---|---|---|
| 1 | calculator-web (exists) | `web-static` | 4 to 12 | the V, test-first, ladder, 9B workers, TAP feedback |
| 2 | calculator-cpp (next) | `cpp-cmake` | 4 to 12 | a second toolchain: compile diagnostics as feedback, CMake, sanitizers, a CLI app, text-screen capture |
| 3 | spreadsheet (web UI and native core) | both | 20 to 40 | reuse (the calculator engine becomes a part), cross-harness contracts, a dependency graph with cycles, UI capture |
| 4 | **knowledge store** (native core, web viewer; `docs/KNOWLEDGE_STORE.md`) | both | 50 to 60 | depth: layers with versioned contracts, property and crash tests, model-based oracle, sanitizers; a component the squad then adopts |
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

## 6. Order of work

1. Extract `harness_common` and add `Harness::gateKind` and blueprint (no behaviour change; all tests green).
2. `cpp-cmake` harness with the reference implementation (also the mock agent's correct file and the test oracle), registered; gate and integrate tested
   with the real compiler and no model.
3. Blueprint `calculator-cpp` and triage routing; a mock-agent end-to-end test as for the web calculator.
4. A real 9B run of rung 2 (after rung 1's run finishes: one CPU-heavy job at a time).
5. Spreadsheet plan (rung 3) written by the architect step, then built.
