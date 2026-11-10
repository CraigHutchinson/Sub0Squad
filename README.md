# Sub0Squad (working title)

**Many small minds, one whole solution.** The squad is part of your team: it delivers complete solutions, delegated and shared among workers,
each part small, tested and handed on.

A native C++23 toolbox for **delegated background work**: submit a job, check its progress, follow its log, cancel it,
resume it. Jobs run in **parallel** on workers that **share resources** (cores, memory, a model server, the exclusive
measurement window) under a **collaborative scheduler**, and one task can be **split automatically into sub-tasks**.
It runs inference through **several engines and runtimes side by side**, llama.cpp and native Sub0Llm directly, and
the scheduler knows each engine's **concurrency cap**.

Built from the Sub0 family, not around it: [Sub0Pipeline](https://github.com/CraigHutchinson/Sub0Pipeline) schedules the
job graphs (deferred completion, cancellation, remote sub-graphs) and
[Sub0Pub](https://github.com/CraigHutchinson/Sub0Pub) carries typed events. The new parts are the resource arbiter, the
durable journal, planners, and the engine seam.

**Status: M1 (daemon, CLI, process jobs) and a sandbox harness (a project task delegated into gated work items with stub frontier escalation) work on Linux; see [docs/SANDBOX.md](docs/SANDBOX.md).** Earlier status: design stage. `sub0squad --selftest` only proves the pinned dependencies link and cooperate here. Start with
[docs/DESIGN.md](docs/DESIGN.md), then [docs/ROADMAP.md](docs/ROADMAP.md) and [docs/DECISIONS.md](docs/DECISIONS.md).
The name is a placeholder; the final `sub0*` name is pending.

Origin: it grew out of Sub0Llm's Qwen optimisation harness (`scripts/optimization/*` there), which becomes the first job kind.

## Build

```sh
cmake -S . -B out/build/dev -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/dev
ctest --test-dir out/build/dev --output-on-failure
```

See [docs/DEV_SETUP.md](docs/DEV_SETUP.md) for the local-source override (`CPM_<Name>_SOURCE`) used on the Sub0 dev machine.

## Licence

MIT, see [LICENSE](LICENSE).
