// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "input/input.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "data/json.h"
#include "platform/paths.h"

namespace {

using Kind = Input::Source::Kind;
using C = Input::Control;

constexpr uint64_t kSettleNs = 300000000;  // a pad's axes settle after it connects

const char* const kControlKeys[] = {"start", "select", "up", "down", "left", "right", "cross", "circle", "square", "triangle",
                                    "l", "r", "guitar_start", "guitar_select", "strum_up", "strum_down", "green", "red",
                                    "yellow", "blue", "orange", "whammy", "gyro"};
const char* const kKindKeys[] = {"button", "axis_pos", "axis_neg", "axis_any", "key"};
const char* const kInstrumentKeys[] = {"guitar", "gamepad"};

Input::Source btn(SDL_GamepadButton b) { return {Kind::Button, int(b)}; }
Input::Source axis(Kind k, SDL_GamepadAxis a) { return {k, int(a)}; }
Input::Source key(SDL_Keycode k) { return {Kind::Key, int(k)}; }

std::string lower(const char* s) {
    std::string n = s ? s : "";
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return n;
}

// Guitar detection, gd-game-10's (input.cpp): Xbox 360 guitars by SDL's type
// or the RedOctane / Harmonix vendor, the HID Rock Band guitars by USB id,
// PS2 guitars through an adapter and Santroller boards by name.
bool isXboxGuitar(SDL_JoystickID id) {
    if (SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_GUITAR) return true;
    const uint16_t vendor = SDL_GetGamepadVendorForID(id);
    const SDL_GamepadType type = SDL_GetGamepadTypeForID(id);
    return (vendor == 0x1bad || vendor == 0x1430) && (type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE);
}
bool isHidRockBandGuitar(SDL_JoystickID id) {
    static const std::pair<uint16_t, uint16_t> kIds[] = {{0x12ba, 0x0200}, {0x1bad, 0x0004}, {0x1bad, 0x3010}, {0x0738, 0x8261},
                                                         {0x0e6f, 0x0173}, {0x0e6f, 0x0248}, {0x0738, 0x4161}, {0x0e6f, 0x0170}};
    const uint16_t vendor = SDL_GetGamepadVendorForID(id), product = SDL_GetGamepadProductForID(id);
    for (const auto& [v, p] : kIds)
        if (vendor == v && product == p) return true;
    return false;
}
bool isPs2Guitar(SDL_Gamepad* pad) {
    const std::string n = lower(SDL_GetGamepadName(pad));
    return n.find("guitar") != std::string::npos && n.find("rock band") == std::string::npos &&
           n.find("harmonix") == std::string::npos && n.find("xbox") == std::string::npos;
}
bool isSantrollerGuitar(SDL_Gamepad* pad) {
    const std::string n = lower(SDL_GetGamepadName(pad));
    if (n.find("santroller") == std::string::npos) return false;
    for (const char* other : {"drum", "dance", "turntable", "dj", "keyboard", "wheel", "stage kit"})
        if (n.find(other) != std::string::npos) return false;
    return true;
}
// The HID Rock Band guitars have no SDL mapping: the Xbox-guitar layout (gd10's kRockBandMappings, [I]).
const char* const kRockBandMappings[] = {
    "03000000ba1200000002000000000000,Harmonix Guitar for PlayStation 3,a:b1,b:b2,x:b0,y:b3,leftshoulder:b4,"
    "rightshoulder:b5,back:b8,start:b9,guide:b12,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,rightx:a2,righty:a3,",
    "03000000ad1b00000400000000000000,Harmonix Guitar for Nintendo Wii,a:b1,b:b2,x:b0,y:b3,leftshoulder:b4,"
    "rightshoulder:b5,back:b8,start:b9,guide:b12,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,rightx:a2,righty:a3,",
    "03000000ad1b00001030000000000000,Harmonix Guitar for Nintendo Wii (RB2),a:b1,b:b2,x:b0,y:b3,leftshoulder:b4,"
    "rightshoulder:b5,back:b8,start:b9,guide:b12,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,rightx:a2,righty:a3,",
};

}  // namespace

