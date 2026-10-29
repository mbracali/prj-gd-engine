// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "script/script.h"

namespace ui {

class UiManager;

// The port's Enhanced Options screen (gd01's Enhanced Settings, in GH2's
// fonts and colours; not in the original game), reached from Options:
// gd01's settings in its sections and order, with its check boxes and
// dropdowns, over a still shot of the second venue with the band posed
// (UiHost::menuBackdrop), the focused setting's explanation in a box on the
// right (guidelines/universal_launcher.md). Every change is saved and applies at
// once. Settings gd02 does not have yet are listed greyed, "not available
// yet", and cannot be chosen. The CHEATS section has GH2's own cheats (the
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
// macOS; the Steam Deck build (and Apply Steam Deck Settings: `deck`);
// Android. Every visual effect is off in every column, the original look
// (GD02-130); only the window, frame rate, resolution and touch differ.
// Only the profile's keys.
std::vector<std::pair<std::string, std::string>> enhancedProfile(bool deck);
// A setting's default in this build: the profile's, else the base default.
std::string enhancedDefault(const std::string& key, bool deck);

}  // namespace ui
