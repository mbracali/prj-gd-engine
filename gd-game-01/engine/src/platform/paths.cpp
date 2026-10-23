// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "platform/paths.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <utility>
#include <vector>

#ifdef _WIN32
#define environ _environ
#else
extern char** environ;
#endif

namespace {

// Copies the files of the old <base>/gh1re/gh1re/ folder into the new
// <base>/gd-engine/gd01/ one, when the new one is still empty. SDL builds
// both as <base>/<org>/<app>/ on Linux, Windows and macOS; on Android the
// folder belongs to the app package, which changed too, so there is nothing
// to reach.
void migrateOldFolder(const std::string& dir) {
#ifndef __ANDROID__
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path now = fs::path(dir).parent_path();  // drop the trailing separator
    fs::path old = now.parent_path().parent_path() / "gh1re" / "gh1re";
    if (!fs::is_directory(old, ec) || !fs::is_empty(now, ec)) return;
    for (const auto& e : fs::directory_iterator(old, ec)) {
        if (e.path().filename() == "gpu_probe") continue;  // a launch in progress, not a setting
        fs::copy(e.path(), now / e.path().filename(), fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    }
    SDL_Log("Settings and saves copied from %s", old.string().c_str());
#else
    (void)dir;
#endif
}

}  // namespace

std::string prefDir() {
    static const std::string dir = [] {
        char* pref = SDL_GetPrefPath("gd-engine", "gd01");
        std::string p = pref ? pref : "";
        SDL_free(pref);
        if (!p.empty()) migrateOldFolder(p);
        return p;
    }();
    return dir;
}

void aliasLegacyEnv() {
    const std::string oldPrefix = "GH1RE_";
    std::vector<std::pair<std::string, std::string>> found;
    for (char** e = environ; e && *e; ++e) {
        std::string kv = *e;
        size_t eq = kv.find('=');
        if (eq == std::string::npos || kv.rfind(oldPrefix, 0) != 0) continue;
        found.emplace_back("GD01_" + kv.substr(oldPrefix.size(), eq - oldPrefix.size()), kv.substr(eq + 1));
    }
    for (const auto& [name, value] : found) {
        if (std::getenv(name.c_str())) continue;
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 0);
#endif
    }
}
