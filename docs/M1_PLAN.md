# M1 plan: daemon, CLI and process jobs, proven end to end on mock work

Status: PLAN, 2026-11-09. Nothing here is implemented. Design context: DESIGN.md sections 3, 4, 13, 14, 18. Legend as in DESIGN:
**[V]** verified, **[P]** proposed, **[U]** unknown until tested.

## Scope

In: one binary `sub0squad` with `serve | submit | status | logs | cancel | wait | resume`; a control socket; the **process job kind**
(an external command in its own supervised process tree); a crash-safe journal; `lost` detection; progress through the event fd and the
state-file adapter; the `mock-worker`; end-to-end scenarios. Out (later milestones): arbiter and leases (M2), metric registry,
manifests and portal (M3), tiers, quotas, decisions, MCP, stub frontier (M3b), planners (M4), real engines (M5).
The data shapes M3 and M3b need (`sub0squad-event-1`, journal record types, job `tool` and `tier` fields) are fixed **now**, so
later milestones add readers, not formats.

## Decisions to take (defaults chosen; owner may overrule)

| Question | Choice [P] | Why |
|---|---|---|
| Transport for the CLI | **unix domain socket** `$XDG_RUNTIME_DIR/sub0squad/control.sock`, dir 0700, socket 0600, `SO_PEERCRED` uid check | local only, no port, no auth secret; Windows named pipe later |
| Framing | a socket `ITransport` implemented in this repo, reusing Sub0Pipeline's `FrameReader` (length-prefixed frames) | one framing for the control socket and later socket workers; Sub0Pipeline ships only the pipe transport [V: `remote/pipe_transport.hpp`, `ITransport` in `remote/transport.hpp`] |
| Message encoding | JSON via **simdjson 5.1.1** (owner decision, pinned via CPM): reads use the on-demand forward parser (register handlers, one pass), writes use `simdjson::builder::string_builder` (a reusable buffer, `clear()` keeps capacity, so no per-message heap in steady state). No nlohmann. simdjson types stay in `.cpp` files, never in public headers | one JSON library across the family (Sub0Llm already uses simdjson 3.10.1); readable journal and events; `tests`: `--selftest` writes and reads a JSON object [V] |
| Process job vs `SUB0PIPELINE_BUILD_REMOTE` | process jobs are **deferred jobs** (`emplaceDeferred`) that run the shim and complete on its exit [V: deferred completion holds no executor thread]; `BUILD_REMOTE=ON` is enabled now and exercised by **one native remote kind** (`sub0squad worker` hosting a registered kind over the pipe transport) | the process kind wraps arbitrary commands (the Python harness) which remote sub-graphs cannot; remote sub-graphs are the path for in-tree native kinds and are proven early so M2/M4 can rely on them |
| Supervision | a per-job **shim** (`sub0squad exec-shim`, the same binary) is session and group leader, sets `PR_SET_CHILD_SUBREAPER`, runs the command, writes the outcome atomically to `<jobdir>/exit.json`, then exits | jobs survive a daemon restart; the outcome is recorded by the thing that actually saw it; double-forked descendants reparent to the shim so teardown can enumerate them |
| Liveness | `pidfd_open` on the shim, plus (pid, `/proc/<pid>/stat` starttime) stored in the journal to defeat pid reuse; **no heartbeats** | zero idle cost; a shim that is gone with no `exit.json` is `lost` |
| Journal | append-only NDJSON segments `journal-1` (`seq`, `ts`, `type`, body), fsync on state transitions only, torn final line discarded on replay, snapshot on clean shutdown | durable truth; the live bus is a view of it (DESIGN section 7) |
| Event fd | worker inherits fd 3 (`SUB0SQUAD_EVENT_FD=3`), NDJSON `sub0squad-event-1` | status without parsing logs |
| Daemon idle | one `epoll` reactor thread (control socket, pidfds, timers); executor threads parked | gate: ~0 % CPU |
| Daemon start | client auto-start is **off** by default; `serve` is started explicitly (a systemd user unit example goes in the docs) | new capability off by default |

## Platform (see DESIGN section 20 and PLAN_REVIEW.md)

The rows above describe the **Linux backend**. They are written against five interfaces (`IProcessSupervisor`, `IIpcEndpoint`,
`IRuntimePaths`, `IDurableFile`, `IHostFacts`), and the Windows backend maps them to a Job Object, a named pipe with an owner-only ACL,
`%LOCALAPPDATA%`, `FlushFileBuffers`/`ReplaceFileW` and processor-group topology. Done-ness is stated per platform: **M1 (Linux)** is
done when the gates pass here; **M1w (Windows)** is done only when the same scenarios pass on a Windows host or CI, which needs a
Windows run the owner must provide. Cooperative stop is a message (not SIGTERM); the event channel is an inherited pipe.

