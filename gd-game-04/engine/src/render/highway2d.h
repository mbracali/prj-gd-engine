// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include "render/canvas.h"

class Globals;
class TextureCache;
struct NoteTrack;
struct TrackNote;
struct PlayerStats;
class Player;

// GH3's gameplay highway, as its scripts and C code lay it out (docs/
// highway.md): a 2D perspective trapezoid built by generate_pos_table from
// guitar_tweaks.qb (one player: playline 655, height 350, top width 160,
// widthOffsetFactor 2.2), 1152 perspective rows (heightPerspFact/Exp
// tables of highway_2d.qb), gems moving top to playline in scroll_time -
// destroy_time, sprites from global_gfx.tex; the strings, the now-bar
// buttons (lip, mid, neck, head: raised on a hit, sinking in
// button_sink_time), the Star Power glow and lightning on the sidebars, the
// hit flames and sparks, the miss pulse, the multiplier-loss shake and the
// intro (the gem container rising from y 720, the buttons popping up).
class Highway2D {
public:
    // Reads the tweaks and builds the row tables; false if data is missing.
    // `players` 1 or 2 (two highways side by side; `player` 0 left, 1 right).
    bool init(const Globals& g, int difficulty, int players = 1, int player = 0);
    // The song's start (current_starttime): the intro's times count from it.
    // `firstMs`: the song time of the scroller's first frame.
    void setStart(double startMs, double firstMs);
    // The gem container's x offset (2P: -/+ x_offset_p2; devil_finish moves
    // player 2's to 1000) [C script setup_highway].
    void setContainerX(float x);
    // lefthanded_gems / lefthanded_button_ups from the lefty flip option [C script start_gem_scroller].
    void setLefty(bool lefty) { lefty_ = lefty; }
    // The song's gem_offset (ms): gems and fretbars reach the playline that
    // much later (gem_scroller's gem_offset = default + the song's) [C script].
    void setGemOffset(double ms) { gemOffsetMs_ = ms; }
    // One 60 Hz game frame (the per-frame scripts and ButtonCheckerPerFrame:
    // the intro rise, the button pixels sinking, the multiplier-loss shake,
    // the Star Power look).
    void step(const PlayerStats& st, double nowMs, double dtMs, bool battleRules, bool twoPlayerModes);
    // Draws the highway, fretbars, strings, sidebars, tails, gems, buttons
    // and effects at song time `nowMs` (ms). `held`: the frets held now.
    void draw(Canvas& c, const TextureCache& tex, const NoteTrack& track, const Player& player, double nowMs, uint8_t held);
    // A note was hit on `frets` with `held` down at song time `nowMs`: a hit
    // flame and sparks per fret; the buttons raised (a chord: every held
    // fret; a single note: the held frets from its fret up) [C 0x8211C9D0].
    void noteHit(uint8_t frets, uint8_t held, double nowMs, bool starPower);
    // highway_pulse_black: a passing note missed while the guitar is muted [C script GuitarEvent_MissedNote].
    void missPulse(double nowMs);
    // The sustain tails, each game frame after step() (docs/highway.md 2.6):
    // the hold's start (WhammyFXOn's sys_whammy_hold01 at the frets) and end
    // (CheckNoteHoldEnd 0x8211D3B8: the tail MakeDeadWhammy'd, its head let
    // go), star_power_whammy's blue tail for a whammied star sustain
    // (`whammy` 0 rest .. 1, `blueWhammys` off in battle / boss) [C].
    void stepTails(const Player& player, const NoteTrack& track, double nowMs, float whammy, bool blueWhammys);
    // A missed note: its tail MakeDeadWhammy'd (the miss judge 0x8211F328) [C].
    void noteMissed(int note);
    uint64_t deadTails = 0, starWhammys = 0, holdFxStarted = 0;  // for the checks
    // Battle / boss: the scripts that morph gem_container<p>.
    // hammer_highway {push time}: from where the container is, a linear
    // morph to there + push over `timeS`, the same back, `timeS` each; a
    // hammer while one runs (battle_p<n>_highway_hammer) does nothing. An
    // attack's hit pushes (-70, 50) / (70, 50) over 0.1 s (battle_trigger_on,
    // bossbattle_trigger_on); Death Drain's bites (0, 30) over 0.01 s
    // (bite_particle) [C scripts].
    void hammer(double nowMs, float pushX, float pushY, float timeS);
    // Each game frame of a battle, before that frame's hammers: the running
    // hammer and shake_highway go on; `shaking` (shake_notes > -1, Amp
    // Overload) rising spawns shake_highway [C scripts battle_lightning, shake_highway].
    void stepBattle(double nowMs, bool shaking);
    uint64_t hammers = 0, battleShakes = 0;
    // Counters for the walk / self checks.
    uint64_t flamesStarted = 0, pulses = 0, shakes = 0;
    // The normalised depth v (0 top, 1 playline) of a note time at song time
    // `nowMs` — for the checks.
    float depthOf(double noteMs, double nowMs) const;
    // The highway surface's texture (the guitarist's highway material's, else
    // the global fallback) [C data].
    void setSurfaceTexture(uint32_t key) { surfaceTex_ = key; }
    // A gem's time on screen: scroll_time - destroy_time (ms).
    float travelMs() const { return travelMs_; }
    // The sustain tails' wibble (whammywibble0 / 1, 128 values; the tail
    // shader WhammyBar_UI scales a vertex's half width by m_vsVertexScales
    // [128 - its 8-row grid row]). SetNewWhammyValue 0x821056F8: `value`
    // (1 rest .. 2 full bar) into the first `speed` entries (whammy_wibble_speed)
    // after the older ones moved down by as many, kept over the hold's
    // remaining time (128 x remaining / travel time + 2 entries), the rest 1.
    // SetAllWhammyValues 0x82105918: every entry `value` [C].
    void setNewWhammyValue(float value, float remainingMs, int speed);
    void setAllWhammyValues(float value) { wibble_.fill(value); }
    const std::array<float, 128>& wibble() const { return wibble_; }
    // The gem container's y (the intro rise; 0 once up).
    float containerY() const { return containerY_; }
    // gem_container<p>'s position at song time `now`: the x offset, the
    // intro's rise, the multiplier-loss shake, the battle push / bob (the
    // scripts' elements under it follow; ui/song_hooks.cpp).
    void containerPos(double now, float& x, float& y) const;
    // Lane k's x at the top and at the playline, the top's y and the
    // playline's y in the container's space (generate_pos_table's gsx / gex
    // .., startY, highway_playline) [C script].
    void lane(int k, float& topX, float& endX, float& topY, float& playline) const {
        topX = startX_[size_t(k)], endX = endX_[size_t(k)], topY = topY_, playline = playline_;
    }
    // A gem of fret `fret` (0 green .. 4 orange) of the note at `noteMs`: its
    // screen centre and sprite scale at song time `now`; false off the highway.
    bool gemAt(int32_t noteMs, int fret, double now, float& x, float& y, float& scale) const;
    // Battle attacks on this highway (Match::BattleSide) [C scripts
    // battle_lefty_notes, battle_lightning, hammer_highway]: gems drawn
    // mirrored, the fret buttons mirrored, gems hidden by Amp Overload's
    // flicker, the highway moved (shake / the hit's push); battle star gems
    // carry their phrase's card icon (`card`: the icon texture of a note, 0 none).
    struct Effects {
        bool leftyGems = false, leftyButtons = false;
        std::function<bool(size_t)> hidden;
        std::function<uint32_t(size_t)> card;
        float dx = 0, dy = 0;
        bool battle = false;  // star gems: sys_BattleGEM_* [C 0x82120C90]
    };
    void setEffects(Effects e) { fx_ = std::move(e); }
    // The five lanes for touch play (menu units): at the strike line and at
    // the far end, as drawn (lefty mirrors) [I: the fingers' map].
    void touchLanes(std::array<float, 5>& nearX, std::array<float, 5>& nearY, std::array<float, 5>& farX, std::array<float, 5>& farY) const {
        for (int k = 0; k < 5; ++k) {
            nearX[size_t(k)] = endX_[size_t(k)], nearY[size_t(k)] = playline_;
            farX[size_t(k)] = startX_[size_t(k)], farY[size_t(k)] = topY_;
        }
    }

private:
    void drawAll(Canvas& c, const TextureCache& tex, const NoteTrack& track, const Player& player, double nowMs, uint8_t held);
    float yAt(float v) const { return topY_ + height_ * v; }
    // The normalised depth of perspective row `r` (0 .. 1152; 1024 the playline).
    float rowDepth(float r) const;
    double rowOf(double noteMs, double nowMs) const;  // the row (0 top .. 1024 playline), unclamped
    float laneX(int lane, float v) const { return startX_[size_t(lane)] + (endX_[size_t(lane)] - startX_[size_t(lane)]) * v; }
    float alphaAt(float y) const;
    void quad(Canvas& c, uint32_t texKey, const TextureCache& tex, float cx, float cy, float w, float h, float justY, SDL_Color col,
              float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, Blend blend = Blend::Alpha) const;
    // A sprite turned by `rotDeg` (clockwise) about its anchor (just jx, jy in -1..1); w / h may be negative (mirrored).
    void sprite(Canvas& c, uint32_t texKey, const TextureCache& tex, float x, float y, float w, float h, float jx, float jy, float rotDeg,
                SDL_Color col, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, Blend blend = Blend::Alpha, bool fade = false) const;
    void drawStarPowerFx(Canvas& c, const TextureCache& tex, double now, bool under) const;
    // One sustain tail (the strip of 0x8238E338) of note `i` on lane `lane`.
    void drawTail(Canvas& c, const TextureCache& tex, const TrackNote& n, int i, int fret, int lane, const Player& player, double now) const;
    // Star Power at song time `ms` (a gem / tail takes its material when created) [C 0x82120C90].
    bool spAt(double ms) const;
    // The song time a note's gem and tail are created (row 0).
    double createdAt(double noteMs) const { return noteMs + gemOffsetMs_ - travelMs_; }
    void drawButtons(Canvas& c, const TextureCache& tex, uint8_t held, bool raised, double now) const;
    SDL_Color surfaceTint(double now) const;

