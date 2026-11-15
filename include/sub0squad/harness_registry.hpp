#pragma once
/// @file harness_registry.hpp
/// The built-in harnesses (web-static, cpp-cmake, py-unittest) as one registration call. The registry lives in the kernel (harness.hpp) and starts
/// empty; the composition root and each test that needs harnesses call this once at start-up.

namespace sub0squad {

/// Register every built-in harness with the registry of harness.hpp. Safe to call more than once; call it before any thread is started.
void registerBuiltinHarnesses();

}  // namespace sub0squad
