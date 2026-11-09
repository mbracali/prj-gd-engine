// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// Every name the program shows or stores, in one place, so a rename is a
// one-file change (docs/reuse_and_lessons.md, L16).
namespace app {

inline constexpr const char* kProgram = "gd05";
inline constexpr const char* kDisplayName = "GD Engine 05";
inline constexpr const char* kWindowTitle = "Guitar Dream Engine: Game 05";
inline constexpr const char* kAppId = "io.github.mbracali.gd05";
// The save folder: SDL_GetPrefPath(kPrefOrg, kPrefApp).
inline constexpr const char* kPrefOrg = "gd-engine";
inline constexpr const char* kPrefApp = "gd05";

#ifdef GD05_VERSION
inline constexpr const char* kVersion = GD05_VERSION;
#else
inline constexpr const char* kVersion = "dev";
#endif

}  // namespace app
