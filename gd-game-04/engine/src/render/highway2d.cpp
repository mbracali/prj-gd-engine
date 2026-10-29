// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/highway2d.h"

#include <algorithm>
#include <cmath>
#include <string>

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
constexpr uint32_t kBattle[5] = {0x06485C80, 0xAE484472, 0xD9987563, 0xCEA8D3F0, 0x36381741};
constexpr uint32_t kBattleHammer[5] = {0x4E605076, 0xDADBCA9A, 0xAB496E94, 0xD6C32766, 0x486C2688};
constexpr uint32_t kWhammy[5] = {0xB47133C1, 0x6C97B6C6, 0xEC463E7A, 0x037E6D2C, 0xCA034A7A};
constexpr uint32_t kWhammyStarPower = 0xE78E1A35;
constexpr uint32_t kFretbarLarge = 0xC1832175, kFretbarMedium = 0x9C77551B;
constexpr uint32_t kSidebar = 0x7F2DACD6, kSpGlow = 0x72B4DF7E, kBolt = 0x9D12571C, kString = 0x2ED9AC77;
constexpr uint32_t kHead[5] = {0x42B576FB, 0x2A2FFBB4, 0x043BD094, 0xC9901DA2, 0x49DED95F};
constexpr uint32_t kHeadLit[5] = {0xDF92D29E, 0x39991412, 0x02D920F1, 0xCDAE1EA5, 0x0E22DEC0};
constexpr uint32_t kHeadDown[5] = {0x31966E14, 0x8EDDCCC9, 0xB59DF82A, 0x7AEAC67E, 0xB966061B};
constexpr uint32_t kLip[5] = {0xCABC5F5B, 0x46C4F2B5, 0x6A87F755, 0x7DCDB59B, 0x6AA1B221};
constexpr uint32_t kMid[5] = {0x8DA374D1, 0xE539F99E, 0xCB2DD2BE, 0x06861F88, 0x86C8DB75};
constexpr uint32_t kNeck = 0x90C16FBD;
constexpr uint32_t kSpark = 0x44E7B154;

constexpr uint32_t kHitFlame = 0xA38B2820, kHitFlameBlue = 0x9E700D1C;  // sys_note_hit01 / _blue_01 [C data]

constexpr int kRows = 1152, kRealRows = 1024;  // highway_lines, real_highway_lines [C data]

float num(const Globals& g, const char* name, float fallback) { return qbNumber(g, qbkey::key(name), fallback); }

const qb::Value* arrayGlobal(const Globals& g, const char* name) {
    const qb::Value* v = g.find(qbkey::key(name));
    return v && v->type == qb::Type::Array && v->arr ? v : nullptr;
}

SDL_Color mix(SDL_Color a, SDL_Color b, float f) {
    f = std::clamp(f, 0.0f, 1.0f);
    auto m = [f](uint8_t x, uint8_t y) { return uint8_t(int(float(x) + (float(y) - float(x)) * f)); };
    return SDL_Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

constexpr SDL_Color kNormal{255, 255, 255, 255}, kStarTint{64, 255, 255, 255}, kPulse{0, 0, 0, 255};  // highway_normal / _starpower / _pulse [C data]

}  // namespace

