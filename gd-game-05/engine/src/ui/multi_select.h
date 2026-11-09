// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>

#include "script/script.h"

// GH2's two-player choice screens (ui/gen/multiplayer.dta, docs/multiplayer.md
// "Menus"): MultiSelectScreen (0x822981A8 .. 0x82298720) with its
// MultiSelectPanels (0x82294B90 .. 0x82296568), one per player, and
// MultiCharSelPanel (0x8228A590 .. 0x8228B460), the two players'
// character select.
namespace ui {

class UiManager;

// MultiSelectScreen, MultiSelectPanel, MultiCharSelPanel; nullptr for other classes.
std::unique_ptr<ScriptObject> makeMultiClass(UiManager& ui, const std::string& cls, const std::string& name);

}  // namespace ui