## Components [P]

- `squad/control` : unix-socket `ITransport`, request/response and streaming (logs, wait, subscribe), `ControlApi` (the one API the
  CLI, later MCP and portal all call).
- `squad/jobs` : job record and state machine (`queued running succeeded failed cancelled lost`), `JobStore` rebuilt from the journal.
- `squad/process` : `ProcessJob` (spawn via the shim, `pidfd`, deferred completion), tree teardown and its verification.
- `squad/journal` : writer, replay, snapshot.
- `squad/events` : `sub0squad-event-1` parser, progress snapshot per job.
- `squad/adapters` : `state-file` progress adapter for `optimization-run-1`.
- `squad/mock` : the `mock-worker` and its scenario script.
- `squad/cli` : subcommands, `--json` on every read command.
All headers follow the family style (camelCase, Doxygen, `#pragma once`, one responsibility, details under `detail/`).

## Cancel and teardown (gate 1)

1. `cancel` marks the job `cancelling` in the journal, sets the stop token, sends SIGTERM to the process **group**.
2. After a grace period (default 5 s) SIGKILL to the group, then to every descendant of the shim found through its subreaper
   children (`/proc/<pid>/task/*/children`), repeated until none remain or a deadline passes.
3. **Verified**: the shim walks `/proc` and confirms no process has the job's pgid, sid or the shim as an ancestor, records
   `exit.json` with `teardown: verified` (or the survivors if not), then exits. The daemon marks `cancelled` only on `verified`;
   otherwise `cancel_failed` with the survivor list, never a silent success.
4. A server the worker started (the mock "server", later `llama-server`) is covered by the same rule. A process that escapes by
   `setsid` plus double fork still reparents to the shim (subreaper), which is why the test uses exactly that.
**[U]** Subreaper semantics under the user's systemd session and a very short-lived grandchild: tested in step 4.

## Lost, resume, status (gates 2 to 4)

- On `serve` start (and on any CLI read when no daemon is up, from the journal read-only) every non-terminal job is reconciled:
  shim alive and starttime matches → adopt (`running`); shim gone with `exit.json` → apply it; shim gone without it → **`lost`**,
  with the last progress and the time last seen. A stale `running` is impossible by construction.
