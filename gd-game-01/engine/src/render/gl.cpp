// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/gl.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <string>
#include <vector>

namespace gl {
#define GD01_DEFINE(type, name) type name = nullptr;
GD01_GL_FUNCTIONS(GD01_DEFINE)
GD01_GL11_FUNCTIONS(GD01_DEFINE)
#undef GD01_DEFINE

namespace {

bool es = false, floatTargets = false;
PFNGLSHADERSOURCEPROC realShaderSource = nullptr;
decltype(&::glTexImage2D) realTexImage2D = nullptr;
decltype(&::glReadPixels) realReadPixels = nullptr;
PFNGLBUFFERSUBDATAPROC bufferSubData = nullptr;
decltype(&::glGetIntegerv) getIntegerv = nullptr;

// GLSL 3.30 core -> GLSL ES 3.00: the shaders keep to the common subset, so
// the version line and default precisions are all that differ.
void APIENTRY esShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    std::string src;
    for (GLsizei i = 0; i < count; ++i)
        src.append(strings[i], lengths && lengths[i] >= 0 ? size_t(lengths[i]) : std::strlen(strings[i]));
    const std::string desktop = "#version 330 core";
    size_t at = src.find(desktop);
    if (at != std::string::npos)
        src.replace(at, desktop.size(),
                    "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;");
    const GLchar* p = src.c_str();
    realShaderSource(shader, 1, &p, nullptr);
}

void APIENTRY esTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format,
                           GLenum type, const void* pixels) {
    if (!pixels && !floatTargets && (internal == GL_RGBA16F || internal == GL_RG16F || internal == GL_R32F))
        internal = GL_RGBA8, format = GL_RGBA, type = GL_UNSIGNED_BYTE;
    realTexImage2D(target, level, internal, w, h, border, format, type, pixels);
}

void APIENTRY esReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data) {
    if (format != GL_DEPTH_COMPONENT) return realReadPixels(x, y, w, h, format, type, data);
    // ES cannot read the depth buffer: report "far" (lens flares stay visible).
    std::vector<float> farDepth(size_t(w) * size_t(h), 1.0f);
    GLint pbo = 0;
    getIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pbo);
    if (pbo)
        bufferSubData(GL_PIXEL_PACK_BUFFER, reinterpret_cast<GLintptr>(data), GLsizeiptr(farDepth.size() * sizeof(float)), farDepth.data());
    else if (data)
        std::memcpy(data, farDepth.data(), farDepth.size() * sizeof(float));
}

bool hasExtension(const char* name) {
    using GetStringi = const GLubyte*(APIENTRY*)(GLenum, GLuint);
    auto getStringi = reinterpret_cast<GetStringi>(SDL_GL_GetProcAddress("glGetStringi"));
    GLint n = 0;
    getIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; getStringi && i < n; ++i)
        if (const GLubyte* e = getStringi(GL_EXTENSIONS, GLuint(i)); e && std::strcmp(reinterpret_cast<const char*>(e), name) == 0)
            return true;
    return false;
}

}  // namespace

bool isEs() { return es; }

const char* load() {
#define GD01_LOAD(type, name)                                                   \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));                 \
    if (!name) return #name;
    GD01_GL_FUNCTIONS(GD01_LOAD)
    GD01_GL11_FUNCTIONS(GD01_LOAD)
#undef GD01_LOAD
    using GetString = const GLubyte*(APIENTRY*)(GLenum);
    auto getString = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
    const char* version = getString ? reinterpret_cast<const char*>(getString(GL_VERSION)) : nullptr;
    es = version && std::strncmp(version, "OpenGL ES", 9) == 0;
    if (es) {
        getIntegerv = reinterpret_cast<decltype(getIntegerv)>(SDL_GL_GetProcAddress("glGetIntegerv"));
        bufferSubData = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(SDL_GL_GetProcAddress("glBufferSubData"));
        if (!getIntegerv) return "glGetIntegerv";
        if (!bufferSubData) return "glBufferSubData";
        floatTargets = hasExtension("GL_EXT_color_buffer_float") || hasExtension("GL_EXT_color_buffer_half_float");
        realShaderSource = glShaderSource, glShaderSource = esShaderSource;
        realTexImage2D = glTexImage2D, glTexImage2D = esTexImage2D;
        realReadPixels = glReadPixels, glReadPixels = esReadPixels;
    }
    return nullptr;
}
}  // namespace gl
