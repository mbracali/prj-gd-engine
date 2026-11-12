// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Which graphics backend this run uses (render/core_backend.h).
#include "render/core_backend.h"

#include <gd/core/render/device.h>

namespace gfx {
namespace {

namespace core = gd::core::render;

Backend g_renderer = Backend::OpenGL3;  // the development tools' OpenGL windows
bool g_hdrRequest = false;

constexpr Backend kOrder[] = {Backend::Vulkan, Backend::Sdl3, Backend::OpenGL3};
static_assert(kOrder[0] == kDefaultBackend, "the default is tried first");

bool parse(const std::string& id, Backend& out) {
    for (Backend b : kOrder)
        if (id == backendId(b)) return out = b, true;
    return false;
}

core::Backend coreBackend(Backend b) {
    return b == Backend::Sdl3 ? core::Backend::SdlGpu : b == Backend::Vulkan ? core::Backend::Vulkan : core::Backend::OpenGL3;
}

}  // namespace

const char* backendId(Backend b) {
    switch (b) {
    case Backend::Vulkan: return "vulkan";
    case Backend::Sdl3: return "sdl3";
    case Backend::OpenGL3: return "opengl3";
    }
    return "vulkan";
}

bool backendAvailable(Backend b) { return core::backendCompiled(coreBackend(b)); }

std::vector<Backend> backendsToTry(const std::string& saved) {
    const char* env = SDL_getenv("GD05_GRAPHICS_BACKEND");  // development aid
    const std::string id = env && *env ? std::string(env) : saved;
    Backend first = kDefaultBackend;
    if (!id.empty() && !parse(id, first)) {
        SDL_Log("graphics backend: unknown '%s'; using %s", id.c_str(), backendId(kDefaultBackend));
        first = kDefaultBackend;
    }
    std::vector<Backend> out;
    if (backendAvailable(first)) out.push_back(first);
    else SDL_Log("graphics backend: %s: not in this build", backendId(first));
    for (Backend b : kOrder)
        if (b != first && backendAvailable(b)) out.push_back(b);
    return out;
}

SDL_WindowFlags prepareWindowFor(Backend b) {
#ifdef __ANDROID__
    const bool gles = true;
#else
    const bool gles = SDL_getenv("GD05_GLES") != nullptr;  // development aid: the mobile (OpenGL ES 3) path
#endif
    // OpenGL3: 3.3 core (forward compatible) or ES 3.0, a 24-bit depth buffer.
    core::prepareWindow(coreBackend(b), gles);
    return SDL_WindowFlags(core::windowFlags(coreBackend(b)));
}

void setRendererBackend(Backend b) { g_renderer = b; }
Backend rendererBackend() { return g_renderer; }

void setHdrRequest(bool on) { g_hdrRequest = on; }
bool hdrRequested() { return g_hdrRequest; }

}  // namespace gfx