    float playline_ = 655, height_ = 350, topWidth_ = 160, widthOffset_ = 2.2f, fade_ = 30;
    float cx_ = 640;    // the highway's centre (x)
    float baseX_ = 0;   // the container's x offset from the one-player layout
    Effects fx_;
    // Lefty Flip's button slide (animate_lefty_flip): the buttons as drawn
    // (lefthanded_button_ups), the one before and when it changed.
    mutable bool btnLefty_ = false, btnFrom_ = false, btnSeen_ = false;
    mutable double btnSlideAt_ = -1e9;
    float topY_ = 305, gemStart_ = 0.25f, gemEnd_ = 0.8f, starScale_ = 1.3f, gemJust_ = 0.83f, starJust_ = 0.5f;
    float fretbarStart_ = 0.15f, fretbarEnd_ = 0.48f, whammyTop_ = 10, whammyOffset_ = 1.8f;
    float sidebarXScale_ = 0.3f, sidebarYScale_ = 1, sidebarXOffset_ = 4;
    float nowScaleX_ = 0.8f, nowScaleY_ = 0.8f, stringScaleX_ = 0.65f, stringScaleY_ = 0.8f, spFxScale_ = 1;
    float neckSprite_ = 16, neckLipAdd_ = 16, neckLipBase_ = 5, buttonUpPixels_ = 20, buttonSinkMs_ = 100;
    float pulseMs_ = 200;  // highway_pulse_time
    float whammyCutoff_ = 1100;  // whammy_cutoff [C data]
    float whammyTweakMs_ = -33;  // whammy_offset_tweak [C data]
    // The tails' state by note index: dead (sys_Whammy2D_Dead, z 3.1, its
    // head moving on from where the hold left it), blue (star_power_whammy).
    struct Tail {
        bool dead = false, star = false;
        double releasedAt = -1;  // the hold's end (-1: never held)
    };
    std::vector<std::pair<int, Tail>> tails_;
    Tail* tail(int note);
    const Tail* tail(int note) const;
    int holdNote_ = -1;     // the note held (Player::sustaining) as last seen
    uint8_t holdFrets_ = 0;
    double holdStart_ = 0;  // WhammyFXOn's sys_whammy_hold01 created
    // star_power_whammy's locals [C script].
    int whammyOn_ = 0, whammyStarOn_ = 0, whammyStarOff_ = 0;
    float lastWhammy_ = 0;
    bool whammyFirst_ = true;
    std::vector<std::pair<double, bool>> spLog_;  // Star Power switched on / off at
    float travelMs_ = 1500;  // scroll_time - destroy_time
    std::array<float, 128> wibble_ = filled(1.0f);
    static std::array<float, 128> filled(float v) {
        std::array<float, 128> a;
        a.fill(v);
        return a;
    }
    std::array<float, 5> startX_{}, endX_{};
    std::vector<float> d_, accum_;  // per-row heights (normalised) and their running sum
    uint32_t surfaceTex_ = 0x2C0ED3FD;  // global fallback highway texture [C data]
    float tiling_ = 1.5f;                // gHighwayTiling1
    bool lefty_ = false;
    double gemOffsetMs_ = 0;
    // The intro (move_highway_2d, intro_buttonup_ripple) [C scripts].
    double startMs_ = 0;
    float containerY_ = 720, riseV_ = -720, riseK_ = 1;
    bool rising_ = false;
    int rippled_ = 0;
    // button_up_pixel_array<p> [C].
    std::array<float, 5> pixels_{};
    // The highway's rgba: highway_normal / highway_starpower / the miss pulse's morphs.
    bool spOn_ = false;
    double spOnAt_ = -1e18;
    double pulseAt_ = -1e18;
    int lastBaseMult_ = 1;
    // highway_pulse_multiplier_loss: the container's morphs [C script].
    struct Shake {
        double start = -1e18, stepMs = 100;
        std::array<float, 7> x{}, y{};  // the morphs' targets (0 and 6: the original position)
    } shake_;
    std::mt19937 rng_{0x47483304};
    struct Flame {
        double start = -1e9;
        bool blue = false;
    };
    std::array<Flame, 5> flames_{};
    // Hit sparks (hit_particle_params / star_hit_particle_params) [C data; motion I].
    struct Spark {
        double born = 0;
        float x = 0, y = 0, vx = 0, vy = 0;
        bool blue = false;
    };
    std::vector<Spark> sparks_;
    struct Emitter {
        double start = -1e9, next = 0;
        float x = 0, y = 0;
        bool blue = false;
    };
    std::array<Emitter, 5> emitters_{};
    // gem_container<p>'s battle morphs: one DoScreenElementMorph at a time
    // (a new one starts from where the last left the container) [C scripts; I: linear].
    struct Morph {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        double start = -1e18, ms = 0;
    } bm_;
    void battleOffset(double now, float& x, float& y) const;
    void battleMorph(double now, float x, float y, double ms);
    // hammer_highway: 0 idle, 1 out (the wait after the push), 2 back.
    struct Hammer {
        int state = 0;
        double until = 0, ms = 100;
        float ox = 0, oy = 0;
    } hammer_;
    // shake_highway: 0 not running, 1 waiting for the hammer, 2 the loop's
    // top (next frame), 3 waiting shake_frequency (0.05 s).
    struct ShakeScript {
        int state = 0;
        double until = 0;
        float ox = 0, oy = 0;
        bool pulseOn = false;
    } shakeScript_;
    bool wasShaking_ = false;
    void runShake(double now, bool shaking);
};
