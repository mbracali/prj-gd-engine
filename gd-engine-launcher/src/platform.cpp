// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace platform {

namespace {

fs::path toPath(const std::string& utf8) { return fs::u8path(utf8); }

#ifdef _WIN32
// A full path with backslashes: LOAD_WITH_ALTERED_SEARCH_PATH looks for the
// library's own DLLs in its folder only when the path uses them.
std::wstring winPath(std::string s) {
    std::replace(s.begin(), s.end(), '/', '\\');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
#endif

}  // namespace

std::string baseDir() {
#ifdef __ANDROID__
    return "";  // relative paths: SDL reads them from the APK's assets
#else
    const char* b = SDL_GetBasePath();
    return b ? b : "./";
#endif
}

std::string nativeLibDir() {
#ifdef __ANDROID__
    // Where this library (libmain.so) was loaded from: the APK's native
    // libraries are extracted there (useLegacyPackaging in android/app/build.gradle).
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&nativeLibDir), &info) && info.dli_fname) return parentDir(info.dli_fname);
#endif
    return "";
}

std::string saveDir() {
    char* p = SDL_GetPrefPath("gd-engine", "gdul");
    std::string s = p ? p : baseDir();
    SDL_free(p);
    return s;
}

std::string userName() {
#ifdef _WIN32
    wchar_t buf[257];
    DWORD n = 257;
    if (GetUserNameW(buf, &n)) return narrow(buf);
    if (const char* u = SDL_getenv("USERNAME")) return u;
#else
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_name) return pw->pw_name;
    for (const char* v : {"USER", "LOGNAME"})
        if (const char* u = SDL_getenv(v); u && *u) return u;
#endif
    return "unknown";
}

std::string isoNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S%z", &tm);
    std::string s = buf;  // ...-0300: ISO 8601's extended form is -03:00
    if (s.size() >= 5 && (s[s.size() - 5] == '+' || s[s.size() - 5] == '-')) s.insert(s.size() - 2, ":");
    return s;
}

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir.back();
    return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

std::string parentDir(const std::string& path) {
    std::string p = path;
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
}

std::string fileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool isFile(const std::string& path) {
    std::error_code ec;
    if (!path.empty() && fs::is_regular_file(toPath(path), ec)) return true;
#ifdef __ANDROID__
    if (!path.empty() && path[0] != '/')  // an asset
        if (SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb")) {
            SDL_CloseIO(io);
            return true;
        }
#endif
    return false;
}

bool isDir(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::is_directory(toPath(path), ec);
}

std::vector<std::string> listFiles(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(toPath(dir), ec))
        if (e.is_regular_file(ec)) out.push_back(e.path().u8string());
    std::sort(out.begin(), out.end());
    return out;
}

void* openLibrary(const std::string& path, std::string& error) {
#ifdef _WIN32
    // The module's own folder first for its DLLs (FFmpeg), then the launcher's (SDL3).
    HMODULE h = LoadLibraryExW(winPath(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) error = "LoadLibrary failed (error " + std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void*>(h);
#else
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) error = dlerror();
    return h;
#endif
}

void* librarySymbol(void* lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

bool closeLibrary(void* lib, const std::string& path) {
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
    HMODULE still = nullptr;
    return !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, winPath(path).c_str(), &still) || !still;
#else
    dlclose(lib);
    void* again = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (!again) return true;
    dlclose(again);
    return false;
#endif
}

}  // namespace platform