const char* actionName(Action a) {
    static const char* const kNames[] = {"Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle", "L", "R",
                                         "Start", "Select", "Lane0", "Lane1", "Lane2", "Lane3", "Overdrive", "TrackLeft",
                                         "TrackRight"};
    return kNames[size_t(a)];
}

const char* Input::controlName(Control c) {
    static const char* const kNames[] = {"Start", "Select", "Up", "Down", "Left", "Right", "Cross", "Circle", "Square",
                                         "Triangle", "L", "R", "Start", "Select", "Strum up", "Strum down", "Green fret",
                                         "Red fret", "Yellow fret", "Blue fret", "Orange fret", "Whammy",
                                         "Gyro special trigger"};
    return kNames[size_t(c)];
}

int Input::controlSection(Control c) { return c <= C::Down ? 0 : c <= C::R ? 1 : 2; }

const char* Input::instrumentName(Instrument i) { return i == Instrument::Guitar ? "5 fret guitar" : "Gamepad"; }

// The defaults (universal_mechanisms.md 1.1.4 adapted to the PSP's buttons):
//   keyboard: the arrows are the d-pad, 1 2 3 4 (F1..F4) the lanes' buttons
//     L Up Triangle R, Space / 5 Cross, Backspace Circle, Q Square, Enter
//     Start, Tab Select; Esc / Backspace / Enter / the arrows always work
//     the menus (keyEvent);
//   gamepad: the PSP's buttons where they sit on a pad (Cross south, Circle
//     east, Square west, Triangle north, L / R the shoulders and triggers,
//     the d-pad and the left stick, Start, Back as Select);
//   guitars: gd10's guitar layouts (green A / south, red B, yellow Y, blue
//     X, orange LB, strum the d-pad, whammy right stick X, gyro right stick
//     Y and Back).
Input::Bindings Input::defaults(Profile p) {
    Bindings b;
    auto set = [&](Control c, std::vector<Source> s) { b[size_t(c)] = std::move(s); };
    switch (p) {
    case Profile::Keyboard:
        set(C::Start, {key(SDLK_RETURN)});
        set(C::Select, {key(SDLK_TAB)});
        set(C::Up, {key(SDLK_UP), key(SDLK_2)});
        set(C::Down, {key(SDLK_DOWN)});
        set(C::Left, {key(SDLK_LEFT)});
        set(C::Right, {key(SDLK_RIGHT)});
        set(C::Cross, {key(SDLK_SPACE), key(SDLK_5)});
        set(C::Circle, {key(SDLK_BACKSPACE)});
        set(C::Square, {key(SDLK_Q)});
        set(C::Triangle, {key(SDLK_3), key(SDLK_F3)});
        set(C::L, {key(SDLK_1), key(SDLK_F1)});
        set(C::R, {key(SDLK_4), key(SDLK_F4)});
        break;
    case Profile::Gamepad:
        set(C::Start, {btn(SDL_GAMEPAD_BUTTON_START)});
        set(C::Select, {btn(SDL_GAMEPAD_BUTTON_BACK)});
        set(C::Up, {btn(SDL_GAMEPAD_BUTTON_DPAD_UP), axis(Kind::AxisNeg, SDL_GAMEPAD_AXIS_LEFTY)});
        set(C::Down, {btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN), axis(Kind::AxisPos, SDL_GAMEPAD_AXIS_LEFTY)});
        set(C::Left, {btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT), axis(Kind::AxisNeg, SDL_GAMEPAD_AXIS_LEFTX)});
        set(C::Right, {btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), axis(Kind::AxisPos, SDL_GAMEPAD_AXIS_LEFTX)});
        set(C::Cross, {btn(SDL_GAMEPAD_BUTTON_SOUTH)});
        set(C::Circle, {btn(SDL_GAMEPAD_BUTTON_EAST)});
        set(C::Square, {btn(SDL_GAMEPAD_BUTTON_WEST)});
        set(C::Triangle, {btn(SDL_GAMEPAD_BUTTON_NORTH)});
        set(C::L, {btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), axis(Kind::AxisPos, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)});
        set(C::R, {btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), axis(Kind::AxisPos, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
        break;
    case Profile::XboxGuitar:
    case Profile::RockBandGuitar:
    case Profile::Ps2Guitar:
        set(C::Start, {btn(SDL_GAMEPAD_BUTTON_START)});
        set(C::Select, {btn(SDL_GAMEPAD_BUTTON_BACK)});
        set(C::Up, {btn(SDL_GAMEPAD_BUTTON_DPAD_UP)});
        set(C::Down, {btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN)});
        set(C::GuitarStart, {btn(SDL_GAMEPAD_BUTTON_START)});
        set(C::GuitarSelect, {btn(SDL_GAMEPAD_BUTTON_BACK)});
        set(C::StrumUp, {btn(SDL_GAMEPAD_BUTTON_DPAD_UP)});
        set(C::StrumDown, {btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN)});
        if (p == Profile::Ps2Guitar) {
            set(C::Green, {axis(Kind::AxisPos, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
            set(C::Red, {btn(SDL_GAMEPAD_BUTTON_EAST)});
            set(C::Yellow, {btn(SDL_GAMEPAD_BUTTON_NORTH)});
            set(C::Blue, {btn(SDL_GAMEPAD_BUTTON_SOUTH)});
            set(C::Orange, {btn(SDL_GAMEPAD_BUTTON_WEST)});
            set(C::Whammy, {axis(Kind::AxisAny, SDL_GAMEPAD_AXIS_RIGHTX)});
            set(C::Gyro, {btn(SDL_GAMEPAD_BUTTON_BACK)});
        } else {
            set(C::Green, {btn(SDL_GAMEPAD_BUTTON_SOUTH)});
            set(C::Red, {btn(SDL_GAMEPAD_BUTTON_EAST)});
            set(C::Yellow, {btn(SDL_GAMEPAD_BUTTON_NORTH)});
            set(C::Blue, {btn(SDL_GAMEPAD_BUTTON_WEST)});
            set(C::Orange, {btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)});
            set(C::Whammy, {axis(Kind::AxisAny, SDL_GAMEPAD_AXIS_RIGHTX)});
            if (p == Profile::RockBandGuitar)
                set(C::Gyro, {btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), btn(SDL_GAMEPAD_BUTTON_BACK)});
            else
                set(C::Gyro, {axis(Kind::AxisAny, SDL_GAMEPAD_AXIS_RIGHTY), btn(SDL_GAMEPAD_BUTTON_BACK)});
        }
        break;
    }
    return b;
}

