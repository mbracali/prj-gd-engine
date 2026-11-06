// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The backend factory and the format helpers of gd::core::render.
#include "gd/core/render/device.h"

#include <SDL3/SDL.h>

#include <cstring>

#if GDCORE_RENDER_OPENGL3
#include "gl/gl_device.h"
#endif

namespace gd::core::render {

const char* backendId(Backend b) {
    switch (b) {
    case Backend::OpenGL3: return "opengl3";
    case Backend::SdlGpu: return "sdl3";
    case Backend::Vulkan: return "vulkan";
    }
    return "";
}

bool parseBackend(const char* id, Backend& out) {
    if (!id) return false;
    for (Backend b : {Backend::OpenGL3, Backend::SdlGpu, Backend::Vulkan})
        if (std::strcmp(id, backendId(b)) == 0) return out = b, true;
    return false;
}

bool backendCompiled(Backend b) {
    switch (b) {
    case Backend::OpenGL3: return GDCORE_RENDER_OPENGL3 != 0;
    case Backend::SdlGpu: return false;  // phase 2
    case Backend::Vulkan: return false;  // phase 3
    }
    return false;
}

bool SamplerDesc::operator==(const SamplerDesc& o) const {
    return minFilter == o.minFilter && magFilter == o.magFilter && mipFilter == o.mipFilter && wrapU == o.wrapU && wrapV == o.wrapV &&
           wrapW == o.wrapW && lodBias == o.lodBias && minLod == o.minLod && maxLod == o.maxLod && maxAnisotropy == o.maxAnisotropy &&
           compare == o.compare && compareOp == o.compareOp && border == o.border;
}

bool isDepthFormat(Format f) { return f >= Format::D16 && f <= Format::D32FS8; }
bool hasStencil(Format f) { return f == Format::D24S8 || f == Format::D32FS8; }
bool isCompressedFormat(Format f) { return f >= Format::BC1 && f <= Format::BC7_SRGB; }

size_t formatBytes(Format f) {
    switch (f) {
    case Format::R8: return 1;
    case Format::RG8: case Format::R16F: case Format::D16: return 2;
    case Format::RGBA8: case Format::RGBA8_SRGB: case Format::BGRA8: case Format::RGB10A2: case Format::RG11B10F:
    case Format::RG16F: case Format::R32F: case Format::D24: case Format::D24S8: case Format::D32F: return 4;
    case Format::RGBA16F: case Format::D32FS8: return 8;
    case Format::RGBA32F: return 16;
    case Format::BC1: case Format::BC1_SRGB: case Format::BC4: return 8;  // per 4x4 block
    case Format::BC2: case Format::BC2_SRGB: case Format::BC3: case Format::BC3_SRGB: case Format::BC5: case Format::BC7:
    case Format::BC7_SRGB: return 16;
    default: return 0;
    }
}

size_t imageBytes(Format f, int w, int h) {
    if (isCompressedFormat(f)) return size_t((w + 3) / 4) * size_t((h + 3) / 4) * formatBytes(f);
    return size_t(w) * size_t(h) * formatBytes(f);
}

uint64_t windowFlags(Backend b) {
    switch (b) {
    case Backend::OpenGL3: return SDL_WINDOW_OPENGL;
    case Backend::Vulkan: return SDL_WINDOW_VULKAN;
    case Backend::SdlGpu: return 0;  // SDL_ClaimWindowForGPUDevice sets the window up
    }
    return 0;
}

void prepareWindow(Backend b, bool gles) {
    if (b != Backend::OpenGL3) return;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gles ? 0 : 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, gles ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
    if (!gles) SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // macOS requires it
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}

std::unique_ptr<Device> createDevice(const DeviceCreateInfo& info, std::string& error) {
    if (!info.window) {
        error = "no window";
        return nullptr;
    }
    switch (info.backend) {
#if GDCORE_RENDER_OPENGL3
    case Backend::OpenGL3: return createGlDevice(info, error);
#endif
    default: break;
    }
    error = std::string("the ") + backendId(info.backend) + " backend is not in this build";
    return nullptr;
}

}  // namespace gd::core::render
