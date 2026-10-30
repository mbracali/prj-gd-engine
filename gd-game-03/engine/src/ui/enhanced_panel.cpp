// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The port's Enhanced Options: the universal Enhanced menu
// (universal_mechanisms.md 1.2.2) as gd02 0.1.8 has it (gd-game-02/engine/
// src/ui/enhanced_panel.cpp, itself gd01's EnhancedPanel): its sections,
// rows, check boxes, dropdowns, scrollbar, Back / Export / Import,
// explanation box and mouse, in the game's menu font and colours (PortDraw)
// over a still shot of a venue (guidelines/universal_launcher.md).
#include "ui/enhanced_panel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
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
    // Every one starts at the original game's look (GD03-019): windowed, the
    // PS2's 60 frames a second, no counter, the window's own resolution and
    // the PS2's 4:3 picture (GD03-016). The mouse is a control, not a look:
    // it starts on, as in every gd-game (universal_mechanisms.md 1.2.2).
    static const std::vector<EnhancedSetting> k = {
        {"mouse", {"on", "off"}},
        {"screen_mode", {"windowed", "fullscreen", "borderless"}},
        {"internal_res", {"0", "448", "480", "720", "800", "900", "1080", "1440", "2160"}},
        {"widescreen", {"off", "on"}},
        {"max_fps", {"60", "120", "240", "unlimited"}},
        {"fps_counter", {"off", "on"}},
    };
    return k;
}

const EnhancedSetting* enhancedSetting(const std::string& key) {
    for (const EnhancedSetting& e : enhancedSettings())
        if (key == e.key) return &e;
    return nullptr;
}

bool enhancedValid(const EnhancedSetting& s, const std::string& value) {
    for (const char* v : s.values)
        if (value == v) return true;
    return false;
}

std::vector<std::pair<std::string, std::string>> enhancedPreset(const std::string& mode) {
    // gd02's presets (gd01's), less the effects gd03 does not have yet: every
    // mode leaves them off, the original look. The window, the picture's
    // aspect and the mouse stay unless a mode names them.
    if (mode == "steam_deck") return {{"screen_mode", "fullscreen"}, {"max_fps", "60"}, {"internal_res", "800"}, {"fps_counter", "off"}};
    if (mode == "original") return {{"max_fps", "60"}};  // effects off + 60 FPS (owner 10-30)
    if (mode == "clean") return {{"internal_res", "1080"}, {"max_fps", "120"}};
    if (mode == "max_hd") return {{"internal_res", "1080"}, {"max_fps", "240"}};
    if (mode == "max_4k") return {{"internal_res", "2160"}, {"max_fps", "240"}};
    if (mode == "budget") return {{"internal_res", "720"}, {"max_fps", "60"}};
    if (mode == "good_old_days") return {{"internal_res", "448"}, {"max_fps", "60"}};  // the PS2's resolution
    return {};
}

void migrateEnhanced(Json& e) {
    if (e.type != Json::Type::Object) return;
    // XInput and Rock Band guitars are always on; Ground Fog and Light
    // Streaks were removed from every game (0.0.2): their values are dropped.
    Json kept = Json::object();
    for (const auto& [k, v] : e.o)
        if (k != "xinput" && k != "rockband" && k != "ground_fog" && k != "light_streaks") kept.set(k, v);
    e = std::move(kept);
}

