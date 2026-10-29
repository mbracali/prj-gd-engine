// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/hud2d.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "data/qbkey.h"
#include "game/player.h"
#include "render/texture_cache.h"

// The tables are career_hud_2d_elements and hud_screen_elements (global
// QB data); the rules are UpdateScoreFastPerFrame 0x821167B0 and the
// scripts named at each part (docs/hud.md).
namespace {

constexpr float kHudScale = 0.7f;  // HUD_2D_Container scale [C data]
// Container positions [C data career_hud_2d_elements].
constexpr float kRockX = 1260, kRockY = 692, kRockOffX = 2000, kRockOffY = 610;
constexpr float kScoreX = 300, kScoreY = 650, kScoreOffX = -500, kScoreOffY = 560;
constexpr float kCounterX = 330, kCounterY = 810, kCounterOff = 800;
constexpr float kScoreFrameWidth = 175;
constexpr float kSmallBulb = 0.7f, kBigBulb = 1.0f;
constexpr float kPerTube = 100.0f / 6;                            // 0x82007A78 [C]
constexpr float kT1 = 14, kT2 = 31.5f;                            // 0x82007A5C, 0x82007A58 [C]
constexpr float kBulbRot[6] = {-47.5f, -33, -18.5f, 0, 21, 42};  // [C data]
// Intro (fastintro / intro props): the HUD moves in from -400 over 200 ms [C script play_intro].
constexpr double kHudIn = -400, kHudMove = 200;

uint32_t K(const char* s) { return qbkey::key(s); }

// GH3's motions (0x822A6B28): 1 smooth, 2 ease_in, 3 ease_out, else linear [C].
float ease(float u, int e) { return HudAnim::ease(e, u); }

struct Xf {
    float x = 0, y = 0, rot = 0, sx = 1, sy = 1;
    // A child at local (px, py).
    Xf child(float px, float py, float r = 0, float s = 1) const {
        const float a = rot * float(M_PI) / 180, c = std::cos(a), sn = std::sin(a);
        const float lx = px * sx, ly = py * sy;
        return Xf{x + lx * c - ly * sn, y + lx * sn + ly * c, rot + r, sx * s, sy * s};
    }
};

struct Item {
    uint32_t tex;
    Xf at;            // the sprite's pivot (its just point)
    float w, h;       // dims (local units)
    float jx, jy;     // just -1..1
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    std::array<uint8_t, 4> rgba{255, 255, 255, 255};
    float alpha = 1;
    float z = 0;
};

}  // namespace

float HudAnim::ease(int motion, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (motion) {
    case 1: return (t - 1.5f) * t * t * -2.0f;  // smooth: 3t^2 - 2t^3
    case 2: return t * t;                       // ease_in
    case 3: return t * 2.0f - t * t;            // ease_out
    default: return t;
    }
}

HudAnim& HudAnim::then(double dur, int motion, uint32_t set, const Props& to, double wait) {
    steps.push_back(Step{dur, wait < 0 ? dur : wait, motion, set, to});
    return *this;
}

double HudAnim::length() const {
    double t = 0, end = 0;
    for (const Step& s : steps) end = std::max(end, t + s.dur), t += s.wait;
    return std::max(end, t);
}

HudAnim::Props HudAnim::at(double t) const {
    Props p = init;
    double t0 = start;
    for (const Step& s : steps) {
        if (t < t0) break;
        const float f = s.dur > 0 ? ease(s.motion, float((t - t0) / s.dur)) : 1.0f;
        auto mix = [f](float a, float b) { return a + (b - a) * f; };
        Props q = p;
        if (s.set & Pos) q.x = mix(p.x, s.to.x), q.y = mix(p.y, s.to.y);
        if (s.set & Scale) q.sx = mix(p.sx, s.to.sx), q.sy = mix(p.sy, s.to.sy);
        if (s.set & Rot) q.rot = mix(p.rot, s.to.rot);
        if (s.set & Alpha) q.alpha = mix(p.alpha, s.to.alpha);
        if (s.set & Rgba)
            for (size_t i = 0; i < 4; ++i) q.rgba[i] = mix(p.rgba[i], s.to.rgba[i]);
        p = q;
        t0 += s.wait;
    }
    return p;
}

void Hud2D::Track::at(double t, V2& p, float& rot, float& scale) const {
    p = rest, rot = 0, scale = 1;
    for (const Tween& s : steps) {
        if (t < s.start) break;
        const float u = s.dur > 0 ? ease(float((t - s.start) / s.dur), s.ease) : 1;
        p = V2{s.from.x + (s.to.x - s.from.x) * u, s.from.y + (s.to.y - s.from.y) * u};
        rot = s.rotFrom + (s.rotTo - s.rotFrom) * u;
        scale = s.scaleFrom + (s.scaleTo - s.scaleFrom) * u;
    }
}

