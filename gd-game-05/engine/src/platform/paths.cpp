// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "platform/paths.h"

#include <SDL3/SDL.h>

#include "app_ids.h"

std::string prefDir() {
    static const std::string dir = [] {
        char* pref = SDL_GetPrefPath(app::kPrefOrg, app::kPrefApp);
        std::string p = pref ? pref : "";
        SDL_free(pref);
        return p;
    }();
    return dir;
}