bool Highway2D::init(const Globals& g, int difficulty, int players, int player) {
    // One player: the "...1" tweaks; two players: the "...2" ones, each
    // highway's container moved by x_offset_p2 left (player 1) or right
    // (player 2) (generate_pos_table, setup_highway) [C script].
    const bool two = players == 2;
    auto tw = [&](const char* name, float fallback) { return num(g, (std::string(name) + (two ? "2" : "1")).c_str(), fallback); };
    playline_ = tw("highway_playline", 655);
    height_ = tw("highway_height", 350);
    topWidth_ = tw("highway_top_width", 160);
    widthOffset_ = tw("widthOffsetFactor", 2.2f);
    fade_ = tw("highway_fade", 30);
    tiling_ = tw("gHighwayTiling", 1.5f);
    gemStart_ = tw("gem_start_scale", 0.25f);
    gemEnd_ = gemStart_ * (1 + widthOffset_);
    starScale_ = tw("gem_star_scale", 1.3f);
    gemJust_ = tw("gem_y_just", 0.83f);
    starJust_ = tw("star_y_just", 0.5f);
    fretbarStart_ = tw("fretbar_start_scale", 0.15f);
    fretbarEnd_ = fretbarStart_ * (1 + widthOffset_);
    whammyTop_ = tw("whammy_top_width", 10);
    whammyOffset_ = tw("whammy_width_offset", 1.8f);
    sidebarXScale_ = tw("sidebar_x_scale", 0.3f);
    sidebarYScale_ = tw("sidebar_y_scale", 1);
    sidebarXOffset_ = tw("sidebar_x_offset", 4);
    nowScaleX_ = tw("nowbar_scale_x", 0.8f);
    nowScaleY_ = tw("nowbar_scale_y", 0.8f);
    stringScaleX_ = tw("string_scale_x", 0.65f);
    stringScaleY_ = tw("string_scale_y", 0.8f);
    spFxScale_ = tw("starpower_fx_scale", 1.0f);
    neckSprite_ = num(g, "neck_sprite_size", 16);
    neckLipAdd_ = num(g, "neck_lip_add", 16);
    neckLipBase_ = num(g, "neck_lip_base", 5);
    buttonUpPixels_ = num(g, "button_up_pixels", 20);
    buttonSinkMs_ = num(g, "button_sink_time", 0.1f) * 1000.0f;
    pulseMs_ = num(g, "highway_pulse_time", 0.2f) * 1000.0f;
    // The lanes in the container's space (x centre 640); the container moves.
    cx_ = 640;
    baseX_ = two ? (player == 0 ? -1.0f : 1.0f) * num(g, "x_offset_p2", 225) : 0.0f;
    topY_ = playline_ - height_;
    const float bottomW = topWidth_ * (1 + widthOffset_);
    for (int k = 0; k < 5; ++k) {
        startX_[size_t(k)] = cx_ - topWidth_ / 2 + topWidth_ / 5 * (float(k) + 0.5f);
        endX_[size_t(k)] = cx_ - bottomW / 2 + bottomW / 5 * (float(k) + 0.5f);
    }
    // Travel time: scroll_time (difficulty_list_props) - destroy_time [C script setup_highway].
    static const char* const kDiff[4] = {"easy", "medium", "hard", "expert"};
    float scroll = 2.5f;
    if (const qb::Value* props = g.find(qbkey::key("difficulty_list_props")); props && props->st)
        if (const qb::Value* d = g.field(*props->st, qbkey::key(kDiff[std::clamp(difficulty, 0, 3)])); d && d->st)
            if (const qb::Value* s = d->st->find(qbkey::key("scroll_time"))) scroll = s->number();
    // Two players: scroll_time x p2_scroll_time_factor [C script difficulty_setup].
    if (two) scroll *= num(g, "p2_scroll_time_factor", 0.8f);
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

void Highway2D::setStart(double startMs, double firstMs) {
    startMs_ = startMs;
    // The container waits at y 720 for the intro's highway_start_time
    // (fastintro -2000 ms); a start already past it begins raised [C scripts].
    containerY_ = 720, riseV_ = -720, riseK_ = 1, rising_ = false, rippled_ = 0;
    pixels_ = {};
    if (firstMs > startMs + 1000) containerY_ = 0, rippled_ = 6;
}

void Highway2D::setContainerX(float x) { baseX_ = x; }

float Highway2D::depthOf(double noteMs, double nowMs) const {
    // e = (now - (note - gem_offset)) + T; row = e / T x 1024 [C 0x821190B8,
    // 0x8210E750]. The 360's default gem offset (xenon_gem_offset -66 ms) is
    // left out on PC (D7); the song's gem_offset is kept.
    const double r = ((nowMs - noteMs - gemOffsetMs_) + travelMs_) / travelMs_ * kRealRows;
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

void Highway2D::sprite(Canvas& c, uint32_t key, const TextureCache& tex, float x, float y, float w, float h, float jx, float jy, float rotDeg,
                       SDL_Color col, float u0, float v0, float u1, float v1, Blend blend, bool fade) const {
    const gfx::TextureId id = tex.find(key);
    if (!id.id) return;
    const float l = -(jx + 1) / 2 * w, t = -(jy + 1) / 2 * h;
    const float a = rotDeg * float(M_PI) / 180, cs = std::cos(a), sn = std::sin(a);
    auto P = [&](float lx, float ly, float u, float v) {
        const float px = x + lx * cs - ly * sn, py = y + lx * sn + ly * cs;
        const uint8_t al = fade ? uint8_t(float(col.a) * alphaAt(py)) : col.a;
        return Vertex{px, py, 0, u, v, col.r, col.g, col.b, al};
    };
    const Vertex A = P(l, t, u0, v0), B = P(l + w, t, u1, v0), C = P(l + w, t + h, u1, v1), D = P(l, t + h, u0, v1);
    const Vertex q[6] = {A, B, C, A, C, D};
    c.draw(q, 6, int(id.id), blend);
}

void Highway2D::noteHit(uint8_t frets, uint8_t held, double now, bool starPower) {
    // The pixels: the frets of the note held (a chord), a single note's fret
    // and the held frets above it (towards orange) [C 0x8211C9D0].
    uint8_t up = uint8_t(held & frets);
    const bool single = frets && !(frets & (frets - 1));
    if (single)
        for (int l = 0; l < 5; ++l)
            if (frets & (1 << l)) up = uint8_t(held & (0x1F << l) & 0x1F);
    if (!held) up = frets;  // autoplay's frame without the frets shown down
    for (int l = 0; l < 5; ++l) {
        if (up & (1 << l)) pixels_[size_t(l)] = buttonUpPixels_;
        if (frets & (1 << l)) {
            flames_[size_t(l)] = Flame{now, starPower};
            emitters_[size_t(l)] = Emitter{now, now, 0, 0, starPower};
            ++flamesStarted;
        }
    }
}

void Highway2D::missPulse(double now) {
    // highway_pulse_black: rgba -> highway_pulse over half of
    // highway_pulse_time, then -> highway_normal over the other half [C script].
    if (now - pulseAt_ < pulseMs_) return;
    pulseAt_ = now, ++pulses;
}

SDL_Color Highway2D::surfaceTint(double now) const {
    const double half = pulseMs_ / 2, t = now - pulseAt_;
    const SDL_Color base = spOn_ ? kStarTint : kNormal;
    if (t >= 0 && t < half) return mix(spOn_ && spOnAt_ < pulseAt_ ? kStarTint : kNormal, kPulse, float(t / half));
    if (t >= half && t < 2 * half) return mix(kPulse, kNormal, float((t - half) / half));
    // The pulse's last morph leaves highway_normal even in Star Power, until the next StarPowerOn [C scripts].
    if (spOn_ && spOnAt_ < pulseAt_ + 2 * half && t >= 0) return kNormal;
    return base;
}

void Highway2D::step(const PlayerStats& st, double now, double dt, bool battleRules, bool twoPlayerModes) {
    // move_highway_2d: from highway_start_time (fastintro -2000), each game
    // frame y += v dt, v *= k, k -= 0.00044, until y <= 0 [C script].
    if (!rising_ && containerY_ > 0 && now > startMs_ - 2000) rising_ = true;
    if (rising_ && containerY_ > 0) {
        containerY_ += riseV_ * float(dt / 1000.0);
        riseV_ *= riseK_;
        riseK_ -= 0.00044f;
        if (containerY_ <= 0) containerY_ = 0, rising_ = false;
    }
    // intro_buttonup_ripple: from button_ripple_start_time (-800), one button
    // a button_ripple_per_button_time (100 ms) [C script].
    while (rippled_ < 6 && now >= startMs_ - 800 + 100.0 * (rippled_ + 1)) {
        if (rippled_ < 5) pixels_[size_t(rippled_)] = buttonUpPixels_;
        ++rippled_;
    }
    // The pixels sink button_up_pixels per button_sink_time [C 0x8211B1E0].
    for (float& p : pixels_) p = std::max(0.0f, p - buttonUpPixels_ * float(dt) / buttonSinkMs_);
    // StarPowerOn / GuitarEvent_StarPowerOff [C 0x8211DA70, script].
    if (st.starPowerActive != spOn_) {
        spOn_ = st.starPowerActive;
        if (spOn_) spOnAt_ = now;
    }
    // guitarevent_multiplier<n>xoff -> highway_pulse_multiplier_loss (not in
    // battle / boss): five morphs of the container, push (0, 4 / 10 / 15)
    // for 2x / 3x / 4x (x0.6 in the two-player modes), random x [C scripts].
    const int r = st.multiplierRun, base = r < 10 ? 1 : r < 20 ? 2 : r < 30 ? 3 : 4;
    if (base < lastBaseMult_ && !battleRules) {
        const int m = lastBaseMult_;
        float push = m == 4 ? 15.0f : m == 3 ? 10.0f : m == 2 ? 4.0f : 3.0f;
        if (twoPlayerModes) push *= 0.6f;
        shake_.start = now, shake_.stepMs = m == 4 ? 110 : 100;
        static const float kPush[5] = {1, -0.7f, 0.4f, -0.3f, 0.2f};
        static const int kRand[5] = {7, 7, 5, 5, 3};
        shake_.x[0] = shake_.y[0] = 0, shake_.x[6] = shake_.y[6] = 0;
        for (int i = 0; i < 5; ++i) {
            std::uniform_int_distribution<int> d(-kRand[i], kRand[i]);
            shake_.x[size_t(i + 1)] = float(d(rng_)), shake_.y[size_t(i + 1)] = push * kPush[i];
        }
        ++shakes;
    }
    lastBaseMult_ = base;
    // Hit sparks: each hit's emitter runs 6 game frames (hit_note_fx),
    // emit_rate 0.02 s, emit_spread 160 deg about up, velocity 10, friction
    // (0,50), life 0.25 s [C data; the particle motion's units I].
    for (size_t l = 0; l < emitters_.size(); ++l) {
        Emitter& e = emitters_[l];
        if (now - e.start >= 6000.0 / 60) continue;
        while (e.next <= now) {
            std::uniform_real_distribution<float> ang(-80, 80);
            const float a = ang(rng_) * float(M_PI) / 180, v = 10 * 60;
            sparks_.push_back(Spark{e.next, 0, 0, v * std::sin(a), -v * std::cos(a), e.blue});
            sparks_.back().x = float(l);  // the lane; placed at draw time
            e.next += 20;
        }
    }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [now](const Spark& s) { return now - s.born >= 250; }), sparks_.end());
}

