// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

#include "render/device.h"

// The graphics backend (Enhanced > Performance > Graphics Backend): gd05 draws
// only through gd-engine-core (gd-engine-core/docs/render.md) under its
// gfx::RenderDevice (render/core_device.cpp); Vulkan by default, the others
// in turn when it cannot start (DECISIONS.md D71). Nothing above the device
// knows which one draws.
namespace gfx {

enum class Backend : uint8_t { Vulkan, Sdl3, OpenGL3 };

// The default, and the order the others are tried in when one cannot start.
constexpr Backend kDefaultBackend = Backend::Vulkan;

// The setting id: "vulkan", "sdl3", "opengl3".
const char* backendId(Backend b);
// Whether this build has the backend (no Vulkan without a loader, no OpenGL3
// on macOS: gd-engine-core rule 7).
bool backendAvailable(Backend b);
// The backends to try for this start of the game, best first: the development
// aid GD05_GRAPHICS_BACKEND=<id>, else `saved` (Enhanced > Graphics Backend as
// App::run latched it, startupGraphicsBackend), else the default; then every
// other one this build has, in kDefaultBackend's order (gd-engine-core rule
// 2: never a black screen). Not cached: Restart Now runs App::run again in
// the same process with the new setting.
std::vector<Backend> backendsToTry(const std::string& saved);

// The window for a backend: call before SDL_CreateWindow and pass the result
// in its flags (the core's prepareWindow: in a launcher module it also asks
// the launcher for a window made for that API).
SDL_WindowFlags prepareWindowFor(Backend b);

// The backend Renderer::init starts the device with (App sets it for the
// window it made; the development tools keep the default, OpenGL3, on their
// OpenGL windows).
void setRendererBackend(Backend b);
Backend rendererBackend();

// A core device on `window` (made with prepareWindowFor(b)'s flags), or
// nullptr with `error` set: the caller tries the next backend.
std::unique_ptr<RenderDevice> createCoreDevice(Backend b, SDL_Window* window, std::string& error);

}  // namespace gfx
