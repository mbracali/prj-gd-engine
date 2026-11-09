// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Folders: the save / settings folder of a program, the program's own folder,
// the folder of the binary that holds this code, and small path helpers.
// Paths are UTF-8 strings everywhere (Windows included).
#pragma once

#include <string>
#include <string_view>

namespace gd::core::platform {

// The organisation every Guitar Dream program saves under ("gd-engine").
inline constexpr const char* kPrefOrg = "gd-engine";

// Where <org>/<app> keeps its settings and saves.
//   Sdl     SDL_GetPrefPath(org, app), exactly as every game and the launcher
//           call it today [C]: Linux $XDG_DATA_HOME/<org>/<app>/ (else
//           ~/.local/share/<org>/<app>/), Windows %APPDATA%\<org>\<app>\,
//           macOS ~/Library/Application Support/<org>/<app>/; Android: the
//           app's internal files/ folder (SDL ignores org and app there), which
//           is what a game's own APK uses.
//   Nested  the launcher's layout on Android: <files>/<org>/<app>/ (the
//           launcher's saveDir and its module shim's SDL_GetPrefPath [C]);
//           the same as Sdl on every other system.
//   Auto    Nested when this copy of the core is built into a launcher module
//           (CMake GDCORE_LAUNCHER_MODULE), Sdl otherwise: the folder the
//           game's own code gets in that same build, so a game moving to this
//           API keeps its folder.
enum class PrefLayout { Auto, Sdl, Nested };

// That folder with a trailing separator, created if missing; "" when the
// system has none (SDL could not make it).
std::string prefDir(std::string_view org, std::string_view app, PrefLayout layout = PrefLayout::Auto);

// prefDir(kPrefOrg, app): "gd01", "gd02", ..., "gdul".
std::string gamePrefDir(std::string_view app, PrefLayout layout = PrefLayout::Auto);

// The program's folder (SDL_GetBasePath) with a trailing separator; "" on
// Android (relative paths read the APK's assets there).
std::string baseDir();

// The folder of the binary this code is linked into (a game module's .so /
// .dll / .dylib, or the program), with a trailing separator; "" if unknown.
// A backend loads its shared libraries from here (library.h).
std::string moduleDir();

// Linux / BSD: $XDG_DATA_HOME, else $HOME/.local/share (where desktop entries
// and icons go); "" elsewhere or when neither is set.
std::string xdgDataHome();

// Android: a document chosen in the system file dialog (content://...).
bool isContentUri(std::string_view path);
// Android: a descriptor path "/proc/self/fd/<n>" (how the launcher hands a
// module its disc when it is such a document).
bool isDescriptorPath(std::string_view path);

// Path helpers ('/' and, on Windows, '\\' are separators).
std::string join(std::string_view dir, std::string_view name);
std::string parentDir(std::string_view path);  // with a trailing separator; "" for a bare name
std::string fileName(std::string_view path);
bool isFile(const std::string& path);
bool isDir(const std::string& path);
bool makeDirs(const std::string& path);  // true if it exists afterwards

}  // namespace gd::core::platform