- `resume` requires a `cancelled`, `lost` or `stalled`-class record; for the optimisation harness it relaunches the declared
  `resumeCommand` (the harness's own `--resume`) against the **real** `state.json`; interrupted attempts stay recorded.
  Gate test uses a copy of `../Sub0Llm/out/optimization/iq1s-pass2-run-001/state.json` (read only, copied into the test dir)
  and the real `iterate.py --resume` with its provider replaced by a stub command so no model runs.
- `status` returns state, phase, `done/total`, elapsed, last event time, and for `optimization-run-1` the attempts, `no_progress`
  and `recoveries`, all from the event stream and the adapter, **never from log text**.

## Steps (each small, tested, committed)

0. **Platform seam.** the five interfaces with the Linux backend and a fake backend for tests; no platform header outside it.
1. **Skeleton.** subcommand dispatch, `--json`, versioned `ControlApi` stub with protocol handshake, `IRuntimePaths`, single-instance
   lock, the `job-spec-1` schema (argv, cwd, allow-listed env, stdin, tool, tier, data class, wall budget, resume command), owner
   raw-command vs registered-manifest submit rule (agents never submit raw commands). Test: CLI parses, schema rejects bad specs.
2. **Journal.** writer, replay, torn-tail test, fuzz of truncation points. Test: replay equals live state.
3. **Control socket.** socket `ITransport` + `FrameReader`, `serve` accept loop on the reactor, `status`/`submit` round trip.
   Test: unauthorised uid rejected (where testable), many clients, a malformed frame does not kill the daemon.
4. **Shim + process job.** spawn, subreaper, `exit.json`, teardown verification. Tests: normal exit, failure code, SIGTERM
   ignored, `setsid` + double-fork escapee, crash of the worker, daemon killed mid-job (job survives, then is adopted).
5. **Event channel + progress snapshot + `logs`/`wait`.** non-blocking worker side with drop counter, bounded daemon queue, per-job
   `stdout.log`/`stderr.log` with a byte cap and rotation. Test: `status` shows progress with the log file deleted; a flooding worker
   cannot stall the daemon or itself.
6. **Lost and offline diagnostics.** kill the shim: job becomes `lost`; with the daemon down, CLI `status` and `sub0squad doctor` still
   say `lost` with `asOf`. `maxParallelJobs` (default 2) is the only M1 concurrency limit; wall-time budget enforced by the shim.
7. **Mock worker + scenarios.** the scripted behaviours of DESIGN section 13; e2e scenarios as ctest entries
   (`e2e_cancel_tree`, `e2e_lost`, `e2e_resume_state`, `e2e_progress`, `e2e_idle`).
8. **state-file adapter + resume on real `optimization-run-1`.**
9. **Remote sub-graph smoke.** `SUB0PIPELINE_BUILD_REMOTE=ON`, `sub0squad worker` hosting one registered kind over the pipe
   transport, one job through it; proves the path M2 and M4 depend on. If it does not fit the pinned Sub0Pipeline, record why
   and propose a change on a **branch of the local checkout** (not edit it) and ask.
10. **Idle gate.** 10 min with one idle and one finished job: daemon CPU time delta ~0 (measured with `/proc/<pid>/stat`, run alone).

## Gates (all required for M1 done)

| Gate | Test |
|---|---|
| Cancel leaves no child and releases everything | `e2e_cancel_tree`: worker + child server + `setsid` double-forked escapee; after `cancel` no process with the job's marks, `exit.json` says `verified`, job `cancelled` not `failed`; repeated 50 times without a leak |
| A dead supervisor is `lost`, never `running` | `e2e_lost`: SIGKILL the shim; daemon up and daemon down both report `lost` |
| Resume works on a real `optimization-run-1` | `e2e_resume_state` on a copy of the real stalled `state.json`, attempts preserved, new attempt appended |
| Status reports progress without parsing logs | `e2e_progress`: logs removed/garbled, progress still correct |
| Daemon idle at ~0 % CPU | step 10 |
Also: `ctest` green from a clean configure with the **pinned** dependencies (local overrides unset) before M1 is declared done, because the overrides can hide a pin that does not build.

## What is deliberately left open for M3 and M3b

Manifests (`tool-manifest-1`), metric registry, portal, tiers and quota leases, decision policy, MCP, stub frontier. M1 only reserves
their seams: every job record already carries `tool`, `tier` (always `local` or `stub` in M1), `owner`, `dataClass`; every journal
record has a stable type name; every event can carry labels. Nothing in M1 can reach a hosted model: there is no network code in
the tree.

## Verification with sub0perf (Sub0Math tooling) [owner direction, 2026-11-09]

`cmake/dependencies.cmake` now pulls Sub0Math (DOWNLOAD_ONLY, pinned; local override honoured) and includes
`cmake/Sub0MathTooling.cmake` [V: configures cleanly; `sub0math_tooling_dir.txt` is written; no hooks are installed until a
benchmark target is declared]. The sub0perf package (calibrated interleaved rounds, controls, per-host baselines, levels, the
measuring lock, the pre-push gate; `Sub0Math/docs/PERFORMANCE.md`) is how every **timing and idle-cost claim** in this project is
checked, instead of ad hoc stopwatch tests. Correctness gates stay in ctest; performance gates move to sub0perf:

| Budget | Benchmark target (`sub0math_add_benchmark`) | Gate |
|---|---|---|
| Journal append (state transition, with and without fsync policy) | `Sub0Squad_Bench` pair: native append vs a plain `write` twin as the control | no regression against the host baseline; fsync only on transitions |
| Control round trip (CLI to daemon `status`) | in-process loopback of the socket transport | ms-scale budget, regression checked |
| Event parse (`sub0squad-event-1` line to progress snapshot) | parser over a recorded stream | no per-event heap allocation (the family rule) shown by the allocation counter |
| Reactor wake and job spawn latency | shim spawn to first event | recorded per host |
| Idle CPU of the daemon | `/proc` CPU-time delta over a fixed window, run through the same tooling's quiet-machine checks | ~0; this replaces the ad hoc step-10 measurement |
| Later: arbiter grant latency, ledger append, metric ring buffer update (M2, M3) | same pattern | |

Rules carried over: timing runs alone (the sub0perf lock enforces one measuring run per repository), baselines are per host and
recorded with a reason, and a result is only trusted when the controls say the environment was quiet (ENVIRONMENT verdicts are
inconclusive, not passes). The pre-push gate is installed by configuring once a benchmark exists; it never pushes anything itself.

**The squad's own exclusive measurement window must interoperate with the sub0perf lock** (DESIGN section 5): the arbiter's
`measurementWindow` lease acquires the same per-repository lock (`.git/sub0perf/lock`, pid + time, stale-takeover rules) before it
grants, so a squad job and a hand-run `sub0perf` never overlap, and the lock is released by the lease's scoped release on cancel or
crash. The lock file format is Sub0Math's; we use it, we do not fork it. Specified here, built in M2.

## Gaps review

[PLAN_REVIEW.md](PLAN_REVIEW.md) lists every gap found in this plan (platform, diagnostics access, lifecycle, threat model) with its
resolution; the steps above already include the blocking and high ones.
