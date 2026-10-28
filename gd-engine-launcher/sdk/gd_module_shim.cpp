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
#undef SDL_GL_SwapWindow
#undef fopen
#undef SDL_GetPrefPath

#include <algorithm>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// fopen that also reads "/proc/self/fd/<n>" (gd_module_shim.h). The launcher
// hands a game its disc so (module_host.cpp) when the player chose it in
// Android's file dialog: a descriptor from the document's provider. Opening
// that path again is refused on Android 11+ for a file of shared storage the
// app does not own (FUSE checks the opener: EACCES), so such a FILE reads the
// descriptor itself: a duplicate, read with pread at the FILE's own offset
// (several FILEs on one disc, on several threads, never share an offset).
namespace {
struct FdFile {
    int fd;
    off64_t pos, size;
};
int fdRead(void* c, char* buf, int n) {
    auto* f = static_cast<FdFile*>(c);
    ssize_t got;
    do got = pread64(f->fd, buf, size_t(n), f->pos);
    while (got < 0 && errno == EINTR);
    if (got > 0) f->pos += got;
    return int(got);
}
fpos64_t fdSeek(void* c, fpos64_t off, int whence) {
    auto* f = static_cast<FdFile*>(c);
    off64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos : f->size;
    if (base + off < 0) {
        errno = EINVAL;
        return -1;
    }
    f->pos = base + off;
    return f->pos;
}
int fdClose(void* c) {
    auto* f = static_cast<FdFile*>(c);
    ::close(f->fd);
    delete f;
    return 0;
}
}  // namespace

extern "C" char* gdshim_GetPrefPath(const char* org, const char* app) {
    char* base = SDL_GetPrefPath(org, app);  // Android: <app files>/, the same for everyone
    if (!base || !org || !app || !*org || !*app) return base;
    const std::string root = base;
    SDL_free(base);
    const std::string dir = root + org + "/" + app + "/";
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(dir.c_str(), &info)) {
        SDL_CreateDirectory(dir.c_str());
        // gd02 ran in launchers before 0.1.6 with <app files>/ as its save
        // folder: its files come along once (the originals stay).
        if (std::strcmp(app, "gd02") == 0) {
            int n = 0;
            if (char** names = SDL_GlobDirectory(root.c_str(), "*", 0, &n)) {
                for (int i = 0; i < n; ++i) {
                    const std::string from = root + names[i];
                    if (std::strncmp(names[i], "gdul", 4) != 0 &&  // the launcher's own
                        SDL_GetPathInfo(from.c_str(), &info) && info.type == SDL_PATHTYPE_FILE &&
                        SDL_CopyFile(from.c_str(), (dir + names[i]).c_str()))
                        SDL_Log("gdul shim: %s copied into %s", names[i], dir.c_str());
                }
                SDL_free(names);
            }
        }
    }
    return SDL_strdup(dir.c_str());
}

extern "C" FILE* gdshim_fopen(const char* path, const char* mode) {
    if (FILE* f = ::fopen(path, mode)) return f;
    const int why = errno;
    static const char kFd[] = "/proc/self/fd/";
    if (!path || std::strncmp(path, kFd, sizeof kFd - 1) != 0 || !mode || mode[0] != 'r' || std::strchr(mode, '+')) {
        errno = why;
        return nullptr;
    }
    char* end = nullptr;
    const long n = std::strtol(path + sizeof kFd - 1, &end, 10);
    struct stat64 st {};
    if (end == path + sizeof kFd - 1 || *end || n < 0 || fstat64(int(n), &st) != 0 || !S_ISREG(st.st_mode)) {
        errno = why;
        return nullptr;
    }
    const int fd = fcntl(int(n), F_DUPFD_CLOEXEC, 0);
    if (fd < 0) return nullptr;
    auto* c = new FdFile{fd, 0, off64_t(st.st_size)};
    FILE* f = funopen64(c, &fdRead, nullptr, &fdSeek, &fdClose);
    if (!f) fdClose(c);
    else SDL_Log("gdul shim: %s read through its descriptor (reopening it: %s)", path, std::strerror(why));
    return f;
}
#endif

namespace {

const GdHost* g_host = nullptr;
std::vector<SDL_InitFlags> g_inits;     // each SDL_Init of the game, undone in reverse by SDL_Quit
std::vector<SDL_GLContext> g_contexts;  // the game's GL contexts still alive
std::vector<SDL_Window*> g_windows;     // the game's own (hidden) windows still alive
bool g_exploring = false;               // the model explorer is open: the launcher presents

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
    g_exploring = false;
}

void gdshim_set_exploring(bool on) { g_exploring = on; }

bool gdshim_make_current() {
    if (!g_host || g_contexts.empty()) return false;
    return SDL_GL_MakeCurrent(hostWindow(), g_contexts.back());
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

extern "C" bool gdshim_GL_SwapWindow(SDL_Window* window) {
    if (g_exploring && window == hostWindow()) return true;  // the launcher presents the frame
    return SDL_GL_SwapWindow(window);
}
