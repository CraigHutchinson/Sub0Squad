# Resume here (written 2026-11-16, to continue on a faster machine; updated 2026-11-17)

Follow-up work with its own brief: [sessions/](sessions/README.md) (handoff docs, one per task). Read this first; it supersedes the "next steps" of the old `HANDOFF.md`, `NEXT_SESSION_PROMPT.md` and `ITERATION_PROGRESS.md` (in git history: [ARCHIVE.md](ARCHIVE.md)). The owner's goal is unchanged: prove the
harness builds sandbox projects from a bare request with local models; the Qwen3.6-35B-A3B (Q4_K_M) is the minimum bar and nothing leaves the machine (frontier stays OFF).

## 1. Where things stand

**Update 2026-11-17** (the rest of this section is as written on 2026-11-16):
* **Windows is a first-class platform**: the program runs natively under MinGW-w64, and CI builds and tests on Windows and Linux. Tests carry `smoke`/`full`
  tiers with area labels: smoke runs on every change, full at the gates ([DEV_SETUP.md](DEV_SETUP.md)). Windows Server 2022 behaves differently from
  Windows 11 for AF_UNIX sockets, and the fixes are in `1f45940` (a socket file is seen without opening it; the waiter sees a connection that arrived before
  its first wait; a socket path over 107 bytes moves to a short name). `f25441c` lets the portal take port 0, so parallel tests never collide.
