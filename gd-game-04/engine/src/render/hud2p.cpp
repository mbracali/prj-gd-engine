// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/hud2p.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "data/qbkey.h"
#include "game/match.h"
#include "render/texture_cache.h"
#include "script/globals.h"

// The tables are coop_career_hud_2d_elements, faceoff_hud_2d_elements and
// battle_hud_2d_elements (global QB data); the element rules are
// create_2d_hud_elements' (setup_hud) and the per-frame ones
// UpdateScoreFastPerFrame's two-player branches (docs/modes.md).
namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

struct Xf {
    float x = 0, y = 0, rot = 0, sx = 1, sy = 1;
    Xf child(float px, float py, float r = 0, float s = 1) const {
        const float a = rot * float(M_PI) / 180, c = std::cos(a), sn = std::sin(a);
        const float lx = px * sx, ly = py * sy;
        return Xf{x + lx * c - ly * sn, y + lx * sn + ly * c, rot + r, sx * s, sy * s};
    }
};

float ease(float u, int e) {
    u = std::clamp(u, 0.0f, 1.0f);
    if (e == 1) return u * u;
    if (e == 2) return 1 - (1 - u) * (1 - u);
    return u;
}

bool pairOf(const qb::Value* v, float& x, float& y) {
    if (!v || v->type != qb::Type::Pair) return false;
    x = v->f[0], y = v->f[1];
    return true;
}

float justOf(const qb::Value& v) {
    if (v.type == qb::Type::Float || v.type == qb::Type::Int) return v.number();
    if (v.key == K("left") || v.key == K("top")) return -1;
    if (v.key == K("right") || v.key == K("bottom")) return 1;
    return 0;
}

bool flag(const qb::Struct& st, const char* name) {
    const uint32_t k = K(name);
    for (const qb::Item& it : st.items)
        if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && it.value.key == k) return true;
    return false;
}

constexpr float kPerTube = 100.0f / 6;  // [C 0x82007A78]

}  // namespace

float Hud2P::tableNumber(const char* name, float fallback) const {
    const qb::Value* t = g_ ? g_->find(table_) : nullptr;
    if (!t || !t->st) return fallback;
    const qb::Value* v = g_->field(*t->st, K(name));
    return v && (v->type == qb::Type::Int || v->type == qb::Type::Float) ? v->number() : fallback;
}

bool Hud2P::lookup(const std::string& n, int player, float& x, float& y) const {
    // setup_hud: player 2 reads <pos_type>_p2, player 1 <pos_type>_p1 with two players, else the plain key [C].
    const qb::Value* t = g_->find(table_);
    if (!t || !t->st) return false;
    if (pairOf(g_->field(*t->st, K((n + (player ? "_p2" : "_p1")).c_str())), x, y)) return true;
    return pairOf(g_->field(*t->st, K(n.c_str())), x, y);
}