// The actions held controls give. PSP-layout devices (keyboard, gamepads):
// each PSP button control is that button. Guitars: in the menus green
// confirms (Cross), red goes back (Circle), the strum bar moves; in a song
// the frets are the four lanes, orange and the gyro Overdrive, the strum
// bar switches tracks [I: the port's choice; Rock Band Unplugged has no
// guitar]. Start / Select from either section.
std::array<bool, size_t(Action::Count)> Input::actionsOf(const std::array<bool, size_t(Control::Count)>& h, bool guitar) const {
    std::array<bool, size_t(Action::Count)> a{};
    auto on = [&](Control c) { return h[size_t(c)]; };
    auto add = [&](Action x, bool v) { a[size_t(x)] = a[size_t(x)] || v; };
    add(Action::Start, on(C::Start) || on(C::GuitarStart));
    add(Action::Select, on(C::Select) || on(C::GuitarSelect));
    add(Action::Up, on(C::Up));
    add(Action::Down, on(C::Down));
    add(Action::Left, on(C::Left));
    add(Action::Right, on(C::Right));
    add(Action::Cross, on(C::Cross));
    add(Action::Circle, on(C::Circle));
    add(Action::Square, on(C::Square));
    add(Action::Triangle, on(C::Triangle));
    add(Action::L, on(C::L));
    add(Action::R, on(C::R));
    if (guitar) {
        if (inGame_) {
            add(Action::Lane0, on(C::Green));
            add(Action::Lane1, on(C::Red));
            add(Action::Lane2, on(C::Yellow));
            add(Action::Lane3, on(C::Blue));
            add(Action::Overdrive, on(C::Orange) || on(C::Gyro));
            add(Action::TrackLeft, on(C::StrumUp));
            add(Action::TrackRight, on(C::StrumDown));
        } else {
            add(Action::Cross, on(C::Green));
            add(Action::Circle, on(C::Red));
            add(Action::Up, on(C::StrumUp));
            add(Action::Down, on(C::StrumDown));
        }
    }
    return a;
}

