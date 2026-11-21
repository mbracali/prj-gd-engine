// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd24's input layer, on gd-game-10's model (gd-game-10/engine/src/input/
// input.*, itself gd01's): keyboard and SDL gamepads turned into the PSP's
// buttons, guitars' frets into the four lanes, through a binding table of up
// to two sources per control (docs/guidelines/universal_mechanisms.md
// 1.1.4, universal-menus.md 1.2.1), overridden per device model by the
// GAMEPADS screen and saved in controllers.json (the save folder).
//
// Rock Band Unplugged is a PSP game: one player, the PSP's buttons. What the
// original does with them (docs/FORMATS.md "Controls"):
//   [C] its Options > Controls assigns seven actions to PSP buttons (TRACK
//       SWITCH LEFT / RIGHT, RED / YELLOW / GREEN / BLUE NOTE, OVERDRIVE:
//       STR_EN.TXT); the help bars use $button1 (confirm), $button2 (back /
//       cancel), $button3, $button4, $left_trigger, $right_trigger, the
//       code's token table at 0x08b4ef38 (left right up down button1..4
//       start menu left_trigger right_trigger ...); the tips say Overdrive is
//       on two buttons, one for each thumb, track switching on two, pause on
//       one ("(default controls)", STR_EN_GAMEPLAY_TIPS.TXT).
//   [I] the default lanes L, Up, Triangle, R; Overdrive Cross or Down; track
//       switching Left / Right (kLaneButtons in input.cpp): the glyphs of
//       the tips are not decoded yet, so these follow the game's known
//       defaults until the code's controls table is traced (GD24-002).
//
// No double inputs: each device's controls are merged into actions before
// anything is sent, so two controls on one key (General Up and the d-pad's
// Up) send the action once. Built-in helpers keep every menu reachable: the
// keyboard's Esc / Backspace (Circle, back), Enter (Cross) and arrows, a
// pad's south (Cross) and east (Circle) buttons. Mouse buttons are never
// bindable.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// What the game receives: the PSP's buttons, and from guitars (and the lane
// table, in a song) the lanes, Overdrive and track switching.
enum class Action {
    Up, Down, Left, Right, Cross, Circle, Square, Triangle, L, R, Start, Select,  // the PSP's buttons
    Lane0, Lane1, Lane2, Lane3, Overdrive, TrackLeft, TrackRight,                 // gameplay (guitars)
    Count
};
const char* actionName(Action a);

// The original's default controls in a song [I] (see the top of this file):
// L, Up, Triangle, R the lanes 0..3; Cross / Down Overdrive; Left / Right
// track switching. Other buttons (and gameplay actions) map to themselves.
inline Action gameplayOf(Action a) {
    switch (a) {
    case Action::L: return Action::Lane0;
    case Action::Up: return Action::Lane1;
    case Action::Triangle: return Action::Lane2;
    case Action::R: return Action::Lane3;
    case Action::Cross: case Action::Down: return Action::Overdrive;
    case Action::Left: return Action::TrackLeft;
    case Action::Right: return Action::TrackRight;
    default: return a;
    }
}

struct InputEvent {
    Action action;
    bool down;
    uint64_t timestampNs;  // SDL_GetTicksNS clock
    int player = 0;
};

class Input {
public:
    // The bindable controls, in the GAMEPADS screen's sections.
    enum class Control {
        Start, Select, Up, Down,                                // General control bindings
        Left, Right, Cross, Circle, Square, Triangle, L, R,     // PSP buttons (keyboard and gamepads)
        GuitarStart, GuitarSelect, StrumUp, StrumDown,          // 5 fret guitar bindings (guitars)
        Green, Red, Yellow, Blue, Orange, Whammy, Gyro,
        Count
    };
    static constexpr size_t kSlots = 2;
    static const char* controlName(Control c);
    // The section a control is listed in: 0 General, 1 PSP buttons, 2 guitar.
    static int controlSection(Control c);
    struct Source {
        enum class Kind { Button, AxisPos, AxisNeg, AxisAny, Key } kind = Kind::Button;
        int index = 0;  // SDL_GamepadButton, SDL_GamepadAxis or SDL_Keycode
        bool operator==(const Source& o) const { return kind == o.kind && index == o.index; }
    };
    using Bindings = std::array<std::vector<Source>, size_t(Control::Count)>;
    // The instruments of the GAMEPADS screen: Rock Band Unplugged has the PSP
    // layout (Gamepad) and guitars play its lanes.
    enum class Instrument { Guitar, Gamepad };
    static const char* instrumentName(Instrument i);
    struct Device {
        std::string id, name;  // "keyboard", "pad:<SDL id>"
        Instrument instrument = Instrument::Gamepad;
        int player = 0;  // 0, or -1: menus only
    };
    static constexpr int kPlayers = 1;  // the GAMEPADS screen lists four; 2..4 are unavailable

