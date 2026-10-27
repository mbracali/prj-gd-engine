// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The port's Enhanced Options: gd01's Enhanced Settings list
// (gd-game-01/engine/src/ui/select_panels.cpp, EnhancedPanel), drawn in
// GH2's menu font and colours over a live venue.
#include "ui/enhanced_panel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "render/canvas.h"
#include "ui/screen_text.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"
#include "ui/ui_objects.h"

namespace ui {

const std::vector<EnhancedSetting>& enhancedSettings() {
    static const std::vector<EnhancedSetting> k = {
        {"screen_mode", {"windowed", "fullscreen", "borderless"}},
        {"max_fps", {"60", "120", "240"}},
        {"fps_counter", {"off", "on"}},
        {"internal_res", {"0", "448", "480", "720", "800", "900", "1080", "1440", "2160"}},
    };
    return k;
}

namespace {

enum class Kind { Section, Check, Choice, Button, Cheat };
struct Option {
    const char* value;
    const char* label;
};
struct Item {
    Kind kind;
    const char* key;   // the setting (enhanced.json), the button's action, or the cheat (its locale token)
    const char* name;  // "" for a cheat: GH2's own name (its locale token)
    bool ready;        // false: not in gd02 yet (greyed, cannot be chosen)
    const char* help;
    std::vector<Option> options = {};
};

// gd01's list, in its sections and order; what gd02 has is ready. The
// cheats are GH2's (config/long_cheats.dta), with their button codes.
const std::vector<Item>& items() {
    static const std::vector<Item> k = {
        {Kind::Section, "deck", "STEAM DECK MODE", true, ""},
        {Kind::Button, "steam_deck", "Apply Steam Deck Settings", false,
         "Sets every option to what runs best on a handheld PC such as the Steam Deck: full screen at 60 FPS, a 1280 x 800 "
         "internal resolution and lighter effects."},
        {Kind::Section, "controls", "CONTROLS AND GAMEPAD", true, ""},
        {Kind::Button, "gamepads", "Gamepads", true,
         "The connected guitars, gamepads and the keyboard: which player each one is, its buttons lit as you press them, "
         "and new buttons for any of them."},
        {Kind::Check, "mouse", "Enable Mouse Selection", false,
         "Point at a menu entry to highlight it and click to choose it; the mouse wheel scrolls lists."},
        {Kind::Check, "touch", "Enable Touch Controls", false,
         "Play on a touch screen: touch a lane at the bottom of the highway when its note arrives."},
        {Kind::Check, "rockband", "Enable Rock Band Guitars", false,
         "Plays with the USB Rock Band guitars for Xbox 360 and PlayStation 3 / 4: the frets, the strum bar, the whammy "
         "bar and tilt."},
        {Kind::Check, "xinput", "Enable XInput Controllers", false,
         "Any XInput gamepad plays like the original game's gamepad mode."},
        {Kind::Section, "graphics", "PERFORMANCE AND GRAPHICS", true, ""},
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
        {Kind::Check, "sharp_ui", "Sharper Text and Sprites", false,
         "Upscales the menu art and the game's fonts with a sharper filter, so text stays crisp on large screens."},
        {Kind::Choice, "antialiasing", "Anti-Aliasing", false, "Smooths jagged edges: FXAA, SSAA or TAA.",
         {{"none", "None"}}},
        {Kind::Choice, "ai_upscaling", "AI Upscaling", false,
         "Draws the 3D scene below your screen's resolution and upscales it (FSR 1, FSR 3.1, DLSS 4).", {{"none", "None"}}},
        {Kind::Choice, "bokeh", "Bokeh Effect", false, "The depth-of-field blur of the close shots.", {{"regular", "Regular"}}},
        {Kind::Check, "particles", "Enable Particles FX", false, "Sparks, smoke, dust and the highway's hit effects."},
        {Kind::Choice, "bloom", "Bloom", false, "A soft glow around the brightest lights of the venue, like a camera lens.",
         {{"regular", "Regular"}}},
        {Kind::Check, "cinematic_color", "Cinematic Color", false,
         "A filmic colour curve with a gentle grade and vignette on the venue."},
        {Kind::Choice, "color_grade", "Color Grading", false, "A colour look for the venue, like a film's grade.",
         {{"off", "Off"}}},
        {Kind::Choice, "film_grain", "Film Grain", false, "Fine animated grain over the venue, like a concert shot on film.",
         {{"off", "Off"}}},
        {Kind::Check, "chromatic", "Chromatic Aberration", false, "A camera lens's colour fringes towards the corners."},
        {Kind::Check, "light_streaks", "Anamorphic Light Streaks", false,
         "The brightest stage lights flare into long horizontal streaks, like a cinema lens."},
        {Kind::Check, "ssao", "Ambient Occlusion", false, "Soft contact shadows where surfaces meet, for more depth."},
        {Kind::Choice, "sharpening", "Sharpening", false, "Contrast-adaptive sharpening of the venue.", {{"off", "Off"}}},
        {Kind::Choice, "star_blur", "Blur on Star Power", false,
         "While Star Power is on, the venue and the band blur with the camera's motion.", {{"none", "None"}}},
        {Kind::Section, "enhancers", "ENHANCERS", true, ""},
        {Kind::Check, "shadows_all", "All Band Mates Cast Shadows", false,
         "Every band mate gets a spotlight from above and casts a shadow on the stage floor."},
        {Kind::Choice, "more_lights", "More Lights on Venues", false,
         "Richer stage lighting: brighter light cones and floor pools, following the venue's own light cues.",
         {{"off", "Off"}}},
        {Kind::Choice, "ground_fog", "Add Fog to the Ground (Experimental)", false,
         "A low fog over the stage floor where the band plays.", {{"none", "None"}}},
        {Kind::Check, "no_shaky_cam", "Disable Shaky Camera", false, "Steady close-ups of the band."},
        {Kind::Button, "export", "Export User Files", false, "Saves your settings and your bands to a folder of your choice."},
        {Kind::Button, "load_settings", "Load User Settings", false, "Loads settings saved with Export User Files."},
        {Kind::Button, "load_progress", "Load User Progress", false, "Loads bands saved with Export User Files."},
        {Kind::Section, "cheats", "CHEATS", true, ""},
        {Kind::Check, "cut_content", "Enable Cut Content", false, "Loads content left unused on the original disc."},
        {Kind::Check, "no_fail", "No Fail Mode", false, "The rock meter cannot fail you: every song plays to the end."},
        {Kind::Check, "autoplay", "Autoplay", false, "The game plays every note of a song by itself, without a miss."},
        {Kind::Choice, "hopo_mode", "Forgiving Hammer-ons and Pull-offs", false,
         "Notes with a coloured top can be played without strumming, more forgivingly than the original.",
         {{"original", "Original"}}},
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
    EnhancedPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "EnhancedPanel") {}

    std::string fileName() override { return ""; }
    bool drawsSomething() const override { return true; }

    void enter() override {
        if (focus_ < 0 || !focusable(focus_)) {  // the first setting gd02 has
            focus_ = next(-1, 1);
            for (int i = 0; i < int(items().size()); ++i)
                if (focusable(i) && items()[size_t(i)].ready) {
                    focus_ = i;
                    break;
                }
        }
        scroll_ = scrollTarget_ = 0;
        keepVisible(true);
        ui.host().menuBackdrop(true);
        PanelObj::enter();
    }
    void exit() override {
        ui.host().menuBackdrop(false);
        PanelObj::exit();
    }

    bool buttonDown(int button, int player) override {
        (void)player;
        if (button == kPad_DUp || button == kPad_DDown) {
            focus_ = next(focus_, button == kPad_DDown ? 1 : -1);
            keepVisible(false);
            ui.playSfx("button_toggle");
            return true;
        }
        const Item& it = items()[size_t(focus_)];
        if (button == kPad_DLeft || button == kPad_DRight) {
            if (it.kind == Kind::Choice || it.kind == Kind::Check || it.kind == Kind::Cheat) change(it, button == kPad_DRight ? 1 : -1);
            return true;
        }
        if (button == kPad_X) return change(it, 1), true;
        return false;  // back: the screen's auto back
    }

    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        const float k = 1 - std::exp(-std::max(float(dtMs), 0.0f) / 70.0f);
        scroll_ += (scrollTarget_ - scroll_) * k;
        if (std::fabs(scrollTarget_ - scroll_) < 0.3f) scroll_ = scrollTarget_;
        timeMs_ += dtMs;
    }

