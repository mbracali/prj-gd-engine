// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "input/input.h"

class FrontEnd;
class PortScreens;

// What the port's walks (mouse, gamepads, stickyaxis) reach in the app.
struct WalkHost {
    virtual ~WalkHost() = default;
    // The mouse in menu units: button 0 a move (hover), 1 a left click, 3 a right click.
    virtual void walkMouse(float x, float y, int button) = 0;
    // A real key (an SDL_Keycode), pressed and released through the input layer.
    virtual void walkKey(int keycode) = 0;
    virtual PortScreens* walkPorts() = 0;
    virtual Input& walkInput() = 0;
    virtual std::string walkSaveDir() = 0;
    virtual std::string walkEnhanced(const std::string& key) = 0;
};

// --walktest <walk>: plays through GH3's own menus with scripted buttons
// (kickoff rule 1: the real user path), headless with
// SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy. Each step waits for a
// flow state (ui_flow_manager_state[0]) or for a song to be playing / over,
// then presses its buttons. The walk passes when every step was reached.
// The port's own screens' walks (guidelines/universal_launcher.md "Tests
// every game keeps"): mouse (the mouse alone: main menu -> OPTIONS ->
// ENHANCED -> In-Game Max FPS's dropdown set to 60 -> right click -> the
// help bar's BACK -> main), gamepads (the yellow fret -> GAMEPADS -> the
// keyboard's Whammy rebound to E and saved -> back -> the help bar's
// GAMEPADS) and stickyaxis (an adapter's D-pad on an axis resting off
// centre presses nothing). Walks keep their settings in the save folder's
// walktest/, fresh each run.
class Walk {
public:
    explicit Walk(const std::string& name);
    bool known() const { return !steps_.empty(); }
    // Called every frame: the buttons to press now. `port`: the port's open
    // screen ("port_gamepads", "port_enhanced"), which stands for the flow
    // state while open.
    std::vector<Action> update(FrontEnd& fe, bool songPlaying, double nowMs, WalkHost* host = nullptr, const std::string& port = "");
    // Every action the menus got (the app counts them; stickyaxis checks them).
    void countPress(Action a) { ++presses_[size_t(a)]; }
    bool done() const { return step_ >= steps_.size(); }
    bool failed() const { return failed_; }
    // The exit code and a summary line.
    int finish(FrontEnd& fe) const;
    // Song counters the walk checks (set by the app).
    int songsStarted = 0, songsEnded = 0;

private:
    // A button, or an act of the port's walks ("click:menu:<element>",
    // "check:...", see walk.cpp), after `delayMs`.
    struct Op {
        Action a = Action::Confirm;
        std::string act;
        double delayMs = 300;
        Op(Action x, double d = 300) : a(x), delayMs(d) {}
        Op(const char* s, double d = 300) : act(s), delayMs(d) {}
    };
    struct Step {
        std::string state;          // flow state to wait for ("song" / "song_over": the song)
        std::vector<Op> keys;       // pressed one per 300 ms once reached
        double timeoutMs = 60000;
        bool optional = false;      // skipped when the next step's state comes first
    };
    std::string name_;
    std::vector<Step> steps_;
    size_t step_ = 0;
    size_t key_ = 0;
    double reachedAt_ = -1, stepStart_ = 0, lastKey_ = 0;
    bool failed_ = false;
    std::string failure_;
    std::vector<std::string> log_;
    // uitest: every flow state with a Create script, started in turn.
    bool uitest_ = false;
    std::vector<uint32_t> states_;
    size_t stateIdx_ = 0;
    double stateAt_ = -1;
    size_t created0_ = 0;
    int empty_ = 0;
    std::vector<std::string> emptyStates_;
    void uiStep(FrontEnd& fe, double now);
    // An act; false fails the walk (failure_ says why).
    bool act(const std::string& what, FrontEnd& fe, WalkHost& host);
    std::array<int, size_t(Action::Start) + 1> presses_{};
    int sideBase_ = 0, rightBase_ = 0;
    unsigned adapter_ = 0;  // stickyaxis: the virtual adapter (SDL_JoystickID)
};
