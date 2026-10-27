// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Turns a game written as a standalone SDL3 program into a launcher module
// without changing its code. Force-included before every C++ file of the
// module (GCC/Clang: -include gd_module_shim.h, MSVC: /FI), it redirects the
// calls that own the process's SDL state:
//
//   SDL_Init / SDL_Quit             -> init / quit only the subsystems the game
//                                      asked for (SDL counts them), never SDL as a whole
//   SDL_CreateWindow                -> the launcher's window (a hidden window,
//                                      e.g. a shader test, stays a real one)
//   SDL_DestroyWindow               -> nothing for the launcher's window
//   SDL_GL_CreateContext / Destroy  -> real contexts on that window, tracked so
//                                      none outlives the game
//   SDL_GL_SwapWindow               -> nothing while the model explorer is open
//                                      (the launcher presents its frames)
//
// With no host (gdshim_begin not called) every call goes straight to SDL, so
// the same objects still work in a standalone program. Implementation:
// gd_module_shim.cpp, compiled into the module.
#pragma once

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED 1  // the module has no main(): the launcher owns the process
#endif

#ifdef __cplusplus
#include <SDL3/SDL.h>

#include "gd_host.h"

extern "C" {
bool gdshim_Init(SDL_InitFlags flags);
void gdshim_Quit(void);
SDL_Window* gdshim_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags);
void gdshim_DestroyWindow(SDL_Window* window);
SDL_GLContext gdshim_GL_CreateContext(SDL_Window* window);
bool gdshim_GL_DestroyContext(SDL_GLContext context);
bool gdshim_GL_SwapWindow(SDL_Window* window);
}

// Called by the module's gd_game_run around the game's own main().
void gdshim_begin(const GdHost* host);
void gdshim_end();
const GdHost* gdshim_host();
// The model explorer (gd_game_explore): while on, SDL_GL_SwapWindow does nothing.
void gdshim_set_exploring(bool on);
// Makes the module's newest GL context current on the launcher's window
// (each explorer call starts with it: the launcher's context is current in between).
bool gdshim_make_current();
// Sets an environment variable for the game (SDL's copy and the C library's).
void gdshim_setenv(const char* name, const char* value);

#ifndef GD_MODULE_SHIM_IMPL
#define SDL_Init gdshim_Init
#define SDL_Quit gdshim_Quit
#define SDL_CreateWindow gdshim_CreateWindow
#define SDL_DestroyWindow gdshim_DestroyWindow
#define SDL_GL_CreateContext gdshim_GL_CreateContext
#define SDL_GL_DestroyContext gdshim_GL_DestroyContext
#define SDL_GL_SwapWindow gdshim_GL_SwapWindow
#endif
#endif  // __cplusplus
