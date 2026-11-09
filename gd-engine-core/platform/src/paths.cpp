// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "gd/core/platform/paths.h"

#include <SDL3/SDL.h>

#include "utf8.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>

#include <cstdlib>
#endif

namespace gd::core::platform {

namespace {

bool isSep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

PrefLayout resolve(PrefLayout layout) {
    if (layout != PrefLayout::Auto) return layout;
#if defined(GDCORE_LAUNCHER_MODULE) && GDCORE_LAUNCHER_MODULE
    return PrefLayout::Nested;
#else
    return PrefLayout::Sdl;
#endif
}

}  // namespace

std::string prefDir(std::string_view org, std::string_view app, PrefLayout layout) {
    const std::string o(org), a(app);
    char* pref = SDL_GetPrefPath(o.c_str(), a.c_str());
    std::string dir = pref ? pref : "";
    SDL_free(pref);
#ifdef __ANDROID__
    // SDL gives every org/app the app's files/ folder there; the launcher and
    // its modules keep one folder each below it (gd_module_shim.cpp).
    if (!dir.empty() && resolve(layout) == PrefLayout::Nested && !o.empty() && !a.empty()) {
        dir += o + "/" + a + "/";
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(dir.c_str(), &info) && !SDL_CreateDirectory(dir.c_str())) return "";
    }
#else
    (void)resolve(layout);  // Nested and Sdl are the same folder off Android
#endif
    return dir;
}

std::string gamePrefDir(std::string_view app, PrefLayout layout) { return prefDir(kPrefOrg, app, layout); }

std::string baseDir() {
#ifdef __ANDROID__
    return "";
#else
    const char* b = SDL_GetBasePath();  // owned by SDL, trailing separator
    return b ? b : "";
#endif
}

std::string moduleDir() {
#ifdef _WIN32
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&moduleDir), &self))
        return "";
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(self, buf.data(), DWORD(buf.size()));
        if (n == 0) return "";
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return parentDir(detail::narrow(buf));
#else
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&moduleDir), &info) || !info.dli_fname) return "";
    std::string p = info.dli_fname;
    if (!p.empty() && p[0] == '/') return parentDir(p);
    // A relative name: the program itself as started (glibc gives argv[0]
    // for it [C: the selftest]) or a library opened by a relative path.
    // The program: its real place (/proc/self/exe); else the path made
    // absolute now.
#ifdef __linux__
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = '\0';
        if (fileName(exe) == fileName(p)) return parentDir(exe);
    }
#endif
    if (char* abs = realpath(p.c_str(), nullptr)) {
        p = abs;
        std::free(abs);
        return parentDir(p);
    }
    return "";
#endif
}

std::string xdgDataHome() {
#if defined(__unix__) && !defined(__ANDROID__) && !defined(__APPLE__)
    const char* xdg = SDL_getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return xdg;
    const char* home = SDL_getenv("HOME");
    if (home && *home) return std::string(home) + "/.local/share";
#endif
    return "";
}

bool isContentUri(std::string_view path) { return path.substr(0, 10) == "content://"; }

bool isDescriptorPath(std::string_view path) {
    constexpr std::string_view kFd = "/proc/self/fd/";
    if (path.size() <= kFd.size() || path.substr(0, kFd.size()) != kFd) return false;
    for (char c : path.substr(kFd.size()))
        if (c < '0' || c > '9') return false;
    return true;
}

std::string join(std::string_view dir, std::string_view name) {
    if (dir.empty()) return std::string(name);
    std::string s(dir);
    if (!isSep(s.back())) s += '/';
    size_t skip = 0;
    while (skip < name.size() && isSep(name[skip])) ++skip;
    s += name.substr(skip);
    return s;
}

std::string parentDir(std::string_view path) {
    size_t end = path.size();
    while (end > 1 && isSep(path[end - 1])) --end;  // "a/b/" is the folder b
    size_t i = end;
    while (i > 0 && !isSep(path[i - 1])) --i;
    return std::string(path.substr(0, i));
}

std::string fileName(std::string_view path) {
    size_t end = path.size();
    while (end > 0 && isSep(path[end - 1])) --end;
    size_t i = end;
    while (i > 0 && !isSep(path[i - 1])) --i;
    return std::string(path.substr(i, end - i));
}

bool isFile(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

bool isDir(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

bool makeDirs(const std::string& path) {
    if (isDir(path)) return true;
    return SDL_CreateDirectory(path.c_str()) && isDir(path);  // SDL makes the parents too
}

}  // namespace gd::core::platform
