// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <vector>

namespace ui {

// The Developer Keyboard Cheats the Enhanced menu lists (GD02-160, #103):
// a few of GH2's quick_cheats keyboard entries (config/cheats.dta). The key
// is the character typed, so case matters: 'P' is Shift+P. `disc` is the
// entry's own name on the disc; uitest checks every row against it.
struct DevKeyCheat {
    char key;
    const char* disc;   // the name in cheats.dta
    const char* label;  // what the help shows
};

inline const std::vector<DevKeyCheat>& devKeyCheats() {
    static const std::vector<DevKeyCheat> k = {
        {'p', "Toggle Autoplay", "Autoplay on / off"},
        {'P', "Cycle Track Speed", "Next song speed"},
        {'w', "Automatically Win", "Win the song (3 stars)"},
        {'l', "Automatically Lose", "Lose the song"},
        {'S', "Fill StarPower Meter", "Fill Star Power"},
        {'e', "Lower Excitement Level", "Lower the crowd meter"},
        {'E', "Raise Excitement Level", "Raise the crowd meter"},
        {'+', "Force Encore", "Force the encore"},
        {'c', "Next Alphabetical Camera", "Next camera"},
        {'V', "Hold Camera on Current Shot", "Hold the camera"},
        {'h', "Hide HUD & Fretboard", "Hide HUD and fretboard"},
        {'H', "Hide HUD Only", "Hide the HUD only"},
        {'A', "(META) Access All (GAME) Toggle Crowd Audio", "Menus: unlock everything"},
    };
    return k;
}

// The option's help: a short intro, then one key per line.
inline const char* devKeyCheatsHelp() {
    static const std::string s = [] {
        std::string h =
            "GH2's own debug keys from its development kit, off in the retail game. A key not bound to a control runs "
            "its cheat. Letters are the keys alone; Shift+ means hold Shift.\n";
        for (const DevKeyCheat& c : devKeyCheats()) {
            // Keycaps: 'p' is P, 'P' is Shift+P (a lowercase l reads as I in GH2's font).
            const bool upper = c.key >= 'A' && c.key <= 'Z', lower = c.key >= 'a' && c.key <= 'z';
            const char cap = lower ? char(c.key - 'a' + 'A') : c.key;
            h += "\n" + (upper ? std::string("Shift+") : std::string()) + cap + " - " + c.label;
        }
        return h;
    }();
    return s.c_str();
}

}  // namespace ui
