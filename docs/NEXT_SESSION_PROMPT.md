# Starting prompt for a new session in this project

Paste the block below as the first message of a new Claude Code session opened in this folder.

```text
You are continuing **Sub0Squad** (working title): https://github.com/CraigHutchinson/Sub0Squad (public, MIT), a native C++23 toolbox for delegated,
parallel, resource-aware agent work. Work in this folder; read other repos, do not edit them (file issues instead).

READ FIRST, in order: docs/HANDOFF.md (state, findings, plan), README.md, docs/DESIGN.md (source of truth), docs/DECISIONS.md, docs/OPTIMIZATION.md,
docs/SQUAD_UX_AND_HARNESS.md, docs/RUNNING_PROJECTS.md.

STATE. The daemon, CLI, sandbox workflow engine, web-static harness, personas, work cards, triage-first `do`, the escalation ladder (retry, decompose,
send back to the producer, rearchitect, stub-frontier escalate), the verify-and-repair llm agent, the per-job optimiser, explicit memory and disk guards and a
verified model fetch all exist and are tested with mock agents (13 tests, all passing). Real-model runs on this CPU (Qwen3.6 35B-A3B at 8 tok/s,
Qwen3.8-27B Q4_K_M at 1.8 tok/s) are too slow to iterate: the owner paused to optimise the LLM engine on this hardware or move to another machine.

THIS SESSION'S JOB (confirm with the owner first): pick the track in docs/HANDOFF.md section 5A. Either (1) engine/hardware work: measure and improve tokens/s
(threads, NUMA, quant, speculative decoding, the Sub0Llm engine via its issues #90, #95, #100, #101) with sub0perf discipline (one CPU-heavy job at a time, check
`sub0squad guard`), or (2) on a new machine: rebuild with the CPM overrides, re-run ctest, re-measure the engines there, then continue with section 5B.

STANDING RULES. Report outcomes faithfully (failures with output; say what was not verified). Verify claims from subagents or docs against the code.
Sub0 style: camelCase public API, Doxygen on every interface, `#pragma once`, one responsibility per header, `[[nodiscard]]` on failure-signalling APIs, no per-call heap
allocation in hot paths, new capability off by default. Commit in small steps as Craig Hutchinson <54269136+CraigHutchinson@users.noreply.github.com> with the trailer
`Co-Authored-By: Claude <noreply@anthropic.com>`. No live frontier model (Claude/Codex) is ever enabled without an explicit owner switch. Never send anything
off the machine without asking. One CPU-heavy job at a time. Never run a model job under `--max-jobs 1` together with the engine as a job (it takes the only slot).
```