void Highway2D::draw(Canvas& c, const TextureCache& tex, const NoteTrack& track, const Player& player, double now, uint8_t held) {
    // The gem container's position: its x offset, the intro's rise, the
    // multiplier-loss shake and the battle push / bob move everything below.
    float sx = 0, sy = 0;
    {
        const double t = now - shake_.start;
        const int k = t >= 0 && t < 6 * shake_.stepMs ? int(t / shake_.stepMs) : 6;
        if (k < 6) {
            const float f = float((t - k * shake_.stepMs) / shake_.stepMs);
            sx = shake_.x[size_t(k)] + (shake_.x[size_t(k + 1)] - shake_.x[size_t(k)]) * f;
            sy = shake_.y[size_t(k)] + (shake_.y[size_t(k + 1)] - shake_.y[size_t(k)]) * f;
        }
    }
    const float dx = baseX_ + fx_.dx + sx, dy = containerY_ + fx_.dy + sy;
    cx_ += dx, topY_ += dy, playline_ += dy;
    for (int k = 0; k < 5; ++k) startX_[size_t(k)] += dx, endX_[size_t(k)] += dx;
    drawAll(c, tex, track, player, now, held);
    cx_ -= dx, topY_ -= dy, playline_ -= dy;
    for (int k = 0; k < 5; ++k) startX_[size_t(k)] -= dx, endX_[size_t(k)] -= dx;
}

