// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>

#include "script/script.h"

namespace ui {

class UiManager;

// The port's Gamepads screen (gd01's gamepad_panel, as is, over gd02's
// input layer): the connected controllers and the keyboard, each one's
// player, its bindings lit live while pressed, and remapping. Defined in
// the port's own DTA (ui/port_screens.cpp) as {new GamepadPanel ...};
// reached from the main menu's blue fret and from Options.
std::unique_ptr<ScriptObject> makeGamepadPanel(UiManager& ui, const std::string& name);

}  // namespace ui
