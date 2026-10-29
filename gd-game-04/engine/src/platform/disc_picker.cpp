// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "platform/disc_picker.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <vector>

#include "platform/paths.h"

namespace {

std::string configPath() { return prefDir().empty() ? std::string() : prefDir() + "config.txt"; }

// Paths are UTF-8 strings everywhere (SDL's, the file dialog's); std::filesystem
// only for listing a folder.
std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// The first .iso in `dir`, or "".
std::string findIsoIn(const std::string& dir) {
    if (dir.empty()) return {};
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(std::u8string(dir.begin(), dir.end())), ec)) {
        std::string ext = utf8(e.path().extension());
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".iso" && e.is_regular_file(ec)) return utf8(e.path());
    }
    return {};
}

#ifdef __ANDROID__
// The app's external files folder (Android/data/io.github.mbracali.gd04/files/).
std::string androidFolder() {
    const char* ext = SDL_GetAndroidExternalStoragePath();
    if (ext && *ext) return std::string(ext) + "/";
    return prefDir();
}
#endif

// config.txt's lines (SDL's file functions: UTF-8 paths on every system).
std::vector<std::string> configLines() {
    std::vector<std::string> lines;
    size_t size = 0;
    char* text = configPath().empty() ? nullptr : static_cast<char*>(SDL_LoadFile(configPath().c_str(), &size));
    if (!text) return lines;
    std::istringstream in(std::string(text, size));
    SDL_free(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

std::string loadRememberedPath() {
    for (const std::string& line : configLines())
        if (line.rfind("data=", 0) == 0 && SDL_GetPathInfo(line.c_str() + 5, nullptr)) return line.substr(5);
#ifdef __ANDROID__
    return findIsoIn(androidFolder());
#else
    const char* base = SDL_GetBasePath();
    return base ? findIsoIn(base) : std::string();
#endif
}

void rememberPath(const std::string& path) {
    if (configPath().empty()) return;
    std::string text = "data=" + path + "\n";
    for (const std::string& line : configLines())
        if (line.rfind("data=", 0) != 0) text += line + "\n";
    if (!SDL_SaveFile(configPath().c_str(), text.data(), text.size()))
        SDL_Log("cannot write %s: %s", configPath().c_str(), SDL_GetError());
}

DiscPicker::~DiscPicker() {
#ifdef __ANDROID__
    if (worker_.joinable()) worker_.join();
#endif
}

void DiscPicker::open(SDL_Window* window) {
    if (busy_.exchange(true)) return;
    if (const char* forced = SDL_getenv("GD04_DIALOG")) return finish(forced, "");
    {
        std::lock_guard lock(mutex_);
        status_ = "Choose your Guitar Hero III: Legends of Rock (Xbox 360) disc image...";
    }
    static const SDL_DialogFileFilter kIso[] = {{"Xbox 360 disc image (.iso)", "iso;ISO"}};
    SDL_ShowOpenFileDialog(picked, this, window, kIso, 1, nullptr, false);
}

void DiscPicker::picked(void* self, const char* const* files, int) {
    auto* me = static_cast<DiscPicker*>(self);
    if (!files || !files[0]) return me->finish("", files ? "" : std::string("File dialog: ") + SDL_GetError());
    // Android too: the content:// document is read in place (data/random_file.cpp),
    // no 8 GB copy (GD04-028).
    me->finish(files[0], "");
}

void DiscPicker::finish(const std::string& path, const std::string& status) {
    std::lock_guard lock(mutex_);
    picked_ = path;
    status_ = status;
    busy_ = false;
}

std::string DiscPicker::status() {
    std::lock_guard lock(mutex_);
    return status_;
}

std::string DiscPicker::takePicked() {
    std::lock_guard lock(mutex_);
    std::string r;
    r.swap(picked_);
    return r;
}

