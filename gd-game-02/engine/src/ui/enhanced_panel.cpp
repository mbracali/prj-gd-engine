// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The port's Enhanced Options: gd01's Enhanced Settings list
// (gd-game-01/engine/src/ui/select_panels.cpp, EnhancedPanel): its
// sections, rows, check boxes, dropdowns, explanation box and mouse, in
// GH2's menu font and colours (PortDraw) over a still shot of a venue
// (guidelines/universal_launcher.md).
#include "ui/enhanced_panel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "render/canvas.h"
#include "ui/port_draw.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"
#include "ui/ui_objects.h"

namespace ui {

const std::vector<EnhancedSetting>& enhancedSettings() {
    static const std::vector<EnhancedSetting> k = {
        {"mouse", {"on", "off"}},
        {"touch", {"on", "off"}},
        {"rockband", {"off", "on"}},
        {"xinput", {"on", "off"}},
        {"screen_mode", {"windowed", "fullscreen", "borderless"}},
        {"max_fps", {"120", "60", "240"}},
        {"fps_counter", {"off", "on"}},
        {"internal_res", {"1080", "0", "448", "480", "720", "800", "900", "1440", "2160"}},
        {"sharp_ui", {"on", "off"}},
        {"antialiasing", {"none", "fxaa", "ssaa", "taa"}},
        // FSR 3.1 and DLSS run through gd01's Vulkan upscaler, not in gd02 (GL): a saved one loads as none.
        {"ai_upscaling", {"none", "fsr1"}},
        {"ai_upscaling_mode", {"quality", "balanced", "performance"}},
        {"bokeh", {"stronger", "none", "regular"}},
        {"particles", {"on", "off"}},
        {"bloom", {"off", "low", "medium", "high"}},
        {"cinematic_color", {"off", "on"}},
        {"color_grade", {"off", "warm", "cold", "vintage", "noir", "neon"}},
        {"film_grain", {"off", "light", "strong"}},
        {"chromatic", {"off", "on"}},
        {"light_streaks", {"off", "on"}},
        {"ssao", {"off", "on"}},
        {"sharpening", {"off", "low", "high"}},
        {"star_blur", {"none", "light", "medium", "high"}},
        {"no_effects", {"off", "on"}},
        {"shadows_all", {"off", "on"}},
        {"more_lights", {"off", "subtle", "strong"}},
        {"light_dust", {"on", "off"}},
        {"ground_fog", {"none", "low", "full", "super"}},
        {"no_shaky_cam", {"off", "on"}},
        {"no_fail", {"off", "on"}},
        {"autoplay", {"off", "on"}},
        {"hopo_mode", {"original", "forgiving"}},
        {"audio_offset", {}, -300, 500},
        {"video_offset", {}, -300, 500},
    };
    return k;
}

const EnhancedSetting* enhancedSetting(const std::string& key) {
    for (const EnhancedSetting& e : enhancedSettings())
        if (key == e.key) return &e;
    return nullptr;
}

bool enhancedValid(const EnhancedSetting& s, const std::string& value) {
    if (s.values.empty()) {
        char* end = nullptr;
        const long ms = std::strtol(value.c_str(), &end, 10);
        return !value.empty() && end && *end == 0 && ms >= s.lo && ms <= s.hi;
    }
    for (const char* v : s.values)
        if (value == v) return true;
    return false;
}

std::vector<std::pair<std::string, std::string>> enhancedProfile(bool deck) {
    std::map<std::string, std::string> d = {
        // Every build.
        {"antialiasing", "none"}, {"ai_upscaling", "none"}, {"bokeh", "stronger"}, {"particles", "on"}, {"cinematic_color", "on"},
        {"color_grade", "off"}, {"chromatic", "on"}, {"light_streaks", "off"}, {"more_lights", "subtle"}, {"no_shaky_cam", "off"},
        // Desktop: Linux, Windows, macOS.
        {"screen_mode", "windowed"}, {"max_fps", "120"}, {"fps_counter", "on"}, {"internal_res", "1080"}, {"sharp_ui", "on"},
        {"bloom", "high"}, {"film_grain", "light"}, {"ssao", "on"}, {"sharpening", "low"}, {"star_blur", "high"},
        {"shadows_all", "on"}, {"light_dust", "on"}, {"ground_fog", "low"}, {"touch", "off"},
    };
    auto set = [&](std::initializer_list<std::pair<const char*, const char*>> kv) {
        for (const auto& [k, v] : kv) d[k] = v;
    };
    auto handheld = [&] {
        set({{"screen_mode", "fullscreen"}, {"max_fps", "60"}, {"fps_counter", "off"}, {"bloom", "medium"}, {"film_grain", "off"},
             {"ssao", "off"}, {"shadows_all", "off"}, {"light_dust", "off"}, {"ground_fog", "none"}});
    };
#if defined(__ANDROID__)
    (void)deck;
    handheld();
    set({{"internal_res", "900"}, {"sharp_ui", "off"}, {"sharpening", "low"}, {"star_blur", "light"}, {"touch", "on"}});
#else
#if defined(GD02_STEAM_DECK)
    deck = true;
#endif
#if defined(_WIN32) || defined(__APPLE__)
    d["internal_res"] = "1440";
#endif
    if (deck) {
        handheld();
        set({{"internal_res", "800"}, {"sharpening", "off"}, {"star_blur", "medium"}});
    }
#endif
    return {d.begin(), d.end()};
}

std::string enhancedDefault(const std::string& key, bool deck) {
    static const auto desktop = enhancedProfile(false), handheld = enhancedProfile(true);
    for (const auto& [k, v] : deck ? handheld : desktop)
        if (k == key) return v;
    const EnhancedSetting* s = enhancedSetting(key);
    if (!s) return {};
    return s->values.empty() ? "0" : s->values.front();
}

namespace {

enum class Kind { Section, Separator, Check, Choice, Button, Cheat };
struct Option {
    const char* value;
    const char* label;
    bool ok = true;  // false: not in gd02 (shown, cannot be chosen)
};
struct Item {
    Kind kind;
    const char* key;   // the setting (enhanced.json), the button's action, or the cheat (its locale token)
    const char* name;  // "" for a cheat: GH2's own name (its locale token)
    bool ready;        // false: not in gd02 yet (greyed, cannot be chosen)
    const char* help;
    std::vector<Option> options = {};
    const char* needs = nullptr;  // shown only while this setting is on (a check) or not none / off (a choice)
};

// gd01's list, in its sections and order, with its explanations (the
// console's names changed); gd02's own rows (Gamepads, Remove All Effects)
// where the guideline puts them. The cheats are GH2's
// (config/long_cheats.dta), with their button codes.
const std::vector<Item>& items() {
    static const std::vector<Item> k = {
        {Kind::Section, "deck", "STEAM DECK MODE", true, "Settings for handheld PCs such as the Steam Deck."},
        {Kind::Button, "steam_deck", "Apply Steam Deck Settings", true,
         "Sets every option to what runs best on a handheld PC such as the Steam Deck: full screen at 60 FPS, a 1280 x 800 "
         "internal resolution, and lighter effects (medium bloom, no ambient occlusion, film grain, fog or extra shadows). "
         "You can still change any option afterwards. The Steam Deck download starts with these settings."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "controls", "CONTROLS AND GAMEPAD", true, "Gamepads, mouse, touch and guitars."},
        {Kind::Button, "gamepads", "Gamepads", true,
         "The connected guitars, gamepads and the keyboard: which player each one is, its buttons lit as you press them, "
         "and new buttons for any of them."},
        {Kind::Check, "mouse", "Enable Mouse Selection", true,
         "Point at a menu entry to highlight it and click to choose it; a right click goes back and the mouse wheel "
         "scrolls lists. The help bar's items are clickable too. Hovering works only while the game window has focus; "
         "the pointer hides after a few seconds, when a key or a button is pressed, and during a song."},
        {Kind::Check, "touch", "Enable Touch Controls", true,
         "Play on a touch screen. In the menus a tap is a click. In a song, touch a lane at the bottom of the highway when "
         "its note arrives: it plays the fret and the strum at once (hold the finger for long notes, several fingers for "
         "chords). Two fingers above the highway deploy Star Power; the top corner pauses. On-screen Up / Down / Start "
         "buttons appear once you touch the screen. Set the buttons, swipes and pause corner in Gamepads > Touch Screen. "
         "Always on in the Android app."},
        {Kind::Check, "rockband", "Enable Rock Band Guitars", true,
         "Plays with the USB Rock Band guitars for Xbox 360 and PlayStation 3 / 4: the five frets (the solo frets too), "
         "the strum bar, the whammy bar, and tilt or Select for Star Power."},
        {Kind::Check, "xinput", "Enable XInput Controllers", true,
         "Any XInput gamepad plays like the original game's gamepad mode: the shoulder buttons and the face buttons are "
         "the frets, and pressing a button plays the note and the strum at once. Off, gamepads only work the menus."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "graphics", "PERFORMANCE AND GRAPHICS", true, "Timing, window, frame rate and image quality."},
        {Kind::Button, "calibrate", "Audio / Video Calibration", true,
         "Measures your setup's delay so notes are judged and drawn in time. First strum (or press green) on each tick "
         "you HEAR, then on each flash you SEE, 8 times each. Back cancels."},
        {Kind::Choice, "screen_mode", "Screen Mode", true,
         "Windowed; full screen at the desktop's resolution; or full screen without a border (a window covering the "
         "screen, the quickest to switch away from).",
         {{"windowed", "Windowed"}, {"fullscreen", "Full Screen"}, {"borderless", "Full Screen (No Border)"}}},
        {Kind::Choice, "max_fps", "In-Game Max FPS", true,
         "The highest frame rate the game draws at; the original ran at 60. The game never draws faster than this, "
         "whatever the display's refresh rate.",
         {{"60", "60"}, {"120", "120"}, {"240", "240"}}},
        {Kind::Check, "fps_counter", "Show FPS Counter", true,
         "Shows the frames drawn per second in the bottom-left corner of the screen."},
        {Kind::Choice, "internal_res", "Internal Resolution", true,
         "The resolution the game draws at before it is scaled to the window. Higher is sharper and costs more; above "
         "the window's own size it also smooths the edges. The Xbox 360 drew 1280 x 720 (x360), the PS2 640 x 448 (ps2). "
         "With AI Upscaling on, the window's resolution is used instead (or this one, when higher).",
         {{"0", "Window"}, {"448", "448p (ps2)"}, {"480", "480p"}, {"720", "720p (x360)"}, {"800", "800p (Steam Deck)"},
          {"900", "900p"}, {"1080", "1080p"}, {"1440", "1440p"}, {"2160", "4K (2160p)"}}},
        {Kind::Check, "sharp_ui", "Sharper Text and Sprites", true,
         "Upscales the menu art and the game's fonts with a sharper filter, so text and sprites stay crisp on large "
         "screens. Screens opened after the change use it."},
        {Kind::Choice, "antialiasing", "Anti-Aliasing", true,
         "Smooths jagged edges. FXAA: a fast filter over the image. SSAA: draws the game at twice the resolution and "
         "scales it down, the sharpest and the most costly. TAA: blends each frame with the last ones for very smooth "
         "edges, with a little softness in motion.",
         {{"none", "None"}, {"fxaa", "FXAA"}, {"ssaa", "SSAA"}, {"taa", "TAA"}}},
        {Kind::Choice, "ai_upscaling", "AI Upscaling", true,
         "Draws the 3D scene below your screen's resolution and upscales it to the screen (menus and the highway stay "
         "sharp), for higher frame rates. FSR 1: spatial, any GPU. FSR 3.1 and DLSS need the Vulkan upscaler, not in "
         "this version.",
         {{"none", "None"}, {"fsr1", "FSR 1 (Spatial)"}, {"fsr31", "FSR 3.1", false}, {"dlss4", "DLSS 4", false},
          {"dlss5", "DLSS 5", false}}},
        {Kind::Choice, "ai_upscaling_mode", "Upscaling Quality", true,
         "How far below the screen's resolution the scene is drawn: Quality (1.5x upscale, the sharpest), Balanced (1.7x) "
         "or Performance (2x, the fastest). On a 1440p screen: 960p, 847p, 720p.",
         {{"performance", "Performance"}, {"balanced", "Balanced"}, {"quality", "Quality"}}, "ai_upscaling"},
        {Kind::Choice, "bokeh", "Bokeh Effect", true,
         "The depth-of-field blur of the close shots. None: always sharp. Regular: as the original game. Stronger: the "
         "focus starts nearer the band and the blur is wider.",
         {{"none", "None"}, {"regular", "Regular"}, {"stronger", "Stronger"}}},
        {Kind::Check, "particles", "Enable Particles FX", true,
         "Sparks, smoke, fire, confetti and the highway's hit effects. Turn them off to save power on low-end hardware; "
         "the gameplay is the same."},
        {Kind::Choice, "bloom", "Bloom", true,
         "A soft glow around the brightest lights of the venue, like a camera lens, over the original game's own glow "
         "(which stays as on the Xbox 360).",
         {{"off", "Off"}, {"low", "Low"}, {"medium", "Medium"}, {"high", "High"}}},
        {Kind::Check, "cinematic_color", "Cinematic Color", true,
         "A filmic colour curve with a gentle grade and vignette on the venue, for a richer, more cinematic image."},
        {Kind::Choice, "color_grade", "Color Grading", true,
         "A colour look for the venue, like a film's grade (with or without Cinematic Color). Warm: golden stage light. "
         "Cold: steel blue. Vintage: faded 70s film. Noir: high-contrast black and white. Neon: punchy purples and cyans.",
         {{"off", "Off"}, {"warm", "Warm"}, {"cold", "Cold"}, {"vintage", "Vintage"}, {"noir", "Noir"}, {"neon", "Neon"}}},
        {Kind::Choice, "film_grain", "Film Grain", true,
         "Fine animated grain over the venue, like a concert shot on film. Light is barely there; Strong is clearly visible.",
         {{"off", "Off"}, {"light", "Light"}, {"strong", "Strong"}}},
        {Kind::Check, "chromatic", "Chromatic Aberration", true,
         "A camera lens's colour fringes: red and blue edges pull apart towards the corners of the venue."},
        {Kind::Check, "light_streaks", "Anamorphic Light Streaks", true,
         "The brightest stage lights flare into long blue-white horizontal streaks, like a cinema lens."},
        {Kind::Check, "ssao", "Ambient Occlusion", true,
         "Soft contact shadows where surfaces meet: under the band's feet, in the folds of clothes, in the corners of the "
         "stage, for more depth. Costs some performance."},
        {Kind::Choice, "sharpening", "Sharpening", true,
         "Contrast-adaptive sharpening of the venue (AMD CAS): crisper detail without halos, good with TAA or an upscaler.",
         {{"off", "Off"}, {"low", "Low"}, {"high", "High"}}},
        {Kind::Choice, "star_blur", "Blur on Star Power", true,
         "While Star Power is on, the venue and the band blur with the camera's motion and leave light trails; it fades "
         "out when Star Power ends. The highway always stays sharp.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"high", "High"}}},
        {Kind::Check, "no_effects", "Remove All Effects", true,
         "Turns every effect off at once, the original game's own too: its glow, depth of field, film noise and colour "
         "filters, every effect above and the camera's shake. The options above keep their values for when you turn "
         "this off."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "enhancers", "ENHANCERS", true, "Visual extras beyond the original game, and your user files."},
        {Kind::Check, "shadows_all", "All Band Mates Cast Shadows", true,
         "Every band mate gets a spotlight from above and casts a shadow on the stage floor."},
        {Kind::Choice, "more_lights", "More Lights on Venues", true,
         "Richer stage lighting: brighter light cones and floor pools, dust in the beams and a fill light on the band, "
         "following the venue's own light cues.",
         {{"off", "Off"}, {"subtle", "Subtle"}, {"strong", "Strong"}}},
        {Kind::Check, "light_dust", "Dust in Light Beams", true,
         "With More Lights on Venues: fine dust drifting and twinkling in the light beams.", {}, "more_lights"},
        {Kind::Choice, "ground_fog", "Add Fog to the Ground (Experimental)", true,
         "A low fog over the stage floor where the band plays, for more depth. Low is a thin haze; Full a thick "
         "stage-fog layer. Super Fog: a volumetric fog with light shafts from the stage lights and a haze in the air, "
         "glowing as you look into the lights (costs more on slower GPUs).",
         {{"none", "None"}, {"low", "Low"}, {"full", "Full"}, {"super", "Super Fog"}}},
        {Kind::Check, "no_shaky_cam", "Disable Shaky Camera", true,
         "The original game shakes the camera on some of its shots of the band, like a handheld camera. Turn this on for "
         "steady shots."},
        {Kind::Button, "export", "Export User Files", true,
         "Choose a folder to save two files in: GD02-user-settings.json with every Enhanced Option, and "
         "GD02-user-progress.json with all your bands and their progress. Use them as a backup or to move to another "
         "computer."},
        {Kind::Button, "load_settings", "Load User Settings", true,
         "Choose a GD02-user-settings.json file to load. The settings take effect at once; any setting in the file that "
         "is missing or not valid keeps its default."},
        {Kind::Button, "load_progress", "Load User Progress", true,
         "Choose a GD02-user-progress.json file to load. It replaces your bands and their progress and is saved at once; "
         "a file that is not a gd02 save is refused."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "cheats", "CHEATS", true, "The original game's cheats and more ways to bend the rules."},
        {Kind::Check, "cut_content", "Enable Cut Content", false, "Loads content left unused on the original disc into the game."},
        {Kind::Check, "no_fail", "No Fail Mode", true, "The rock meter cannot fail you: every song plays to the end."},
        {Kind::Check, "autoplay", "Autoplay", true, "The game plays every note of a song by itself, without a miss."},
        {Kind::Choice, "hopo_mode", "Forgiving Hammer-ons and Pull-offs", true,
         "Notes with a coloured top can be played without strumming. Off (as on the Xbox 360): a fret pressed or let go "
         "within the note's window after the previous note was hit, and a wrong hammer-on locks the note out. On "
         "(forgiving): a wrong fret can be corrected, and a note whose fret is already held when it arrives plays by "
         "itself.",
         {{"original", "Original"}, {"forgiving", "Forgiving"}}},
        {Kind::Cheat, "cheat_unlock_all", "", true,
         "The original cheat: every song, venue, character and guitar is open. Saving and the career are off while it "
         "is on. Code at the main menu: Blue, Yellow, Orange, Red, Yellow, Orange, Blue, Yellow, Blue, Yellow, Blue, "
         "Yellow, Blue, Yellow, Blue, Yellow."},
        {Kind::Cheat, "cheat_air_guitar", "", true,
         "The original cheat: the guitarists play without a guitar. Code at the main menu: Yellow, Blue, Yellow, "
         "Orange, Yellow, Blue."},
        {Kind::Cheat, "cheat_performance_mode", "", true,
         "The original cheat: no highway and no meters, only the band and the venue. Code at the main menu: Blue, "
         "Blue, Yellow, Blue, Blue, Orange, Blue, Blue."},
        {Kind::Cheat, "cheat_hyper_speed_mode", "", true,
         "The original cheat: the notes come down the highway one and a half times as fast. Code at the main menu: "
         "Blue, Orange, Yellow, Orange, Blue, Orange, Yellow, Yellow."},
        {Kind::Cheat, "cheat_flaming_head", "", false,
         "The original cheat: the guitarist's head is on fire (the band's flames do not burn yet in this version). "
         "Code at the main menu: Yellow, Orange, Yellow, Yellow, Orange, Yellow, Yellow, Orange."},
        {Kind::Cheat, "cheat_eyeball_head_crowd", "", true,
         "The original cheat: everyone in the crowd has a giant eyeball for a head. Code at the main menu: Yellow, "
         "Orange, Blue, Blue, Blue, Orange, Yellow."},
        {Kind::Cheat, "cheat_monkey_head_crowd", "", true,
         "The original cheat: everyone in the crowd wears a monkey head. Code at the main menu: Orange, Yellow, "
         "Blue, Blue, Yellow, Orange, Blue, Blue."},
    };
    return k;
}

class EnhancedPanelObj : public PanelObj {
public:
    EnhancedPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "EnhancedPanel"), d_(ui) {}

    std::string fileName() override { return ""; }
    bool drawsSomething() const override { return true; }

    void enter() override {
        rebuild();
        // The first setting gd02 has (Gamepads), as the screen opens.
        if (!entered_) {
            for (size_t r = 0; r < rows_.size(); ++r)
                if (item(int(r)).ready && item(int(r)).kind != Kind::Section && item(int(r)).kind != Kind::Separator) {
                    focus_ = int(r);
                    break;
                }
            entered_ = true;
        }
        dropdown_ = -1;
        cal_ = Calib{};
        scroll_ = scrollTarget_ = 0;
        keepVisible();
        scroll_ = scrollTarget_;
        ui.host().menuBackdrop(true);
        PanelObj::enter();
    }
    void exit() override {
        ui.host().menuBackdrop(false);
        PanelObj::exit();
    }

    bool buttonDown(int button, int player) override {
        (void)player;
        if (rows_.empty()) return false;
        if (cal_.phase != 0) return calibButton(button), true;
        const Item& it = item(focus_);
        if (dropdown_ >= 0) {  // the open dropdown takes the input
            const int n = int(it.options.size());
            if (button == kPad_DUp || button == kPad_DDown) {
                ddFocus_ = (ddFocus_ + (button == kPad_DDown ? 1 : n - 1)) % n;
                ui.playSfx("button_toggle");
            } else if (button == kPad_X) {
                if (!it.options[size_t(ddFocus_)].ok) return ui.playSfx("button_error"), true;
                dropdown_ = -1;
                setValue(it, it.options[size_t(ddFocus_)].value);
            } else if (button == kPad_Tri) {
                dropdown_ = -1;
                ui.playSfx("button_back");
            }
            return true;
        }
        if (button == kPad_DUp || button == kPad_DDown) {
            moveFocus(button == kPad_DDown ? 1 : -1);
            return true;
        }
        if (button == kPad_DLeft || button == kPad_DRight) {
            const int dir = button == kPad_DRight ? 1 : -1;
            if (it.kind == Kind::Choice) {
                if (!editable(it)) return ui.playSfx("button_error"), true;
                const int n = int(it.options.size());
                int i = optionIndex(it);
                for (int k = 0; k < n; ++k) {  // the next option gd02 has
                    i = (i + dir + n) % n;
                    if (it.options[size_t(i)].ok) break;
                }
                setValue(it, it.options[size_t(i)].value);
            } else if (it.kind == Kind::Check || it.kind == Kind::Cheat) {
                if (isOn(it) != (dir > 0)) activate();
            }
            return true;
        }
        if (button == kPad_X) return activate(), true;
        return false;  // back: the screen's auto back
    }

    bool mouse(float x, float y, bool click) override {
        if (rows_.empty()) return false;
        if (cal_.phase != 0) {  // a click is a press
            if (click) calibButton(kPad_X);
            return true;
        }
        x -= d_.offset();
        // The open dropdown: its options, or a click anywhere else closes it.
        if (dropdown_ >= 0) {
            const Item& it = item(focus_);
            for (size_t k = 0; k < it.options.size(); ++k) {
                const float oy = ddTop_ + float(k) * kDdRow;
                if (x < kDdPopX0 || x > kCtrlX1 || y < oy || y > oy + kDdRow) continue;
                if (int(k) != ddFocus_) ddFocus_ = int(k), ui.playSfx("button_toggle");
                if (click && !it.options[k].ok) ui.playSfx("button_error");
                else if (click) dropdown_ = -1, setValue(it, it.options[k].value);
                return true;
            }
            if (click) dropdown_ = -1, ui.playSfx("button_back");
            return true;
        }
        for (size_t r = 0; r < rows_.size(); ++r) {
            const float top = rowY(int(r)), h = rowHeight(item(int(r)));
            if (item(int(r)).kind == Kind::Separator || top < kListTop - 1 || top + h > kListBottom + 1) continue;
            if (x < kListX0 - 6 || x > kCtrlX1 + 12 || y < top || y > top + h) continue;
            // Hovering focuses (without scrolling the list) and shows the explanation.
            if (int(r) != focus_) focus_ = int(r), ui.playSfx("button_toggle");
            if (click) activate();
            return true;
        }
        return false;
    }

    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        // The list eases toward its scroll target.
        const float k = 1 - std::exp(-std::max(float(dtMs), 0.0f) / 70.0f);
        scroll_ += (scrollTarget_ - scroll_) * k;
        if (std::fabs(scrollTarget_ - scroll_) < 0.3f) scroll_ = scrollTarget_;
        // The user files' (or Steam Deck's) result shows for a few seconds.
        const std::string st = ui.host().enhancedStatus();
        if (st != status_) status_ = st, statusMs_ = st.empty() ? 0 : 5000;
        statusMs_ = std::max(0.0f, statusMs_ - float(dtMs));
        calibPoll();
    }

