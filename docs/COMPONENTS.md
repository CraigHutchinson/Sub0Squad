# Components: units and component systems (2026-11-11)

Measured from the tree (about 11,000 lines in `src/`, 40 headers). A **unit** is one source file with its header (one responsibility, testable alone). A **component system**
is a layer of units that is built, tested and changed together; a layer may depend only on layers below it. The rule is enforced by the `layering` test
(`cmake/CheckLayers.cmake`): any upward include fails the build's test run, and the list of tolerated violations may only shrink (it is empty today).

| Layer | Component system | Units | Responsibility | Test home |
|---|---|---|---|---|
| 0 | **Kernel** | paths, frame, unix_socket, journal, job_spec, job_store, process_tree, progress, event_channel, host_guard, harness.hpp (descriptor) | durable events, jobs, sockets, process control, host guards | test_journal, test_job_store, test_job_spec, test_host_guard |
| 1 | **Runtime** | shim, supervisor, status, mock_worker | running one job safely (session leader, subreaper, resume), status without the daemon | test_shim, test_idle |
| 2 | **Domain** (workflow model) | engine (profiles, model client), plan, division, policy, plan_lint, ledger, workflow, workflow_status, overseer, briefing, recall, trace, triage, edits, qa | pure rules over data: plans, divisions, the decision ladder, `advance()`, governance records, diagnostics and observation | test_workflow (monolith: to split by unit) |
| 3 | **Harnesses** | harness_registry (registers entries), harness_web, web_templates, calculator_templates, sandbox | what a project type provides: scaffold, stubs, gate, integrator, mocks | test_sandbox |
| 4 | **Agents** | llm_agent, architect, optimizer, replay | model-driven workers: card, drafts, repair, fork, division, isolated replay | (covered through test_workflow and manual replays) |
| 5 | **Orchestration** | daemon | event loop, leases and shedding, executing workflow actions, requests | test_e2e, test_project |
| 6 | **Presentation** | portal, portal_page, client, sub0squad (main) | CLI, portal, composition root | test_project |

## Findings from measuring them

1. **Layer violations found and fixed in this pass:** the workflow, plan, daemon, sandbox, triage and agents compared the string `"web-static"` (8 files); they now ask the harness registry
   (`harness.hpp`, `harness_registry.cpp`). `engine.hpp` was used from the domain, trace and harness code; it belongs in the domain layer (profiles and the model client), not with the agents.
   The layer check found one more (triage including the web harness), also fixed.
2. **`daemon.cpp` (1,060 lines) is one `Impl` struct of about 40 methods** in four concerns that want to be separate units: reactor/IPC (`post`, `epollAdd`, `send`, `flush`,
   `handle`, `onClient`, `onAccept`); job lifecycle and scheduling (`record`, `reconcileAtStart`, `adopt`, `shedIfPressured`, `scheduleQueued`, `startRun`, `onShimStart`, `onPidfd`,
   `finalizeWith`); workflow execution (`executeWorkflowAction`, `advanceWorkflows`, `doWorkflowSubmit/Plan/Cancel`); governance (`applyGuidance`, `firstResponder`, `answerRequest`,
   `raiseRequest`, `doRequestRaise/Answer`). Next cleanup step: a private `daemon_impl.hpp` and one translation unit per concern, behaviour unchanged.
3. **`llm_agent.cpp` (646)** mixes card building, the attempt/repair loop, continuation, drafts, the fork flow and the plan-phase division; **`client.cpp` (840)** is the CLI dispatch for
   every command (one function per command family would let each be tested); **`portal.cpp` (661)** mixes the sampler, the HTTP server and the API handlers.
4. **Tests are thin where the risk is:** `test_workflow.cpp` (553 lines) is one monolith covering a dozen units; the agents layer (llm_agent, architect, replay) has no direct unit tests
   (they need an engine; they should get a recorded-reply fixture harness, BUILDING_BLOCKS steps 1-2).
5. **One library, one executable:** everything is `sub0squad_core`. Splitting into one static library per layer makes the link direction a build fact and lets each layer's tests link only what they need;
   done after the daemon split, because the layer check already guards the include direction in the meantime.

## Cleanup order (each step keeps the whole suite green; a failing step is reverted, not patched forward)
1. Harness registry and the layer check (done in this pass).
2. Split `daemon.cpp` into its four units behind a private header.
3. Split `test_workflow.cpp` by unit (plan, policy, workflow, division, edits, qa, overseer/briefing/recall).
4. Split `llm_agent.cpp` (card, attempt loop, fork) and `client.cpp` (command groups); `portal.cpp` (sampler, server, api).
5. One CMake library per layer, with the layering test retained.
6. Fixture harness for agents: recorded model replies as inputs, no engine needed (this is the BUILDING_BLOCKS corpus).
Then the extension work proceeds on this base (docs/PROCESS_DESIGN.md).
