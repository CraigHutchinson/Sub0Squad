# Roadmap

Milestones from [DESIGN.md](DESIGN.md) section 10. Tickets for them live in Sub0Llm for now (#86 to #90).

| Milestone | Deliverable | State |
|---|---|---|
| M0 | design agreed; name, repo, licence decided; Sub0Pipeline/Sub0Pub versions pinned | **in progress**: design written (Part B added); graph-growth question answered by `tests/m0_graph_growth.cpp` (no); versions pinned and proven by `sub0squad --selftest` (CPM, local overrides verified); name, licence, repo pending |
| M1 | daemon + CLI, control socket, process job kind with verified tree teardown, journal, `lost`, event-fd progress, mock worker, resume on a real `optimization-run-1`; plan and results in [M1_PLAN.md](M1_PLAN.md) | **Linux gates met 2026-11-09** except the real-harness resume, the remote sub-graph smoke and a few listed items |
| M1w | Windows backend of the platform seam, same gates | not started; needs a Windows run |
| M2 | arbiter on mock resources, ledger | not started |
| M3 | metric registry, tool manifests, SSE, portal v0 (read-only) | not started |
| M3b | tiers, quotas, share cap, permission gate, decision policy + replay, stub frontier, MCP, portal Usage/Settings/Audit | not started |
| M4 | planner, staged fan-out, integrator (mock first, then the quant-format splitter) | not started |
| M5 | `IEngine`, `EngineCaps`, routing: mock engine, llama.cpp, Sub0Llm, Strata | not started |
| M6 | consumed by Sub0Llm as a dependency | not started |
| M7 | live Claude/Codex, enabled only by the owner in the portal | not started |

Gates are in DESIGN.md. Next concrete step: decide the name, then M1 (it needs `SUB0PIPELINE_BUILD_REMOTE=ON` and a socket or pipe transport).
