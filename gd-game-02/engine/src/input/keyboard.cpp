// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "input/keyboard.h"

std::optional<KeyAction> keyAction(const SDL_Event& e) {
    if (e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_KEY_UP) return std::nullopt;
    if (e.key.repeat) return std::nullopt;
    const bool down = e.type == SDL_EVENT_KEY_DOWN;
    KeyAction a{KeyAction::Back, -1, e.key.timestamp};
    switch (e.key.key) {
    case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5:
        a.kind = down ? KeyAction::FretDown : KeyAction::FretUp;
        a.fret = int(e.key.key - SDLK_1);
        return a;
    case SDLK_UP: case SDLK_DOWN: case SDLK_RETURN: case SDLK_KP_ENTER:
        if (!down) return std::nullopt;  // the strum bar strums on the press
        a.kind = KeyAction::Strum;
        return a;
    case SDLK_SPACE:
        if (!down) return std::nullopt;
        a.kind = KeyAction::StarPower;
        return a;
    case SDLK_W:
        a.kind = down ? KeyAction::WhammyDown : KeyAction::WhammyUp;
        return a;
    case SDLK_ESCAPE: case SDLK_BACKSPACE:
        if (!down) return std::nullopt;
        a.kind = KeyAction::Back;
        return a;
    default:
        return std::nullopt;
    }
}
