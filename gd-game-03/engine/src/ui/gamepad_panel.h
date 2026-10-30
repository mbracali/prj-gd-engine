// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>

#include "script/script.h"

namespace ui {

class UiManager;

// The port's Gamepad menu (universal_mechanisms.md 1.2.1, gd01's
// gamepad_panel over gd03's input layer): four player boxes with their
// instrument and device, and each device's bindings (two per control) lit
// live while pressed, saved or discarded as a whole. Defined in the port's
// own DTA (ui/port_screens.cpp) as {new GamepadPanel ...}; reached from the
// main menu (the yellow fret / the help bar's GAMEPADS) and Enhanced Options
// (CONTROLS > Gamepads).
std::unique_ptr<ScriptObject> makeGamepadPanel(UiManager& ui, const std::string& name);

}  // namespace ui
