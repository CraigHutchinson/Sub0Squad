# Working in Sub0Squad

Instructions for agents and people continuing this project. Everything needed is in this repository; nothing depends on notes kept outside it.

1. **The owner's standing rules: [docs/STATUS.md](docs/STATUS.md) section 6.** Read them first; they are the complete list (style, commits and
   pushes, clean-up, CI, briefs, the shared engine, what to stop and ask about).
2. **Where the work stands:** [docs/RESUME.md](docs/RESUME.md), then [docs/BACKLOG.md](docs/BACKLOG.md).
3. **A task with its own brief:** [docs/sessions/](docs/sessions/README.md). Start a session with "Read `docs/sessions/<file>.md` and do it."
4. **Building and testing:** [docs/DEV_SETUP.md](docs/DEV_SETUP.md) (toolchains, presets, test tiers and labels, the pre-push gate, CI).
   **The code's layers:** [docs/COMPONENTS.md](docs/COMPONENTS.md).
5. **The canvas library** (outline, edits, canvas, stable block ids and layers) lives in
   [Sub0Canvas](https://github.com/CraigHutchinson/Sub0Canvas), pinned in `cmake/dependencies.cmake`. Its own `AGENTS.md` covers working there.
