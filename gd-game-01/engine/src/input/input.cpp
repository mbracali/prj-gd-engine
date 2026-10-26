// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "input/input.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "data/json.h"
#include "platform/paths.h"

namespace {

using Kind = Input::Source::Kind;
using C = Input::Control;

std::string lower(const char* s) {
    std::string n = s ? s : "";
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return n;
}

ControllerMode modeFor(bool joypad) { return joypad ? ControllerMode::Joypad : ControllerMode::Guitar; }

// Xbox-style guitars: the Xbox 360 Guitar Hero (RedOctane 0x1430) and Rock
// Band (Harmonix 0x1bad) guitars, or anything SDL types as a guitar. On Linux
// the xpad driver reports the Rock Band one as a plain Xbox 360 pad (type
// GAMEPAD), so the vendor decides.
bool isXboxGuitar(SDL_JoystickID id) {
    if (SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_GUITAR) return true;
    uint16_t vendor = SDL_GetGamepadVendorForID(id);
    SDL_GamepadType type = SDL_GetGamepadTypeForID(id);
    return (vendor == 0x1bad || vendor == 0x1430) && (type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE);
}

// The HID Rock Band guitars by USB id (Harmonix PS3 / Wii, Mad Catz and PDP
// Rock Band 4 guitars).
bool isHidRockBandGuitar(SDL_JoystickID id) {
    static const std::pair<uint16_t, uint16_t> kIds[] = {
        {0x12ba, 0x0200}, {0x1bad, 0x0004}, {0x1bad, 0x3010}, {0x0738, 0x8261},
        {0x0e6f, 0x0173}, {0x0e6f, 0x0248}, {0x0738, 0x4161}, {0x0e6f, 0x0170}};
    uint16_t vendor = SDL_GetGamepadVendorForID(id), product = SDL_GetGamepadProductForID(id);
    for (const auto& [v, p] : kIds)
        if (vendor == v && product == p) return true;
    return false;
}

// A PS2 guitar through a USB adapter (beatmatch_controller.dta's guitar):
// a guitar by name that is neither an Xbox nor a Rock Band guitar.
bool isPs2Guitar(SDL_Gamepad* pad) {
    std::string n = lower(SDL_GetGamepadName(pad));
    return n.find("guitar") != std::string::npos && n.find("rock band") == std::string::npos &&
           n.find("harmonix") == std::string::npos && n.find("xbox") == std::string::npos;
}

// SDL has no gamepad mappings for the HID Rock Band guitars: the Xbox-guitar
// layout (A green, B red, Y yellow, X blue, left shoulder orange, d-pad
// strum, right stick X whammy). [I] Button and axis numbers follow the
// common PS3 / Wii guitar HID report (blue 0, green 1, red 2, yellow 3,
// orange 4, tilt 5, select 8, start 9); not checked on hardware.
const char* const kRockBandMappings[] = {
    "03000000ba1200000002000000000000,Harmonix Guitar for PlayStation 3,a:b1,b:b2,x:b0,y:b3,leftshoulder:b4,"
    "rightshoulder:b5,back:b8,start:b9,guide:b12,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,rightx:a2,righty:a3,",
    "03000000ad1b00000400000000000000,Harmonix Guitar for Nintendo Wii,a:b1,b:b2,x:b0,y:b3,leftshoulder:b4,"
    "rightshoulder:b5,back:b8,start:b9,guide:b12,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,rightx:a2,righty:a3,",
    "03000000ad1b00001030000000000000,Harmonix Guitar for Nintendo Wii (RB2),a:b1,b:b2,x:b0,y:b3,leftshoulder:b4,"
    "rightshoulder:b5,back:b8,start:b9,guide:b12,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,rightx:a2,righty:a3,",
};

// Touch (menu units): the pause corner's area (top right, or mirrored top
// left); the HUD's Star Power meter area deploys.
constexpr float kPauseX0 = 1160, kPauseY1 = 110;
constexpr float kMeterX0 = 900, kMeterX1 = 1200, kMeterY0 = 430;
constexpr uint64_t kChordNs = 30000000;   // fingers this close strum once
constexpr uint64_t kTwoFingerNs = 250000000;
constexpr float kSlide = 22;              // a finger that moves farther slides (no tap)
constexpr float kSwipe = 60;              // a two-finger swipe moves both fingers this far up or down
// [I] The on-screen buttons sit at the right edge, where the menus have the
// least to show (menu units); the song's pause button in the pause corner.
constexpr float kButtonX = 1246, kButtonR = 30, kPauseR = 28;
constexpr uint64_t kSameNs = 80000000;    // the same press from two devices (menus)

const char* const kControlKeys[] = {"green", "red", "yellow", "blue", "orange", "strum_up", "strum_down", "whammy",
                                    "star_power", "start", "select", "back", "menu_up", "menu_down", "menu_left", "menu_right"};
const char* const kKindKeys[] = {"button", "axis_pos", "axis_neg", "axis_any", "key"};
// controllers.json "type" values, by Input::Profile (Keyboard is not a choice).
const char* const kTypeKeys[] = {"ps2_guitar", "xbox_guitar", "rock_band", "gamepad"};
constexpr size_t kRawEvents = 400;

std::string prefPath(const char* file) {
    return prefDir().empty() ? std::string(file) : prefDir() + file;
}

Input::Source btn(SDL_GamepadButton b) { return {Kind::Button, int(b)}; }
Input::Source axis(Kind k, SDL_GamepadAxis a) { return {k, int(a)}; }
Input::Source key(SDL_Keycode k) { return {Kind::Key, int(k)}; }

}  // namespace

const char* Input::controlName(Control c) {
    static const char* const kNames[] = {"Green Fret", "Red Fret", "Yellow Fret", "Blue Fret", "Orange Fret", "Strum Up",
                                         "Strum Down", "Whammy", "Star Power", "Start (Pause)", "Select / Confirm", "Back",
                                         "Menu Up", "Menu Down", "Menu Left", "Menu Right"};
    return kNames[size_t(c)];
}

const char* Input::profileName(Profile p) {
    switch (p) {
    case Profile::Ps2Guitar: return "PS2 guitar (adapter)";
    case Profile::XboxGuitar: return "Xbox guitar";
    case Profile::RockBand: return "Rock Band guitar";
    case Profile::Joypad: return "Gamepad";
    case Profile::Keyboard: return "Keyboard";
    }
    return "";
}

