#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "game/player.h"

enum class Action {
    Fret0, Fret1, Fret2, Fret3, Fret4,
    Strum, StarPower, Whammy,
    Up, Down, Left, Right, Confirm, Back,
    Start,  // pad Start (pause in game)
};

struct InputEvent {
    Action action;
    bool down;
    uint64_t timestampNs;  // SDL_GetTicksNS clock
    ControllerMode mode;   // mode of the device that produced it
    int player = 0;        // the player slot (pad port) of the device
};

inline int fretOf(Action a) { return static_cast<int>(a) - static_cast<int>(Action::Fret0); }
inline bool isFret(Action a) { return a >= Action::Fret0 && a <= Action::Fret4; }

// Translates keyboard and gamepad events into game actions. Each device
// drives "controls" (the frets, strum up / down, whammy, Star Power, Start,
// select / back, menu directions) through a binding table: per profile
// defaults following config/beatmatch_controller.dta (a PS2 guitar through an
// adapter: R2, Circle, Triangle, X, Square; an Xbox-style guitar; a joypad:
// L2, L1, R1, R2, X, where each fret press also strums), overridden per device
// model by the Gamepads screen and saved in controllers.json (SDL pref dir)
// with the device's player. One device per player; a device without one (and
// the keyboard's Esc / Enter / arrows, always) still works the menus.
class Input {
public:
    enum class Control {
        Green, Red, Yellow, Blue, Orange, StrumUp, StrumDown, Whammy, StarPower, Start, Select, Back,
        MenuUp, MenuDown, MenuLeft, MenuRight, Count
    };
    static const char* controlName(Control c);
    // A binding source: a gamepad button, an axis (past half way from its rest
    // in one direction, or either), or a keyboard key.
    struct Source {
        enum class Kind { Button, AxisPos, AxisNeg, AxisAny, Key } kind = Kind::Button;
        int index = 0;  // SDL_GamepadButton, SDL_GamepadAxis or SDL_Keycode
        bool operator==(const Source& o) const { return kind == o.kind && index == o.index; }
    };
    using Bindings = std::array<std::vector<Source>, size_t(Control::Count)>;
    // A connected device for the Gamepads screen; the keyboard is id "keyboard".
    struct Device {
        std::string id, name, profile;
        int player = 0;  // 0 or 1, -1: none (the menus only)
    };

    ~Input() { close(); }
    // Closes all controllers; must run before SDL_Quit().
    void close();
    void handle(const SDL_Event& e, std::vector<InputEvent>& out);
    ControllerMode lastMode() const { return lastMode_; }
    // The guitars' analog whammy bar of player `player`'s pads, 0 at rest .. 1
    // fully pressed; the Whammy action still fires past a threshold.
    float whammyValue(int player = 0) const;
    // Player slots like PS2 pad ports: the first pad takes slot 0, the second
    // slot 1 (a removed pad frees its slot) unless the Gamepads screen gave the
    // device a player. The keyboard plays for slot 0 when no pad holds it, else
    // for the lowest empty slot (or its chosen player). With the split
    // keyboard (GH1RE_KB2P / --kb2p) it plays both: P2 on H J K L ; (frets),
    // N / M (strum), B (Star Power), O (Start).
    bool padConnected(int slot) const;
    void setSplitKeyboard(bool on) { splitKeyboard_ = on; }

    // Enhanced Settings. XInput Controllers: a joypad plays the game (the
    // original's gamepad mode: L2 L1 R1 R2 Cross are the frets, each press
    // also strums, the sticks bend the whammy); off, joypads only work the
    // menus and pause. Rock Band Guitars: the HID Rock Band guitars (PS3 / Wii
    // ones get SDL mappings, Rock Band 4 ones by USB id) play with the Xbox
    // guitar layout and tilt for Star Power. Xbox 360 guitars (Guitar Hero and
    // Rock Band) are always Xbox guitars. Touch Controls: see the touch section.
    void setGamepadPlay(bool on) { gamepadPlay_ = on; }
    void setRockBand(bool on);

    // -- the Gamepads screen
    std::vector<Device> devices() const;
    // Gives `id` the player (-1: none); the device that had it takes `id`'s old one.
    void setPlayer(const std::string& id, int player);
    // The keyboard's frets also strum (the original's joypad mode).
    bool keyboardFretsStrum() const { return kbFretsStrum_; }
    void setKeyboardFretsStrum(bool on);
    const Bindings& bindings(const std::string& id) const;
    std::string sourceName(const std::string& id, const Source& s) const;
    bool controlDown(const std::string& id, Control c) const;
    // The device's raw state: pressed buttons / keys, and axis values -1..1.
    std::vector<std::string> rawPressed(const std::string& id) const;
    std::vector<std::pair<std::string, float>> rawAxes(const std::string& id) const;
    // The next button, axis move or key on that device binds `c` (replacing its
    // bindings); its input does nothing else meanwhile. Esc cancels, Delete
    // leaves `c` unbound (clearBinding).
    void beginCapture(const std::string& id, Control c);
    void clearBinding(const std::string& id, Control c);
    void cancelCapture() { capturing_ = false; }
    bool capturing() const { return capturing_; }
    void resetBindings(const std::string& id);

