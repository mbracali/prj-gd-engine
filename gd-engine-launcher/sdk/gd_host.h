// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The game module ABI of the Guitar Dream Universal Launcher (gdul).
//
// A game module is a shared library (gd-game-NN.so / gd-game-NN.dll) that the
// launcher loads into its own process. It exports
//
//     extern "C" int gd_game_run(const GdHost* host);
//
// which plays the game in the launcher's window and returns when the player
// quits (its exit code: 0 normal). The launcher then unloads the library, so
// every global of the game starts fresh on the next run.
//
// Plain C types only: the launcher and the modules may be built with
// different compilers or C++ runtimes. Both link the same SDL3 shared library
// (the one next to the launcher), so the SDL objects below are shared.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GD_HOST_ABI_VERSION 1

#if defined(_WIN32)
#define GD_MODULE_EXPORT __declspec(dllexport)
#else
#define GD_MODULE_EXPORT __attribute__((visibility("default")))
#endif

typedef struct GdHost {
    uint32_t abi_version;  // GD_HOST_ABI_VERSION of the launcher
    uint32_t size;         // sizeof(GdHost) of the launcher (fields are only ever appended)

    // The launcher's SDL_Window* (OpenGL, resizable, high pixel density). The
    // module draws into it; it must not destroy it. SDL is already initialised
    // (video, events, gamepad); the module may init more subsystems and must
    // quit only those it initialised.
    void* window;
    // The launcher's SDL_GLContext. A module may draw with it, or (what the
    // shims do) create its own context on the same window and destroy it before
    // returning; the launcher makes its own current again afterwards.
    void* gl_context;

    // argv-style arguments for the game's own command line: argv[0] is the
    // program name, the last argument the disc image path.
    int argc;
    const char* const* argv;

    const char* launcher_version;  // "0.1.0"
    const char* game_id;           // "gd-game-02"
    const char* game_dir;          // the module's folder (UTF-8, ends with a separator)
    const char* save_dir;          // the launcher's save folder (games keep their own)

    // Appends a line to the launcher's action log (date, user, "game <id>: <line>").
    void (*log)(void* user, const char* line);
    void* log_user;
} GdHost;

typedef int (*GdGameRunFn)(const GdHost* host);

#define GD_GAME_RUN_SYMBOL "gd_game_run"

#ifdef __cplusplus
}
#endif