Input::Bindings Input::defaults(Profile p) {
    Bindings b;
    auto set = [&](Control c, std::vector<Source> s) { b[size_t(c)] = std::move(s); };
    switch (p) {
    case Profile::RockBand:
    case Profile::XboxGuitar:
        // The guitars select and go back with the green and red frets only
        // (App::uiButton), so Select / Back stay unbound.
        set(C::Green, {btn(SDL_GAMEPAD_BUTTON_SOUTH)});
        set(C::Red, {btn(SDL_GAMEPAD_BUTTON_EAST)});
        set(C::Yellow, {btn(SDL_GAMEPAD_BUTTON_NORTH)});
        set(C::Blue, {btn(SDL_GAMEPAD_BUTTON_WEST)});
        set(C::Orange, {btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)});
        set(C::StrumUp, {btn(SDL_GAMEPAD_BUTTON_DPAD_UP)});
        set(C::StrumDown, {btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN)});
        set(C::Whammy, {axis(Kind::AxisAny, SDL_GAMEPAD_AXIS_RIGHTX)});
        // The HID guitars' tilt bit is the right shoulder.
        if (p == Profile::RockBand)
            set(C::StarPower, {btn(SDL_GAMEPAD_BUTTON_BACK), btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)});
        else
            set(C::StarPower, {btn(SDL_GAMEPAD_BUTTON_BACK)});
        set(C::Start, {btn(SDL_GAMEPAD_BUTTON_START)});
        set(C::MenuLeft, {btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT)});
        set(C::MenuRight, {btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)});
        break;
    case Profile::Ps2Guitar:  // green is R2 (a trigger axis)
        set(C::Green, {axis(Kind::AxisPos, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
        set(C::Red, {btn(SDL_GAMEPAD_BUTTON_EAST)});
        set(C::Yellow, {btn(SDL_GAMEPAD_BUTTON_NORTH)});
        set(C::Blue, {btn(SDL_GAMEPAD_BUTTON_SOUTH)});
        set(C::Orange, {btn(SDL_GAMEPAD_BUTTON_WEST)});
        set(C::StrumUp, {btn(SDL_GAMEPAD_BUTTON_DPAD_UP)});
        set(C::StrumDown, {btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN)});
        set(C::Whammy, {axis(Kind::AxisAny, SDL_GAMEPAD_AXIS_RIGHTX), axis(Kind::AxisAny, SDL_GAMEPAD_AXIS_LEFTX)});
        set(C::StarPower, {btn(SDL_GAMEPAD_BUTTON_BACK)});
        set(C::Start, {btn(SDL_GAMEPAD_BUTTON_START)});
        set(C::MenuLeft, {btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT)});
        set(C::MenuRight, {btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)});
        break;
    case Profile::Joypad:  // L2 L1 R1 R2 Cross; the frets play only with XInput Controllers on
        set(C::Green, {axis(Kind::AxisPos, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)});
        set(C::Red, {btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)});
        set(C::Yellow, {btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)});
        set(C::Blue, {axis(Kind::AxisPos, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
        set(C::Orange, {btn(SDL_GAMEPAD_BUTTON_SOUTH)});
        set(C::Whammy, {axis(Kind::AxisAny, SDL_GAMEPAD_AXIS_LEFTX), axis(Kind::AxisAny, SDL_GAMEPAD_AXIS_RIGHTX)});
        set(C::StarPower, {btn(SDL_GAMEPAD_BUTTON_BACK)});
        set(C::Start, {btn(SDL_GAMEPAD_BUTTON_START)});
        set(C::Select, {btn(SDL_GAMEPAD_BUTTON_SOUTH)});
        set(C::Back, {btn(SDL_GAMEPAD_BUTTON_EAST)});
        set(C::MenuUp, {btn(SDL_GAMEPAD_BUTTON_DPAD_UP)});
        set(C::MenuDown, {btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN)});
        set(C::MenuLeft, {btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT)});
        set(C::MenuRight, {btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)});
        break;
    case Profile::Keyboard:  // Esc, Enter and the arrows also always work the menus (keyEvent)
        set(C::Green, {key(SDLK_1)});
        set(C::Red, {key(SDLK_2)});
        set(C::Yellow, {key(SDLK_3)});
        set(C::Blue, {key(SDLK_4)});
        set(C::Orange, {key(SDLK_5)});
        set(C::StrumUp, {key(SDLK_UP)});
        set(C::StrumDown, {key(SDLK_DOWN)});
        set(C::Whammy, {key(SDLK_W)});
        set(C::StarPower, {key(SDLK_SPACE)});
        set(C::Start, {key(SDLK_TAB)});
        set(C::Select, {key(SDLK_RETURN)});
        set(C::Back, {key(SDLK_BACKSPACE)});
        set(C::MenuLeft, {key(SDLK_LEFT)});
        set(C::MenuRight, {key(SDLK_RIGHT)});
        break;
    }
    return b;
}

void Input::setRockBand(bool on) {
    rockBand_ = on;
    if (on && !rbMappings_) {  // SDL re-announces devices a new mapping covers
        rbMappings_ = true;
        for (const char* m : kRockBandMappings)
            if (SDL_AddGamepadMapping(m) < 0) SDL_Log("rock band mapping: %s", SDL_GetError());
    }
}

void Input::close() {
    for (auto& [id, p] : pads_) SDL_CloseGamepad(p.pad);
    pads_.clear();
}

void Input::emit(std::vector<InputEvent>& out, Action a, bool down, uint64_t ts, ControllerMode mode, int player) {
    if (!inGame()) {  // the menus: one press from two devices at once acts once
        const int k = int(a);
        if (down) {
            Recent& r = recent_[size_t(k)];
            if (r.ns && ts >= r.ns && ts - r.ns < kSameNs && r.device != device_) {
                dropped_.insert({device_, k});
                return;
            }
            r = {ts, device_};
        } else if (dropped_.erase({device_, k})) {
            return;
        }
    }
    if (player == 0) lastMode_ = mode;
    out.push_back({a, down, ts, mode, player});
}

// The keyboard's chosen player, else the lowest one no pad holds (none with two pads).
int Input::keyboardSlot() const {
    if (auto it = players_.find("keyboard"); it != players_.end()) return it->second;
    for (int s = 0; s < 2; ++s) {
        bool taken = false;
        for (const auto& [id, p] : pads_) taken = taken || p.slot == s;
        if (!taken) return s;
    }
    return -1;
}

// Held by another connected pad, or chosen for the keyboard.
bool Input::slotTaken(int slot, const Pad* except) const {
    for (const auto& [id, p] : pads_)
        if (&p != except && p.slot == slot) return true;
    auto kb = players_.find("keyboard");
    return kb != players_.end() && kb->second == slot;
}

bool Input::padConnected(int slot) const {
    if (splitKeyboard_ && slot < 2) return true;
    for (const auto& [id, p] : pads_)
        if (p.slot == slot) return true;
    return keyboardSlot() == slot;
}

// ----------------------------------------------------------- persistence