void Hud2D::Track::add(double start, double dur, V2 to, float rotTo, float scaleTo, int e, double now) {
    // Steps already over can go (keeping the latest as the rest).
    while (steps.size() > 1 && steps[1].start + steps[1].dur < now) steps.erase(steps.begin());
    Tween s;
    float r, sc;
    at(start, s.from, r, sc);
    s.rotFrom = r, s.scaleFrom = sc;
    s.start = start, s.dur = dur, s.to = to, s.rotTo = rotTo, s.scaleTo = scaleTo, s.ease = e;
    // A step that starts before others end replaces the later ones.
    steps.erase(std::remove_if(steps.begin(), steps.end(), [&](const Tween& o) { return o.start >= start; }), steps.end());
    steps.push_back(s);
}

void Hud2D::update(const PlayerStats& st, double now) {
    if (!started_) {
        started_ = true;
        rock_.rest = V2{kRockOffX, kRockOffY};
        score_.rest = V2{kScoreOffX, kScoreOffY};
        note_.rest = V2{kCounterX, kCounterY + kCounterOff};
        for (auto& t : bigTube_) t.rest = V2{0, 0};
        // move_2d_elements_to_default, then the settle [C script intro_hud_move].
        // (current_intro times count from current_starttime.)
        const double t0 = startMs_;
        rock_.add(t0 + kHudIn, kHudMove, V2{kRockX, kRockY}, 0, 1, 0, now);
        score_.add(t0 + kHudIn, kHudMove, V2{kScoreX, kScoreY}, 0, 1, 0, now);
        rock_.add(t0 - 200, 100, V2{kRockX - 50, kRockY}, -5, 1, 0, now);
        score_.add(t0 - 200, 100, V2{kScoreX + 50, kScoreY}, 0, 1, 0, now);
        rock_.add(t0 - 100, 125, V2{kRockX + 25, kRockY}, 5, 1, 0, now);
        score_.add(t0 - 100, 125, V2{kScoreX - 25, kScoreY}, 0, 1, 0, now);
        rock_.add(t0 + 25, 100, V2{kRockX, kRockY}, 0, 1, 0, now);
        score_.add(t0 + 25, 100, V2{kScoreX, kScoreY}, 0, 1, 0, now);
    }

    // Score text [C 0x82116884].
    const int64_t s = st.score;
    if (s != lastScore_) {
        lastScore_ = s;
        char b[32];
        std::snprintf(b, sizeof b, "%lld", static_cast<long long>(s));
        scoreText_ = b;
        // Past 5 characters: scale (score_frame_width / width, 1.0).
        if (scoreText_.size() > 5 && width_) {
            const float w = width_(K("num_a9"), scoreText_, 1.0f, 5);
            if (w > 0) scoreSquash_ = kScoreFrameWidth / w;
        }
        ++n_.scoreChanges;
    }

    // Star Power tubes [C 0x821164D0].
    const float a = st.starPower;
    if (a != lastSp_) {
        const bool decreasing = a < lastSp_;
        bool apply = true;
        if (!decreasing && oldAnimateSp_ > a) apply = false;
        if (apply) {
            oldAnimateSp_ = a;
            float left = a;
            for (size_t k = 0; k < 6; ++k) {
                const float f = left >= kPerTube ? 1.0f : std::max(0.0f, left / kPerTube);
                tubeFill_[k] = f;
                fullAlpha_[k] = f;
                if (!decreasing || f >= 1) oldAlpha_[k] = f;
                left = std::max(0.0f, left - kPerTube);
            }
        }
        lastSp_ = a;
        ++n_.tubeChanges;
    }

    // Health: needle, backgrounds, lights, danger [C 0x82116D08].
    const float S = 2.0f, d = S - st.health;
    if (d != lastD_) {
        lastD_ = d;
        const float ang = d / S * -84.0f + 42.0f;
        needleAngle_ = ang;
        if (ang > 0) {
            bgGreen_ = std::min(ang / kT1, 1.0f), bgYellow_ = 1;
            light_ = ang < kT1 ? 1 : 2;
        } else if (ang == 0) {
            bgGreen_ = 0, bgYellow_ = 1, light_ = 1;
        } else {
            bgGreen_ = 0;
            bgYellow_ = ang > -kT1 ? (kT1 + ang) / kT1 : 0;
            light_ = ang > -kT1 ? 1 : 0;
        }
        const bool danger = ang <= -kT2;
        if (danger && !danger_) danger_ = true, dangerStart_ = now, ++n_.dangerStarts;
        else if (!danger && danger_) danger_ = false;  // hud_flash_red_bg_kill
        ++n_.needleChanges;
    }

    // Star Power look [C scripts show_star_power_ready, rock_meter_star_power_on / off, hud_activated_star_power].
    if (!active_ && !ready_ && a >= 50 && lastAmount_ < 50) {
        ready_ = true, readyStart_ = now, ++n_.starReady;
        // show_star_power_ready: waits for a showing combo alert, then (not in
        // Star Power) the text from (640,230) - (0,20) at scale 4, alpha 0,
        // rot 3, with the lightning; base scale 1.2, x1.5 [C script].
        const double at = comboText_.running(now) ? comboText_.start + comboText_.length() : now;
        if (!st.starPowerActive) hudalert::ready(readyText_, at, 640, 210, 1.2f, 1.5f), lightningStart_ = at;
        rock_.add(now, 150, V2{kRockX, kRockY - 50}, 10, 1.5f, 2, now);
        rock_.add(now + 150, 150, V2{kRockX, kRockY + 25}, -15, 0.5f, 2, now);
        rock_.add(now + 300, 150, V2{kRockX, kRockY}, 0, 1, 0, now);
        for (size_t i = 0; i < 3; ++i) bigTube_[i].add(now + 200.0 * double(i), 400, V2{0, -170}, 0, 1, 0, now);
        std::uniform_real_distribution<double> r(100, 500);
        for (size_t k = 0; k < 6; ++k) pulsePhase_[k] = r(rng_), pulsePeriod_[k] = r(rng_);
    }
    lastAmount_ = a;
    if (st.starPowerActive && !active_) {
        active_ = true, activeStart_ = now, ++n_.starActivations;
        lastRun_ = -1, lastMult_ = -1;  // UpdateNixie
        ready_ = true;
    } else if (!st.starPowerActive && active_) {
        active_ = false, ready_ = false, offStart_ = now, ++n_.starOffs;
        lastRun_ = -1, lastMult_ = -1;
        // Tubes 6..4 sink back one after another [C script rock_meter_star_power_off].
        for (int j = 2, i = 0; j >= 0; --j, ++i) {
            const double t0 = now + 700.0 * i;
            bigTube_[size_t(j)].add(t0, 100, V2{0, -187}, 0, 1, 0, now);
            bigTube_[size_t(j)].add(t0 + 200, 400, V2{0, 0}, 0, 1, 0, now);
        }
    }

    // Streak and multiplier [C 0x82117204, 0x8210A748].
    const int run = st.currentRun;
    if (run != lastRun_) {
        const int base = run < 10 ? 1 : run < 20 ? 2 : run < 30 ? 3 : 4;
        const bool sp = st.starPowerActive;
        const int m = sp ? base * 2 : base;
        if (lastRun_ >= 0) {  // (an UpdateNixie re-apply does not move the bar)
            if (run < 25 && barShown_) {
                barShown_ = false, ++n_.streakBarOuts;
                note_.add(now, 250, V2{kCounterX, kCounterY - 60}, 0, 1, 3, now);
                note_.add(now + 750, 500, V2{kCounterX, kCounterY + kCounterOff}, 0, 1, 0, now);
            } else if (run >= 25 && !barShown_) {
                barShown_ = true, ++n_.streakBarIns;
                note_.add(now, 500, V2{kCounterX, kCounterY - 60}, 0, 1, 3, now);
                note_.add(now + 500, 500.0 / 3, V2{kCounterX, kCounterY}, 0, 1, 2, now);
                note_.add(now + 500 + 500.0 / 3, 100, V2{kCounterX, kCounterY + 10}, 0, 1, 3, now);
                note_.add(now + 600 + 500.0 / 3, 100, V2{kCounterX, kCounterY}, 0, 1, 2, now);
            } else if (run >= 25 && barShown_ && run > lastRun_) {
                flipDial_ = run % 1000 == 0 ? 4 : run % 100 == 0 ? 3 : run % 10 == 0 ? 2 : 1;
                flipStart_ = now, ++n_.dialFlips;
            }
            // hud_show_note_streak_combo (not while one shows; after a showing
            // "Star Power Ready"): "<n> Note Streak!" at (640,211), scale 3 ->
            // 1, the wobble (s = 0.8), up 230 px; the glow burst after 0.2 s [C scripts].
            if ((run == 50 || (run > 50 && run % 100 == 0)) && !comboText_.running(now)) {
                combo_ = run, ++n_.combos;
                const double at = readyText_.running(now) ? readyText_.start + readyText_.length() : now;
                hudalert::combo(comboText_, glowburst_, at, 640, 211, 1, 0.8f, 640, 237, 1);
            }
        }
        digits_ = {run % 10, run / 10 % 10, run / 100 % 10, run / 1000};
        if (m != lastMult_ || sp != multSp_) ++n_.multiplierChanges;
        mult_ = m, multSp_ = sp, lastMult_ = m;
        lights_ = run > 30 ? 10 : run > 20 ? run - 20 : run > 10 ? run - 10 : run;
        lastRun_ = run;
    }
}

