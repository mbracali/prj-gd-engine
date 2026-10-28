// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
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

// The settings the panel lists that gd02 puts in effect, with their values
// (the first one the default): screen_mode, max_fps, fps_counter (off / on),
// internal_res (0: the window's own, else a height in lines).
struct EnhancedSetting {
    const char* key;
    std::vector<const char*> values;
};
const std::vector<EnhancedSetting>& enhancedSettings();

}  // namespace ui
