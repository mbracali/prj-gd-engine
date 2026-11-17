// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// Every name the program shows or stores, in one place (as gd-game-10's app_ids.h).
namespace app {

inline constexpr const char* kProgram = "gd24";
inline constexpr const char* kDisplayName = "GD Engine 24";
inline constexpr const char* kWindowTitle = "Guitar Dream Engine: Game 24";
inline constexpr const char* kAppId = "io.github.mbracali.gd24";
// The save folder: SDL_GetPrefPath(kPrefOrg, kPrefApp).
inline constexpr const char* kPrefOrg = "gd-engine";
inline constexpr const char* kPrefApp = "gd24";
// Rock Band Unplugged (USA): UMD_DATA.BIN's disc ID.
inline constexpr const char* kDiscId = "ULUS-10418";

#ifdef GD24_VERSION
inline constexpr const char* kVersion = GD24_VERSION;
#else
inline constexpr const char* kVersion = "dev";
#endif

}  // namespace app
