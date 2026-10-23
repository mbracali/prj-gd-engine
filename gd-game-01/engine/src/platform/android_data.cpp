// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "platform/android_data.h"
#include "platform/paths.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <vector>

AndroidIsoImport::~AndroidIsoImport() {
    if (worker_.joinable()) worker_.join();
}

std::string AndroidIsoImport::folder() {
    const char* ext = SDL_GetAndroidExternalStoragePath();
    if (ext && *ext) return std::string(ext) + "/";
    return prefDir();
}

std::string AndroidIsoImport::findIso() {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(folder(), ec)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (e.is_regular_file(ec) && ext == ".iso") return e.path().string();
    }
    return {};
}

void AndroidIsoImport::pick(SDL_Window* window) {
    if (busy_) return;
    busy_ = true;
    {
        std::lock_guard lock(mutex_);
        status_ = "Choose your Guitar Hero (USA) disc image...";
    }
    static const SDL_DialogFileFilter kIso[] = {{"PS2 disc image (.iso)", "iso"}};
    SDL_ShowOpenFileDialog(picked, this, window, kIso, 1, nullptr, false);
}

void AndroidIsoImport::picked(void* self, const char* const* files, int) {
    auto* me = static_cast<AndroidIsoImport*>(self);
    if (!files || !files[0]) {
        std::lock_guard lock(me->mutex_);
        me->status_ = files ? "" : std::string("File picker: ") + SDL_GetError();
        me->busy_ = false;
        return;
    }
    if (me->worker_.joinable()) me->worker_.join();
    me->worker_ = std::thread(&AndroidIsoImport::copy, me, std::string(files[0]));
}

void AndroidIsoImport::copy(std::string uri) {
    auto fail = [&](const std::string& why) {
        std::lock_guard lock(mutex_);
        status_ = why;
        busy_ = false;
    };
    SDL_IOStream* in = SDL_IOFromFile(uri.c_str(), "rb");  // SDL reads content:// URIs on Android
    if (!in) return fail(std::string("Cannot open the chosen file: ") + SDL_GetError());
    std::string dest = folder() + "game.iso", part = dest + ".part";
    std::FILE* out = std::fopen(part.c_str(), "wb");
    if (!out) {
        SDL_CloseIO(in);
        return fail("Cannot write " + part);
    }
    total_ = SDL_GetIOSize(in);
    done_ = 0;
    std::vector<char> buf(4 << 20);
    bool ok = true;
    for (;;) {
        size_t n = SDL_ReadIO(in, buf.data(), buf.size());
        if (n == 0) {
            ok = SDL_GetIOStatus(in) == SDL_IO_STATUS_EOF;
            break;
        }
        if (std::fwrite(buf.data(), 1, n, out) != n) {
            ok = false;
            break;
        }
        done_ += (long long)n;
    }
    SDL_CloseIO(in);
    ok = std::fclose(out) == 0 && ok;
    std::error_code ec;
    if (ok) std::filesystem::rename(part, dest, ec);
    if (!ok || ec) {
        std::filesystem::remove(part, ec);
        return fail("Copying the disc image failed (is there enough free space?)");
    }
    std::lock_guard lock(mutex_);
    status_.clear();
    ready_ = dest;
    busy_ = false;
}

std::string AndroidIsoImport::status() {
    if (busy_ && total_ > 0) {
        char line[96];
        std::snprintf(line, sizeof line, "Copying the disc image... %lld / %lld MB", done_ / 1048576, (long long)total_ / 1048576);
        return line;
    }
    std::lock_guard lock(mutex_);
    return status_;
}

std::string AndroidIsoImport::takeReady() {
    std::lock_guard lock(mutex_);
    std::string r = ready_;
    ready_.clear();
    if (!r.empty() && worker_.joinable()) worker_.join();
    return r;
}
