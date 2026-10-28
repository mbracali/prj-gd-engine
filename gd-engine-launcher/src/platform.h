// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// What differs between systems: folders, the user's name, the clock, shared
// libraries. Paths are UTF-8 strings everywhere in the launcher.
#pragma once

#include <string>
#include <vector>

namespace platform {

#if defined(_WIN32)
inline constexpr const char* kOs = "windows";
inline constexpr const char* kModuleExt = ".dll";
#elif defined(__APPLE__)
inline constexpr const char* kOs = "macos";
inline constexpr const char* kModuleExt = ".dylib";
#elif defined(__ANDROID__)
inline constexpr const char* kOs = "android";
inline constexpr const char* kModuleExt = ".so";
#else
inline constexpr const char* kOs = "linux";
inline constexpr const char* kModuleExt = ".so";
#endif
#if defined(__x86_64__) || defined(_M_X64)
inline constexpr const char* kArch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
inline constexpr const char* kArch = "arm64";
#else
inline constexpr const char* kArch = "unknown";
#endif

std::string baseDir();  // the program's folder, with a trailing separator ("" on Android: the APK's assets)
std::string saveDir();  // the launcher's save folder (SDL_GetPrefPath("gd-engine", "gdul"))
std::string userName();  // the OS account name
std::string isoNow();    // local time, ISO 8601 with offset: 2026-10-27T14:03:12-03:00

std::string join(const std::string& dir, const std::string& name);
std::string parentDir(const std::string& path);
std::string fileName(const std::string& path);
bool isFile(const std::string& path);  // Android: also a file in the APK's assets (a relative path)
bool isDir(const std::string& path);
std::vector<std::string> listFiles(const std::string& dir);  // full paths, sorted
// The same; `error` gets why the folder cannot be read (missing, no permission).
std::vector<std::string> listFiles(const std::string& dir, std::string& error);

// macOS: the .app the launcher runs from (no trailing separator), "" elsewhere.
std::string appBundle();
// macOS runs an app that came with a download's quarantine mark from a
// read-only copy (App Translocation) until it is moved with the Finder: true
// for such a path.
bool isTranslocated(const std::string& path);
// That app's real place (Security.framework's SecTranslocateCreateOriginalPathForURL), or "".
std::string originalAppBundle(const std::string& app);

// Android: the folder of the APK's native libraries (libmain.so, the game
// modules lib<id>.so), with a trailing separator; "" elsewhere.
std::string nativeLibDir();

// Android: a disc image chosen in the file dialog is a document, a content://
// URI, read where it is (never copied). isDocument: such a path (false
// elsewhere). openDocument: a file descriptor to read it (the caller closes
// it with closeDocument), or -1 and `error` says why (no permission any more,
// not a file on the device). documentName: its name as the file dialog shows
// it. displayName: that for a document, the file name for any other path.
bool isDocument(const std::string& path);
int openDocument(const std::string& uri, std::string& error);
void closeDocument(int fd);
std::string documentName(const std::string& uri);
std::string displayName(const std::string& path);

// Shared libraries (the game modules).
void* openLibrary(const std::string& path, std::string& error);
void* librarySymbol(void* lib, const char* name);
// Unloads; returns whether the library really left the process.
bool closeLibrary(void* lib, const std::string& path);

}  // namespace platform
