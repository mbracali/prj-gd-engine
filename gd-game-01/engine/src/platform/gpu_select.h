// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// Picks the dedicated GPU on machines with two (hybrid laptops).
//
// Windows: the exported NvOptimusEnablement / AmdPowerXpressRequestHighPerformance
// (gpu_select.cpp) ask the NVIDIA / AMD drivers for the dedicated GPU; the
// drivers fall back to the default one by themselves.
// Linux: before SDL starts, the PRIME render offload variables (NVIDIA driver)
// or DRI_PRIME=pci-... (Mesa) select it. If graphics then fail to start, or
// the previous launch died while starting them, the game uses the GPU the
// system picks (and remembers that, in gpu_system in the save folder, until
// the driver or kernel changes). GD01_GPU=system skips the preference; GD01_GPU=testfail
// fakes a dedicated GPU that cannot start.
// macOS and Android: nothing to choose.
namespace gpu {

// Keeps argv for a restart on the system's GPU. Call first thing in main.
void setArgs(int argc, char** argv);

// Before SDL_Init.
void preferDedicated();

// After the window and the GL context: ok = graphics started. When the
// dedicated GPU was asked for and they did not start (or landed on a software
// renderer), restarts the game on the system's GPU; returns only if it could not.
void started(bool ok, const std::string& glRenderer);

}  // namespace gpu
