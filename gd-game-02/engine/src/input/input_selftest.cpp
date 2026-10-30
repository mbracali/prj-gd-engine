// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "input/input_selftest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "input/input.h"
#include "platform/paths.h"

namespace {

struct Checker {
    int failures = 0;
    void operator()(bool ok, const char* what) {
        if (!ok) ++failures;
        std::printf("  input: %s %s\n", ok ? "ok  " : "FAIL", what);
    }
};

void pump(Input& in, std::vector<InputEvent>& out) {
    SDL_UpdateJoysticks();
    SDL_Event e;
    while (SDL_PollEvent(&e)) in.handle(e, out);
}

bool has(const std::vector<InputEvent>& evs, Action a, bool down, ControllerMode mode) {
    for (const InputEvent& e : evs)
        if (e.action == a && e.down == down && e.mode == mode) return true;
    return false;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

int runInputChecks() {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        std::printf("  input: skipped (no gamepad subsystem: %s)\n", SDL_GetError());
        return 0;
    }
    Checker check;
    // Its own save folder, so the player's controllers.json and report stay untouched.
    const std::string dir = (prefDir().empty() ? std::string("./") : prefDir()) + "selftest-input/";
    SDL_CreateDirectory(dir.c_str());
    std::remove((dir + "controllers.json").c_str());
    {
        // The universal controls on the keyboard's defaults (universal_mechanisms.md
        // 1.1.4 / 1.2.1, gd01's input self-test): no double inputs where two
        // controls share a key, none missed, the keyboard's Start selects in
        // the menus, two slots per control, an edit saved or discarded whole.
        Input in;
        in.setSaveDir(dir);
        std::vector<InputEvent> out;
        uint64_t t = SDL_GetTicksNS();
        auto key = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = k, e.key.down = down, e.key.timestamp = t += 200000000ull;
            in.handle(e, out);
        };
        auto count = [&](Action a, bool down) {
            return std::count_if(out.begin(), out.end(), [&](const InputEvent& e) { return e.action == a && e.down == down; });
        };
        key(SDLK_UP, true);
        check(count(Action::Up, true) == 1 && count(Action::Strum, true) == 1 && out.size() == 2, "Up arrow: Up and Strum once");
        out.clear();
        key(SDLK_UP, false);
        check(count(Action::Up, false) == 1 && count(Action::Strum, false) == 1 && out.size() == 2, "... released once");
        out.clear();
        key(SDLK_RETURN, true);
        check(out.size() == 1 && out[0].action == Action::Confirm && out[0].enterKey, "Enter (both Starts): one select in the menus");
        key(SDLK_RETURN, false);
        out.clear();
        in.setInGame(true);
        key(SDLK_RETURN, true);
        check(out.size() == 1 && out[0].action == Action::Start, "... and Start (pause) in a song");
        key(SDLK_RETURN, false);
        in.setInGame(false);
        out.clear();
        key(SDLK_1, true), key(SDLK_F1, true);
        check(count(Action::Fret0, true) == 1 && out.size() == 1, "1 + F1: green pressed once");
        out.clear();
        key(SDLK_1, false);
        check(out.empty(), "1 released, F1 held: green stays");
        key(SDLK_F1, false);
        check(count(Action::Fret0, false) == 1, "F1 released: green released");
        out.clear();
        key(SDLK_TAB, true);
        check(count(Action::StarPower, true) == 1 && out.size() == 1, "Tab (Select, Select, gyro): Star Power once");
        key(SDLK_TAB, false);
        out.clear();
        key(SDLK_UP, true), key(SDLK_DOWN, true);
        check(count(Action::Strum, true) == 2 && count(Action::Down, true) == 1, "Up then Down: two strums (none missed)");
        key(SDLK_UP, false), key(SDLK_DOWN, false);
        check(count(Action::Strum, false) == 2 && count(Action::Up, false) == 1 && count(Action::Down, false) == 1, "... both released");
        out.clear();
        key(SDLK_BACKSPACE, true);
        check(count(Action::Back, true) == 1, "Backspace: back");
        key(SDLK_BACKSPACE, false);
        out.clear();
        // Customize inputs: Key 2 of the green fret to G, then discarded.
        using C = Input::Control;
        in.beginEdit("keyboard");
        in.beginCapture("keyboard", C::Green, 1);
        key(SDLK_G, true), key(SDLK_G, false);
        const auto& g = in.bindings("keyboard")[size_t(C::Green)];
        check(g.size() == 2 && g[0].index == int(SDLK_1) && g[1].index == int(SDLK_G), "Key 2 of the green fret captured (Key 1 kept)");
        out.clear();
        key(SDLK_G, true);
        check(count(Action::Fret0, true) == 1, "... and plays at once");
        key(SDLK_G, false);
        in.discardEdit();
        check(in.bindings("keyboard")[size_t(C::Green)].size() == 2 && in.bindings("keyboard")[size_t(C::Green)][1].index == int(SDLK_F1) &&
                  readFile(dir + "controllers.json").empty(),
              "... discarded: F1 back, nothing written");
        in.beginEdit("keyboard");
        in.setRequireStrum("keyboard", false);
        in.commitEdit();
        check(!in.requireStrum("keyboard") && readFile(dir + "controllers.json").find("\"require_strum\": false") != std::string::npos,
              "Require Strum off saved on Save");
        out.clear();
        key(SDLK_2, true);
        check(out.size() == 1 && out[0].action == Action::Fret1 && out[0].mode == ControllerMode::Joypad, "... a fret then also strums (joypad mode)");
        key(SDLK_2, false);
    }
    {
        // controllers.json of 0.1.7 (version 1): its keyboard bindings carry over.
        std::ofstream(dir + "controllers.json")
            << R"({"devices":{"keyboard":{"frets_strum":true,"bindings":{"green":[{"kind":"key","index":103}],)"
               R"("start":[{"kind":"key","index":9}],"star_power":[{"kind":"key","index":32}],"select":[{"kind":"key","index":13}],)"
               R"("back":[{"kind":"key","index":8}]}}}})";
        Input in;
        in.setSaveDir(dir);
        std::vector<InputEvent> out;
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_UP, e.key.key = SDLK_F12;
        in.handle(e, out);  // loads controllers.json
        using C = Input::Control;
        const Input::Bindings& b = in.bindings("keyboard");
        auto only = [&](C c, SDL_Keycode k) { return b[size_t(c)].size() == 1 && b[size_t(c)][0].index == int(k); };
        check(only(C::Green, SDLK_G) && only(C::Start, SDLK_TAB) && only(C::GuitarStart, SDLK_TAB) && only(C::Gyro, SDLK_SPACE) &&
                  only(C::Select, SDLK_SPACE) && only(C::StrumUp, SDLK_UP) && only(C::MenuUp, SDLK_UP) && !in.requireStrum("keyboard"),
              "controllers.json version 1 migrated (green, Start, Star Power, frets also strum)");
        in.setPlayerEnabled(1, false);
        const std::string saved = readFile(dir + "controllers.json");
        check(saved.find("\"version\": 2") != std::string::npos && saved.find("guitar_start") != std::string::npos &&
                  saved.find("\"player2\": false") != std::string::npos && !in.playerEnabled(1),
              "... saved again as version 2 (player 2 off)");
        std::remove((dir + "controllers.json").c_str());
    }
    {
        Input in;
        in.setSaveDir(dir);
        std::vector<InputEvent> evs;
        pump(in, evs);
        const SDL_JoystickID guitarId = Input::attachVirtualPad("1430:4748:guitar");
        const SDL_JoystickID padId = Input::attachVirtualPad("045e:028e");
        // Input opens each one on its SDL_EVENT_GAMEPAD_ADDED.
        SDL_Joystick *guitar = nullptr, *pad = nullptr;
        for (int k = 0; k < 100 && !(guitar && pad); ++k) {
            pump(in, evs);
            guitar = SDL_GetJoystickFromID(guitarId), pad = SDL_GetJoystickFromID(padId);
            if (!(guitar && pad)) SDL_Delay(10);
        }
        check(guitar && pad, "two virtual controllers open as gamepads");
        if (!guitar || !pad) {
            in.close();
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
            return check.failures;
        }
        const std::string g = "pad:" + std::to_string(guitarId), p = "pad:" + std::to_string(padId);
        check(in.deviceType(g) == "Auto (Xbox 360 guitar)", "the XInput guitar is an Xbox 360 guitar");
        check(in.gh2Type(g) == "hx_guitar_xbox_new", "... GH2's hx_guitar_xbox_new");
        check(in.deviceType(p) == "Auto (Gamepad)" && in.gh2Type(p) == "joypad_guitar", "the Xbox 360 pad is GH2's joypad_guitar");
        check(in.devices().size() == 4, "devices(): keyboard, touch and the two pads");

        // The whammy bar rests at -32768: read as the rest during the 300 ms settle.
        SDL_SetJoystickVirtualAxis(guitar, SDL_GAMEPAD_AXIS_RIGHTX, -32768);
        pump(in, evs);
        SDL_Delay(350);
        pump(in, evs);
        evs.clear();

        SDL_SetJoystickVirtualButton(guitar, SDL_GAMEPAD_BUTTON_NORTH, true);  // Y: yellow
        pump(in, evs);
        check(has(evs, Action::Fret2, true, ControllerMode::Guitar), "guitar Y is the yellow fret (hx_guitar_xbox_new)");
        evs.clear();
        SDL_SetJoystickVirtualButton(guitar, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
        pump(in, evs);
        check(has(evs, Action::Strum, true, ControllerMode::Guitar), "guitar D-pad down strums");
        evs.clear();
        SDL_SetJoystickVirtualAxis(guitar, SDL_GAMEPAD_AXIS_RIGHTX, 32767);
        pump(in, evs);
        check(std::abs(in.whammyValue(-1) - 1.0f) < 0.01f && in.analogWhammy(), "whammy bar fully pressed reads 1");
        bool analog = false;
        for (const InputEvent& e : evs) analog = analog || (e.action == Action::Whammy && e.down && e.analog);
        check(analog, "... and its Whammy action is marked analog");
        evs.clear();
        SDL_SetJoystickVirtualAxis(guitar, SDL_GAMEPAD_AXIS_RIGHTY, 32767);  // tilt
        pump(in, evs);
        check(has(evs, Action::StarPower, true, ControllerMode::Guitar), "guitar tilt deploys Star Power");
        evs.clear();
        SDL_SetJoystickVirtualButton(guitar, SDL_GAMEPAD_BUTTON_START, true);
        pump(in, evs);
        check(has(evs, Action::Start, true, ControllerMode::Guitar), "guitar Start is Start (pause)");
        SDL_SetJoystickVirtualButton(guitar, SDL_GAMEPAD_BUTTON_START, false);
        SDL_SetJoystickVirtualButton(guitar, SDL_GAMEPAD_BUTTON_NORTH, false);
        SDL_SetJoystickVirtualButton(guitar, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
        SDL_SetJoystickVirtualAxis(guitar, SDL_GAMEPAD_AXIS_RIGHTX, -32768);
        SDL_SetJoystickVirtualAxis(guitar, SDL_GAMEPAD_AXIS_RIGHTY, 0);
        pump(in, evs);
        evs.clear();

        // In the menus the guitar's green fret is the green fret, once (the
        // menus read it as kPad_X, UiManager::action), not also Confirm.
        SDL_SetJoystickVirtualButton(guitar, SDL_GAMEPAD_BUTTON_SOUTH, true);
        pump(in, evs);
        int greens = 0;
        for (const InputEvent& e : evs) greens += e.down && (e.action == Action::Fret0 || e.action == Action::Confirm) && !e.joypad;
        check(has(evs, Action::Fret0, true, ControllerMode::Guitar) && greens == 1, "guitar green is one green press in the menus");
        SDL_SetJoystickVirtualButton(guitar, SDL_GAMEPAD_BUTTON_SOUTH, false);
        pump(in, evs);
        evs.clear();

        // The joypad: LT is green, each press a strum (Joypad mode).
        in.setInGame(true);
        SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
        pump(in, evs);
        check(has(evs, Action::Fret0, true, ControllerMode::Joypad), "pad LT is the green fret in joypad mode");
        bool joypadFlag = false;
        for (const InputEvent& e : evs) joypadFlag = joypadFlag || (e.action == Action::Fret0 && e.joypad);
        check(joypadFlag, "... marked as a joypad's fret (the menus skip its yellow / blue / orange)");
        check(in.lastMode() == ControllerMode::Joypad, "... and the last mode is the joypad's");
        SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
        pump(in, evs);
        evs.clear();
        in.setLefty(true, true);
        SDL_SetJoystickVirtualButton(pad, SDL_GAMEPAD_BUTTON_SOUTH, true);  // A: lefty green
        pump(in, evs);
        check(has(evs, Action::Fret0, true, ControllerMode::Joypad) && in.gh2Type(p) == "lefty_joypad_guitar",
              "lefty: pad A is green (lefty_joypad_guitar)");
        SDL_SetJoystickVirtualButton(pad, SDL_GAMEPAD_BUTTON_SOUTH, false);
        pump(in, evs);
        in.setLefty(false, false);
        in.setInGame(false);
        evs.clear();

        // Type cycling: Auto -> Xbox 360 guitar -> Gamepad, saved per model.
        in.cycleDeviceType(g, 1);
        check(in.deviceType(g) == "Xbox 360 guitar", "Type cycles to a chosen Xbox 360 guitar");
        in.cycleDeviceType(g, 1);
        check(in.deviceType(g) == "Gamepad" && in.gh2Type(g) == "joypad_guitar", "... then to Gamepad");
        check(readFile(dir + "controllers.json").find("\"gamepad\"") != std::string::npos, "... saved in controllers.json");
        in.setInGame(true);
        SDL_SetJoystickVirtualAxis(guitar, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
        pump(in, evs);
        check(has(evs, Action::Fret0, true, ControllerMode::Joypad), "... and plays as a joypad");
        in.setInGame(false);
        for (int k = 0; k < 4; ++k) in.cycleDeviceType(g, 1);
        check(in.deviceType(g) == "Auto (Xbox 360 guitar)", "... and back to Auto");

        // A D-pad SDL's mapping reads from an axis resting off centre (an
        // adapter's): nothing at rest, a press from that rest, and no chatter
        // around SDL's own mid-range test; A held as it connects is nothing
        // until released (the menus' key repeat moved left / right by itself).
        const SDL_JoystickID adapterId = Input::attachVirtualPad("0079:0006:dpadaxis=-32768");
        SDL_Joystick* adapter = nullptr;
        for (int k = 0; k < 100 && !adapter; ++k) {
            pump(in, evs);
            adapter = SDL_GetJoystickFromID(adapterId);
            if (!adapter) SDL_Delay(10);
        }
        if (adapter) {
            SDL_SetJoystickVirtualButton(adapter, SDL_GAMEPAD_BUTTON_SOUTH, true);
            pump(in, evs);
            SDL_Delay(350);
            evs.clear();
            SDL_SetJoystickVirtualAxis(adapter, 6, -31000);
            SDL_SetJoystickVirtualAxis(adapter, 7, 1200);
            pump(in, evs);
            const auto sideways = [&] { return has(evs, Action::Left, true, ControllerMode::Joypad) || has(evs, Action::Right, true, ControllerMode::Joypad); };
            check(!sideways() && evs.empty(), "a D-pad on an axis resting off centre presses nothing at rest");
            SDL_SetJoystickVirtualButton(adapter, SDL_GAMEPAD_BUTTON_SOUTH, false);
            pump(in, evs);
            check(evs.empty(), "... nor does A held as it connected, released");
            SDL_SetJoystickVirtualAxis(adapter, 6, 0);
            pump(in, evs);
            check(has(evs, Action::Right, true, ControllerMode::Joypad), "... moved from its rest it presses");
            evs.clear();
            for (int v : {-14000, -18000, -15000, -17500}) SDL_SetJoystickVirtualAxis(adapter, 6, Sint16(v)), pump(in, evs);
            int presses = 0;
            for (const InputEvent& e : evs) presses += e.action == Action::Right;
            check(presses == 0, "... and does not chatter around the middle of its range");
            SDL_SetJoystickVirtualButton(adapter, SDL_GAMEPAD_BUTTON_SOUTH, true);
            pump(in, evs);
            check(has(evs, Action::Confirm, true, ControllerMode::Joypad), "... A, pressed again, selects");
            evs.clear();
            SDL_DetachVirtualJoystick(adapterId);
            pump(in, evs);
            check(has(evs, Action::Right, false, ControllerMode::Joypad) && has(evs, Action::Confirm, false, ControllerMode::Joypad),
                  "a controller removed releases what it held");
            evs.clear();
        } else {
            check(false, "the adapter opens as a gamepad");
        }

        check(in.writeReport(), "controllers-report.txt written");
        const std::string report = readFile(in.reportPath());
        check(report.find("Xbox 360 Guitar (virtual)") != std::string::npos &&
                  report.find("Xbox 360 Controller (virtual)") != std::string::npos &&
                  report.find("GH2 type hx_guitar_xbox_new") != std::string::npos,
              "the report lists both controllers with their GH2 types");
        in.close();
        SDL_DetachVirtualJoystick(guitarId);
        SDL_DetachVirtualJoystick(padId);
    }
    std::remove((dir + "controllers.json").c_str());
    std::remove((dir + "controllers-report.txt").c_str());
    SDL_RemovePath(dir.c_str());
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    return check.failures;
}
