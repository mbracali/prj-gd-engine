// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <optional>

// The keyboard as a guitar, with gd01's default keyboard profile
// (gd-game-01/engine/src/input/input.cpp): 1-5 green..orange, Up / Down
// strum, plus Enter as a strum, Space Star Power, W whammy. gd01's full
// input/ (controllers, bindings, touch, controllers.json) comes later, with
// the Xbox 360 guitar as the reference controller (L10).
struct KeyAction {
    enum Kind { FretDown, FretUp, Strum, StarPower, WhammyDown, WhammyUp, Back } kind;
    int fret = -1;
    uint64_t timestampNs = 0;  // the event's SDL timestamp
};

// Maps one SDL event to a guitar action, if it is one. Key repeats are ignored.
std::optional<KeyAction> keyAction(const SDL_Event& e);
