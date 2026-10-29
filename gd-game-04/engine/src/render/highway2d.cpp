// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/highway2d.h"

#include <algorithm>
#include <cmath>

#include "data/qbkey.h"
#include "game/note_track.h"
#include "game/player.h"
#include "render/texture_cache.h"
#include "script/globals.h"

namespace {

// Texture keys in global_gfx.tex [C data; docs/highway.md].
constexpr uint32_t kGem[5] = {0x151EE874, 0x27DD8DC4, 0x39E35D20, 0xC715D48D, 0xA5A7F110};
constexpr uint32_t kGemHammer[5] = {0x59C6A636, 0xD51DD55E, 0x9E1B6D3E, 0xD326251A, 0x0243B96C};
constexpr uint32_t kGemStarPower = 0x6FB9F94E, kGemStarPowerHammer = 0x5BDA8E6A;
constexpr uint32_t kStar[5] = {0xC2C8127D, 0xBDB47035, 0xCE93E0D6, 0xC76F7566, 0xE8D694D6};
constexpr uint32_t kStarHammer[5] = {0x22E10D71, 0x5C5C9114, 0x76D89EA6, 0xB695B723, 0xEA804AF4};
constexpr uint32_t kWhammy[5] = {0xB47133C1, 0x6C97B6C6, 0xEC463E7A, 0x037E6D2C, 0xCA034A7A};
constexpr uint32_t kWhammyStarPower = 0xE78E1A35;
constexpr uint32_t kFretbarLarge = 0xC1832175, kFretbarMedium = 0x9C77551B;
constexpr uint32_t kSidebar = 0x7F2DACD6;
constexpr uint32_t kHead[5] = {0x42B576FB, 0x2A2FFBB4, 0x043BD094, 0xC9901DA2, 0x49DED95F};
constexpr uint32_t kHeadDown[5] = {0x31966E14, 0x8EDDCCC9, 0xB59DF82A, 0x7AEAC67E, 0xB966061B};

constexpr int kRows = 1152, kRealRows = 1024;  // highway_lines, real_highway_lines [C data]

float num(const Globals& g, const char* name, float fallback) { return qbNumber(g, qbkey::key(name), fallback); }

const qb::Value* arrayGlobal(const Globals& g, const char* name) {
    const qb::Value* v = g.find(qbkey::key(name));
    return v && v->type == qb::Type::Array && v->arr ? v : nullptr;
}

}  // namespace

bool Highway2D::init(const Globals& g, int difficulty) {
    // One player: the "...1" tweaks (generate_pos_table) [C script].
    playline_ = num(g, "highway_playline1", 655);
    height_ = num(g, "highway_height1", 350);
    topWidth_ = num(g, "highway_top_width1", 160);
    widthOffset_ = num(g, "widthOffsetFactor1", 2.2f);
    fade_ = num(g, "highway_fade1", 30);
    gemStart_ = num(g, "gem_start_scale1", 0.25f);
    gemEnd_ = gemStart_ * (1 + widthOffset_);
    starScale_ = num(g, "gem_star_scale1", 1.3f);
    gemJust_ = num(g, "gem_y_just1", 0.83f);
    starJust_ = num(g, "star_y_just1", 0.5f);
    fretbarStart_ = num(g, "fretbar_start_scale1", 0.15f);
    fretbarEnd_ = fretbarStart_ * (1 + widthOffset_);
    whammyTop_ = num(g, "whammy_top_width1", 10);
    whammyOffset_ = num(g, "whammy_width_offset1", 1.8f);
    sidebarXScale_ = num(g, "sidebar_x_scale1", 0.3f);
    sidebarYScale_ = num(g, "sidebar_y_scale1", 1);
    sidebarXOffset_ = num(g, "sidebar_x_offset1", 4);
    topY_ = playline_ - height_;
    const float bottomW = topWidth_ * (1 + widthOffset_);
    for (int k = 0; k < 5; ++k) {
        startX_[size_t(k)] = 640 - topWidth_ / 2 + topWidth_ / 5 * (float(k) + 0.5f);
        endX_[size_t(k)] = 640 - bottomW / 2 + bottomW / 5 * (float(k) + 0.5f);
    }
    // Travel time: scroll_time (difficulty_list_props) - destroy_time [C script setup_highway].
    static const char* const kDiff[4] = {"easy", "medium", "hard", "expert"};
    float scroll = 2.5f;
    if (const qb::Value* props = g.find(qbkey::key("difficulty_list_props")); props && props->st)
        if (const qb::Value* d = g.field(*props->st, qbkey::key(kDiff[std::clamp(difficulty, 0, 3)])); d && d->st)
            if (const qb::Value* s = d->st->find(qbkey::key("scroll_time"))) scroll = s->number();
    travelMs_ = (scroll - num(g, "destroy_time", 1.0f)) * 1000.0f;
    // The perspective rows [C script generate_pos_table].
    const qb::Value* ft = arrayGlobal(g, "heightPerspFactTable");
    const qb::Value* et = arrayGlobal(g, "heightPerspExpTable");
    if (!ft || !et) return false;
    const size_t hh = size_t(std::clamp(int(height_) - 162, 0, 510));
    if (hh >= ft->arr->values.size() || hh >= et->arr->values.size()) return false;
    const double F = ft->arr->values[hh].number(), E = et->arr->values[hh].number();
    std::vector<double> d(kRows);
    d[0] = 1;
    for (int i = 1; i < kRows; ++i) d[size_t(i)] = std::pow(d[size_t(i - 1)] * F, E);
    double sum = 0;
    for (int i = 0; i < kRealRows; ++i) sum += d[size_t(i)];
    d_.assign(kRows, 0);
    accum_.assign(kRows, 0);
    for (int i = 0; i < kRows; ++i) d_[size_t(i)] = float(d[size_t(i)] / sum);
    for (int i = 1; i < kRows; ++i) accum_[size_t(i)] = accum_[size_t(i - 1)] + d_[size_t(i)];
    return travelMs_ > 0;
}

