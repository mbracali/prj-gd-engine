// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "devaids/dev_aids.h"

#include <cstdlib>
#include <sstream>

#include "render/canvas.h"

DevAids::DevAids() {
    if (const char* k = SDL_getenv("GD24_KEYS")) {
        std::stringstream ss(k);
        std::string item;
        while (std::getline(ss, item, ',')) {
            const size_t c = item.find(':');
            if (c == std::string::npos) continue;
            const SDL_Keycode key = SDL_GetKeyFromName(item.substr(c + 1).c_str());
            if (key == SDLK_UNKNOWN) SDL_Log("GD24_KEYS: unknown key %s", item.substr(c + 1).c_str());
            else keys_.push_back({std::strtoull(item.c_str(), nullptr, 10), key});
        }
    }
    if (const char* f = SDL_getenv("GD24_SCREENSHOT")) {
        shotFile_ = f;
        std::stringstream ss(SDL_getenv("GD24_SCREENSHOT_AT") ? SDL_getenv("GD24_SCREENSHOT_AT") : "1000");
        std::string item;
        while (std::getline(ss, item, ',')) shots_.push_back(std::strtoull(item.c_str(), nullptr, 10));
        multi_ = shots_.size() > 1;
    }
    if (const char* q = SDL_getenv("GD24_QUIT_AFTER")) quitMs_ = std::strtoull(q, nullptr, 10);
}

std::vector<SDL_Event> DevAids::keysDue(uint64_t ms) {
    std::vector<SDL_Event> out;
    for (auto it = keys_.begin(); it != keys_.end();) {
        if (it->ms > ms) {
            ++it;
            continue;
        }
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = it->key;
        e.key.down = true;
        e.key.timestamp = SDL_GetTicksNS();
        out.push_back(e);
        e.type = SDL_EVENT_KEY_UP;
        e.key.down = false;
        out.push_back(e);
        it = keys_.erase(it);
    }
    return out;
}

void DevAids::afterDraw(uint64_t ms, const Canvas& canvas) {
    for (auto it = shots_.begin(); it != shots_.end();) {
        if (*it > ms) {
            ++it;
            continue;
        }
        std::string path = shotFile_;
        if (multi_) {
            const size_t dot = path.rfind('.');
            path = path.substr(0, dot) + "_" + std::to_string(*it) + (dot == std::string::npos ? ".bmp" : path.substr(dot));
        }
        if (canvas.save(path)) SDL_Log("GD24_SCREENSHOT: saved %s at %llu ms", path.c_str(), static_cast<unsigned long long>(ms));
        else SDL_Log("GD24_SCREENSHOT: cannot save %s: %s", path.c_str(), SDL_GetError());
        it = shots_.erase(it);
    }
}