void Input::load() {
    loaded_ = true;
    std::ifstream in(prefPath("controllers.json"));
    if (!in) return;
    Json root;
    try {
        root = Json::parse(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    } catch (const std::exception& ex) {
        SDL_Log("controllers.json: %s", ex.what());
        return;
    }
    const Json& t = root["touch"];
    const char* const kTouchKeys[] = {"buttons", "swipe_up", "swipe_down", "pause_corner"};
    for (int i = 0; i < kTouchOptions; ++i)
        if (t[kTouchKeys[i]].type == Json::Type::Number) touchOpt_[size_t(i)] = std::clamp(int(t[kTouchKeys[i]].n), 0, 2);
    for (const auto& [guid, d] : root["devices"].o) {
        if (d["player"].type == Json::Type::Number) players_[guid] = std::clamp(int(d["player"].n), -1, 1);
        if (guid == "keyboard") kbFretsStrum_ = d["frets_strum"].truthy();
        for (int t = 0; t < 4; ++t)
            if (d["type"].str() == kTypeKeys[t]) types_[guid] = Profile(t);
        if (d["bindings"].type != Json::Type::Object) continue;
        // A control saved empty stays unbound; one missing takes the default.
        const Bindings def = defaults(Profile::Keyboard);
        Bindings b;
        for (size_t c = 0; c < size_t(Control::Count); ++c) {
            const Json& list = d["bindings"][kControlKeys[c]];
            if (list.type != Json::Type::Array) {
                if (guid == "keyboard") b[c] = def[c];
                continue;
            }
            for (const Json& j : list.a)
                for (size_t k = 0; k < 5; ++k)
                    if (j["kind"].str() == kKindKeys[k] && j["index"].type == Json::Type::Number)
                        b[c].push_back({Kind(k), int(j["index"].n)});
        }
        overrides_[guid] = b;
    }
}

void Input::save() const {
    Json devs = Json::object();
    std::set<std::string> ids;
    for (const auto& [g, b] : overrides_) ids.insert(g);
    for (const auto& [g, p] : players_) ids.insert(g);
    for (const auto& [g, t] : types_) ids.insert(g);
    if (kbFretsStrum_) ids.insert("keyboard");
    for (const std::string& g : ids) {
        Json d = Json::object();
        if (g == "keyboard" && kbFretsStrum_) d.set("frets_strum", Json::boolean(true));
        if (auto it = players_.find(g); it != players_.end()) d.set("player", Json::number(it->second));
        if (auto it = types_.find(g); it != types_.end()) d.set("type", Json::string(kTypeKeys[size_t(it->second)]));
        for (const auto& [id, p] : pads_)
            if (p.guid == g) d.set("name", Json::string(p.name));
        if (auto it = overrides_.find(g); it != overrides_.end()) {
            Json bj = Json::object();
            for (size_t c = 0; c < size_t(Control::Count); ++c) {
                Json list = Json::array();
                for (const Source& s : it->second[c]) {
                    Json sj = Json::object();
                    sj.set("kind", Json::string(kKindKeys[size_t(s.kind)]));
                    sj.set("index", Json::number(s.index));
                    list.a.push_back(sj);
                }
                bj.set(kControlKeys[c], list);
            }
            d.set("bindings", bj);
        }
        devs.set(g, d);
    }
    Json root = Json::object();
    root.set("devices", devs);
    Json t = Json::object();
    const char* const kTouchKeys[] = {"buttons", "swipe_up", "swipe_down", "pause_corner"};
    for (int i = 0; i < kTouchOptions; ++i) t.set(kTouchKeys[i], Json::number(touchOpt_[size_t(i)]));
    root.set("touch", t);
    std::ofstream out(prefPath("controllers.json"));
    out << root.dump(2) << "\n";
    if (!out) SDL_Log("could not save controllers.json");
}

const Input::Bindings& Input::bindingsFor(const std::string& guid, Profile p) const {
    if (auto it = overrides_.find(guid); it != overrides_.end()) return it->second;
    static const std::array<Bindings, 5> kDefaults = {defaults(Profile::Ps2Guitar), defaults(Profile::XboxGuitar),
                                                      defaults(Profile::RockBand), defaults(Profile::Joypad),
                                                      defaults(Profile::Keyboard)};
    return kDefaults[size_t(p)];
}

// ----------------------------------------------------------- events

void Input::handle(const SDL_Event& e, std::vector<InputEvent>& out) {
    if (!loaded_) load();
    if (raw_.size() > kRawEvents) raw_.erase(raw_.begin(), raw_.begin() + long(raw_.size() - kRawEvents));
    switch (e.type) {
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
        raw_.push_back({e.jbutton.timestamp, e.jbutton.which, 'b', e.jbutton.button, e.jbutton.down ? 1 : 0});
        break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        raw_.push_back({e.jhat.timestamp, e.jhat.which, 'h', e.jhat.hat, e.jhat.value});
        break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
        // Axes report constantly: kept when they move a sixth of their range from the last one kept.
        int last = 0;
        for (auto r = raw_.rbegin(); r != raw_.rend(); ++r)
            if (r->id == e.jaxis.which && r->kind == 'a' && r->index == e.jaxis.axis) {
                last = r->value;
                break;
            }
        if (std::abs(int(e.jaxis.value) - last) > 10000) raw_.push_back({e.jaxis.timestamp, e.jaxis.which, 'a', e.jaxis.axis, e.jaxis.value});
        break;
    }
    case SDL_EVENT_JOYSTICK_ADDED:
        // A controller SDL has no gamepad mapping for (some PS2 adapters,
        // guitars, arcade sticks) gets a generic one, so it shows in the
        // Gamepads screen and its buttons can be bound; SDL then announces it
        // as a gamepad.
        if (!SDL_IsGamepad(e.jdevice.which)) addGenericMapping(e.jdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_ADDED: {
        SDL_JoystickID id = e.gdevice.which;
        if (pads_.count(id)) break;
        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (!pad) break;
        Profile profile = Profile::Joypad;
        if (rockBand_ && isHidRockBandGuitar(id)) profile = Profile::RockBand;
        else if (isXboxGuitar(id)) profile = Profile::XboxGuitar;
        else if (isPs2Guitar(pad)) profile = Profile::Ps2Guitar;
        Pad np;
        np.pad = pad, np.profile = np.detected = profile;
        char guid[64];
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof guid);
        np.guid = guid;
        np.name = SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "Gamepad";
        if (auto t = types_.find(np.guid); t != types_.end()) np.profile = profile = t->second;  // chosen in GAMEPADS
        // The axes' rest values as connected (a whammy bar rests at -32768).
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) np.axes[size_t(a)] = np.rest[size_t(a)] = SDL_GetGamepadAxis(pad, SDL_GamepadAxis(a));
        // Some report zeros until their first report: the rest values settle first.
        np.settleNs = SDL_GetTicksNS() + 300000000;
        // Its chosen player unless a connected device holds it, else the
        // lowest free one, else none (a third pad works the menus only).
        np.slot = -1;
        auto chosen = players_.find(np.guid);
        if (chosen != players_.end() && (chosen->second < 0 || !slotTaken(chosen->second, nullptr)))
            np.slot = chosen->second, np.chosenSlot = true;
        else
            for (int s = 0; s < 2 && np.slot < 0; ++s)
                if (!slotTaken(s, nullptr)) np.slot = s;
        pads_[id] = np;
        SDL_Log("Controller connected: %s (%04x:%04x, %s, %s)", np.name.c_str(), SDL_GetGamepadVendorForID(id),
                SDL_GetGamepadProductForID(id), profileName(profile), np.slot < 0 ? "no player" : np.slot == 0 ? "player 1" : "player 2");
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (auto it = pads_.find(e.gdevice.which); it != pads_.end()) {
            SDL_CloseGamepad(it->second.pad);
            pads_.erase(it);
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (auto it = pads_.find(e.gbutton.which); it != pads_.end() && e.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) {
            Pad& p = it->second;
            p.buttons[e.gbutton.button] = e.gbutton.down;
            device_ = int64_t(it->first);
            if (e.gbutton.down) touchUsed_ = false;
            if (e.gbutton.down && captureFrom("pad:" + std::to_string(it->first), btn(SDL_GamepadButton(e.gbutton.button)))) break;
            if (capturing_ && captureId_ == "pad:" + std::to_string(it->first)) break;
            update(p, e.gbutton.timestamp, out);
        }
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (auto it = pads_.find(e.gaxis.which); it != pads_.end() && e.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT) {
            Pad& p = it->second;
            const int a = e.gaxis.axis;
            p.axes[size_t(a)] = e.gaxis.value;
            device_ = int64_t(it->first);
            const std::string id = "pad:" + std::to_string(it->first);
            if (capturing_ && captureId_ == id) {
                int d = int(e.gaxis.value) - int(captureRest_[size_t(a)]);
                if (std::abs(d) > 20000) {
                    // The whammy and Star Power (tilt) take the axis either way; the others its direction.
                    Kind k = captureControl_ == C::Whammy || captureControl_ == C::StarPower ? Kind::AxisAny : d > 0 ? Kind::AxisPos : Kind::AxisNeg;
                    captureFrom(id, axis(k, SDL_GamepadAxis(a)));
                }
                break;
            }
            update(p, e.gaxis.timestamp, out);
        }
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        device_ = -2;
        if (touch_) touchEvent(e.tfinger, e.type == SDL_EVENT_FINGER_DOWN, out);
        break;
    case SDL_EVENT_FINGER_MOTION:
        if (touch_) touchMotion(e.tfinger);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        device_ = -1;
        if (e.key.down) touchUsed_ = false;
        if (!e.key.repeat) keyEvent(e.key, out);
        break;
    default: break;
    }
}

std::string Input::deviceType(const std::string& id) const {
    const Pad* p = padById(id);
    if (!p) return "";
    if (types_.count(p->guid)) return profileName(p->profile);
    return std::string("Auto (") + profileName(p->detected) + ")";
}

void Input::cycleDeviceType(const std::string& id, int dir) {
    Pad* p = padById(id);
    if (!p) return;
    // Auto, then the four types.
    static const int kOrder[] = {-1, int(Profile::Joypad), int(Profile::Ps2Guitar), int(Profile::XboxGuitar), int(Profile::RockBand)};
    int cur = types_.count(p->guid) ? int(types_[p->guid]) : -1, i = 0;
    for (int k = 0; k < 5; ++k)
        if (kOrder[k] == cur) i = k;
    i = (i + (dir < 0 ? 4 : 1)) % 5;
    if (kOrder[i] < 0) types_.erase(p->guid), p->profile = p->detected;
    else types_[p->guid] = p->profile = Profile(kOrder[i]);
    p->down = {};  // the controls start released under the new type
    resync(id);
    save();
}

std::string Input::reportPath() const { return prefPath("controllers-report.txt"); }

bool Input::writeReport() const {
    std::ofstream out(reportPath());
    if (!out) return false;
    const int v = SDL_GetVersion();
    out << "gd01 " << GD01_VERSION << " controller report (send this file with a bug report)\n"
        << "System: " << SDL_GetPlatform() << ", SDL " << SDL_VERSIONNUM_MAJOR(v) << "." << SDL_VERSIONNUM_MINOR(v) << "."
        << SDL_VERSIONNUM_MICRO(v) << "\nTouch controls: " << (touch_ ? "on" : "off") << ", XInput play: "
        << (gamepadPlay_ ? "on" : "off") << ", Rock Band guitars: " << (rockBand_ ? "on" : "off") << "\n\n";
    out << "Keyboard: player " << keyboardSlot() + 1 << (kbFretsStrum_ ? ", frets also strum" : "") << "\n\n";
    auto hex4 = [](unsigned x) {
        char b[8];
        std::snprintf(b, sizeof b, "%04x", x & 0xffff);
        return std::string(b);
    };
    for (const auto& [id, p] : pads_) {
        const std::string sid = "pad:" + std::to_string(id);
        SDL_Joystick* j = SDL_GetGamepadJoystick(p.pad);
        out << "Controller " << id << ": " << p.name << "\n"
            << "  USB id " << hex4(SDL_GetGamepadVendor(p.pad)) << ":" << hex4(SDL_GetGamepadProduct(p.pad))
            << ", GUID " << p.guid << ", SDL type " << (SDL_GetGamepadStringForType(SDL_GetGamepadType(p.pad)) ? SDL_GetGamepadStringForType(SDL_GetGamepadType(p.pad)) : "?")
            << ", " << (j ? SDL_GetNumJoystickButtons(j) : 0) << " buttons, " << (j ? SDL_GetNumJoystickAxes(j) : 0) << " axes, "
            << (j ? SDL_GetNumJoystickHats(j) : 0) << " hats\n"
            << "  Type: " << deviceType(sid) << ", player " << (p.slot < 0 ? std::string("none") : std::to_string(p.slot + 1)) << "\n";
        if (char* m = SDL_GetGamepadMapping(p.pad)) {
            out << "  SDL mapping: " << m << "\n";
            SDL_free(m);
        }
        const Bindings& b = bindingsFor(p.guid, p.profile);
        out << "  Bindings:";
        for (size_t c = 0; c < size_t(Control::Count); ++c) {
            std::string what;
            for (const Source& x : b[c]) what += (what.empty() ? "" : "/") + sourceName(sid, x);
            out << " " << controlName(Control(c)) << "=" << (what.empty() ? "-" : what) << ";";
        }
        out << "\n\n";
    }
    int n = 0;
    if (SDL_JoystickID* all = SDL_GetJoysticks(&n)) {
        for (int k = 0; k < n; ++k)
            if (!pads_.count(all[k]))
                out << "Joystick " << all[k] << " (not a gamepad): " << (SDL_GetJoystickNameForID(all[k]) ? SDL_GetJoystickNameForID(all[k]) : "?")
                    << ", USB id " << hex4(SDL_GetJoystickVendorForID(all[k])) << ":" << hex4(SDL_GetJoystickProductForID(all[k])) << "\n";
        SDL_free(all);
    }
    out << "\nLast raw events (the device's own button / axis / hat numbers):\n";
    for (const RawEvent& r : raw_)
        out << "  " << double(r.ns) / 1e9 << " s  controller " << r.id << "  "
            << (r.kind == 'b' ? "button " : r.kind == 'a' ? "axis " : "hat ") << r.index << " = " << r.value << "\n";
    return bool(out);
}

void Input::keyEvent(const SDL_KeyboardEvent& k, std::vector<InputEvent>& out) {
    const bool down = k.down;
    const uint64_t ts = k.timestamp;
    if (capturing_) {
        if (down && k.key == SDLK_ESCAPE) {  // Esc cancels any capture
            capturing_ = false;
            return;
        }
        if (down && k.key == SDLK_DELETE) {  // Delete leaves the control unbound
            clearBinding(captureId_, captureControl_);
            return;
        }
        if (captureId_ == "keyboard") {
            if (down) captureFrom("keyboard", key(k.key));
            if (down) keysDown_.insert(k.key);
            else keysDown_.erase(k.key);
            return;
        }
    }
    if (splitKeyboard_) {  // player 2's half of the keyboard
        auto p2 = [&](Action a) { emit(out, a, down, ts, ControllerMode::Guitar, 1); };
        switch (k.key) {
        case SDLK_H: p2(Action::Fret0); return;
        case SDLK_J: p2(Action::Fret1); return;
        case SDLK_K: p2(Action::Fret2); return;
        case SDLK_L: p2(Action::Fret3); return;
        case SDLK_SEMICOLON: p2(Action::Fret4); return;
        case SDLK_N: p2(Action::Up), p2(Action::Strum); return;
        case SDLK_M: p2(Action::Down), p2(Action::Strum); return;
        case SDLK_B: p2(Action::StarPower); return;
        case SDLK_O: p2(Action::Start); return;
        default: break;
        }
    }
    if (down) keysDown_.insert(k.key);
    else keysDown_.erase(k.key);
    const Bindings& b = bindingsFor("keyboard", Profile::Keyboard);
    const int kb = splitKeyboard_ ? 0 : keyboardSlot();
    bool bound = false;
    auto was = keyControls_;
    for (size_t c = 0; c < size_t(Control::Count); ++c) {
        bool now = false;
        for (const Source& s : b[c]) {
            const bool is = s.kind == Kind::Key && SDL_Keycode(s.index) == k.key;
            bound = bound || is;
            now = now || (s.kind == Kind::Key && keysDown_.count(SDL_Keycode(s.index)));
        }
        keyControls_[c] = now;
    }
    // "Frets also strum": the keyboard plays like the original's joypad.
    const ControllerMode mode = kbFretsStrum_ ? ControllerMode::Joypad : ControllerMode::Guitar;
    controlEdges(was, keyControls_, mode, kb, false, ts, out);
    if (bound) return;
    // Esc, Enter and the arrows work the menus whatever the bindings (or the
    // keyboard's player) are, unless bound to something else.
    Action a;
    switch (k.key) {
    case SDLK_ESCAPE: case SDLK_AC_BACK: a = Action::Back; break;  // AC_BACK: Android's back button
    case SDLK_RETURN: case SDLK_KP_ENTER: a = Action::Confirm; break;
    case SDLK_UP: a = Action::Up; break;
    case SDLK_DOWN: a = Action::Down; break;
    case SDLK_LEFT: a = Action::Left; break;
    case SDLK_RIGHT: a = Action::Right; break;
    default: return;
    }
    emit(out, a, down, ts, mode, std::max(kb, 0));
}

bool Input::sourceActive(const Pad& p, const Source& s, bool wasDown) const {
    if (s.kind == Kind::Button) return s.index >= 0 && s.index < SDL_GAMEPAD_BUTTON_COUNT && p.buttons[size_t(s.index)];
    if (s.kind == Kind::Key || s.index < 0 || s.index >= SDL_GAMEPAD_AXIS_COUNT) return false;
    // Past half way from the rest value; released below a third (no chatter).
    const int d = int(p.axes[size_t(s.index)]) - int(p.rest[size_t(s.index)]), th = wasDown ? 10000 : 16000;
    return s.kind == Kind::AxisPos ? d > th : s.kind == Kind::AxisNeg ? d < -th : std::abs(d) > th;
}

void Input::update(Pad& p, uint64_t ts, std::vector<InputEvent>& out) {
    const Bindings& b = bindingsFor(p.guid, p.profile);
    if (ts < p.settleNs) {  // just connected: its axes' rest is what they report now; nothing is pressed yet
        p.rest = p.axes;
        return;
    }
    const bool joypad = p.profile == Profile::Joypad;
    // The analog whammy: the first bound axis, from its rest (a bar resting at
    // -32768 spans the whole range; a centred stick either way).
    p.whammyAxis = 0;
    for (const Source& s : b[size_t(C::Whammy)]) {
        if (s.kind == Kind::Button || s.kind == Kind::Key) continue;
        const int rest = p.rest[size_t(s.index)], v = p.axes[size_t(s.index)];
        float x = rest < -30000 ? float(v - rest) / 65535.0f : std::abs(float(v - rest)) / 32767.0f;
        constexpr float kDead = 0.08f;
        x = std::clamp(x, 0.0f, 1.0f);
        p.whammyAxis = std::max(p.whammyAxis, x < kDead ? 0.0f : (x - kDead) / (1 - kDead));
    }
    auto was = p.down;
    for (size_t c = 0; c < size_t(Control::Count); ++c) {
        bool now = false;
        for (const Source& s : b[c]) now = now || sourceActive(p, s, was[c]);
        if (Control(c) == C::Whammy && p.whammyAxis > 0.15f) now = true;
        p.down[c] = now;
    }
    curGuitar_ = !joypad;
    controlEdges(was, p.down, modeFor(joypad), p.slot, joypad, ts, out);
    curGuitar_ = false;
}

// The controls that changed. Select / Back bound to the green / red fret's
// button would select twice (the frets already do on guitars): the fret wins.
// A device without a player works the menus as player 1, and only pauses in a song.
void Input::controlEdges(const std::array<bool, size_t(Control::Count)>& was, const std::array<bool, size_t(Control::Count)>& now,
                         ControllerMode mode, int player, bool joypad, uint64_t ts, std::vector<InputEvent>& out) {
    auto changed = [&](C c) { return was[size_t(c)] != now[size_t(c)]; };
    for (size_t c = 0; c < size_t(Control::Count); ++c) {
        if (was[c] == now[c]) continue;
        if (C(c) == C::Select && changed(C::Green) && now[size_t(C::Green)] == now[c]) continue;
        if (C(c) == C::Back && changed(C::Red) && now[size_t(C::Red)] == now[c]) continue;
        if (player < 0 && inGame() && C(c) != C::Start && C(c) != C::Back && C(c) != C::Select) continue;
        controlEdge(C(c), now[c], mode, std::max(player, 0), joypad, ts, out);
    }
}

// A control's game actions: a guitar's strum also moves the menus; a
// joypad's frets and whammy play only with XInput Controllers on.
void Input::controlEdge(Control c, bool down, ControllerMode mode, int player, bool joypad, uint64_t ts, std::vector<InputEvent>& out) {
    auto act = [&](Action a) { emit(out, a, down, ts, mode, player); };
    const bool play = !joypad || gamepadPlay_;
    const bool lefty = lefty_[size_t(player & 1)];
    switch (c) {
    case C::Green: case C::Red: case C::Yellow: case C::Blue: case C::Orange: {
        int fret = int(c);
        if (lefty && joypad) fret = 4 - fret;  // lefty_joypad: X R2 R1 L1 L2
        if (play) act(Action(int(Action::Fret0) + fret));
        break;
    }
    case C::StrumUp: act(lefty && curGuitar_ ? Action::Down : Action::Up), act(Action::Strum); break;
    case C::StrumDown: act(lefty && curGuitar_ ? Action::Up : Action::Down), act(Action::Strum); break;
    case C::Whammy: if (play) act(Action::Whammy); break;
    case C::StarPower: act(Action::StarPower); break;
    case C::Start: act(Action::Start); break;
    case C::Select: act(Action::Confirm); break;
    case C::Back: act(Action::Back); break;
    case C::MenuUp: act(Action::Up); break;
    case C::MenuDown: act(Action::Down); break;
    case C::MenuLeft: act(Action::Left); break;
    case C::MenuRight: act(Action::Right); break;
    case C::Count: break;
    }
}

float Input::whammyValue(int player) const {
    float w = 0;
    for (const auto& [id, p] : pads_)
        if (p.slot == player) w = std::max(w, p.whammyAxis);
    return w;
}

// ----------------------------------------------------------- the Gamepads screen

Input::Pad* Input::padById(const std::string& id) {
    if (id.rfind("pad:", 0) != 0) return nullptr;
    auto it = pads_.find(SDL_JoystickID(std::strtoul(id.c_str() + 4, nullptr, 10)));
    return it == pads_.end() ? nullptr : &it->second;
}

const Input::Pad* Input::padById(const std::string& id) const { return const_cast<Input*>(this)->padById(id); }

std::vector<Input::Device> Input::devices() const {
    std::vector<Device> d;
    d.push_back({"keyboard", "Keyboard", profileName(Profile::Keyboard), keyboardSlot()});
    if (touch_) d.push_back({"touch", "Touch Screen", "Touch: taps, lanes, swipes, on-screen buttons", 0});
    std::vector<SDL_JoystickID> ids;
    for (const auto& [id, p] : pads_) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (SDL_JoystickID id : ids) {
        const Pad& p = pads_.at(id);
        d.push_back({"pad:" + std::to_string(id), p.name, profileName(p.profile), p.slot});
    }
    return d;
}

void Input::setPlayer(const std::string& id, int player) {
    if (id == "touch") return;  // the touch screen always plays as player 1
    player = std::clamp(player, -1, 1);
    Pad* mine = padById(id);
    if (!mine && id != "keyboard") return;
    if (player >= 0) {  // one device per player: the one that had it swaps to this one's old player
        const int old = mine ? mine->slot : keyboardSlot();
        if (id != "keyboard" && keyboardSlot() == player) players_["keyboard"] = old;
        for (auto& [jid, p] : pads_)
            if (&p != mine && p.slot == player) p.slot = old, p.chosenSlot = true, players_[p.guid] = old;
        // Disconnected devices saved with it choose again when they come back.
        for (auto it = players_.begin(); it != players_.end();) {
            bool connected = it->first == "keyboard";
            for (const auto& [jid, p] : pads_) connected = connected || p.guid == it->first;
            it = !connected && it->second == player ? players_.erase(it) : std::next(it);
        }
    }
    if (mine) mine->slot = player, mine->chosenSlot = true, players_[mine->guid] = player;
    else players_["keyboard"] = player;
    save();
}

void Input::setKeyboardFretsStrum(bool on) {
    kbFretsStrum_ = on;
    save();
}

const Input::Bindings& Input::bindings(const std::string& id) const {
    if (const Pad* p = padById(id)) return bindingsFor(p->guid, p->profile);
    return bindingsFor("keyboard", Profile::Keyboard);
}

std::string Input::sourceName(const std::string& id, const Source& s) const {
    if (s.kind == Kind::Key) return SDL_GetKeyName(SDL_Keycode(s.index));
    if (s.kind == Kind::Button) {
        static const char* const kButtons[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "Left Stick", "Right Stick",
                                               "LB", "RB", "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right", "Misc",
                                               "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad", "Misc 2",
                                               "Misc 3", "Misc 4", "Misc 5", "Misc 6"};
        // PlayStation-style pads show their face-button symbols' names.
        if (const Pad* p = padById(id); p && s.index <= SDL_GAMEPAD_BUTTON_NORTH) {
            SDL_GamepadButtonLabel l = SDL_GetGamepadButtonLabel(p->pad, SDL_GamepadButton(s.index));
            if (l == SDL_GAMEPAD_BUTTON_LABEL_CROSS) return "Cross";
            if (l == SDL_GAMEPAD_BUTTON_LABEL_CIRCLE) return "Circle";
            if (l == SDL_GAMEPAD_BUTTON_LABEL_SQUARE) return "Square";
            if (l == SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE) return "Triangle";
        }
        return s.index >= 0 && s.index < int(std::size(kButtons)) ? kButtons[s.index] : "Button " + std::to_string(s.index);
    }
    static const char* const kAxes[] = {"Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y", "LT", "RT"};
    std::string n = s.index >= 0 && s.index < 6 ? kAxes[s.index] : "Axis " + std::to_string(s.index);
    if (s.index >= 4) return n;  // triggers: one way
    return n + (s.kind == Kind::AxisPos ? " +" : s.kind == Kind::AxisNeg ? " -" : "");
}

bool Input::controlDown(const std::string& id, Control c) const {
    if (const Pad* p = padById(id)) return p->down[size_t(c)];
    return id == "keyboard" && keyControls_[size_t(c)];
}

std::vector<std::string> Input::rawPressed(const std::string& id) const {
    std::vector<std::string> v;
    if (const Pad* p = padById(id)) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (p->buttons[size_t(b)]) v.push_back(sourceName(id, btn(SDL_GamepadButton(b))));
    } else if (id == "keyboard") {
        for (SDL_Keycode k : keysDown_) v.push_back(SDL_GetKeyName(k));
    }
    return v;
}

