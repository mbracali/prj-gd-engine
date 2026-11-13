// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

#include "render/device.h"

// The graphics backend (Enhanced > Performance > Graphics Backend, GDCORE-008):
// "local" is gd10's own OpenGL device (render/gl_device.cpp), untouched;
// "opengl3", "sdl3" and "vulkan" are gd-engine-core's backends
// (gd-engine-core/docs/render.md) under the same gfx::RenderDevice
// (render/core_device.cpp), so nothing above the device changes.
namespace gfx {

enum class Backend : uint8_t { Local, OpenGL3, Sdl3, Vulkan };

// The setting id: "local", "opengl3", "sdl3", "vulkan".
const char* backendId(Backend b);
// The backend for this start of the game: the development aid
// GD10_GRAPHICS_BACKEND=<id>, else `saved` (Enhanced > Graphics Backend as
// App::run latched it, startupGraphicsBackend), else local. A backend this
// build does not have is local (and logged). Not cached: Restart Now runs
// App::run again in the same process with the new setting.
Backend chosenBackend(const std::string& saved);

// The window for a backend: call before SDL_CreateWindow and pass the result
// in its flags (local and opengl3: gfx::prepareWindow()'s OpenGL window; sdl3
// and vulkan: the core's prepareWindow, which in a launcher module also asks
// the launcher for a window made for that API).
SDL_WindowFlags prepareWindowFor(Backend b);

// The backend Renderer::init starts the device with (App sets it once it has
// made the window for it; default local, also for every development tool).
void setRendererBackend(Backend b);
Backend rendererBackend();

// A core device on `window` (made with prepareWindowFor(b)'s flags), or
// nullptr with `error` set: the caller falls back to local (rule 2).
std::unique_ptr<RenderDevice> createCoreDevice(Backend b, SDL_Window* window, std::string& error);

}  // namespace gfx
