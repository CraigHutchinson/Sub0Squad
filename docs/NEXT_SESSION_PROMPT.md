# Starting prompt for a new session in this project

Paste the block below as the first message of a new Claude Code session opened in this folder.

```text
You are starting work on **Sub0Squad** (working title), a new spin-off project at
/run/media/craig-hutchinson/54D4C5011B8AF586/GitHub/Sub0Squad : a native C++23 toolbox for delegated, parallel,
resource-aware agent work. Work only in that folder; read other repos, do not edit them.

READ FIRST, in order: README.md, docs/DESIGN.md (source of truth), docs/DECISIONS.md, docs/ROADMAP.md, docs/DEV_SETUP.md.

WHAT IT IS. A daemon + CLI (one binary, `sub0squad serve|submit|status|logs|cancel|wait|resume`) that runs long jobs in the
background: delegate, check progress, follow logs, cancel (whole process tree, verified), resume. Workers run in parallel and
share resources (core sets, memory, model slots, an exclusive measurement window) under a collaborative scheduler. One task can
be split automatically into sub-tasks (planner -> fan-out -> integrator). Engines/runtimes are pluggable side by side: llama.cpp
and native Sub0Llm (server, spawned process, or in-process library); the scheduler knows each engine's concurrency cap
(EngineCaps). Stretch (M7): Claude Code / Codex as worker kinds and escalation to a frontier model when work is too complex,
stalls, or needs splitting, under budgets and data-governance rules. Native and without redundancy: scheduling = Sub0Pipeline,
in-process messaging = Sub0Pub; the only new core parts are the resource arbiter and a durable journal.

STATE. One commit. `sub0squad --selftest` passes: it builds Sub0Pipeline + Sub0Pub (CPM, pinned SHAs) and proves a DAG, a
deferred job completed from another thread, and typed events. Nothing else is implemented. No remote exists. Name, licence and
repository are still decisions for the owner. A throwaway Python supervisor was prototyped and discarded: do not rebuild a
second scheduler.

ENVIRONMENT (CrogBuntu1). AMD Ryzen 9 8945HX 16C/32T, ~29.6 GiB RAM visible, Linux 7.0, clang 21, CMake 4.4, Ninja. The Sub0
libraries are cloned beside this project (Sub0Pipeline Sub0Pub Sub0Log Sub0ECS Sub0Math Sub0MemPage Sub0TieredCache Sub0HexGrid
Sub0Physics) and `CPM_<Name>_SOURCE` is exported for them (docs/DEV_SETUP.md), so builds use the LOCAL checkouts; CI pins are in
cmake/dependencies.cmake. Open a new shell if the variables are missing (`. ~/.config/sub0/cpm-env.sh`).

THINGS TO READ (do not copy): Sub0Pipeline docs/design/cross-process-orchestration.md (deferred completion, remote sub-graphs,
Payload/IPayloadStore, ITransport, WorkerHost) and docs/observability.md; Sub0Pub docs/IPC.md and USAGE.md. The first job kind to
wrap is Sub0Llm's optimisation harness: ../Sub0Llm/scripts/optimization/iterate.py, llama_provider.py, evaluate_kernel.py,
docs/performance/QWEN_OPTIMIZATION_HARNESS.md; its run record is out/optimization/<run>/state.json (schema optimization-run-1;
a real stalled example is ../Sub0Llm/out/optimization/iq1s-pass2-run-001/). Related Sub0Llm tickets: #86-#90 (milestones),
#68 (harness), #78 (machine test).

STANDING RULES. Report outcomes faithfully (failures with output; say what was not verified). Verify claims from subagents or
docs against the code before relying on them. Sub0 family style: camelCase public API, Doxygen on every interface, `#pragma once`,
one responsibility per header (details under `detail/`), `[[nodiscard]]` on failure-signalling APIs, no per-call heap allocation
in hot paths. New capability off by default. Never push, create a remote, merge, or send anything off the machine without asking.
Commit locally in small steps as Craig Hutchinson <54269136+CraigHutchinson@users.noreply.github.com> with the trailer
`Co-Authored-By: Claude <noreply@anthropic.com>`. One CPU-heavy job at a time; do not run timing work while another job runs.
Delegate research to a subagent, but you own verification.

FIRST TASKS.
1. Verify the state: configure, build, run `ctest` and `sub0squad --selftest`.
2. Close M0's open technical question with a test in this repo (not in Sub0Pipeline): can a Sub0Pipeline job add jobs to the graph
   WHILE it runs? Record the answer in docs/DESIGN.md section 11. If not, specify the staged-pipeline fallback.
3. Plan M1 (see docs/ROADMAP.md): enable SUB0PIPELINE_BUILD_REMOTE, pick the transport (the pipe transport ships; a socket is
   consumer-supplied), design the local control socket between the CLI and `serve`, and the process-job kind that wraps an
   external command in its own process group. Write the plan to docs/M1_PLAN.md with the gates below before coding.
4. Implement M1 in small tested steps. Gates: cancel leaves no child process (including a spawned server) and releases
   everything; a dead supervisor is reported `lost`, never `running`; resume works on a real optimization-run-1 state.json;
   status reports progress without parsing logs; the daemon is idle at ~0% CPU.

STOP AND ASK the owner before: choosing the final name or licence, creating the GitHub repository, changing the Sub0Pipeline or
Sub0Pub libraries (propose a branch in the local checkout instead), or enabling any frontier-model (Claude/Codex) tier.
```