    void draw() override {
        if (ui.headless()) return;
        const bool live = ui.host().drawMenuBackdrop();
        pass_ = 0;
        if (!live) quad(0, 0, kScreenW, kScreenH, {0.02f, 0.02f, 0.03f, 1});
        drawAll(live);
        ui.canvas().flush();
        ui.screenText().begin(ui.canvas().renderer());
        pass_ = 1;
        drawAll(live);
    }

private:
    static constexpr float kBoxX0 = 40, kBoxX1 = 600, kBoxY0 = 26, kBoxY1 = 640;
    static constexpr float kListX0 = 66, kListX1 = 574, kListTop = 150, kListBottom = 624, kRow = 32, kSectionRow = 42;
    static constexpr float kHelpX0 = 876, kHelpX1 = 1240, kHelpY1 = 640;
    // GH2's options colours: the gold of a button, white when focused, the
    // red-orange of the posters' titles.
    static constexpr std::array<float, 4> kGold{0.96f, 0.76f, 0.26f, 1}, kWhite{1, 1, 1, 1}, kTitle{0.93f, 0.33f, 0.16f, 1},
        kDim{0.5f, 0.5f, 0.52f, 1}, kSoft{0.82f, 0.8f, 0.76f, 1}, kOn{1, 0.84f, 0.3f, 1};
    static constexpr const char* kFont = "helveticablack.font";

