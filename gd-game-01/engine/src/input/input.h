// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "game/player.h"

class MidiIn;

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
    // The keyboard's Start key in the menus: sent as Confirm (Enter selects),
    // and a text entry takes it as START (the name is accepted).
    bool startKey = false;
};

inline int fretOf(Action a) { return static_cast<int>(a) - static_cast<int>(Action::Fret0); }
inline bool isFret(Action a) { return a >= Action::Fret0 && a <= Action::Fret4; }

// Translates keyboard, gamepad, MIDI and touch events into game actions
// (universal_mechanisms.md 1.1.4 at the repository root). Each device drives
// the universal "controls" (General: Start, Select, Up, Down; 5-fret guitar:
// Start, Select, Strum Up / Down, the frets, Whammy, Gyro special trigger)
// through a binding table of up to two sources per control: per profile
// defaults (the doc's keyboard and XInput tables; the guitars as
// config/beatmatch_controller.dta), overridden per device model by the
// Gamepad menu and saved in controllers.json (SDL pref dir) with the device's
// player, type and Require Strum. One device per player.
//
// No double inputs: the controls of one device are merged into actions
// before anything is sent, so two controls on the same key or button (menu
// Up and Strum Up on the Up arrow, Start in both sections on Enter) send the
// action once, and releasing one of them never releases an action another
// still holds. Strum is a pulse: every strum control pressed strums once.
// Built-in helpers that cannot be rebound keep every menu reachable: the
// keyboard's Esc / Backspace (back), Enter (select, when not bound) and the
// arrows; a pad's d-pad left / right; a gamepad's south (select) and east
// (back) buttons. Mouse buttons are never bindable (they are the mouse).
class Input {
public:
    enum class Control {
        Start, Select, MenuUp, MenuDown,                // SECTION: General control bindings
        GuitarStart, GuitarSelect, StrumUp, StrumDown,  // SECTION: 5 fret guitar bindings
        Green, Red, Yellow, Blue, Orange, Whammy, Gyro,
        Count
    };
    static constexpr size_t kSlots = 2;  // bindings per control (Key 1, Key 2)
    static const char* controlName(Control c);
    // A binding source: a gamepad button, an axis (past half way from its rest
    // in one direction, or either), or a keyboard key.
    struct Source {
        enum class Kind { Button, AxisPos, AxisNeg, AxisAny, Key, MidiNote, MidiCc } kind = Kind::Button;
        int index = 0;  // SDL_GamepadButton, SDL_GamepadAxis, SDL_Keycode, or the MIDI note / controller
        bool operator==(const Source& o) const { return kind == o.kind && index == o.index; }
    };
    using Bindings = std::array<std::vector<Source>, size_t(Control::Count)>;
    // A connected device for the Gamepad menu; the keyboard is id "keyboard",
    // pads "pad:<SDL id>", MIDI ports "midi:<port name>", the touch screen "touch".
    struct Device {
        std::string id, name, profile;
        int player = 0;  // 0 or 1, -1: none (the menus only)
    };
    // The instrument types of the Gamepad menu (GH1 has neither drums nor vocals).
    enum class Instrument { Guitar, Gamepad, Midi };
    static const char* instrumentName(Instrument i);

    Input();
    ~Input();
    // The self-test: the built-in defaults only, nothing read or written.
    void testMode() { loaded_ = true, saveHeld_ = true; }
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
    // keyboard (GD01_KB2P / --kb2p) it plays both: P2 on H J K L ; (frets),
    // N / M (strum), B (Star Power), O (Start).
    bool padConnected(int slot) const;
    // Players 1..2 (GH1 has two). Player 1 is always on; a player turned off
    // has no device (its device works the menus only) until turned on again.
    static constexpr int kPlayers = 2;
    bool playerEnabled(int player) const { return player == 0 || (player == 1 && p2Enabled_); }
    void setPlayerEnabled(int player, bool on);
    // The device bound to `player` ("" none) and every device that can be.
    std::string playerDevice(int player) const;
    void setSplitKeyboard(bool on) { splitKeyboard_ = on; }

