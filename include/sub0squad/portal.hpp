#pragma once
/// @file portal.hpp
/// A read-only status portal: a loopback-only HTTP server (127.0.0.1, GET only) with one page and one JSON endpoint.

#include <functional>
#include <string>
#include <vector>

namespace sub0squad {

/// Serve until killed. `GET /` is the page, `GET /api/state` returns `state()`, `GET /api/live?n=N` returns the sampled system and engine
/// series (CPU, memory, swap, GPU, and every loopback llama-server found in the workflow engine profiles or in `extraEngines`, sampled at
/// 1 Hz from its `/slots`), anything else is 404. Returns a process exit code.
[[nodiscard]] int servePortal(int port, const std::function<std::string()>& state, const std::vector<std::string>& extraEngines = {});

/// The portal page (HTML, CSS and JS in one string; no external resources).
[[nodiscard]] const char* portalPage();

}  // namespace sub0squad