    void draw() override {
        if (ui.headless()) return;
        d_.updateTransition();
        // The still venue behind (App::menuBackdrop), else a black page.
        const bool still = ui.host().drawMenuBackdrop();
        for (int pass : {0, 1}) {
            d_.begin(pass);
            if (pass == 0) {
                if (!still) d_.quad(0, 0, kScreenW, kScreenH, {0, 0, 0, 1});
                else d_.quad(0, 0, kScreenW, kScreenH, {0, 0, 0, 0.25f});  // the venue a little darker, as gd01's blurred one reads
            }
            drawAll();
        }
        // The open dropdown over everything (its own quads, then its texts).
        if (dropdown_ >= 0)
            for (int pass : {0, 1}) d_.begin(pass), drawDropdown();
        if (cal_.phase != 0)
            for (int pass : {0, 1}) d_.begin(pass), drawCalib();
    }

private:
    // gd01's layout (menu units): the options box on the left, the
    // explanation bottom right (bottom-aligned with the box).
    static constexpr float kListTop = 168, kListBottom = 612, kListX0 = 80, kNameX = 118, kCtrlX1 = 612;
    static constexpr float kBoxX0 = 56, kBoxY0 = 22, kBoxX1 = 640, kBoxY1 = 640;
    static constexpr float kDdX0 = 360, kDdPopX0 = 300, kDdRow = 30, kPopX0 = 700, kPopX1 = 1224;
    using Color = PortDraw::Color;
    static constexpr Color kTitle = PortDraw::kTitle, kFocused = PortDraw::kFocused, kNormal = PortDraw::kNormal,
                           kDim = PortDraw::kDim, kAccent = PortDraw::kAccent, kGold = PortDraw::kGold, kWarn = PortDraw::kWarn;