void Hud2D::draw(Canvas& c, const TextureCache& tex, double now) const {
    if (!started_) return;  // nothing until the first game frame placed it (offscreen)
    std::vector<Item> items;
    items.reserve(64);
    const Xf root{0, 0, 0, kHudScale, kHudScale};
    auto sprite = [&](const char* name, const Xf& at, float jx, float jy, float z, float alpha = 1,
                      std::array<uint8_t, 4> rgba = {255, 255, 255, 255}, float w = 0, float h = 0) {
        Item it{K(name), at, w, h, jx, jy};
        it.z = z, it.alpha = alpha, it.rgba = rgba;
        items.push_back(it);
        return items.size() - 1;
    };

    // Rock meter.
    V2 rp;
    float rr, rs;
    rock_.at(now, rp, rr, rs);
    const Xf rock = root.child(rp.x, rp.y, rr, rs);
    const bool sp = active_ || ready_;
    // The danger flash: bg_red pulses black / [225 225 225] (0.2 s each, twice, then 0.5 s) [C script hud_flash_red_bg_p1].
    std::array<uint8_t, 4> red{255, 255, 255, 255};
    if (danger_) {
        const double p = std::fmod(now - dangerStart_, 1300.0);
        float v = 225;
        if (p < 800) {
            const double q = std::fmod(p, 400.0);
            v = q < 200 ? float(225 * (1 - q / 200)) : float(225 * (q - 200) / 200);
        }
        red = {uint8_t(v), uint8_t(v), uint8_t(v), 255};
    }
    // The big glow pulses while Star Power is ready [C script pulsate_big_glow].
    float glow = 0;
    std::array<uint8_t, 4> glowRgba{95, 205, 255, 255};
    if (ready_ && !active_) {
        const double p = std::fmod(now - readyStart_, 2000.0);
        const float u = float(p < 1000 ? p / 1000 : (p - 1000) / 1000);
        glow = p < 1000 ? 1 - ease(u, 2) : ease(u, 3);
        const float k = p < 1000 ? u : 1 - u;
        glowRgba = {uint8_t(255 - 160 * k), uint8_t(255 - 50 * k), 255, 255};
    }
    sprite("Char_Select_Hilite1", rock.child(-50, -100), -1, -1, -10, glow, glowRgba, 350, 350);
    sprite("HUD_rock_body", rock, -1, -1, 20);
    sprite("HUD_rock_BG_green", rock, -1, -1, 16, bgGreen_);
    sprite("HUD_rock_BG_red", rock, -1, -1, 14, 1, red);
    sprite("HUD_rock_BG_yellow", rock, -1, -1, 15, bgYellow_);
    sprite("HUD_rock_lights_all", rock, -1, -1, 17);
    sprite("HUD_rock_lights_green", rock.child(128, 0), -1, -1, 18, light_ == 2 ? 1.0f : 0.0f);
    sprite("HUD_rock_lights_red", rock, -1, -1, 18, light_ == 0 ? 1.0f : 0.0f);
    sprite("HUD_rock_lights_yellow", rock.child(128, 0), 0, -1, 18, light_ == 1 ? 1.0f : 0.0f);
    {
        const Xf n = rock.child(132, 165, needleAngle_);
        sprite("HUD_rock_needle", n, 0.5f, 0.8f, 19);
    }
    for (size_t k = 0; k < 6; ++k) {
        const Xf bulb = rock.child(128, 128, kBulbRot[k]);
        const bool big = k >= 3;
        const float bs = big ? kBigBulb : kSmallBulb;
        V2 tp{0, -160};
        if (big) {
            float r0, s0;
            bigTube_[k - 3].at(now, tp, r0, s0);
        }
        // Pulsing between 0.3 and old_alpha while ready, not active [C script pulsate_star_power_bulb].
        float pulse = 1;
        float fa = fullAlpha_[k];
        if (ready_ && !active_) {
            const double per = pulsePhase_[k] + pulsePeriod_[k];
            const double p = std::fmod(now - readyStart_, per);
            const float o = oldAlpha_[k];
            const float lo = 0.3f;
            const float v = p < pulsePhase_[k] ? o + (lo - o) * ease(float(p / pulsePhase_[k]), 2)
                                               : lo + (o - lo) * ease(float((p - pulsePhase_[k]) / pulsePeriod_[k]), 3);
            pulse = o > 0 ? v / o : 1;
            fa = v;
        }
        sprite("HUD_rock_tube", bulb.child(tp.x, tp.y), 0, 0, 0, 1, {255, 255, 255, 255}, 64 * bs, 128 * bs);
        const float fillOff = big ? 32 : 40;
        if (tubeFill_[k] > 0) {
            // The fill: 64x16 scaled (0.56, 3f), anchored centre bottom [C 0x821164D0].
            const Xf f = bulb.child(tp.x, tp.y + fillOff);
            sprite(sp ? "HUD_rock_tube_glow_fill_b" : "HUD_rock_tube_glow_fill", f, 0, 1, 0.1f, std::min(1.0f, pulse), {255, 255, 255, 255},
                   64 * bs * 0.56f, 16 * bs * 3 * tubeFill_[k]);
        }
        sprite(sp ? "HUD_rock_tube_glow_full_b" : "HUD_rock_tube_glow_full", bulb.child(tp.x, tp.y), 0, 0, 0.2f, fa, {255, 255, 255, 255},
               64 * bs, 128 * bs);
    }

    // Score body.
    V2 spos;
    float sr, ss;
    score_.at(now, spos, sr, ss);
    const Xf score = root.child(spos.x, spos.y, sr, ss);
    sprite("HUD_score_body", score, -1, -1, 5);
    {
        // Light textures: _blue in Star Power, else by multiplier [C jump table 0x821172D8].
        const char* suffix = multSp_ ? "_blue" : mult_ == 3 ? "_green" : mult_ == 4 ? "_purple" : "";
        const int full = lights_ / 2;
        for (int i = 0; i < 5; ++i) {
            const float y = 200.0f - 30.0f * float(i);
            char n0[48], n1[48], n2[48];
            std::snprintf(n0, sizeof n0, "hud_score_light_0%s", suffix);
            std::snprintf(n1, sizeof n1, "hud_score_light_1%s", suffix);
            std::snprintf(n2, sizeof n2, "hud_score_light_2%s", suffix);
            const bool any = lights_ > 1;
            items.push_back(Item{K(n0), score.child(0, y), 0, 0, -1, -1});
            items.back().z = 5;
            items.push_back(Item{K(n1), score.child(0, y), 0, 0, -1, -1});
            items.back().z = 5.1f, items.back().alpha = any && (lights_ & 1) && i == full ? 1.0f : 0.0f;
            items.push_back(Item{K(n2), score.child(0, y), 0, 0, -1, -1});
            items.back().z = 5.2f, items.back().alpha = any && i < full ? 1.0f : 0.0f;
        }
        char nixie[32];
        std::snprintf(nixie, sizeof nixie, "HUD_score_nixie_%d%c", mult_, multSp_ ? 'b' : 'a');
        sprite(nixie, score.child(70, 90), -1, -1, 4);
        // The flash on activation: alpha 1 scale 5 over 0.2 s, back over 0.1 s [C script hud_activated_star_power].
        const double ft = now - activeStart_;
        if (ft >= 0 && ft < 300) {
            const float u = ft < 200 ? float(ft / 200) : 1 - float((ft - 200) / 100);
            items.push_back(Item{K("HUD_score_flash"), score.child(128, 128, 0, 1 + 4 * u), 0, 0, 0, 0});
            items.back().z = 20, items.back().alpha = u;
        }
    }

    // Note-streak counter.
    V2 np;
    float nr, ns;
    note_.at(now, np, nr, ns);
    const Xf note = root.child(np.x, np.y, nr, ns);
    sprite("HUD_counter_body", note, -1, -1, 9);
    sprite("HUD_counter_drum", note.child(4, 40), -1, -1, 8);
    if (digits_[3] == 0) sprite("HUD_counter_drum_icon", note.child(44, 40), -1, -1, 26);

    // The alerts' sprites, in hud_destroygroup (screen space): the glow burst
    // (z 50) and the four lightning frames (800x100 at the text - (0,20), z 45,
    // each fading in over 0.2 s as the one before fades out) [C scripts
    // hud_glowburst_alert, hud_lightning_alert].
    if (glowburst_.running(now)) {
        const HudAnim::Props g = glowburst_.at(now);
        const std::array<uint8_t, 4> rgba{uint8_t(g.rgba[0]), uint8_t(g.rgba[1]), uint8_t(g.rgba[2]), uint8_t(g.rgba[3])};
        sprite("Char_Select_Hilite1", Xf{g.x, g.y, 0, g.sx, g.sy}, 0, 0, 50, g.alpha, rgba);
    }
    {
        const double t = now - lightningStart_;
        static const char* const kBolt[4] = {"HUD_lightning_01", "HUD_lightning_03", "HUD_lightning_05", "HUD_lightning_07"};
        if (t >= 0 && t < 1000)
            for (int i = 0; i < 4; ++i) {
                const double a = t - 200.0 * i;  // in over [0, 200], out over [200, 400]
                const float al = a < 0 || a >= 400 ? 0.0f : a < 200 ? float(a / 200) : float(1 - (a - 200) / 200);
                sprite(kBolt[i], Xf{640, 190}, 0, -1, 45, al, {255, 255, 255, 255}, 800, 100);
            }
    }

    std::stable_sort(items.begin(), items.end(), [](const Item& x, const Item& y) { return x.z < y.z; });
    std::vector<Vertex> v;
    for (const Item& it : items) {
        if (it.alpha <= 0.002f) continue;
        const gfx::TextureId id = tex.find(it.tex);
        if (!id.id) {
            const_cast<Hud2D*>(this)->n_.missingTextures++;
            continue;
        }
        float w = it.w, h = it.h;
        if (w <= 0 || h <= 0) {
            int tw = 0, th = 0;
            tex.size(it.tex, tw, th);
            w = float(tw), h = float(th);
        }
        const float l = -(it.jx + 1) / 2 * w, t = -(it.jy + 1) / 2 * h;
        const float a = it.at.rot * float(M_PI) / 180, cs = std::cos(a), sn = std::sin(a);
        auto P = [&](float lx, float ly, float u, float vv) {
            lx *= it.at.sx, ly *= it.at.sy;
            const uint8_t al = uint8_t(std::clamp(float(it.rgba[3]) * it.alpha, 0.0f, 255.0f));
            return Vertex{it.at.x + lx * cs - ly * sn, it.at.y + lx * sn + ly * cs, 0, u, vv, it.rgba[0], it.rgba[1], it.rgba[2], al};
        };
        const Vertex A = P(l, t, it.u0, it.v0), B = P(l + w, t, it.u1, it.v0), C = P(l + w, t + h, it.u1, it.v1), D = P(l, t + h, it.u0, it.v1);
        const Vertex q[6] = {A, B, C, A, C, D};
        c.draw(q, 6, int(id.id), Blend::Alpha);
    }

    if (!text_) return;
    // Score text: num_a9, (222,70) in the score body, scale 1.1, right bottom, z 20,
    // font_spacing 5, shadow (3,3) black; past 5 digits the scale becomes
    // (score_frame_width / text width, 1.0) [C setup_hud, 0x82116884; the
    // width read unscaled I].
    {
        const Xf p = score.child(222, 70);
        Hud2D::TextStyle ts;
        ts.spacing = 5;
        float k = 1.1f * kHudScale * ss;
        if (scoreSquash_ > 0) k = 1.0f * kHudScale * ss, ts.sx = scoreSquash_;
        text_(c, K("num_a9"), scoreText_, p.x + 3 * kHudScale, p.y + 3 * kHudScale, k, {0, 0, 0, 255}, 0x44, ts);
        text_(c, K("num_a9"), scoreText_, p.x, p.y, k, {255, 255, 255, 255}, 0x44, ts);
    }
    // Streak digits: num_a7 at (222 - 37 i, 78), centred; dial 1 dark blue [C setup_hud].
    for (int i = 1; i <= 4; ++i) {
        Xf p = note.child(222.0f - 37.0f * float(i), 78);
        float al = 200;
        if (i == flipDial_ && now - flipStart_ < 100) {  // hud_flip_note_streak_num [C script]
            const float u = float((now - flipStart_) / 100);
            p = note.child(222.0f - 37.0f * float(i), 78 + 10 * (1 - u));
            al = 200 * u;
        }
        const std::array<uint8_t, 4> col = i == 1 ? std::array<uint8_t, 4>{15, 15, 70, uint8_t(al)}
                                                  : std::array<uint8_t, 4>{230, 230, 230, uint8_t(al)};
        text_(c, K("num_a7"), std::to_string(digits_[size_t(i - 1)]), p.x, p.y, kHudScale, col, 0x22, Hud2D::TextStyle{});
    }
    // "<n> Note Streak!" and "Star Power Ready" (text_a6, just center top,
    // shadow (2,2) black) [C scripts].
    if (comboText_.running(now)) {
        char b[48];
        std::snprintf(b, sizeof b, "%d Note Streak!", combo_);
        drawAlert(c, comboText_, K("text_a6"), b, now, 0x12);
    }
    if (readyText_.running(now) && ready_) drawAlert(c, readyText_, K("text_a6"), "Star Power Ready", now, 0x12);
}

