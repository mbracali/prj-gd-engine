// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <vector>

// Development aid: GD02_KEYS's menu items (gd01's GD01_KEYS), at wall-clock
// times since the menus started: "<ms>:<key>", with key one of confirm,
// back, up, down, left, right, start, select, green, red, yellow, blue,
// orange (green = confirm = kPad_X, red = back = kPad_Tri, yellow / blue /
// orange = kPad_Circle / kPad_Square / kPad_L1); "+key" only presses it,
// "-key" only releases it (holds, for the menus' key repeat); "p2key" is
// player 2. Items written "s<song ms>:<key>" are song items (game/
// key_script.h) and are skipped here.
class MenuKeys {
public:
    struct Key {
        double at;
        int button;  // kPad_*
        int edge;    // 0 press and release, 1 press, -1 release
        int player;
    };
    MenuKeys() = default;
    explicit MenuKeys(const std::string& text);
    bool valid() const { return valid_; }
    // The keys due by `nowMs` (each once, in order).
    std::vector<Key> due(double nowMs);

    // The song items ("s<ms>:..."), for KeyScript.
    static std::string songItems(const std::string& text);

private:
    std::vector<Key> keys_;
    size_t next_ = 0;
    bool valid_ = true;
};
