// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The port's Enhanced Options: gd01's Enhanced Settings list
// (gd-game-01/engine/src/ui/select_panels.cpp, EnhancedPanel) as gd02 ported
// it (gd-game-02/engine/src/ui/enhanced_panel.cpp): its sections, rows, check
// boxes, dropdowns, explanation box and mouse, in GH3's fonts and colours
// (PortDraw), over GH3's own screen dimmed (App draws it first; the guideline's
// fallback). Not in the original game (docs/guidelines/universal_launcher.md).
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
    uint32_t cheatVar = 0;   // a cheat: its global (0 locked, 1 on, 2 off) and index in guitar_hero_cheats
    int cheatIndex = -1;
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
    int index = -1;
    for (const qb::Value& c : list->arr->values) {
        ++index;
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
        const qb::Value* var = qs::Vm::param(*c.st, qbkey::key("var"));
        Item it{Kind::Cheat, key, name, var != nullptr,
                "The original cheat, on or off (unlocked if it was not). Its code, in Options > Cheats > Enter Cheat, one strum "
                "per chord (G R Y B O: green, red, yellow, blue, orange): " + code + "."};
        it.cheatVar = var ? var->key : 0, it.cheatIndex = index;
        out.push_back(std::move(it));
    }
    return out;
}

// The universal Enhanced menu (universal_mechanisms.md 1.2.2): gd03's list
// in the doc's sections and order, GH3's own rows (cut content, the debug
// menu), then GH3's own cheats.
std::vector<Item> buildItems(qs::Vm& vm) {
    std::vector<Item> k = {
        // universal_mechanisms.md 1.2.2: MODES, CONTROLS, PERFORMANCE &
        // GRAPHICS, CHEATS & ENHANCERS, in the doc's order (gd03's list, gd01's).
        // Modes are presets (enhancedPreset): one click sets every value they name.
        {Kind::Section, "modes", "MODES", true, "Presets: one click sets many options below at once."},
        {Kind::Button, "mode_steam_deck", "Steam Deck", true,
         "Sets the options to run best on a handheld PC such as the Steam Deck: full screen at 60 FPS, an 800p internal "
         "resolution and light effects (original bloom, cinematic colour, a light Star Power blur, FXAA). You can still "
         "change any option afterwards."},
        {Kind::Button, "mode_original", "Original Feel", true,
         "Looks like the original Xbox 360 game: every enhanced effect off, the original depth of field and particles, 60 "
         "FPS. The window and the resolution stay as they are. This is how the game starts."},
        {Kind::Button, "mode_clean", "Clean Image", true,
         "Removes every enhanced effect for a clean, sharp picture: full HD (1080p) internal resolution, FXAA, sharper "
         "text and sprites, 120 FPS."},
        {Kind::Button, "mode_max_hd", "Max Out (Full HD)", true,
         "Every effect at its strongest at a full HD (1080p) internal resolution, up to 240 FPS. Needs a strong GPU."},
        {Kind::Button, "mode_max_4k", "Max Out (4K)", true,
         "Every effect at its strongest at a 4K (2160p) internal resolution, up to 240 FPS. Needs a very strong GPU."},
        {Kind::Button, "mode_budget", "Rocking on a Budget", true,
         "Every effect at its lowest (even the original particles and depth of field are off) at 720p and 60 FPS, for "
         "slow computers."},
        {Kind::Button, "mode_good_old_days", "Good Old Days", true,
         "Every effect at its strongest, in sepia, at the Xbox 360's resolution (720p), 60 FPS."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "controls", "CONTROLS", true, "Gamepads, mouse, touch and your setup's delay."},
        {Kind::Button, "gamepads", "Gamepads", true,
         "The Gamepad menu: each player's instrument and device, and every device's buttons (two per control), lit as "
         "you press them. Also on the main menu's yellow fret."},
        {Kind::Check, "mouse", "Enable Mouse Selection", true,
         "Point at a menu entry to highlight it and click to choose it; a right click goes back and the mouse wheel "
         "scrolls lists. The help bar's buttons (SELECT, BACK, UP / DOWN, GAMEPADS, ENHANCED) are clickable too. The "
         "pointer hides after a few seconds, when a key or a button is pressed, and during a song. Mouse buttons are "
         "never used as game buttons."},
        {Kind::Check, "shake_whammy", "Shake for Whammy", true,
         "Shake the phone during a song to use the whammy bar: keep shaking to hold it, stop to let go. A real whammy bar "
         "keeps working. Android only (a phone's accelerometer)."},
        {Kind::Check, "touch", "Enable Touch Controls", true,
         "Play on a touch screen; also shows the on-screen buttons. In the menus a tap is a click. In a song, touch a "
         "lane at the bottom of the highway when its note arrives: it plays the fret and the strum at once (hold the "
         "finger for long notes, several fingers for chords). Two fingers above the highway deploy Star Power; the top "
         "corner pauses. Its settings are in Gamepads > Player 1 > Customize inputs."},
        {Kind::Button, "calibrate", "Audio / Video Calibration", true,
         "Measures your setup's delay so notes are judged and drawn in time. First strum (or press green) on each tick "
         "you HEAR, then on each flash you SEE, 8 times each. Back cancels."},
        {Kind::Button, "calibrate_reset", "Reset Calibration", true,
         "Removes the calibration: the audio and video delays go back to 0 ms, as before calibrating."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "graphics", "PERFORMANCE & GRAPHICS", true, "Window, frame rate, image quality and the venue's effects."},
        // engine-backend-guidelines.md 4 (GDCORE-010): the gd-engine-core
        // backends, first in the section (universal-menus.md "Backends").
        {Kind::Choice, "graphics_backend", "Graphics Backend", true,
         "The library the game draws with. local: the game's own OpenGL renderer, as always. SDL3: SDL's GPU API "
         "(Vulkan, Metal or Direct3D 12 underneath). Vulkan: Vulkan directly. "
#ifndef __APPLE__
         "OpenGL3: the shared OpenGL 3.3 / GLES 3 renderer. "
#endif
         "If the one chosen cannot start, the game uses local. Takes effect when the game starts again: Restart Now does it at once.",
         {{"local", "local"}, {"sdl3", "SDL3"}, {"vulkan", "Vulkan"},
#ifndef __APPLE__
          {"opengl3", "OpenGL3"},  // no OpenGL on macOS (engine-backend-guidelines.md rule 7)
#endif
         }},
        {Kind::Choice, "audio_backend", "Audio Backend", true,
         "The library the game plays its sound through. local: the game's own audio output, as always. "
         "miniaudio: the shared mixer for every system. OpenAL: OpenAL Soft, 3D sound. FAudio: the XAudio2 "
         "re-implementation. If the one chosen cannot start, the game uses local. Takes effect when the game "
         "starts again: Restart Now does it at once.",
         {{"local", "local"}, {"miniaudio", "miniaudio"}, {"openal", "OpenAL"}, {"faudio", "FAudio"}}},
        {Kind::Choice, "screen_mode", "Screen Mode", true,
         "Windowed; full screen at the desktop's resolution; or full screen without a border (a window covering the "
         "screen, the quickest to switch away from).",
         {{"windowed", "Windowed"}, {"fullscreen", "Full Screen"}, {"borderless", "Full Screen (No Border)"}}},
        {Kind::Choice, "internal_res", "Resolution", true,
         "The resolution the game draws the venue at before it is scaled to the window. Higher is sharper and costs "
         "more; above the window's own size it also smooths the edges. The Xbox 360 drew 1280 x 720 (x360).",
         {{"0", "Window"}, {"720", "720p (x360)"}, {"800", "800p (Steam Deck)"}, {"900", "900p"}, {"1080", "1080p"},
          {"1440", "1440p"}, {"2160", "4K (2160p)"}}},
        {Kind::Check, "sharp_ui", "Sharper Text & Sprites", true,
         "Upscales the menu art and the game's fonts with a sharper filter, so text and sprites stay crisp on large "
         "screens (from the next start)."},
        {Kind::Choice, "antialiasing", "Anti-Aliasing", true,
         "Smooths jagged edges. FXAA: a fast filter over the image. SSAA: draws the venue at twice the resolution and "
         "scales it down, the sharpest and the most costly. TAA: blends each frame with the last ones for very smooth "
         "edges, with a little softness in motion.",
         {{"none", "None"}, {"fxaa", "FXAA"}, {"ssaa", "SSAA"}, {"taa", "TAA"}}},
        {Kind::Choice, "ai_upscaling", "AI Upscaling", true,
         "Draws the 3D scene below your screen's resolution and upscales it to the screen (menus and the highway stay "
         "sharp), for higher frame rates. FSR 1: spatial, any GPU. FSR 3.1 and DLSS need AMD's and NVIDIA's own "
         "libraries, not in this version.",
         {{"none", "None"}, {"fsr1", "FSR 1 (Spatial)"}, {"fsr31", "FSR 3.1", false}, {"dlss4", "DLSS 4", false},
          {"dlss5", "DLSS 5", false}}},
        {Kind::Choice, "ai_upscaling_mode", "Upscaling Quality", true,
         "How far below the screen's resolution the scene is drawn: Quality (1.5x upscale, the sharpest), Balanced (1.7x) "
         "or Performance (2x, the fastest).",
         {{"performance", "Performance"}, {"balanced", "Balanced"}, {"quality", "Quality"}}, "ai_upscaling"},
        {Kind::Choice, "max_fps", "Max FPS", true,
         "The highest frame rate the game draws at; the original ran at 60. Unlimited draws as fast as the computer can.",
         {{"60", "60"}, {"120", "120"}, {"144", "144"}, {"240", "240"}, {"unlimited", "Unlimited"}}},
        {Kind::Check, "fps_counter", "Show FPS Counter", true,
         "Shows the frames drawn per second in the bottom-left corner of the screen."},
        {Kind::Choice, "hdr", "HDR", true,
         "High dynamic range for the venue and the band: None (as the original), Emulated (a filmic curve and an automatic "
         "exposure, on any display) or True HDR on an HDR display (not in gd04 yet).",
         {{"none", "None"}, {"emulated", "Emulated"}, {"true", "True HDR", false}}},
        {Kind::Choice, "bokeh", "Bokeh Effect", true,
         "The depth-of-field blur of the close shots. None: always sharp. Medium: as the original game. Light is softer; "
         "Strong and Stronger blur wider.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium (original)"}, {"strong", "Strong"}, {"stronger", "Stronger"}}},
        {Kind::Choice, "particles", "Enable Particles FX", true,
         "Sparks, smoke, fire and the band's Star Power effects (from the next song). None turns them off to save power "
         "(the gameplay is the same); 2x emits twice as many.",
         {{"none", "None"}, {"original", "Original"}, {"2x", "2x"}}},
        {Kind::Choice, "bloom", "Bloom", true,
         "The glow around the venue's brightest lights. Original: the original game's own bloom, as on the Xbox 360. "
         "None turns it off; 2x and 3x add a stronger glow over it.",
         {{"none", "None"}, {"original", "Original"}, {"2x", "2x"}, {"3x", "3x"}}},
        {Kind::Check, "cinematic_color", "Cinematic Color", true,
         "A filmic colour curve with a gentle grade and vignette on the venue, for a richer, more cinematic image."},
        {Kind::Choice, "color_grade", "Color Grading", true,
         "A colour look for the venue, like a film's grade (with or without Cinematic Color). Warm: golden stage light. "
         "Cold: steel blue. Vintage: faded 70s film. Noir: high-contrast black and white. Neon: punchy purples and cyans. "
         "Sepia: an old photograph.",
         {{"off", "Off"}, {"warm", "Warm"}, {"cold", "Cold"}, {"vintage", "Vintage"}, {"noir", "Noir"}, {"neon", "Neon"}, {"sepia", "Sepia"}}},
        {Kind::Choice, "film_grain", "Film Grain", true,
         "Fine animated grain over the venue, like a concert shot on film. Light is barely there; Stronger is clearly visible.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"}, {"stronger", "Stronger"}}},
        {Kind::Choice, "chromatic", "Chromatic Aberration", true,
         "A camera lens's colour fringes: red and blue edges pull apart towards the corners of the venue.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"}}},
        {Kind::Choice, "ssao", "Ambient Occlusion", true,
         "Soft contact shadows where surfaces meet: under the band's feet, in the folds of clothes, in the corners of the "
         "stage, for more depth. Costs some performance.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"}, {"stronger", "Stronger"}}},
        {Kind::Choice, "sharpening", "Sharpening", true,
         "Contrast-adaptive sharpening of the venue (AMD CAS): crisper detail without halos, good with TAA or an upscaler.",
         {{"off", "Off"}, {"low", "Low"}, {"high", "High"}}},
        {Kind::Choice, "star_blur", "Blur on Star Power", true,
         "While Star Power is on, the venue and the band blur with the camera's motion and leave light trails; it fades "
         "out when Star Power ends. The highway always stays sharp.",
         {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"strong", "Strong"}, {"stronger", "Stronger"}}},
        {Kind::Check, "shadows_all", "All Band Mates Cast Shadows", true,
         "Every band mate gets a spotlight from above and casts a shadow on the stage floor."},
        {Kind::Choice, "more_lights", "More Lights on Venues", true,
         "Richer stage lighting: brighter floor pools and a soft beam on each band mate.",
         {{"none", "None"}, {"1.5x", "1.5x"}, {"2x", "2x"}, {"2.5x", "2.5x"}}},
        {Kind::Choice, "light_dust", "Dust in Light Beams", true,
         "Fine dust drifting and twinkling in the light beams over the band (with or without More Lights).",
         {{"none", "None"}, {"1.5x", "1.5x"}, {"2x", "2x"}, {"2.5x", "2.5x"}}},
        {Kind::Check, "no_shaky_cam", "Disable Shaky Camera", true, "Steady shots: the hand-held cameras' gentle sway is turned off."},
        {Kind::Check, "no_effects", "Remove All Effects", true,
         "Turns every effect off at once, the original game's own too: its bloom, depth of field, colour transforms and "
         "flashes, every effect above and the camera's shake. The options above keep their values for when you turn "
         "this off."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "accessibility", "ACCESSIBILITY", true, "Ways to make the game easier to play on any controller."},
        {Kind::Check, "easy_chords", "Easy Chords", true,
         "A chord counts when you press any of its frets (one or more of them, none outside the chord). Off, "
         "as the original: every fret of the chord."},
        {Kind::Separator, "", "", true, ""},
        {Kind::Section, "cheats", "CHEATS & ENHANCERS", true, "Your user files, the original game's cheats and more ways to bend the rules."},
        {Kind::Button, "export", "Export User Files", true,
         "Choose a folder to save two files in: GD04-user-settings.json with every Enhanced Option, and "
         "GD04-user-progress.json with your save (bands and their progress). Use them as a backup or to move to another "
         "computer."},
        {Kind::Button, "load_settings", "Load User Settings", true,
         "Choose a GD04-user-settings.json file to load. The settings take effect at once; any setting in the file that "
         "is missing or not valid keeps its value."},
        {Kind::Button, "load_progress", "Load User Progress", true,
         "Choose a GD04-user-progress.json file to load. It replaces your save and takes effect at once; a file that is "
         "not a GH3 save is refused."},
        {Kind::Check, "no_fail", "No Fail Mode", true, "The rock meter cannot fail you: every song plays to the end."},
        {Kind::Check, "autoplay", "Autoplay", true, "The game plays every note of a song by itself, without a miss."},
        {Kind::Choice, "hopo_mode", "Forgiving Hammer-ons & Pull-offs", true,
         "Notes with a coloured top can be played without strumming. Forgiving: a note whose fret is already held when "
         "it arrives plays by itself.",
         {{"original", "Original"}, {"forgiving", "Forgiving"}}},
        {Kind::Check, "dev_debug_menu", "Developer Debug Menu", true,
         "Developer cheats: GH3's own debug switch from its development kit (enable_button_cheats), off on a retail disc. "
         "With this on, the pause menu offers GH3's DEBUG MENU (Unlock All, Repeat Last Song, Skip Into Song, Character "
         "Select, Play Credits...) and the developers' button cheats work. Some of its entries only show debug output the "
         "port does not draw."},
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
        ask_ = -1;
        scroll_ = scrollTarget_ = 0;
        keepVisible();
        scroll_ = scrollTarget_;
    }

    bool button(PortButton b) override {
        if (cal_.phase) return calibButton(b), true;
        if (ask_ >= 0) return askButton(b), true;  // back answers the pop-up (Later), not the screen
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
        if (cal_.phase) {  // the calibration takes the mouse: a click is a press
            if (click) calibButton(PortButton::Select);
            return true;
        }
        if (ask_ >= 0) return askMouse(x, y, click), true;
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
        calibPoll();
        if (!ctx_.status.empty()) {  // Export / Load User Files' outcome (App)
            status_ = ctx_.status, statusMs_ = 5000;
            ctx_.status.clear();
            rebuild();  // loaded settings change the rows
        }
    }

    void draw(PortDraw& d) override {
        d_ = &d;
        // The band's still shot behind (App's backdrop), undimmed: the box on
        // the left darkens only what is behind it (docs/guidelines/universal_launcher.md).
        if (!d.hasBackdrop()) d.quad(0, 0, kScreenW, kScreenH, {0, 0, 0, 1});
        drawAll();
        if (dropdown_ >= 0) drawDropdown();  // over everything
        if (cal_.phase) drawCalib();
        if (ask_ >= 0) drawAsk();
        d_ = nullptr;
    }

    // For walks: the centre of a setting's row, and of its open dropdown's option `k`.
    // (Walks) the row's centre, the list scrolled to it first if it is off the list.
    bool rowCenter(const std::string& key, float& x, float& y) {
        for (size_t r = 0; r < rows_.size(); ++r)
            if (item(int(r)).key == key) {
                x = (kNameX + kCtrlX1) * 0.5f, y = rowY(int(r)) + rowHeight(item(int(r))) * 0.5f;
                if (y <= kListTop || y >= kListBottom) {
                    focus_ = int(r);
                    keepVisible();
                    scroll_ = scrollTarget_;
                    y = rowY(int(r)) + rowHeight(item(int(r))) * 0.5f;
                }
                return y > kListTop && y < kListBottom;
            }
        return false;
    }
    bool optionCenter(int k, float& x, float& y) const {
        if (dropdown_ < 0) return false;
        x = (kDdPopX0 + kCtrlX1) * 0.5f, y = ddTop_ + (float(k) + 0.5f) * kDdRow;
        return true;
    }
    // The Restart Now / Later pop-up: its button b's centre (false: closed).
    bool askCenter(int b, float& x, float& y) const {
        if (ask_ < 0) return false;
        float x0, x1;
        askButtonRect(b, x0, x1);
        x = (x0 + x1) * 0.5f, y = (kAskBtnY0 + kAskBtnY1) * 0.5f;
        return true;
    }
    bool asking() const { return ask_ >= 0; }

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
    bool isOn(const Item& it) const {
        if (it.kind == Kind::Cheat) {  // GH3's own global: 1 on [C script toggle_cheat]
            const qb::Value* v = ctx_.fe.vm().global(it.cheatVar);
            return it.ready && v && v->number() == 1;
        }
        return it.ready && ctx_.store.get(it.key) == "on";
    }
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
        const std::string before = ctx_.store.get(it.key);
        store(it.key, v);
        rebuild();  // a choice may show or hide its dependent row
        ctx_.sound("select");
        // A backend other than the one running: ask to restart now or later
        // (the value is saved already: Later keeps it for the next start).
        if ((it.key == "graphics_backend" || it.key == "audio_backend") && v != before && ctx_.backendNow && v != ctx_.backendNow(it.key))
            ask_ = 0;
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
        case Kind::Cheat: {
            if (!it.ready) return ctx_.sound("error");
            // GH3's toggle_cheat (its timing windows for Easy Expert / Precision
            // Mode); a locked cheat is unlocked first (2: off) [C script menu_cheats.qb].
            qs::Vm& vm = ctx_.fe.vm();
            if (const qb::Value* v = vm.global(it.cheatVar); !v || v->number() <= 0) vm.setGlobal(it.cheatVar, qs::Vm::makeInt(2));
            qb::Struct p;
            qs::Vm::set(p, qbkey::key("cheat"), qs::Vm::makeKey(it.cheatVar));
            qs::Vm::set(p, qbkey::key("index"), qs::Vm::makeInt(it.cheatIndex));
            qs::Vm::set(p, qbkey::key("id"), qs::Vm::makeKey(qbkey::key("port_enhanced_cheat")));
            vm.run(qbkey::key("toggle_cheat"), p);
            rebuild();
            ctx_.sound("select");
            return;
        }
        case Kind::Check:
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
            if (it.key == "calibrate") return calibStart();
            if (it.key == "calibrate_reset") {  // #47 (gd01-gd03's)
                store("audio_offset", "0"), store("video_offset", "0");
                status_ = "Calibration reset: audio 0 ms, video 0 ms", statusMs_ = 5000;
                rebuild();
                return;
            }
            if ((it.key == "export" || it.key == "load_settings" || it.key == "load_progress") && ctx_.action) {
                ctx_.status.clear();
                ctx_.action(it.key);
                return;
            }
            if (it.key.rfind("mode_", 0) == 0) {  // MODES: a preset, once; every option stays editable (gd03's)
                for (const auto& [k, v] : enhancedPreset(it.key.substr(5))) store(k, v);
                status_ = it.name + " applied", statusMs_ = 5000;
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
        if (key == "calibrate_reset") return "Reset";
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
    int ask_ = -1;  // the Restart Now / Later pop-up: -1 closed, else the focused button
    bool entered_ = false;

    // -- Restart Now / Later: after a Graphics or Audio Backend change
    // (universal-menus.md "Backends"; gd01's, gd02's), in this page's look: a
    // dark box with the orange top line, GH3's fonts. Left / right (or up /
    // down) moves, select chooses, back is Later; the mouse hovers and
    // clicks. Restart Now ends the game and starts it again on the new
    // backends, back on this screen (runApp).
    static constexpr float kAskX0 = 330, kAskX1 = 950, kAskY0 = 250, kAskY1 = 470, kAskBtnY0 = 400, kAskBtnY1 = 440, kAskBtnW = 220;
    static void askButtonRect(int b, float& x0, float& x1) {
        const float cx = (kAskX0 + kAskX1) * 0.5f;
        x0 = b == 0 ? cx - 20 - kAskBtnW : cx + 20, x1 = x0 + kAskBtnW;
    }
    void askChoose(int b) {
        ask_ = -1;
        if (b == 0) {
            ctx_.sound("select");
            if (ctx_.restart) ctx_.restart();
        } else {
            ctx_.sound("back");
        }
    }
    void askButton(PortButton b) {
        if (b == PortButton::Left || b == PortButton::Right || b == PortButton::Up || b == PortButton::Down) ask_ = 1 - ask_, ctx_.sound("toggle");
        else if (b == PortButton::Select) askChoose(ask_);
        else if (b == PortButton::Back) askChoose(1);
    }
    void askMouse(float x, float y, bool click) {
        for (int b = 0; b < 2; ++b) {
            float x0, x1;
            askButtonRect(b, x0, x1);
            if (x < x0 || x > x1 || y < kAskBtnY0 || y > kAskBtnY1) continue;
            if (ask_ != b) ask_ = b, ctx_.sound("toggle");
            if (click) askChoose(b);
            return;
        }
    }
    void drawAsk() {
        PortDraw& d = *d_;
        const float o = d.offset(), cx = (kAskX0 + kAskX1) * 0.5f + o;
        d.quad(o, 0, kScreenW + o, kScreenH, {0, 0, 0, 0.6f});
        d.quad(kAskX0 + o, kAskY0, kAskX1 + o, kAskY1, {0.06f, 0.06f, 0.08f, 0.96f});
        d.frame(kAskX0 + o, kAskY0, kAskX1 + o, kAskY1, 1.5f, PortDraw::kEdge);
        d.quad(kAskX0 + o, kAskY0, kAskX1 + o, kAskY0 + 5, kAccent);
        d.text("RESTART THE GAME?", cx, kAskY0 + 50, 34, kTitle, 0x22);
        d.text("The new backend runs when the game starts again.", cx, kAskY0 + 100, 22, kNormal, 0x22);
        d.text("Restart now, or keep playing and use it next time.", cx, kAskY0 + 128, 22, kNormal, 0x22);
        for (int b = 0; b < 2; ++b) {
            float x0, x1;
            askButtonRect(b, x0, x1);
            x0 += o, x1 += o;
            const Color c = b == 0 ? Color{0.45f, 0.95f, 0.5f, 1} : Color{1, 0.86f, 0.35f, 1};  // green, yellow
            const bool h = ask_ == b;
            d.quad(x0, kAskBtnY0, x1, kAskBtnY1, {c[0], c[1], c[2], h ? 0.4f : 0.2f});
            d.frame(x0, kAskBtnY0, x1, kAskBtnY1, h ? 3.0f : 1.5f, c);
            d.text(b == 0 ? "Restart Now" : "Later", (x0 + x1) * 0.5f, (kAskBtnY0 + kAskBtnY1) * 0.5f + 1, 22, h ? kFocused : kNormal, 0x22);
        }
    }
    float scroll_ = 0, scrollTarget_ = 0, ddTop_ = 0;

    // -- Audio / Video Calibration (gd02's; the original had none). A beat
    // every 750 ms: the audio step plays a tick, the video step flashes a
    // ring silently. After 3 lead-in beats, each press is matched to the
    // nearest beat; the median of 8 is the step's offset. audio_offset is
    // added to GH3's lag_calibration (the judging), video_offset draws the
    // highway ahead (app.cpp, song_screen.cpp).
    static constexpr uint64_t kBeatNs = 750000000ull;
    static constexpr int kLeadIn = 3, kSamples = 8;
    struct Calib {
        int phase = 0;  // 0 off, 1 audio, 2 video, 3 done
        uint64_t nextTick = 0, flashNs = 0;
        std::vector<uint64_t> ticks;
        std::vector<float> samples;
        float audio = 0, video = 0;
    };
    Calib cal_;
    void calibStart() {
        cal_ = Calib{};
        cal_.phase = 1;
        cal_.nextTick = SDL_GetTicksNS() + 1000000000ull;
    }
    void calibPoll() {
        if (cal_.phase != 1 && cal_.phase != 2) return;
        const uint64_t now = SDL_GetTicksNS();
        if (now < cal_.nextTick) return;
        if (cal_.phase == 1) ctx_.sound("toggle");
        else cal_.flashNs = now;
        cal_.ticks.push_back(now);
        cal_.nextTick += kBeatNs;
        if (cal_.nextTick < now) cal_.nextTick = now + kBeatNs;  // a stall: no burst of ticks
    }
    void calibButton(PortButton b) {
        if (b == PortButton::Back) {  // cancel (or close the result)
            cal_.phase = 0;
            ctx_.sound("back");
            return;
        }
        if (cal_.phase == 3) {
            if (b == PortButton::Select) cal_.phase = 0, ctx_.sound("select"), rebuild();
            return;
        }
        if (b != PortButton::Select && b != PortButton::Up && b != PortButton::Down) return;  // green, Enter or the strum bar
        if (int(cal_.ticks.size()) < kLeadIn) return;
        const uint64_t now = SDL_GetTicksNS();
        float best = 1e9f;
        for (uint64_t t : cal_.ticks)
            if (std::fabs(float(int64_t(now - t)) / 1e6f) < std::fabs(best)) best = float(int64_t(now - t)) / 1e6f;
        if (const float dn = float(int64_t(now - cal_.nextTick)) / 1e6f; std::fabs(dn) < std::fabs(best)) best = dn;  // early for the next
        if (std::fabs(best) > float(kBeatNs) / 1e6f * 0.45f) return;
        cal_.samples.push_back(best);
        if (int(cal_.samples.size()) < kSamples) return;
        std::vector<float> v = cal_.samples;
        std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
        const float median = std::clamp(v[v.size() / 2], -300.0f, 500.0f);
        if (cal_.phase == 1) {
            cal_.audio = median;
            cal_.phase = 2;
            cal_.ticks.clear(), cal_.samples.clear();
            cal_.nextTick = now + 1500000000ull;
        } else {
            cal_.video = median;
            cal_.phase = 3;
            store("audio_offset", std::to_string(std::lround(cal_.audio)));
            store("video_offset", std::to_string(std::lround(cal_.video)));
        }
    }
    static std::string signedMs(long ms) { return (ms > 0 ? "+" : "") + std::to_string(ms) + " ms"; }
    void drawCalib() {
        PortDraw& d = *d_;
        const float o = d.offset(), cx = kScreenW * 0.5f + o, cy = 360;
        d.quad(o, 0, kScreenW + o, kScreenH, {0, 0, 0, 0.88f});
        d.text("CALIBRATION", cx, 110, 56, kTitle, 0x22, true);
        if (cal_.phase == 3) {
            d.text("Audio " + signedMs(std::lround(cal_.audio)) + "   Video " + signedMs(std::lround(cal_.video)), cx, cy, 34, PortDraw::kOn, 0x22);
            d.text("Saved. Press green to close.", cx, cy + 60, 22, kNormal, 0x22);
            return;
        }
        const bool audio = cal_.phase == 1;
        d.text(audio ? "Step 1 of 2: AUDIO" : "Step 2 of 2: VIDEO", cx, 200, 30, kAccent, 0x22);
        d.text(audio ? "Strum or press green on each tick you HEAR" : "Strum or press green on each flash you SEE", cx, 245, 24, kNormal, 0x22);
        if (!audio) {  // the ring, lit for 120 ms on each beat
            const float lit = cal_.flashNs ? std::max(0.0f, 1 - float(SDL_GetTicksNS() - cal_.flashNs) / 120e6f) : 0;
            for (int i = 0; i < 48; ++i) {
                const float a0 = float(i) / 48 * 6.2831853f, a1 = float(i + 1) / 48 * 6.2831853f;
                d.line(cx + 70 * std::cos(a0), cy + 70 * std::sin(a0), cx + 70 * std::cos(a1), cy + 70 * std::sin(a1), 6,
                       {0.4f + 0.6f * lit, 0.4f + 0.46f * lit, 0.45f - 0.25f * lit, 1});
                if (lit > 0)
                    d.triangle(cx, cy, cx + 62 * std::cos(a0), cy + 62 * std::sin(a0), cx + 62 * std::cos(a1), cy + 62 * std::sin(a1),
                               {1, 0.86f, 0.2f, lit});
            }
        }
        for (int k = 0; k < kSamples; ++k) {  // one dot per press taken
            const float x = cx + (float(k) - (kSamples - 1) * 0.5f) * 34;
            d.quad(x - 9, cy + 130, x + 9, cy + 148, k < int(cal_.samples.size()) ? PortDraw::kOn : Color{0.3f, 0.3f, 0.34f, 1});
        }
        if (int(cal_.ticks.size()) < kLeadIn) d.text("Get ready...", cx, cy + 190, 22, kDim, 0x22);
        d.text("Back cancels", cx, cy + 230, 18, kDim, 0x22);
    }
};

}  // namespace

std::unique_ptr<PortScreen> makeEnhancedScreen(PortContext& ctx) { return std::make_unique<EnhancedScreen>(ctx); }

// For walks: where a setting's row is, and an open dropdown's option.
bool enhancedRowCenter(PortScreen& s, const std::string& key, float& x, float& y) {
    return static_cast<EnhancedScreen&>(s).rowCenter(key, x, y);
}
bool enhancedOptionCenter(PortScreen& s, int k, float& x, float& y) { return static_cast<EnhancedScreen&>(s).optionCenter(k, x, y); }
bool enhancedAskCenter(PortScreen& s, int b, float& x, float& y) { return static_cast<EnhancedScreen&>(s).askCenter(b, x, y); }
bool enhancedAsking(PortScreen& s) { return static_cast<EnhancedScreen&>(s).asking(); }