void Input::edges(std::array<bool, size_t(Action::Count)>& was, const std::array<bool, size_t(Action::Count)>& now, uint64_t ts,
                  std::vector<InputEvent>& out) {
    for (size_t i = 0; i < size_t(Action::Count); ++i)
        if (was[i] != now[i]) out.push_back({Action(i), now[i], ts, 0});
    was = now;
}

void Input::prepareSdl() {
    // Started by Steam, take Steam's virtual pads (gd10's Input::prepareSdl).
    SDL_SetHint("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1");
}

Input::Input() = default;
Input::~Input() { close(); }

void Input::close() {
    for (auto& [id, p] : pads_)
        if (p.pad) SDL_CloseGamepad(p.pad);
    pads_.clear();
}

void Input::setSaveDir(std::string dir) {
    saveDir_ = std::move(dir), loaded_ = false;
    overrides_.clear(), instruments_.clear(), players_.clear();
}

std::string Input::savePath() const { return (saveDir_.empty() ? prefDir() : saveDir_) + "controllers.json"; }

void Input::load() {
    loaded_ = true;
    std::ifstream in(savePath());
    if (!in) return;
    Json root;
    try {
        root = Json::parse(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    } catch (const std::exception& ex) {
        SDL_Log("controllers.json: %s", ex.what());
        return;
    }
    for (const auto& [guid, d] : root["devices"].o) {
        if (d["player"].type == Json::Type::Number) players_[guid] = std::clamp(int(d["player"].n), -1, kPlayers - 1);
        for (size_t i = 0; i < 2; ++i)
            if (d["instrument"].str() == kInstrumentKeys[i]) instruments_[guid] = Instrument(i);
        if (d["bindings"].type != Json::Type::Object) continue;
        Bindings b;
        for (size_t c = 0; c < size_t(Control::Count); ++c)
            for (const Json& j : d["bindings"][kControlKeys[c]].a)
                for (size_t k = 0; k < 5; ++k)
                    if (j["kind"].str() == kKindKeys[k] && j["index"].type == Json::Type::Number && b[c].size() < kSlots)
                        b[c].push_back({Kind(k), int(j["index"].n)});
        overrides_[guid] = b;
    }
}

void Input::save() const {
    if (saveHeld_) return;
    std::set<std::string> ids;
    for (const auto& [g, b] : overrides_) ids.insert(g);
    for (const auto& [g, i] : instruments_) ids.insert(g);
    for (const auto& [g, p] : players_) ids.insert(g);
    Json devs = Json::object();
    for (const std::string& g : ids) {
        Json d = Json::object();
        for (const auto& [id, p] : pads_)
            if (p.guid == g) d.set("name", Json::string(p.name));
        if (auto it = players_.find(g); it != players_.end()) d.set("player", Json::number(it->second));
        if (auto it = instruments_.find(g); it != instruments_.end()) d.set("instrument", Json::string(kInstrumentKeys[size_t(it->second)]));
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
    root.set("version", Json::number(1));
    root.set("devices", devs);
    std::ofstream out(savePath());
    out << root.dump(2) << "\n";
    if (!out) SDL_Log("could not save controllers.json");
}

const Input::Bindings& Input::bindingsFor(const std::string& guid, Profile p) const {
    if (!loaded_) const_cast<Input*>(this)->load();
    if (auto it = overrides_.find(guid); it != overrides_.end()) return it->second;
    static std::map<Profile, Bindings> cache;
    auto it = cache.find(p);
    if (it == cache.end()) it = cache.emplace(p, defaults(p)).first;
    return it->second;
}

Input::Pad* Input::padById(const std::string& id) {
    if (id.rfind("pad:", 0) != 0) return nullptr;
    auto it = pads_.find(SDL_JoystickID(std::strtoul(id.c_str() + 4, nullptr, 10)));
    return it == pads_.end() ? nullptr : &it->second;
}
const Input::Pad* Input::padById(const std::string& id) const { return const_cast<Input*>(this)->padById(id); }

std::string Input::guidOf(const std::string& id) const {
    if (id == "keyboard") return id;
    const Pad* p = padById(id);
    return p ? p->guid : std::string();
}

Input::Profile Input::profileOf(const std::string& id) const {
    if (id == "keyboard") return Profile::Keyboard;
    const Pad* p = padById(id);
    return p ? p->profile : Profile::Gamepad;
}

void Input::handle(const SDL_Event& e, std::vector<InputEvent>& out) {
    const uint64_t ts = e.common.timestamp ? e.common.timestamp : SDL_GetTicksNS();
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED: {
        const SDL_JoystickID id = e.gdevice.which;
        if (pads_.count(id)) break;
        if (isHidRockBandGuitar(id)) {
            char guid[64];
            SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof guid);
            for (const char* m : kRockBandMappings)
                if (std::string(m).compare(0, 32, guid) == 0) SDL_AddGamepadMapping(m);
        }
        SDL_Gamepad* g = SDL_OpenGamepad(id);
        if (!g) break;
        Pad p;
        p.pad = g;
        char guid[64];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof guid);
        p.guid = guid;
        p.name = SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "Gamepad";
        p.detected = isHidRockBandGuitar(id) ? Profile::RockBandGuitar
                     : isXboxGuitar(id) || isSantrollerGuitar(g) ? Profile::XboxGuitar
                     : isPs2Guitar(g) ? Profile::Ps2Guitar
                                      : Profile::Gamepad;
        if (!loaded_) load();
        auto inst = instruments_.find(p.guid);
        p.profile = inst == instruments_.end() ? p.detected
                    : inst->second == Instrument::Gamepad ? Profile::Gamepad
                    : isGuitar(p.detected) ? p.detected
                                           : Profile::XboxGuitar;
        p.settleNs = ts + kSettleNs;
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) p.axes[size_t(a)] = p.rest[size_t(a)] = SDL_GetGamepadAxis(g, SDL_GamepadAxis(a));
        SDL_Log("input: %s connected (%s, %04x:%04x)", p.name.c_str(), isGuitar(p.profile) ? "guitar" : "gamepad",
                SDL_GetGamepadVendor(g), SDL_GetGamepadProduct(g));
        pads_[id] = p;
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
        auto it = pads_.find(e.gdevice.which);
        if (it == pads_.end()) break;
        std::array<bool, size_t(Action::Count)> none{};
        edges(padActions_[it->first], none, ts, out);
        SDL_CloseGamepad(it->second.pad);
        pads_.erase(it);
        padActions_.erase(e.gdevice.which);
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        auto it = pads_.find(e.gbutton.which);
        if (it == pads_.end() || e.gbutton.button >= SDL_GAMEPAD_BUTTON_COUNT) break;
        Pad& p = it->second;
        p.buttons[e.gbutton.button] = e.gbutton.down;
        const std::string id = "pad:" + std::to_string(it->first);
        if (e.gbutton.down && detect_ >= 0 && detectFrom(id)) break;
        if (e.gbutton.down && capturing_ && captureId_ == id && captureFrom(id, btn(SDL_GamepadButton(e.gbutton.button)))) break;
        update(p, ts, out);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        auto it = pads_.find(e.gaxis.which);
        if (it == pads_.end() || e.gaxis.axis >= SDL_GAMEPAD_AXIS_COUNT) break;
        Pad& p = it->second;
        p.axes[e.gaxis.axis] = e.gaxis.value;
        if (ts < p.settleNs) {
            p.rest[e.gaxis.axis] = e.gaxis.value;
            break;
        }
        const std::string id = "pad:" + std::to_string(it->first);
        const int d = int(e.gaxis.value) - int(p.rest[e.gaxis.axis]);
        if (capturing_ && captureId_ == id && std::abs(d) > 16000 &&
            captureFrom(id, axis(d > 0 ? Kind::AxisPos : Kind::AxisNeg, SDL_GamepadAxis(e.gaxis.axis))))
            break;
        update(p, ts, out);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        keyEvent(e.key, out);
        break;
    default:
        break;
    }
}

