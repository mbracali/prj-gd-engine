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
// A module may also export (optional) the model explorer of the launcher's
// "Explore models" screen:
//
//     extern "C" const GdExplorer* gd_game_explore(const GdHost* host);
//
// The launcher draws the screen (the list, the search, the help) and drives
// it; the module lists the disc's models and draws the chosen one with its
// own loaders and renderer (GdExplorer below).
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

// ---- The model explorer (optional) ----

// One model of the disc.
typedef struct GdExploreItem {
    const char* group;   // the list's section: "Characters", "Guitars", "Venues", ...
    const char* name;    // what the list shows: "Alterna (Johnny Napalm)", "Gibson SG"
    const char* detail;  // a second line: the archive path, say
} GdExploreItem;

// How the launcher wants the selected model drawn this frame. The camera
// orbits the model's centre: the module frames the model by itself (its
// bounds), the launcher only turns, zooms and pans relative to that.
typedef struct GdExploreView {
    int x, y, w, h;      // the viewer's area in window pixels (origin top-left): the
                         // model is centred there; the module may draw the whole window
    float yaw;           // radians around the up axis; 0 = the model's front
    float pitch;         // radians above the horizon (-1.4 .. 1.4)
    float zoom;          // distance factor: 1 frames the whole model, < 1 closer
    float pan_x, pan_y;  // the orbit centre moved in the view plane, in model radii
    float seconds;       // time since the model was selected (animations)
    int clip;            // the animation clip to play (an index of clip_name), -1 none
    float background[3]; // the launcher's backdrop colour (linear 0..1), for the clear
} GdExploreView;

typedef struct GdExplorer {
    uint32_t size;  // sizeof(GdExplorer) of the module (fields are only ever appended)
    void* self;
    // The models, in list order (grouped: items of a group are together).
    int (*item_count)(void* self);
    int (*item)(void* self, int index, GdExploreItem* out);
    // Loads a model (may take a moment: the launcher shows that it is loading).
    // Returns 0 when it can be drawn. `info` (UTF-8, size `info_size`) gets a
    // short description (meshes, bones, ...) or what went wrong.
    int (*select)(void* self, int index, char* info, int info_size);
    // The selected model's animation clips (0: none).
    int (*clip_count)(void* self);
    const char* (*clip_name)(void* self, int index);
    // Draws the selected model into the launcher's window with the module's
    // own GL context (made current by the module). It must not present the
    // frame: the launcher draws its panels over it and presents it (the
    // shims make SDL_GL_SwapWindow do nothing while the explorer is open).
    void (*draw)(void* self, const GdExploreView* view);
    // Frees everything (models, GL context, SDL subsystems); the launcher
    // unloads the library afterwards.
    void (*close)(void* self);
} GdExplorer;

typedef const GdExplorer* (*GdGameExploreFn)(const GdHost* host);

#define GD_GAME_EXPLORE_SYMBOL "gd_game_explore"

#ifdef __cplusplus
}
#endif
