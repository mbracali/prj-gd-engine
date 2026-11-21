// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Development aids, from the environment (as gd10's GD10_KEYS / GD10_SCREENSHOT):
//   GD24_KEYS="<ms>:<key>,..."      scripted key presses (SDL key names: Down,
//                                   Return, Escape, F11, Tab, ...) at those times
//   GD24_SCREENSHOT=<file.bmp>      saves the picture at GD24_SCREENSHOT_AT=<ms>[,<ms>...]
//                                   (more than one: <file>_<ms>.bmp)
//   GD24_QUIT_AFTER=<ms>            quits then
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

class Canvas;

class DevAids {
public:
    DevAids();
    std::vector<SDL_Event> keysDue(uint64_t ms);
    void afterDraw(uint64_t ms, const Canvas& canvas);
    bool quitDue(uint64_t ms) const { return quitMs_ && ms >= quitMs_; }

private:
    struct Key {
        uint64_t ms;
        SDL_Keycode key;
    };
    std::vector<Key> keys_;
    std::vector<uint64_t> shots_;
    std::string shotFile_;
    bool multi_ = false;  // more than one screenshot: the time in each name
    uint64_t quitMs_ = 0;
};
