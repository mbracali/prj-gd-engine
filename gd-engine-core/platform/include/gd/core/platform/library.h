// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Shared libraries loaded at run time: what a backend uses for a library it
// must not link (OpenAL Soft is LGPL: a separate shared library next to the
// module, loaded when the player picks that backend). The packages put such a
// library next to the game module (docs/README.md, "Shared libraries in the
// packages"); on Android it is in the APK's native library folder.
#pragma once

#include <string>
#include <vector>

namespace gd::core::platform {

class SharedLibrary {
public:
    SharedLibrary() = default;
    ~SharedLibrary();
    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;
    SharedLibrary(SharedLibrary&& o) noexcept;
    SharedLibrary& operator=(SharedLibrary&& o) noexcept;

    // Tries each name, first in moduleDir() (next to the game module), then
    // by name alone (the system's search: Android's app library folder, the
    // program's folder on Windows, the loader path elsewhere). Names are file
    // names: {"libopenal.so.1", "libopenal.so"}, {"OpenAL32.dll", "soft_oal.dll"}.
    // false and `error` (every attempt) when none loads.
    bool open(const std::vector<std::string>& names, std::string* error = nullptr);
    void close();
    bool isOpen() const { return handle_ != nullptr; }
    const std::string& path() const { return path_; }  // what was loaded
    void* symbol(const char* name) const;

private:
    void* handle_ = nullptr;
    std::string path_;
};

}  // namespace gd::core::platform
