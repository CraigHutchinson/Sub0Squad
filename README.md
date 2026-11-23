# Sub0Squad (working title)

**Many small minds, one whole solution.** Small work: be the expert and do it as well as it can be done. Larger work: divide it well, and make
sure the separation and the interactions hold. The squad is part of your team, delivering complete solutions, delegated and shared among workers.

A native C++23 toolbox for **delegated background work**: submit a job, check its progress, follow its log, cancel it,
resume it. Jobs run in **parallel** on workers that **share resources** (cores, memory, a model server, the exclusive
measurement window) under a **collaborative scheduler**, and one task can be **split automatically into sub-tasks**.
It runs inference through **several engines and runtimes side by side**, llama.cpp and native Sub0Llm directly, and
the scheduler knows each engine's **concurrency cap**.

Built from the Sub0 family, not around it: [Sub0Pipeline](https://github.com/CraigHutchinson/Sub0Pipeline) schedules the
job graphs (deferred completion, cancellation, remote sub-graphs) and
[Sub0Pub](https://github.com/CraigHutchinson/Sub0Pub) carries typed events. The new parts are the resource arbiter, the
durable journal, planners, and the engine seam.

**Status:** the daemon, the CLI, project workflows over three project harnesses (C++ with CMake, Python, a static web page), three executors, the planner and
the model-free proving grounds work on Linux and are covered by the test suite. No real local model has finished a project yet: the work now is the proving
sandbox on a local llama.cpp server, one A/B experiment at a time ([docs/STATUS.md](docs/STATUS.md), [docs/EXPERIMENTS.md](docs/EXPERIMENTS.md)). There is no
release. The name is a placeholder; the final `sub0*` name is pending.

Origin: it grew out of Sub0Llm's Qwen optimisation harness (`scripts/optimization/*` there), which becomes the first job kind.

## Build

```sh
cmake -S . -B out/build/dev -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/dev
ctest --test-dir out/build/dev --output-on-failure
```

See [docs/DEV_SETUP.md](docs/DEV_SETUP.md) for the local-source override (`CPM_<Name>_SOURCE`) used on the Sub0 dev machine.

## First use

**The shortest path is the wizard:** run `sub0squad wizard` (by its path in the build directory, as below) for a checklist console that takes you from
here to a first finished project: it shows what is needed, starts nothing without your yes, uses the mock agent unless you give it a model server that
is already running, and asks the one planning question a vague request leaves open. `--yes` takes every default; `--script FILE` replays recorded
answers. The plain commands below do the same by hand and are the ones to script.

<!-- first-use:begin -->
From a clean checkout to a finished project, with **no model**: the mock agent writes the reference deliverables, and everything else is
real (the plan, the scheduler, the gates that compile and test each item, the integration build). The commands below are run, exactly as written, by the
test `test_readme`, so they cannot rot.

```sh
export PATH=$PWD/out/build/dev:$PATH          # after the build above
export SUB0SQUAD_HOME=$PWD/out/home           # where this instance keeps its state (default: ~/.local/state)

sub0squad doctor                              # checks memory, tools, ports; says what to fix
sub0squad up                                  # starts the daemon and the portal in the background
sub0squad do "write a calculator in C++"      # triage picks the blueprint, the plan loads itself, the work starts
sub0squad project wait w-000001 --timeout 900 # blocks until the project ends (seconds with the mock agent)
sub0squad project status                      # the workflow and each item
```

* **Watch it live:** `up` prints the portal's address (http://127.0.0.1:8780/ unless `SUB0SQUAD_PORTAL_PORT` or `--portal-port` says otherwise; `0` lets the system pick a free port, as the tests do, and `up` prints the one it got). It shows the
  work graph; click a node for its conversation, which follows the model's tokens as they are generated.
* **Where the result is:** `$SUB0SQUAD_HOME/state/workflows/w-000001/project` is the finished project (here `project/build/calc_cli '2^3^2'` prints 512),
  `state/workflows/w-000001/report.md` the report, and `items/<item>/attempt-N/` every attempt with its conversation, files and gate output.
* **Other starts:** `sub0squad do "write a calculator in Python"`, `sub0squad do "write a scientific calculator as a web page"`, or by name:
  `sub0squad project submit --task calculator-py`. A task with no blueprint stops at "awaiting a plan" and says where the request is.
* **A workflow of your own.** A workflow is a **process**: a file that lists its **stages** in order. A stage either runs one registered tool as a job (the
  planner, the final integration, a review) or runs the plan's items (which executor writes developers' and testers' items, which rungs of the escalation
  ladder exist, how long each kind of job may run). When a stage starts and when it is done are conditions, such as `stage.build.done && !requests.openBlocking`.
  Built in: `standard` (plan, build, integrate), `sections` (the same stages; developers' items are written as forked sections and never divided into modules)
  and `reviewed` (the standard stages and a `review` stage after integration). A new one is a copy with changes, or a file you wrote:

  ```sh
  sub0squad process list                        # built-in processes and the files in $SUB0SQUAD_HOME/state/processes/
  sub0squad process add my-flow --from reviewed --set build.ascent.maxRoundTrips=5 --describe "reviewed, five integration round trips"
  sub0squad do "write a calculator in Python" --process my-flow
  sub0squad project wait w-000002 --timeout 900
  sub0squad process show my-flow                # the file: every stage and parameter, so it says what runs
  ```

  The engine has no stage of its own: it walks the process's stages, so `--without integrate` (a process with a stage fewer), a stage added in the file
  (`process tools` lists what a job stage may run; `process check FILE` says what is wrong with a file; `process add NAME --file FILE` registers it) or another
  condition is a new workflow with no code changed. `project status --json` shows each stage's state and, for one that waits, the condition it waits on. The
  workflow keeps a copy of its process, so editing the file later changes nothing that is already running. With the mock agent the executors are not used (the
  mock writes reference files); the stages, the ladder's rungs and the wall times are.
* **If something is wrong:** `sub0squad doctor` again; `sub0squad status` and `sub0squad logs ID` show the jobs; the daemon's log is under `state/up/`.
  After a rebuild restart the daemon (`down`, `up`): a submit from a different build than the running daemon is refused and says so. `sub0squad archive --label NAME`
  (daemon stopped) sets the finished runs aside under `$SUB0SQUAD_HOME/archive/` and leaves a fresh, empty state.

```sh
sub0squad down                                # stops the daemon and the portal
```
<!-- first-use:end -->

### With a local model

Start an OpenAI-compatible server yourself (for example llama.cpp's `llama-server`; the squad never starts or stops your model), check it, and name it:

```sh
sub0squad doctor --engine qwen=http://127.0.0.1:8092/v1
sub0squad up --frontier off                   # a pass must mean the model: the stub tier never supplies the answer
sub0squad do "write a scientific calculator in C++" --agent llm --engine qwen=http://127.0.0.1:8092/v1,model=qwen,ctx=8192,slots=4 --executor fork
```

`--executor` chooses how an item is written: `session` (one conversation; the baseline), `canvas` (small transactions on a revisioned document, each checked
before it is kept) or `fork` (a skeleton, then one function per session, side by side on the idle slots). These commands need a model and are not run by the
test suite; [docs/BLOCK_PROVING_GROUNDS.md](docs/BLOCK_PROVING_GROUNDS.md) lists what has been proven without one (`sub0squad block run`).
`--stance specialist` tells each worker its item is a dedicated assignment it owns, with the standing to contest the card or tests and propose delegation in a
`CONCERN` block (an experiment, off by default: [docs/PROMPTS.md](docs/PROMPTS.md)).

## Documents

| Document | What it holds |
|---|---|
| [RESUME.md](docs/RESUME.md) | Where the work stands: the proving ladder, what each rung shows, the priority order, the engine and the rules |
| [AGENTS.md](AGENTS.md) | Start here to continue the work: where the standing rules, the state, the briefs and the build instructions are |
| [STATUS.md](docs/STATUS.md) | Where the project is, what is proven and by which test, open questions for the owner, standing rules, host safety |
| [EXPERIMENTS.md](docs/EXPERIMENTS.md) | The proving sandbox: method, host and engine, tools, campaigns, results on the real model, the experiment queue |
| [BACKLOG.md](docs/BACKLOG.md) | Work understood but not done: now, next, later |
| [PROMPTS.md](docs/PROMPTS.md) | What every worker is told and why: terms, stances, the CONCERN block, prompt costs, rules for changing a prompt |
| [PROCESS_DESIGN.md](docs/PROCESS_DESIGN.md) | Workflows as processes of stages, predicates, the escalation ladder, mends, planning questions, the wizard |
| [CLEAN_SLATE.md](docs/CLEAN_SLATE.md) | Planning from a bare request with no reference in the harness; how a plan should be written |
| [CANVAS.md](docs/CANVAS.md) | The canvas, forked sections, transactional edits, folded views and read requests |
| [CONTEXT_AND_PLANNING.md](docs/CONTEXT_AND_PLANNING.md) | Context levels, knowledge and the concern channel's design |
| [BLOCK_PROVING_GROUNDS.md](docs/BLOCK_PROVING_GROUNDS.md), [SCENARIOS.md](docs/SCENARIOS.md) | Building blocks proven without a model; capability scenarios and their fixtures |
| [METRICS.md](docs/METRICS.md) | What is measured and how: accounting, ticks, scorecards, quality |
| [INTERVENTIONS.md](docs/INTERVENTIONS.md) | Every manual fix, as a catalogue row with a detector, a mend and a fixture |
| [COMPONENTS.md](docs/COMPONENTS.md), [DECISIONS.md](docs/DECISIONS.md), [DEV_SETUP.md](docs/DEV_SETUP.md) | Layers and components; decisions taken; the development machine |
| [ARCHIVE.md](docs/ARCHIVE.md) | Retired documents, by permalink to the commit before they were removed |

## Licence

MIT, see [LICENSE](LICENSE).