std::vector<std::pair<std::string, float>> Input::rawAxes(const std::string& id) const {
    std::vector<std::pair<std::string, float>> v;
    if (const Pad* p = padById(id))
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            v.push_back({sourceName(id, axis(Kind::AxisAny, SDL_GamepadAxis(a))), float(p->axes[size_t(a)]) / 32767.0f});
    return v;
}

void Input::beginCapture(const std::string& id, Control c) {
    capturing_ = true;
    captureId_ = id;
    captureControl_ = c;
    if (const Pad* p = padById(id)) captureRest_ = p->axes;
}

bool Input::captureFrom(const std::string& id, const Source& s) {
    if (!capturing_ || id != captureId_) return false;
    capturing_ = false;
    setBinding(id, captureControl_, {s});
    return true;
}

void Input::clearBinding(const std::string& id, Control c) {
    if (capturing_ && captureId_ == id && captureControl_ == c) capturing_ = false;
    setBinding(id, c, {});
}

void Input::setBinding(const std::string& id, Control c, std::vector<Source> s) {
    std::string guid = "keyboard";
    Profile profile = Profile::Keyboard;
    if (const Pad* p = padById(id)) guid = p->guid, profile = p->profile;
    else if (id != "keyboard") return;
    Bindings b = bindingsFor(guid, profile);
    b[size_t(c)] = std::move(s);
    overrides_[guid] = b;
    save();
    resync(id);
}

