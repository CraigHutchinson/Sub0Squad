#pragma once
/// @file canvas.hpp
/// Forwarding header: this part of the canvas tooling lives in the sub0canvas library (`sub0canvas/canvas.hpp`, github.com/CraigHutchinson/sub0canvas),
/// pinned in cmake/dependencies.cmake. Its names are visible here as `sub0squad::` so callers need not change; new code may use `sub0canvas::`.
#include "sub0canvas/canvas.hpp"

namespace sub0squad { using namespace sub0canvas; }
