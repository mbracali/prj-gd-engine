// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>

#include "ui/select_panels.h"

class Input;

// The port's Gamepad menu (universal_mechanisms.md 1.2.1; main menu, the
// blue fret / GAMEPADS help item, Enhanced > Controls): four player boxes
// with their instrument and device, and each device's bindings (two per
// control) lit live while pressed, saved or discarded as a whole.
std::unique_ptr<NativePanel> makeGamepadPanel(SelectHost& host, Input* input);
