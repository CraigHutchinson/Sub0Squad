# Follow-up session: Project and test work overview

Status: open (written 2026-11-18). Backlog: [Portal](../BACKLOG.md). Design: [Portal work overview](../PORTAL_WORK_OVERVIEW.md). Evidence contract: [Model tiers](../BLOCK_PROVING_GROUNDS.md#scope-and-scaling-beyond-the-proving-grounds).

## Goal

Let the owner see running, queued, staged and archived work across projects in one local portal and open the original evidence for each row. The first slice reuses the existing workflow detail and adds a read-only inventory over two registered homes and a curated staged list.

## What is known

The current portal selects one workflow in one home with `?wf=`. The workflow daemon and archive already provide useful status, and a workflow or trial can outlive its daemon. Source-local workflow IDs can collide across homes. A trial directory can appear before a worker starts.

## Steps

1. Register two local homes with stable source IDs and provide one staged item. Read status from the owning source, and retain timestamps and evidence provenance.
2. Add the overview and selector. Keep support for existing standalone workflow links. Drill from the overview to the existing detail using the project and source identity.
3. Receive the view with one live or controllably simulated workflow, one staged item and one archived workflow. Test two identical local workflow IDs, one offline source, and inaccessible checker paths. Defer campaign adapters and graph redesign until that slice works.

## Done means

The browser shows the expected statuses and grouping from test-owned or approved sources, links to the correct workflow even when IDs collide, preserves selection on refresh, and shows archived evidence when the daemon exits. The staged row never appears as a queued or running job based on its brief alone. Focused portal tests and the browser walkthrough both pass.

## Environment and constraints

Local loopback portal. Reuse existing status and artifact routes where they fit. No downloads, remote listeners or cross-project controls in the first slice. Coordinate any live run and leave the shared inference server untouched.
