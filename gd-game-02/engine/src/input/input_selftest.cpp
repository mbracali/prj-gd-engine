// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "input/input_selftest.h"

#include <SDL3/SDL.h>

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
