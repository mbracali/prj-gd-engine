// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "gd/core/platform/library.h"

#include <SDL3/SDL.h>

#include "gd/core/platform/paths.h"

namespace gd::core::platform {

SharedLibrary::~SharedLibrary() { close(); }

SharedLibrary::SharedLibrary(SharedLibrary&& o) noexcept : handle_(o.handle_), path_(std::move(o.path_)) {
    o.handle_ = nullptr;
}

SharedLibrary& SharedLibrary::operator=(SharedLibrary&& o) noexcept {
    if (this != &o) {
        close();
        handle_ = o.handle_;
        path_ = std::move(o.path_);
        o.handle_ = nullptr;
    }
    return *this;
}

bool SharedLibrary::open(const std::vector<std::string>& names, std::string* error) {
    close();
    std::string why;
    const std::string here = moduleDir();
    for (const std::string& name : names) {
        std::vector<std::string> tries;
        if (!here.empty()) tries.push_back(join(here, name));
        tries.push_back(name);
        for (const std::string& t : tries) {
            if (SDL_SharedObject* so = SDL_LoadObject(t.c_str())) {
                handle_ = so;
                path_ = t;
                return true;
            }
            why += (why.empty() ? "" : "; ") + t + ": " + SDL_GetError();
        }
    }
    if (error) *error = why.empty() ? "no library name given" : why;
    return false;
}

void SharedLibrary::close() {
    if (handle_) SDL_UnloadObject(static_cast<SDL_SharedObject*>(handle_));
    handle_ = nullptr;
    path_.clear();
}

void* SharedLibrary::symbol(const char* name) const {
    if (!handle_ || !name) return nullptr;
    return reinterpret_cast<void*>(SDL_LoadFunction(static_cast<SDL_SharedObject*>(handle_), name));
}

}  // namespace gd::core::platform
