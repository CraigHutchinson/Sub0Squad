# Components: units, layers and libraries

This document describes the code as it is. A **unit** is one source file with its header: one responsibility, testable alone. A **layer** is a set of
units that is built, tested and changed together, and that may depend only on the layers below it. Each layer is **one static library**, and the rule
"only downward" is enforced twice, by two different mechanisms, because each catches what the other cannot see.

## The layers

The lists live in ONE file, `cmake/Layers.cmake`, read by both the include check and the build. Adding a unit means adding its stem to one list.

| Layer | Library | Units | Responsibility | Tests that link it as their highest layer |
|---|---|---|---|---|
| 0 | `sub0squad_kernel` | build_id, paths, frame, unix_socket, journal, job_spec, job_store, process_tree, progress, event_channel, host_guard, harness (the descriptor AND the registry) | durable events, jobs, sockets, process control, host guards, the harness registry | test_job_spec, test_journal, test_job_store, test_host_guard, test_adapter; the tests that only drive the binary (test_e2e, test_project, test_archive, test_idle) |
| 1 | `sub0squad_runtime` | shim, supervisor, status, mock_worker | running one job safely (session leader, subreaper, resume); status without the daemon | test_shim |
| 2 | `sub0squad_domain` | engine, plan, division, policy, predicate, process, mends, contract, attempt_failure, plan_lint, ledger, workflow, workflow_status, overseer, briefing, recall, trace, triage, edits, outline, canvas, turn_control, throughput, qa, scenario_compare | pure rules over data: plans, the decision ladder, processes and their predicates, `advance()`, mends, governance records, diagnostics | test_predicate, test_process, test_stream |
| 3 | `sub0squad_harnesses` | harness_registry (`registerBuiltinHarnesses`), harness_common, harness_web, harness_cpp, harness_py, cpp_templates, py_templates, web_templates, calculator_spec, sandbox | what a project type provides: scaffold, stubs, gate, integrator, precheck, blueprints, mocks | test_plan, test_policy, test_edits, test_diagnostics, test_workflow, test_workflow_stages, test_workflow_mends, test_platforms |
| 4 | `sub0squad_agents` | llm_agent, agent_card, canvas_agent, fork, architect, optimizer, replay, scenario, blocks | model-driven workers (session, canvas, forked sections), the scenario suite, the model-free proving grounds | test_agent, test_canvas_agent, test_fork, test_mends, test_blocks, test_scenarios, test_executors, test_heal, test_nearpass |
| 5 | `sub0squad_orchestration` | daemon (reactor, IPC), daemon_jobs, daemon_workflow, daemon_governance, behind `daemon_impl.hpp` | the event loop, leases and shedding, executing workflow actions, requests | (through the binary) |
| 6 | `sub0squad_presentation` | client, cli, cli_jobs, cli_project, cli_tools, cmd_process, cmd_archive, first_use, portal, portal_sampler, portal_files, portal_page (and the embedded `web/portal.html`, `web/portal.js`) | the CLI, the first-use commands, the portal | test_portal, test_portal_page (node) |

`src/sub0squad.cpp` is `main()`: the **composition root**. It is in no library. It registers the built-in harnesses and dispatches every command.

## How "only downward" is enforced

1. **Includes: the `layering` test** (`cmake/CheckLayers.cmake`). A source or header may include only its own layer or a lower one; a file assigned to
   no layer fails it; the list of tolerated violations is empty and may only shrink. Run by hand it needs its argument:
   `cmake -DSRC_DIR=<repo> -P cmake/CheckLayers.cmake`. Without `SRC_DIR` it is now an error (it used to check nothing and print "layering ok").
2. **Calls: the `link_<layer>` targets and tests.** An include check cannot see a function that is DECLARED low and DEFINED high. So for every layer
   the build links a program (`tests/link_main.cpp`, an empty `main`) from that layer's library with every object forced in (whole-archive) and only
   the libraries below it. If any unit of the layer calls upward, that program does not link and the build fails. The seven `link_*` tests run them so
   the report shows them.

The second mechanism found exactly one upward call when it was introduced: `findHarness` was declared in layer 0, called from layer 2 (`workflow`,
`plan`, `triage`) and defined in layer 3. The fix is the shape now in the code: **the registry is kernel code and starts empty**
(`registerHarness`, `findHarness`, `knownGateKind`, `harnessWithBlueprint`, `harnessNames` in `src/harness.cpp`), and the harnesses layer offers one
call, `registerBuiltinHarnesses()` (`harness_registry.hpp`), made by the composition root and by each test that needs harnesses. Registration is
explicit rather than by static objects because a static library's unreferenced object is dropped by the linker. Registering twice is harmless; it is
done at start-up, before any thread.

A test links the highest layer it needs (`sub0squad_add_test(name layer)`), never the whole program, so a kernel test cannot reach the daemon by accident.

## Private headers

`src/*.hpp` are private to a layer: `daemon_impl.hpp` (5), `agent_impl.hpp` (4), `portal_impl.hpp`, `cli_impl.hpp`, `cmd_process.hpp`,
`cmd_archive.hpp`, `first_use.hpp` (6), and `json_util.hpp` (the JSON writer and reader helpers, used by every layer; it includes nothing of
the project).

## Rules for a new unit

* One responsibility, a header with Doxygen comments, `#pragma once`, `[[nodiscard]]` on what returns a value that matters.
* Put its stem in the lowest layer whose units it needs. If it wants something from above, the thing above registers itself or is passed in.
* A test of its own, linked against its layer. A unit that starts processes takes the program's path as an argument; nothing re-executes
  `/proc/self/exe` (catalogue row 32).
* A new capability is off by default: a process parameter, a flag, or a registered process of its own.

## Known shape debts (stated, not hidden)

* `fork.cpp` (about 1,000 lines) holds the skeleton, the rounds, the assembly and the mend application of forked sections in one unit.
* `workflow.cpp` (about 900 lines) holds the journal projection, the stage derivation and the item ladder's bookkeeping.
* The orchestration layer has no test that links it: it is exercised only through the binary (test_e2e, test_project and the daemon-driven tests).
* `test_mends` links the agents layer because it exercises the fork's mend application, although `mends` itself is a domain unit.
