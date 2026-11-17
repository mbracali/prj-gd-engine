// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/inputtest.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

#include "input/input.h"

namespace {

int gFail = 0;

void check(bool ok, const std::string& what) {
    std::printf("inputtest: %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++gFail;
}

// Pumps SDL and returns the actions that went down.
std::vector<Action> pump(Input& in) {
    SDL_UpdateJoysticks();
    std::vector<Action> down;
    SDL_Event e;
    std::vector<InputEvent> out;
    while (SDL_PollEvent(&e)) {
        out.clear();
        in.handle(e, out);
        for (const InputEvent& ev : out)
            if (ev.down) down.push_back(ev.action);
    }
    return down;
}

std::string names(const std::vector<Action>& a) {
    std::string s;
    for (Action x : a) s += std::string(s.empty() ? "" : " ") + actionName(x);
    return s.empty() ? "(none)" : s;
}

void press(Input& in, SDL_Joystick* j, SDL_GamepadButton b, const std::vector<Action>& want, const std::string& what) {
    // The virtual joystick's buttons are SDL gamepad buttons in order (SDL's default mapping for virtual pads).
    SDL_SetJoystickVirtualButton(j, int(b), true);
    const auto got = pump(in);
    SDL_SetJoystickVirtualButton(j, int(b), false);
    pump(in);
    check(got == want, what + ": " + names(got) + " (want " + names(want) + ")");
}

void key(Input& in, SDL_Keycode k, const std::vector<Action>& want, const std::string& what) {
    std::vector<InputEvent> out;
    std::vector<Action> got;
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = k, e.key.down = true, e.key.timestamp = SDL_GetTicksNS();
    in.handle(e, out);
    for (const InputEvent& ev : out)
        if (ev.down) got.push_back(ev.action);
    e.type = SDL_EVENT_KEY_UP, e.key.down = false;
    in.handle(e, out);
    check(got == want, what + ": " + names(got) + " (want " + names(want) + ")");
}

}  // namespace

int inputTest() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI", "0");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0x1430/0x4748,0x045e/0x028e");
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_VIDEO)) {
        std::printf("inputtest: SDL: %s\n", SDL_GetError());
        return 1;
    }
    {
        Input in;
        const char* tmp = SDL_getenv("GD24_SAVE_DIR");
        in.setSaveDir(tmp ? std::string(tmp) + "/" : std::string("./"));
        in.resetBindings("keyboard");

        // The keyboard: PSP buttons; 2 is Up as the arrow is (one action).
        key(in, SDLK_1, {Action::L}, "key 1 is L");
        key(in, SDLK_2, {Action::Up}, "key 2 is Up");
        key(in, SDLK_3, {Action::Triangle}, "key 3 is Triangle");
        key(in, SDLK_4, {Action::R}, "key 4 is R");
        key(in, SDLK_SPACE, {Action::Cross}, "Space is Cross");
        key(in, SDLK_ESCAPE, {Action::Circle}, "Esc is Circle (helper)");
        key(in, SDLK_RETURN, {Action::Start}, "Enter is Start");
        key(in, SDLK_TAB, {Action::Select}, "Tab is Select");
        key(in, SDLK_UP, {Action::Up}, "the Up arrow: Up once (General Up and the helper)");

        // A virtual Xbox 360 pad: the PSP's buttons where they sit.
        const SDL_JoystickID padId = Input::attachVirtualPad("045e:028e");
        SDL_Joystick* pad = SDL_OpenJoystick(padId);
        pump(in);
        SDL_Delay(350);  // the pad settles
        pump(in);
        check(in.devices().size() == 2, "the virtual gamepad is listed (" + std::to_string(in.devices().size()) + " devices)");
        press(in, pad, SDL_GAMEPAD_BUTTON_SOUTH, {Action::Cross}, "pad south is Cross");
        press(in, pad, SDL_GAMEPAD_BUTTON_EAST, {Action::Circle}, "pad east is Circle");
        press(in, pad, SDL_GAMEPAD_BUTTON_WEST, {Action::Square}, "pad west is Square");
        press(in, pad, SDL_GAMEPAD_BUTTON_NORTH, {Action::Triangle}, "pad north is Triangle");
        press(in, pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, {Action::L}, "pad LB is L");
        press(in, pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, {Action::R}, "pad RB is R");
        press(in, pad, SDL_GAMEPAD_BUTTON_DPAD_UP, {Action::Up}, "pad d-pad up is Up");
        press(in, pad, SDL_GAMEPAD_BUTTON_START, {Action::Start}, "pad Start is Start");
        press(in, pad, SDL_GAMEPAD_BUTTON_BACK, {Action::Select}, "pad Back is Select");
        check(in.instrument("pad:" + std::to_string(padId)) == Input::Instrument::Gamepad, "the pad is a Gamepad");

        // A virtual Guitar Hero Xbox 360 guitar.
        const SDL_JoystickID gId = Input::attachVirtualPad("1430:4748:guitar");
        if (!gId || !SDL_IsGamepad(gId)) std::printf("inputtest: virtual guitar id %u, gamepad %d: %s\n", unsigned(gId), gId ? int(SDL_IsGamepad(gId)) : 0, SDL_GetError());
        SDL_Joystick* gtr = SDL_OpenJoystick(gId);
        pump(in);
        SDL_Delay(350);
        pump(in);
        const std::string gid = "pad:" + std::to_string(gId);
        check(in.instrument(gid) == Input::Instrument::Guitar, "the guitar is a 5 fret guitar");
        press(in, gtr, SDL_GAMEPAD_BUTTON_SOUTH, {Action::Cross}, "menus: green is Cross");
        press(in, gtr, SDL_GAMEPAD_BUTTON_EAST, {Action::Circle}, "menus: red is Circle");
        press(in, gtr, SDL_GAMEPAD_BUTTON_DPAD_DOWN, {Action::Down}, "menus: strum down is Down");
        in.setInGame(true);
        press(in, gtr, SDL_GAMEPAD_BUTTON_SOUTH, {Action::Lane0}, "song: green is lane 1");
        press(in, gtr, SDL_GAMEPAD_BUTTON_EAST, {Action::Lane1}, "song: red is lane 2");
        press(in, gtr, SDL_GAMEPAD_BUTTON_NORTH, {Action::Lane2}, "song: yellow is lane 3");
        press(in, gtr, SDL_GAMEPAD_BUTTON_WEST, {Action::Lane3}, "song: blue is lane 4");
        press(in, gtr, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, {Action::Overdrive}, "song: orange is Overdrive");
        in.setInGame(false);
        // The lane table [I]: L Up Triangle R.
        check(gameplayOf(Action::L) == Action::Lane0 && gameplayOf(Action::Up) == Action::Lane1 &&
                  gameplayOf(Action::Triangle) == Action::Lane2 && gameplayOf(Action::R) == Action::Lane3,
              "PSP L / Up / Triangle / R play lanes 1..4");

        // Rebinding: Triangle on the keyboard's T in slot 2, then reset.
        in.beginEdit("keyboard");
        in.beginCapture("keyboard", Input::Control::Triangle, 1);
        key(in, SDLK_T, {}, "capture takes T (no action)");
        key(in, SDLK_T, {Action::Triangle}, "T is Triangle after the capture");
        in.discardEdit();
        key(in, SDLK_T, {}, "Discard: T does nothing again");

        SDL_CloseJoystick(gtr);
        SDL_CloseJoystick(pad);
        SDL_DetachVirtualJoystick(gId);
        SDL_DetachVirtualJoystick(padId);
        pump(in);
        in.close();
    }
    SDL_Quit();
    std::printf("inputtest: %d failures\n", gFail);
    return gFail ? 1 : 0;
}
