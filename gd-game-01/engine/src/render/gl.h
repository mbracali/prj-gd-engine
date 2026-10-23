// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// OpenGL 3.3 core entry points, loaded at runtime through SDL so no GL
// library or loader generator is needed at build time.

#include <SDL3/SDL_opengl.h>

#define GD01_GL_FUNCTIONS(X)                                                         \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                        \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                          \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                              \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                    \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                              \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                        \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                        \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                          \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                        \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                        \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                          \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                              \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                    \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                              \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                                      \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                    \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                          \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                  \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                            \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                              \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                            \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                          \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                                              \
    X(PFNGLUNIFORM2FVPROC, glUniform2fv)                                              \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv)                                  \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                                                \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                              \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                  \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                              \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                            \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                                \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                                        \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                                \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                                \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                    \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                    \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                          \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                  \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                  \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                            \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                    \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                      \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                                    \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                              \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                            \
    X(PFNGLMAPBUFFERRANGEPROC, glMapBufferRange)                                      \
    X(PFNGLUNMAPBUFFERPROC, glUnmapBuffer)

// GL 1.1 functions: their prototypes come from SDL_opengl.h; only the types
// are used (nothing is linked), and the pointers are still fetched through SDL.
#define GD01_GL11_FUNCTIONS(X)                                                        \
    X(decltype(&::glEnable), glEnable)                                                \
    X(decltype(&::glDisable), glDisable)                                              \
    X(decltype(&::glBlendFunc), glBlendFunc)                                          \
    X(decltype(&::glDepthFunc), glDepthFunc)                                          \
    X(decltype(&::glDepthMask), glDepthMask)                                          \
    X(decltype(&::glCullFace), glCullFace)                                            \
    X(decltype(&::glViewport), glViewport)                                            \
    X(decltype(&::glScissor), glScissor)                                              \
    X(decltype(&::glClearColor), glClearColor)                                        \
    X(decltype(&::glClear), glClear)                                                  \
    X(decltype(&::glDrawArrays), glDrawArrays)                                        \
    X(decltype(&::glDrawElements), glDrawElements)                                    \
    X(decltype(&::glGenTextures), glGenTextures)                                      \
    X(decltype(&::glDeleteTextures), glDeleteTextures)                                \
    X(decltype(&::glBindTexture), glBindTexture)                                      \
    X(decltype(&::glTexImage2D), glTexImage2D)                                        \
    X(decltype(&::glTexParameteri), glTexParameteri)                                  \
    X(decltype(&::glPixelStorei), glPixelStorei)                                      \
    X(decltype(&::glReadPixels), glReadPixels)                                        \
    X(decltype(&::glGetError), glGetError)                                            \
    X(decltype(&::glPolygonOffset), glPolygonOffset)                                  \
    X(decltype(&::glColorMask), glColorMask)                                          \
    X(decltype(&::glCopyTexSubImage2D), glCopyTexSubImage2D)                          \
    X(PFNGLFENCESYNCPROC, glFenceSync)                                                \
    X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync)                                      \
    X(PFNGLDELETESYNCPROC, glDeleteSync)

namespace gl {
#define GD01_DECLARE(type, name) extern type name;
GD01_GL_FUNCTIONS(GD01_DECLARE)
GD01_GL11_FUNCTIONS(GD01_DECLARE)
#undef GD01_DECLARE

// Loads every entry point; returns the name of the first missing one, or nullptr.
// On an OpenGL ES 3 context (Android, or GD01_GLES=1 on desktop) a few entry
// points are wrapped so the desktop code runs unchanged: shaders get the
// "#version 300 es" preamble, float render targets fall back to RGBA8 without
// EXT_color_buffer_(half_)float, and depth reads (no ES equivalent) return 1.
const char* load();
// Whether the current context is OpenGL ES.
bool isEs();
}  // namespace gl
