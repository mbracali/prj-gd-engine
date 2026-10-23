// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/highway.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kHorizonY = 110, kStrikeY = 600, kHalfWidth = 250, kDepth = 3.0f;

const SDL_Color kFretColors[5] = {
    {40, 200, 60, 255}, {230, 40, 40, 255}, {240, 220, 40, 255}, {40, 110, 240, 255}, {250, 140, 30, 255}};
const SDL_Color kStarColor{120, 220, 255, 255};
const SDL_Color kWhite{255, 255, 255, 255};

SDL_Color scaled(SDL_Color c, float k) {
    auto f = [k](Uint8 v) { return static_cast<Uint8>(std::clamp(v * k, 0.0f, 255.0f)); };
    return {f(c.r), f(c.g), f(c.b), c.a};
}

float laneX(int fret) { return (fret - 2) * 0.4f; }

}  // namespace

Highway::Point Highway::project(float lane, float dtMs) const {
    float z = dtMs / aheadMs_;
    float s = 1.0f / (1.0f + kDepth * z);
    float sFar = 1.0f / (1.0f + kDepth);
    float y = kHorizonY + (kStrikeY - kHorizonY) * (s - sFar) / (1.0f - sFar);
    return {kScreenW * 0.5f + lane * kHalfWidth * s, y, s};
}

void Highway::quad(Point a, Point b, float halfWidth, SDL_Color c) {
    float wa = halfWidth * kHalfWidth * a.scale, wb = halfWidth * kHalfWidth * b.scale;
    Vertex p0{a.x - wa, a.y, 0, 0, 0, c.r, c.g, c.b, c.a}, p1{a.x + wa, a.y, 0, 1, 0, c.r, c.g, c.b, c.a};
    Vertex p2{b.x + wb, b.y, 0, 1, 1, c.r, c.g, c.b, c.a}, p3{b.x - wb, b.y, 0, 0, 1, c.r, c.g, c.b, c.a};
    batch_.insert(batch_.end(), {p0, p1, p2, p0, p2, p3});
}

void Highway::disc(float x, float y, float rx, float ry, SDL_Color c) {
    constexpr int kSeg = 20;
    Vertex center{x, y, 0, 0.5f, 0.5f, c.r, c.g, c.b, c.a};
    for (int i = 0; i < kSeg; ++i) {
        float a0 = i * 6.2831853f / kSeg, a1 = (i + 1) * 6.2831853f / kSeg;
        Vertex p0{x + std::cos(a0) * rx, y + std::sin(a0) * ry, 0, 0, 0, c.r, c.g, c.b, c.a};
        Vertex p1{x + std::cos(a1) * rx, y + std::sin(a1) * ry, 0, 0, 0, c.r, c.g, c.b, c.a};
        batch_.insert(batch_.end(), {center, p0, p1});
    }
}

