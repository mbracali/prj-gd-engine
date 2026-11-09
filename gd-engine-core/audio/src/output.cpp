// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "gd/core/audio/output.h"

#include <algorithm>
#include <mutex>

#include <SDL3/SDL.h>

#include "backends.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace gd::core::audio {
namespace {

std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

std::vector<Backend>& registry() {
    static std::vector<Backend> r = [] {
        std::vector<Backend> v;
#if GDCORE_AUDIO_HAS_MINIAUDIO
        v.push_back(detail::miniaudioBackend());
#endif
#if GDCORE_AUDIO_HAS_OPENAL
        v.push_back(detail::openalBackend());
#endif
#if GDCORE_AUDIO_HAS_FAUDIO
        v.push_back(detail::faudioBackend());
#endif
        return v;
    }();
    return r;
}

}  // namespace

std::vector<Backend> backends() {
    std::lock_guard<std::mutex> lock(registryMutex());
    return registry();
}

std::vector<std::string> available() {
    std::vector<std::string> ids;
    for (const Backend& b : backends()) {
        std::string why;
        if (b.probe && b.probe(why)) ids.push_back(b.id);
    }
    return ids;
}

std::unique_ptr<Output> create(const std::string& id, std::string& error) {
    for (const Backend& b : backends()) {
        if (b.id != id) continue;
        std::string why;
        if (b.probe && !b.probe(why)) {
            error = id + " is not available: " + why;
            return nullptr;
        }
        std::unique_ptr<Output> out = b.make ? b.make() : nullptr;
        if (!out) error = id + ": could not be created";
        return out;
    }
    error = "unknown audio backend \"" + id + "\"";
    return nullptr;
}

void registerBackend(Backend backend) {
    std::lock_guard<std::mutex> lock(registryMutex());
    std::vector<Backend>& r = registry();
    auto it = std::find_if(r.begin(), r.end(), [&](const Backend& b) { return b.id == backend.id; });
    if (it != r.end()) *it = std::move(backend);
    else r.push_back(std::move(backend));
}

namespace detail {

// [I] Candidate to move into gd-engine-core/platform/ (shared paths) once it
// exists; kept here so the audio area builds on its own.
std::string thisModuleDir() {
    static const char marker = 0;  // an address inside this library
#if defined(_WIN32)
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&marker), &self))
        return "";
    wchar_t wide[32768];
    DWORD n = GetModuleFileNameW(self, wide, DWORD(sizeof wide / sizeof wide[0]));
    if (n == 0 || n >= sizeof wide / sizeof wide[0]) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, int(n), nullptr, 0, nullptr, nullptr);
    std::string path(size_t(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, int(n), path.data(), len, nullptr, nullptr);
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
#elif defined(__ANDROID__)
    // The APK's libraries are found by name in the app's linker namespace
    // (and may sit inside the APK, with no folder of their own).
    (void)marker;
    return "";
#else
    Dl_info info{};
    if (!dladdr(&marker, &info) || !info.dli_fname) return "";
    std::string path = info.dli_fname;
    if (path.empty() || path[0] != '/') {
        // [I] The main program reports the name it was started by (maybe
        // relative): SDL knows its folder.
        const char* base = SDL_GetBasePath();
        return base ? std::string(base) : std::string();
    }
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
#endif
}

}  // namespace detail
}  // namespace gd::core::audio
