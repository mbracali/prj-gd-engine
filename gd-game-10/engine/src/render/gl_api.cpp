// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/gl_api.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace gl {
#define GD10_GL_DEFINE(type, name) type name = nullptr;
GD10_GL_FUNCTIONS(GD10_GL_DEFINE)
GD10_GL1_FUNCTIONS(GD10_GL_DEFINE)
#undef GD10_GL_DEFINE

const char* load() {
#define GD10_GL_LOAD(type, name)                                    \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));     \
    if (!name) return #name;
    GD10_GL_FUNCTIONS(GD10_GL_LOAD)
    GD10_GL1_FUNCTIONS(GD10_GL_LOAD)
#undef GD10_GL_LOAD
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

}  // namespace gl
