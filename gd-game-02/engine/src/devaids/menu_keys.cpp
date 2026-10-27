// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "devaids/menu_keys.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <map>

MenuKeys::MenuKeys(const std::string& text) {
    static const std::map<std::string, int> kButtons = {
        {"confirm", 6}, {"green", 6},  {"back", 4},   {"red", 4},     {"yellow", 5}, {"blue", 7},   {"orange", 2},
        {"start", 11},  {"select", 8}, {"up", 12},    {"right", 13},  {"down", 14},  {"left", 15}, {"starpower", 8}};
    size_t start = 0;
    while (start < text.size()) {
        size_t comma = text.find(',', start);
        const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? text.size() : comma + 1;
        if (item.empty() || item[0] == 's') continue;  // a song item
        const size_t colon = item.find(':');
        if (colon == std::string::npos) {
            valid_ = false;
            continue;
        }
        std::string name = item.substr(colon + 1);
        int edge = 0, player = 0;
        if (!name.empty() && (name[0] == '+' || name[0] == '-')) edge = name[0] == '+' ? 1 : -1, name = name.substr(1);
        if (name.rfind("p2", 0) == 0) player = 1, name = name.substr(2);
        const double at = std::strtod(item.c_str(), nullptr);
        if (name.size() > 1 && name[0] == 'k') {  // a real keyboard key
            const SDL_Keycode k = SDL_GetKeyFromName(name.c_str() + 1);
            if (k == SDLK_UNKNOWN) {
                valid_ = false;
                continue;
            }
            keys_.push_back({at, -1, edge, player, int(k)});
            continue;
        }
        if (name.size() > 1 && name[0] == 'j') {  // a virtual controller's button
            const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(name.c_str() + 1);
            if (b == SDL_GAMEPAD_BUTTON_INVALID) {
                valid_ = false;
                continue;
            }
            if (edge >= 0) keys_.push_back({at, -1, 1, player, 0, int(b)});
            if (edge <= 0) keys_.push_back({edge < 0 ? at : at + 80, -1, -1, player, 0, int(b)});
            continue;
        }
        auto it = kButtons.find(name);
        if (it == kButtons.end()) {
            valid_ = false;
            continue;
        }
        keys_.push_back({at, it->second, edge, player});
    }
    std::stable_sort(keys_.begin(), keys_.end(), [](const Key& a, const Key& b) { return a.at < b.at; });
}

std::vector<MenuKeys::Key> MenuKeys::due(double nowMs) {
    std::vector<Key> out;
    while (next_ < keys_.size() && keys_[next_].at <= nowMs) out.push_back(keys_[next_++]);
    return out;
}

std::string MenuKeys::songItems(const std::string& text) {
    std::string out;
    size_t start = 0;
    while (start < text.size()) {
        size_t comma = text.find(',', start);
        const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? text.size() : comma + 1;
        if (!item.empty() && item[0] == 's') out += (out.empty() ? "" : ",") + item;
    }
    return out;
}
