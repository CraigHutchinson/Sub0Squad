# Follow-up session — Portal access and a Node.js viewer

Status — open. [Portal design](../PORTAL_WORK_OVERVIEW.md) · [Backlog](../BACKLOG.md)

## Goal

Let the owner check activity, staged work, and tier verdicts from a second device. Keep the full operator view on the host. Give remote viewers a small dashboard with project labels, work titles approved for display, lifecycle, tier, freshness, and gate counts.

## Design

| Mode | Access | Data |
| --- | --- | --- |
| Local operator | Existing native portal on loopback | Full registered Work view, evidence drill-down, and the owner inbox when enabled |
| Local viewer | Optional Node.js process on its own loopback port | The same limited view used in remote mode, suitable for testing before sharing |
| Private remote | Tailscale Serve targeting the viewer port, restricted by an explicit tailnet grant to the owner's devices or named readers | Approved viewer projection |
| External guest | Time-limited ngrok HTTPS endpoint targeting the viewer port, with OAuth or OIDC and an explicit named identity allowlist | Approved demo data by default. Real summaries require explicit owner approval for that audience and provider |

The native portal already owns workflow discovery, status, and the gate inventory. Its HTML can contain an inbox token, and the local GET routes can expose files, conversations, search results, and run reports. Give the viewer its own page and a dedicated versioned JSON projection with an explicit field allowlist and opt-in project list. Use opaque public IDs and strip native detail links, raw paths, arbitrary messages, and any other fields outside the contract. The viewer reads only this projection from a fixed loopback source and serves its own routes. There is no catch-all proxy. The preview port, workflow inbox, inference API, and full operator view stay on the host.

Use the installed Node.js 24 LTS line for a dependency-free prototype with `node:http` and `node:test`. Keep the existing browser approach for the first slice. Node is an optional delivery process, so the native operator portal and workflow executables can still start on their own. Compare the prototype's polling cost and operational burden before moving any other portal code. The viewer should cache one bounded upstream snapshot across clients, show when it was last refreshed, and present a labeled stale view when the upstream goes offline.

## Receiving order

1. Use synthetic projects to prove the local viewer can show a running job, staged work, archived evidence, and a stale snapshot. Browser-test filtering, refresh, and stable selection. Verify that HTTP responses contain only approved fields and that write methods, file and report routes, previews, and unknown routes are rejected.
2. After the owner approves which real data may leave the host and who may see it, try Tailscale Serve from a second device. Restrict the grant to the intended audience. Prove access for an allowed identity and denial for another identity or device. Check that the workflow operator view and inbox remain reachable locally. Record the exact listener, grant, verification, and shutdown steps without secrets. Use Serve for this private view.
3. Try ngrok only for a defined audience that cannot use the tailnet. Configure the identity allowlist before enabling the endpoint. Verify denial when signed out and when signed in as an unlisted user, then check the allowed session and remove the endpoint. Keep account tokens, client credentials, named identities, and endpoint details in local operator configuration.
4. Measure native portal responsiveness while several viewer clients poll. Check that upstream refresh is shared, transitions and outages remain visible, and native detail still opens locally. Decide from that evidence whether the viewer justifies another process or whether a native projection is sufficient for the long term.

## Environment and scope

The first slice uses local fixtures and needs no inference workload. Node.js is already available on the Windows work host. Provider setup, any installation, and outbound sharing of real portal data require the owner's agreement under the current [standing rules](../STATUS.md#6-standing-rules). Per-project permissions for multiple remote audiences and remote workflow control are outside this slice.

Provider references for the access tests. [Node.js release support](https://nodejs.org/en/about/previous-releases) · [Tailscale Serve](https://tailscale.com/docs/features/tailscale-serve) · [ngrok OAuth protection](https://ngrok.com/docs/gateway/traffic-policy/examples/oauth-protection)