void Hud2D::drawAlert(Canvas& c, const HudAnim& a, uint32_t font, const std::string& s, double now, uint32_t align) const {
    hudalert::draw(c, text_, a, font, s, now, align);
}

namespace hudalert {

void combo(HudAnim& text, HudAnim& glow, double at, float x, float y, float base, float sw, float gx, float gy, float gk) {
    // hud_show_note_streak_combo: scale base x 3, alpha 0 -> base, alpha 1
    // (0.2 s ease_in); the glow burst; the wobble (s = sw); rot 0; up 230 px
    // at scale base x 0.8 (0.35 s ease_in) [C script].
    using A = HudAnim;
    A::Props p;
    p.x = x, p.y = y, p.sx = p.sy = base * 3, p.alpha = 0, p.rgba = {223, 223, 223, 255};
    text.begin(at, p);
    auto P = [](float sc, float rot, bool c1) {
        A::Props q;
        q.sx = q.sy = sc, q.rot = rot;
        q.rgba = c1 ? std::array<float, 4>{245, 255, 160, 255} : std::array<float, 4>{245, 255, 120, 255};
        q.alpha = 1;
        return q;
    };
    const float b = base;
    text.then(200, 2, A::Scale | A::Alpha, P(b, 0, false));
    text.then(400, 3, A::Scale | A::Rgba | A::Rot, P(b + sw, 3, true));
    text.then(400, 2, A::Scale | A::Rgba | A::Rot, P(b, 2, false));
    text.then(300, 3, A::Scale | A::Rgba | A::Rot, P(b + sw / 1.5f, -2, true));
    text.then(300, 2, A::Scale | A::Rgba | A::Rot, P(b, -1, false));
    text.then(200, 3, A::Scale | A::Rgba | A::Rot, P(b + sw / 2, 2, true));
    text.then(200, 2, A::Scale | A::Rgba | A::Rot, P(b, 1, false));
    text.then(100, 3, A::Scale | A::Rgba | A::Rot, P(b + sw / 2.5f, -1, true));
    text.then(100, 2, A::Scale | A::Rgba | A::Rot, P(b, 1, false));
    text.then(0, 0, A::Scale | A::Rot, P(b, 0, false));
    A::Props up = P(b * 0.8f, 0, false);
    up.x = x, up.y = y - 230;
    text.then(350, 2, A::Pos | A::Scale, up);
    // hud_glowburst_alert: Char_Select_Hilite1, scale (15,1) -> (20,5) a 0.5
    // -> (15,0.5) [245 255 160] -> (80,0) a 0 (face-off: halved, gk 0.5) [C script].
    A::Props g;
    g.x = gx, g.y = gy, g.sx = 15 * gk, g.sy = 1 * gk, g.rgba = {245, 255, 200, 255};
    glow.begin(at + 200, g);
    g.sx = 20 * gk, g.sy = 5 * gk, g.alpha = 0.5f;
    glow.then(100, 3, A::Scale | A::Alpha, g);
    g.sx = 15 * gk, g.sy = 0.5f * gk, g.rgba = {245, 255, 160, 255};
    glow.then(100, 3, A::Scale | A::Alpha | A::Rgba, g);
    g.sx = 80 * gk, g.sy = 0, g.alpha = 0;
    glow.then(800, 2, A::Scale | A::Alpha, g);
}

void ready(HudAnim& text, double at, float x, float y, float base, float mult) {
    // show_star_power_ready: at the position, scale 4, rgba [190 225 255 250],
    // alpha 0, rot 3; -> base a 1 rot -3 (0.3 ease_in); base x mult rot 4 (0.3
    // ease_out); base rot -5 [145 215 235 250] (0.3 ease_in); 12 x rot x -0.7
    // from 10 (0.08 ease_out); rot 0; up 230 at base x 0.5, alpha 0 (0.3 ease_in) [C script].
    using A = HudAnim;
    A::Props p;
    p.x = x, p.y = y, p.sx = p.sy = 4, p.rot = 3, p.alpha = 0, p.rgba = {190, 225, 255, 250};
    text.begin(at, p);
    auto P = [x, y](float sc, float rot, float alpha) {
        A::Props q;
        q.sx = q.sy = sc, q.rot = rot, q.alpha = alpha, q.rgba = {145, 215, 235, 250}, q.x = x, q.y = y;
        return q;
    };
    text.then(300, 2, A::Scale | A::Alpha | A::Rot, P(base, -3, 1));
    text.then(300, 3, A::Scale | A::Rot, P(base * mult, 4, 1));
    text.then(300, 2, A::Scale | A::Rot | A::Rgba, P(base, -5, 1));
    float r = 10;
    for (int i = 0; i < 12; ++i) {
        r *= -0.7f;
        text.then(80, 3, A::Rot | A::Alpha, P(base, r, 1));
    }
    text.then(0, 3, A::Rot, P(base, 0, 1));
    A::Props out = P(base * 0.5f, 0, 0);
    out.y = y - 230;
    text.then(300, 2, A::Pos | A::Scale | A::Alpha, out);
}

void draw(Canvas& c, const Hud2D::TextFn& text, const HudAnim& a, uint32_t font, const std::string& s, double now, uint32_t align) {
    if (!text || !a.running(now)) return;
    const HudAnim::Props p = a.at(now);
    if (p.alpha <= 0.002f) return;
    Hud2D::TextStyle ts;
    ts.rot = p.rot, ts.sx = p.sy > 0 ? p.sx / p.sy : 1;
    // shadow_offs (2,2) black, turning with the text [I].
    const float r = p.rot * float(M_PI) / 180, ox = 2 * p.sy, oy = 2 * p.sy;
    const auto al = uint8_t(std::clamp(p.rgba[3] * p.alpha, 0.0f, 255.0f));
    text(c, font, s, p.x + ox * std::cos(r) - oy * std::sin(r), p.y + ox * std::sin(r) + oy * std::cos(r), p.sy, {0, 0, 0, uint8_t(255 * p.alpha)}, align,
         ts);
    text(c, font, s, p.x, p.y, p.sy, {uint8_t(p.rgba[0]), uint8_t(p.rgba[1]), uint8_t(p.rgba[2]), al}, align, ts);
}

}  // namespace hudalert

