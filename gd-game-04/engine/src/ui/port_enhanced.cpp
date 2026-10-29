// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The port's Enhanced Options: gd01's Enhanced Settings list
// (gd-game-01/engine/src/ui/select_panels.cpp, EnhancedPanel) as gd02 ported
// it (gd-game-02/engine/src/ui/enhanced_panel.cpp): its sections, rows, check
// boxes, dropdowns, explanation box and mouse, in GH3's fonts and colours
// (PortDraw), on a black page (gd04 draws no venue yet; the guideline's
// fallback). Not in the original game (guidelines/universal_launcher.md).
// Rows gd04 does not have yet stay in place, greyed with the warning sign.
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "data/qbkey.h"
#include "render/canvas.h"
#include "script/vm.h"
#include "ui/front_end.h"
#include "ui/port_screens.h"

namespace {

enum class Kind { Section, Separator, Check, Choice, Button, Cheat };
struct Option {
    std::string value, label;
    bool ok = true;  // false: not in gd04 (shown, cannot be chosen)
};
struct Item {
    Kind kind;
    std::string key;   // the setting (enhanced.json), the button's action, or the cheat
    std::string name;
    bool ready;        // false: not in gd04 yet (greyed, cannot be chosen)
    std::string help;
    std::vector<Option> options = {};
    std::string needs = {};  // shown only while this setting is on (a check) or not none / off (a choice)
};

// GH3's cheats [C data: guitar_hero_cheats, scripts/guitar/menu/
// menu_cheats.qb]: name_text and unlock_pattern, one strummed chord per
// entry, a bit per fret: 0x10000 green, 0x1000 red, 0x100 yellow, 0x10 blue,
// 1 orange [I: Hyperspeed's pattern 1 16 1 256 ... is its published code
// Orange Blue Orange Yellow ...].
std::vector<Item> gh3Cheats(qs::Vm& vm) {
    std::vector<Item> out;
    const qb::Value* list = vm.global(qbkey::key("guitar_hero_cheats"));
    if (!list || !list->arr) return out;
    for (const qb::Value& c : list->arr->values) {
        if (!c.st) continue;
        const qb::Value* nt = qs::Vm::param(*c.st, qbkey::key("name_text"));
        const qb::Value* pat = qs::Vm::param(*c.st, qbkey::key("unlock_pattern"));
        if (!nt || !pat || !pat->arr) continue;
        std::string name = nt->str, key = "cheat_";
        bool up = true;
        for (char& ch : name) {
            key += ch == ' ' ? '_' : ch;
            if (up) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            up = ch == ' ';
        }
        std::string code;
        for (const qb::Value& v : pat->arr->values) {
            const int bits = int(v.number());
            std::string chord;
            for (const auto& [bit, letter] : {std::pair<int, char>{0x10000, 'G'}, {0x1000, 'R'}, {0x100, 'Y'}, {0x10, 'B'}, {1, 'O'}})
                if (bits & bit) chord += letter;
            code += (code.empty() ? "" : " ") + chord;
        }
        out.push_back({Kind::Cheat, key, name, false,
                       "The original cheat. Entered in Options > Cheats > Enter Cheat, one strum per chord (G R Y B O: green, "
                       "red, yellow, blue, orange): " + code + "."});
    }
    return out;
}

// gd01's list, in its sections and order, with its explanations (gd02's
// wording); gd02's Gamepads button where the guideline puts it; then GH3's
// own cheats.
std::vector<Item> buildItems(qs::Vm& vm) {
    std::vector<Item> k = {
        {Kind::Section, "deck", "STEAM DECK MODE", true, "Settings for handheld PCs such as the Steam Deck."},
        {Kind::Button, "steam_deck", "Apply Steam Deck Settings", true,
         "Sets every option to what runs best on a handheld PC such as the Steam Deck: full screen at 60 FPS and lighter "
         "effects. You can still change any option afterwards."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "controls", "CONTROLS & GAMEPAD", true, "Gamepads, mouse, touch and guitars."},
        {Kind::Button, "gamepads", "Gamepads", true,
         "The connected guitars, gamepads and the keyboard: which player each one is, its buttons lit as you press them, "
         "and new buttons for any of them."},
        {Kind::Check, "mouse", "Enable Mouse Selection", true,
         "Point at a menu entry to highlight it and click to choose it; a right click goes back and the mouse wheel "
         "moves up and down. The help bar's items are clickable too. Hovering works only while the game window has "
         "focus; the pointer hides after a few seconds, when a key or a button is pressed, and during a song."},
        {Kind::Check, "touch", "Enable Touch Controls", false,
         "Play on a touch screen. In the menus a tap is a click. In a song, touch a lane at the bottom of the highway when "
         "its note arrives: it plays the fret and the strum at once."},
        {Kind::Check, "rockband", "Enable Rock Band Guitars", true,
         "Plays with the USB Rock Band guitars for Xbox 360 and PlayStation 3 / 4: the five frets (the solo frets too), "
         "the strum bar, the whammy bar, and tilt or Select for Star Power."},
        {Kind::Check, "xinput", "Enable XInput Controllers", true,
         "Any XInput gamepad plays: the shoulder buttons and the face buttons are the frets, and pressing a button plays "
         "the note and the strum at once. Off, gamepads only work the menus."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "graphics", "PERFORMANCE & GRAPHICS", true, "Timing, window, frame rate and image quality."},
        {Kind::Button, "calibrate", "Audio / Video Calibration", false,
         "Measures your setup's delay so notes are judged and drawn in time: strum on each tick you hear, then on each "
         "flash you see."},
        {Kind::Choice, "screen_mode", "Screen Mode", true,
         "Windowed; full screen at the desktop's resolution; or full screen without a border (a window covering the "
         "screen, the quickest to switch away from).",
         {{"windowed", "Windowed"}, {"fullscreen", "Full Screen"}, {"borderless", "Full Screen (No Border)"}}},
        {Kind::Choice, "max_fps", "In-Game Max FPS", true,
         "The highest frame rate the game draws at; the original ran at 60. The game never draws faster than this, "
         "whatever the display's refresh rate.",
         {{"60", "60"}, {"120", "120"}, {"240", "240"}}},
        {Kind::Check, "fps_counter", "Show FPS Counter", true, "Shows the frames drawn per second in the bottom-left corner of the screen."},
        {Kind::Choice, "internal_res", "Internal Resolution", false,
         "The resolution the game draws at before it is scaled to the window. The Xbox 360 drew 1280 x 720 (x360).",
         {{"720", "720p (x360)"}, {"800", "800p (Steam Deck)"}, {"900", "900p"}, {"1080", "1080p"}, {"1440", "1440p"},
          {"2160", "4K (2160p)"}}},
        {Kind::Check, "sharp_ui", "Sharper Text & Sprites", false,
         "Upscales the menu art and the game's fonts with a sharper filter, so text and sprites stay crisp on large screens."},
        {Kind::Choice, "antialiasing", "Anti-Aliasing", false, "Smooths jagged edges: FXAA, SSAA or TAA.",
         {{"none", "None"}, {"fxaa", "FXAA"}, {"ssaa", "SSAA"}, {"taa", "TAA"}}},
        {Kind::Choice, "ai_upscaling", "AI Upscaling", false,
         "Draws the 3D scene below your screen's resolution and upscales it to the screen, for higher frame rates.",
         {{"none", "None"}, {"fsr1", "FSR 1 (Spatial)", false}, {"fsr31", "FSR 3.1", false}, {"dlss4", "DLSS 4", false},
          {"dlss5", "DLSS 5", false}}},
        {Kind::Choice, "ai_upscaling_mode", "Upscaling Quality", false,
         "How far below the screen's resolution the scene is drawn: Quality (1.5x), Balanced (1.7x) or Performance (2x).",
         {{"performance", "Performance"}, {"balanced", "Balanced"}, {"quality", "Quality"}}, "ai_upscaling"},
        {Kind::Choice, "bokeh", "Bokeh Effect", false,
         "The depth-of-field blur of the close shots. None: always sharp. Regular: as the original game. Stronger: a wider blur.",
         {{"none", "None"}, {"regular", "Regular"}, {"stronger", "Stronger"}}},
        {Kind::Check, "particles", "Enable Particles FX", false, "Sparks, smoke, fire and the highway's hit effects."},
        {Kind::Choice, "bloom", "Bloom", false, "A soft glow around the brightest lights of the venue, over the original game's own.",
         {{"off", "Off"}, {"low", "Low"}, {"medium", "Medium"}, {"high", "High"}}},
        {Kind::Check, "cinematic_color", "Cinematic Color", false, "A filmic colour curve with a gentle grade and vignette on the venue."},
        {Kind::Choice, "color_grade", "Color Grading", false, "A colour look for the venue: Warm, Cold, Vintage, Noir or Neon.",
         {{"off", "Off"}, {"warm", "Warm"}, {"cold", "Cold"}, {"vintage", "Vintage"}, {"noir", "Noir"}, {"neon", "Neon"}}},
        {Kind::Choice, "film_grain", "Film Grain", false, "Fine animated grain over the venue, like a concert shot on film.",
         {{"off", "Off"}, {"light", "Light"}, {"strong", "Strong"}}},
        {Kind::Check, "chromatic", "Chromatic Aberration", false, "A camera lens's colour fringes towards the corners of the venue."},
        {Kind::Check, "light_streaks", "Anamorphic Light Streaks", false, "The brightest stage lights flare into long horizontal streaks."},
        {Kind::Check, "ssao", "Ambient Occlusion", false, "Soft contact shadows where surfaces meet, for more depth."},
        {Kind::Choice, "sharpening", "Sharpening", false, "Contrast-adaptive sharpening of the venue (AMD CAS).",
         {{"off", "Off"}, {"low", "Low"}, {"high", "High"}}},
        {Kind::Choice, "star_blur", "Blur on Star Power", false,
         "While Star Power is on, the venue and the band blur with the camera's motion and leave light trails.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"high", "High"}}},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "enhancers", "ENHANCERS", true, "Visual extras beyond the original game, and your user files."},
        {Kind::Check, "shadows_all", "All Band Mates Cast Shadows", false,
         "Every band mate gets a spotlight from above and casts a shadow on the stage floor."},
        {Kind::Choice, "more_lights", "More Lights on Venues", false,
         "Richer stage lighting: brighter light cones and floor pools and a fill light on the band.",
         {{"off", "Off"}, {"subtle", "Subtle"}, {"strong", "Strong"}}},
        {Kind::Check, "light_dust", "Dust in Light Beams", false, "With More Lights on Venues: fine dust drifting in the light beams.", {},
         "more_lights"},
        {Kind::Choice, "ground_fog", "Add Fog to the Ground (Experimental)", false, "A low fog over the stage floor where the band plays.",
         {{"none", "None"}, {"low", "Low"}, {"full", "Full"}, {"super", "Super Fog"}}},
        {Kind::Check, "no_shaky_cam", "Disable Shaky Camera", false, "Steady shots instead of the original game's handheld camera shake."},
        {Kind::Button, "export", "Export User Files", false,
         "Choose a folder to save your Enhanced Options and your progress in, as a backup or to move to another computer."},
        {Kind::Button, "load_settings", "Load User Settings", false, "Choose a user settings file to load."},
        {Kind::Button, "load_progress", "Load User Progress", false, "Choose a user progress file to load."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "cheats", "CHEATS", true, "The original game's cheats and more ways to bend the rules."},
        {Kind::Check, "cut_content", "Enable Cut Content", false, "Loads content left unused on the original disc into the game."},
        {Kind::Check, "no_fail", "No Fail Mode", true, "The rock meter cannot fail you: every song plays to the end."},
        {Kind::Check, "autoplay", "Autoplay", true, "The game plays every note of a song by itself, without a miss."},
        {Kind::Choice, "hopo_mode", "Forgiving Hammer-ons & Pull-offs", false,
         "Notes with a coloured top can be played without strumming. Forgiving: a wrong fret can be corrected, and a "
         "note whose fret is already held when it arrives plays by itself.",
         {{"original", "Original"}, {"forgiving", "Forgiving"}}},
    };
    for (Item& c : gh3Cheats(vm)) k.push_back(std::move(c));
    return k;
}

class EnhancedScreen : public PortScreen {
public:
    explicit EnhancedScreen(PortContext& ctx) : ctx_(ctx), items_(buildItems(ctx.fe.vm())) {}

    void enter() override {
        rebuild();
        // The first setting gd04 has (Gamepads), as the screen first opens.
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
    }

    bool button(PortButton b) override {
        if (rows_.empty()) return false;
        const Item& it = item(focus_);
        if (dropdown_ >= 0) {  // the open dropdown takes the input
            const int n = int(it.options.size());
            if (b == PortButton::Up || b == PortButton::Down) {
                ddFocus_ = (ddFocus_ + (b == PortButton::Down ? 1 : n - 1)) % n;
                ctx_.sound("toggle");
            } else if (b == PortButton::Select) {
                if (!it.options[size_t(ddFocus_)].ok) return ctx_.sound("error"), true;
                dropdown_ = -1;
                setValue(it, it.options[size_t(ddFocus_)].value);
            } else if (b == PortButton::Back) {
                dropdown_ = -1;
                ctx_.sound("back");
            }
            return true;
        }
        if (b == PortButton::Up || b == PortButton::Down) {
            moveFocus(b == PortButton::Down ? 1 : -1);
            return true;
        }
        if (b == PortButton::Left || b == PortButton::Right) {
            const int dir = b == PortButton::Right ? 1 : -1;
            if (it.kind == Kind::Choice) {
                if (!it.ready) return ctx_.sound("error"), true;
                const int n = int(it.options.size());
                int i = optionIndex(it);
                for (int k = 0; k < n; ++k) {  // the next option gd04 has
                    i = (i + dir + n) % n;
                    if (it.options[size_t(i)].ok) break;
                }
                setValue(it, it.options[size_t(i)].value);
            } else if (it.kind == Kind::Check || it.kind == Kind::Cheat) {
                if (isOn(it) != (dir > 0)) activate();
            }
            return true;
        }
        if (b == PortButton::Select) return activate(), true;
        return false;  // back: the screens' stack
    }

    bool mouse(float x, float y, bool click) override {
        if (rows_.empty()) return false;
        // The open dropdown: its options, or a click anywhere else closes it.
        if (dropdown_ >= 0) {
            const Item& it = item(focus_);
            for (size_t k = 0; k < it.options.size(); ++k) {
                const float oy = ddTop_ + float(k) * kDdRow;
                if (x < kDdPopX0 || x > kCtrlX1 || y < oy || y > oy + kDdRow) continue;
                if (int(k) != ddFocus_) ddFocus_ = int(k), ctx_.sound("toggle");
                if (click && !it.options[k].ok) ctx_.sound("error");
                else if (click) dropdown_ = -1, setValue(it, it.options[k].value);
                return true;
            }
            if (click) dropdown_ = -1, ctx_.sound("back");
            return true;
        }
        for (size_t r = 0; r < rows_.size(); ++r) {
            const float top = rowY(int(r)), h = rowHeight(item(int(r)));
            if (item(int(r)).kind == Kind::Separator || top < kListTop - 1 || top + h > kListBottom + 1) continue;
            if (x < kListX0 - 6 || x > kCtrlX1 + 12 || y < top || y > top + h) continue;
            // Hovering focuses (without scrolling the list) and shows the explanation.
            if (int(r) != focus_) focus_ = int(r), ctx_.sound("toggle");
            if (click) activate();
            return true;
        }
        return false;
    }

    void update(double dtMs) override {
        // The list eases toward its scroll target.
        const float k = 1 - std::exp(-std::max(float(dtMs), 0.0f) / 70.0f);
        scroll_ += (scrollTarget_ - scroll_) * k;
        if (std::fabs(scrollTarget_ - scroll_) < 0.3f) scroll_ = scrollTarget_;
        statusMs_ = std::max(0.0f, statusMs_ - float(dtMs));
    }

    void draw(PortDraw& d) override {
        d_ = &d;
        d.quad(0, 0, kScreenW, kScreenH, {0, 0, 0, 1});  // a black page: no venue renderer yet (the guideline's fallback)
        drawAll();
        if (dropdown_ >= 0) drawDropdown();  // over everything
        d_ = nullptr;
    }

    // For walks: the centre of a setting's row, and of its open dropdown's option `k`.
    bool rowCenter(const std::string& key, float& x, float& y) const {
        for (size_t r = 0; r < rows_.size(); ++r)
            if (item(int(r)).key == key) {
                x = (kNameX + kCtrlX1) * 0.5f, y = rowY(int(r)) + rowHeight(item(int(r))) * 0.5f;
                return y > kListTop && y < kListBottom;
            }
        return false;
    }
    bool optionCenter(int k, float& x, float& y) const {
        if (dropdown_ < 0) return false;
        x = (kDdPopX0 + kCtrlX1) * 0.5f, y = ddTop_ + (float(k) + 0.5f) * kDdRow;
        return true;
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
        for (size_t i = 0; i < items_.size(); ++i) {
            const Item& it = items_[i];
            if (it.kind == Kind::Section) hidden = collapsed(it.key);
            if (it.kind == Kind::Separator) hidden = false;
            if (it.kind != Kind::Section && it.kind != Kind::Separator && hidden) continue;
            if (!it.needs.empty() && !settingOn(it.needs)) continue;
            rows_.push_back(int(i));
        }
        focus_ = 0;
        for (size_t r = 0; r < rows_.size(); ++r)
            if (rows_[r] == keep) focus_ = int(r);
        scrollTarget_ = std::clamp(scrollTarget_, 0.0f, maxScroll());
    }
    const Item& item(int row) const { return items_[size_t(rows_[size_t(row)])]; }
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
        // A section's first row keeps its heading in view.
        if (dir < 0 && focus_ > 0 && item(focus_ - 1).kind == Kind::Section) {
            const int f = focus_;
            focus_ = f - 1, keepVisible(), focus_ = f;
        }
        keepVisible();
        ctx_.sound("toggle");
    }

    // -- values
    bool settingOn(const std::string& key) const {
        const std::string v = ctx_.store.get(key);
        return !v.empty() && v != "none" && v != "off";
    }
    bool isOn(const Item& it) const { return it.ready && ctx_.store.get(it.key) == "on"; }
    // A choice of exactly two values is shown and used as a check box (gd01).
    static bool isToggle(const Item& it) { return it.kind == Kind::Choice && it.options.size() == 2; }
    int optionIndex(const Item& it) const {
        const std::string v = ctx_.store.get(it.key);
        for (size_t k = 0; k < it.options.size(); ++k)
            if (v == it.options[k].value) return int(k);
        return 0;
    }
    void store(const std::string& key, const std::string& v) {
        ctx_.store.set(key, v);
        if (ctx_.changed) ctx_.changed(key);
    }
    void setValue(const Item& it, const std::string& v) {
        if (!it.ready) return ctx_.sound("error");
        for (const Option& o : it.options)
            if (v == o.value && !o.ok) return ctx_.sound("error");
        store(it.key, v);
        rebuild();  // a choice may show or hide its dependent row
        ctx_.sound("select");
    }
    void activate() {
        const Item& it = item(focus_);
        switch (it.kind) {
        case Kind::Section:
            collapsed_[it.key] = !collapsed(it.key);
            rebuild();
            ctx_.sound("select");
            return;
        case Kind::Separator: return;
        case Kind::Check:
        case Kind::Cheat:
            if (!it.ready) return ctx_.sound("error");
            store(it.key, isOn(it) ? "off" : "on");
            rebuild();  // a check may show or hide its dependent row
            ctx_.sound("select");
            return;
        case Kind::Choice:
            if (!it.ready) return ctx_.sound("error");
            if (isToggle(it)) return setValue(it, it.options[optionIndex(it) == 1 ? 0 : 1].value);
            dropdown_ = focus_;
            ddFocus_ = optionIndex(it);
            ctx_.sound("select");
            return;
        case Kind::Button:
            if (!it.ready) return ctx_.sound("error");
            ctx_.sound("select");
            if (it.key == "gamepads") return ctx_.screens.open(PortScreens::Id::Gamepads);
            if (it.key == "steam_deck") {  // the handheld column, once; nothing is locked (gd01)
                for (const auto& [k, v] : enhancedProfile(true)) store(k, v);
                status_ = "Steam Deck settings applied", statusMs_ = 5000;
            }
            rebuild();
            return;
        }
    }

    // -- drawing
    void drawAll() {
        PortDraw& d = *d_;
        const float o = d.offset();
        d.quad(kBoxX0 + o, kBoxY0, kBoxX1 + o, kBoxY1, PortDraw::kBox);
        d.frame(kBoxX0 + o, kBoxY0, kBoxX1 + o, kBoxY1, 1.5f, PortDraw::kEdge);
        d.quad(kBoxX0 + o, kBoxY0, kBoxX1 + o, kBoxY0 + 4, kTitle);
        d.text("ENHANCED OPTIONS", 90 + o, 44, 62, kTitle, 0x11, true);
        d.text("Features of this PC version, beyond the original game", 92 + o, 112, 20, kDim, 0x11);
        if (statusMs_ > 0) {
            Color c = PortDraw::kOn;
            c[3] = std::min(1.0f, statusMs_ / 500);
            d.text(d.fit(status_, 18, kBoxX1 - kBoxX0 - 60), 92 + o, 138, 18, c, 0x11);
        }
        for (size_t r = 0; r < rows_.size(); ++r) drawRow(int(r));
        // More above / below.
        if (scroll_ > 1) d.arrow(kListX0 + 270 + o, kListTop - 10, true, kDim);
        if (contentHeight() - scroll_ > kListBottom - kListTop + 1) d.arrow(kListX0 + 270 + o, kListBottom + 8, false, kDim);
        drawHelp();
    }

    void drawRow(int r) {
        PortDraw& d = *d_;
        const Item& it = item(r);
        const float h = rowHeight(it), top = rowY(r), cy = top + h * 0.5f, o = d.offset();
        if (top < kListTop - 1 || top + h > kListBottom + 1) return;
        const bool f = r == focus_;
        if (it.kind == Kind::Separator) {
            d.quad(kListX0 + o, cy - 1, kCtrlX1 + 8 + o, cy + 1, {0.3f, 0.3f, 0.34f, 1});
            return;
        }
        if (f) {
            d.quad(kListX0 - 6 + o, top + 2, kCtrlX1 + 12 + o, top + h - 2, PortDraw::kFocusBar);
            d.quad(kListX0 - 6 + o, top + 2, kListX0 - 2 + o, top + h - 2, it.ready ? kGold : kDim);
        }
        if (it.kind == Kind::Section) {
            const float ax = kListX0 + 8 + o;
            const Color c = f ? kFocused : kAccent;
            if (collapsed(it.key)) d.triangle(ax - 4, cy - 8, ax - 4, cy + 8, ax + 8, cy, c);
            else d.triangle(ax - 7, cy - 5, ax + 7, cy - 5, ax, cy + 7, c);
            d.text(it.name, kListX0 + 26 + o, cy + 1, 27, c, 0x21, true);
            return;
        }
        const bool locked = !it.ready;
        const Color nameC = f ? kFocused : locked ? kDim : kNormal;
        // A long name next to a dropdown is drawn smaller to fit before it.
        float nameH = 23;
        const float room = (it.kind == Kind::Choice && !isToggle(it) ? kDdX0 : kCtrlX1 - (it.kind == Kind::Button ? 130 : 40)) - kNameX - 14;
        if (const float w = d.width(it.name, 23); w > room - (locked ? 30 : 0)) nameH = std::max(15.0f, 23 * (room - (locked ? 30 : 0)) / w);
        d.text(it.name, kNameX + o, cy + 1, nameH, nameC, 0x21);
        if (locked) d.warning(kNameX + o + d.width(it.name, nameH) + 10, cy);
        const Color ctrl = locked ? kDim : f ? kFocused : kNormal;
        const Kind shown = isToggle(it) || it.kind == Kind::Cheat ? Kind::Check : it.kind;
        switch (shown) {
        case Kind::Check:
            d.checkBox(kCtrlX1 + o, cy, isToggle(it) ? optionIndex(it) == 1 && !locked : isOn(it), ctrl, locked ? kDim : kGold);
            break;
        case Kind::Choice: {
            const float x1 = kCtrlX1 + o, x0 = kDdX0 + o;
            d.frame(x0, cy - 13, x1, cy + 13, 2, locked ? kDim : Color{0.5f, 0.5f, 0.55f, 1});
            const std::string label = it.options.empty() ? "" : it.options[size_t(optionIndex(it))].label;
            d.text(d.fit(label, 20, x1 - x0 - 40), x0 + 10, cy + 1, 20, ctrl, 0x21);
            d.triangle(x1 - 22, cy - 4, x1 - 8, cy - 4, x1 - 15, cy + 5, ctrl);
            break;
        }
        case Kind::Button: {
            const float x1 = kCtrlX1 + o, x0 = x1 - 120;
            d.frame(x0, cy - 13, x1, cy + 13, 2, ctrl);
            d.text(buttonLabel(it.key), (x0 + x1) * 0.5f, cy + 1, 20, ctrl, 0x22);
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
        PortDraw& d = *d_;
        const Item& it = item(dropdown_);
        const float rowTop = rowY(dropdown_), h = rowHeight(it), n = float(it.options.size());
        ddTop_ = rowTop + h - 2;
        if (ddTop_ + n * kDdRow > kScreenH - 70) ddTop_ = rowTop + 2 - n * kDdRow;  // no room below: open upward
        const float o = d.offset(), x0 = kDdPopX0 + o, x1 = kCtrlX1 + o;
        d.quad(x0, ddTop_, x1, ddTop_ + n * kDdRow, {0.08f, 0.07f, 0.06f, 0.97f});
        d.frame(x0, ddTop_, x1, ddTop_ + n * kDdRow, 2, kAccent);
        const int cur = optionIndex(it);
        for (size_t k = 0; k < it.options.size(); ++k) {
            const float oy = ddTop_ + float(k) * kDdRow;
            const bool hot = int(k) == ddFocus_;
            if (hot) d.quad(x0 + 2, oy + 2, x1 - 2, oy + kDdRow - 2, PortDraw::kFocusBar);
            const bool ok = it.options[k].ok;
            d.text(ok ? it.options[k].label : it.options[k].label + " (not available yet)", x0 + 12, oy + kDdRow * 0.5f + 1, 20,
                   !ok ? kDim : hot ? kFocused : kNormal, 0x21);
            if (int(k) == cur) d.line(x1 - 26, oy + 15, x1 - 21, oy + 20, 3, kGold), d.line(x1 - 21, oy + 20, x1 - 12, oy + 9, 3, kGold);
        }
    }

    // The focused option's explanation, in its box on the right.
    void drawHelp() {
        PortDraw& d = *d_;
        const Item& it = item(focus_);
        if (it.kind == Kind::Separator) return;
        const float o = d.offset(), x0 = kPopX0 + o, x1 = kPopX1 + o, pad = 24, bodyH = 22, lineH = 30;
        const std::vector<std::string> lines = d.wrap(it.help, x1 - x0 - 2 * pad, bodyH);
        const bool notYet = !it.ready && it.kind != Kind::Section;
        const float height = pad + 46 + float(lines.size()) * lineH + (notYet ? 42 : 0) + pad;
        const float top = kBoxY1 - height;  // bottom-aligned with the options box
        float y = top + pad;
        d.quad(x0, top, x1, kBoxY1, {0, 0, 0, 0.72f});
        d.frame(x0, top, x1, kBoxY1, 1.5f, PortDraw::kEdge);
        d.quad(x0, top, x1, top + 5, kAccent);
        float th = 30;
        if (const float w = d.width(it.name, th), room = x1 - x0 - 2 * pad; w > room) th = std::max(18.0f, th * room / w);
        d.text(it.name, x0 + pad, y + 2 + (30 - th) * 0.5f, th, kGold, 0x11);
        y += 46;
        for (const std::string& l : lines) d.text(l, x0 + pad, y, bodyH, kNormal, 0x11), y += lineH;
        if (notYet) {
            y += 6;
            d.warning(x0 + pad, y + 12);
            d.text("Not available yet", x0 + pad + 30, y + 13, 21, kWarn, 0x21);
        }
    }

    PortContext& ctx_;
    std::vector<Item> items_;
    PortDraw* d_ = nullptr;
    std::string status_;
    float statusMs_ = 0;
    std::vector<int> rows_;  // indexes into items_ of the rows shown
    std::map<std::string, bool> collapsed_;
    int focus_ = 0, dropdown_ = -1, ddFocus_ = 0;
    bool entered_ = false;
    float scroll_ = 0, scrollTarget_ = 0, ddTop_ = 0;
};

}  // namespace

std::unique_ptr<PortScreen> makeEnhancedScreen(PortContext& ctx) { return std::make_unique<EnhancedScreen>(ctx); }

// For walks: where a setting's row is, and an open dropdown's option.
bool enhancedRowCenter(PortScreen& s, const std::string& key, float& x, float& y) {
    return static_cast<EnhancedScreen&>(s).rowCenter(key, x, y);
}
bool enhancedOptionCenter(PortScreen& s, int k, float& x, float& y) { return static_cast<EnhancedScreen&>(s).optionCenter(k, x, y); }