    // -- the rows shown (sections collapse; separators stay)
    void rebuild() {
        const int keep = rows_.empty() ? -1 : rows_[size_t(std::clamp(focus_, 0, int(rows_.size()) - 1))];
        rows_.clear();
        bool hidden = false;
        for (size_t i = 0; i < items().size(); ++i) {
            const Item& it = items()[i];
            if (it.kind == Kind::Section) hidden = collapsed(it.key);
            if (it.kind == Kind::Separator) hidden = false;
            if (it.kind != Kind::Section && it.kind != Kind::Separator && hidden) continue;
            if (it.needs && !settingOn(it.needs)) continue;
            rows_.push_back(int(i));
        }
        focus_ = 0;
        for (size_t r = 0; r < rows_.size(); ++r)
            if (rows_[r] == keep) focus_ = int(r);
        scrollTarget_ = std::clamp(scrollTarget_, 0.0f, maxScroll());
    }
    const Item& item(int row) const { return items()[size_t(rows_[size_t(row)])]; }
    bool collapsed(const std::string& section) const {
        auto it = collapsed_.find(section);
        return it == collapsed_.end() ? section == "deck" : it->second;  // Steam Deck starts collapsed (gd01)
    }
    static float rowHeight(const Item& it) { return it.kind == Kind::Section ? 44 : it.kind == Kind::Separator ? 18 : 34; }
    float rowY(int row) const {
        float y = kListTop - scroll_;
        for (int r = 0; r < row; ++r) y += rowHeight(item(r));
        return y;
    }
    float contentHeight() const {
        float h = 0;
        for (size_t r = 0; r < rows_.size(); ++r) h += rowHeight(item(int(r)));
        return h;
    }
    float maxScroll() const { return std::max(0.0f, contentHeight() - (kListBottom - kListTop)); }
    // Keep the focus in view, with the section title above a first row.
    void keepVisible() {
        const float top = rowY(focus_) + scroll_ - kListTop, h = rowHeight(item(focus_));
        if (top < scrollTarget_ + 4) scrollTarget_ = top - (focus_ > 0 ? 8 : 0);
        if (top + h > scrollTarget_ + (kListBottom - kListTop) - 4) scrollTarget_ = top + h - (kListBottom - kListTop) + 8;
        scrollTarget_ = std::clamp(scrollTarget_, 0.0f, maxScroll());
    }
    void moveFocus(int dir) {
        const int n = int(rows_.size());
        for (int k = 0; k < n; ++k) {
            focus_ = (focus_ + dir + n) % n;
            if (item(focus_).kind != Kind::Separator) break;
        }
        keepVisible();
        ui.playSfx("button_toggle");
    }

