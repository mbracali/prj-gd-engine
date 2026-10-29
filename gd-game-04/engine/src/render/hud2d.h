// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "render/canvas.h"

class TextureCache;
struct PlayerStats;

// A screen element's props as DoScreenElementMorph / <id>:DoMorph change
// them: each step sets some props, interpolated from their values at the
// step's start over its time by its motion (GH3's 0x822A6B28: 0 linear,
// 1 smooth 3t^2 - 2t^3, 2 ease_in t^2, 3 ease_out 2t - t^2; values =
// start + (target - start) x f, step 0x8221BC28) [C]; the steps follow one
// another as the scripts' blocking DoMorph calls do (`wait`: the next step's
// delay, default its time) [C scripts].
struct HudAnim {
    struct Props {
        float x = 0, y = 0, sx = 1, sy = 1, rot = 0, alpha = 1;
        std::array<float, 4> rgba{255, 255, 255, 255};
    };
    enum Field : uint32_t { Pos = 1, Scale = 2, Rot = 4, Alpha = 8, Rgba = 16 };
    struct Step {
        double dur = 0, wait = 0;
        int motion = 0;
        uint32_t set = 0;
        Props to;
    };
    double start = -1e18;
    Props init;
    std::vector<Step> steps;
    static float ease(int motion, float t);
    void begin(double t, const Props& p) { start = t, init = p, steps.clear(); }
    // A step of `durMs`; `waitMs` < 0: the next starts when this one ends.
    HudAnim& then(double durMs, int motion, uint32_t set, const Props& to, double waitMs = -1);
    double length() const;
    bool running(double t) const { return t >= start && t < start + length(); }
    Props at(double t) const;
};

// GH3's one-player HUD (career_hud_2d_elements in HUD_2D_Container, scale
// 0.7): the rock meter (backgrounds, lights, needle, the six Star Power
// tubes), the score body (score text, multiplier lights and nixies, flash)
// and the note-streak counter. GH3 builds it from screen elements
// (create_2d_hud_elements) and drives it from C (UpdateScoreFastPerFrame
// 0x821167B0) and a few scripts (hud_move_note_scorebar,
// hud_flash_red_bg_p1, rock_meter_star_power_on / off, ...); the port draws
// the same sprites from the same table and runs the same rules here
// (docs/hud.md) [C data, C XEX; easing curves I].
class Hud2D {
public:
    // Text in one of GH3's fonts (ui/ui_system.h drawText): `scale` times the
    // font's line height, `align` 0x11 top left .. 0x44 bottom right.
    // `style`: rotation (degrees, about the anchor), horizontal scale, font spacing (ui_system.h TextStyle).
    struct TextStyle {
        float rot = 0, sx = 1, spacing = 0;
    };
    using TextFn = std::function<void(Canvas&, uint32_t font, const std::string&, float x, float y, float scale,
                                      const std::array<uint8_t, 4>& rgba, uint32_t align, const TextStyle& style)>;
    using WidthFn = std::function<float(uint32_t font, const std::string&, float scale, float spacing)>;
    void setText(TextFn text, WidthFn width) { text_ = std::move(text), width_ = std::move(width); }

    // The song's start (current_starttime, ms): the intro's times count from it.
    void setStart(double startMs) { startMs_ = startMs; }
    // One game frame's state at song time `nowMs` (the driver: runs only on changes).
    void update(const PlayerStats& st, double nowMs);
    void draw(Canvas& c, const TextureCache& tex, double nowMs) const;

    // Walk / self-test counters.
    struct Counters {
        uint64_t scoreChanges = 0, needleChanges = 0, tubeChanges = 0, multiplierChanges = 0, streakBarIns = 0,
                 streakBarOuts = 0, dialFlips = 0, combos = 0, dangerStarts = 0, starReady = 0, starActivations = 0,
                 starOffs = 0, missingTextures = 0;
    };
    const Counters& counters() const { return n_; }

private:
    struct V2 {
        float x = 0, y = 0;
    };
    // A morph: from -> to over [start, start + dur] (ms), ease 0 linear,
    // 1 ease_in, 2 ease_out.
    struct Tween {
        double start = -1e18, dur = 0;
        V2 from, to;
        float rotFrom = 0, rotTo = 0, scaleFrom = 1, scaleTo = 1;
        int ease = 0;
    };
    struct Track {
        std::vector<Tween> steps;
        V2 rest;  // before the first step
        // Value at `t`: the last started step's (held at its end).
        void at(double t, V2& p, float& rot, float& scale) const;
        void add(double start, double dur, V2 to, float rotTo, float scaleTo, int ease, double t);
    };

