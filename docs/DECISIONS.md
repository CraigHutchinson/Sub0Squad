# Decisions

## Made (2026-11-09, owner direction)

1. **Native C++23 application**, performant and **without redundancy**: no second scheduler or message bus. Scheduling is
   Sub0Pipeline, in-process messaging is Sub0Pub; only the resource arbiter and the durable journal are new.
2. **Background jobs** that can be delegated to, checked, followed, cancelled and resumed.
3. **Parallel workers and automatic decomposition** of one task into sub-tasks (planner, fan-out, integrator).
4. **Shared resources and collaborative scheduling** (cores, memory, model slots, exclusive measurement window).
5. **Multiple engines and runtimes side by side**: llama.cpp and native Sub0Llm directly (server, spawned process, in-process
   library), with the scheduler aware of each engine's **concurrency cap**.
6. **Spin-off project** with its own `sub0*` name; Sub0Llm is one engine and one consumer.
7. A throwaway Python supervisor was prototyped and discarded: it duplicated Sub0Pipeline.
8. One multi-call binary, `sub0squad`, with subcommands (`serve` for the daemon, `submit`, `status`, `logs`, `cancel`, `wait`, `resume`), instead of a clumsy `sub0squadd` plus a separate CLI.
9. (Stretch) **Agent workers** (Claude Code, Codex) and escalation to a frontier model when work is too complex or needs splitting, under explicit budgets and data-governance rules (DESIGN section 8a).
10. Dependencies via **CPM**, pinned for CI and overridable per machine with `CPM_<NAME>_SOURCE` (docs/DEV_SETUP.md).
11. (2026-11-09) **Mock-first**: the whole control loop is built and proven on scripted stand-ins (mock worker, mock engine, stub frontier) before real engines or frontier models attach (DESIGN section 13).
12. (2026-11-09) **Observable and accountable**: a loopback monitoring portal, a metric registry that tools extend through manifests, and an append-only usage ledger (DESIGN sections 14, 15, 17). New tools, for example Strata as a local engine, plug in by manifest.
13. (2026-11-09) **Local-first with bounded frontier escalation**: tiers, quotas and a frontier share cap; a decision policy exposed as a tool call and MCP (DESIGN section 16).
14. (2026-11-09) **Frontier tiers are stubbed for all development work**; the only way to go live is the owner's explicit switch in the portal's Status and Settings page. CLI, agents and MCP cannot change it.
15. (2026-11-09) **Graph growth**: a running Sub0Pipeline graph cannot be extended (test: SIGSEGV); planners use staged pipelines (DESIGN section 11).
16. (2026-11-09, owner) **JSON is simdjson** (5.1.1 pinned): on-demand forward reads and `builder::string_builder` writes; no nlohmann. Finding: Sub0Llm's notes say simdjson "does not emit JSON"; true for 3.10.1, not for 5.x, which has a builder (verified by building and round-tripping under clang 21). C++26 static reflection in the builder is not available on this toolchain (not detected), so writers are explicit for now.
17. (2026-11-09) **Performance verification uses Sub0Math's sub0perf** (DESIGN section 19).
18. (2026-11-09, owner pointer) **HTTP is Boost.Beast + Asio**, as in Sub0Llm's archived chapter project (which migrated off cpp-httplib), behind our own `IHttpFront` and `IHttpClient` seams; Asio is not the daemon's event loop (DESIGN section 17). Version to pin: 1.92.0 (Sub0Llm uses 1.91.0); first built and verified in M3 (portal) or M5 (llama.cpp client), not before.
19. (2026-11-09, owner) **Windows and Linux are first-class targets; macOS is potential, not a priority.** OS specifics sit behind a platform seam (DESIGN section 20); done-ness is claimed per platform and Windows needs a Windows run.
20. (2026-11-09) **Diagnostics are reachable when the daemon is not**: offline `doctor` and `portal --offline` read the journal, ledger and rollups; metrics and KPIs are a versioned API with `asOf`/`source`, KPIs are data with visible formulas (DESIGN section 21).
21. (2026-11-09) **Agents submit only registered tool manifests, never raw commands**; the daemon's own diagnostics use Sub0Log, not a new logger.
22. (2026-11-09, owner) **A sandbox harness delegates a project-scope task** (`project submit --task calculator-scientific`): an internal planner decision splits it into work items run as jobs, with trusted per-item gates, a recorded decision policy, bounded escalation to `frontier-stub` and a ledger (docs/SANDBOX.md). `frontier-stub` is the only frontier tier in this build, it sends nothing, and `--frontier off|stub` is an owner setting of the daemon.
23. (2026-11-09, owner) **Public repository `CraigHutchinson/Sub0Squad`, MIT licence.** The name is still the working title (rename checklist below); the repository and licence are decided.

## Pending

| Item | State |
|---|---|
| **Final name** | working title `Sub0Squad`; candidates below |
| Licence | decided: MIT (LICENSE file) |
| Remote repository | created: github.com/CraigHutchinson/Sub0Squad (public) |
| Pinned versions | Sub0Pipeline `3d11ffe`, Sub0Pub `504d772` (heads on 2026-11-09), proven only by the selftest |
| Boost version and `CPM_SOURCE_CACHE` | 1.92.0 proposed (about 102 MiB tarball); a shared CPM cache location to choose |
| Strata | not found in the GitHub folder; owner to point at it before its engine manifest is written |
| Windows CI or host | needed to claim Windows done; implies a remote (owner decision) |

### Name candidates (family style is a plain noun: Pipeline, Pub, Log, Math, ECS)

| Name | Daemon / CLI | For | Against |
|---|---|---|---|
| **Sub0Squad** (recommended) | `sub0squad` / `sub0squad` | says what it schedules (work items, workers); shortest | generic |
| Sub0Crew | `sub0crewd` / `sub0crew` | workers as a crew | friendly but vaguer about scheduling and resources |
| Sub0Dispatch | `sub0dispatchd` / `sub0dispatch` | scheduling and routing to engines | leaves out workers and resources |
| Sub0Foreman | `sub0foremand` / `sub0foreman` | assigns and supervises | collides with the well-known Foreman tools |

Availability and trademark checks have not been done for any of them. **Rename checklist** (the name is kept in few places):
`CMakeLists.txt` (`project()`), `cmake/`, `src/` (binary names, namespaces), `README.md`, `docs/`, then the folder.