    // -- values
    std::string name(const Item& it) const { return it.kind == Kind::Cheat ? ui.localize(it.key) : it.name; }
    bool settingOn(const char* key) const {
        const std::string v = ui.host().enhancedValue(key);
        return !v.empty() && v != "none" && v != "off";
    }
    bool editable(const Item& it) const {
#ifdef __ANDROID__
        if (std::string(it.key) == "touch") return false;  // always on: a phone may have no other control (gd01)
#endif
        return it.ready;
    }
    bool isOn(const Item& it) const {
        if (it.kind == Kind::Cheat) return ui.cheatOn(it.key);
        return it.ready && ui.host().enhancedValue(it.key) == "on";
    }
    // A choice of exactly two values is shown and used as a check box (gd01).
    static bool isToggle(const Item& it) { return it.kind == Kind::Choice && it.options.size() == 2; }
    int optionIndex(const Item& it) const {
        const std::string v = it.ready ? ui.host().enhancedValue(it.key) : std::string();
        for (size_t k = 0; k < it.options.size(); ++k)
            if (v == it.options[k].value) return int(k);
        return 0;
    }
    void setValue(const Item& it, const std::string& v) {
        if (!editable(it)) return ui.playSfx("button_error");
        for (const Option& o : it.options)
            if (v == o.value && !o.ok) return ui.playSfx("button_error");
        ui.host().setEnhancedValue(it.key, v);
        rebuild();  // a choice may show or hide its dependent row
        ui.playSfx("button_select");
    }
    void activate() {
        const Item& it = item(focus_);
        switch (it.kind) {
        case Kind::Section:
            collapsed_[it.key] = !collapsed(it.key);
            rebuild();
            ui.playSfx("button_select");
            return;
        case Kind::Separator: return;
        case Kind::Check:
            if (!editable(it)) return ui.playSfx("button_error");
            ui.host().setEnhancedValue(it.key, isOn(it) ? "off" : "on");
            rebuild();  // a check may show or hide its dependent row
            ui.playSfx("button_select");
            return;
        case Kind::Cheat:
            if (!it.ready) return ui.playSfx("button_error");
            ui.setCheat(it.key, !isOn(it));
            return;
        case Kind::Choice:
            if (!editable(it)) return ui.playSfx("button_error");
            if (isToggle(it)) return setValue(it, it.options[optionIndex(it) == 1 ? 0 : 1].value);
            dropdown_ = focus_;
            ddFocus_ = optionIndex(it);
            ui.playSfx("button_select");
            return;
        case Kind::Button:
            if (!it.ready) return ui.playSfx("button_error");
            ui.playSfx("button_select");
            if (std::string(it.key) == "gamepads") return ui.gotoScreen("gamepads_screen");
            if (std::string(it.key) == "calibrate") return calibStart();
            ui.host().enhancedAction(it.key);  // Apply Steam Deck Settings, Export / Load User Files
            rebuild();
            return;
        }
    }