    bool focusable(int i) const { return i >= 0 && i < int(items().size()) && items()[size_t(i)].kind != Kind::Section; }
    int next(int from, int dir) const {
        const int n = int(items().size());
        int i = from;
        for (int k = 0; k < n; ++k) {
            i = (i + dir + n) % n;
            if (focusable(i)) return i;
        }
        return std::max(from, 0);
    }
    float rowHeight(const Item& it) const { return it.kind == Kind::Section ? kSectionRow : kRow; }
    float rowTop(int i) const {  // in list units, from the list's top
        float y = 0;
        for (int k = 0; k < i; ++k) y += rowHeight(items()[size_t(k)]);
        return y;
    }
    void keepVisible(bool jump) {
        const float top = rowTop(focus_), h = rowHeight(items()[size_t(focus_)]), view = kListBottom - kListTop;
        // The section header above the first row of a section stays in view with it.
        const float want = focus_ > 0 && items()[size_t(focus_ - 1)].kind == Kind::Section ? top - kSectionRow : top;
        if (want < scrollTarget_) scrollTarget_ = want;
        if (top + h > scrollTarget_ + view) scrollTarget_ = top + h - view;
        const float total = rowTop(int(items().size()));
        scrollTarget_ = std::clamp(scrollTarget_, 0.0f, std::max(0.0f, total - view));
        if (jump) scroll_ = scrollTarget_;
    }

    bool ready(const Item& it) const { return it.ready; }
    std::string name(const Item& it) const { return it.kind == Kind::Cheat ? ui.localize(it.key) : it.name; }
    bool isOn(const Item& it) const {
        if (it.kind == Kind::Cheat) return ui.cheatOn(it.key);
        return ui.host().enhancedValue(it.key) == "on";
    }
    std::string value(const Item& it) const {
        if (!ready(it)) return "";
        if (it.kind == Kind::Check || it.kind == Kind::Cheat) return isOn(it) ? "ON" : "OFF";
        if (it.kind == Kind::Choice) {
            const std::string v = ui.host().enhancedValue(it.key);
            for (const Option& o : it.options)
                if (v == o.value) return o.label;
            return it.options.empty() ? "" : it.options[0].label;
        }
        return "";
    }