bool Hud2P::init(const Globals& g, const Match& m) {
    g_ = &g;
    switch (m.mode()) {
    case GameMode::Coop: mode_ = 0, table_ = K("coop_career_hud_2d_elements"); break;
    case GameMode::FaceOff:
    case GameMode::ProFaceOff: mode_ = 1, table_ = K("faceoff_hud_2d_elements"); break;
    default: mode_ = 2, table_ = K("battle_hud_2d_elements"); break;
    }
    const qb::Value* t = g.find(table_);
    if (!t || !t->st) return false;
    scale_ = tableNumber("scale", 0.75f);
    smallBulb_ = tableNumber("small_bulb_scale", 0.7f);
    bigBulb_ = tableNumber("big_bulb_scale", 1.0f);
    frameWidth_ = tableNumber("score_frame_width", 175);
    // The needle's zones: co-op 24.67 / 55.5, others 14 / 31.5 [C 0x82007A60..0x82007A74].
    if (mode_ == 0) t1_ = 24.6667f, t2_ = 55.5f;
    numbered_.clear();
    char b[48];
    for (int n = 1; n <= 8; ++n)
        for (char sub : {'a', 'b'}) {
            std::snprintf(b, sizeof b, "HUD2D_score_nixie_%d%c", n, sub);
            numbered_[K(b)] = Numbered{1, n, sub};
        }
    for (int n = 1; n <= 5; ++n) {
        std::snprintf(b, sizeof b, "HUD2D_score_light_unlit_%d", n), numbered_[K(b)] = Numbered{2, n, 0};
        std::snprintf(b, sizeof b, "HUD2D_score_light_halflit_%d", n), numbered_[K(b)] = Numbered{3, n, 0};
        std::snprintf(b, sizeof b, "HUD2D_score_light_allwaylit_%d", n), numbered_[K(b)] = Numbered{4, n, 0};
    }
    for (int n = 1; n <= 6; ++n) std::snprintf(b, sizeof b, "HUD2D_rock_tube_%d", n), numbered_[K(b)] = Numbered{5, n, 0};
    posNames_.clear();
    for (const char* n : {"offscreen_rock_pos", "offscreen_score_pos", "rock_pos", "score_pos", "counter_pos", "card_default_2", "card_default_3"})
        posNames_[K(n)] = n;
    const qb::Value* arr = g.field(*t->st, K("elements"));
    if (!arr || !arr->arr) return false;
    els_.clear();
    for (const qb::Value& v : arr->arr->values) {
        if (!v.st) continue;
        const qb::Struct& s = *v.st;
        El e;
        if (const qb::Value* x = g.field(s, K("element_id"))) e.id = x->key;
        if (const qb::Value* x = g.field(s, K("element_parent"))) e.parent = x->key;
        if (const qb::Value* x = g.field(s, K("texture"))) e.tex = x->key;
        if (const qb::Value* x = g.field(s, K("pos_type"))) e.posType = x->key;
        pairOf(g.field(s, K("pos_off")), e.px, e.py);
        e.hasP2Pos = pairOf(g.field(s, K("pos_off_p2")), e.p2x, e.p2y);
        if (const qb::Value* x = g.field(s, K("rot"))) e.rot = x->number();
        if (const qb::Value* x = g.field(s, K("rot_p2"))) e.rot2 = x->number(), e.hasP2Rot = true;
        if (const qb::Value* x = g.field(s, K("zoff"))) e.z = x->number();
        if (const qb::Value* x = g.field(s, K("alpha"))) e.alpha = x->number();
        if (const qb::Value* x = g.field(s, K("scale")); x && (x->type == qb::Type::Float || x->type == qb::Type::Int)) e.scale = x->number();
        if (!pairOf(g.field(s, K("element_dims")), e.w, e.h)) pairOf(g.field(s, K("dims")), e.w, e.h);
        if (const qb::Value* x = g.field(s, K("just")); x && x->arr && x->arr->values.size() == 2)
            e.jx = justOf(x->arr->values[0]), e.jy = justOf(x->arr->values[1]);
        e.parentContainer = flag(s, "parent_container");
        e.createOnce = flag(s, "create_once");
        e.noteBar = flag(s, "note_streak_bar");
        e.battlePos = flag(s, "battle_pos");
        e.container = flag(s, "container");
        if (flag(s, "small_bulb")) e.bulb = smallBulb_;
        if (flag(s, "big_bulb")) e.bulb = bigBulb_, e.big = true;
        if (const qb::Value* tb = g.field(s, K("tube")); tb && tb->st) {
            e.tube = true;
            float ox = 0, oy = 40;
            pairOf(g.field(*tb->st, K("pos_off")), ox, oy);
            e.tubeFillOff = oy;
        }
        if (const qb::Value* fl = g.field(s, K("flags")); fl && fl->st) {
            const bool p1 = flag(*fl->st, "p1"), p2 = flag(*fl->st, "p2");
            const bool both = !p1 && !p2;
            for (int p = 0; p < 2; ++p) {
                const bool mine = both || (p == 0 ? p1 : p2);
                e.flipH[p] = mine && flag(*fl->st, "flip_h");
                e.flipV[p] = mine && flag(*fl->st, "flip_v");
            }
        }
        els_.push_back(e);
    }
    // One instance per player; create_once elements only for player 1 [C create_2d_hud_elements].
    inst_.clear();
    for (int p = 0; p < 2; ++p)
        for (const El& e : els_) {
            if (p == 1 && e.createOnce) continue;
            Inst in;
            in.el = &e, in.player = p;
            for (int k = int(inst_.size()) - 1; k >= 0; --k)
                if (inst_[size_t(k)].player == p && inst_[size_t(k)].el->id == e.parent) {
                    in.parent = k;
                    break;
                }
            // A create_once parent lives in player 1's tree.
            if (in.parent < 0 && e.parent)
                for (int k = 0; k < int(inst_.size()); ++k)
                    if (inst_[size_t(k)].el->id == e.parent && inst_[size_t(k)].el->createOnce) in.parent = k;
            inst_.push_back(in);
        }
    return !inst_.empty();
}