    // Joypads always play (the original's gamepad mode: L2 L1 R1 R2 Cross are
    // the frets, the sticks bend the whammy), and the HID Rock Band guitars
    // (PS3 / Wii ones get SDL mappings, Rock Band 4 ones by USB id) play with
    // the Xbox guitar layout and tilt for Star Power: both on since 1.1.7 (the
    // setters stay for the tests). Touch Controls: see the touch section.
    void setGamepadPlay(bool on) { gamepadPlay_ = on; }
    // Lefty Flip per player (beatmatch_controller.dta, docs/ui_system.md): a
    // gamepad's frets take the lefty_joypad order (X R2 R1 L1 L2, green to
    // orange: the joypad's reversed), and a guitar's strum up / down move the
    // menus the other way (the guitar is held upside down). The highway's
    // mirroring is TrackRenderer::setLefty.
    void setLefty(bool p1, bool p2) { lefty_ = {p1, p2}; }
    void setRockBand(bool on);

    // -- the Gamepad menu
    std::vector<Device> devices() const;
    // Gives `id` the player (-1: none); the device that had it takes `id`'s old one.
    void setPlayer(const std::string& id, int player);
    // The next button, key or MIDI note on any device (never the mouse)
    // binds that device to `player`; Esc cancels. detecting(): still waiting.
    void beginDetect(int player) { detect_ = player; }
    void cancelDetect() { detect_ = -1; }
    bool detecting() const { return detect_ >= 0; }
    // Require Strum (the Gamepad menu's option, per device model): off, a fret
    // press also strums (the original's joypad mode). Defaults: keyboard and
    // guitars on, gamepads, MIDI and touch off.
    bool requireStrum(const std::string& id) const;
    void setRequireStrum(const std::string& id, bool on);
    // The keyboard's frets also strum (Require Strum off), for older callers.
    bool keyboardFretsStrum() const { return !requireStrum("keyboard"); }
    void setKeyboardFretsStrum(bool on) { setRequireStrum("keyboard", !on); }
    const Bindings& bindings(const std::string& id) const;
    std::string sourceName(const std::string& id, const Source& s) const;
    bool controlDown(const std::string& id, Control c) const;
    // The device's raw state: pressed buttons / keys / notes, and axis values -1..1.
    std::vector<std::string> rawPressed(const std::string& id) const;
    std::vector<std::pair<std::string, float>> rawAxes(const std::string& id) const;
    // The next button, axis move, key or MIDI note / controller on that device
    // binds slot `slot` (0, 1) of `c`; its input does nothing else meanwhile.
    // Esc cancels, Delete leaves the slot empty (clearBinding). A source
    // already bound to another control stays there too (one press then works
    // both, sent once: see the class comment).
    void beginCapture(const std::string& id, Control c, size_t slot = 0);
    void clearBinding(const std::string& id, Control c, size_t slot);
    void clearBinding(const std::string& id, Control c);  // both slots
    void cancelCapture() { capturing_ = false; }
    bool capturing() const { return capturing_; }
    bool capturingSlot(const std::string& id, Control c, size_t slot) const {
        return capturing_ && captureId_ == id && captureControl_ == c && captureSlot_ == slot;
    }
    void resetBindings(const std::string& id);
    // Customize inputs: an edit of one device's bindings and Require Strum that
    // is saved or discarded as a whole. Changes work at once (so they can be
    // tried); controllers.json is written by commitEdit, discardEdit puts the
    // device back as it was.
    void beginEdit(const std::string& id);
    void commitEdit();
    void discardEdit();
    bool editing() const { return !editId_.empty(); }
    // The instrument of a device and the ones it may be (Gamepad menu's
    // Instrument dropdown); a pad's guitar types stay in deviceType.
    Instrument instrument(const std::string& id) const;
    std::vector<Instrument> instruments(const std::string& id) const;
    void setInstrument(const std::string& id, Instrument i);
    // Export / Import in the Gamepad menu: controllers.json to / from a file.
    bool exportTo(const std::string& file) const;
    bool importFrom(const std::string& file);
    // Generic MIDI: the backend ("ALSA", ...) or why there is none.
    bool midiAvailable() const;
    std::string midiBackend() const;
    // The controller's type (a pad): Auto (as detected), or chosen: Gamepad,
    // PS2 guitar (adapter), Xbox guitar, Rock Band guitar. A guitar plays
    // with the strum bar; a gamepad's frets also strum. Some adapters for PS2
    // guitars say they are a PlayStation 3 pad, so the player picks it.
    // Saved per controller model in controllers.json ("type").
    std::string deviceType(const std::string& id) const;
    void cycleDeviceType(const std::string& id, int dir);
    // controllers-report.txt in the save folder: the system, every
    // controller (name, USB id, SDL mapping, type, player, bindings) and the
    // last raw button / axis / hat events, for bug reports.
    std::string reportPath() const;
    bool writeReport() const;