    void change(const Item& it, int dir) {
        if (!ready(it)) return ui.playSfx("button_error");
        switch (it.kind) {
        case Kind::Check: ui.host().setEnhancedValue(it.key, isOn(it) ? "off" : "on"), ui.playSfx("button_select"); break;
        case Kind::Cheat: ui.setCheat(it.key, !isOn(it)); break;
        case Kind::Choice: {
            const std::string v = ui.host().enhancedValue(it.key);
            const int n = int(it.options.size());
            int i = 0;
            for (int k = 0; k < n; ++k)
                if (v == it.options[size_t(k)].value) i = k;
            i = (i + dir + n) % n;
            ui.host().setEnhancedValue(it.key, it.options[size_t(i)].value);
            ui.playSfx("button_select");
            break;
        }
        case Kind::Button:
            if (dir < 0) return;
            if (std::string(it.key) == "gamepads") {
                ui.playSfx("button_select");
                ui.gotoScreen("gamepads_screen");
            }
            break;
        default: break;
        }
    }

    void drawAll(bool live) {
        // The list's box: a dark glass over the venue's left side, a soft
        // shadow to the right of it so the stage reads behind.
        if (live) {
            quad(kBoxX0, kBoxY0, kBoxX1, kBoxY1, {0, 0, 0, 0.8f});
            for (int i = 0; i < 12; ++i) quad(kBoxX1 + float(i) * 3, kBoxY0, kBoxX1 + float(i + 1) * 3, kBoxY1, {0, 0, 0, 0.3f * (1 - float(i) / 12)});
        }
        quad(kBoxX0, kBoxY0, kBoxX1, kBoxY0 + 3, kTitle);
        text("ENHANCED OPTIONS", kListX0, 48, 44, kTitle, 0x11);
        text("Features of this PC version, beyond the original game", kListX0 + 2, 100, 16, kSoft, 0x11);
        // The rows (clipped to the list's band).
        const auto& list = items();
        float y = kListTop - scroll_;
        for (int i = 0; i < int(list.size()); ++i) {
            const Item& it = list[size_t(i)];
            const float h = rowHeight(it);
            if (y + h > kListTop - 0.5f && y < kListBottom + 0.5f && y >= kListTop - 0.5f && y + h <= kListBottom + 0.5f) drawRow(it, i, y, h);
            y += h;
        }
        // More above / below.
        if (scroll_ > 1) arrow(kListX1 + 13, kListTop + 8, true);
        if (rowTop(int(list.size())) - scroll_ > kListBottom - kListTop + 1) arrow(kListX1 + 13, kListBottom - 8, false);
        drawHelp(list[size_t(focus_)]);
    }

    void drawRow(const Item& it, int i, float y, float h) {
        if (it.kind == Kind::Section) {
            text(it.name, kListX0, y + h - 9, 21, kTitle, 0x41);
            quad(kListX0, y + h - 5, kListX1, y + h - 3, {kTitle[0], kTitle[1], kTitle[2], 0.55f});
            return;
        }
        const bool f = i == focus_, ok = ready(it);
        const float mid = y + h * 0.5f;
        if (f) {
            quad(kListX0 - 10, y + 1, kListX1 + 8, y + h - 1, {1, 0.8f, 0.3f, ok ? 0.2f : 0.1f});
            quad(kListX0 - 10, y + 1, kListX0 - 6, y + h - 1, ok ? kGold : kDim);
        }
        const std::array<float, 4> c = !ok ? kDim : f ? kWhite : kGold;
        std::string v = ok ? value(it) : std::string("NOT AVAILABLE YET");
        if (it.kind == Kind::Button && ok) v = f ? "OPEN  >" : "OPEN";
        if ((it.kind == Kind::Choice) && ok && f && !v.empty()) v = "<  " + v + "  >";
        const float vh = ok ? 18 : 14;
        // The name shrinks to leave room for the value.
        const std::string n = name(it);
        const float room = kListX1 - (v.empty() ? 0 : measure(v, vh) + 18) - (kListX0 + 6);
        float nh = 20;
        if (const float w = measure(n, nh); w > room && w > 0) nh = std::max(13.0f, nh * room / w);
        text(n, kListX0 + 6, mid + 1, nh, c, 0x21);
        if (v.empty()) return;
        std::array<float, 4> vc = !ok ? kDim : (it.kind == Kind::Check || it.kind == Kind::Cheat) && isOn(it) ? kOn : f ? kWhite : kSoft;
        text(v, kListX1, mid + 1, vh, vc, 0x24);
    }

