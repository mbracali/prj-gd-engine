// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// The OpenGL entry points of the OpenGL3 backend, fetched at run time through
// SDL_GL_GetProcAddress (no GL library at link time). Everything listed
// exists in both OpenGL 3.3 core and OpenGL ES 3.0. Only the backend's own
// files include this, and its names live in their own namespace so they never
// meet a game's GL loader (gd01's gl::, gd02's gl::).

#include <SDL3/SDL_opengl.h>

#define GDCORE_GL_FUNCTIONS(X)                                                  \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                   \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                     \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                         \
    X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange)                               \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                               \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                             \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                                       \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                               \
    X(PFNGLBLENDCOLORPROC, glBlendColor)                                         \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate)                   \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                           \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                               \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                         \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                   \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                 \
    X(PFNGLCLEARBUFFERFIPROC, glClearBufferfi)                                   \
    X(PFNGLCLEARBUFFERFVPROC, glClearBufferfv)                                   \
    X(PFNGLCLEARBUFFERIVPROC, glClearBufferiv)                                   \
    X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync)                                 \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                   \
    X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D)                     \
    X(PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC, glCompressedTexSubImage2D)               \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                   \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                     \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                   \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                         \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                   \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                       \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                                 \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                     \
    X(PFNGLDELETESYNCPROC, glDeleteSync)                                         \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                         \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced)                       \
    X(PFNGLDRAWBUFFERSPROC, glDrawBuffers)                                       \
    X(PFNGLDRAWELEMENTSINSTANCEDPROC, glDrawElementsInstanced)                   \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)               \
    X(PFNGLFENCESYNCPROC, glFenceSync)                                           \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)               \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                     \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                         \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                                 \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                               \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                             \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers)                                       \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                               \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                           \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                     \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                             \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                       \
    X(PFNGLGETSTRINGIPROC, glGetStringi)                                         \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex)                     \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                         \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                       \
    X(PFNGLMAPBUFFERRANGEPROC, glMapBufferRange)                                 \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf)                           \
    X(PFNGLSAMPLERPARAMETERFVPROC, glSamplerParameterfv)                         \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                           \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                     \
    X(PFNGLSTENCILFUNCSEPARATEPROC, glStencilFuncSeparate)                       \
    X(PFNGLSTENCILOPSEPARATEPROC, glStencilOpSeparate)                           \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                           \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding)                       \
    X(PFNGLUNMAPBUFFERPROC, glUnmapBuffer)                                       \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                         \
    X(PFNGLVERTEXATTRIBDIVISORPROC, glVertexAttribDivisor)                       \
    X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer)                     \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

// OpenGL 1.x entry points: SDL_opengl.h declares their prototypes, used only
// for the types (nothing is linked).
#define GDCORE_GL1_FUNCTIONS(X)                                                 \
    X(decltype(&::glBindTexture), glBindTexture)                                 \
    X(decltype(&::glColorMask), glColorMask)                                     \
    X(decltype(&::glCullFace), glCullFace)                                       \
    X(decltype(&::glDeleteTextures), glDeleteTextures)                           \
    X(decltype(&::glDepthFunc), glDepthFunc)                                     \
    X(decltype(&::glDepthMask), glDepthMask)                                     \
    X(decltype(&::glDisable), glDisable)                                         \
    X(decltype(&::glDrawArrays), glDrawArrays)                                   \
    X(decltype(&::glDrawElements), glDrawElements)                               \
    X(decltype(&::glEnable), glEnable)                                           \
    X(decltype(&::glFinish), glFinish)                                           \
    X(decltype(&::glFlush), glFlush)                                             \
    X(decltype(&::glFrontFace), glFrontFace)                                     \
    X(decltype(&::glGenTextures), glGenTextures)                                 \
    X(decltype(&::glGetError), glGetError)                                       \
    X(decltype(&::glGetFloatv), glGetFloatv)                                     \
    X(decltype(&::glGetIntegerv), glGetIntegerv)                                 \
    X(decltype(&::glGetString), glGetString)                                     \
    X(decltype(&::glPixelStorei), glPixelStorei)                                 \
    X(decltype(&::glPolygonOffset), glPolygonOffset)                             \
    X(decltype(&::glReadBuffer), glReadBuffer)                                   \
    X(decltype(&::glReadPixels), glReadPixels)                                   \
    X(decltype(&::glScissor), glScissor)                                         \
    X(decltype(&::glStencilMask), glStencilMask)                                 \
    X(decltype(&::glTexImage2D), glTexImage2D)                                   \
    X(decltype(&::glTexParameteri), glTexParameteri)                             \
    X(decltype(&::glTexSubImage2D), glTexSubImage2D)                             \
    X(decltype(&::glViewport), glViewport)

namespace gd::core::render::glapi {
#define GDCORE_GL_DECLARE(type, name) extern type name;
GDCORE_GL_FUNCTIONS(GDCORE_GL_DECLARE)
GDCORE_GL1_FUNCTIONS(GDCORE_GL_DECLARE)
#undef GDCORE_GL_DECLARE
// glDepthRange (desktop, doubles) or glDepthRangef (ES): whichever exists.
extern void(APIENTRY* glDepthRangeD)(double, double);
extern void(APIENTRY* glDepthRangeF)(float, float);

// Loads every entry point for the current context; returns the name of the
// first one missing, or nullptr.
const char* load();
bool hasExtension(const char* name);
}  // namespace gd::core::render::glapi
