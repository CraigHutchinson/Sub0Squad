# Portal work overview

Status: initial overview and evidence adapters implemented, 2026-11-18. This extends the current per-workflow portal with a read-only view of projects, backlog work, scenarios and tier campaigns. Use the [implementation brief](sessions/portal-work-overview.md) to begin the first slice.

## What the owner needs to see

The overview answers three questions without opening several terminals. What is running now? What has been staged for a later run? Which completed runs have supplied the evidence the next work needs? Open any row to see the exact workflow stage, backlog item, scenario result or artifact behind it.

Start with three views on one inventory. **Activity** groups active, waiting and queued work across projects, and shows stage, age, last progress and the next dependency. **Staged and backlog** groups prepared work and open design work, with a short reason for readiness or blockage. **Evidence** groups tier campaigns by build and engine with expected, completed, passing and failed trial counts. Filters for project, work type, tier and state apply to each view. Include a stable workflow selector and preserve the selection, filters and scroll position across refreshes.

The first walkthrough is small. An owner sees a calculator T1 diagnostic running, a separate project with one staged backlog item, and a completed T0 campaign. They open the active run's original detail, find the staged item's brief, and inspect an archived failure after its daemon shuts down. The trade-off is a narrow inventory model with existing detail views. Rich graphs, metrics and controls can follow once the overview tells the truth across homes.

## Work and run identity

| Record | Identity and relationship | Source |
| --- | --- | --- |
| Project | Stable configured project ID, display name, and registered homes or artifact roots. A project can have several registered sources. | Local portal configuration. |
| Work item | Project-scoped ID, type, title, owner when recorded, dependencies and links to a brief or fixture. A backlog item can exist without a run. | An explicit, curated list of staged and backlog work. Repository Markdown remains the authored brief. |
| Campaign | Project-scoped ID for a tier, scenario cohort or experiment. Identifies planned cases and seeds, profile and admission evidence. | Runner manifest and gate report. |
| Execution | Project ID, registered source ID, and source-local workflow or trial ID. A work item or campaign can have many executions. | The owning daemon, immutable trial folder or archive. |

The compound execution key matters because different homes can both use `w-000001`. Display the familiar short ID and preserve the source ID in the deep link. An archived run and a live run can refer to the same execution only when the source provides stable provenance. If identity is uncertain, show each as a separate record. Preserve immutable trial IDs and show reruns and superseded builds as separate attempts within a campaign.

## State and evidence

Keep three fields separate: the work's lifecycle, the latest execution's verdict, and the freshness of the source. A project may have several simultaneous states across its items. Group counts come from those items. A failed run may leave the corresponding backlog item open.

| Lifecycle | Evidence that justifies it |
| --- | --- |
| Backlog | A registered problem or design item with no prepared execution spec. |
| Staged | A concrete brief or run spec and its dependencies are recorded. Submission to the runner has yet to occur. |
| Queued | The owning runner acknowledged the job or trial. Show resource wait when reported. |
| Running | The owning process reports a current execution and there is fresh activity. Show phase and the time of the last observed progress. |
| Waiting | The runner reports a specific dependency, owner response, gate, resource or external input. Show why and when it began. |
| Finished | A terminal outcome was recorded by the owner of the execution. |

Show the recorded verdict separately as pass, fail, incomplete, indeterminate or cancelled, with the source and time. Show diagnostic, forced, scripted, replayed and stale evidence visibly in campaign detail. A scripted pass can demonstrate a harness path. A forced T1 run can diagnose a problem. A current release verdict comes only from its owning gate. Keep last-known running data when the source stops answering, with its timestamp and freshness marked stale or offline. An old `live.txt`, a pending folder or a queue file alone cannot establish active work.

## Read path and ownership

Use one long-lived loopback overview over explicitly registered local roots. Individual workflow homes and their daemons keep authority over scheduling, the workflow graph and inbox. The existing portal already offers `/api/state`, `/api/live`, and per-workflow files, search and file views, with `?wf=` selection and an offline state. Reuse these as the workflow detail path. The overview adapter projects the bounded summaries needed for the list: current daemon status where available, last recorded status and archives otherwise, plus campaign manifests and verdict JSON. [Current portal](../src/portal.cpp), [current page](../web/portal.js).

