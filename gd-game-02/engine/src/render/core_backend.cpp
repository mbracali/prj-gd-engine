// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Which graphics backend this run uses (render/core_backend.h).
#include "render/core_backend.h"

#include <gd/core/render/device.h>

#include <cstring>
#include <exception>
#include <fstream>
#include <sstream>

#include "data/json.h"
#include "platform/paths.h"

namespace gfx {
namespace {

Backend g_renderer = Backend::Local;

bool parse(const std::string& id, Backend& out) {
    if (id == "local") return out = Backend::Local, true;
    if (id == "opengl3") return out = Backend::OpenGL3, true;
    if (id == "sdl3") return out = Backend::Sdl3, true;
    if (id == "vulkan") return out = Backend::Vulkan, true;
    return false;
}

namespace core = gd::core::render;

core::Backend coreBackend(Backend b) {
    return b == Backend::Sdl3 ? core::Backend::SdlGpu : b == Backend::Vulkan ? core::Backend::Vulkan : core::Backend::OpenGL3;
}

}  // namespace

const char* backendId(Backend b) {
    switch (b) {
    case Backend::Local: return "local";
    case Backend::OpenGL3: return "opengl3";
    case Backend::Sdl3: return "sdl3";
    case Backend::Vulkan: return "vulkan";
    }
    return "local";
}

Backend chosenBackend(const std::string& saved) {
    const Backend chosen = [&] {
        const char* env = SDL_getenv("GD02_GRAPHICS_BACKEND");  // development aid
        const std::string id = env && *env ? std::string(env) : saved.empty() ? std::string("local") : saved;
        Backend b = Backend::Local;
        if (!parse(id, b)) {
            SDL_Log("graphics backend: unknown '%s'; using local", id.c_str());
            return Backend::Local;
        }
        if (b != Backend::Local && !core::backendCompiled(coreBackend(b))) {
            SDL_Log("graphics backend: %s: not available yet; using local", id.c_str());
            return Backend::Local;
        }
        if (b != Backend::Local) SDL_Log("graphics backend: %s (gd-engine-core)", id.c_str());
        return b;
    }();
    return chosen;
}

SDL_WindowFlags prepareWindowFor(Backend b) {
    if (b == Backend::Local || b == Backend::OpenGL3) {
        // gd02's GL attributes (OpenGL 3.3 core / ES 3.0), as always. After an
        // SDL3 GPU / Vulkan window whose device did not start, the launcher's
        // module shim is asked for an OpenGL window again; otherwise nothing
        // is asked (every launcher module before the core: OpenGL).
        const SDL_PropertiesID g = SDL_GetGlobalProperties();
        if (SDL_GetStringProperty(g, core::kWindowApiRequestProperty, nullptr))
            SDL_SetStringProperty(g, core::kWindowApiRequestProperty, core::backendId(core::Backend::OpenGL3));
        return prepareWindow();
    }
    core::prepareWindow(coreBackend(b), false);
    return SDL_WindowFlags(core::windowFlags(coreBackend(b)));
}

void setRendererBackend(Backend b) { g_renderer = b; }
Backend rendererBackend() { return g_renderer; }

}  // namespace gfx
