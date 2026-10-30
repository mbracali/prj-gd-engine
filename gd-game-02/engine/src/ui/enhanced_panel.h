// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "data/json.h"
#include "script/script.h"

namespace ui {

class UiManager;

// The port's Enhanced Options screen (universal_mechanisms.md 1.2.2; gd01's
// Enhanced Settings, in GH2's fonts and colours; not in the original game),
// reached from Options: the doc's sections (MODES, CONTROLS, PERFORMANCE &
// GRAPHICS, CHEATS & ENHANCERS), collapsible, with a scrollbar on the left
// and Back / Export / Import under the list, check boxes and dropdowns, over
// a still shot of a venue with the band posed (UiHost::menuBackdrop), the
// focused setting's explanation in a box on the right
// (guidelines/universal_launcher.md). Every change is saved and applies at
// once. Settings gd02 does not have yet are listed greyed, "not available
// yet", and cannot be chosen. CHEATS & ENHANCERS has GH2's own cheats (the
// main menu's button codes, config/long_cheats.dta). Defined in the port's
// DTA (ui/port_screens.cpp) as {new EnhancedPanel ...}.
std::unique_ptr<ScriptObject> makeEnhancedPanel(UiManager& ui, const std::string& name);

// The settings the panel lists (enhanced.json, gd01's keys), with their
// values: a choice's, "off" / "on" for a check box, or none for a number
// (ms) from `lo` to `hi` (the calibration's offsets). The first value is the
// setting's base default (gd01's enhancedDefaults); the build's profile
// (enhancedDefault) wins over it.
struct EnhancedSetting {
    const char* key;
    std::vector<const char*> values;
    int lo = 0, hi = 0;
};
const std::vector<EnhancedSetting>& enhancedSettings();
const EnhancedSetting* enhancedSetting(const std::string& key);
// Whether `value` is one the setting takes.
bool enhancedValid(const EnhancedSetting& s, const std::string& value);

// The graphics, enhancer and control defaults each download ships with, in
// one table (laid out as gd01's profileValues): desktop Linux, Windows and
// macOS; the Steam Deck build (`deck`); Android. Every visual effect is off
// in every column, the original look (GD02-130, the "Original feel" mode);
// only the window, frame rate, resolution and touch differ. Only the
// profile's keys.
std::vector<std::pair<std::string, std::string>> enhancedProfile(bool deck);
// Enhanced > MODES (gd01's enhancedPreset): the values one mode sets at once
// (steam_deck, original, clean, max_hd, max_4k, budget, good_old_days);
// anything it leaves out keeps its value. Empty for an unknown mode.
std::vector<std::pair<std::string, std::string>> enhancedPreset(const std::string& mode);
// enhanced.json (or a user settings file) saved by 0.1.7 and older takes the
// new values (check boxes that became ranges, renamed levels); the removed
// XInput / Rock Band switches are dropped. Unknown values are left for the
// validation (they fall back to the default).
void migrateEnhanced(Json& e);
// A setting's default in this build: the profile's, else the base default.
std::string enhancedDefault(const std::string& key, bool deck);

}  // namespace ui
