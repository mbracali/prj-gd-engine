// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The SDL redirections of gd_module_shim.h (see there).
#define GD_MODULE_SHIM_IMPL 1
#include "gd_module_shim.h"
// Forced includes come first: undo the redirections here, where the real
// functions are called.
#undef SDL_Init
#undef SDL_Quit
#undef SDL_CreateWindow
#undef SDL_DestroyWindow
#undef SDL_GL_CreateContext
#undef SDL_GL_DestroyContext

#include <algorithm>
#include <vector>

namespace {

const GdHost* g_host = nullptr;
std::vector<SDL_InitFlags> g_inits;     // each SDL_Init of the game, undone in reverse by SDL_Quit
std::vector<SDL_GLContext> g_contexts;  // the game's GL contexts still alive
std::vector<SDL_Window*> g_windows;     // the game's own (hidden) windows still alive

SDL_Window* hostWindow() { return g_host ? static_cast<SDL_Window*>(g_host->window) : nullptr; }

}  // namespace

void gdshim_begin(const GdHost* host) {
    g_host = host;
    g_inits.clear(), g_contexts.clear(), g_windows.clear();
}

void gdshim_end() {
    // Whatever the game left behind (an early return, an error path).
    for (SDL_GLContext c : g_contexts) SDL_GL_DestroyContext(c);
    for (SDL_Window* w : g_windows) SDL_DestroyWindow(w);
    gdshim_Quit();
    g_contexts.clear(), g_windows.clear();
    g_host = nullptr;
}

const GdHost* gdshim_host() { return g_host; }

void gdshim_setenv(const char* name, const char* value) {
    // Both copies: SDL_getenv reads SDL's environment, getenv the C library's.
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
    SDL_setenv_unsafe(name, value, 1);
}

extern "C" bool gdshim_Init(SDL_InitFlags flags) {
    if (!g_host) return SDL_Init(flags);
    if (!SDL_InitSubSystem(flags)) return false;
    g_inits.push_back(flags);
    return true;
}

extern "C" void gdshim_Quit(void) {
    if (!g_host) {
        SDL_Quit();
        return;
    }
    for (auto it = g_inits.rbegin(); it != g_inits.rend(); ++it) SDL_QuitSubSystem(*it);
    g_inits.clear();
}

extern "C" SDL_Window* gdshim_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags) {
    if (!g_host || (flags & SDL_WINDOW_HIDDEN)) {
        SDL_Window* win = SDL_CreateWindow(title, w, h, flags);
        if (win && g_host) g_windows.push_back(win);
        return win;
    }
    SDL_Window* win = hostWindow();
    if (title) SDL_SetWindowTitle(win, title);
    return win;
}

extern "C" void gdshim_DestroyWindow(SDL_Window* window) {
    if (window && window == hostWindow()) return;  // the launcher's: it stays
    g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), window), g_windows.end());
    SDL_DestroyWindow(window);
}

extern "C" SDL_GLContext gdshim_GL_CreateContext(SDL_Window* window) {
    SDL_GLContext c = SDL_GL_CreateContext(window);
    if (c && g_host) g_contexts.push_back(c);
    return c;
}

extern "C" bool gdshim_GL_DestroyContext(SDL_GLContext context) {
    g_contexts.erase(std::remove(g_contexts.begin(), g_contexts.end(), context), g_contexts.end());
    return SDL_GL_DestroyContext(context);
}