    // -- drawing
    void drawAll() {
        const float o = d_.offset();
        d_.quad(kBoxX0 + o, kBoxY0, kBoxX1 + o, kBoxY1, PortDraw::kBox);
        d_.frame(kBoxX0 + o, kBoxY0, kBoxX1 + o, kBoxY1, 1.5f, PortDraw::kEdge);
        d_.quad(kBoxX0 + o, kBoxY0, kBoxX1 + o, kBoxY0 + 4, kTitle);
        d_.text("ENHANCED OPTIONS", 90 + o, 44, 62, kTitle, 0x11);
        d_.text("Features of this PC version, beyond the original game", 92 + o, 112, 20, kDim, 0x11);
        // The user files' (or Steam Deck's) result for a few seconds.
        if (statusMs_ > 0) {
            Color c = PortDraw::kOn;
            c[3] = std::min(1.0f, statusMs_ / 500);
            d_.text(d_.fit(status_, 18, kBoxX1 - kBoxX0 - 60), 92 + o, 138, 18, c, 0x11);
        }
        for (size_t r = 0; r < rows_.size(); ++r) drawRow(int(r));
        // More above / below.
        if (scroll_ > 1) d_.arrow(kListX0 + 270 + o, kListTop - 10, true, kDim);
        if (contentHeight() - scroll_ > kListBottom - kListTop + 1) d_.arrow(kListX0 + 270 + o, kListBottom + 8, false, kDim);
        drawHelp();
    }

