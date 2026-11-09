# Running sandbox projects, and adding another

Status: 2026-11-09, Linux. Background: [SANDBOX.md](SANDBOX.md) (the delegation loop), [SQUAD_UX_AND_HARNESS.md](SQUAD_UX_AND_HARNESS.md)
(triage, personas, work cards, harness, metrics).

## 1. Build and start

```sh
cmake -S . -B out/build/dev -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/dev
export SUB0SQUAD_HOME=$PWD/out/demo-home          # optional: an isolated state directory (default: XDG state and runtime dirs)
out/build/dev/sub0squad serve --max-jobs 4 &       # the daemon; --frontier off|stub (default stub: nothing is ever sent anywhere)
```

`--max-jobs` is the only concurrency limit today. Use **1** whenever agents call a model server that has one slot (the scheduler does
not yet know an engine's cap; arbiter and `EngineCaps` are M2/M5). Mock runs can use 4.

## 2. Run the calculator web project

One call, triage first:

```sh
sub0squad do "write a scientific calculator as a web page"                 # mock agents: deterministic, seconds
sub0squad project wait w-000001 && sub0squad project status w-000001      # items, decisions, ledger, work-vs-harness
```

Triage recognises the task, picks the `calculator-web` blueprint (no architect, no frontier) and the workflow runs: tester items write
tests from the contract, developer items are graded by those tests, the integrator merges, runs the whole suite and fetches the page from
the static server. The result is a directory:

```sh
cd $SUB0SQUAD_HOME/state/workflows/w-000001/project     # or the --sandbox directory you gave
npm test                                                # node's built-in runner, 18 tests
node serve.mjs 8080                                     # open http://127.0.0.1:8080
```

Useful options: `--inject eval-tests:1 --inject engine` (the mock fails the first attempt of `eval-tests`, and every local attempt of `engine`, so
you see a retry and an escalation to the `frontier-stub` tier), `--sandbox DIR`, `--max-local N`. Per item evidence lives in
`items/<id>/attempt-N/`: `prompt.txt` and `reply.txt` (llm agents), `gate.txt` (what the next attempt is told), `out/` (what the agent wrote),
`ws/` (the workspace the gate ran in). `sub0squad logs <job>` shows a job's output; `sub0squad status` lists jobs; `sub0squad project cancel <id>`
stops a workflow and verifies no process is left.

### With a real local model

An engine is a profile: `NAME=http://127.0.0.1:PORT/v1[,model=..][,ctx=N][,slots=N][,kind=..]`. Loopback only. Two kinds are in use:

| Engine | How it is provided | Profile |
|---|---|---|
| Ubuntu `qwen3-6` inference snap (Qwen 3.6 35B-A3B, llama.cpp b9482) | already running as a snap service | `qwen3-6=http://127.0.0.1:8342/v1,model=qwen3.6-35b-a3b,ctx=32768` (`qwen3-6 status` shows the URL) |
| llama.cpp `llama-server` with the Qwen3.8-27B IQ1_S GGUF | started as a squad process job so its teardown is verified | see below |

```sh
sub0squad serve --max-jobs 1 &
sub0squad do "write a scientific calculator as a web page" --agent llm \
    --engine "qwen3-6=http://127.0.0.1:8342/v1,model=qwen3.6-35b-a3b,ctx=32768" --max-tokens 3500
```

Engines are per persona: `--engine A=... --engine B=... --engine-for tester=A --engine-for developer=B` routes the tester items to A
and the developer items to B, and the status output reports tokens and decode speed per engine, which is how engines are compared.
A local attempt that fails its gate is retried once with only the gate's failure excerpt added to the prompt, then escalated to
`frontier-stub` (the reference templates, recorded as a stub); with `serve --frontier off` the item fails instead.

Spawned server (the model path and flags come from Sub0Llm's `perf/optimization/llama-cpu.json`; run `llama-server --help` for the rest):

```sh
sub0squad submit --tool llama-server -- /path/to/llama-server -m /path/to/Qwen3.8-27B-UD-IQ1_S.gguf --port 8090 -c 4096 -np 1 -t 8
sub0squad cancel <job>      # kills the server and anything it spawned, and verifies it
```
then use `--engine "qwen27=http://127.0.0.1:8090/v1,model=qwen27,ctx=4096"`.

### Reading the metrics

`project status` ends with: per-tier ledger lines (notional cost, nothing is sent anywhere), per-engine lines, and
`work ratio` = accepted deliverable tokens over all prompt and completion tokens of the agent attempts; `rework` = deliverable tokens of rejected
attempts; `prompt overhead` = system block plus feedback tokens; `harness supplied` = bytes of scaffold the model did not have to write.
Token splits are estimates (prompt segments are counted with the engine's tokenizer, the completion is split by characters).
`--json` gives the same data, and `ledger.ndjson` in the state directory is the raw record.

## 3. Adding another sandbox project

Three routes, cheapest first.

### A. No code: let triage defer to an architect (a file handoff)

```sh
sub0squad do "build a unit converter page with length and mass"
# triage: web-static project, ... architect: deferred
# awaiting a plan: see needs-plan.md in the workflow sandbox
```

`needs-plan.md` (in the workflow's sandbox directory) states the task, the triage, what the harness provides, the plan-2 syntax, the card size
budget and the rules the daemon enforces. Give it to a frontier model or write the plan yourself, save one `plan.json`, then:

```sh
sub0squad project plan w-000002 --file plan.json      # validated; the workflow starts
```

Nothing is sent anywhere by the squad: the "deferral" is you carrying the file. The plan must use the `web-static` harness (the only one
today), test-first items (a tester item with no dependencies, then a developer item that depends on it and `include`s its test file), tiny items
(one to three files) and cards under 1,500 tokens. Start from `sub0squad blueprint calculator-web` as the worked example.

### B. A reusable blueprint (so triage recognises the task)

1. Write the plan-2 JSON (copy `calculator-web`: `kCalculatorWeb` in `src/harness_web.cpp`). Check it with `Plan::fromJson` (the planner does
   this when it writes it) and keep each card under budget.
2. Register the task name: `isKnownTask()` and the matching rules in `triage()` (`src/triage.cpp`), and the name in `runMockPlanner()` (`src/sandbox.cpp`).
3. For deterministic mock runs and tests, add mock deliverables for the new files (`webMockFile`, `webMockBugTarget` in `src/web_templates.cpp`)
   including one deliberate defect per item (a single textual replacement), and a test in `tests/test_project.cpp`. The mock is what lets the
   retry, escalation and gate paths be tested without a model.
4. Real models need nothing else: `llm-agent` builds the prompt from the card and the harness brief.

### C. A new project harness (a different kind of project)

A harness is code that creates the workspace, scaffold and stubs, so an agent writes only deliverables. To add one (for example `cpp-cmake`):

1. Name it in `Plan::fromJson` (known harnesses) and give `web-static`-like functions: `scaffold()`, a `brief()` for the system block, a gate runner
   (`runHarnessGate`) and an integrator (`runHarnessIntegrate`). Gates stay declarative (`GateSpec.kind`): a plan never carries commands.
2. Make the stubs fail loudly ("not implemented") so a tester's tests can be checked to fail against them (`mustFailOnStub`).
3. Keep personas' write rules (testers under `tests/`) or extend them for the new layout in the plan validator.
4. Document the harness in the `needs-plan.md` packet (`needsPlanPacket`), because that is what a deferred architect reads.

## 4. Known limits

Mock agents only cover the calculator. A tester's tests are checked only to fail against the stub (they are not yet checked against a
reference), so a wrong test can cost a developer attempt. One engine call is one attempt; there is no in-attempt repair loop. Items live
in plain directories (no git worktrees). `--max-jobs` is global (no per-engine slots). The HTTP client is a small loopback-only stand-in for
the Boost.Beast stack of DECISIONS 18.
