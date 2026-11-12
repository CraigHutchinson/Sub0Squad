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

**Status:** the daemon, the CLI, project workflows over three project harnesses (C++ with CMake, Python, a static web page) and the model-free proving grounds
work on Linux and are covered by the test suite. Driving a project with a real local model works mechanically but is not yet proven end to end (the first
35B run of the C++ calculator failed; the answer in progress is sections, forks and the canvas: [docs/CANVAS.md](docs/CANVAS.md),
[docs/BLOCK_PROVING_GROUNDS.md](docs/BLOCK_PROVING_GROUNDS.md)). Design: [docs/DESIGN.md](docs/DESIGN.md), [docs/ROADMAP.md](docs/ROADMAP.md),
[docs/DECISIONS.md](docs/DECISIONS.md). The name is a placeholder; the final `sub0*` name is pending.

Origin: it grew out of Sub0Llm's Qwen optimisation harness (`scripts/optimization/*` there), which becomes the first job kind.

## Build

```sh
cmake -S . -B out/build/dev -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/dev
ctest --test-dir out/build/dev --output-on-failure
```

See [docs/DEV_SETUP.md](docs/DEV_SETUP.md) for the local-source override (`CPM_<Name>_SOURCE`) used on the Sub0 dev machine.

## First use

<!-- first-use:begin -->
From a clean checkout to a finished project in a few minutes, with **no model**: the mock agent writes the reference deliverables, and everything else is
real (the plan, the scheduler, the gates that compile and test each item, the integration build). The commands below are run, exactly as written, by the
test `test_readme`, so they cannot rot.

```sh
export PATH=$PWD/out/build/dev:$PATH          # after the build above
export SUB0SQUAD_HOME=$PWD/out/home           # where this instance keeps its state (default: ~/.local/state)

sub0squad doctor                              # checks memory, tools, ports; says what to fix
sub0squad up                                  # starts the daemon and the portal in the background
sub0squad do "write a calculator in C++"      # triage picks the blueprint, the plan loads itself, the work starts
sub0squad project wait w-000001 --timeout 900 # blocks until the project ends (about a minute here)
sub0squad project status                      # the workflow and each item
sub0squad down                                # stops the daemon and the portal
```

* **Watch it live:** `up` prints the portal's address (http://127.0.0.1:8780/ unless `SUB0SQUAD_PORTAL_PORT` or `--portal-port` says otherwise). It shows the
  work graph; click a node for its conversation, which follows the model's tokens as they are generated.
* **Where the result is:** `$SUB0SQUAD_HOME/state/workflows/w-000001/project` is the finished project (here `project/build/calc_cli '2^3^2'` prints 512),
  `state/workflows/w-000001/report.md` the report, and `items/<item>/attempt-N/` every attempt with its conversation, files and gate output.
* **Other starts:** `sub0squad do "write a calculator in Python"`, `sub0squad do "write a scientific calculator as a web page"`, or by name:
  `sub0squad project submit --task calculator-py`. A task with no blueprint stops at "awaiting a plan" and says where the request is.
* **If something is wrong:** `sub0squad doctor` again; `sub0squad status` and `sub0squad logs ID` show the jobs; the daemon's log is under `state/up/`.
<!-- first-use:end -->

### With a local model

Start an OpenAI-compatible server yourself (for example llama.cpp's `llama-server`; the squad never starts or stops your model), check it, and name it:

```sh
sub0squad doctor --engine qwen=http://127.0.0.1:8092/v1
sub0squad up --frontier off                   # a pass must mean the model: the stub tier never supplies the answer
sub0squad do "write a calculator in C++" --agent llm --engine qwen=http://127.0.0.1:8092/v1,model=qwen,ctx=8192,slots=4 --executor fork
```

`--executor` chooses how an item is written: `session` (one conversation; the baseline), `canvas` (small transactions on a revisioned document, each checked
before it is kept) or `fork` (a skeleton, then one function per session, side by side on the idle slots). These commands need a model and are not run by the
test suite; [docs/BLOCK_PROVING_GROUNDS.md](docs/BLOCK_PROVING_GROUNDS.md) lists what has been proven without one (`sub0squad block run`).

## Licence

MIT, see [LICENSE](LICENSE).