void Highway2D::drawButtons(Canvas& c, const TextureCache& tex, uint8_t held, bool raised) const {
    // Per lane: lip / mid / neck / head at pos_2d (lefty: left_pos_2d, x
    // scale negated), scale now_scale, just centre bottom; z 3.9 / 3.6 / 3.7
    // / 3.8, +1 while raised (pixels > 0); the neck at pos_2d - (0,
    // neck_lip_base), y scale (pixels + neck_lip_add) / neck_sprite_size; the
    // head pixels up: normal, _Down held, the lit HeadL held and raised [C
    // script setup_highway, 0x8211B1E0].
    const bool lefty = lefty_ != fx_.leftyButtons;
    const SDL_Color white{255, 255, 255, 255};
    for (int l = 0; l < 5; ++l) {
        const float p = pixels_[size_t(l)];
        if ((p > 0) != raised) continue;
        const float x = endX_[size_t(lefty ? 4 - l : l)], y = playline_;
        const float sxl = lefty ? -nowScaleX_ : nowScaleX_;
        auto at = [&](uint32_t key, float px, float py, float scx, float scy) {
            int tw = 128, th = 64;
            tex.size(key, tw, th);
            sprite(c, key, tex, px, py, float(tw) * scx, float(th) * scy, 0, 1, 0, white);
        };
        at(kMid[l], x, y, sxl, nowScaleY_);
        at(kNeck, x, y - neckLipBase_, sxl, (p + neckLipAdd_) / neckSprite_);
        const bool down = held & (1 << l);
        at(!down ? kHead[l] : p <= 0 ? kHeadDown[l] : kHeadLit[l], x, y - p, sxl, nowScaleY_);
        at(kLip[l], x, y, sxl, nowScaleY_);
    }
}

