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
// And (optional) the game's menu sounds, read from the disc, which the
// launcher plays in its own menus:
//
//     extern "C" int gd_game_ui_sounds(const GdHost* host, GdUiSoundFn sound, void* user);
//
// Plain C types only: the launcher and the modules may be built with
// different compilers or C++ runtimes. Both link the same SDL3 shared library
// (the one next to the launcher), so the SDL objects below are shared.
#pragma once

#include <stddef.h>
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

    // (gdul 0.1.6) GD_HOST_* flags: what the launcher knows about the
    // machine. Read only when `size` covers it (an older launcher: 0).
    uint32_t flags;

    // (gdul 1.1.13, optional: GD_HOST_HAS(host, request_window).) The window
    // for another graphics API than OpenGL: gd-engine-core's SDL3 GPU and
    // Vulkan backends (gd-engine-core/docs/render.md section 5). A window
    // cannot carry a GL context and a GPU swapchain at once, and Vulkan /
    // Metal surfaces want a window made for them, so the launcher releases
    // its OpenGL context and makes the window again for `api`
    // (GD_WINDOW_API_*: same size, place, full screen state and title). It
    // returns the SDL_Window* to draw into from now on (also in `window`;
    // `gl_context` is NULL while the window is not an OpenGL one); the window
    // the module had before may be gone. NULL: the launcher cannot (no Vulkan
    // loader...): the window is as it was, an OpenGL one.
    // GD_WINDOW_API_OPENGL asks for an OpenGL window again (a module whose
    // backend failed to start). Before asking for another API, or returning
    // from gd_game_run, the module releases the window (destroys its GPU
    // device / Vulkan surface); the launcher takes its OpenGL window back by
    // itself after gd_game_run. Called on the thread that runs gd_game_run.
    // The module shim calls it from SDL_CreateWindow (gd_module_shim.h): a
    // game written on gd-engine-core needs no code of its own for it.
    void* (*request_window)(void* user, uint32_t api);
    void* request_window_user;
} GdHost;

// The graphics APIs of request_window.
#define GD_WINDOW_API_OPENGL 0u   // SDL_WINDOW_OPENGL: the launcher's own (every module starts with it)
#define GD_WINDOW_API_SDL_GPU 1u  // no graphics flag: for SDL_ClaimWindowForGPUDevice
#define GD_WINDOW_API_VULKAN 2u   // SDL_WINDOW_VULKAN: for SDL_Vulkan_CreateSurface
#define GD_WINDOW_API_METAL 3u    // SDL_WINDOW_METAL: for SDL_Metal_CreateView (macOS / iOS)

// A GdHost field the launcher has (its `size` covers it) and has set.
#define GD_HOST_HAS(h, field) ((h)->size >= offsetof(GdHost, field) + sizeof((h)->field) && (h)->field)

// How gd-engine-core and the module shim talk about the window (SDL global
// properties, strings: the core is not built with the shim). Its
// gd::core::render::prepareWindow(backend) sets GD_PROP_WINDOW_API_REQUEST to
// the backend's id ("opengl3", "sdl3", "vulkan") before the game makes its
// window; the shim's SDL_CreateWindow asks request_window for that API and
// sets GD_PROP_WINDOW (a pointer property: the launcher's window it returns)
// and GD_PROP_WINDOW_API (what that window is made for: "opengl3", "sdl3",
// "vulkan", "metal"); gd::core::render::createDevice refuses a backend that
// window is not made for, with the reason (and the game falls back to
// OpenGL). All are cleared at gdshim_begin / gdshim_end.
#define GD_PROP_WINDOW_API_REQUEST "gd.render.window_api_request"
#define GD_PROP_WINDOW_API "gd.render.window_api"
#define GD_PROP_WINDOW "gd.render.window"

// A handheld PC (a Steam Deck: the launcher's Steam Deck download, or the
// SteamDeck=1 environment / DMI board "Jupiter" or "Galileo"): a module
// starts with its Steam Deck settings as the defaults (full screen 1280 x 800,
// 60 FPS, lighter effects, handheld controls). The player's own saved
// settings still win; nothing is locked.
#define GD_HOST_HANDHELD 0x1u
// The flags of a host, 0 when it is older than the field.
#define GD_HOST_FLAGS(h) ((h)->size >= offsetof(GdHost, flags) + sizeof(uint32_t) ? (h)->flags : 0u)

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
    int clip;            // the animation clip to play (an index of clip_name), -1 none:
                         // the rest (bind) pose (a T-pose for GH2's characters)
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
    // own GL context (made current by the module, and left current: the
    // launcher reads the viewer's area of that context's back buffer, then
    // draws it with its own context and presents it). It must not present
    // the frame (the shims make SDL_GL_SwapWindow do nothing while the
    // explorer is open).
    void (*draw)(void* self, const GdExploreView* view);
    // Frees everything (models, GL context, SDL subsystems); the launcher
    // unloads the library afterwards.
    void (*close)(void* self);

    // (gdul 0.1.6, optional: read only when `size` covers them.) The selected
    // model carries an instrument (a character's guitar, bass or drum kit):
    // 1, else 0. The launcher then shows a "Show instrument" checkbox.
    int (*has_instrument)(void* self);
    // Shows (1, the default) or hides (0) it, from the next select on and on
    // the selected model at once (it may reload it: the module makes its own
    // context current, the launcher its own again afterwards).
    void (*show_instrument)(void* self, int shown);
} GdExplorer;

// A GdExplorer field the module has (its `size` covers it).
#define GD_EXPLORER_HAS(api, field) ((api)->size >= offsetof(GdExplorer, field) + sizeof((api)->field) && (api)->field)

typedef const GdExplorer* (*GdGameExploreFn)(const GdHost* host);

#define GD_GAME_EXPLORE_SYMBOL "gd_game_explore"

// ---- Menu sounds (optional) ----

// One sound: `role` is what it is for in the launcher's menus, "move" (the
// focus moved), "select", "play" (a game starts), "back" or "error"; a role
// may come several times (variations: the launcher picks one at random).
// Interleaved stereo float samples at `rate` Hz, valid during the call.
typedef void (*GdUiSoundFn)(void* user, const char* role, const float* stereo, int frames, int rate);

// Reads the menu sounds from the disc (host->argv's last argument) and gives
// each to `sound`; returns how many it gave (0: none, the launcher stays
// silent). Called once, with no window or GL work; the library is unloaded
// afterwards.
typedef int (*GdGameUiSoundsFn)(const GdHost* host, GdUiSoundFn sound, void* user);

#define GD_GAME_UI_SOUNDS_SYMBOL "gd_game_ui_sounds"

#ifdef __cplusplus
}
#endif
