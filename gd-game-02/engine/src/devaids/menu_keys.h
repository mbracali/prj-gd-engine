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
// key_script.h) and are skipped here. "k<SDL key name>" (kTab, kReturn,
// kEscape, k1, kUp) is a real keyboard key and "j<SDL gamepad button>"
// (ja, jb, jstart, jdpup) a button of GD02_VIRTUAL_PAD's controller: both
// go through the input layer (bindings, key repeat, the frets' menu
// buttons), not straight to the menus; a "j" press is released 80 ms on;
// "j<SDL gamepad axis>=<value>" (jleftx=-20000, jrighttrigger=0; ja6=0 the
// controller's own axis 6) moves an axis of that controller and leaves it there. "hover<x>_<y>" /
// "click<x>_<y>" (menu units, 1280 x 720) move / left-click the mouse,
// "rclick<x>_<y>" right-clicks, "wheelup" / "wheeldown" scroll (gd01's).
class MenuKeys {
public:
    struct Key {
        double at;
        int button;  // kPad_*
        int edge;    // 0 press and release, 1 press, -1 release
        int player;
        int keycode = 0;   // "k": an SDL_Keycode (button unused)
        int padButton = -1;  // "j": an SDL_GamepadButton (button unused)
        int padAxis = -1;    // "j<axis>=<v>": an SDL_GamepadAxis set to axisValue
        int axisValue = 0;
        int mouse = 0;       // 1 hover, 2 click, 3 right click, 4 wheel up, 5 wheel down (at mx, my)
        float mx = 0, my = 0;
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
