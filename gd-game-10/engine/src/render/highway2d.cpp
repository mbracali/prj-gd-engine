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
constexpr uint32_t kWhammyStarPower = 0xE78E1A35, kWhammyDead = 0x2A589F46;
constexpr uint32_t kWhammyHold = 0x736906DE;  // sys_whammy_hold01: 4x4 flip-book at 30 fps, additive [C data]
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
    // gh4 (Metallica): one struct per layout, highway_guitar1 / _guitar2
    // {highway_playline highway_height highway_top_width widthoffsetfactor
    // gem_start_scale [6] gem_end_scale [6] perspectivefact perspectiveexp
    // ...} (setup_highway copies it into highway_pos_table) [C data
    // guitar_gems.qb]; GH3's "...1" / "...2" globals as the fallback.
    const qb::Value* hs = g.find(qbkey::key(two ? "highway_guitar2" : "highway_guitar1"));
    const qb::Struct* hw = hs && hs->type == qb::Type::Struct && hs->st ? hs->st.get() : nullptr;
    auto tw = [&](const char* name, float fallback) {
        if (hw) {
            if (const qb::Value* v = g.field(*hw, qbkey::key(name))) {
                if (v->type == qb::Type::Array && v->arr && !v->arr->values.empty()) return v->arr->values[0].number();
                if (v->type == qb::Type::Pair) return v->f[1];
                if (v->type == qb::Type::Float || v->type == qb::Type::Int) return v->number();
            }
        }
        return num(g, (std::string(name) + (two ? "2" : "1")).c_str(), fallback);
    };
    playline_ = tw("highway_playline", 655);
    height_ = tw("highway_height", 350);
    topWidth_ = tw("highway_top_width", 160);
    widthOffset_ = tw("widthOffsetFactor", 2.2f);
    fade_ = tw("highway_fade", 30);
    tiling_ = tw("gHighwayTiling", 1.5f);
    gemStart_ = tw("gem_start_scale", 0.25f);
    gemEnd_ = hw && g.field(*hw, qbkey::key("gem_end_scale")) ? tw("gem_end_scale", 1.0f) : gemStart_ * (1 + widthOffset_);
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
    whammyCutoff_ = num(g, "whammy_cutoff", 1100);
    whammyTweakMs_ = num(g, "whammy_offset_tweak", -33);
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
    double F = 0, E = 0;
    if (hw && g.field(*hw, qbkey::key("perspectivefact")) && g.field(*hw, qbkey::key("perspectiveexp"))) {
        // gh4: the layout's own factor and exponent [C data].
        F = g.field(*hw, qbkey::key("perspectivefact"))->number();
        E = g.field(*hw, qbkey::key("perspectiveexp"))->number();
    } else {
        const qb::Value* ft = arrayGlobal(g, "heightPerspFactTable");
        const qb::Value* et = arrayGlobal(g, "heightPerspExpTable");
        if (!ft || !et) return false;
        const size_t hh = size_t(std::clamp(int(height_) - 162, 0, 510));
        if (hh >= ft->arr->values.size() || hh >= et->arr->values.size()) return false;
        F = ft->arr->values[hh].number(), E = et->arr->values[hh].number();
    }
    std::vector<double> d(kRows);
    d[0] = 1;
    for (int i = 1; i < kRows; ++i) d[size_t(i)] = std::pow(d[size_t(i - 1)] * F, E);
    double sum = 0;
    for (int i = 0; i < kRealRows; ++i) sum += d[size_t(i)];
    d_.assign(kRows, 0);
    accum_.assign(kRows, 0);
    for (int i = 0; i < kRows; ++i) d_[size_t(i)] = float(d[size_t(i)] / sum);
    for (int i = 1; i < kRows; ++i) accum_[size_t(i)] = accum_[size_t(i - 1)] + d_[size_t(i)];
    // gh4: the lanes and travel are read, but its gems are 3D (Gem_Set_01's
    // scene) and its skins live in highway_<name> paks: until those draw,
    // the song keeps the labelled debug highway (D13); GD10_GH4_HIGHWAY=1
    // draws this one (development aid).
    gh4_ = hw && gems_.load(g);
    if (hw && !gh4_) return false;
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

float Highway2D::rowDepth(float r) const {
    if (r < 0) return -1;
    if (r >= float(kRows - 1)) return accum_.back() + 1;
    const int i = int(r);
    const float f = r - float(i);
    return accum_[size_t(i)] + (d_[size_t(i + 1)] - d_[size_t(i)]) * f;
}

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

double Highway2D::rowOf(double noteMs, double nowMs) const {
    return ((nowMs - noteMs - gemOffsetMs_) + travelMs_) / travelMs_ * kRealRows;
}

