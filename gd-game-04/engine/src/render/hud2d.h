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
    using TextFn = std::function<void(Canvas&, uint32_t font, const std::string&, float x, float y, float scale,
                                      const std::array<uint8_t, 4>& rgba, uint32_t align)>;
    using WidthFn = std::function<float(uint32_t font, const std::string&, float scale)>;
    void setText(TextFn text, WidthFn width) { text_ = std::move(text), width_ = std::move(width); }

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
    float scoreSquash_ = 1;
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
    double comboStart_ = -1e18;
    int combo_ = 0;
    // Star Power look.
    bool ready_ = false, active_ = false;
    double readyStart_ = -1e18, activeStart_ = -1e18, offStart_ = -1e18;
    float lastAmount_ = 0;
    std::array<double, 6> pulsePhase_{}, pulsePeriod_{};
    Track rock_, score_, note_;
    std::array<Track, 3> bigTube_;  // tubes 4..6: initial (0,0) -> final (0,-170)
    bool started_ = false;
};
