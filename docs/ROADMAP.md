# Roadmap

Milestones from [DESIGN.md](DESIGN.md) section 10. Tickets for them live in Sub0Llm for now (#86 to #90).

| Milestone | Deliverable | State |
|---|---|---|
| M0 | design agreed; name, repo, licence decided; Sub0Pipeline/Sub0Pub versions pinned | **in progress**: design written; versions pinned and proven by `sub0workd --selftest` (CPM, local overrides verified); name, licence, repo pending |
| M1 | `sub0workd` + CLI running the existing Python harness unchanged as an external job kind: submit, status, logs, cancel (tree teardown verified), wait, resume | not started |
| M2 | resource arbiter (core sets, memory budget, exclusive measurement lease), journal and replay | not started |
| M3 | Sub0Pub event stream, progress snapshots, Chrome-Trace timeline | not started |
| M4 | planner, fan-out and integrator; first splitter: one sub-task per quant format | not started |
| M5 | `IEngine` seam, `EngineCaps`, capability routing, slot leasing: llama.cpp first, then Sub0Llm (process and in-process) | not started |
| M6 | consumed by Sub0Llm as a dependency | not started |

Gates are in DESIGN.md. Next concrete step: decide the name, then M1 (it needs `SUB0PIPELINE_BUILD_REMOTE=ON` and a socket or pipe transport).
