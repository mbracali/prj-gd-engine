// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// Every name the program shows or stores, in one place, so a rename is a
// one-file change (docs/reuse_and_lessons.md, L16).
namespace app {

inline constexpr const char* kProgram = "gd06";
inline constexpr const char* kDisplayName = "Guitar Hero: Aerosmith";
inline constexpr const char* kWindowTitle = "Guitar Hero: Aerosmith";
inline constexpr const char* kAppId = "io.github.mbracali.gd06";
// The save folder: SDL_GetPrefPath(kPrefOrg, kPrefApp).
inline constexpr const char* kPrefOrg = "gd-engine";
inline constexpr const char* kPrefApp = "gd06";

#ifdef GD06_VERSION
inline constexpr const char* kVersion = GD06_VERSION;
#else
inline constexpr const char* kVersion = "dev";
#endif

}  // namespace app