    static void prepareSdl();  // before SDL_Init (Steam's virtual pads)
    Input();
    ~Input();
    void close();  // before SDL_Quit
    void handle(const SDL_Event& e, std::vector<InputEvent>& out);
    // In a song guitars play lanes; in the menus their frets work as Cross / Circle.
    void setInGame(bool on) { inGame_ = on; }

    // -- the GAMEPADS screen
    std::vector<Device> devices() const;
    std::string playerDevice(int player) const;
    void setPlayer(const std::string& id, int player);
    void beginDetect(int player) { detect_ = player; }
    void cancelDetect() { detect_ = -1; }
    bool detecting() const { return detect_ >= 0; }
    const Bindings& bindings(const std::string& id) const;
    std::string sourceName(const std::string& id, const Source& s) const;
    bool controlDown(const std::string& id, Control c) const;
    void beginCapture(const std::string& id, Control c, size_t slot);
    void cancelCapture() { capturing_ = false; }
    bool capturing() const { return capturing_; }
    bool capturingSlot(const std::string& id, Control c, size_t slot) const {
        return capturing_ && captureId_ == id && captureControl_ == c && captureSlot_ == slot;
    }
    void clearBinding(const std::string& id, Control c, size_t slot);
    void resetBindings(const std::string& id);
    void beginEdit(const std::string& id);
    void commitEdit();
    void discardEdit();
    bool editing() const { return !editId_.empty(); }
    Instrument instrument(const std::string& id) const;
    void setInstrument(const std::string& id, Instrument i);
    bool exportTo(const std::string& file) const;
    bool importFrom(const std::string& file);
    // Where controllers.json goes (default the SDL pref dir), with a trailing separator.
    void setSaveDir(std::string dir);

    // Development aid / self-test: a virtual pad (GD24_VIRTUAL_PAD
    // "<vendor>:<product>[:guitar]"); returns its id, 0 on failure.
    static SDL_JoystickID attachVirtualPad(const std::string& spec);

private:
    enum class Profile { Gamepad, XboxGuitar, RockBandGuitar, Ps2Guitar, Keyboard };
    struct Pad {
        SDL_Gamepad* pad = nullptr;
        Profile profile = Profile::Gamepad, detected = Profile::Gamepad;
        std::string guid, name;
        std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
        std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{}, rest{};
        std::array<bool, size_t(Control::Count)> down{};
        uint64_t settleNs = 0;
    };
    static Bindings defaults(Profile p);
    static bool isGuitar(Profile p) { return p == Profile::XboxGuitar || p == Profile::RockBandGuitar || p == Profile::Ps2Guitar; }
    const Bindings& bindingsFor(const std::string& guid, Profile p) const;
    bool sourceActive(const Pad& p, const Source& s) const;
    void update(Pad& p, uint64_t ts, std::vector<InputEvent>& out);
    void keyEvent(const SDL_KeyboardEvent& k, std::vector<InputEvent>& out);
    // The actions a device's held controls give (each once).
    std::array<bool, size_t(Action::Count)> actionsOf(const std::array<bool, size_t(Control::Count)>& held, bool guitar) const;
    void edges(std::array<bool, size_t(Action::Count)>& was, const std::array<bool, size_t(Action::Count)>& now, uint64_t ts,
               std::vector<InputEvent>& out);
    Pad* padById(const std::string& id);
    const Pad* padById(const std::string& id) const;
    std::string guidOf(const std::string& id) const;
    Profile profileOf(const std::string& id) const;
    bool captureFrom(const std::string& id, const Source& s);
    bool detectFrom(const std::string& id);
    void load();
    void save() const;
    std::string savePath() const;

    std::unordered_map<SDL_JoystickID, Pad> pads_;
    std::map<std::string, Bindings> overrides_;    // by GUID ("keyboard")
    std::map<std::string, Instrument> instruments_;  // chosen by GUID
    std::map<std::string, int> players_;           // chosen by GUID
    std::string saveDir_;
    bool loaded_ = false, inGame_ = false;
    std::set<SDL_Keycode> keysDown_;
    std::array<bool, size_t(Control::Count)> keyControls_{};
    std::array<bool, size_t(Action::Count)> keyActions_{};
    std::unordered_map<SDL_JoystickID, std::array<bool, size_t(Action::Count)>> padActions_;
    bool capturing_ = false;
    std::string captureId_;
    Control captureControl_ = Control::Start;
    size_t captureSlot_ = 0;
    int detect_ = -1;
    std::string editId_, editGuid_;
    bool editHad_ = false, saveHeld_ = false;
    Bindings editBindings_{};
};
