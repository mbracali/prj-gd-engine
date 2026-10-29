// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "render/canvas.h"

class Globals;
class TextureCache;
struct NoteTrack;
class Player;

// GH3's gameplay highway, as its scripts and C code lay it out (docs/
// highway.md): a 2D perspective trapezoid built by generate_pos_table from
// guitar_tweaks.qb (one player: playline 655, height 350, top width 160,
// widthOffsetFactor 2.2), 1152 perspective rows (heightPerspFact/Exp
// tables of highway_2d.qb), gems moving top to playline in scroll_time -
// destroy_time, sprites from global_gfx.tex. One player only for now.
class Highway2D {
public:
    // Reads the tweaks and builds the row tables; false if data is missing.
    // `players` 1 or 2 (two highways side by side; `player` 0 left, 1 right).
    bool init(const Globals& g, int difficulty, int players = 1, int player = 0);
    // Draws the highway, fretbars, sidebars, tails, gems and strike heads
    // at song time `nowMs` (ms). `held`: the frets held now.
    void draw(Canvas& c, const TextureCache& tex, const NoteTrack& track, const Player& player, double nowMs, uint8_t held);
    // A note was hit on these frets at song time `nowMs`: a hit flame per fret.
    void noteHit(uint8_t frets, double nowMs, bool starPower);
    // Counters for the walk / self checks.
    uint64_t flamesStarted = 0;
    // The screen position (y) of a note time at song time `nowMs`, and its
    // normalised depth v (0 top, 1 playline) — for the checks.
    float depthOf(double noteMs, double nowMs) const;
    // The highway surface's texture (the guitarist's highway material's, else
    // the global fallback) [C data].
    void setSurfaceTexture(uint32_t key) { surfaceTex_ = key; }
    // A gem's time on screen: scroll_time - destroy_time (ms).
    float travelMs() const { return travelMs_; }
    // Battle attacks on this highway (Match::BattleSide) [C scripts
    // battle_lefty_notes, battle_lightning, hammer_highway]: gems drawn
    // mirrored, the fret buttons mirrored, gems hidden by Amp Overload's
    // flicker, the highway moved (shake / the hit's push).
    struct Effects {
        bool leftyGems = false, leftyButtons = false;
        std::function<bool(size_t)> hidden;
        float dx = 0, dy = 0;
    };
    void setEffects(Effects e) { fx_ = std::move(e); }

private:
    void drawAll(Canvas& c, const TextureCache& tex, const NoteTrack& track, const Player& player, double nowMs, uint8_t held);
    float yAt(float v) const { return topY_ + height_ * v; }
    float laneX(int lane, float v) const { return startX_[size_t(lane)] + (endX_[size_t(lane)] - startX_[size_t(lane)]) * v; }
    float alphaAt(float y) const;
    void quad(Canvas& c, uint32_t texKey, const TextureCache& tex, float cx, float cy, float w, float h, float justY, SDL_Color col,
              float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, Blend blend = Blend::Alpha) const;

    float playline_ = 655, height_ = 350, topWidth_ = 160, widthOffset_ = 2.2f, fade_ = 30;
    float cx_ = 640;  // the highway's centre (x)
    Effects fx_;
    float topY_ = 305, gemStart_ = 0.25f, gemEnd_ = 0.8f, starScale_ = 1.3f, gemJust_ = 0.83f, starJust_ = 0.5f;
    float fretbarStart_ = 0.15f, fretbarEnd_ = 0.48f, whammyTop_ = 10, whammyOffset_ = 1.8f;
    float sidebarXScale_ = 0.3f, sidebarYScale_ = 1, sidebarXOffset_ = 4;
    float travelMs_ = 1500;  // scroll_time - destroy_time
    std::array<float, 5> startX_{}, endX_{};
    std::vector<float> d_, accum_;
    uint32_t surfaceTex_ = 0x2C0ED3FD;
    struct Flame {
        double start = -1e9;
        bool blue = false;
    };
    std::array<Flame, 5> flames_{};  // global fallback highway texture [C data]
    float tiling_ = 1.5f;              // gHighwayTiling1  // per-row heights (normalised) and their running sum
};
