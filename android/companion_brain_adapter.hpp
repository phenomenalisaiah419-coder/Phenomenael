#pragma once

#include <string>

namespace bedrockai::companion {

// Stage 1 only: verify that the v0.35 ControlHub loads and defaults to no automation
// when the server identity and game observations are unavailable.
std::string run_native_safety_self_check();

} // namespace bedrockai::companion
