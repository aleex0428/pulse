// SPDX-License-Identifier: MIT
// The host tests' stand-in for the core module (build\swofty_core.dll): the exports the SDK headers look for. The
// preview needs the overlay version; the pause reasons and the postfx call answer "nothing" here.
#include <cstdint>

extern "C" __declspec(dllexport) uint32_t SwoftyCoreOverlayVersion(void) { return 3; }
extern "C" __declspec(dllexport) uint32_t SwoftyCorePauseReasons(void) { return 0; }