    // -- touch (SDL finger events). In the game, a finger on a lane of the
    // highway (from its far end down past the smashers) holds that fret and
    // strums (fingers landing within 30 ms strum once, as a chord); a tap on
    // the Star Power meter, or a two-finger tap above the highway, deploys
    // Star Power; the pause corner (top right by default) is Start; two-finger
    // swipes up / down do what the touch settings say. Elsewhere (menus,
    // pause) a tap is a mouse click (takeMenuTaps, on lift and only if the
    // finger did not slide), and two-finger swipes move up / down. On-screen
    // buttons (touchButtons) give Up / Down / Start in the menus and Start in a
    // song; by default they show once the screen is touched, and hide again
    // when a keyboard or controller is used.
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
    // Emits the strums of chords whose 30 ms window has closed; reads MIDI.
    void poll(uint64_t nowNs, std::vector<InputEvent>& out);
    std::vector<std::pair<float, float>> takeMenuTaps() { return std::move(menuTaps_); }

    // The touch settings (Gamepads screen > Touch Screen; controllers.json "touch").
    static constexpr int kTouchOptions = 4;
    static const char* touchOptionName(int i);
    std::string touchOptionValue(int i) const;
    void cycleTouchOption(int i, int dir);
    void resetTouchOptions();
    // The on-screen buttons shown now (none while hidden), in menu units.
    struct TouchButton {
        Action action;
        float x, y, r;
    };
    std::vector<TouchButton> touchButtons() const;
private:
    enum class Profile { Ps2Guitar, XboxGuitar, RockBand, Joypad, Keyboard, Midi };
    struct Pad {
        SDL_Gamepad* pad = nullptr;
        Profile profile = Profile::Joypad;
        Profile detected = Profile::Joypad;  // what the game guessed (type Auto)
        std::string guid, name;
        int slot = 0;
        bool chosenSlot = false;  // from controllers.json
        std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
        std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{}, rest{};
        std::array<bool, size_t(Control::Count)> down{};
        std::array<bool, 4> helper{};  // the built-in menu helpers held (Input::update)
        float whammyAxis = 0;
        uint64_t settleNs = 0;  // until then (300 ms after connecting) its axes settle: rest re-read, no actions
    };
    static Bindings defaults(Profile p);
    static const char* profileName(Profile p);
    const Bindings& bindingsFor(const std::string& guid, Profile p) const;
    bool sourceActive(const Pad& p, const Source& s, bool wasDown) const;
    void update(Pad& p, uint64_t ts, std::vector<InputEvent>& out);
    // The actions the held controls of one device give (each at most once).
    struct ActionSet {
        std::array<bool, size_t(Action::Start) + 1> on{};
        int strums = 0;  // strum controls held
    };
    ActionSet actionsOf(const std::array<bool, size_t(Control::Count)>& held, bool joypad, int player) const;
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
    std::map<std::string, Profile> types_;       // chosen controller types by GUID
    bool loaded_ = false, capturing_ = false;
    std::string captureId_;
    Control captureControl_ = Control::Green;
    std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> captureRest_{};
    std::set<SDL_Keycode> keysDown_;
    std::array<bool, size_t(Control::Count)> keyControls_{};
    struct Finger {
        int player = 0, fret = -1;  // fret -1: not on a lane
        float mx = 0, my = 0;       // where it is now
        uint64_t downNs = 0;
        float x0 = 0, y0 = 0;       // where it landed
        bool moved = false;         // slid away from there (no tap)
        bool multi = false;         // landed with another finger (a two-finger tap or swipe, no tap)
        bool done = false;          // its two-finger tap or swipe already acted
        int button = -1;            // the on-screen button it holds (an Action), -1 none
    };
    void touchEvent(const SDL_TouchFingerEvent& f, bool down, std::vector<InputEvent>& out);
    void touchMotion(const SDL_TouchFingerEvent& f);
    void touchGesture(bool up, bool game, uint64_t ts, std::vector<InputEvent>& out);
    // MIDI ports (MidiIn): a device like a pad, with notes and controllers.
    struct MidiDev {
        std::string id, name, guid;  // id "midi:<port>"; guid: the port's name without ALSA's client numbers
        int slot = -1;
        std::array<uint8_t, 128> notes{}, cc{};
        std::array<bool, size_t(Control::Count)> down{};
        float whammyAxis = 0;
    };
    std::unique_ptr<MidiIn> midi_;
    std::map<std::string, MidiDev> midiDevs_;  // by id
    uint64_t midiRefreshNs_ = 0;
    void pollMidi(uint64_t nowNs, std::vector<InputEvent>& out);
    void updateMidi(MidiDev& m, uint64_t ts, std::vector<InputEvent>& out);
    MidiDev* midiById(const std::string& id);
    const MidiDev* midiById(const std::string& id) const;
    std::string guidOf(const std::string& id) const;
    bool detectFrom(const std::string& id);
    int detect_ = -1;
    bool p2Enabled_ = true;
    std::map<std::string, bool> requireStrum_;  // chosen per GUID
    bool saveHeld_ = false;  // during an edit controllers.json waits for commitEdit
    struct Edit {
        bool hadOverride = false, hadStrum = false, strum = false;
        Bindings bindings;
    };
    std::string editId_, editGuid_;
    Edit edit_;
    size_t captureSlot_ = 0;
    // The touch settings: on-screen buttons (0 when touched, 1 always, 2 off),
    // two-finger swipe up / down in a song (0 Star Power, 1 pause, 2 nothing),
    // the pause corner (0 top right, 1 top left, 2 none).
    std::array<int, kTouchOptions> touchOpt_ = {0, 0, 1, 0};
    bool touchUsed_ = false;  // the screen was touched since the last key or button
    std::array<bool, 2> lefty_{};
    bool curGuitar_ = false;  // the device being read is a guitar (lefty: menu strum swapped)
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
    void emit(std::vector<InputEvent>& out, Action a, bool down, uint64_t ts, ControllerMode mode, int player = 0, bool startKey = false);
    // The device whose input is being turned into actions (a pad's id, -1 the
    // keyboard, -2 touch). In the menus one press that arrives from two
    // devices within kSameNs (a Steam Deck seen as its own controller and as
    // Steam's virtual one, or Steam's desktop layout typing Enter for A) acts once.
    int64_t device_ = -1;
    struct Recent {
        uint64_t ns = 0;
        int64_t device = 0;
    };
    std::array<Recent, size_t(Action::Start) + 1> recent_{};
    std::set<std::pair<int64_t, int>> dropped_;  // (device, action) presses whose release is dropped too
    void addGenericMapping(SDL_JoystickID id);
    struct RawEvent {
        uint64_t ns;
        SDL_JoystickID id;
        char kind;  // 'b' button, 'a' axis, 'h' hat (joystick events: the device's own numbers)
        int index, value;
    };
    std::vector<RawEvent> raw_;  // the last kRawEvents, oldest first (for the report)
    std::unordered_map<SDL_JoystickID, Pad> pads_;
    ControllerMode lastMode_ = ControllerMode::Guitar;
};
