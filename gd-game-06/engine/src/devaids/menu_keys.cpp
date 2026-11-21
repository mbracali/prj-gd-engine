// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "devaids/menu_keys.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>
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
        if (name == "wheelup" || name == "wheeldown") {
            Key k{at, -1, 0, player};
            k.mouse = name == "wheelup" ? 4 : 5;
            keys_.push_back(k);
            continue;
        }
        for (const auto& [verb, kind] : {std::pair<const char*, int>{"hover", 1}, {"click", 2}, {"rclick", 3}}) {
            const size_t n = std::strlen(verb);
            if (name.compare(0, n, verb) != 0 || name.find('_') == std::string::npos) continue;
            Key k{at, -1, 0, player};
            k.mouse = kind;
            k.mx = float(std::atof(name.c_str() + n));
            k.my = float(std::atof(name.c_str() + name.find('_') + 1));
            keys_.push_back(k);
            name.clear();
            break;
        }
        if (name.empty()) continue;
        if (const size_t eq = name.find('='); name.size() > 1 && name[0] == 'j' && eq != std::string::npos) {  // a virtual axis
            const std::string an = name.substr(1, eq - 1);
            // "a<N>": the controller's own axis N (a virtual pad's extra axes).
            const int a = an.size() > 1 && an[0] == 'a' && std::isdigit(static_cast<unsigned char>(an[1])) ? std::atoi(an.c_str() + 1)
                                                                                                            : int(SDL_GetGamepadAxisFromString(an.c_str()));
            if (a < 0) {
                valid_ = false;
                continue;
            }
            Key k{at, -1, 0, player};
            k.padAxis = int(a), k.axisValue = std::atoi(name.c_str() + eq + 1);
            keys_.push_back(k);
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