float Highway2D::depthOf(double noteMs, double nowMs) const {
    // e = (now - note) + T; row = e / T x 1024 [C 0x821190B8, 0x8210E750]. The
    // 360's gem offset (-66 ms) is left out on PC, like the input offset [I].
    const double r = ((nowMs - noteMs) + travelMs_) / travelMs_ * kRealRows;
    if (r < 0) return -1;
    if (r >= kRows - 1) return accum_.back() + 1;
    const int i = int(r);
    const float f = float(r - i);
    return accum_[size_t(i)] + (d_[size_t(i + 1)] - d_[size_t(i)]) * f;
}

float Highway2D::alphaAt(float y) const {
    // Transparent at the top edge, opaque highway_fade px below [I: m_startfade / m_endfade].
    return std::clamp((y - topY_) / std::max(1.0f, fade_), 0.0f, 1.0f);
}

void Highway2D::quad(Canvas& c, uint32_t key, const TextureCache& tex, float cx, float cy, float w, float h, float justY, SDL_Color col,
                     float u0, float v0, float u1, float v1, Blend blend) const {
    const gfx::TextureId id = tex.find(key);
    const float x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - h * (justY + 1) / 2, y1 = y0 + h;
    const Vertex a{x0, y0, 0, u0, v0, col.r, col.g, col.b, col.a}, b{x1, y0, 0, u1, v0, col.r, col.g, col.b, col.a};
    const Vertex cc{x1, y1, 0, u1, v1, col.r, col.g, col.b, col.a}, d{x0, y1, 0, u0, v1, col.r, col.g, col.b, col.a};
    const Vertex v[6] = {a, b, cc, a, cc, d};
    c.draw(v, 6, int(id.id), blend);
}