// After the bindings change: the controls take the device's current state without an action.
void Input::resync(const std::string& id) {
    if (Pad* p = padById(id)) {
        const Bindings& b = bindingsFor(p->guid, p->profile);
        for (size_t c = 0; c < size_t(Control::Count); ++c) {
            bool now = false;
            for (const Source& x : b[c]) now = now || sourceActive(*p, x, p->down[c]);
            p->down[c] = now;
        }
    } else if (id == "keyboard") {
        const Bindings& b = bindingsFor("keyboard", Profile::Keyboard);
        for (size_t c = 0; c < size_t(Control::Count); ++c) {
            bool now = false;
            for (const Source& x : b[c]) now = now || (x.kind == Kind::Key && keysDown_.count(SDL_Keycode(x.index)));
            keyControls_[c] = now;
        }
    }
}

void Input::resetBindings(const std::string& id) {
    if (const Pad* p = padById(id)) overrides_.erase(p->guid);
    else if (id == "keyboard") overrides_.erase("keyboard");
    resync(id);
    save();
}

void Input::addGenericMapping(SDL_JoystickID id) {
    SDL_Joystick* j = SDL_OpenJoystick(id);
    if (!j) return;
    const int buttons = SDL_GetNumJoystickButtons(j), axes = SDL_GetNumJoystickAxes(j), hats = SDL_GetNumJoystickHats(j);
    std::string name = SDL_GetJoystickName(j) ? SDL_GetJoystickName(j) : "Controller";
    char guid[64];
    SDL_GUIDToString(SDL_GetJoystickGUID(j), guid, sizeof guid);
    SDL_CloseJoystick(j);
    if (buttons <= 0) return;  // sensors and the like
    for (char& c : name)
        if (c == ',') c = ' ';
    // Buttons and axes in order; the first hat is the d-pad. The Gamepads
    // screen rebinds anything (captured from this mapping).
    static const char* const kButtons[] = {"a", "b", "x", "y", "leftshoulder", "rightshoulder", "back", "start",
                                           "leftstick", "rightstick", "guide", "misc1", "paddle1", "paddle2",
                                           "paddle3", "paddle4", "misc2", "misc3", "misc4", "misc5", "misc6", "touchpad"};
    static const char* const kAxes[] = {"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
    std::string m = std::string(guid) + "," + name + " (generic)";
    for (int b = 0; b < buttons && b < int(std::size(kButtons)); ++b) m += std::string(",") + kButtons[b] + ":b" + std::to_string(b);
    for (int a = 0; a < axes && a < int(std::size(kAxes)); ++a) m += std::string(",") + kAxes[a] + ":a" + std::to_string(a);
    if (hats > 0) m += ",dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8";
    m += ",";
    if (SDL_AddGamepadMapping(m.c_str()) < 0) SDL_Log("generic mapping for %s: %s", name.c_str(), SDL_GetError());
    else SDL_Log("Controller without a known mapping: %s (%d buttons, %d axes, %d hats), generic mapping added", name.c_str(), buttons, axes, hats);
}

// ------------------------------------------------------------------- touch

const char* Input::touchOptionName(int i) {
    static const char* const kNames[] = {"On-Screen Buttons", "Two-Finger Swipe Up (in a song)",
                                         "Two-Finger Swipe Down (in a song)", "Pause Corner (in a song)"};
    return i >= 0 && i < kTouchOptions ? kNames[i] : "";
}

std::string Input::touchOptionValue(int i) const {
    static const char* const kValues[kTouchOptions][3] = {{"When touched", "Always", "Off"},
                                                          {"Star Power", "Pause", "Nothing"},
                                                          {"Star Power", "Pause", "Nothing"},
                                                          {"Top right", "Top left", "None"}};
    return i >= 0 && i < kTouchOptions ? kValues[i][touchOpt_[size_t(i)]] : "";
}

void Input::cycleTouchOption(int i, int dir) {
    if (i < 0 || i >= kTouchOptions) return;
    touchOpt_[size_t(i)] = (touchOpt_[size_t(i)] + (dir < 0 ? 2 : 1)) % 3;
    save();
}

void Input::resetTouchOptions() {
    touchOpt_ = {0, 0, 1, 0};
    save();
}

std::vector<Input::TouchButton> Input::touchButtons() const {
    std::vector<TouchButton> b;
    if (!touch_ || touchOpt_[0] == 2 || (touchOpt_[0] == 0 && !touchUsed_)) return b;
    if (inGame()) {
        if (touchOpt_[3] == 0) b.push_back({Action::Start, 1280 - 42, 46, kPauseR});
        if (touchOpt_[3] == 1) b.push_back({Action::Start, 42, 46, kPauseR});
        return b;
    }
    b.push_back({Action::Up, kButtonX, 300, kButtonR});
    b.push_back({Action::Down, kButtonX, 386, kButtonR});
    b.push_back({Action::Start, kButtonX, 472, kButtonR});
    return b;
}

// A two-finger swipe: in a song what the settings say, in the menus Up / Down.
void Input::touchGesture(bool up, bool game, uint64_t ts, std::vector<InputEvent>& out) {
    Action a = up ? Action::Up : Action::Down;
    if (game) {
        const int what = touchOpt_[up ? 1 : 2];
        if (what == 2) return;
        a = what == 0 ? Action::StarPower : Action::Start;
    }
    emit(out, a, true, ts, ControllerMode::Joypad, 0);
    emit(out, a, false, ts, ControllerMode::Joypad, 0);
}

void Input::touchMotion(const SDL_TouchFingerEvent& f) {
    auto it = fingers_.find(f.fingerID);
    if (it == fingers_.end() || !toMenu_) return;
    Finger& fg = it->second;
    toMenu_(f.x, f.y, fg.mx, fg.my);
    if (std::abs(fg.mx - fg.x0) > kSlide || std::abs(fg.my - fg.y0) > kSlide) fg.moved = true;
}

void Input::touchEvent(const SDL_TouchFingerEvent& f, bool down, std::vector<InputEvent>& out) {
    const uint64_t ts = f.timestamp;
    const bool game = inGame();
    if (!down) {  // lifted or cancelled
        auto it = fingers_.find(f.fingerID);
        if (it == fingers_.end()) return;
        Finger fg = it->second;
        fingers_.erase(it);
        if (toMenu_) toMenu_(f.x, f.y, fg.mx, fg.my);
        if (std::abs(fg.mx - fg.x0) > kSlide || std::abs(fg.my - fg.y0) > kSlide) fg.moved = true;
        if (fg.button >= 0) return emit(out, Action(fg.button), false, ts, ControllerMode::Joypad, 0);
        if (fg.fret >= 0) return emit(out, Action(int(Action::Fret0) + fg.fret), false, ts, ControllerMode::Joypad, fg.player);
        if (fg.done) return;
        // With its partner finger (landed together, not on a lane or button):
        // both slid the same way up or down -> a swipe; neither slid -> in a
        // song, a two-finger tap (Star Power).
        for (auto& [id, o] : fingers_) {
            if (o.fret >= 0 || o.button >= 0 || o.done || !fg.multi || !o.multi) continue;
            if (fg.downNs > o.downNs ? fg.downNs - o.downNs > kTwoFingerNs : o.downNs - fg.downNs > kTwoFingerNs) continue;
            const float dy = fg.my - fg.y0, ody = o.my - o.y0;
            if (std::abs(dy) > kSwipe && std::abs(ody) > kSwipe && (dy < 0) == (ody < 0) && std::abs(dy) > std::abs(fg.mx - fg.x0)) {
                touchGesture(dy < 0, game, ts, out);
                o.done = true;
            } else if (game && !fg.moved && !o.moved) {
                emit(out, Action::StarPower, true, ts, ControllerMode::Joypad, 0);
                emit(out, Action::StarPower, false, ts, ControllerMode::Joypad, 0);
                o.done = true;
            }
            break;
        }
        // The menus: a lone finger that did not slide is a click where it landed.
        if (!game && !fg.multi && !fg.moved) menuTaps_.push_back({fg.x0, fg.y0});
        return;
    }
    touchUsed_ = true;
    float mx = 0, my = 0;
    if (toMenu_) toMenu_(f.x, f.y, mx, my);
    Finger fg{0, -1, mx, my, ts, mx, my};
    // An on-screen button: held while the finger stays down.
    for (const TouchButton& b : touchButtons())
        if (std::hypot(mx - b.x, my - b.y) <= b.r * 1.3f) {
            fg.button = int(b.action);
            fingers_[f.fingerID] = fg;
            return emit(out, b.action, true, ts, ControllerMode::Joypad, 0);
        }
    // Landing with another free finger (within 250 ms): a two-finger tap or swipe.
    for (auto& [id, o] : fingers_)
        if (o.fret < 0 && o.button < 0 && ts - o.downNs < kTwoFingerNs) o.multi = fg.multi = true;
    if (!game) {
        fingers_[f.fingerID] = fg;
        return;
    }
    const bool corner = touchOpt_[3] == 0 ? mx >= kPauseX0 && my <= kPauseY1 : touchOpt_[3] == 1 && mx <= 1280 - kPauseX0 && my <= kPauseY1;
    if (corner) {
        emit(out, Action::Start, true, ts, ControllerMode::Joypad, 0);
        emit(out, Action::Start, false, ts, ControllerMode::Joypad, 0);
        return;
    }
    // The nearest lane at the finger's height, within 0.6 of the lane spacing.
    float best = 1e9f;
    for (int p = 0; p < 2; ++p) {
        const TouchLanes& l = lanes_[size_t(p)];
        if (!l.active) continue;
        float dy = l.farY[2] - l.nearY[2];
        if (std::abs(dy) < 1) continue;
        float u = (my - l.nearY[2]) / dy;
        if (u > 1) continue;  // above the highway's far end
        auto laneX = [&](int k) { return l.nearX[size_t(k)] + (l.farX[size_t(k)] - l.nearX[size_t(k)]) * std::max(u, 0.0f); };
        float spacing = std::abs(laneX(1) - laneX(0));
        for (int k = 0; k < 5; ++k) {
            float d = std::abs(mx - laneX(k));
            if (d < best && d < spacing * 0.6f) best = d, fg.fret = k, fg.player = p;
        }
    }
    if (fg.fret >= 0) {
        fg.multi = false;
        emit(out, Action(int(Action::Fret0) + fg.fret), true, ts, ControllerMode::Joypad, fg.player);
        PendingStrum& s = strum_[size_t(fg.player)];
        if (!s.on) s.on = true, s.dueNs = ts + kChordNs;
        s.ts = ts;  // strum with the chord's last finger
        fingers_[f.fingerID] = fg;
        return;
    }
    // Off the lanes: a tap on the Star Power meter deploys at once.
    if (mx >= kMeterX0 && mx <= kMeterX1 && my >= kMeterY0 && !fg.multi) {
        emit(out, Action::StarPower, true, ts, ControllerMode::Joypad, 0);
        emit(out, Action::StarPower, false, ts, ControllerMode::Joypad, 0);
        fg.done = true;
    }
    fingers_[f.fingerID] = fg;
}

void Input::poll(uint64_t nowNs, std::vector<InputEvent>& out) {
    for (int p = 0; p < 2; ++p) {
        PendingStrum& s = strum_[size_t(p)];
        if (!s.on || nowNs < s.dueNs) continue;
        s.on = false;
        emit(out, Action::Strum, true, s.ts, ControllerMode::Joypad, p);
        emit(out, Action::Strum, false, s.ts, ControllerMode::Joypad, p);
    }
    if (!lanes_[0].active && !lanes_[1].active) {  // left the game: no fret stays held
        for (auto& [id, fg] : fingers_) fg.fret = -1;
    }
}