bool Input::sourceActive(const Pad& p, const Source& s) const {
    switch (s.kind) {
    case Kind::Button: return s.index >= 0 && s.index < SDL_GAMEPAD_BUTTON_COUNT && p.buttons[size_t(s.index)];
    case Kind::Key: return false;
    default: break;
    }
    if (s.index < 0 || s.index >= SDL_GAMEPAD_AXIS_COUNT) return false;
    const int d = int(p.axes[size_t(s.index)]) - int(p.rest[size_t(s.index)]);
    // Triggers rest at 0 and go to 32767: past a third; sticks past half way.
    const int threshold = (s.index == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || s.index == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) ? 10000 : 16000;
    return s.kind == Kind::AxisPos ? d > threshold : s.kind == Kind::AxisNeg ? d < -threshold : std::abs(d) > threshold;
}

void Input::update(Pad& p, uint64_t ts, std::vector<InputEvent>& out) {
    const Bindings& b = bindingsFor(p.guid, p.profile);
    for (size_t c = 0; c < size_t(Control::Count); ++c) {
        bool on = false;
        for (const Source& s : b[c]) on = on || sourceActive(p, s);
        p.down[c] = on;
    }
    auto a = actionsOf(p.down, isGuitar(p.profile));
    // Built-in helpers: a pad's south / east buttons always confirm / go back
    // in the menus (a guitar's green / red already do).
    if (!inGame_) {
        a[size_t(Action::Cross)] = a[size_t(Action::Cross)] || p.buttons[SDL_GAMEPAD_BUTTON_SOUTH];
        a[size_t(Action::Circle)] = a[size_t(Action::Circle)] || p.buttons[SDL_GAMEPAD_BUTTON_EAST];
    }
    for (const auto& [id, pad] : pads_)
        if (&pad == &p) edges(padActions_[id], a, ts, out);
}