void Highway2D::draw(Canvas& c, const TextureCache& tex, const NoteTrack& track, const Player& player, double now, uint8_t held) {
    const PlayerStats& st = player.stats();
    // The highway surface. GH3 draws the character's highway texture
    // (e.g. sys_axel_1_highway) as row strips scrolling with the gems [C
    // 0x8238DE48]; the character paks are not loaded yet, so a dark surface
    // stands in [approximation, backlog GD04-018].
    {
        const SDL_Color base = st.starPowerActive ? SDL_Color{20, 70, 90, 255} : SDL_Color{28, 28, 34, 255};
        const int strips = 32;
        for (int s = 0; s < strips; ++s) {
            const float va = float(s) / strips * 1.6f, vb = float(s + 1) / strips * 1.6f;
            const float ya = yAt(va), yb = yAt(vb);
            if (ya > 720) break;
            const float wa = topWidth_ * (1 + widthOffset_ * va), wb = topWidth_ * (1 + widthOffset_ * vb);
            const auto A = uint8_t(255 * alphaAt(ya)), B = uint8_t(255 * alphaAt(yb));
            const Vertex p0{640 - wa / 2, ya, 0, 0, 0, base.r, base.g, base.b, A}, p1{640 + wa / 2, ya, 0, 0, 0, base.r, base.g, base.b, A};
            const Vertex p2{640 + wb / 2, yb, 0, 0, 0, base.r, base.g, base.b, B}, p3{640 - wb / 2, yb, 0, 0, 0, base.r, base.g, base.b, B};
            const Vertex v[6] = {p0, p1, p2, p0, p2, p3};
            c.draw(v, 6, 0, Blend::Alpha);
        }
    }
    // Sidebars: sys_sidebar2D, scale (sidebar_x_scale, sidebar_y_scale), at
    // (sidebar_x, sidebar_y) rotated by the edge angle; the colour alternates
    // per beat between white and the state's colour [C script setup_highway].
    {
        int beat = 0;
        for (size_t i = 0; i < track.fretbars.size() && track.fretbars[i] <= now; ++i) beat = int(i);
        SDL_Color second{192, 255, 255, 255};
        if (st.starPowerActive) second = {192, 255, 255, 255};
        else if (st.health <= 0.333f) second = {255, 80, 80, 255};
        else if (st.starPower >= 50) second = {128, 192, 255, 255};
        const SDL_Color col = (beat % 2) ? second : SDL_Color{255, 255, 255, 255};
        int tw = 32, th = 512;
        tex.size(kSidebar, tw, th);
        const float bottomW = topWidth_ * (1 + widthOffset_);
        const float stx = 640 - topWidth_ / 2, sbx = 640 - bottomW / 2;
        const float sx = sbx + (sbx - stx) * 0.25f - sidebarXOffset_, sy = playline_ + height_ * 0.25f;
        const float ang = std::atan2(height_, stx - sbx);  // from the x axis
        const float w = float(tw) * sidebarXScale_, h = float(th) * sidebarYScale_;
        for (int side = 0; side < 2; ++side) {
            const float dirx = std::cos(ang) * (side ? -1.0f : 1.0f), diry = -std::sin(ang);  // up the edge
            const float nx = -diry, ny = dirx;  // across
            const float cx = side ? 1280 - sx : sx;
            auto P = [&](float a, float b, float u, float v) {
                return Vertex{cx + dirx * a + nx * b, sy + diry * a + ny * b, 0, u, v, col.r, col.g, col.b,
                              uint8_t(255 * alphaAt(sy + diry * a))};
            };
            // Anchored at its bottom end (sidebar_y is below the playline, the
            // bar runs up the highway's edge) [I: the element's just].
            const Vertex a = P(h, -w / 2, 0, 0), b = P(h, w / 2, 1, 0), cc = P(0, w / 2, 1, 1), d = P(0, -w / 2, 0, 1);
            const Vertex v[6] = {a, b, cc, a, cc, d};
            c.draw(v, 6, int(tex.find(kSidebar).id), Blend::Alpha);
        }
    }
    // Fretbars: large on the first beat of a measure, medium on the others,
    // down the centre at fretbar_start_scale -> end scale [C 0x821111B8, 0x82121E00].
    {
        int numerator = 4, since = 0;
        size_t ts = 0;
        for (size_t i = 0; i < track.fretbars.size(); ++i) {
            while (ts < track.timesig.size() && track.timesig[ts].time <= track.fretbars[i]) {
                numerator = std::max(1, track.timesig[ts].num);
                since = int(i);
                ++ts;
            }
            const float v = depthOf(track.fretbars[i], now);
            if (v < 0) break;
            if (v > 1.6f) continue;
            const bool measure = (int(i) - since) % numerator == 0;
            const uint32_t key = measure ? kFretbarLarge : kFretbarMedium;
            int tw = 512, th = 32;
            tex.size(key, tw, th);
            const float s = fretbarStart_ + (fretbarEnd_ - fretbarStart_) * v;
            const float y = yAt(v);
            quad(c, key, tex, 640, y, float(tw) * s, float(th) * s, 0, SDL_Color{255, 255, 255, uint8_t(255 * alphaAt(y))});
        }
    }
    // Sustain tails, then gems, back to front [C z: tails 4.0, gems 4.1].
    const size_t first = player.current() > 8 ? player.current() - 8 : 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = track.notes.size(); i-- > first;) {
            const TrackNote& n = track.notes[i];
            const float v = depthOf(n.time, now);
            if (v < 0 || v > accum_.back()) continue;
            const bool hit = i < player.current() && (player.sustaining() != int(i));
            const bool missed = i < player.current() && !hit && player.sustaining() != int(i);
            (void)missed;
            for (int l = 0; l < 5; ++l) {
                if (!(n.frets & (1 << l))) continue;
                if (pass == 0 && n.sustain) {
                    // The tail: from the gem (pinned at the playline while held [I:
                    // row 1100, whammy_cutoff]) to the end of the hold.
                    float vHead = v;
                    if (player.sustaining() == int(i)) vHead = std::min(v, 1.0f);
                    const float vTail = depthOf(double(n.time) + n.holdMs, now);
                    if (vTail < 0 && vHead < 0) continue;
                    const uint32_t key = st.starPowerActive ? kWhammyStarPower : kWhammy[l];
                    const int segs = 12;
                    const float a0 = std::max(0.0f, vTail), a1 = vHead;
                    for (int s = 0; s < segs; ++s) {
                        const float va = a0 + (a1 - a0) * float(s) / segs, vb = a0 + (a1 - a0) * float(s + 1) / segs;
                        const float wa = whammyTop_ * (1 + whammyOffset_ * va), wb = whammyTop_ * (1 + whammyOffset_ * vb);
                        const float xa = laneX(l, va), xb = laneX(l, vb), ya = yAt(va), yb = yAt(vb);
                        const auto A = uint8_t(255 * alphaAt(ya)), B = uint8_t(255 * alphaAt(yb));
                        const Vertex p0{xa - wa, ya, 0, 0, 0, 255, 255, 255, A}, p1{xa + wa, ya, 0, 1, 0, 255, 255, 255, A};
                        const Vertex p2{xb + wb, yb, 0, 1, 1, 255, 255, 255, B}, p3{xb - wb, yb, 0, 0, 1, 255, 255, 255, B};
                        const Vertex q[6] = {p0, p1, p2, p0, p2, p3};
                        c.draw(q, 6, int(tex.find(key).id), Blend::Alpha);
                    }
                }
                if (pass == 1 && i >= player.current()) {
                    // The gem sprite [C 0x82120C90]: Star Power / star / normal,
                    // hammer variants for HOPOs; star sprites are 4x4 flip-books [C data].
                    const bool star = n.phrase >= 0;
                    uint32_t key;
                    float u1 = 1, v1 = 1, u0 = 0, v0 = 0, scale = gemStart_ + (gemEnd_ - gemStart_) * v, just = gemJust_;
                    if (st.starPowerActive) key = n.hopo ? kGemStarPowerHammer : kGemStarPower;
                    else if (star) {
                        key = n.hopo ? kStarHammer[l] : kStar[l];
                        const int frame = int(now / 1000.0 * 30) % 16;  // 4x4 @ 30 fps [C data; frame order I]
                        u0 = float(frame % 4) / 4, v0 = float(frame / 4) / 4, u1 = u0 + 0.25f, v1 = v0 + 0.25f;
                        scale *= starScale_;
                        just = starJust_;
                    } else key = n.hopo ? kGemHammer[l] : kGem[l];
                    int tw = 128, th = 64;
                    tex.size(key, tw, th);
                    const float fw = float(tw) * (u1 - u0), fh = float(th) * (v1 - v0);
                    const float y = yAt(v);
                    quad(c, key, tex, laneX(l, v), y, fw * scale, fh * scale, just, SDL_Color{255, 255, 255, uint8_t(255 * alphaAt(y))},
                         u0, v0, u1, v1);
                }
            }
        }
    }
    // The strike heads at the playline: lit when held [C script setup_highway; scale I].
    for (int l = 0; l < 5; ++l) {
        const uint32_t key = (held & (1 << l)) ? kHeadDown[l] : kHead[l];
        int tw = 128, th = 64;
        tex.size(key, tw, th);
        quad(c, key, tex, endX_[size_t(l)], playline_, float(tw) * gemEnd_, float(th) * gemEnd_, gemJust_, SDL_Color{255, 255, 255, 255});
    }
}