    // -- touch (SDL finger events). In the game, a finger on a lane of the
    // highway (from its far end down past the smashers) holds that fret and
    // strums (fingers landing within 30 ms strum once, as a chord); a tap on
    // the Star Power meter, or two fingers above the highway, deploys Star
    // Power; a tap in the top-right corner is Start. Elsewhere (menus, pause)
    // a tap is a mouse click (takeMenuTaps).
    struct TouchLanes {
        bool active = false;
        // Per fret, in menu units (1280 x 720): the lane at the strike line and at its far end.
        std::array<float, 5> nearX{}, nearY{}, farX{}, farY{};
    };
    void setTouch(bool on) { touch_ = on; }
    bool touch() const { return touch_; }
    // Window-normalised (0..1) to menu units.
    void setTouchToMenu(std::function<void(float, float, float&, float&)> f) { toMenu_ = std::move(f); }
    void setTouchLanes(int player, const TouchLanes& lanes) { lanes_[size_t(player & 1)] = lanes; }
    // Emits the strums of chords whose 30 ms window has closed.
    void poll(uint64_t nowNs, std::vector<InputEvent>& out);
    std::vector<std::pair<float, float>> takeMenuTaps() { return std::move(menuTaps_); }

private:
    enum class Profile { Ps2Guitar, XboxGuitar, RockBand, Joypad, Keyboard };
    struct Pad {
        SDL_Gamepad* pad = nullptr;
        Profile profile = Profile::Joypad;
        std::string guid, name;
        int slot = 0;
        bool chosenSlot = false;  // from controllers.json
        std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
        std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{}, rest{};
        std::array<bool, size_t(Control::Count)> down{};
        float whammyAxis = 0;
    };
    static Bindings defaults(Profile p);
    static const char* profileName(Profile p);
    const Bindings& bindingsFor(const std::string& guid, Profile p) const;
    bool sourceActive(const Pad& p, const Source& s, bool wasDown) const;
    void update(Pad& p, uint64_t ts, std::vector<InputEvent>& out);
    void controlEdge(Control c, bool down, ControllerMode mode, int player, bool joypad, uint64_t ts, std::vector<InputEvent>& out);
    void controlEdges(const std::array<bool, size_t(Control::Count)>& was, const std::array<bool, size_t(Control::Count)>& now,
                      ControllerMode mode, int player, bool joypad, uint64_t ts, std::vector<InputEvent>& out);
    bool inGame() const { return lanes_[0].active || lanes_[1].active; }
    bool slotTaken(int slot, const Pad* except) const;
    void keyEvent(const SDL_KeyboardEvent& k, std::vector<InputEvent>& out);
    void load();
    void save() const;
    void setBinding(const std::string& id, Control c, std::vector<Source> s);
    Pad* padById(const std::string& id);
    const Pad* padById(const std::string& id) const;
    bool captureFrom(const std::string& id, const Source& s);
    void resync(const std::string& id);

    std::map<std::string, Bindings> overrides_;  // by GUID ("keyboard" for the keyboard)
    std::map<std::string, int> players_;         // chosen players by GUID
    bool loaded_ = false, capturing_ = false, kbFretsStrum_ = false;
    std::string captureId_;
    Control captureControl_ = Control::Green;
    std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> captureRest_{};
    std::set<SDL_Keycode> keysDown_;
    std::array<bool, size_t(Control::Count)> keyControls_{};
    struct Finger {
        int player = 0, fret = -1;  // fret -1: not on a lane
        float mx = 0, my = 0;
        uint64_t downNs = 0;
    };
    void touchEvent(const SDL_TouchFingerEvent& f, bool down, std::vector<InputEvent>& out);
    bool touch_ = true, gamepadPlay_ = true, rockBand_ = false, rbMappings_ = false;
    std::function<void(float, float, float&, float&)> toMenu_;
    std::array<TouchLanes, 2> lanes_;
    std::unordered_map<SDL_FingerID, Finger> fingers_;
    struct PendingStrum {
        bool on = false;
        uint64_t dueNs = 0, ts = 0;
    };
    std::array<PendingStrum, 2> strum_;
    std::vector<std::pair<float, float>> menuTaps_;
    int keyboardSlot() const;
    bool splitKeyboard_ = false;
    void emit(std::vector<InputEvent>& out, Action a, bool down, uint64_t ts, ControllerMode mode, int player = 0);

    std::unordered_map<SDL_JoystickID, Pad> pads_;
    ControllerMode lastMode_ = ControllerMode::Guitar;
};