void Input::keyEvent(const SDL_KeyboardEvent& k, std::vector<InputEvent>& out) {
    const uint64_t ts = k.timestamp ? k.timestamp : SDL_GetTicksNS();
    if (k.down && !k.repeat) {
        if (detect_ >= 0 && k.key != SDLK_ESCAPE && detectFrom("keyboard")) return;
        if (capturing_ && captureId_ == "keyboard") {
            if (k.key == SDLK_ESCAPE) capturing_ = false;
            else if (k.key == SDLK_DELETE) clearBinding("keyboard", captureControl_, captureSlot_), capturing_ = false;
            else captureFrom("keyboard", key(k.key));
            return;
        }
        if (detect_ >= 0 && k.key == SDLK_ESCAPE) {
            detect_ = -1;
            return;
        }
    }
    if (k.repeat) return;
    if (k.down) keysDown_.insert(k.key);
    else keysDown_.erase(k.key);
    const Bindings& b = bindingsFor("keyboard", Profile::Keyboard);
    for (size_t c = 0; c < size_t(Control::Count); ++c) {
        bool on = false;
        for (const Source& s : b[c]) on = on || (s.kind == Kind::Key && keysDown_.count(SDL_Keycode(s.index)));
        keyControls_[c] = on;
    }
    auto a = actionsOf(keyControls_, false);
    // Built-in helpers (never unbound): the arrows, Enter (Cross), Esc / Backspace (Circle).
    auto held = [&](SDL_Keycode x) { return keysDown_.count(x) > 0; };
    a[size_t(Action::Up)] = a[size_t(Action::Up)] || held(SDLK_UP);
    a[size_t(Action::Down)] = a[size_t(Action::Down)] || held(SDLK_DOWN);
    a[size_t(Action::Left)] = a[size_t(Action::Left)] || held(SDLK_LEFT);
    a[size_t(Action::Right)] = a[size_t(Action::Right)] || held(SDLK_RIGHT);
    a[size_t(Action::Circle)] = a[size_t(Action::Circle)] || held(SDLK_ESCAPE) || held(SDLK_BACKSPACE);
    if (!inGame_) a[size_t(Action::Cross)] = a[size_t(Action::Cross)] || held(SDLK_KP_ENTER);
    edges(keyActions_, a, ts, out);
}