    TextFn text_;
    WidthFn width_;
    Counters n_;
    std::mt19937 rng_{0x47483303};

    // Driver caches (UpdateScoreFastInit: -1).
    int64_t lastScore_ = -1;
    int lastRun_ = -1, lastMult_ = -1;
    float lastSp_ = -1, lastD_ = -1;
    bool barShown_ = false;
    // Applied state.
    std::string scoreText_;
    float scoreSquash_ = 0;  // > 0: the score's x scale (squeezed), y 1.0
    float needleAngle_ = 0, bgGreen_ = 0, bgYellow_ = 1;
    int light_ = 1;  // 0 red, 1 yellow, 2 green
    bool danger_ = false;
    double dangerStart_ = 0;
    std::array<float, 6> tubeFill_{}, fullAlpha_{}, oldAlpha_{};
    float oldAnimateSp_ = 0;
    int mult_ = 1;
    bool multSp_ = false;
    int lights_ = 0;  // L (0..10)
    std::array<int, 4> digits_{};
    int flipDial_ = 0;
    double flipStart_ = -1e18;
    int combo_ = 0;
    // The alerts: "<n> Note Streak!" (+ its glow burst), "Star Power Ready"
    // (+ its lightning) [C scripts hud_show_note_streak_combo,
    // hud_glowburst_alert, show_star_power_ready, hud_lightning_alert].
    HudAnim comboText_, glowburst_, readyText_;
    double lightningStart_ = -1e18;
    double startMs_ = 0;
    void drawAlert(Canvas& c, const HudAnim& a, uint32_t font, const std::string& s, double now, uint32_t align) const;
    // Star Power look.
    bool ready_ = false, active_ = false;
    double readyStart_ = -1e18, activeStart_ = -1e18, offStart_ = -1e18;
    float lastAmount_ = 0;
    std::array<double, 6> pulsePhase_{}, pulsePeriod_{};
    Track rock_, score_, note_;
    std::array<Track, 3> bigTube_;  // tubes 4..6: initial (0,0) -> final (0,-170)
    bool started_ = false;
};

// The HUD alerts' scripts, shared by both HUDs [C scripts hud_show_note_streak_combo,
// hud_glowburst_alert, show_star_power_ready]: they fill the animations.
namespace hudalert {
// "<n> Note Streak!" at (x, y), base scale, wobble `sw`; its glow burst at (gx, gy), scales x gk.
void combo(HudAnim& text, HudAnim& glow, double at, float x, float y, float base, float sw, float gx, float gy, float gk);
// "Star Power Ready" at (x, y), base scale, the big scale's multiplier.
void ready(HudAnim& text, double at, float x, float y, float base, float mult);
// Draws an alert's text (shadow (2,2) black).
void draw(Canvas& c, const Hud2D::TextFn& text, const HudAnim& a, uint32_t font, const std::string& s, double now, uint32_t align);
}  // namespace hudalert

// The song's intro texts (setup_sprites' intro_song_info_text,
// intro_performed_by_text, intro_artist_info_text in text_a10, run by
// intro_song_info): the title, 'performed by' and the artist, upper case,
// moved to current_intro's positions, faded in over song_title_fade_time,
// held song_title_on_time, faded out [C scripts; fastintro props].
class IntroText {
public:
    // `firstMs`: the song time the scroller starts at (the script's first frame).
    void setup(const std::string& title, const std::string& by, const std::string& artist, double startMs, double firstMs);
    void draw(Canvas& c, const Hud2D::TextFn& text, double nowMs) const;

private:
    std::string title_, by_, artist_;
    double at_ = -1e18;  // the fade-in's start
};
