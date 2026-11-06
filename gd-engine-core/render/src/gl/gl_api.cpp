// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "gl/gl_api.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace gd::core::render::glapi {
#define GDCORE_GL_DEFINE(type, name) type name = nullptr;
GDCORE_GL_FUNCTIONS(GDCORE_GL_DEFINE)
GDCORE_GL1_FUNCTIONS(GDCORE_GL_DEFINE)
#undef GDCORE_GL_DEFINE
void(APIENTRY* glDepthRangeD)(double, double) = nullptr;
void(APIENTRY* glDepthRangeF)(float, float) = nullptr;

const char* load() {
#define GDCORE_GL_LOAD(type, name)                                    \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));       \
    if (!name) return #name;
    GDCORE_GL_FUNCTIONS(GDCORE_GL_LOAD)
    GDCORE_GL1_FUNCTIONS(GDCORE_GL_LOAD)
#undef GDCORE_GL_LOAD
    glDepthRangeF = reinterpret_cast<void(APIENTRY*)(float, float)>(SDL_GL_GetProcAddress("glDepthRangef"));
    glDepthRangeD = reinterpret_cast<void(APIENTRY*)(double, double)>(SDL_GL_GetProcAddress("glDepthRange"));
    if (!glDepthRangeF && !glDepthRangeD) return "glDepthRange";
    return nullptr;
}

bool hasExtension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i)
        if (const GLubyte* e = glGetStringi(GL_EXTENSIONS, GLuint(i)); e && std::strcmp(reinterpret_cast<const char*>(e), name) == 0)
            return true;
    return false;
}

}  // namespace gd::core::render::glapi
