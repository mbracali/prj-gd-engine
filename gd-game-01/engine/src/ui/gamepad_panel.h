#pragma once

#include <memory>

#include "ui/select_panels.h"

class Input;

// The port's Gamepads screen (main menu, the blue fret / GAMEPADS): the
// connected controllers and the keyboard, each one's player, its bindings
// lit live while pressed, and remapping (Input's binding tables).
std::unique_ptr<NativePanel> makeGamepadPanel(SelectHost& host, Input* input);
