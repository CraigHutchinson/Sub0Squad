# Handoff docs: briefs for follow-up sessions

A handoff doc is a self-contained brief for one piece of work that a fresh session (or a person) picks up without the
conversation that produced it. Start a session with: "Read `docs/sessions/<file>.md` and do it." The project-wide
state stays in [../RESUME.md](../RESUME.md) and [../BACKLOG.md](../BACKLOG.md); a brief is for one task.

## The workflow

1. **Write** one when work is found that belongs in its own session (a separate investigation, change or repository),
   instead of leaving it in chat. Copy the template below; name the file after the task (`kebab-case.md`); add it to
   *Open* below, and point the backlog item at it.
2. **Pick up**: the session reads the brief, sets `Status: in progress (<date>, <who or session name>)`, and works from
   it. It asks the owner where the brief is unclear rather than guessing.
3. **Close**: the result goes where it belongs (code, the backlog, RESUME, EXPERIMENTS). The brief gets an
   `## Outcome` section (what was found or done, the commit, what is left), moves from *Open* to *Done* below, and is
   then retired to git in a later tidy-up (as [../ARCHIVE.md](../ARCHIVE.md) does for retired documents).

A brief never holds the evidence itself: it links to the doc that does, so the evidence has one home.

## Template

```markdown
# Follow-up session: <task>

Status: open (written <date>). Backlog: <link to the backlog item>. Evidence: <links>.

## Goal
<one paragraph: what to build or establish, and what not to do>

## What is known
<the facts the session starts from, briefly, with links>

## Steps
1. ...

## Done means
<the observable end state: which test passes, which doc says what, which run shows it>

## Environment and constraints
<machine, paths, engine, models; the owner's standing rules: ask before downloads or installs, nothing leaves the
machine (frontier off), no commits without the owner's go-ahead, ask before changes to Sub0Pipeline or Sub0Pub>
```

## Open

- [Canvas reasoning workflow](canvas-reasoning-workflow.md) — measure structured thinking and design feedback from scratch checks.

- [Portal access and a Node.js viewer](portal-access.md) — receive a local summary viewer, then verify private remote access from a second device.

| Brief | Task | Written |
|---|---|---|
| [engine-registry-integration.md](engine-registry-integration.md) | Run the squad on Sub0LlmEngine and Sub0LlmModelRegistry instead of its own `llama-server` line | 2026-11-16 |
| [platform-unification.md](platform-unification.md) | Sub0Platform (github.com/CraigHutchinson/Sub0Platform): Python package live; C++ move and remaining decisions open | 2026-11-17 |

Elsewhere: Sub0LlmEngine's `docs/sessions/` has the brief for isolating the Qwen3.8-27B UD-IQ3_XXS CUDA fault.
Sub0Canvas's `docs/sessions/` has the canvas briefs (S0 scale harness, B0 format catch-up, G1 git history); work that reaches this repository arrives as a pull request bumping its pin.

## Done

| Brief | Task | Closed |
|---|---|---|
| [portal-work-overview.md](portal-work-overview.md) | Cross-project work overview and evidence sources, received on Windows | 2026-11-18 |
| [layered-document.md](layered-document.md) | The layered document: slice 2 of the issues layer (built, measured, adaptive by default); the backbone design (in Sub0Canvas, where it continues) | 2026-11-18 |