void Highway2D::drawAll(Canvas& c, const TextureCache& tex, const NoteTrack& track, const Player& player, double now, uint8_t held) {
    const PlayerStats& st = player.stats();
    // The highway surface: one strip per 8 rows, width top_width x (1 +
    // widthOffsetFactor x depth), texture V = row x tiling / 1024 scrolling
    // with the gems (m_velocityV = -tiling / T) [C 0x8238DE48, script Set2DHighwaySpeed].
    {
        const SDL_Color tint = surfaceTint(now);
        const double scroll = now / travelMs_ * kRealRows;  // rows moved since song time 0
        const int texId = int(tex.find(surfaceTex_).id);
        float ya = topY_, va = 0;
        for (int r = 0; r < kRows - 8; r += 8) {
            const float vb = accum_[size_t(r + 8)];
            const float yb = yAt(vb);
            if (ya > 720) break;
            const float wa = topWidth_ * (1 + widthOffset_ * va), wb = topWidth_ * (1 + widthOffset_ * vb);
            const float ta = float((r - scroll) * tiling_ / kRealRows), tb = float((r + 8 - scroll) * tiling_ / kRealRows);
            const auto A = uint8_t(float(tint.a) * alphaAt(ya)), B = uint8_t(float(tint.a) * alphaAt(yb));
            const Vertex p0{cx_ - wa / 2, ya, 0, 0, -ta, tint.r, tint.g, tint.b, A}, p1{cx_ + wa / 2, ya, 0, 1, -ta, tint.r, tint.g, tint.b, A};
            const Vertex p2{cx_ + wb / 2, yb, 0, 1, -tb, tint.r, tint.g, tint.b, B}, p3{cx_ - wb / 2, yb, 0, 0, -tb, tint.r, tint.g, tint.b, B};
            const Vertex v[6] = {p0, p1, p2, p0, p2, p3};
            c.draw(v, 6, texId, Blend::Alpha, true);
            ya = yb, va = vb;
        }
    }
    // Fretbars (z 1): large on the first beat of a measure, medium on the
    // others, down the centre at fretbar_start_scale -> end scale [C 0x821111B8, 0x82121E00].
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
            quad(c, key, tex, cx_, y, float(tw) * s, float(th) * s, 0, SDL_Color{255, 255, 255, uint8_t(255 * alphaAt(y))});
        }
    }
    // Strings (z 2): sys_String01 at pos_2d, turned by the lane's angle
    // (atan of (start_x - end_x) / height), scale (string_scale_x, _y), just
    // centre bottom, rgba [200 200 200 200] [C script setup_highway].
    for (int l = 0; l < 5; ++l) {
        int tw = 32, th = 512;
        tex.size(kString, tw, th);
        const float ang = std::atan2(startX_[size_t(l)] - endX_[size_t(l)], height_) * 180.0f / float(M_PI);
        sprite(c, kString, tex, endX_[size_t(l)], playline_, float(tw) * stringScaleX_, float(th) * stringScaleY_, 0, 1, ang,
               SDL_Color{200, 200, 200, 200});
    }
    // Sidebars (z 3): containers at (sidebar_x, sidebar_y) / (1280 - x, y)
    // turned by +/- sidebar_angle; sys_sidebar2D scale (+/-x_scale, y_scale),
    // just centre bottom, gem fade; the colour alternates per beat between
    // white and the state's colour [C script setup_highway, set_sidebar_flash].
    // In Star Power the containers' glow (SDGLOW) and two lightning bolts
    // (Big_Bolt01, 8 frames at 15 fps, additive) show [C 0x8211DA70; frame order I].
    {
        int beat = 0;
        for (size_t i = 0; i < track.fretbars.size() && track.fretbars[i] - gemOffsetMs_ <= now; ++i) beat = int(i);
        SDL_Color second{192, 255, 255, 255};
        if (st.starPowerActive) second = {192, 255, 255, 255};
        else if (st.health <= 0.333f) second = {255, 80, 80, 255};
        else if (st.starPower >= 50) second = {128, 192, 255, 255};
        const SDL_Color col = (beat % 2) ? second : SDL_Color{255, 255, 255, 255};
        int tw = 32, th = 512;
        tex.size(kSidebar, tw, th);
        const float bottomW = topWidth_ * (1 + widthOffset_);
        const float stx = cx_ - topWidth_ / 2, sbx = cx_ - bottomW / 2;
        const float sbX = sbx + (sbx - stx) * 0.25f - sidebarXOffset_, sbY = playline_ + height_ * 0.25f;
        const float ang = std::atan2(stx - sbx, height_) * 180.0f / float(M_PI);  // sidebar_angle (clockwise)
        int gw = 128, gh = 512, bw = 1024, bh = 512;
        tex.size(kSpGlow, gw, gh);
        tex.size(kBolt, bw, bh);
        const int frame = int(now / 1000.0 * 15) % 8;
        const float bu0 = float(frame) / 8, bu1 = bu0 + 1.0f / 8;
        for (int side = 0; side < 2; ++side) {
            const float m = side ? -1.0f : 1.0f;
            const float px = side ? 2 * cx_ - sbX : sbX, rot = ang * m;
            sprite(c, kSidebar, tex, px, sbY, float(tw) * sidebarXScale_ * m, float(th) * sidebarYScale_, 0, 1, rot, col, 0, 0, 1, 1, Blend::Alpha,
                   true);
            if (!spOn_) continue;
            const float a = rot * float(M_PI) / 180, cs = std::cos(a), sn = std::sin(a);
            auto child = [&](float lx, float ly, float& ox, float& oy) { ox = px + lx * cs - ly * sn, oy = sbY + lx * sn + ly * cs; };
            float ox, oy;
            child(-55 * spFxScale_ * m, 55 * spFxScale_, ox, oy);
            sprite(c, kSpGlow, tex, ox, oy, float(gw) * spFxScale_ * m, float(gh) * 1.1f * spFxScale_, 0, 1, rot, SDL_Color{255, 255, 255, 255});
            const float fw = float(bw) / 8, fh = float(bh);
            sprite(c, kBolt, tex, px, sbY, fw * 2 * spFxScale_, fh * 0.9f * spFxScale_, 0, -1, rot + 180, SDL_Color{255, 255, 255, 255}, bu0, 0, bu1,
                   1, Blend::Add);
            sprite(c, kBolt, tex, px, sbY, fw * -0.5f * m * spFxScale_, fh * 0.9f * spFxScale_, 0, -1, rot + 180, SDL_Color{0, 0, 128, 128}, bu0, 0,
                   bu1, 1, Blend::Add);
        }
    }
    // The now-bar buttons at rest (z 3.6 .. 3.9) under the tails and gems.
    drawButtons(c, tex, held, false);
    // Sustain tails, then gems, back to front [C z: tails 4.0, gems 4.1].
    const bool leftyGems = lefty_ != fx_.leftyGems;
    const size_t first = player.current() > 8 ? player.current() - 8 : 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = track.notes.size(); i-- > first;) {
            const TrackNote& n = track.notes[i];
            const float v = depthOf(n.time, now);
            if (v < 0 || v > accum_.back()) continue;
            for (int l = 0; l < 5; ++l) {
                if (!(n.frets & (1 << l))) continue;
                const int lane = leftyGems ? 4 - l : l;  // lefthanded_gems: mirrored [C 0x82120D20]
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
                        const float xa = laneX(lane, va), xb = laneX(lane, vb), ya = yAt(va), yb = yAt(vb);
                        const auto A = uint8_t(255 * alphaAt(ya)), B = uint8_t(255 * alphaAt(yb));
                        // V runs once along the whole tail, tail end (0) to head (1) [I].
                        const float ta = float(s) / segs, tb = float(s + 1) / segs;
                        const Vertex p0{xa - wa, ya, 0, 0, ta, 255, 255, 255, A}, p1{xa + wa, ya, 0, 1, ta, 255, 255, 255, A};
                        const Vertex p2{xb + wb, yb, 0, 1, tb, 255, 255, 255, B}, p3{xb - wb, yb, 0, 0, tb, 255, 255, 255, B};
                        const Vertex q[6] = {p0, p1, p2, p0, p2, p3};
                        c.draw(q, 6, int(tex.find(key).id), Blend::Alpha);
                    }
                }
                if (pass == 1 && i >= player.current() && !(fx_.hidden && fx_.hidden(i))) {
                    // The gem sprite [C 0x82120C90]: Star Power / battle star /
                    // star / normal, hammer variants for HOPOs; star sprites are
                    // flip-books (Star2D 4x4, BattleGEM 2x4, 30 fps) [C data; frame order I].
                    const bool star = n.phrase >= 0;
                    uint32_t key;
                    float u1 = 1, v1 = 1, u0 = 0, v0 = 0, scale = gemStart_ + (gemEnd_ - gemStart_) * v, just = gemJust_;
                    const int frame = int(now / 1000.0 * 30) % 16;
                    if (st.starPowerActive) key = n.hopo ? kGemStarPowerHammer : kGemStarPower;
                    else if (star && fx_.battle) {
                        key = n.hopo ? kBattleHammer[l] : kBattle[l];
                        const int f8 = frame % 8;
                        u0 = float(f8 % 2) / 2, v0 = float(f8 / 2) / 4, u1 = u0 + 0.5f, v1 = v0 + 0.25f;
                        scale *= starScale_;
                        just = starJust_;
                    } else if (star) {
                        key = n.hopo ? kStarHammer[l] : kStar[l];
                        u0 = float(frame % 4) / 4, v0 = float(frame / 4) / 4, u1 = u0 + 0.25f, v1 = v0 + 0.25f;
                        scale *= starScale_;
                        just = starJust_;
                    } else key = n.hopo ? kGemHammer[l] : kGem[l];
                    int tw = 128, th = 64;
                    tex.size(key, tw, th);
                    const float fw = float(tw) * (u1 - u0), fh = float(th) * (v1 - v0);
                    const float y = yAt(v);
                    const auto alpha = uint8_t(255 * alphaAt(y));
                    quad(c, key, tex, laneX(lane, v), y, fw * scale, fh * scale, just, SDL_Color{255, 255, 255, alpha}, u0, v0, u1, v1);
                    // A battle star gem's card icon: a child sprite <gem>_battle_icon,
                    // texture = the card's card_texture, scale 1.2, alpha 0.5 [C
                    // 0x821218A8..0x821219D0; centred on the gem I].
                    if (star && fx_.battle && fx_.card && !st.starPowerActive)
                        if (const uint32_t icon = fx_.card(i)) {
                            int iw = 64, ih = 64;
                            tex.size(icon, iw, ih);
                            const float cy = y - fh * scale * (just + 1) / 2 + fh * scale / 2;
                            quad(c, icon, tex, laneX(lane, v), cy, float(iw) * 1.2f * scale, float(ih) * 1.2f * scale, 0,
                                 SDL_Color{255, 255, 255, uint8_t(alpha / 2)});
                        }
                }
            }
        }
    }
    // The raised buttons (z 4.6 .. 4.9) over the gems.
    drawButtons(c, tex, held, true);
    // Hit sparks (z 8): sys_Particle_Spark01, colour start_color -> end_color,
    // scale 1 -> 0.5, additive [C data hit_particle_params / star_hit_particle_params].
    {
        int sw = 32, sh = 32;
        tex.size(kSpark, sw, sh);
        for (const Spark& s : sparks_) {
            const float t = float((now - s.born) / 1000.0);
            if (t < 0 || t >= 0.25f) continue;
            const float f = t / 0.25f;
            const SDL_Color col = s.blue ? mix(SDL_Color{0, 255, 255, 255}, SDL_Color{0, 255, 255, 0}, f)
                                         : mix(SDL_Color{255, 128, 0, 255}, SDL_Color{255, 0, 0, 0}, f);
            const int lane = int(s.x);
            const float x0 = endX_[size_t(leftyGems ? 4 - lane : lane)], y0 = playline_;
            // friction (0, 50): the y speed eases off [I].
            const float x = x0 + s.vx * t, y = y0 + s.vy * t + 0.5f * 50 * 60 * t * t;
            const float k = 1 - 0.5f * f;
            quad(c, kSpark, tex, x, y, float(sw) * k, float(sh) * k, 0, col, 0, 0, 1, 1, Blend::Add);
        }
    }
    // Hit flames: 16 frames of the 4x4 flip-book at 60 fps, additive, 20 px
    // above the strike point, anchored at their bottom, z 10 [C 0x8211BCBC, script hit_note_fx].
    for (int l = 0; l < 5; ++l) {
        const Flame& f = flames_[size_t(l)];
        const int frame = int((now - f.start) / (1000.0 / 60.0));
        if (frame < 0 || frame >= 16) continue;
        const uint32_t key = f.blue ? kHitFlameBlue : kHitFlame;
        int tw = 512, th = 1024;
        tex.size(key, tw, th);
        const float u0 = float(frame % 4) / 4, v0 = float(frame / 4) / 4;
        quad(c, key, tex, endX_[size_t(leftyGems ? 4 - l : l)], playline_ - 20, float(tw) / 4, float(th) / 4, 1, SDL_Color{255, 255, 255, 255}, u0, v0,
             u0 + 0.25f, v0 + 0.25f, Blend::Add);
    }
}