    void drawRow(int r) {
        const Item& it = item(r);
        const float h = rowHeight(it), top = rowY(r), cy = top + h * 0.5f, o = d_.offset();
        if (top < kListTop - 1 || top + h > kListBottom + 1) return;
        const bool f = r == focus_;
        if (it.kind == Kind::Separator) {
            d_.quad(kListX0 + o, cy - 1, kCtrlX1 + 8 + o, cy + 1, {0.3f, 0.3f, 0.34f, 1});
            return;
        }
        if (f) {
            d_.quad(kListX0 - 6 + o, top + 2, kCtrlX1 + 12 + o, top + h - 2, PortDraw::kFocusBar);
            d_.quad(kListX0 - 6 + o, top + 2, kListX0 - 2 + o, top + h - 2, it.ready ? kGold : kDim);
        }
        if (it.kind == Kind::Section) {
            const float ax = kListX0 + 8 + o;
            const Color c = f ? kFocused : kAccent;
            if (collapsed(it.key)) d_.triangle(ax - 4, cy - 8, ax - 4, cy + 8, ax + 8, cy, c);
            else d_.triangle(ax - 7, cy - 5, ax + 7, cy - 5, ax, cy + 7, c);
            d_.text(it.name, kListX0 + 26 + o, cy + 1, 27, c, 0x21);
            return;
        }
        const bool locked = it.kind == Kind::Button || it.kind == Kind::Cheat ? !it.ready : !editable(it);
        const Color nameC = f ? kFocused : locked ? kDim : kNormal;
        const std::string n = name(it);
        // A long name next to a dropdown is drawn smaller to fit before it.
        float nameH = 23;
        const float room = (it.kind == Kind::Choice && !isToggle(it) ? kDdX0 : kCtrlX1 - (it.kind == Kind::Button ? 130 : 40)) - kNameX - 14;
        if (const float w = d_.width(n, 23); w > room - (locked ? 30 : 0)) nameH = std::max(15.0f, 23 * (room - (locked ? 30 : 0)) / w);
        d_.text(n, kNameX + o, cy + 1, nameH, nameC, 0x21);
        if (!it.ready) d_.warning(kNameX + o + d_.width(n, nameH) + 10, cy);
        const Color ctrl = locked ? kDim : f ? kFocused : kNormal;
        const Kind shown = isToggle(it) ? Kind::Check : it.kind == Kind::Cheat ? Kind::Check : it.kind;
        switch (shown) {
        case Kind::Check:
            d_.checkBox(kCtrlX1 + o, cy, isToggle(it) ? optionIndex(it) == 1 : isOn(it), ctrl, locked ? kDim : kGold);
            break;
        case Kind::Choice: {
            const float x1 = kCtrlX1 + o, x0 = kDdX0 + o;
            d_.frame(x0, cy - 13, x1, cy + 13, 2, locked ? kDim : Color{0.5f, 0.5f, 0.55f, 1});
            const std::string label = it.options.empty() ? "" : it.options[size_t(optionIndex(it))].label;
            d_.text(d_.fit(label, 20, x1 - x0 - 40), x0 + 10, cy + 1, 20, ctrl, 0x21);
            d_.triangle(x1 - 22, cy - 4, x1 - 8, cy - 4, x1 - 15, cy + 5, ctrl);
            break;
        }
        case Kind::Button: {
            const float x1 = kCtrlX1 + o, x0 = x1 - 120;
            d_.frame(x0, cy - 13, x1, cy + 13, 2, ctrl);
            d_.text(buttonLabel(it.key), (x0 + x1) * 0.5f, cy + 1, 20, ctrl, 0x22);
            break;
        }
        default: break;
        }
    }