namespace {

enum class Kind { Section, Separator, Check, Choice, Button, Cheat };
struct Option {
    const char* value;
    const char* label;
    bool ok = true;  // false: not in gd03 (shown, cannot be chosen)
};
struct Item {
    Kind kind;
    const char* key;   // the setting (enhanced.json), the button's action, or the cheat (its locale token)
    const char* name;  // "" for a cheat: the game's own name (its locale token)
    bool ready;        // false: not in gd03 yet (greyed, cannot be chosen)
    const char* help;
    std::vector<Option> options = {};
    const char* needs = nullptr;  // shown only while this setting is on (a check) or not none / off (a choice)
};

// gd02's list (gd01's), in the doc's sections and order, with its
// explanations (the console's names changed; the cheats' codes are this
// disc's, config/long_cheats.dta); gd03's own Widescreen (16:9)
// after Resolution, and the rows gd03 had before 0.0.2 (Gamepads, Color
// Grading, Sharpening, Disable Shaky Camera) where gd02 has them. Ground Fog
// and Light Streaks are not here (removed from every game, 10-30).
const std::vector<Item>& items() {
    static const std::vector<Item> k = {
        // Modes are presets (enhancedPreset): one click sets every value they name.
        {Kind::Section, "modes", "MODES", true, "Presets: one click sets many options below at once."},
        {Kind::Button, "mode_steam_deck", "Steam Deck", true,
         "Sets the options to run best on a handheld PC such as the Steam Deck: full screen at 60 FPS and an 800p "
         "internal resolution. You can still change any option afterwards."},
        {Kind::Button, "mode_original", "Original Feel", true,
         "Looks like the original PS2 game: every enhanced effect off, 60 FPS. The window, the resolution and the "
         "picture's shape stay as they are. This is how the game starts."},
        {Kind::Button, "mode_clean", "Clean Image", true,
         "Removes every enhanced effect for a clean, sharp picture: full HD (1080p) internal resolution, 120 FPS."},
        {Kind::Button, "mode_max_hd", "Max Out (Full HD)", true,
         "Every effect at its strongest at a full HD (1080p) internal resolution, up to 240 FPS. The effects are not in "
         "this version yet: for now it sets the resolution and the frame rate."},
        {Kind::Button, "mode_max_4k", "Max Out (4K)", true,
         "Every effect at its strongest at a 4K (2160p) internal resolution, up to 240 FPS. The effects are not in this "
         "version yet: for now it sets the resolution and the frame rate."},
        {Kind::Button, "mode_budget", "Rocking on a Budget", true,
         "Every effect at its lowest at 720p and 60 FPS, for slow computers."},
        {Kind::Button, "mode_good_old_days", "Good Old Days", true,
         "Every effect at its strongest, in sepia, at the PlayStation 2's resolution (448p), 60 FPS. The effects are not "
         "in this version yet: for now it sets the resolution and the frame rate."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "controls", "CONTROLS", true, "Gamepads, mouse, touch and your setup's delay."},
        {Kind::Button, "gamepads", "Gamepads", true,
         "The Gamepad menu: each player's instrument and device, and every device's buttons (two per control), lit as "
         "you press them. Also on the main menu's yellow fret."},
        {Kind::Check, "mouse", "Enable Mouse Selection", true,
         "Point at a menu entry to highlight it and click to choose it; a right click goes back and the mouse wheel "
         "scrolls lists. The help bar's buttons (SELECT, BACK, START, UP / DOWN) are clickable too. Hovering works only "
         "while the game window has focus; the pointer hides after a few seconds, when a key or a button is pressed, and "
         "during a song. Mouse buttons are never used as game buttons."},
        {Kind::Check, "touch", "Enable Touch Controls", false,
         "Play on a touch screen; also shows the on-screen buttons. In a song, touch a lane at the bottom of the highway "
         "when its note arrives. On by default on the Steam Deck and Android."},
        {Kind::Button, "calibrate", "Audio / Video Calibration", false,
         "Measures your setup's delay so notes are judged and drawn in time: strum on each tick you hear, then on each "
         "flash you see."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "graphics", "PERFORMANCE & GRAPHICS", true, "Window, frame rate, image quality and the venue's effects."},
        {Kind::Choice, "screen_mode", "Screen Mode", true,
         "Windowed; full screen at the desktop's resolution; or full screen without a border (a window covering the "
         "screen, the quickest to switch away from).",
         {{"windowed", "Windowed"}, {"fullscreen", "Full Screen"}, {"borderless", "Full Screen (No Border)"}}},
        {Kind::Choice, "internal_res", "Resolution", true,
         "The resolution the game draws at before it is scaled to the window. Higher is sharper and costs more; above "
         "the window's own size it also smooths the edges. The PS2 drew 640 x 448 (ps2).",
         {{"0", "Window"}, {"448", "448p (ps2)"}, {"480", "480p"}, {"720", "720p"}, {"800", "800p (Steam Deck)"},
          {"900", "900p"}, {"1080", "1080p"}, {"1440", "1440p"}, {"2160", "4K (2160p)"}}},
        {Kind::Check, "widescreen", "Widescreen (16:9)", true,
         "Off (as on the PS2): the game's picture is 4:3, with black bars at the sides of a wide screen. On: the menus, "
         "the highway and the venues fill a 16:9 screen and show more of the scene at the sides; nothing is stretched. "
         "The port's own screens, such as this one, always use the whole screen."},
        {Kind::Check, "sharp_ui", "Sharper Text & Sprites", false,
         "Upscales the menu art and the game's fonts with a sharper filter, so text and sprites stay crisp on large screens."},
        {Kind::Choice, "antialiasing", "Anti-Aliasing", false,
         "Smooths jagged edges. FXAA: a fast filter over the image. SSAA: draws the game at twice the resolution and "
         "scales it down. TAA: blends each frame with the last ones.",
         {{"none", "None"}, {"fxaa", "FXAA"}, {"ssaa", "SSAA"}, {"taa", "TAA"}}},
        {Kind::Choice, "ai_upscaling", "AI Upscaling", false,
         "Draws the 3D scene below your screen's resolution and upscales it to the screen, for higher frame rates.",
         {{"none", "None"}, {"fsr1", "FSR 1 (Spatial)"}, {"fsr31", "FSR 3.1"}, {"dlss4", "DLSS 4"}}},
        {Kind::Choice, "ai_upscaling_mode", "Upscaling Quality", false,
         "How far below the screen's resolution the scene is drawn: Quality (1.5x upscale, the sharpest), Balanced (1.7x) "
         "or Performance (2x, the fastest).",
         {{"performance", "Performance"}, {"balanced", "Balanced"}, {"quality", "Quality"}}},
        {Kind::Choice, "max_fps", "Max FPS", true,
         "The highest frame rate the game draws at; the original ran at 60. Unlimited draws as fast as the computer can "
         "(no vertical sync).",
         {{"60", "60"}, {"120", "120"}, {"240", "240"}, {"unlimited", "Unlimited"}}},
        {Kind::Check, "fps_counter", "Show FPS Counter", true,
         "Shows the frames drawn per second in the bottom-left corner of the screen."},
        {Kind::Choice, "bokeh", "Bokeh Effect", false,
         "The depth-of-field blur of the close shots. None: always sharp. Light to Stronger blur wider.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"}, {"stronger", "Stronger"}}},
        {Kind::Choice, "particles", "Enable Particles FX", false,
         "Sparks, smoke, fire and the highway's hit effects. None turns them off; 2x emits twice as many.",
         {{"none", "None"}, {"original", "Original"}, {"2x", "2x"}}},
        {Kind::Choice, "bloom", "Bloom", false, "A soft glow around the brightest lights of the venue, like a camera lens.",
         {{"none", "None"}, {"original", "Original"}, {"2x", "2x"}, {"3x", "3x"}}},
        {Kind::Check, "cinematic_color", "Cinematic Color", false,
         "A filmic colour curve with a gentle grade and vignette on the venue."},
        {Kind::Choice, "color_grade", "Color Grading", false, "A colour look for the venue, like a film's grade.",
         {{"off", "Off"}, {"warm", "Warm"}, {"cold", "Cold"}, {"vintage", "Vintage"}, {"noir", "Noir"}, {"neon", "Neon"}, {"sepia", "Sepia"}}},
        {Kind::Choice, "film_grain", "Film Grain", false, "Fine animated grain over the venue, like a concert shot on film.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"}, {"stronger", "Stronger"}}},
        {Kind::Choice, "chromatic", "Chromatic Aberration", false, "A camera lens's colour fringes towards the corners.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"}}},
        {Kind::Choice, "ssao", "Ambient Occlusion", false, "Soft contact shadows where surfaces meet, for more depth.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"}, {"stronger", "Stronger"}}},
        {Kind::Choice, "sharpening", "Sharpening", false, "Contrast-adaptive sharpening of the venue.",
         {{"off", "Off"}, {"low", "Low"}, {"high", "High"}}},
        {Kind::Choice, "star_blur", "Blur on Star Power", false,
         "While Star Power is on, the venue and the band blur with the camera's motion.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"}, {"stronger", "Stronger"}}},
        {Kind::Check, "shadows_all", "All Band Mates Cast Shadows", false,
         "Every band mate gets a spotlight from above and casts a shadow on the stage floor."},
        {Kind::Choice, "more_lights", "More Lights on Venues", false,
         "Richer stage lighting: brighter light cones and floor pools, following the venue's own light cues.",
         {{"none", "None"}, {"1.5x", "1.5x"}, {"2x", "2x"}, {"2.5x", "2.5x"}}},
        {Kind::Choice, "light_dust", "Dust in Light Beams", false, "Fine dust drifting and twinkling in the light beams over the band.",
         {{"none", "None"}, {"1.5x", "1.5x"}, {"2x", "2x"}, {"2.5x", "2.5x"}}},
        {Kind::Check, "no_shaky_cam", "Disable Shaky Camera", false, "Steady close-ups of the band."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "cheats", "CHEATS & ENHANCERS", true, "Your user files, the original game's cheats and more ways to bend the rules."},
        {Kind::Button, "export", "Export User Files", true,
         "Choose a folder to save two files in: GD03-user-settings.json with every Enhanced Option, and "
         "GD03-user-progress.json with all your bands and their progress. Use them as a backup or to move to another "
         "computer."},
        {Kind::Button, "load_settings", "Load User Settings", true,
         "Choose a GD03-user-settings.json file to load. The settings take effect at once; any setting in the file that "
         "is missing or not valid keeps its default."},
        {Kind::Button, "load_progress", "Load User Progress", true,
         "Choose a GD03-user-progress.json file to load. It replaces your bands and their progress and is saved at once; "
         "a file that is not a gd03 save is refused."},
        {Kind::Check, "cut_content", "Enable Cut Content", false, "Loads content left unused on the original disc."},
        {Kind::Check, "no_fail", "No Fail Mode", false, "The rock meter cannot fail you: every song plays to the end."},
        {Kind::Check, "autoplay", "Autoplay", false, "The game plays every note of a song by itself, without a miss."},
        {Kind::Choice, "hopo_mode", "Forgiving Hammer-ons and Pull-offs", false,
         "Notes with a coloured top can be played without strumming, more forgivingly than the original.",
         {{"original", "Original"}, {"forgiving", "Forgiving"}}},
        {Kind::Cheat, "cheat_unlock_all", "", true,
         "The original cheat: every song, venue, character and guitar is open. Saving and the career are off while it "
         "is on. Code at the main menu: Blue, Orange, Yellow, Red, Orange, Yellow, Blue, Yellow, Red, Yellow, Blue, "
         "Yellow, Red, Yellow, Blue, Yellow."},
        {Kind::Cheat, "cheat_air_guitar", "", true,
         "The original cheat: the guitarists play without a guitar. Code at the main menu: Yellow, Blue, Yellow, "
         "Orange, Blue, Blue."},
        {Kind::Cheat, "cheat_performance_mode", "", true,
         "The original cheat: no highway and no meters, only the band and the venue. Code at the main menu: Blue, "
         "Blue, Orange, Yellow, Yellow, Blue, Orange, Blue."},
        {Kind::Cheat, "cheat_hyper_speed_mode", "", true,
         "The original cheat: the notes come down the highway one and a half times as fast. Code at the main menu: "
         "Yellow, Blue, Orange, Orange, Blue, Yellow, Yellow, Orange."},
        {Kind::Cheat, "cheat_flaming_head", "", false,
         "The original cheat: the guitarist's head is on fire (the band's flames do not burn yet in this version). "
         "Code at the main menu: Yellow, Orange, Yellow, Orange, Yellow, Orange, Blue, Orange."},
        {Kind::Cheat, "cheat_eyeball_head_crowd", "", true,
         "The original cheat: everyone in the crowd has a giant eyeball for a head. Code at the main menu: Yellow, "
         "Blue, Orange, Orange, Orange, Blue, Yellow."},
        {Kind::Cheat, "cheat_monkey_head_crowd", "", true,
         "The original cheat: everyone in the crowd wears a monkey head. Code at the main menu: Blue, Blue, "
         "Orange, Yellow, Blue, Blue, Orange, Yellow."},
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
        // The first setting gd03 has (MODES > Steam Deck), as the screen opens.
        if (!entered_) {
            for (size_t r = 0; r < rows_.size(); ++r)
                if (item(int(r)).ready && item(int(r)).kind != Kind::Section && item(int(r)).kind != Kind::Separator) {
                    focus_ = int(r);
                    break;
                }
            entered_ = true;
        }
        dropdown_ = -1;
        scroll_ = scrollTarget_ = 0;
        keepVisible();
        scroll_ = scrollTarget_;
        backdropMissed_ = false, backdropFromMs_ = -1;
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
                for (int k = 0; k < n; ++k) {  // the next option gd03 has
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
        x -= d_.offset();
        if (dropdown_ < 0) {
            // Back / Export / Import under the list (the mockup).
            for (int b = 0; b < 3; ++b) {
                float x0, x1;
                bottomButton(b, x0, x1);
                if (x < x0 || x > x1 || y < kBtnY0 || y > kBtnY1) continue;
                if (hoverButton_ != b) hoverButton_ = b, ui.playSfx("button_toggle");
                if (click && b == 0) ui.buttonDown(kPad_Tri, 0), ui.buttonUp(kPad_Tri, 0);  // as the back button
                else if (click) ui.playSfx("button_select"), ui.host().enhancedAction(b == 1 ? "export" : "load_settings");
                return true;
            }
            hoverButton_ = -1;
            // The scrollbar: a click on its track scrolls there.
            if (x >= kBarX - 10 && x <= kBarX + 10 && y >= kListTop && y <= kListBottom) {
                if (click) scrollTarget_ = std::clamp((y - kListTop) / (kListBottom - kListTop) * maxScroll(), 0.0f, maxScroll()), ui.playSfx("button_toggle");
                return true;
            }
        }
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
        // The user files' (or a mode's) result shows for a few seconds.
        const std::string st = ui.host().enhancedStatus();
        if (st != status_) status_ = st, statusMs_ = st.empty() ? 0 : 5000;
        statusMs_ = std::max(0.0f, statusMs_ - float(dtMs));
    }

    void draw() override {
        if (ui.headless()) return;
        d_.updateTransition();
        // The still venue behind (App::menuBackdrop), else a black page.
        // It is the game's picture, 4:3 or 16:9 as the Widescreen row says,
        // so the change shows at once; the page itself is on the whole frame.
        const bool still = ui.host().drawMenuBackdrop();
        const PortDraw::FullFrame frame(ui);
        for (int pass : {0, 1}) {
            d_.begin(pass);
            if (pass == 0) {
                // The venue a little darker, as gd01's blurred one reads; one
                // that arrives after the screen opened fades in over 0.3 s.
                const double now = double(SDL_GetTicksNS()) / 1e6;
                if (!still) backdropMissed_ = true;
                else if (backdropMissed_ && backdropFromMs_ < 0) backdropFromMs_ = now;
                const float in = backdropFromMs_ < 0 ? 1.0f : float(std::clamp((now - backdropFromMs_) / 300.0, 0.0, 1.0));
                d_.quad(0, 0, kScreenW, kScreenH, {0, 0, 0, still ? 1.0f - 0.75f * in : 1.0f});
            }
            drawAll();
        }
        // The open dropdown over everything (its own quads, then its texts).
        if (dropdown_ >= 0)
            for (int pass : {0, 1}) d_.begin(pass), drawDropdown();
    }