* **The canvas tooling lives in [sub0canvas](https://github.com/CraigHutchinson/sub0canvas)**: outline, edits, canvas and doc_layers, consumed through CPM at a
  pinned tag (v0.2.0), with forwarding headers so callers are unchanged. A change there lands in sub0canvas first, is tagged, and the pin is bumped here
  (then run `test_blocks`). The next step is [sessions/layered-document.md](sessions/layered-document.md).
* **Model tiers and the tier 0 gate** ([BLOCK_PROVING_GROUNDS.md](BLOCK_PROVING_GROUNDS.md)): 24 seeded cases on the reference calculator; tier 1 and
  above run only on a tier 0 PASS. The last run (build `1e74bd4`, the 35B, stopped at 52 of 72 runs as superseded): every case passed 3 of 3 except
  wrong-approach (0 of 3: a draft that did not compile ranked best, fixed in `58d8c28`) and write-tests (2 of 3: folded test bodies, fixed in `38da8e7`).
  The definitive gate on the current build is next. The smoke set (`run_t0.sh <repo> <engine> 1 @smoke`, about 8 minutes) is for iterating, and never
  stands in for the gate.
* **Engine**: Qwen3.6-35B-A3B APEX I-Compact on `llama-server` port 8092 (`-c 65536 -np 4`, alias `qwen35`). A 9B (Qwen3.5-9B Q4_K_M) is downloaded
  for tier 0 once the 35B passes it reliably.

**The clean-slate path exists end to end** (`sub0squad do "write a scientific calculator in C++" --agent llm --executor fork`): a planner model plans from the request alone (no blueprint, no reference),
mechanical mends and checks repair what they can, the squad builds test-first, a developer's failure can be disputed back to its tester, the harness integrates generically, and a
validation grader outside the harness judges the result.

**The request slipped (found 2026-11-16, owner): the sandbox project is a SCIENTIFIC calculator** (the web sample was always "write a scientific calculator as a web page"). Every clean-slate run in this section used `write a calculator in C++`, so the planner correctly planned only `+ - * /` and parentheses, and the trig functions, inverses and degrees mode that the old hand-written blueprint carried were never asked for. All results below are for the plain calculator: they prove the machinery, not the scientific project. `scripts/campaign.py` and `docs/CLEAN_SLATE.md` now say `scientific`; the next full run must use it, and the planner will have to get nested trig expectations right (the tester's wrong-literal failure gets harder). `validation/grade_blackbox.py` has a separate `scientific` group of cases.

**Hands-off parallelism bar (2026-11-16, owner):** the planned process now runs the planner with `--min-speedup 2.0` and `--max-turns 8`: a plan that passes every check but whose expected speedup on the engine's slots (plan-lint) is below 2.0x goes back to the planner as a problem ("too serial ... split into more independent deliverables") until it reaches the bar; if no turn does, the best valid plan stands and `planner.json` says `barMet: false`. First use: `w-000024` ("write a scientific calculator as a web page"): turn 2 was valid at 1.66x, turn 3 reached 2.23x with three independent module pairs (engine, trig/log functions, ui). The engine now runs `-c 65536 -np 4` (16384 per slot, the service peaked at 24.0 GiB of its 25 GiB cap; `scripts/start-campaign.sh` defaults to it); the planner's reply budget follows the profile's window (45 %, at most 7000 tokens).

**It has not passed end to end yet.** Evidence (35B on this CPU, 3 to 5 tok/s decode, 4 slots at 8192 tokens):

| What | Result |
|---|---|
| Planner valid plan (single reply, current code) | about half of runs; every failure was format (JSON slips, cut-off replies), not planning |
| Planner with `--prose` (prose lead, then one formatter call) | 1 of 4 valid: one formatter call for the whole plan does not help; per-item formatting is untested |
| Supplied plan, all four items accepted, then integration by hand | run `w-000020`: product 12 of 12 black-box cases, 0 wrong test literals (`validation/grade_blackbox.py`) |
| Full run from the request, planner passed | `w-000022`: engine pair accepted at once; `dev-app` failed 6 attempts (the engine returns NaN on error and the app's card did not say so). Fixed since: a developer's card shows the contracts of the items it builds on |
| Supplied plan, whole workflow (items, dispute, integration, no manual step) | run `w-000023`: **succeeded** ("all items accepted; integration passed"); `validation/grade_blackbox.py out/e2e/supplied-7`: product 12 of 12, 5 literals audited, 0 wrong. The plan came from `w-000022`'s planner; the full run from the request (planner included) has still not passed in one go |

Wrong expected values in the tester's tests (`2 + 3 * 4 - (5 / 5)` = 14, `3 + 4 * 2 / (1 - 5)` = 2, `10 / 3` as a float literal) recur in nearly every run; the dispute rung catches them but costs
about an hour on this CPU. Nothing in the harness has an oracle (by decision); the structural answer is the expectation QA step below.

Last pushed commit when this was written: `4adca03` on `origin/main`; 48 of 48 ctest tests pass (`ctest -j2`, about 6 minutes). On 2026-11-17: `f25441c`, CI full green on Linux and Windows; 54 of 54 locally.

## 2. The design direction the owner set this week (read before building)

1. **`docs/CLEAN_SLATE.md`**: the principle (no pre-built plan, no reference in the harness; the oracle is in `validation/`), the split table, the first-probe results (section 7).
2. **`docs/CLEAN_SLATE.md` section 8** (was `PLAN_FORMAT.md`): why format, not planning, is the failure; prior art (BAML schema-aligned parsing, "Let Me Speak Freely", TOON, grammar-constrained decoding); the options table; the
   recommendation; and **the discipline ladder** (end of section 8): plan prose, plan form, work prose, work source. Each layer has one job, a format problem is repaired in place by edits and never
   climbs, a content problem is raised to the AUTHOR of the layer above (the same hand-off as a fork's coordinator), so a layer's author is recorded with it.
3. **Repairs are edits, not regenerations** (BACKLOG "Repairs as edits"): built for the planner (`planner/doc/turn-N/`, canvas transactions, block 1 has four repair cases); not yet for the other roles.
4. **Context kept on disk**: llama-server slot save and restore (unmeasured here).

## 3. Priority order for the next session

Cheap first; each step names what proves it. Rules in section 5 apply throughout.

0. **(2026-11-17) The tier 0 gate first**: a PASS on the 35B (`run_t0.sh` then `gate_t0.py`; `out/runs/t0/gate.md`), then tier 1 (`run_tier.sh <repo> 1
   <engine>`), then tier 0 on the 9B. The items below predate the model tiers.

1. **Re-measure on the faster machine before anything else**: decode and prefill speed, then the planner success rate over at least 10 seeds with the current code (`llm-planner --workflow-dir D --engine "qwen35=URL,model=qwen35,ctx=8192,slots=4,seed=N" --request "write a scientific calculator in C++" --max-turns 6`, with and without `--prose`). A faster machine changes which timeouts, wall budgets and context sizes are right; the numbers in section 1 are CPU-bound. Record them in `docs/CLEAN_SLATE.md` section 7.
2. **Get one full run through `do` to pass and grade it** (`validation/grade_blackbox.py WORKFLOW_DIR`, and the literal audit it includes). Then repeat for a 20-seed clean-slate baseline (`scripts/start-campaign.sh`; the campaign driver still starts from blueprints and has no grader, plan-validity or repair columns: extend `scripts/campaign.py`).
3. **Plan format, per BACKLOG "Plan format and discipline boundaries"**: (a) measure the format-only failure share on the existing `planner.json` records; (b) prose plan, then one formatter call per item in parallel on idle slots, repaired by edits, against the single-reply planner on the same seeds; (c) pass a grammar or JSON schema to llama-server for the format step only and check content quality does not fall; (d) a schema-guided tolerant parser in place of one mend per slip. Thinking stays off for format steps.
4. **Expectation QA** (BACKLOG "Many experts, one result"): an explicit step that checks the tester's expected values against the contract and the worked examples before a developer is graded (a referee call that re-derives each literal), with the failing line as the dispute text. The one-failing-test shortcut was tried and reverted (it breaks `test_nearpass`); the reason and the alternative are in the BACKLOG.
5. **Work prose** for tester and developer (describe, then write); the concern hand-off from a failing item to the layer above (generalise `reviseTests`); the recorded author per layer.
6. **Prompt wording A/B** (BACKLOG): prompts as data, variants B to E from the owner against today's wording, at least 10 seeds each. Built since (local session, 2026-11-16): the worker stance switch `--stance baseline|specialist` and the CONCERN block (experiment E1,
   [EXPERIMENTS.md](EXPERIMENTS.md) section 9, [PROMPTS.md](PROMPTS.md)); `--stance` is the first prompt-as-data switch. Also built: the platform layer
   ([PLATFORM.md](PLATFORM.md)), process launch in one place.
7. **Portal**: the planning card and the planning graph node are built (see BACKLOG); still to do: the dispute edges and gate classes in the decision view, a workflow selector (today `?wf=w-000022` in the URL); make `up` start the portal by default (it was off only because `--no-portal` was passed); ngrok later (needs the owner: install, account, token, authentication design).
8. **Slot save and restore** measurement; per-role windows; the phased design and execution planner (BACKLOG, CLEAN_SLATE section 6).
9. Fault-injection campaign at every step and role; web and Python scaffolds (only C++ self-scaffolds today); a section-count cap in the fork split (a 15-section skeleton for a 20-line file); the A/B context campaigns.

## 4. Known defects and how they are held (INTERVENTIONS rows 76 to 81 and this week's fixes)

Mended mechanically in `mendPlan` (`src/plan_checks.cpp`), each with a test in `tests/test_planner.cpp`: weak or unreachable gates, a developer not tied to its tester, the program's item not depending on the library item (row 79), test files named off-pattern, a missing `main.cpp` stub, stub parameters the body does not read, card includes that name no file, project headers added to every card. Checked in `checkPlan` and `cppCheckPlanScaffold`: stubs must link together, the contract's `NAME::` must be declared by a header. Fixed: a planner-made plan has no reference tier (row 78); the integrator makes no calculator assumptions for planned plans; `app::run("A B")` is ONE argument (a string starting with `-` is a command line). Open: the portal gap, the whole list in section 3.

## 5. Rules that still bind

STOP AND ASK before: the final project name; enabling any frontier tier; changes to Sub0Pipeline or Sub0Pub; downloads or installs; credentials. Never stop the daemon or portal while a request is open. One CPU-heavy job at a time; builds at `-j2` (revisit on the new machine); process-spawning tests under `ulimit -u` and a timeout. Source `scripts/dev-env.sh` before a build that may reconfigure. Style: camelCase, Doxygen, `#pragma once`, `[[nodiscard]]`, new capability off by default, C++23 `-Wall -Wextra -Werror`, `std::format`/`std::print` rather than `printf` (DECISIONS 25). Every manual fix is catalogued in `docs/INTERVENTIONS.md` with a detector, a mend and a fixture. Report outcomes faithfully; claim nothing that was not observed. The Bash tool expands `${...}` even in quoted heredocs: write such text with the Write or Edit tool. Commit as the owner's noreply identity, trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`; push to origin/main.

## 6. Starting on the new machine

```bash
git pull && source scripts/dev-env.sh && cmake --preset dev && cmake --build out/build/dev -j2   # see docs/DEV_SETUP.md
scripts/start-campaign.sh status          # the engine service (sub0-llama35) and the campaign; MODEL, LLAMA, CTX, SLOTS, THREADS can be overridden
out/build/dev/sub0squad up && out/build/dev/sub0squad portal --port 8780   # the portal: http://127.0.0.1:8780/
out/build/dev/sub0squad do "write a scientific calculator in C++" --agent llm --engine "qwen35=http://127.0.0.1:8092/v1,model=qwen35,ctx=8192,slots=4" --executor fork --defaults
```

A plan can be supplied instead of planned: put `plan.json` and an empty `plan.supplied` in a directory and pass `--sandbox DIR` to `do`; it still goes through the mends and checks. Memory notes for the assistant are in the user's `.claude` project memory (`sub0squad-iteration-state.md`).
