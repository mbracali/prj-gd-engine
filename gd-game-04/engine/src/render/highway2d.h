// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
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
    bool init(const Globals& g, int difficulty);
    // Draws the highway, fretbars, sidebars, tails, gems and strike heads
    // at song time `nowMs` (ms). `held`: the frets held now.
    void draw(Canvas& c, const TextureCache& tex, const NoteTrack& track, const Player& player, double nowMs, uint8_t held);
    // The screen position (y) of a note time at song time `nowMs`, and its
    // normalised depth v (0 top, 1 playline) — for the checks.
    float depthOf(double noteMs, double nowMs) const;

private:
    float yAt(float v) const { return topY_ + height_ * v; }
    float laneX(int lane, float v) const { return startX_[size_t(lane)] + (endX_[size_t(lane)] - startX_[size_t(lane)]) * v; }
    float alphaAt(float y) const;
    void quad(Canvas& c, uint32_t texKey, const TextureCache& tex, float cx, float cy, float w, float h, float justY, SDL_Color col,
              float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, Blend blend = Blend::Alpha) const;

    float playline_ = 655, height_ = 350, topWidth_ = 160, widthOffset_ = 2.2f, fade_ = 30;
    float topY_ = 305, gemStart_ = 0.25f, gemEnd_ = 0.8f, starScale_ = 1.3f, gemJust_ = 0.83f, starJust_ = 0.5f;
    float fretbarStart_ = 0.15f, fretbarEnd_ = 0.48f, whammyTop_ = 10, whammyOffset_ = 1.8f;
    float sidebarXScale_ = 0.3f, sidebarYScale_ = 1, sidebarXOffset_ = 4;
    float travelMs_ = 1500;  // scroll_time - destroy_time
    std::array<float, 5> startX_{}, endX_{};
    std::vector<float> d_, accum_;  // per-row heights (normalised) and their running sum
};
