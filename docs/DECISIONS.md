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
8. Dependencies via **CPM**, pinned for CI and overridable per machine with `CPM_<NAME>_SOURCE` (docs/DEV_SETUP.md).

## Pending

| Item | State |
|---|---|
| **Final name** | working title `Sub0Work`; candidates below |
| Licence | undecided (no LICENSE file yet) |
| Remote repository | not created; name first |
| Pinned versions | Sub0Pipeline `3d11ffe`, Sub0Pub `504d772` (heads on 2026-11-09), proven only by the selftest |
| Graph growth mid-run in Sub0Pipeline | untested (staged pipelines are the fallback) |
| Socket transport, Windows support | open |

### Name candidates (family style is a plain noun: Pipeline, Pub, Log, Math, ECS)

| Name | Daemon / CLI | For | Against |
|---|---|---|---|
| **Sub0Work** (recommended) | `sub0workd` / `sub0work` | says what it schedules (work items, workers); shortest | generic |
| Sub0Crew | `sub0crewd` / `sub0crew` | workers as a crew | friendly but vaguer about scheduling and resources |
| Sub0Dispatch | `sub0dispatchd` / `sub0dispatch` | scheduling and routing to engines | leaves out workers and resources |
| Sub0Foreman | `sub0foremand` / `sub0foreman` | assigns and supervises | collides with the well-known Foreman tools |

Availability and trademark checks have not been done for any of them. **Rename checklist** (the name is kept in few places):
`CMakeLists.txt` (`project()`), `cmake/`, `src/` (binary names, namespaces), `README.md`, `docs/`, then the folder.
