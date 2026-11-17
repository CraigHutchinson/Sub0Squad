#pragma once
/// @file portal.hpp
/// A read-only status portal: a loopback-only HTTP server (127.0.0.1, GET only) with one page and one JSON endpoint.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sub0squad {

/// Serve until killed. `GET /` is the page, `GET /api/state` returns `state()`, `GET /api/live?n=N` returns the sampled system and engine
/// series (CPU, memory, swap, GPU, and every loopback llama-server found in the workflow engine profiles or in `extraEngines`, sampled at
/// 1 Hz from its `/slots`), anything else is 404. Returns a process exit code.
/// The portal is read only except for two POST routes, `/api/raise` and `/api/answer` (the owner's inbox: requirements, clarifications, answers).
/// They require the per-run token embedded in the page (`X-Portal-Token`) and a same-origin `Origin`, and call `act(command, workflow, specJson)`,
/// which returns the daemon's JSON reply. A page the models wrote is on another origin and has neither the token nor a way to read it.
/// Generated sites are served from the next port, `port + 1`. `port` 0 lets the system pick a free pair (parallel instances never collide);
/// `portFile`, when given, receives the portal's port once both are held, which is how a caller learns the port the system picked.
using PortalAct = std::function<std::string(const std::string& command, const std::string& workflow, const std::string& specJson)>;
[[nodiscard]] int servePortal(int port, const std::function<std::string()>& state, const std::vector<std::string>& extraEngines = {}, const PortalAct& act = {},
                              const std::filesystem::path& portFile = {});

/// The portal page (HTML, CSS and JS in one string; no external resources).
[[nodiscard]] const char* portalPage();

}  // namespace sub0squad
