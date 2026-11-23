// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "Custom songs" overlay (additive-songs mod, owner §7, GDALL-042/070): a
// semi-transparent menu opened with the yellow fret on the Quick Play song
// screen. It lists the shared games' songs (and later custom songs) by game, as
// collapsible folders with instrument markers; selecting one hands off to the
// difficulty screen so the normal Quick Play flow plays it. Drawn with PortDraw,
// the same look as the Enhanced / GAMEPADS port menus.
#pragma once

#include <memory>
#include <string>

#include "ui/ui_objects.h"

namespace ui {

// Registered as the DTA class "CustomSongsPanel" (ui/panels.cpp factory).
std::unique_ptr<ScriptObject> makeCustomSongsPanel(UiManager& ui, const std::string& name);

}  // namespace ui
