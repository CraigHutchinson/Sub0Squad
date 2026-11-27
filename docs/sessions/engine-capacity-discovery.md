# Follow-up session: receive engine capacity in scheduling

Status: in progress (2026-11-27). Backlog: [engine capacity](../BACKLOG.md#engine-capacity-discovery).

## Goal

Have the planner and workflow admission use checked capabilities of the actual selected endpoint, including a one-slot server. Preserve explicit
configuration and handle unsupported, stale, busy and unreachable endpoints honestly; do not infer spare capacity from a failed query.

## Evidence and steps

On 2026-11-27 Inkling 2 Mini at the owner's local 8092 endpoint reported `/props` `total_slots: 1` and `/slots` one idle slot. `engineSlots` reads
`/slots`, and the fork executor uses its idle count. `src/planner.cpp` defaults to profile `maxConcurrent`; `src/workflow.cpp` assigns lease caps from
that configured value. Audit all consumers, including multiple profiles pointing at the same endpoint, before designing a single capacity contract.
Distinguish total capacity from momentary idle count and from leases held by this daemon; do not treat an idle snapshot as a reservation. Add
deterministic tests for one and multiple slots, conflicting configuration, missing capability responses, and concurrent admission; receive the result
in a named workflow. Check host ownership before real-model runs. No server restart or retuning is authorized by this brief.

## Progress (2026-11-27)

Planner jobs now acquire the configured endpoint lease, the planner bounds its width by `/slots`, and the daemon bounds HTTP endpoint leases by the
reported total (one if unavailable), retaining the most restrictive active or queued configuration for each identical resource string. A fake
one-slot server and a four-slot configuration exercise daemon admission. This is not a global server reservation: other clients and other daemon
instances do not share these leases, equivalent URL spellings are not canonicalized, and forked multi-request jobs need further receiving tests.
The capability request is synchronous on the daemon's scheduling path when the configured cap exceeds one, so slow endpoints can delay scheduling.
Address those limits and multi-slot, failure, restart and competing-profile coverage before closing this brief.