void IntroText::setup(const std::string& title, const std::string& by, const std::string& artist, double startMs, double firstMs) {
    auto upper = [](std::string t) {
        for (char& ch : t) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        return t;
    };
    title_ = upper(title), by_ = upper(by), artist_ = upper(artist);
    // fastintro: song_title_start_time -6700 after current_starttime, so at
    // the scroller's first frame [C data fastintro_sequence_props].
    at_ = std::max(firstMs, startMs - 6700.0);
}

void IntroText::draw(Canvas& c, const Hud2D::TextFn& text, double now) const {
    if (!text) return;
    // Fade 700 ms in, 3000 ms on, 700 ms out (song_title_fade_time / on_time) [C data].
    const double t = now - at_;
    if (t < 0 || t >= 4400) return;
    const float a = t < 700 ? float(t / 700) : t < 3700 ? 1.0f : float(1 - (t - 3700) / 700);
    const uint32_t font = K("text_a10");
    const Hud2D::TextStyle plain;
    Hud2D::TextStyle half;
    half.sx = 2;  // scale (1.0, 0.5): half height, full width
    auto draw = [&](const std::string& s, float x, float y, float h, const std::array<uint8_t, 4>& rgba, const Hud2D::TextStyle& st) {
        if (s.empty()) return;
        text(c, font, s, x + 1, y + 1, h, {0, 0, 0, uint8_t(255 * a)}, 0x11, st);  // shadow_offs (1,1) [C]
        text(c, font, s, x, y, h, {rgba[0], rgba[1], rgba[2], uint8_t(float(rgba[3]) * a)}, 0x11, st);
    };
    // Positions: song_title_pos (255,75), performed_by_pos (255,135), song_artist_pos (255,150);
    // title scale 1.23 rgba [210 210 210 250], by (1, 0.5) [230 205 160 255], artist 1.0 [255 190 70 255] [C].
    draw(title_, 255, 75, 1.23f, {210, 210, 210, 250}, plain);
    draw(by_, 255, 135, 0.5f, {230, 205, 160, 255}, half);
    draw(artist_, 255, 150, 1.0f, {255, 190, 70, 255}, plain);
}