private:
    // gd01's layout (menu units): the options box on the left, the
    // explanation bottom right (bottom-aligned with the box).
    static constexpr float kListTop = 168, kListBottom = 572, kListX0 = 80, kNameX = 118, kCtrlX1 = 612;
    // The mockup's scrollbar (left of the list) and its Back / Export / Import buttons (the box's bottom).
    static constexpr float kBarX = 66, kBtnY0 = 590, kBtnY1 = 626, kBtnW = 150;
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
        return it == collapsed_.end() ? false : it->second;  // every section starts open (gd01)
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
            ui.host().enhancedAction(it.key);  // a mode, Export / Load User Files
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
        // The user files' (or a mode's) result for a few seconds.
        if (statusMs_ > 0) {
            Color c = PortDraw::kOn;
            c[3] = std::min(1.0f, statusMs_ / 500);
            d_.text(d_.fit(status_, 18, kBoxX1 - kBoxX0 - 60), 92 + o, 138, 18, c, 0x11);
        }
        for (size_t r = 0; r < rows_.size(); ++r) drawRow(int(r));
        // More above / below, and the scrollbar.
        if (scroll_ > 1) d_.arrow(kListX0 + 270 + o, kListTop - 10, true, kDim);
        if (contentHeight() - scroll_ > kListBottom - kListTop + 1) d_.arrow(kListX0 + 270 + o, kListBottom + 8, false, kDim);
        {
            const float h = kListBottom - kListTop, content = std::max(contentHeight(), h), thumb = std::max(24.0f, h * h / content);
            const float t0 = kListTop + (h - thumb) * (maxScroll() > 0 ? std::clamp(scroll_ / maxScroll(), 0.0f, 1.0f) : 0.0f);
            d_.quad(kBarX - 3 + o, kListTop, kBarX + 3 + o, kListBottom, {1, 1, 1, 0.1f});
            d_.quad(kBarX - 4 + o, t0, kBarX + 4 + o, t0 + thumb, {1, 1, 1, 0.45f});
        }
        for (int b = 0; b < 3; ++b) drawBottomButton(b);
        drawHelp();
    }

    // -- Back / Export / Import under the list (the mockup's colours: red, yellow, green)
    static void bottomButton(int b, float& x0, float& x1) {
        const float step = (kBoxX1 - kBoxX0 - 40 - kBtnW) / 2;
        x0 = kBoxX0 + 20 + float(b) * step, x1 = x0 + kBtnW;
    }
    void drawBottomButton(int b) {
        const float o = d_.offset();
        float x0, x1;
        bottomButton(b, x0, x1);
        const Color c = b == 0 ? Color{1, 0.45f, 0.45f, 1} : b == 1 ? Color{1, 0.86f, 0.35f, 1} : Color{0.45f, 0.95f, 0.5f, 1};
        const bool h = hoverButton_ == b;
        d_.quad(x0 + o, kBtnY0, x1 + o, kBtnY1, {c[0], c[1], c[2], h ? 0.4f : 0.2f});
        d_.frame(x0 + o, kBtnY0, x1 + o, kBtnY1, h ? 3.0f : 1.5f, c);
        d_.text(b == 0 ? "< Back" : b == 1 ? "Export" : "Import", (x0 + x1) * 0.5f + o, (kBtnY0 + kBtnY1) * 0.5f + 1, 21, h ? kFocused : kNormal, 0x22);
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
        const float height = pad + 46 + float(lines.size()) * lineH + (notYet || locked ? 42 : 0) + pad;
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
        if (notYet) {
            y += 6;
            d_.warning(x0 + pad, y + 12);
            d_.text("Not available yet", x0 + pad + 30, y + 13, 21, kWarn, 0x21);
        } else if (locked) {
            y += 6;
            d_.text("Always on in the Android app", x0 + pad, y + 13, 21, kAccent, 0x21);
        }
    }

    PortDraw d_;
    std::string status_;
    float statusMs_ = 0;
    std::vector<int> rows_;  // indexes into items() of the rows shown
    std::map<std::string, bool> collapsed_;
    int focus_ = 0, dropdown_ = -1, ddFocus_ = 0, hoverButton_ = -1;
    bool entered_ = false;
    bool backdropMissed_ = false;  // drawn without the backdrop since entering (it fades in when it comes)
    double backdropFromMs_ = -1;
    float scroll_ = 0, scrollTarget_ = 0, ddTop_ = 0;
};

}  // namespace

std::unique_ptr<ScriptObject> makeEnhancedPanel(UiManager& ui, const std::string& name) { return std::make_unique<EnhancedPanelObj>(ui, name); }

}  // namespace ui