    // A button's label (gd01's).
    static const char* buttonLabel(const std::string& key) {
        if (key == "gamepads") return "Open...";
        if (key == "calibrate") return "Calibrate...";
        if (key == "export") return "Export...";
        if (key == "load_settings" || key == "load_progress") return "Load...";
        return "Apply";
    }

    void drawDropdown() {
        const Item& it = item(dropdown_);
        const float rowTop = rowY(dropdown_), h = rowHeight(it), n = float(it.options.size());
        ddTop_ = rowTop + h - 2;
        if (ddTop_ + n * kDdRow > kScreenH - 70) ddTop_ = rowTop + 2 - n * kDdRow;  // no room below: open upward
        const float o = d_.offset(), x0 = kDdPopX0 + o, x1 = kCtrlX1 + o;
        d_.quad(x0, ddTop_, x1, ddTop_ + n * kDdRow, {0.08f, 0.07f, 0.06f, 0.97f});
        d_.frame(x0, ddTop_, x1, ddTop_ + n * kDdRow, 2, kAccent);
        const int cur = optionIndex(it);
        for (size_t k = 0; k < it.options.size(); ++k) {
            const float oy = ddTop_ + float(k) * kDdRow;
            const bool hot = int(k) == ddFocus_;
            if (hot) d_.quad(x0 + 2, oy + 2, x1 - 2, oy + kDdRow - 2, PortDraw::kFocusBar);
            const bool ok = it.options[k].ok;
            d_.text(ok ? std::string(it.options[k].label) : std::string(it.options[k].label) + " (not available yet)", x0 + 12,
                    oy + kDdRow * 0.5f + 1, 20, !ok ? kDim : hot ? kFocused : kNormal, 0x21);
            if (int(k) == cur) d_.line(x1 - 26, oy + 15, x1 - 21, oy + 20, 3, kGold), d_.line(x1 - 21, oy + 20, x1 - 12, oy + 9, 3, kGold);
        }
    }

    // The focused option's explanation, in its box on the right.
    void drawHelp() {
        const Item& it = item(focus_);
        if (it.kind == Kind::Separator) return;
        const float o = d_.offset(), x0 = kPopX0 + o, x1 = kPopX1 + o, pad = 24, bodyH = 22, lineH = 30;
        const std::vector<std::string> lines = d_.wrap(it.help, x1 - x0 - 2 * pad, bodyH);
        const bool notYet = !it.ready && it.kind != Kind::Section;
        const bool locked = it.kind == Kind::Check && it.ready && !editable(it);
        const bool calib = std::string(it.key) == "calibrate";  // its status line
        const float height = pad + 46 + float(lines.size()) * lineH + (notYet || locked || calib ? 42 : 0) + pad;
        const float top = kBoxY1 - height;  // bottom-aligned with the options box
        float y = top + pad;
        d_.quad(x0, top, x1, kBoxY1, {0, 0, 0, 0.72f});
        d_.frame(x0, top, x1, kBoxY1, 1.5f, PortDraw::kEdge);
        d_.quad(x0, top, x1, top + 5, kAccent);
        const std::string n = name(it);
        float th = 30;
        if (const float w = d_.width(n, th), room = x1 - x0 - 2 * pad; w > room) th = std::max(18.0f, th * room / w);
        d_.text(n, x0 + pad, y + 2 + (30 - th) * 0.5f, th, kGold, 0x11);
        y += 46;
        for (const std::string& l : lines) d_.text(l, x0 + pad, y, bodyH, kNormal, 0x11), y += lineH;
        if (calib) {
            y += 6;
            d_.text("Now: audio " + signedMs(ui.host().enhancedValue("audio_offset")) + ", video " +
                        signedMs(ui.host().enhancedValue("video_offset")),
                    x0 + pad, y + 13, 21, PortDraw::kOn, 0x21);
        } else if (notYet) {
            y += 6;
            d_.warning(x0 + pad, y + 12);
            d_.text("Not available yet", x0 + pad + 30, y + 13, 21, kWarn, 0x21);
        } else if (locked) {
            y += 6;
            d_.text("Always on in the Android app", x0 + pad, y + 13, 21, kAccent, 0x21);
        }
    }

