# Status: one page for the owner and for whoever works on this next

Written 2026-11-15 at the end of the eighth pass of the iteration. Sub0Squad is a working title. **No real model has completed a project on this
harness yet.** Everything marked "proven" below is proven without a model (mocks, a fake engine, recorded replies, real compilers and test runners).
The running log with every detail is `docs/ITERATION_PROGRESS.md`; the catalogue of every defect and its mend is `docs/INTERVENTIONS.md`.

## 1. What works and is proven (and the test that proves it)

| Capability | Proof (ctest name) |
|---|---|
| Durable jobs: journal, job store, shim, supervisor, resume, host guards, shedding under memory pressure | test_journal, test_job_store, test_job_spec, test_shim, test_idle, test_host_guard, test_e2e |
| The workflow engine: a process is a list of stages with entry and exit predicates; the decision ladder; goldens of every scenario (36 action logs) | test_workflow, test_workflow_stages, test_workflow_mends, test_process, test_predicate, test_policy |
| Three project harnesses (web-static, cpp-cmake, py-unittest): scaffold, gate, integrator, blueprints, with real builds and test runs | test_platforms, test_project, test_plan |
| The mock path end to end, exactly as the README says | test_readme |
| Executors `session`, `canvas`, `fork` driven by recorded replies; the three through a real daemon | test_agent, test_canvas_agent, test_fork, test_executors |
| Turn control: early abort, continuation, overlap trimming, bounded reads | test_stream, test_agent |
| Self-healing: the mends table, the gate reporting a mend, `add-section`, `silence-unused`, a process's own table | test_mends, test_workflow_mends, test_heal (real daemon, real C++ build) |
| Near pass kept, best state never lost, class members as sections, failure records, no repeated rung (rows 70 to 73) | test_nearpass (real daemon) |
| A stuck near pass thinks first (gate exit 7) | test_policy, golden `stuck-near-pass-thinks-first`, test_nearpass |
| Known diagnostics, thrash control, the unaccepted section as first cause (rows 74, 75; gate exit 8) | test_mends (run-v12's real texts), test_thrash (real daemon), golden `unaccepted-section-is-first-cause` |
| The planning stage that asks once and remembers (`--process clarified`) | test_clarify (rules and a real daemon), golden `stages-clarified-asks-once` |
| `sub0squad wizard` from recorded keystrokes | test_wizard |
| The portal page and its file view; the archive; the model-free block proofs; the scenario fixtures | test_portal, test_portal_page (needs node), test_archive, test_blocks, test_scenarios |
| Layering: includes go downward, and no layer CALLS upward (one library per layer) | layering, link_kernel ... link_presentation |

## 2. What has run on a real model, and what happened

All on the Qwen3.6-35B-A3B (the owner's minimum bar), the C++ calculator, executor `fork`. None finished the project. Each run's findings became
catalogue rows, and each row is built and proven without a model; **whether the mend holds on the model is unknown until the next run.**

| Run | Result | Main cause | Rows |
|---|---|---|---|
| v8 | failed | first real use of the fork: a tester's item was divided into a whole project; division by modules on a compiled platform | 44 to 46 |
| v9 | failed; the fork mechanism worked, no attempt converged | sections ran one after another (all pinned to one engine slot); every round re-ran every section; a section refused for others' errors | 47 to 52 |
| v10 | failed | the merged file had no definition of the contract's entry point; `-Wunused` errors spent attempts | 59 to 64 |
| v11 | failed after 8 attempts; attempt 1 had passed 20 of 22 tests | the near pass was thrown away by the ladder; class members never defined; a failed tester said nothing | 70 to 73 |
| v12 | did not finish `dev-engine` (8 attempts; the last gate texts read by this pass say 3 of 13 tests failing) | the entry point `evaluate` refused three times for one compile error, the model resending the same code; then three mends and five attempts spent on its consequences | 74, 75 |

The archive of v12 is `out/archive/run-v12-35b-cpp-fork.tar.gz` in the main checkout; the lead's own notes on each run are authoritative where they differ.

## 3. Built, proven without a model, NOT yet seen on a real model

The mends (`undefined-contract-symbol`, `undefined-declared-member`, `helper-never-defined`, `unused-entity`); contract and member sections; the
near-pass keep and the best-state resume; `think` for a stuck near pass and for an unaccepted section; the diagnostics table (ONE entry, written from
GCC's wording) with its hint and edit; thrash detection and the convergence counts in `fork.json`; the first-cause rule of the gate; the planning
stage with an llm agent (its executor question has only been exercised in the pure rules); the wizard with a real engine (only the fake one).
**What to look for in the next real run:** `fork.json` (`mended`, `hinted`, `thrash`, `progress`, `thought`, `nearPass`, `resumedFrom`), a gate text
that starts `GATE FAIL: FIRST CAUSE`, decisions `mend`, `think`, and whether any of them changes the outcome.

## 4. NOT built

From this pass's list (each with its next step in `docs/ITERATION_PROGRESS.md`, "Loose ends"):
* The six scenarios of `docs/SCENARIOS.md` as `scenario run` entries; rows 25 and 26 beyond quoting the failing test; block 7; row 64 (b).
* The rewrites of `DESIGN.md`, `M1_PLAN.md`, `WORK_GRAPH.md` section 23 and `SCENARIOS.md` (they carry a dated note saying what superseded them).
* The dead-code sweep and the final consistency pass over the documents.
* The wizard's other half: building, starting and supervising `llama-server`, offering and fetching models (each needs the owner's go-ahead).

From the backlog (`docs/BACKLOG.md`): the portal graph layout (containers, crossing minimisation, the V, zoom); agents raising requests themselves;
review by a model; the debugger worker's model half; the knowledge register's readers; the metrics instrumentation and scorecard of `docs/METRICS.md`
(the per-section convergence counts are the first piece); the portal on the local network (proposed, not agreed).

## 5. Open questions for the owner

1. **The project name** (still the working title).
2. **The frontier tier** stays `off` or `stub`; nothing has ever been sent off the machine. Enabling a real one is the owner's decision.
3. **The wizard and the engine:** may the build fetch and build llama.cpp, and may the wizard start a contained engine service and fetch a model? Each
   is a download, an install or a service; none was done.
4. **Planning questions:** should "the bar for done" and "which engine" become questions? They need the process and the engines to be replaceable
   after submission (`docs/PROCESS_DESIGN.md` section 15); today they are settled as "not asked", with the reason shown.
5. **Is "one record of answers per state directory" the right meaning of "the project"** for remembered answers?
6. **Diagnostics table:** grow it only from real runs (the rule so far), or seed it with common compiler diagnostics a 35B is known to trip on?
7. **`leaf.thrashControl`, `leaf.memberSections`, `leaf.allow.thinkFirst` are ON by default** (as `contractSections` is), although the rule is "new
   capability off by default": they are fixes decided by the lead, each with a switch to reproduce the defect. Confirm or reverse.
8. The questions of `docs/METRICS.md` section 10 (the oracle, whether a healed run counts as success, wall time).

## 6. Build, test, run

```
. scripts/dev-env.sh                                   # every shell that configures or builds
cmake -S . -B out/build/dev -G Ninja && nice cmake --build out/build/dev -j2
ulimit -u $(( $(ps -u $USER -L --no-headers | wc -l) + 400 )); ctest --test-dir out/build/dev -j1
```
The full suite is 48 tests and takes about 6 to 12 minutes run sequentially. `cmake -DSRC_DIR=$PWD -P cmake/CheckLayers.cmake` is the layer check by
hand (without `SRC_DIR` it is an error).

**Mock (no model):** `sub0squad wizard`, or `sub0squad up`, `sub0squad do "write a calculator in C++"`, `sub0squad project wait w-000001`,
`sub0squad down` (README "First use"). With the planning question: add `--process clarified`.

**35B (the owner or the lead only; never an agent):** start the engine as a contained user service (a memory cap and no swap, so an out-of-memory
kill takes the engine and not the session), then
`sub0squad doctor --engine qwen35=http://127.0.0.1:8092/v1`, `sub0squad up --frontier off`,
`sub0squad do "write a calculator in C++" --agent llm --engine qwen35=http://127.0.0.1:8092/v1,model=qwen35,ctx=8192,slots=4,seed=42 --executor fork --max-tokens 3500 --repair-turns 2`.
The exact `systemd-run` line and the model path are in the lead's notes and `docs/PROVING_GROUND.md` section 7; `docs/RUNNING_PROJECTS.md` has the
engine profiles. Archive every run (`sub0squad archive`).

## 7. Host safety (the machine ran out of memory twice)

* Builds at `-j2`, one at a time; never while a real run needs the machine unless told it is idle.
* Every test or command that starts processes runs under a process cap and a timeout (the `ulimit` line above). Nothing re-executes `/proc/self/exe`
  (catalogue row 32: a fork bomb of 33,000 processes); a program path is passed in and verified.
* An agent never starts, stops or restarts an engine, a daemon, a portal or a service that is not its own test's; no real-model runs by agents.
* The 35B needs about 25 GB of the host's 30.
* In the agent's shell tool a dollar-brace reference in a heredoc is expanded to nothing: write such files with the file tool or from a script file.
* Do not edit a source file while the compiler is reading it (the compile dies with SIGBUS).

## 8. Where every document lives

| Read this | For |
|---|---|
| `README.md` | what it is, the build, first use (run by `test_readme`) |
| `docs/STATUS.md` | this page |
| `docs/ITERATION_PROGRESS.md` | the running log, newest first; the loose ends with their next steps |
| `docs/ITERATION_BRIEF.md` | the rules and the direction of the iteration; host safety |
| `docs/INTERVENTIONS.md` | every defect found (rows 1 to 75): cause, detector, mend, state |
| `docs/PROCESS_DESIGN.md` | processes as data; sections 13 to 16 are the code as it is (stages, self-healing, the planning stage, the wizard) |
| `docs/COMPONENTS.md` | units, layers, libraries, how layering is enforced |
| `docs/CANVAS.md`, `docs/FOLDED_VIEWS.md` | the canvas and fork executors, folded views |
| `docs/METRICS.md` | KPIs, the tick model, habits and the craft score (design; mostly not instrumented) |
| `docs/BLOCK_PROVING_GROUNDS.md`, `docs/BUILDING_BLOCKS.md`, `docs/PROVING_GROUND.md` | what each building block must prove and how; the model bar |
| `docs/SCENARIOS.md` | the scenario suite (six scenarios specified, not built) |
| `docs/BACKLOG.md` | what is wanted and not built |
| `docs/RUNNING_PROJECTS.md`, `docs/DEV_SETUP.md`, `docs/HANDOFF.md` | running projects and engines, the developer setup, the earlier handoff (hardware limits) |
| `docs/DESIGN.md`, `docs/M1_PLAN.md`, `docs/WORK_GRAPH.md`, `docs/DESIGN_AUDIT.md`, `docs/OPTIMIZATION.md`, `docs/FEEDBACK_LOOPS.md`, `docs/SQUAD_UX_AND_HARNESS.md`, `docs/KNOWLEDGE_STORE*.md` | earlier designs: partly superseded, each says so at its top; NOT rewritten |
| `docs/DECISIONS.md`, `docs/ROADMAP.md`, `docs/PLAN_REVIEW.md`, `docs/PROMPT_REVIEW.md`, `docs/NEXT_SESSION_PROMPT.md` | decisions, the roadmap, reviews |
