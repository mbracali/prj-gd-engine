// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "platform/paths.h"

#include "app_ids.h"
#include "gd/core/platform/paths.h"

// gd-engine-core's pref folder: SDL_GetPrefPath("gd-engine", "gd05") as
// before, the launcher shim's nested folder on Android in a module build.
static_assert(std::string_view(app::kPrefOrg) == gd::core::platform::kPrefOrg, "the core's organisation folder");

std::string prefDir() {
    static const std::string dir = gd::core::platform::gamePrefDir(app::kPrefApp);
    return dir;
}
