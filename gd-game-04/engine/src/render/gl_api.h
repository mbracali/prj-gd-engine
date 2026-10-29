// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// The OpenGL entry points the GL device uses, fetched at run time through
// SDL_GL_GetProcAddress: no GL library or generated loader at build time.
// Every function here exists in both OpenGL 3.3 core and OpenGL ES 3.0, so
// one list serves both. Only gl_device.cpp includes this.

#include <SDL3/SDL_opengl.h>

#define GD04_GL_FUNCTIONS(X)                                                   \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                  \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                    \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                        \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                        \
    X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange)                              \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                              \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                                      \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                              \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                                  \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                              \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                          \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                        \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                  \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                \
    X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync)                                \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                  \
    X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D)                    \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                  \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                    \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                  \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                        \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                  \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                                \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                    \
    X(PFNGLDELETESYNCPROC, glDeleteSync)                                        \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                        \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)            \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)              \
    X(PFNGLFENCESYNCPROC, glFenceSync)                                          \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                    \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                        \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                              \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers)                                      \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                              \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                          \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                    \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                            \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                      \
    X(PFNGLGETSTRINGIPROC, glGetStringi)                                        \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex)                    \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                        \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                      \
    X(PFNGLMAPBUFFERRANGEPROC, glMapBufferRange)                                \
    X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf)                          \
    X(PFNGLSAMPLERPARAMETERFVPROC, glSamplerParameterfv)                        \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                          \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                    \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                          \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding)                      \
    X(PFNGLUNMAPBUFFERPROC, glUnmapBuffer)                                      \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                        \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

// OpenGL 1.x entry points: SDL_opengl.h declares their prototypes, used here
// only for the types (nothing is linked against a GL library).
#define GD04_GL1_FUNCTIONS(X)                                                  \
    X(decltype(&::glBindTexture), glBindTexture)                                \
    X(decltype(&::glBlendFunc), glBlendFunc)                                    \
    X(decltype(&::glClear), glClear)                                            \
    X(decltype(&::glClearColor), glClearColor)                                  \
    X(decltype(&::glColorMask), glColorMask)                                    \
    X(decltype(&::glCullFace), glCullFace)                                      \
    X(decltype(&::glDeleteTextures), glDeleteTextures)                          \
    X(decltype(&::glDepthFunc), glDepthFunc)                                    \
    X(decltype(&::glDepthMask), glDepthMask)                                    \
    X(decltype(&::glDisable), glDisable)                                        \
    X(decltype(&::glDrawArrays), glDrawArrays)                                  \
    X(decltype(&::glDrawElements), glDrawElements)                              \
    X(decltype(&::glEnable), glEnable)                                          \
    X(decltype(&::glFrontFace), glFrontFace)                                    \
    X(decltype(&::glGenTextures), glGenTextures)                                \
    X(decltype(&::glGetError), glGetError)                                      \
    X(decltype(&::glGetIntegerv), glGetIntegerv)                                \
    X(decltype(&::glGetString), glGetString)                                    \
    X(decltype(&::glPixelStorei), glPixelStorei)                                \
    X(decltype(&::glReadPixels), glReadPixels)                                  \
    X(decltype(&::glScissor), glScissor)                                        \
    X(decltype(&::glTexImage2D), glTexImage2D)                                  \
    X(decltype(&::glTexParameteri), glTexParameteri)                            \
    X(decltype(&::glTexSubImage2D), glTexSubImage2D)                            \
    X(decltype(&::glViewport), glViewport)

namespace gl {
#define GD04_GL_DECLARE(type, name) extern type name;
GD04_GL_FUNCTIONS(GD04_GL_DECLARE)
GD04_GL1_FUNCTIONS(GD04_GL_DECLARE)
#undef GD04_GL_DECLARE

// Loads every entry point for the current context; returns the name of the
// first one missing, or nullptr.
const char* load();
bool hasExtension(const char* name);
}  // namespace gl