void Highway2D::setNewWhammyValue(float value, float remainingMs, int speed) {
    // 0x821056F8 [C]: the travel time in s (its sign dropped), n entries over
    // the remaining time (truncated, + 2, at most 128), at least `speed`.
    const int len = int(wibble_.size());
    float t = travelMs_ / 1000.0f;
    if (t < 0) t = -t;
    int n = t > 0 ? int(float(len) / t * (remainingMs * 0.001f)) + 2 : len;
    n = std::min(n, len);
    n = std::max(n, std::max(speed, 1));
    for (int i = n - 1; i >= speed; --i) wibble_[size_t(i)] = wibble_[size_t(i - speed)];
    for (int i = 0; i < speed && i < len; ++i) wibble_[size_t(i)] = value;
    for (int i = n; i < len; ++i) wibble_[size_t(i)] = 1.0f;
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

void Highway2D::gemGh4(Canvas& c, const TextureCache& tex, uint32_t material, float x, float y, float scale, float just, uint8_t alpha,
                       double now) const {
    const gemmat::Material* m = gems_.find(material);
    if (!m) return;
    // A flip-book (AnimatedTexture: m_ucells x m_vcells at m_FPS) [C data; frame order I].
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (m->fps > 0 && m->ucells >= 1 && m->vcells >= 1) {
        const int nu = int(m->ucells), nv = int(m->vcells);
        const int frame = int(now / 1000.0 * double(m->fps)) % (nu * nv);
        u0 = float(frame % nu) / float(nu), v0 = float(frame / nu) / float(nv);
        u1 = u0 + 1.0f / float(nu), v1 = v0 + 1.0f / float(nv);
    }
    for (const gemmat::Pass& p : m->passes) {
        int tw = 128, th = 64;
        tex.size(p.texture, tw, th);
        auto ch = [](float f) { return uint8_t(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
        const SDL_Color col{ch(p.color[0]), ch(p.color[1]), ch(p.color[2]), uint8_t(float(alpha) * std::clamp(p.color[3], 0.0f, 1.0f))};
        if (!col.a) continue;
        const float fw = float(tw) * (u1 - u0), fh = float(th) * (v1 - v0);
        quad(c, p.texture, tex, x, y, fw * scale, fh * scale, just, col, u0, v0, u1, v1, m->additive ? Blend::Add : Blend::Alpha);
    }
}

void Highway2D::matSprite(Canvas& c, const TextureCache& tex, uint32_t material, float x, float y, float sx, float sy, float jx, float jy,
                          uint8_t alpha, double now) const {
    const gemmat::Material* m = gems_.find(material);
    if (!m) return;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (m->fps > 0 && m->ucells >= 1 && m->vcells >= 1) {
        const int nu = int(m->ucells), nv = int(m->vcells);
        const int frame = int(now / 1000.0 * double(m->fps)) % (nu * nv);
        u0 = float(frame % nu) / float(nu), v0 = float(frame / nu) / float(nv);
        u1 = u0 + 1.0f / float(nu), v1 = v0 + 1.0f / float(nv);
    }
    for (const gemmat::Pass& p : m->passes) {
        int tw = 128, th = 64;
        tex.size(p.texture, tw, th);
        auto ch = [](float f) { return uint8_t(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
        const SDL_Color col{ch(p.color[0]), ch(p.color[1]), ch(p.color[2]), uint8_t(float(alpha) * std::clamp(p.color[3], 0.0f, 1.0f))};
        if (!col.a) continue;
        sprite(c, p.texture, tex, x, y, float(tw) * (u1 - u0) * sx, float(th) * (v1 - v0) * sy, jx, jy, 0, col, u0, v0, u1, v1,
               m->additive ? Blend::Add : Blend::Alpha);
    }
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

Highway2D::Tail* Highway2D::tail(int note) {
    for (auto& t : tails_)
        if (t.first == note) return &t.second;
    tails_.push_back({note, Tail{}});
    return &tails_.back().second;
}

const Highway2D::Tail* Highway2D::tail(int note) const {
    for (const auto& t : tails_)
        if (t.first == note) return &t.second;
    return nullptr;
}

void Highway2D::noteMissed(int note) {
    // The miss judge makes the note's whammy bars dead: MakeDeadWhammy's
    // 0x8210C0D0 with button_models.<c>.dead_whammy (sys_Whammy2D_Dead), z
    // 3.1 (under the now-bar buttons) [C 0x8211F5C0-0x8211F660].
    if (note < 0) return;
    Tail* t = tail(note);
    if (!t->dead) t->dead = true, ++deadTails;
}

void Highway2D::stepTails(const Player& player, const NoteTrack& track, double now, float whammy, bool blueWhammys) {
    // star_power_activate_and_drain's LaunchGemEvent star_power_on / _off
    // (0x8211A570 -> 0x821195C0) walks every gem out: on, each gem and star
    // gem takes star_power_[hammer_]material (gem_y_just, gem scale) and each
    // tail star_power_whammy_material; off, the gems gem_[hammer_]material,
    // the star gems star_[hammer_]material (battle_star_* in p2_battle;
    // star_y_just, gem_star_scale x the gem scale) and the tails
    // whammy_material -- a star whammy's blue goes too. Gems made in between
    // take the Star Power look from star_power_used (0x82120C90). So the
    // look follows Star Power at the frame it switches; drawn from the
    // player's state (drawAll / drawTail) [C 0x821198A0-0x82119E60,
    // 0x82119BE8-0x82119C20, script star_power_activate_and_drain].
    if (player.stats().starPowerActive != tailsSp_) {
        tailsSp_ = player.stats().starPowerActive;
        if (!tailsSp_)
            for (auto& t : tails_) t.second.star = false;
    }
    const int held = player.sustaining();
    if (held != holdNote_) {
        // CheckNoteHoldEnd: every fret's tail of the hold made dead (z 3.1),
        // `holding` 0 (the head scrolls on from row whammy_cutoff) -- a hold
        // played to its end has no tail left to show [C 0x8211D3B8-0x8211D570].
        if (holdNote_ >= 0) {
            Tail* t = tail(holdNote_);
            if (!t->dead) ++deadTails;
            t->dead = true, t->star = false, t->releasedAt = now;
        }
        // GuitarEvent_WhammyOn -> WhammyFXOn (0x8211C1A0): per fret of the
        // pattern with a sustain, Function_8211BE98 makes fx_whammy_<c>p<n>
        // (sys_whammy_hold01 at button_up_models pos_2d, just (0, 1), scale 1,
        // z 10) and its particle system (render: drawAll; the particles:
        // ui/song_hooks.cpp) [C].
        holdNote_ = held, holdStart_ = now;
        holdFrets_ = held >= 0 && size_t(held) < track.notes.size() ? track.notes[size_t(held)].frets : 0;
        if (held >= 0) ++holdFxStarted;
        whammyOn_ = whammyStarOn_ = whammyStarOff_ = 0, whammyFirst_ = true;
    }
    // star_power_whammy (spawned for a held star note; it returns at once in
    // Star Power): the bar moving more than 0.03 a frame keeps whammy_on at
    // 5 frames; 5 frames of it MakeStarWhammy the pattern's bars
    // (star_power_whammy_material, z 4), 5 frames without MakeNormalWhammy
    // them; not in battle / boss (do_blue_whammys) [C script, 0x8210E5C8 / 0x8210E6B8].
    if (held >= 0 && size_t(held) < track.notes.size()) {
        const TrackNote& n = track.notes[size_t(held)];
        if (n.phrase >= 0 && !player.phraseLost(n.phrase) && !player.stats().starPowerActive) {
            if (whammyFirst_) lastWhammy_ = whammy, whammyFirst_ = false;
            if (std::fabs(whammy - lastWhammy_) > 0.03f) whammyOn_ = 5;
            Tail* t = tail(held);
            if (whammyOn_ > 0) {
                whammyStarOff_ = 0, ++whammyStarOn_, --whammyOn_;
                if (blueWhammys && whammyStarOn_ == 5 && !t->star) t->star = true, ++starWhammys;
            } else {
                whammyStarOn_ = 0, ++whammyStarOff_;
                if (blueWhammys && whammyStarOff_ == 5) t->star = false;
            }
            lastWhammy_ = whammy;
        }
    }
    // A tail's element goes when its end passes row 1152 [C 0x8210E750].
    tails_.erase(std::remove_if(tails_.begin(), tails_.end(),
                                [&](const std::pair<int, Tail>& t) {
                                    if (size_t(t.first) >= track.notes.size()) return true;
                                    const TrackNote& n = track.notes[size_t(t.first)];
                                    return t.first != held && rowOf(double(n.time) - whammyTweakMs_ + n.holdMs, now) > kRows;
                                }),
                 tails_.end());
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
    float dx = 0, dy = 0;
    containerPos(now, dx, dy);
    cx_ += dx, topY_ += dy, playline_ += dy;
    for (int k = 0; k < 5; ++k) startX_[size_t(k)] += dx, endX_[size_t(k)] += dx;
    drawAll(c, tex, track, player, now, held);
    cx_ -= dx, topY_ -= dy, playline_ -= dy;
    for (int k = 0; k < 5; ++k) startX_[size_t(k)] -= dx, endX_[size_t(k)] -= dx;
}

void Highway2D::containerPos(double now, float& x, float& y) const {
    float sx = 0, sy = 0;
    const double t = now - shake_.start;
    const int k = t >= 0 && t < 6 * shake_.stepMs ? int(t / shake_.stepMs) : 6;
    if (k < 6) {
        const float f = float((t - k * shake_.stepMs) / shake_.stepMs);
        sx = shake_.x[size_t(k)] + (shake_.x[size_t(k + 1)] - shake_.x[size_t(k)]) * f;
        sy = shake_.y[size_t(k)] + (shake_.y[size_t(k + 1)] - shake_.y[size_t(k)]) * f;
    }
    float bx = 0, by = 0;
    battleOffset(now, bx, by);
    x = baseX_ + fx_.dx + sx + bx, y = containerY_ + fx_.dy + sy + by;
}

void Highway2D::battleOffset(double now, float& x, float& y) const {
    const double t = now - bm_.start;
    const float u = bm_.ms <= 0 || t >= bm_.ms ? 1.0f : t <= 0 ? 0.0f : float(t / bm_.ms);
    x = bm_.x0 + (bm_.x1 - bm_.x0) * u, y = bm_.y0 + (bm_.y1 - bm_.y0) * u;
}

void Highway2D::battleMorph(double now, float x, float y, double ms) {
    float cx = 0, cy = 0;
    battleOffset(now, cx, cy);
    bm_ = Morph{cx, cy, x, y, now, ms};
}

namespace {
// A script's Wait <t> Seconds started at `from` is over on the first game
// frame at or after it (the 60 Hz steps' rounding allowed for).
bool waited(double now, double until) { return now >= until - 0.5; }
}  // namespace

void Highway2D::hammer(double now, float pushX, float pushY, float timeS) {
    // hammer_highway: GetScreenElementPosition, then DoScreenElementMorph
    // pos = original + push, just [center bottom], time; Wait time;
    // back to the original; Wait time; the flag cleared [C script].
    if (hammer_.state != 0) return;
    float ox = 0, oy = 0;
    battleOffset(now, ox, oy);
    hammer_.ox = ox, hammer_.oy = oy, hammer_.ms = double(timeS) * 1000.0;
    battleMorph(now, ox + pushX, oy + pushY, hammer_.ms);
    hammer_.until = now + hammer_.ms, hammer_.state = 1;
    ++hammers;
}

void Highway2D::runShake(double now, bool shaking) {
    // shake_highway: waits (a game frame at a time) for the hammer to end,
    // takes the container's position, then while shake_notes > -1 morphs it
    // to there + (0, 8) and - (0, 8) by turns over shake_frequency (0.05 s),
    // waiting that and a game frame each; at the end back at once [C script].
    ShakeScript& s = shakeScript_;
    for (int guard = 0; guard < 4; ++guard) {
        switch (s.state) {
        case 1:
            if (hammer_.state != 0) return;
            battleOffset(now, s.ox, s.oy);
            s.state = 2, s.pulseOn = false;
            continue;
        case 2:
            if (!shaking) {
                bm_ = Morph{s.ox, s.oy, s.ox, s.oy, now, 0};
                s.state = 0;
                return;
            }
            battleMorph(now, s.ox, s.oy + (s.pulseOn ? -8.0f : 8.0f), 50);
            s.until = now + 50, s.state = 3;
            return;
        case 3:
            if (!waited(now, s.until)) return;
            s.pulseOn = !s.pulseOn;
            s.state = 2;  // Wait 1 gameframe: the loop's top next frame
            return;
        default: return;
        }
    }
}

void Highway2D::stepBattle(double now, bool shaking) {
    // The running scripts first [I: their order within a frame], then
    // battle_lightning's spawn of shake_highway (spawnscriptnow: it runs at
    // once, before battle_trigger_on's hammer_highway) [C scripts].
    if (hammer_.state == 1 && waited(now, hammer_.until)) {
        battleMorph(now, hammer_.ox, hammer_.oy, hammer_.ms);
        hammer_.until = now + hammer_.ms, hammer_.state = 2;
    } else if (hammer_.state == 2 && waited(now, hammer_.until)) {
        hammer_.state = 0;
    }
    if (shakeScript_.state != 0) runShake(now, shaking);
    if (shaking && !wasShaking_ && shakeScript_.state == 0) {
        shakeScript_.state = 1;
        ++battleShakes;
        runShake(now, shaking);
    }
    wasShaking_ = shaking;
}

bool Highway2D::gemAt(int32_t noteMs, int fret, double now, float& x, float& y, float& scale) const {
    if (startX_.size() < 5 || endX_.size() < 5 || accum_.empty() || d_.empty()) return false;  // init failed (gh4 tables, M4)
    const float v = depthOf(noteMs, now);
    if (v < 0 || v > accum_.back()) return false;
    const int lane = (lefty_ != fx_.leftyGems) ? 4 - fret : fret;
    float dx = 0, dy = 0;
    containerPos(now, dx, dy);
    x = laneX(lane, v) + dx, y = yAt(v) + dy;
    scale = gemStart_ + (gemEnd_ - gemStart_) * v;
    return true;
}

void Highway2D::drawButtons(Canvas& c, const TextureCache& tex, uint8_t held, bool raised, double now) const {
    // Per lane: lip / mid / neck / head at pos_2d (lefty: left_pos_2d, x
    // scale negated), scale now_scale, just centre bottom; z 3.9 / 3.6 / 3.7
    // / 3.8, +1 while raised (pixels > 0); the neck at pos_2d - (0,
    // neck_lip_base), y scale (pixels + neck_lip_add) / neck_sprite_size; the
    // head pixels up: normal, _Down held, the lit HeadL held and raised [C
    // script setup_highway, 0x8211B1E0].
    // Lefty Flip (animate_lefty_flip): each button's base slides from its
    // place to the other side's (pos_2d to left_pos_2d or back) in 0.15 s,
    // then lefthanded_button_ups flips (the sprites mirrored) [C scripts
    // battle_lefty_notes, animate_lefty_flip; I: a linear morph, the base's
    // `- pixels` y term left out]. The match flips the buttons as the slide
    // starts: drawn the old way, sliding, for 0.15 s.
    bool lefty = lefty_ != fx_.leftyButtons;
    if (!btnSeen_) btnSeen_ = true, btnLefty_ = btnFrom_ = lefty;
    if (lefty != btnLefty_) btnFrom_ = btnLefty_, btnLefty_ = lefty, btnSlideAt_ = now;
    const float slide = float(std::clamp((now - btnSlideAt_) / 150.0, 0.0, 1.0));
    const bool sliding = slide < 1 && btnFrom_ != lefty;
    if (sliding) lefty = btnFrom_;
    const SDL_Color white{255, 255, 255, 255};
    for (int l = 0; l < 5; ++l) {
        const float p = pixels_[size_t(l)];
        if ((p > 0) != raised) continue;
        float x = endX_[size_t(lefty ? 4 - l : l)];
        if (sliding) x += (endX_[size_t(lefty ? l : 4 - l)] - x) * slide;
        const float y = playline_;
        const float sxl = lefty ? -nowScaleX_ : nowScaleX_;
        auto at = [&](uint32_t key, float px, float py, float scx, float scy) {
            int tw = 128, th = 64;
            tex.size(key, tw, th);
            sprite(c, key, tex, px, py, float(tw) * scx, float(th) * scy, 0, 1, 0, white);
        };
        if (gh4_) {  // the same parts as materials [C data button_up_models]
            const gemmat::Lane& ln = gems_.lane(l);
            const float sx = sxl * ln.scale;
            const bool down = held & (1 << l);
            matSprite(c, tex, ln.mid, x, y, sx, nowScaleY_, 0, 1, 255, now);
            matSprite(c, tex, ln.neck, x, y - neckLipBase_, sx, (p + neckLipAdd_) / neckSprite_, 0, 1, 255, now);
            matSprite(c, tex, !down ? ln.head : p <= 0 ? ln.down : ln.headLit, x, y - p, sx, nowScaleY_, 0, 1, 255, now);
            matSprite(c, tex, ln.lip, x, y, sx, nowScaleY_, 0, 1, 255, now);
            continue;
        }
        at(kMid[l], x, y, sxl, nowScaleY_);
        at(kNeck, x, y - neckLipBase_, sxl, (p + neckLipAdd_) / neckSprite_);
        const bool down = held & (1 << l);
        at(!down ? kHead[l] : p <= 0 ? kHeadDown[l] : kHeadLit[l], x, y - p, sxl, nowScaleY_);
        at(kLip[l], x, y, sxl, nowScaleY_);
    }
}

// Star Power's highway lightning [C script setup_highway: the starpower
// containers (z 3) hold sidebar_*_glow (SDGLOW, z 0), Lightning02 (Big_Bolt01
// x (2, 0.9), turned 180, z 0.02) and Lightning01 (x (-0.5, 0.9), rgba
// 0 0 128 128, z 4); the highway is z 0.1]: the glow and the big bolt go
// under the highway, the small bolt over it (owner 11-04 doc 1.8: all three
// were drawn over the fretboard). 8 frames at 15 fps, additive [C 0x8211DA70; frame order I].
void Highway2D::drawStarPowerFx(Canvas& c, const TextureCache& tex, double now, bool under) const {
    if (!spOn_) return;
    const float bottomW = topWidth_ * (1 + widthOffset_);
    const float stx = cx_ - topWidth_ / 2, sbx = cx_ - bottomW / 2;
    const float sbX = sbx + (sbx - stx) * 0.25f - sidebarXOffset_, sbY = playline_ + height_ * 0.25f;
    const float ang = std::atan2(stx - sbx, height_) * 180.0f / float(M_PI);
    int gw = 128, gh = 512, bw = 1024, bh = 512;
    tex.size(kSpGlow, gw, gh);
    tex.size(kBolt, bw, bh);
    const int frame = int(now / 1000.0 * 15) % 8;
    const float bu0 = float(frame) / 8, bu1 = bu0 + 1.0f / 8;
    const float fw = float(bw) / 8, fh = float(bh);
    for (int side = 0; side < 2; ++side) {
        const float m = side ? -1.0f : 1.0f;
        const float px = side ? 2 * cx_ - sbX : sbX, rot = ang * m;
        if (under) {
            const float a = rot * float(M_PI) / 180, cs = std::cos(a), sn = std::sin(a);
            const float lx = -55 * spFxScale_ * m, ly = 55 * spFxScale_;
            const float ox = px + lx * cs - ly * sn, oy = sbY + lx * sn + ly * cs;
            sprite(c, kSpGlow, tex, ox, oy, float(gw) * spFxScale_ * m, float(gh) * 1.1f * spFxScale_, 0, 1, rot, SDL_Color{255, 255, 255, 255});
            sprite(c, kBolt, tex, px, sbY, fw * 2 * spFxScale_, fh * 0.9f * spFxScale_, 0, -1, rot + 180, SDL_Color{255, 255, 255, 255}, bu0, 0, bu1,
                   1, Blend::Add);
        } else {
            sprite(c, kBolt, tex, px, sbY, fw * -0.5f * m * spFxScale_, fh * 0.9f * spFxScale_, 0, -1, rot + 180, SDL_Color{0, 0, 128, 128}, bu0, 0,
                   bu1, 1, Blend::Add);
        }
    }
}

void Highway2D::drawTail(Canvas& c, const TextureCache& tex, const TrackNote& n, int i, int fret, int lane, const Player& player, double now) const {
    // Rows (0 top .. 1024 playline .. 1152 the last): the mover's elapsed
    // time carries whammy_offset_tweak (-33 ms: the tail 33 ms behind its
    // gem); the tail end is the length L = hold / T behind the head
    // [C 0x82121428-0x82121444, 0x8210E750].
    const Tail* t = tail(i);
    const double noteMs = double(n.time) - whammyTweakMs_;
    const double rowEnd = rowOf(noteMs + n.holdMs, now);
    const double pinned = double(whammyCutoff_) * kRealRows / 1152.0;
    double rowHead = rowOf(noteMs, now);
    // Held: the head's start time is reset each frame to now - whammy_cutoff
    // x T / 1152, the head at row 1100 x 1024 / 1152 = 977.8 (above the
    // playline, under the raised fret buttons) [C 0x8210E8D4-0x8210E910];
    // let go (CheckNoteHoldEnd's holding 0), it scrolls on from there [C].
    if (player.sustaining() == i) rowHead = pinned;
    else if (t && t->releasedAt >= 0) rowHead = pinned + (now - t->releasedAt) / travelMs_ * kRealRows;
    if (rowHead <= 0 || rowEnd >= kRows - 8) return;
    // The strip (0x8238E338): vertices on the 8-row grid from the grid row at
    // the tail end to the first grid row past the head; half width =
    // whammy_top_width x (1 + whammy_width_offset x D) / 2, D the depth of
    // the grid row; grid row k carries the wibble entry 128 - k, which
    // WhammyBar_UI's vertex shader multiplies the half width by. Texture u 0
    // (left) .. 1 (right); v 1.0 (the texture's bottom row: the bar) except
    // the first DAT_8291E730 = 4 grid cells from the tail end, v = (cells
    // from the tail end) / 4: the texture's rounded cap [C 0x8238E338, 0x8238ECF8].
    const double r0 = std::max(0.0, rowEnd);
    const double rLast = std::min(double(kRows - 8), (std::floor(rowHead / 8.0) + 1.0) * 8.0);
    if (r0 >= rLast) return;
    std::vector<double> rows{r0};
    for (double g = std::floor(r0 / 8.0) * 8.0 + 8.0; g < rLast; g += 8.0) rows.push_back(g);
    rows.push_back(rLast);
    const auto entry = [&](int k) {
        const int e = 128 - k;
        return e >= 0 && e < 128 ? double(wibble_[size_t(e)]) : 1.0;
    };
    const auto scaleAt = [&](double r) {
        const int k = int(std::floor(r / 8.0));
        const double f = r / 8.0 - k;
        return float(entry(k) + (entry(k + 1) - entry(k)) * f);
    };
    // The tail end's v: 0 at the end, 1 four grid cells (32 rows) on [C]. The
    // texel centres of the first / last rows keep a wrapping sampler off the far edge.
    const auto vAt = [&](double r) {
        const double cap = rowEnd < 0 ? 1.0 : std::min(1.0, (r - rowEnd) / 32.0);
        return float(std::clamp(cap, 0.5 / 32.0, 1.0 - 0.5 / 32.0));
    };
    // The material: dead (sys_Whammy2D_Dead), else Star Power (on now:
    // LaunchGemEvent star_power_on / _off, stepTails) or a star whammy
    // (sys_Whammy2D_StarPower), else the fret's [C 0x82121360, 0x8210C0D0,
    // 0x821195C0; dead kept over Star Power I].
    const uint32_t key = t && t->dead                                        ? kWhammyDead
                         : (t && t->star) || player.stats().starPowerActive ? kWhammyStarPower
                                                                              : kWhammy[fret];
    const int texId = int(tex.find(key).id);
    for (size_t s = 0; s + 1 < rows.size(); ++s) {
        const float va = rowDepth(float(rows[s])), vb = rowDepth(float(rows[s + 1]));
        const float wa = whammyTop_ * (1 + whammyOffset_ * va) * 0.5f * scaleAt(rows[s]);
        const float wb = whammyTop_ * (1 + whammyOffset_ * vb) * 0.5f * scaleAt(rows[s + 1]);
        const float xa = laneX(lane, va), xb = laneX(lane, vb), ya = yAt(va), yb = yAt(vb);
        const auto A = uint8_t(255 * alphaAt(ya)), B = uint8_t(255 * alphaAt(yb));
        const float ta = vAt(rows[s]), tb = vAt(rows[s + 1]);
        const Vertex p0{xa - wa, ya, 0, 0, ta, 255, 255, 255, A}, p1{xa + wa, ya, 0, 1, ta, 255, 255, 255, A};
        const Vertex p2{xb + wb, yb, 0, 1, tb, 255, 255, 255, B}, p3{xb - wb, yb, 0, 0, tb, 255, 255, 255, B};
        const Vertex q[6] = {p0, p1, p2, p0, p2, p3};
        c.draw(q, 6, texId, Blend::Alpha);
    }
}

void Highway2D::drawAll(Canvas& c, const TextureCache& tex, const NoteTrack& track, const Player& player, double now, uint8_t held) {
    const PlayerStats& st = player.stats();
    drawStarPowerFx(c, tex, now, true);  // the glow and the big bolt (z 0, 0.02) under the highway (z 0.1)
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
        for (int side = 0; side < 2; ++side) {
            const float m = side ? -1.0f : 1.0f;
            const float px = side ? 2 * cx_ - sbX : sbX, rot = ang * m;
            sprite(c, kSidebar, tex, px, sbY, float(tw) * sidebarXScale_ * m, float(th) * sidebarYScale_, 0, 1, rot, col, 0, 0, 1, 1, Blend::Alpha,
                   true);
        }
    }
    drawStarPowerFx(c, tex, now, false);  // Lightning01 (z 4) over the highway
    const bool leftyGems = lefty_ != fx_.leftyGems;
    const size_t first = player.current() > 8 ? player.current() - 8 : 0;
    // Dead tails (MakeDeadWhammy: z 3.1) under the now-bar buttons at rest
    // (z 3.6 .. 3.9); the live tails (z 4.0) and gems (4.1) over them, back
    // to front [C 0x8210C0D0, z values].
    for (int dead = 1; dead >= 0; --dead) {
        for (size_t i = track.notes.size(); i-- > first;) {
            const TrackNote& n = track.notes[i];
            if (!n.sustain) continue;
            const Tail* t = tail(int(i));
            if ((t && t->dead) != (dead == 1)) continue;
            for (int l = 0; l < 5; ++l)
                if (n.frets & (1 << l)) drawTail(c, tex, n, int(i), l, leftyGems ? 4 - l : l, player, now);
        }
        if (dead) drawButtons(c, tex, held, false, now);
    }
    for (int pass = 1; pass < 2; ++pass) {
        for (size_t i = track.notes.size(); i-- > first;) {
            const TrackNote& n = track.notes[i];
            const float v = depthOf(n.time, now);
            if (v < 0 || v > accum_.back()) continue;
            for (int l = 0; l < 5; ++l) {
                if (!(n.frets & (1 << l))) continue;
                const int lane = leftyGems ? 4 - l : l;  // lefthanded_gems: mirrored [C 0x82120D20]
                if (pass == 1 && i >= player.current() && !(fx_.hidden && fx_.hidden(i))) {
                    // The gem sprite [C 0x82120C90]: Star Power / battle star /
                    // star / normal, hammer variants for HOPOs; star sprites are
                    // flip-books (Star2D 4x4, BattleGEM 2x4, 30 fps) [C data; frame order I].
                    // A lost phrase's gems are normal gems again (star_miss_note,
                    // owner 11-04 doc 1.8: they kept rolling as stars) [C 0x8211F56C].
                    const bool star = n.phrase >= 0 && !player.phraseLost(n.phrase);
                    uint32_t key;
                    float u1 = 1, v1 = 1, u0 = 0, v0 = 0, scale = gemStart_ + (gemEnd_ - gemStart_) * v, just = gemJust_;
                    const int frame = int(now / 1000.0 * 30) % 16;
                    // Star Power gives every gem out the Star Power gem (plain size,
                    // gem_y_just) from the frame it starts to the frame it ends:
                    // LaunchGemEvent star_power_on / _off re-material the gems out,
                    // star_power_used the new ones (stepTails) [C 0x821195C0, 0x82120C90].
                    const bool spGem = st.starPowerActive;
                    if (spGem) key = n.hopo ? kGemStarPowerHammer : kGemStarPower;
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
                    if (gh4_) {
                        const gemmat::Lane& ln = gems_.lane(l);
                        const uint32_t mat = spGem ? (n.hopo ? ln.spHammer : ln.spGem) : star ? (n.hopo ? ln.starHammer : ln.star)
                                                                                              : (n.hopo ? ln.hammer : ln.gem);
                        const float y = yAt(v);
                        gemGh4(c, tex, mat, laneX(lane, v), y, gemStart_ + (gemEnd_ - gemStart_) * v, just, uint8_t(255 * alphaAt(y)), now);
                        continue;
                    }
                    int tw = 128, th = 64;
                    tex.size(key, tw, th);
                    const float fw = float(tw) * (u1 - u0), fh = float(th) * (v1 - v0);
                    const float y = yAt(v);
                    const auto alpha = uint8_t(255 * alphaAt(y));
                    quad(c, key, tex, laneX(lane, v), y, fw * scale, fh * scale, just, SDL_Color{255, 255, 255, alpha}, u0, v0, u1, v1);
                    // A battle star gem's card icon: a child sprite <gem>_battle_icon,
                    // texture = the card's card_texture, scale 1.2, alpha 0.5 [C
                    // 0x821218A8..0x821219D0; centred on the gem I].
                    if (star && fx_.battle && fx_.card && !spGem)
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
    drawButtons(c, tex, held, true, now);
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
            // The particle system at the hit pos - (0, 20) [C 0x8211BCBC-0x8211BD00, DAT_82007A9C = 20].
            const float x0 = endX_[size_t(leftyGems ? 4 - lane : lane)], y0 = playline_ - 20;
            // friction (0, 50): the y speed eases off [I].
            const float x = x0 + s.vx * t, y = y0 + s.vy * t + 0.5f * 50 * 60 * t * t;
            const float k = 1 - 0.5f * f;
            quad(c, kSpark, tex, x, y, float(sw) * k, float(sh) * k, 0, col, 0, 0, 1, 1, Blend::Add);
        }
    }
    // The hold's fx_whammy_<c>p<n> (WhammyFXOn -> 0x8211BE98): sys_whammy_hold01
    // at the fret's button_up_models pos_2d (left_pos_2d lefty), just (0, 1)
    // (anchored at its bottom), scale 1 (a 128x128 frame), z 10, until
    // WhammyFXOff; the material's AnimatedTexture_UI flip-book: 4 x 4 frames
    // at 30 fps, additive (d8 1) [C; the frames counted from its creation I].
    if (holdNote_ >= 0 && holdFrets_) {
        const bool leftyB = lefty_ != fx_.leftyButtons;
        int tw = 512, th = 512;
        tex.size(kWhammyHold, tw, th);
        const int frame = int((now - holdStart_) / (1000.0 / 30.0)) % 16;
        const float u0 = float(std::max(frame, 0) % 4) / 4, v0 = float(std::max(frame, 0) / 4) / 4;
        for (int l = 0; l < 5; ++l)
            if (holdFrets_ & (1 << l))
                quad(c, kWhammyHold, tex, endX_[size_t(leftyB ? 4 - l : l)], playline_, float(tw) / 4, float(th) / 4, 1, SDL_Color{255, 255, 255, 255},
                     u0, v0, u0 + 0.25f, v0 + 0.25f, Blend::Add);
    }
    // Hit flames: 16 frames of the 4x4 flip-book at 60 fps, additive, at
    // the hit_note's pos (button_up_models.<c>.pos_2d: the lane's x,
    // highway_playline [C 0x8211EED0-0x8211EF2C, script generate_pos_table]),
    // anchored at their bottom (just (0, 1)), z 10: NoteFX hands the sprite
    // the pos as given (0x8211BDE8 reloads y) -- only the sparks sit 20 px
    // up [C 0x8211BCBC-0x8211BDF4, script hit_note_fx] (GD10-108).
    for (int l = 0; l < 5; ++l) {
        const Flame& f = flames_[size_t(l)];
        const int frame = int((now - f.start) / (1000.0 / 60.0));
        if (frame < 0 || frame >= 16) continue;
        const uint32_t key = f.blue ? kHitFlameBlue : kHitFlame;
        int tw = 512, th = 1024;
        tex.size(key, tw, th);
        const float u0 = float(frame % 4) / 4, v0 = float(frame / 4) / 4;
        quad(c, key, tex, endX_[size_t(leftyGems ? 4 - l : l)], playline_, float(tw) / 4, float(th) / 4, 1, SDL_Color{255, 255, 255, 255}, u0, v0,
             u0 + 0.25f, v0 + 0.25f, Blend::Add);
    }
}