void Hud2P::update(const Match& m, double now) {
    started_ = true;
    for (int p = 0; p < 2; ++p) {
        PlayerState& s = ps_[size_t(p)];
        const PlayerStats& st = m.player(p).stats();
        const PlayerStats& o = m.player(1 - p).stats();
        // Score: co-op shows the sum [C 0x82116938].
        const int64_t score = mode_ == 0 ? st.score + o.score : st.score;
        if (score != s.score) {
            s.score = score;
            s.scoreText = std::to_string(score);
            ++n_.scoreChanges;
        }
        // Streak / multiplier: co-op reads both runs [C 0x821174C8].
        const int run = mode_ == 0 ? st.currentRun + o.currentRun : st.currentRun;
        if (run != s.run) {
            if (run >= 25 && !s.barShown) s.barShown = true, s.barAt = now;
            else if (run < 25 && s.barShown) s.barShown = false, s.barAt = now;
            s.run = run;
        }
        s.mult = st.multiplier() / (st.starPowerActive ? 2 : 1);
        s.sp = st.starPowerActive;
        const int r = st.multiplierRun;
        s.lights = r > 30 ? 10 : r > 20 ? r - 20 : r > 10 ? r - 10 : r;
        s.sp100 = st.starPower;
        // The needle [C 0x82116D08, 0x82116E8C].
        float ang;
        if (mode_ == 0) {
            const float havg = (st.health + o.health) * 0.5f;
            ang = 74.0f * (havg - 1.0f);
        } else if (mode_ == 1) {
            const float diff = float(m.player(0).stats().score - m.player(1).stats().score);
            ang = std::clamp(diff * -0.0065f, -65.0f, 65.0f);
        } else {
            const float d = std::max(0.0f, 2.0f - st.health);
            ang = d / 2.0f * -84.0f + 42.0f;
        }
        if (ang != s.needle) ++n_.needleChanges;
        s.needle = ang;
        if (ang > 0) s.bgGreen = std::min(ang / t1_, 1.0f), s.bgYellow = 1, s.light = ang < t1_ ? 1 : 2;
        else if (ang == 0) s.bgGreen = 0, s.bgYellow = 1, s.light = 1;
        else s.bgGreen = 0, s.bgYellow = ang > -t1_ ? (t1_ + ang) / t1_ : 0, s.light = ang > -t1_ ? 1 : 0;
    }
    if (mode_ == 1) {
        // The lit side: the leader's BG / crystal [C 0x82116B9C].
        const float a = ps_[0].needle;
        const int lead = a > 0 ? 1 : a < 0 ? -1 : lead_;
        if (lead != lead_) lead_ = lead, ++n_.leadChanges;
    }
}