std::vector<Input::Device> Input::devices() const {
    std::vector<Device> out;
    out.push_back({"keyboard", "Keyboard", instrument("keyboard"), 0});
    for (const auto& [id, p] : pads_) {
        const std::string sid = "pad:" + std::to_string(id);
        auto pl = players_.find(p.guid);
        out.push_back({sid, p.name, instrument(sid), pl == players_.end() ? 0 : pl->second});
    }
    if (auto pl = players_.find("keyboard"); pl != players_.end()) out[0].player = pl->second;
    std::sort(out.begin() + 1, out.end(), [](const Device& a, const Device& b) { return a.id < b.id; });
    return out;
}

// One player: the device chosen for player 1, else the first pad, else the keyboard.
std::string Input::playerDevice(int player) const {
    if (player != 0) return "";
    const auto devs = devices();
    for (const Device& d : devs) {
        auto it = players_.find(guidOf(d.id));
        if (it != players_.end() && it->second == 0) return d.id;
    }
    for (const Device& d : devs)
        if (d.id != "keyboard" && d.player == 0) return d.id;
    return "keyboard";
}

void Input::setPlayer(const std::string& id, int player) {
    for (const Device& d : devices())
        if (d.id != id && player >= 0) {
            auto it = players_.find(guidOf(d.id));
            if (it != players_.end() && it->second == player) it->second = -1;
        }
    players_[guidOf(id)] = player;
    save();
}

bool Input::detectFrom(const std::string& id) {
    const int player = detect_;
    detect_ = -1;
    setPlayer(id, player);
    return true;
}

const Input::Bindings& Input::bindings(const std::string& id) const { return bindingsFor(guidOf(id), profileOf(id)); }

std::string Input::sourceName(const std::string& id, const Source& s) const {
    (void)id;
    static const char* const kAxes[] = {"LS X", "LS Y", "RS X", "RS Y", "LT", "RT"};
    switch (s.kind) {
    case Kind::Key: {
        const char* n = SDL_GetKeyName(SDL_Keycode(s.index));
        return n && *n ? n : "Key " + std::to_string(s.index);
    }
    case Kind::Button: {
        static const char* const kButtons[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "L stick", "R stick", "LB", "RB",
                                               "D-pad up", "D-pad dn", "D-pad lt", "D-pad rt", "Misc", "Paddle 1", "Paddle 2",
                                               "Paddle 3", "Paddle 4", "Touchpad"};
        if (s.index >= 0 && s.index < int(sizeof kButtons / sizeof *kButtons)) return kButtons[s.index];
        return "Button " + std::to_string(s.index);
    }
    default: {
        std::string n = s.index >= 0 && s.index < 6 ? kAxes[s.index] : "Axis " + std::to_string(s.index);
        if (s.index >= 4) return n;
        return n + (s.kind == Kind::AxisPos ? " +" : s.kind == Kind::AxisNeg ? " -" : "");
    }
    }
}

bool Input::controlDown(const std::string& id, Control c) const {
    if (id == "keyboard") return keyControls_[size_t(c)];
    const Pad* p = padById(id);
    return p && p->down[size_t(c)];
}

void Input::beginCapture(const std::string& id, Control c, size_t slot) {
    capturing_ = true, captureId_ = id, captureControl_ = c, captureSlot_ = std::min(slot, kSlots - 1);
}