    // The focused setting's explanation, bottom right over the venue.
    void drawHelp(const Item& it) {
        std::vector<std::string> lines;
        wrap(it.help, 17, kHelpX1 - kHelpX0 - 36, lines);
        const float lineH = 23, top = kHelpY1 - 70 - float(lines.size()) * lineH;
        quad(kHelpX0, top, kHelpX1, kHelpY1, {0, 0, 0, 0.78f});
        quad(kHelpX0, top, kHelpX1, top + 3, ready(it) ? kGold : kDim);
        const std::string n = name(it);
        float th = 22;
        if (const float w = measure(n, th), room = kHelpX1 - kHelpX0 - 36; w > room) th = std::max(14.0f, th * room / w);
        text(n, kHelpX0 + 18, top + 16 + (22 - th) * 0.5f, th, ready(it) ? kGold : kDim, 0x11);
        float ly = top + 52;
        for (const std::string& l : lines) text(l, kHelpX0 + 18, ly, 17, kSoft, 0x11), ly += lineH;
        if (!ready(it)) text("Not available in this version yet.", kHelpX0 + 18, ly + 2, 15, kDim, 0x11);
    }
    void wrap(const std::string& s, float h, float width, std::vector<std::string>& out) {
        std::string line, word;
        auto flush = [&] {
            if (word.empty()) return;
            const std::string tryLine = line.empty() ? word : line + " " + word;
            if (!line.empty() && measure(tryLine, h) > width) out.push_back(line), line = word;
            else line = tryLine;
            word.clear();
        };
        for (char ch : s) {
            if (ch == ' ') flush();
            else word += ch;
        }
        flush();
        if (!line.empty()) out.push_back(line);
    }
    float measure(const std::string& s, float h) { return ui.headless() ? float(s.size()) * h * 0.5f : ui.screenText().width(kFont, s, h); }

    void arrow(float x, float y, bool up) {
        if (pass_ != 0) return;
        const uint8_t a = 200;
        const float d = up ? -1.0f : 1.0f;
        const Vertex v[3] = {{x - 8, y - 4 * d, 0, 0, 0, 245, 194, 66, a}, {x + 8, y - 4 * d, 0, 0, 0, 245, 194, 66, a},
                             {x, y + 5 * d, 0, 0, 0, 245, 194, 66, a}};
        ui.canvas().draw(v, 3, 0, Blend::Alpha);
    }
    void text(const std::string& s, float x, float y, float h, const std::array<float, 4>& c, uint32_t align) {
        if (pass_ == 1) ui.screenText().draw(ui.canvas().renderer().materials(), kFont, s, x, y, h, c, align);
    }
    void quad(float x0, float y0, float x1, float y1, const std::array<float, 4>& c) {
        if (pass_ != 0) return;
        const uint8_t r = uint8_t(c[0] * 255), g = uint8_t(c[1] * 255), bl = uint8_t(c[2] * 255), a = uint8_t(c[3] * 255);
        const Vertex v[6] = {{x0, y0, 0, 0, 0, r, g, bl, a}, {x1, y0, 0, 0, 0, r, g, bl, a}, {x1, y1, 0, 0, 0, r, g, bl, a},
                             {x0, y0, 0, 0, 0, r, g, bl, a}, {x1, y1, 0, 0, 0, r, g, bl, a}, {x0, y1, 0, 0, 0, r, g, bl, a}};
        ui.canvas().draw(v, 6, 0, Blend::Alpha);
    }

    int focus_ = -1, pass_ = 0;
    float scroll_ = 0, scrollTarget_ = 0;
    double timeMs_ = 0;
};

}  // namespace

std::unique_ptr<ScriptObject> makeEnhancedPanel(UiManager& ui, const std::string& name) { return std::make_unique<EnhancedPanelObj>(ui, name); }

}  // namespace ui