void Hud2P::draw(Canvas& c, const TextureCache& tex, const Match& m, double now) const {
    if (!started_) return;
    struct Item {
        uint32_t tex;
        Xf at;
        float w, h, jx, jy, z, alpha;
        bool fh, fv;
    };
    std::vector<Item> items;
    std::vector<Xf> xf(inst_.size());
    const Xf root{0, 0, 0, scale_, scale_};
    auto posName = [&](uint32_t k) {
        auto it = posNames_.find(k);
        return it == posNames_.end() ? qbkey::name(k) : it->second;
    };
    std::vector<Xf> noteXf(2), scoreXf(2), rockXf(2);
    for (size_t k = 0; k < inst_.size(); ++k) {
        const Inst& in = inst_[k];
        const El& e = *in.el;
        const int p = in.player;
        const PlayerState& s = ps_[size_t(p)];
        const Xf parent = in.parent >= 0 ? xf[size_t(in.parent)] : root;
        float px = (p == 1 && e.hasP2Pos) ? e.p2x : e.px, py = (p == 1 && e.hasP2Pos) ? e.p2y : e.py;
        const float rot = (p == 1 && e.hasP2Rot) ? e.rot2 : e.rot;
        if (e.parentContainer) {
            // A container placed by pos_type; the intro slides it from the
            // offscreen spot over 200 ms (move_2d_elements_to_default) [C].
            float ox = 0, oy = 0, fx = 0, fy = 0;
            const std::string pt = posName(e.posType);
            const bool off = pt.rfind("offscreen_", 0) == 0;
            lookup(pt, p, ox, oy);
            fx = ox, fy = oy;
            if (off) lookup(pt.substr(10), p, fx, fy);
            if (e.noteBar) {
                // The streak bar: counter_pos plus the offscreen offset while hidden, 500 ms slides [C hud_move_note_scorebar].
                float bx = 0, by = 0;
                if (!lookup("offscreen_note_streak_bar_off", p, bx, by)) by = 800;
                float u = float((now - s.barAt) / 500.0);
                u = ease(u, 2);
                const float shown = s.barShown ? u : 1 - u;
                fx = ox + bx * (1 - shown), fy = oy + by * (1 - shown);
                ox = fx, oy = fy;
            }
            const float u = float((now + 400.0) / 200.0);
            const float t = ease(u, 0);
            px = ox + (fx - ox) * t + e.px, py = oy + (fy - oy) * t + e.py;
            if (mode_ == 0 && p == 1) {
                xf[k] = Xf{-10000, -10000};  // co-op: one HUD
                continue;
            }
        }
        if (e.battlePos) lookup(posName(e.posType), p, px, py);
        Xf at = parent.child(px, py, rot, e.scale);
        // The needle turns with the driver.
        if (e.id == K("hud2d_rock_needle")) at = parent.child(px, py, mode_ == 1 ? ps_[0].needle : s.needle);
        xf[k] = at;
        auto nb = numbered_.find(e.id);
        const Numbered num = nb == numbered_.end() ? Numbered{} : nb->second;
        if (e.id == K("HUD2D_rock_container")) rockXf[size_t(p)] = at;
        if (e.id == K("HUD2D_score_container")) scoreXf[size_t(p)] = at;
        if (e.id == K("HUD2D_note_container")) noteXf[size_t(p)] = at;
        if (!e.tex) continue;
        float alpha = e.alpha;
        uint32_t texKey = e.tex;
        // Driven elements.
        if (e.id == K("hud2d_rock_lights_green")) alpha = s.light == 2 ? 1.0f : 0.0f;
        else if (e.id == K("hud2d_rock_lights_red")) alpha = s.light == 0 ? 1.0f : 0.0f;
        else if (e.id == K("hud2d_rock_lights_yellow")) alpha = s.light == 1 ? 1.0f : 0.0f;
        else if (e.id == K("hud2d_rock_bg_green")) alpha = s.bgGreen;
        else if (e.id == K("hud2d_rock_bg_yellow")) alpha = s.bgYellow;
        else if (e.id == K("HUD2D_rock_BG_p1") || e.id == K("HUD2D_rock_crystal_p1")) alpha = lead_ < 0 ? 1.0f : 0.0f;
        else if (e.id == K("HUD2D_rock_BG_p2") || e.id == K("HUD2D_rock_crystal_p2")) alpha = lead_ > 0 ? 1.0f : 0.0f;
        else if (num.kind == 1) {
            // HUD2D_score_nixie_<mult><a|b>: the multiplier's nixie (b in Star Power) [C UpdateNixie].
            alpha = (num.n == (s.sp ? s.mult * 2 : s.mult) && (num.sub == 'b') == s.sp) ? 1.0f : 0.0f;
        } else if (num.kind >= 2 && num.kind <= 4) {
            // The multiplier lights; textures by the multiplier [C jump table 0x821172D8].
            const int i = num.n - 1, full = s.lights / 2;
            const bool any = s.lights > 1;
            const bool half = num.kind == 3, all = num.kind == 4;
            if (half) alpha = any && (s.lights & 1) && i == full ? 1.0f : 0.0f;
            else if (all) alpha = any && i < full ? 1.0f : 0.0f;
            else alpha = 1;
            const char* suffix = s.sp ? "_blue" : s.mult == 3 ? "_green" : s.mult == 4 ? "_purple" : "";
            texKey = K((std::string(half ? "hud_score_light_1" : all ? "hud_score_light_2" : "hud_score_light_0") + suffix).c_str());
        } else if (e.id == K("HUD_counter_drum") || e.id == K("HUD2D_counter_body")) {
            if (mode_ == 0 && p == 1) continue;
        }
        float w = e.w, h = e.h;
        if (w <= 0 || h <= 0) {
            int tw = 0, th = 0;
            tex.size(texKey, tw, th);
            w = float(tw), h = float(th);
        }
        w *= e.bulb, h *= e.bulb;
        items.push_back(Item{texKey, at, w, h, e.jx, e.jy, e.z, alpha, e.flipH[p], e.flipV[p]});
        if (e.tube) {
            // The tube's fill and full sprites [C 0x821164D0].
            const int t = num.kind == 5 ? num.n : 1;
            const float left = s.sp100 - kPerTube * float(t - 1);
            const float f = left >= kPerTube ? 1.0f : std::max(0.0f, left / kPerTube);
            const bool b = s.sp;
            if (f > 0)
                items.push_back(Item{K(b ? "HUD_rock_tube_glow_fill_b" : "HUD_rock_tube_glow_fill"), at.child(0, e.tubeFillOff), 64 * e.bulb * 0.56f,
                                     16 * e.bulb * 3 * f, 0, 1, e.z + 0.1f, 1, false, false});
            items.push_back(Item{K(b ? "HUD_rock_tube_glow_full_b" : "HUD_rock_tube_glow_full"), at, w, h, e.jx, e.jy, e.z + 0.2f, f, false, false});
        }
    }
    // Battle: the hand's cards over the default icons (guitar_battle_hud_icons) [C data card_*_off; placement I].
    if (mode_ == 2) {
        for (int p = 0; p < m.players(); ++p) {
            const Match::BattleSide& b = m.battle(p);
            float x = 0, y = 0, cx = 0, cy = 0;
            for (size_t i = 0; i < b.cards.size() && i < 3; ++i) {
                const int card = b.cards[b.cards.size() - 1 - i];
                char key[24];
                std::snprintf(key, sizeof key, "card_%zu_off", i + 1);
                if (lookup(key, p, cx, cy)) x += cx, y += cy;
                const float sz = i == 0 ? 74 : i == 1 ? 48 : 28;
                if (card >= 0 && card < int(m.battleData().powerups.size()))
                    items.push_back(Item{m.battleData().powerups[size_t(card)].cardTexture, rockXf[size_t(p)].child(x, y), sz, sz, -1, -1, 19.5f, 1, false,
                                         false});
                ++const_cast<Hud2P*>(this)->n_.cardsShown;
            }
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.z < b.z; });
    for (const Item& it : items) {
        if (it.alpha <= 0.002f || !it.tex) continue;
        const gfx::TextureId id = tex.find(it.tex);
        if (!id.id) {
            const_cast<Hud2P*>(this)->n_.missingTextures++;
            continue;
        }
        const float l = -(it.jx + 1) / 2 * it.w, t = -(it.jy + 1) / 2 * it.h;
        const float a = it.at.rot * float(M_PI) / 180, cs = std::cos(a), sn = std::sin(a);
        float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
        if (it.fh) std::swap(u0, u1);
        if (it.fv) std::swap(v0, v1);
        auto P = [&](float lx, float ly, float u, float vv) {
            lx *= it.at.sx, ly *= it.at.sy;
            const uint8_t al = uint8_t(std::clamp(255.0f * it.alpha, 0.0f, 255.0f));
            return Vertex{it.at.x + lx * cs - ly * sn, it.at.y + lx * sn + ly * cs, 0, u, vv, 255, 255, 255, al};
        };
        const Vertex A = P(l, t, u0, v0), B = P(l + it.w, t, u1, v0), C = P(l + it.w, t + it.h, u1, v1), D = P(l, t + it.h, u0, v1);
        const Vertex q[6] = {A, B, C, A, C, D};
        c.draw(q, 6, int(id.id), Blend::Alpha);
    }
    if (!text_) return;
    const std::array<uint8_t, 4> white{255, 255, 255, 255}, black{0, 0, 0, 255};
    for (int p = 0; p < 2; ++p) {
        const PlayerState& s = ps_[size_t(p)];
        if (mode_ != 2 && !(mode_ == 0 && p == 1)) {
            // Score text: num_a9 1.1 at (222,70) (co-op (226,85)), squeezed to the frame [C setup_hud].
            const Xf at = scoreXf[size_t(p)].child(mode_ == 0 ? 226.0f : 222.0f, mode_ == 0 ? 85.0f : 70.0f);
            float k = 1.1f * scale_;
            if (width_ && s.scoreText.size() > size_t(mode_ == 0 ? 6 : 5)) {
                const float w = width_(K("num_a9"), s.scoreText, 1.1f);
                if (w > 0) k *= std::min(1.0f, frameWidth_ / w);
            }
            text_(c, K("num_a9"), s.scoreText, at.x + 3 * scale_, at.y + 3 * scale_, k, black, 0x44);
            text_(c, K("num_a9"), s.scoreText, at.x, at.y, k, white, 0x44);
            // Streak digits (222 - 37 i, 78) [C setup_hud].
            const int digits[4] = {s.run % 10, s.run / 10 % 10, s.run / 100 % 10, s.run / 1000};
            for (int i = 1; i <= 4; ++i) {
                const Xf d = noteXf[size_t(p)].child(222.0f - 37.0f * float(i), 78);
                const std::array<uint8_t, 4> col = i == 1 ? std::array<uint8_t, 4>{15, 15, 70, 200} : std::array<uint8_t, 4>{230, 230, 230, 200};
                text_(c, K("num_a7"), std::to_string(std::max(0, digits[i - 1])), d.x, d.y, scale_, col, 0x22);
            }
        }
        if (mode_ == 2) {
            // The card's name when awarded (attack_ready_text_pos_p<n>) [C battlemode_ready].
            const Match::BattleSide& b = m.battle(p);
            const double t = now - b.textAt;
            float x = 0, y = 0;
            if (t >= 0 && t < 3000 && !b.text.empty() && lookup("attack_ready_text_pos", p, x, y)) {
                text_(c, K("text_a6"), b.text, x + 2, y + 2, 0.8f, black, 0x22);
                text_(c, K("text_a6"), b.text, x, y, 0.8f, std::array<uint8_t, 4>{255, 230, 120, 255}, 0x22);
                if (t < 20) ++const_cast<Hud2P*>(this)->n_.attackTexts;
            }
        }
    }
    // Co-op: the raise-axe prompt, P1 (415,240)/(415,270) [C show_coop_raise_axe_for_starpower; side I].
    if (mode_ == 0 && m.raiseAxeShowing(now)) {
        const float u = float((now - m.raiseAxeStart()) / 200.0);
        const float sc = std::min(1.0f, u);
        text_(c, K("text_a6"), "Tilt guitar to trigger", 415, 240, 0.7f * sc, white, 0x22);
        text_(c, K("text_a6"), "Star Power", 415, 270, 1.0f * sc, std::array<uint8_t, 4>{190, 225, 255, 255}, 0x22);
    }
}