bool Input::captureFrom(const std::string& id, const Source& s) {
    if (!capturing_ || id != captureId_) return false;
    Bindings b = bindings(id);
    auto& list = b[size_t(captureControl_)];
    list.erase(std::remove(list.begin(), list.end(), s), list.end());
    if (captureSlot_ < list.size()) list[captureSlot_] = s;
    else list.push_back(s);
    if (list.size() > kSlots) list.resize(kSlots);
    overrides_[guidOf(id)] = b;
    capturing_ = false;
    save();
    return true;
}

void Input::clearBinding(const std::string& id, Control c, size_t slot) {
    Bindings b = bindings(id);
    auto& list = b[size_t(c)];
    if (slot < list.size()) list.erase(list.begin() + std::ptrdiff_t(slot));
    overrides_[guidOf(id)] = b;
    save();
}

void Input::resetBindings(const std::string& id) {
    overrides_.erase(guidOf(id));
    save();
}

void Input::beginEdit(const std::string& id) {
    editId_ = id, editGuid_ = guidOf(id);
    auto it = overrides_.find(editGuid_);
    editHad_ = it != overrides_.end();
    if (editHad_) editBindings_ = it->second;
    saveHeld_ = true;
}

void Input::commitEdit() {
    saveHeld_ = false;
    editId_.clear();
    save();
}

void Input::discardEdit() {
    if (editHad_) overrides_[editGuid_] = editBindings_;
    else overrides_.erase(editGuid_);
    saveHeld_ = false;
    capturing_ = false;
    editId_.clear();
}

Input::Instrument Input::instrument(const std::string& id) const {
    return isGuitar(profileOf(id)) ? Instrument::Guitar : Instrument::Gamepad;
}

void Input::setInstrument(const std::string& id, Instrument i) {
    Pad* p = padById(id);
    if (!p) return;  // the keyboard plays the PSP layout
    instruments_[p->guid] = i;
    p->profile = i == Instrument::Gamepad ? Profile::Gamepad : isGuitar(p->detected) ? p->detected : Profile::XboxGuitar;
    save();
}

bool Input::exportTo(const std::string& file) const {
    std::ifstream in(savePath(), std::ios::binary);
    std::ofstream out(file, std::ios::binary);
    if (!in) save();
    std::ifstream again(savePath(), std::ios::binary);
    out << again.rdbuf();
    return bool(out);
}

bool Input::importFrom(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        Json::parse(ss.str());
    } catch (const std::exception&) {
        return false;
    }
    std::ofstream(savePath(), std::ios::binary) << ss.str();
    overrides_.clear(), instruments_.clear(), players_.clear();
    load();
    return true;
}

SDL_JoystickID Input::attachVirtualPad(const std::string& spec) {
    unsigned vendor = 0, product = 0;
    char kind[16] = "";
    if (std::sscanf(spec.c_str(), "%x:%x:%15s", &vendor, &product, kind) < 2) return 0;
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type = std::string(kind) == "guitar" ? SDL_JOYSTICK_TYPE_GUITAR : SDL_JOYSTICK_TYPE_GAMEPAD;
    d.vendor_id = uint16_t(vendor), d.product_id = uint16_t(product);
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.name = d.type == SDL_JOYSTICK_TYPE_GUITAR ? "Xbox 360 Guitar (virtual)" : "Xbox 360 Controller (virtual)";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
    if (id && d.type == SDL_JOYSTICK_TYPE_GUITAR) {
        // SDL maps a virtual gamepad by itself, not a virtual guitar: the
        // same layout (its buttons and axes in SDL's gamepad order).
        char guid[64];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof guid);
        const std::string m = std::string(guid) + "," + d.name +
                              ",a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,"
                              "rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,rightx:a2,"
                              "righty:a3,lefttrigger:a4,righttrigger:a5,";
        if (SDL_AddGamepadMapping(m.c_str()) < 0) SDL_Log("virtual guitar mapping: %s", SDL_GetError());
    }
    return id;
}