Prioritize durable inputs that already exist: workflow `run-info.json`, workflow status, `gate.json`, the scenario runner's result, and the sandbox's `manifest.json` and `verdict.json`. Treat historical `summary.md` as a human-readable link. For an older campaign without a machine-readable planned denominator, display the verified counts and leave the total unknown. Prefer progress emitted by the owning runner when adding a campaign adapter. Tail or index only what the view requests, keep refresh work bounded, and surface source errors per row so one broken archive cannot blank the overview.

Serve the overview through the existing local portal boundary. Give browser requests opaque project/source/run keys. Resolve artifact paths against their registered roots on the server and preserve the existing path checks, preview isolation and output caps. Exclude private checker artifacts from the index. Cross-project views are read-only in the first slice. The existing single-workflow inbox remains routed to its owning home and is outside the overview. Remote access and scheduling controls remain separate owner decisions.

## Run the overview

Save a registry such as the following as `out/work-overview.json`. Paths are relative to the registry. Use a home directory for each workflow source and a public root for each brief or report.

```json
{
  "localProject": "Squad",
  "homes": [{"id":"calculator-t1","project":"Calculator","home":"runs/t1/qwen35-example"}],
  "work": [{"id":"parser-repair","project":"Calculator","kind":"scenario","state":"staged","tier":"T1",
            "title":"Parser repair","root":"../docs","brief":"T0_LAYERS.md","dependsOn":["t0-gate"]}],
  "reports": [{"id":"t0-gate","project":"Calculator","kind":"gate","tier":"T0","root":"runs/t0","file":"gate.json"}],
  "sandboxCampaigns": [{"id":"iterative","project":"Calculator","root":"runs/t0-iterative"},
                       {"id":"products","project":"Calculator","root":"runs/product-tiers"}]
}
```

Launch `out/build/mingw/sub0squad.exe portal --port 0 --work-registry out/work-overview.json` and open the Work tab at the printed local address. The gate adapter reads the counted case rows, the scenario adapter reads `scenario-results.json`, and sandbox campaigns read public manifests and terminal verdicts. Homes with no current daemon use the existing journal reader. An unconfirmed trial gets its own state until the owning daemon or a terminal artifact supplies evidence.

On Windows, the HTTP and Edge receiving check used two archived homes with the same workflow ID, a staged scenario, a historical T0 gate and the sandbox/product campaigns. Both deep links served their original detail and the checker path was rejected. The native fixture also checks a live source projection and the page test checks filtering and refresh. Cross-platform browser receiving and measured refresh cost stay open.

## Backlog and receiving order

The [portal access brief](sessions/portal-access.md) proposes a separate Node.js viewer for the approved summary, starting locally and then receiving it through Tailscale Serve from a second device. The existing local dashboard continues to own workflow detail and operator actions.

| Slice | Delivery | Receiving check |
| --- | --- | --- |
| **Overview and selector** | Register two homes and a small staged-work list. Render activity and backlog, then open existing workflow detail by composite key. Use a polling read path first. | Both homes can have a `w-000001`. Show one live, one staged, and one terminal archived execution. Going offline preserves the last verified status, and selection survives refresh. |
| **Campaign and scenario view** | Add adapters for one sandbox cohort and one T0 gate. Present planned versus counted trials and link each case to artifacts. Add named scenario results next. | A forced, scripted or stale trial is visibly distinct from counted release evidence. Missing or partial reports render incomplete with the known counts. |
| **Dependencies and evidence** | Link work items to proving blocks, tier cases and receiving workflows. Reuse existing planning and decision detail for gates, disputes and reruns. | A failing T1 case can be followed to its T0 or scenario reproduction, then forward to the rerun which exercised the fix. Display dependency holds separately from the latest run's verdict. |
| **Durability and scale** | Verify mixed live and archived histories on Windows and Linux. Add bounded incremental indexing and history pagination when measurement shows a need. | Validate cache invalidation, malformed input, deleted archives and restarted daemons with two-home fixtures. Load a representative campaign and measure refresh cost while a real workflow runs. |

The first slice pairs the owner's activity view with the actual durable sources and their failure modes. Test normalization and source ownership separately from page rendering. Extend `test_portal` for registered roots, same-ID collisions, stale snapshots and private paths. Extend `test_portal_page` for the cross-project overview, filters, archived detail and refresh stability. Use a browser receiving check with live and archived sources before calling the view complete.
