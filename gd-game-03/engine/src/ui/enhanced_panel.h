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
// and gd02's universal Enhanced menu, in the game's fonts and colours; not in
// the original game), reached from Options: the doc's sections (MODES,
// CONTROLS, PERFORMANCE & GRAPHICS, CHEATS & ENHANCERS), collapsible, with a
// scrollbar on the left and Back / Export / Import under the list, check
// boxes and dropdowns, over a still shot of the second venue with the band
// posed (UiHost::menuBackdrop), the focused setting's explanation in a box on
// the right (guidelines/universal_launcher.md). Every change is saved and
// applies at once. Settings gd03 does not have yet (the port's visual
// effects among them) are listed greyed, "not available yet", and cannot be
// chosen. CHEATS & ENHANCERS has the game's own cheats (the main menu's
// button codes). Defined in the port's DTA (ui/port_screens.cpp) as
// {new EnhancedPanel ...}; it opens on the next frame (no transition).
std::unique_ptr<ScriptObject> makeEnhancedPanel(UiManager& ui, const std::string& name);

// The settings gd03 puts in effect (enhanced.json), with their values (the
// first one the default, the original game's look, GD03-019): mouse (on /
// off), screen_mode, internal_res (0: the window's own, else a height in
// lines), widescreen (off: the PS2's 4:3 pillarboxed, GD03-016), max_fps
// (60, 120, 240, unlimited), fps_counter (off / on).
struct EnhancedSetting {
    const char* key;
    std::vector<const char*> values;
};
const std::vector<EnhancedSetting>& enhancedSettings();
const EnhancedSetting* enhancedSetting(const std::string& key);
// Whether `value` is one the setting takes.
bool enhancedValid(const EnhancedSetting& s, const std::string& value);

// Enhanced > MODES (gd01's / gd02's enhancedPreset): the values one mode
// sets at once (steam_deck, original, clean, max_hd, max_4k, budget,
// good_old_days), limited to the settings gd03 has (its effects are not in
// gd03 yet, so every mode keeps them off); anything a mode leaves out keeps
// its value. Empty for an unknown mode.
std::vector<std::pair<std::string, std::string>> enhancedPreset(const std::string& mode);
// enhanced.json (or a user settings file) from 0.0.1: the removed XInput /
// Rock Band switches (always on) and Ground Fog / Light Streaks (removed
// from every game) are dropped; unknown keys and values are left for the
// validation (they fall back to the default).
void migrateEnhanced(Json& e);

}  // namespace ui