void Highway::draw(Renderer& r, const Song& song, const Player& player, float nowMs, float trackSpeed,
                   const std::string& title, const std::vector<float>& hitFlashes) {
    batch_.clear();
    aheadMs_ = 1600.0f / trackSpeed;
    const float behindMs = -aheadMs_ * 0.08f;

    // Track surface and lane lines.
    bool star = player.starPowerActive();
    quad(project(0, behindMs), project(0, aheadMs_), 1.08f, star ? SDL_Color{20, 40, 70, 255} : SDL_Color{28, 24, 30, 255});
    for (int f = 0; f <= 5; ++f) {
        float x = (f - 2.5f) * 0.4f;
        quad(project(x, behindMs), project(x, aheadMs_), 0.006f, {70, 64, 74, 255});
    }
    for (const BeatLine& b : song.beats) {
        float dt = b.ms - nowMs;
        if (dt < behindMs || dt > aheadMs_) continue;
        Point p = project(0, dt);
        float w = 1.0f * kHalfWidth * p.scale;
        float h = b.measure ? 3.0f : 1.5f;
        SDL_Color lc = b.measure ? SDL_Color{140, 130, 150, 255} : SDL_Color{80, 74, 86, 255};
        Vertex q0{p.x - w, p.y - 1, 0, 0, 0, lc.r, lc.g, lc.b, 255}, q1{p.x + w, p.y - 1, 0, 0, 0, lc.r, lc.g, lc.b, 255};
        Vertex q2{p.x + w, p.y - 1 + h, 0, 0, 0, lc.r, lc.g, lc.b, 255}, q3{p.x - w, p.y - 1 + h, 0, 0, 0, lc.r, lc.g, lc.b, 255};
        batch_.insert(batch_.end(), {q0, q1, q2, q0, q2, q3});
    }

    // Fret buttons at the strike line.
    for (int f = 0; f < 5; ++f) {
        Point p = project(laneX(f), 0);
        bool held = player.heldFrets() & (1u << f);
        disc(p.x, p.y, 34, 15, scaled(kFretColors[f], 0.35f));
        disc(p.x, p.y, 27, 11, held ? kFretColors[f] : SDL_Color{20, 20, 24, 255});
        float flash = f < static_cast<int>(hitFlashes.size()) ? hitFlashes[f] : 0;
        if (flash > 0) disc(p.x, p.y - 6, 40 * flash, 22 * flash, {255, 240, 200, static_cast<Uint8>(200 * flash)});
    }

    // Gems and sustains, far to near so nearer gems draw on top.
    const auto& gems = player.chart().gems;
    auto first = std::lower_bound(gems.begin(), gems.end(), nowMs - 4000.0f, [](const Gem& g, float v) { return g.ms < v; });
    std::vector<int> visible;
    for (auto it = first; it != gems.end() && it->ms - nowMs <= aheadMs_; ++it) visible.push_back(int(it - gems.begin()));
    for (auto vi = visible.rbegin(); vi != visible.rend(); ++vi) {
        int i = *vi;
        const Gem& g = gems[i];
        bool isHit = player.gemHit(i);
        bool starGem = player.phraseIntact(g.phrase);
        bool sustaining = player.sustainGem() == i;
        const bool hopo = g.hopo && i >= 2;  // the HOPO look skips the first two gems (FUN_0015d840)
        for (int f = 0; f < 5; ++f) {
            if (!(g.frets & (1u << f))) continue;
            SDL_Color c = starGem ? kStarColor : kFretColors[f];
            if (!g.noSustain) {
                float start = std::max(g.ms - nowMs, isHit ? 0.0f : behindMs);
                float end = std::min(g.ms + g.durationMs - nowMs, aheadMs_);
                if (end > start && (!isHit || sustaining)) {
                    float wob = sustaining ? 0.012f * std::sin(nowMs * 0.05f) : 0.0f;
                    quad(project(laneX(f) + wob, start), project(laneX(f), end), 0.05f,
                         sustaining ? c : scaled(c, 0.6f));
                }
            }
            if (isHit) continue;
            float dt = g.ms - nowMs;
            if (dt < behindMs) continue;
            Point p = project(laneX(f), dt);
            float rx = 30 * p.scale, ry = 13 * p.scale;
            disc(p.x, p.y + 3 * p.scale, rx, ry, scaled(c, 0.45f));
            disc(p.x, p.y, rx * 0.92f, ry * 0.92f, c);
            disc(p.x, p.y - 1 * p.scale, rx * (hopo ? 0.55f : 0.35f), ry * (hopo ? 0.55f : 0.35f),
                 hopo ? kWhite : scaled(c, 1.5f));
        }
    }

    r.draw(batch_.data(), batch_.size(), 0, Blend::Alpha);
    drawHud(r, player, title);
}

void Highway::drawHud(Renderer& r, const Player& player, const std::string& title) {
    bool star = player.starPowerActive();
    char buf[64];
    r.text(24, 20, 2, {220, 220, 230, 255}, title);
    std::snprintf(buf, sizeof buf, "%d", player.score());
    r.text(1000, 120, 4, kWhite, buf);
    std::snprintf(buf, sizeof buf, "x%d", player.multiplier());
    r.text(1000, 170, 5, star ? kStarColor : SDL_Color{255, 210, 80, 255}, buf);
    std::snprintf(buf, sizeof buf, "streak %d", player.streak());
    r.text(1000, 220, 2, {200, 200, 210, 255}, buf);

    // Rock meter: red / yellow / green zones with a needle.
    float mx = 80, my = 200, mh = 300, mw = 26;
    float warn = player.warningLevel();
    r.fillRect(mx, my, mw, mh * (1 - 0.66f), {40, 160, 60, 255});
    r.fillRect(mx, my + mh * (1 - 0.66f), mw, mh * (0.66f - warn), {200, 180, 40, 255});
    r.fillRect(mx, my + mh * (1 - warn), mw, mh * warn, {190, 40, 40, 255});
    float ny = my + mh * (1 - player.rockMeter());
    r.fillRect(mx - 10, ny - 3, mw + 20, 6, kWhite);
    r.text(mx - 18, my + mh + 12, 1.5f, {200, 200, 210, 255}, "ROCK");

    // Star Power meter.
    float sx = 140, sw = 18;
    r.fillRect(sx, my, sw, mh, {30, 30, 40, 255});
    r.fillRect(sx, my + mh * (1 - player.starMeter()), sw, mh * player.starMeter(),
             player.starPowerReady() || star ? kStarColor : SDL_Color{60, 120, 160, 255});
    r.fillRect(sx - 4, my + mh * 0.5f - 1, sw + 8, 2, kWhite);
    r.text(sx - 6, my + mh + 12, 1.5f, {200, 200, 210, 255}, "SP");
    if (player.starPowerReady())
        r.textCentered(kScreenW * 0.5f, 650, 2, kStarColor, "STAR POWER READY");
}