    // -- Audio / Video Calibration (gd01's; the original had none). A beat
    // every 750 ms: the audio step plays a tick, the video step flashes a
    // ring silently. After 3 lead-in beats, each press is matched to the
    // nearest beat; the median of 8 is the step's offset. audio_offset
    // (heard + input delay) moves the judging, video_offset (seen + input)
    // draws the highway ahead (app.cpp).
    static constexpr uint64_t kBeatNs = 750000000ull;
    static constexpr int kLeadIn = 3, kSamples = 8;
    struct Calib {
        int phase = 0;  // 0 off, 1 audio, 2 video, 3 done
        uint64_t nextTick = 0, flashNs = 0;
        std::vector<uint64_t> ticks;
        std::vector<float> samples;
        float audio = 0, video = 0;
    };
    static std::string signedMs(const std::string& v) {
        const int ms = std::atoi(v.c_str());
        return (ms > 0 ? "+" : "") + std::to_string(ms) + " ms";
    }
    void calibStart() {
        cal_ = Calib{};
        cal_.phase = 1;
        cal_.nextTick = SDL_GetTicksNS() + 1000000000ull;
    }
    void calibPoll() {
        if (cal_.phase != 1 && cal_.phase != 2) return;
        const uint64_t now = SDL_GetTicksNS();
        if (now < cal_.nextTick) return;
        if (cal_.phase == 1) ui.playSfx("button_toggle");
        else cal_.flashNs = now;
        cal_.ticks.push_back(now);
        cal_.nextTick += kBeatNs;
        if (cal_.nextTick < now) cal_.nextTick = now + kBeatNs;  // a stall: no burst of ticks
    }
    void calibButton(int button) {
        if (button == kPad_Tri) {  // cancel (or close the result)
            cal_.phase = 0;
            ui.playSfx("button_back");
            return;
        }
        if (cal_.phase == 3) {
            if (button == kPad_X) cal_.phase = 0, ui.playSfx("button_select");
            return;
        }
        if (button != kPad_X && button != kPad_DUp && button != kPad_DDown) return;  // green, Enter or the strum bar
        if (int(cal_.ticks.size()) < kLeadIn) return;
        const uint64_t now = SDL_GetTicksNS();
        float best = 1e9f;
        for (uint64_t t : cal_.ticks)
            if (std::fabs(float(int64_t(now - t)) / 1e6f) < std::fabs(best)) best = float(int64_t(now - t)) / 1e6f;
        if (const float d = float(int64_t(now - cal_.nextTick)) / 1e6f; std::fabs(d) < std::fabs(best)) best = d;  // early for the next
        if (std::fabs(best) > float(kBeatNs) / 1e6f * 0.45f) return;
        cal_.samples.push_back(best);
        if (int(cal_.samples.size()) < kSamples) return;
        std::vector<float> s = cal_.samples;
        std::nth_element(s.begin(), s.begin() + long(s.size() / 2), s.end());
        const float median = std::clamp(s[s.size() / 2], -300.0f, 500.0f);
        if (cal_.phase == 1) {
            cal_.audio = median;
            cal_.phase = 2;
            cal_.ticks.clear(), cal_.samples.clear();
            cal_.nextTick = now + 1500000000ull;
        } else {
            cal_.video = median;
            cal_.phase = 3;
            ui.host().setEnhancedValue("audio_offset", std::to_string(std::lround(cal_.audio)));
            ui.host().setEnhancedValue("video_offset", std::to_string(std::lround(cal_.video)));
        }
    }
    void drawCalib() {
        const float o = d_.offset(), cx = kScreenW * 0.5f + o, cy = 360;
        d_.quad(o, 0, kScreenW + o, kScreenH, {0, 0, 0, 0.88f});
        d_.text("CALIBRATION", cx, 110, 56, kTitle, 0x22);
        if (cal_.phase == 3) {
            d_.text("Audio " + signedMs(std::to_string(std::lround(cal_.audio))) + "   Video " + signedMs(std::to_string(std::lround(cal_.video))), cx,
                    cy, 34, PortDraw::kOn, 0x22);
            d_.text("Saved. Press green to close.", cx, cy + 60, 22, kNormal, 0x22);
            return;
        }
        const bool audio = cal_.phase == 1;
        d_.text(audio ? "Step 1 of 2: AUDIO" : "Step 2 of 2: VIDEO", cx, 200, 30, kAccent, 0x22);
        d_.text(audio ? "Strum or press green on each tick you HEAR" : "Strum or press green on each flash you SEE", cx, 245, 24, kNormal, 0x22);
        if (!audio) {  // the ring, lit for 120 ms on each beat
            const float lit = cal_.flashNs ? std::max(0.0f, 1 - float(SDL_GetTicksNS() - cal_.flashNs) / 120e6f) : 0;
            for (int i = 0; i < 48; ++i) {
                const float a0 = float(i) / 48 * 6.2831853f, a1 = float(i + 1) / 48 * 6.2831853f;
                d_.line(cx + 70 * std::cos(a0), cy + 70 * std::sin(a0), cx + 70 * std::cos(a1), cy + 70 * std::sin(a1), 6,
                        {0.4f + 0.6f * lit, 0.4f + 0.46f * lit, 0.45f - 0.25f * lit, 1});
                if (lit > 0)
                    d_.triangle(cx, cy, cx + 62 * std::cos(a0), cy + 62 * std::sin(a0), cx + 62 * std::cos(a1), cy + 62 * std::sin(a1),
                                {1, 0.86f, 0.2f, lit});
            }
        }
        // Progress: one dot per press taken.
        for (int k = 0; k < kSamples; ++k) {
            const float x = cx + (float(k) - (kSamples - 1) * 0.5f) * 34;
            d_.quad(x - 9, cy + 130, x + 9, cy + 148, k < int(cal_.samples.size()) ? PortDraw::kOn : Color{0.3f, 0.3f, 0.34f, 1});
        }
        if (int(cal_.ticks.size()) < kLeadIn) d_.text("Get ready...", cx, cy + 190, 22, kDim, 0x22);
        d_.text("Back cancels", cx, cy + 230, 18, kDim, 0x22);
    }

    PortDraw d_;
    Calib cal_;
    std::string status_;
    float statusMs_ = 0;
    std::vector<int> rows_;  // indexes into items() of the rows shown
    std::map<std::string, bool> collapsed_;
    int focus_ = 0, dropdown_ = -1, ddFocus_ = 0;
    bool entered_ = false;
    float scroll_ = 0, scrollTarget_ = 0, ddTop_ = 0;
};

}  // namespace

std::unique_ptr<ScriptObject> makeEnhancedPanel(UiManager& ui, const std::string& name) { return std::make_unique<EnhancedPanelObj>(ui, name); }

}  // namespace ui
