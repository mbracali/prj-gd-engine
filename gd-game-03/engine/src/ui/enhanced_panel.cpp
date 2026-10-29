// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The port's Enhanced Options: gd01's Enhanced Settings list
// (gd-game-01/engine/src/ui/select_panels.cpp, EnhancedPanel): its
// sections, rows, check boxes, dropdowns, explanation box and mouse, in
// GH2's menu font and colours (PortDraw) over a still shot of a venue
// (guidelines/universal_launcher.md).
#include "ui/enhanced_panel.h"

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
    // it starts on, as in every gd-game (guidelines/universal_launcher.md).
    static const std::vector<EnhancedSetting> k = {
        {"screen_mode", {"windowed", "fullscreen", "borderless"}},
        {"max_fps", {"60", "120", "240"}},
        {"fps_counter", {"off", "on"}},
        {"mouse", {"on", "off"}},
        {"internal_res", {"0", "448", "480", "720", "800", "900", "1080", "1440", "2160"}},
        {"widescreen", {"off", "on"}},
    };
    return k;
}

namespace {

enum class Kind { Section, Separator, Check, Choice, Button, Cheat };
struct Option {
    const char* value;
    const char* label;
};
struct Item {
    Kind kind;
    const char* key;   // the setting (enhanced.json), the button's action, or the cheat (its locale token)
    const char* name;  // "" for a cheat: GH2's own name (its locale token)
    bool ready;        // false: not in gd03 yet (greyed, cannot be chosen)
    const char* help;
    std::vector<Option> options = {};
    const char* needs = nullptr;  // shown only while this setting is on (gd01's dependent rows)
};

// gd01's list, in its sections and order; what gd03 has is ready. The
// cheats are GH2's (config/long_cheats.dta), with their button codes.
const std::vector<Item>& items() {
    static const std::vector<Item> k = {
        {Kind::Section, "deck", "STEAM DECK MODE", true, "Settings for handheld PCs such as the Steam Deck."},
        {Kind::Button, "steam_deck", "Apply Steam Deck Settings", false,
         "Sets every option to what runs best on a handheld PC such as the Steam Deck: full screen at 60 FPS, a 1280 x 800 "
         "internal resolution and lighter effects."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "controls", "CONTROLS AND GAMEPAD", true, "Gamepads, mouse, touch and guitars."},
        {Kind::Button, "gamepads", "Gamepads", true,
         "The connected guitars, gamepads and the keyboard: which player each one is, its buttons lit as you press them, "
         "and new buttons for any of them."},
        {Kind::Check, "mouse", "Enable Mouse Selection", true,
         "Point at a menu entry to highlight it and click to choose it; a right click goes back and the mouse wheel "
         "scrolls lists. The help bar's items are clickable too. Hovering works only while the game window has focus; "
         "the pointer hides after a few seconds, when a key or a button is pressed, and during a song."},
        {Kind::Check, "touch", "Enable Touch Controls", false,
         "Play on a touch screen: touch a lane at the bottom of the highway when its note arrives."},
        {Kind::Check, "rockband", "Enable Rock Band Guitars", false,
         "Plays with the USB Rock Band guitars for Xbox 360 and PlayStation 3 / 4: the frets, the strum bar, the whammy "
         "bar and tilt."},
        {Kind::Check, "xinput", "Enable XInput Controllers", false,
         "Any XInput gamepad plays like the original game's gamepad mode."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "graphics", "PERFORMANCE AND GRAPHICS", true, "Timing, window, frame rate and image quality."},
        {Kind::Button, "calibrate", "Audio / Video Calibration", false,
         "Measures your setup's delay so notes are judged and drawn in time."},
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
         "the window's own size it also smooths the edges. The Xbox 360 drew 1280 x 720 (x360), the PS2 640 x 448 (ps2).",
         {{"0", "Window"}, {"448", "448p (ps2)"}, {"480", "480p"}, {"720", "720p (x360)"}, {"800", "800p"},
          {"900", "900p"}, {"1080", "1080p"}, {"1440", "1440p"}, {"2160", "4K (2160p)"}}},
        {Kind::Check, "widescreen", "Widescreen (16:9)", true,
         "Off (as on the PS2): the game's picture is 4:3, with black bars at the sides of a wide screen. On: the menus, "
         "the highway and the venues fill a 16:9 screen and show more of the scene at the sides; nothing is stretched. "
         "The port's own screens, such as this one, always use the whole screen."},
        {Kind::Check, "sharp_ui", "Sharper Text and Sprites", false,
         "Upscales the menu art and the game's fonts with a sharper filter, so text stays crisp on large screens."},
        {Kind::Choice, "antialiasing", "Anti-Aliasing", false, "Smooths jagged edges: FXAA, SSAA or TAA.",
         {{"none", "None"}, {"fxaa", "FXAA"}, {"ssaa", "SSAA"}, {"taa", "TAA"}}},
        {Kind::Choice, "ai_upscaling", "AI Upscaling", false,
         "Draws the 3D scene below your screen's resolution and upscales it (FSR 1, FSR 3.1, DLSS 4).",
         {{"none", "None"}, {"fsr1", "FSR 1 (Spatial)"}, {"fsr31", "FSR 3.1"}, {"dlss4", "DLSS 4"}}},
        {Kind::Choice, "ai_upscaling_mode", "Upscaling Quality", false,
         "How far below the screen's resolution the scene is drawn: Quality (1.5x), Balanced (1.7x) or Performance (2x).",
         {{"quality", "Quality"}, {"balanced", "Balanced"}, {"performance", "Performance"}}, "ai_upscaling"},
        {Kind::Choice, "bokeh", "Bokeh Effect", false,
         "The depth-of-field blur of the close shots. Regular: as the original game. None: always sharp. Stronger: a "
         "wider blur.",
         {{"regular", "Regular"}, {"none", "None"}, {"stronger", "Stronger"}}},
        {Kind::Check, "particles", "Enable Particles FX", false, "Sparks, smoke, dust and the highway's hit effects."},
        {Kind::Choice, "bloom", "Bloom", false, "A soft glow around the brightest lights of the venue, like a camera lens.",
         {{"off", "Off"}, {"low", "Low"}, {"medium", "Medium"}, {"high", "High"}}},
        {Kind::Check, "cinematic_color", "Cinematic Color", false,
         "A filmic colour curve with a gentle grade and vignette on the venue."},
        {Kind::Choice, "color_grade", "Color Grading", false, "A colour look for the venue, like a film's grade.",
         {{"off", "Off"}, {"warm", "Warm"}, {"cold", "Cold"}, {"vintage", "Vintage"}, {"noir", "Noir"}, {"neon", "Neon"}}},
        {Kind::Choice, "film_grain", "Film Grain", false, "Fine animated grain over the venue, like a concert shot on film.",
         {{"off", "Off"}, {"light", "Light"}, {"strong", "Strong"}}},
        {Kind::Check, "chromatic", "Chromatic Aberration", false, "A camera lens's colour fringes towards the corners."},
        {Kind::Check, "light_streaks", "Anamorphic Light Streaks", false,
         "The brightest stage lights flare into long horizontal streaks, like a cinema lens."},
        {Kind::Check, "ssao", "Ambient Occlusion", false, "Soft contact shadows where surfaces meet, for more depth."},
        {Kind::Choice, "sharpening", "Sharpening", false, "Contrast-adaptive sharpening of the venue.",
         {{"off", "Off"}, {"low", "Low"}, {"high", "High"}}},
        {Kind::Choice, "star_blur", "Blur on Star Power", false,
         "While Star Power is on, the venue and the band blur with the camera's motion.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"high", "High"}}},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "enhancers", "ENHANCERS", true, "Additions to the venues and the camera, and your files."},
        {Kind::Check, "shadows_all", "All Band Mates Cast Shadows", false,
         "Every band mate gets a spotlight from above and casts a shadow on the stage floor."},
        {Kind::Choice, "more_lights", "More Lights on Venues", false,
         "Richer stage lighting: brighter light cones and floor pools, following the venue's own light cues.",
         {{"off", "Off"}, {"subtle", "Subtle"}, {"strong", "Strong"}}},
        {Kind::Check, "light_dust", "Dust in Light Beams", false, "With More Lights on Venues: fine dust drifting in the beams.",
         {}, "more_lights"},
        {Kind::Choice, "ground_fog", "Add Fog to the Ground (Experimental)", false,
         "A low fog over the stage floor where the band plays.",
         {{"none", "None"}, {"low", "Low"}, {"full", "Full"}, {"super", "Super Fog"}}},
        {Kind::Check, "no_shaky_cam", "Disable Shaky Camera", false, "Steady close-ups of the band."},
        {Kind::Button, "export", "Export User Files", false, "Saves your settings and your bands to a folder of your choice."},
        {Kind::Button, "load_settings", "Load User Settings", false, "Loads settings saved with Export User Files."},
        {Kind::Button, "load_progress", "Load User Progress", false, "Loads bands saved with Export User Files."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "cheats", "CHEATS", true, "The original game's cheats, and the port's."},
        {Kind::Check, "cut_content", "Enable Cut Content", false, "Loads content left unused on the original disc."},
        {Kind::Check, "no_fail", "No Fail Mode", false, "The rock meter cannot fail you: every song plays to the end."},
        {Kind::Check, "autoplay", "Autoplay", false, "The game plays every note of a song by itself, without a miss."},
        {Kind::Choice, "hopo_mode", "Forgiving Hammer-ons and Pull-offs", false,
         "Notes with a coloured top can be played without strumming, more forgivingly than the original.",
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
        // The first setting gd03 has (Gamepads), as the screen opens.
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
                setValue(it, it.options[size_t(ddFocus_)].value);
                dropdown_ = -1;
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
                if (!it.ready) return ui.playSfx("button_error"), true;
                const int n = int(it.options.size());
                setValue(it, it.options[size_t((optionIndex(it) + dir + n) % n)].value);
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
        // The open dropdown: its options, or a click anywhere else closes it.
        if (dropdown_ >= 0) {
            const Item& it = item(focus_);
            for (size_t k = 0; k < it.options.size(); ++k) {
                const float oy = ddTop_ + float(k) * kDdRow;
                if (x < kDdPopX0 || x > kCtrlX1 || y < oy || y > oy + kDdRow) continue;
                if (int(k) != ddFocus_) ddFocus_ = int(k), ui.playSfx("button_toggle");
                if (click) setValue(it, it.options[k].value), dropdown_ = -1;
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
    }

    void draw() override {
        if (ui.headless()) return;
        d_.updateTransition();
        // The still venue behind (App::menuBackdrop), else a black page. It
        // is the game's picture, 4:3 or 16:9 as the Widescreen row says, so
        // the change shows at once; the page itself is on the whole frame.
        const bool still = ui.host().drawMenuBackdrop();
        const PortDraw::FullFrame frame(ui);
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
    // A setting another row depends on is on: a check on, a choice not none / off.
    bool settingOn(const char* key) const {
        const std::string v = ui.host().enhancedValue(key);
        return !v.empty() && v != "off" && v != "none";
    }
    std::string name(const Item& it) const { return it.kind == Kind::Cheat ? ui.localize(it.key) : it.name; }
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
        if (!it.ready) return ui.playSfx("button_error");
        ui.host().setEnhancedValue(it.key, v);
        rebuild();  // a dependent row may show or hide
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
            if (!it.ready) return ui.playSfx("button_error");
            ui.host().setEnhancedValue(it.key, isOn(it) ? "off" : "on");
            rebuild();  // a dependent row may show or hide
            ui.playSfx("button_select");
            return;
        case Kind::Cheat:
            if (!it.ready) return ui.playSfx("button_error");
            ui.setCheat(it.key, !isOn(it));
            return;
        case Kind::Choice:
            if (!it.ready) return ui.playSfx("button_error");
            if (isToggle(it)) return setValue(it, it.options[optionIndex(it) == 1 ? 0 : 1].value);
            dropdown_ = focus_;
            ddFocus_ = optionIndex(it);
            ui.playSfx("button_select");
            return;
        case Kind::Button:
            if (!it.ready) return ui.playSfx("button_error");
            ui.playSfx("button_select");
            if (std::string(it.key) == "gamepads") ui.gotoScreen("gamepads_screen");
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
        const bool locked = !it.ready;
        const Color nameC = f ? kFocused : locked ? kDim : kNormal;
        const std::string n = name(it);
        // A long name next to a dropdown is drawn smaller to fit before it.
        float nameH = 23;
        const float room = (it.kind == Kind::Choice && !isToggle(it) ? kDdX0 : kCtrlX1 - (it.kind == Kind::Button ? 130 : 40)) - kNameX - 14;
        if (const float w = d_.width(n, 23); w > room - (locked ? 30 : 0)) nameH = std::max(15.0f, 23 * (room - (locked ? 30 : 0)) / w);
        d_.text(n, kNameX + o, cy + 1, nameH, nameC, 0x21);
        if (locked) d_.warning(kNameX + o + d_.width(n, nameH) + 10, cy);
        const Color ctrl = locked ? kDim : f ? kFocused : kNormal;
        const Kind shown = isToggle(it) ? Kind::Check : it.kind == Kind::Cheat ? Kind::Check : it.kind;
        switch (shown) {
        case Kind::Check:
            d_.checkBox(kCtrlX1 + o, cy, isToggle(it) ? optionIndex(it) == 1 : isOn(it), ctrl, locked ? kDim : PortDraw::kOn);  // "on" is green (gd01)
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
            d_.text(it.options[k].label, x0 + 12, oy + kDdRow * 0.5f + 1, 20, hot ? kFocused : kNormal, 0x21);
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
        const float height = pad + 46 + float(lines.size()) * lineH + (notYet ? 42 : 0) + pad;
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
        }
    }

    PortDraw d_;
    std::vector<int> rows_;  // indexes into items() of the rows shown
    std::map<std::string, bool> collapsed_;
    int focus_ = 0, dropdown_ = -1, ddFocus_ = 0;
    bool entered_ = false;
    float scroll_ = 0, scrollTarget_ = 0, ddTop_ = 0;
};

}  // namespace

std::unique_ptr<ScriptObject> makeEnhancedPanel(UiManager& ui, const std::string& name) { return std::make_unique<EnhancedPanelObj>(ui, name); }

}  // namespace ui
