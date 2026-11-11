// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/hdr_output.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace hdr {
namespace {

bool requested = false;
Surface eglSurface = Surface::Sdr;  // what the EGL surface callback asked for (Wayland)
std::string eglReason;

#if defined(__linux__) && !defined(__ANDROID__)
// EGL tokens (EGL_KHR_gl_colorspace, EGL_EXT_pixel_format_float, EGL_EXT_gl_colorspace_*,
// EGL_EXT_surface_SMPTE2086_metadata).
constexpr int kEglNone = 0x3038, kEglExtensions = 0x3055, kEglColorspace = 0x309D, kEglScRgbLinear = 0x3350,
              kEglBt2020Pq = 0x3340, kEglComponentType = 0x3339, kEglComponentFloat = 0x333B;
constexpr int kEglRedX = 0x3341, kEglRedY = 0x3342, kEglGreenX = 0x3343, kEglGreenY = 0x3344, kEglBlueX = 0x3345,
              kEglBlueY = 0x3346, kEglWhiteX = 0x3347, kEglWhiteY = 0x3348, kEglMaxLum = 0x3349, kEglMinLum = 0x334A;
constexpr int kEglScale = 50000;  // EGL_METADATA_SCALING_EXT

bool hasExtension(const char* list, const char* name) {
    if (!list) return false;
    const size_t n = std::strlen(name);
    for (const char* p = list; (p = std::strstr(p, name)); p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
    return false;
}

// SDL appends these to eglCreateWindowSurface's attributes.
SDL_EGLint* SDLCALL surfaceAttribs(void*, SDL_EGLDisplay display, SDL_EGLConfig config) {
    using QueryString = const char* (*)(void*, int);
    using GetConfigAttrib = unsigned (*)(void*, void*, int, int*);
    auto queryString = reinterpret_cast<QueryString>(SDL_EGL_GetProcAddress("eglQueryString"));
    auto getConfigAttrib = reinterpret_cast<GetConfigAttrib>(SDL_EGL_GetProcAddress("eglGetConfigAttrib"));
    const char* ext = queryString ? queryString(display, kEglExtensions) : nullptr;
    int type = 0;
    const bool isFloat = getConfigAttrib && getConfigAttrib(display, config, kEglComponentType, &type) && type == kEglComponentFloat;
    auto* a = static_cast<SDL_EGLint*>(SDL_malloc(sizeof(SDL_EGLint) * 32));
    if (!a) return nullptr;
    int n = 0;
    eglSurface = Surface::Sdr;
    if (isFloat && hasExtension(ext, "EGL_EXT_gl_colorspace_scrgb_linear")) {
        a[n++] = kEglColorspace, a[n++] = kEglScRgbLinear;
        eglSurface = Surface::ScRgb;
    } else if (hasExtension(ext, "EGL_EXT_gl_colorspace_bt2020_pq")) {
        a[n++] = kEglColorspace, a[n++] = kEglBt2020Pq;
        eglSurface = Surface::Pq;
        if (hasExtension(ext, "EGL_EXT_surface_SMPTE2086_metadata")) {
            // The image's mastering display: BT.709 primaries (what the game draws), D65, 1000 nits.
            const int m[] = {kEglRedX, 32000, kEglRedY, 16500, kEglGreenX, 15000, kEglGreenY, 30000, kEglBlueX, 7500,
                             kEglBlueY, 3000, kEglWhiteX, 15635, kEglWhiteY, 16450, kEglMaxLum, 1000 * kEglScale, kEglMinLum, 5};
            for (int v : m) a[n++] = v;
        }
    } else {
        eglReason = isFloat ? "the display server's EGL has no scRGB or PQ colour space"
                            : "the graphics driver offers no floating-point or PQ window surface";
    }
    a[n] = kEglNone;
    SDL_Log("hdr: EGL %s config, asking for %s", isFloat ? "RGBA16F" : "8 / 10-bit",
            eglSurface == Surface::ScRgb ? "scRGB linear" : eglSurface == Surface::Pq ? "BT.2020 PQ" : "no HDR colour space");
    return a;
}
#endif

}  // namespace

std::string unsupportedReason() {
#if defined(__ANDROID__)
    return "not available on Android";
#elif defined(__APPLE__)
    return "not available on macOS with OpenGL";
#elif defined(_WIN32)
    return "";
#else
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver || std::strcmp(driver, "wayland") != 0) return "needs a Wayland desktop session (X11 has no HDR)";
    return "";
#endif
}

bool request() {
    if (!unsupportedReason().empty()) return false;
    requested = true;
    SDL_GL_SetAttribute(SDL_GL_FLOATBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 16);
#if defined(__linux__) && !defined(__ANDROID__)
    SDL_EGL_SetAttributeCallbacks(nullptr, surfaceAttribs, nullptr, nullptr);
#endif
    return true;
}

void cancel() {
    if (!requested) return;
    requested = false;
    eglSurface = Surface::Sdr;
    SDL_GL_SetAttribute(SDL_GL_FLOATBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#if defined(__linux__) && !defined(__ANDROID__)
    SDL_EGL_SetAttributeCallbacks(nullptr, nullptr, nullptr, nullptr);
#endif
}

std::string failure(const std::string& what, const char* sdlError) {
    if (!eglReason.empty()) return eglReason;
    return what + ": " + (sdlError ? sdlError : "");
}

Surface obtained(std::string& reason) {
    if (!requested) return Surface::Sdr;
    // The default framebuffer's component type: floating point, or not.
    using GetAttachment = void (*)(unsigned, unsigned, unsigned, int*);
    using GetError = unsigned (*)();
    auto getAttachment = reinterpret_cast<GetAttachment>(SDL_GL_GetProcAddress("glGetFramebufferAttachmentParameteriv"));
    auto getError = reinterpret_cast<GetError>(SDL_GL_GetProcAddress("glGetError"));
    int type = 0;
    if (getAttachment && getError) {
        while (getError()) {}
        getAttachment(0x8D40 /* GL_FRAMEBUFFER */, 0x0402 /* GL_BACK_LEFT */, 0x8211 /* COMPONENT_TYPE */, &type);
        if (getError()) type = 0;
    }
    const bool isFloat = type == 0x1406;  // GL_FLOAT
#if defined(_WIN32)
    if (isFloat) return Surface::ScRgb;
    reason = "the graphics driver offers no floating-point window";
    return Surface::Sdr;
#elif defined(__linux__) && !defined(__ANDROID__)
    if (eglSurface == Surface::ScRgb && isFloat) return Surface::ScRgb;
    if (eglSurface == Surface::Pq) return Surface::Pq;
    reason = eglReason.empty() ? "the graphics driver offers no floating-point window" : eglReason;
    return Surface::Sdr;
#else
    (void)isFloat;
    reason = unsupportedReason();
    return Surface::Sdr;
#endif
}

}  // namespace hdr
