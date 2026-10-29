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

// A sprite at (x, y) (its just point), w x h (negative: mirrored), turned
// clockwise by rot degrees about that point.
void spriteAt(Canvas& c, const TextureCache& tex, uint32_t key, float x, float y, float w, float h, float jx, float jy, float rot,
              std::array<uint8_t, 4> rgba, Blend blend = Blend::Alpha, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    const gfx::TextureId id = tex.find(key);
    if (!id.id || rgba[3] == 0) return;
    const float l = -(jx + 1) / 2 * w, t = -(jy + 1) / 2 * h;
    const float a = rot * float(M_PI) / 180, cs = std::cos(a), sn = std::sin(a);
    auto P = [&](float lx, float ly, float u, float v) {
        return Vertex{x + lx * cs - ly * sn, y + lx * sn + ly * cs, 0, u, v, rgba[0], rgba[1], rgba[2], rgba[3]};
    };
    const Vertex A = P(l, t, u0, v0), B = P(l + w, t, u1, v0), C = P(l + w, t + h, u1, v1), D = P(l, t + h, u0, v1);
    const Vertex q[6] = {A, B, C, A, C, D};
    c.draw(q, 6, int(id.id), blend);
}

void texSize(const TextureCache& tex, uint32_t key, float& w, float& h) {
    int tw = 0, th = 0;
    tex.size(key, tw, th);
    w = float(tw), h = float(th);
}

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
    if (const qb::Value* gm = g.find(K("game_mode"))) career2_ = gm->key == K("p2_career");
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
    // The hands (battlemode_ready / battle_trigger_on) [C scripts].
    if (mode_ == 2)
        for (int p = 0; p < m.players(); ++p) {
            Hand& h = hand_[size_t(p)];
            const std::vector<int>& now_ = m.battle(p).cards;
            if (now_ == h.cards) continue;
            const int oldN = int(h.cards.size()), newN = int(now_.size());
            h.prevRank.assign(size_t(newN), -1);
            for (int j = 0; j < newN && j < oldN; ++j) h.prevRank[size_t(j)] = oldN - 1 - j;
            h.flew = newN > oldN;
            if (h.flew) {
                // begin_pos: the last hit note's pos_2d - (0,90), -/+ 225 (two highways) [C script].
                const Player& pl = m.player(p);
                const NoteTrack& t = m.track(p);
                int lane = 0;
                if (pl.current() > 0 && pl.current() <= t.notes.size())
                    for (int l = 0; l < 5; ++l)
                        if (t.notes[pl.current() - 1].frets & (1 << l)) {
                            lane = l;
                            break;
                        }
                const float tw = g_ ? qbNumber(*g_, K("highway_top_width2"), 170) : 170;
                const float wof = g_ ? qbNumber(*g_, K("widthOffsetFactor2"), 1.33f) : 1.33f;
                const float pl2 = g_ ? qbNumber(*g_, K("highway_playline2"), 655) : 655;
                const float bw = tw * (1 + wof);
                h.flyX = 640 - bw / 2 + bw / 5 * (float(lane) + 0.5f) + (p == 0 ? -225.0f : 225.0f);
                h.flyY = pl2 - 90;
            }
            h.cards = now_;
            h.at = now;
        }
    // death_text_wing_flap: every 4 game frames the alert turns to a random
    // -7..7 deg and the wings flap (-40 / 40 up, -10 / 10 down) [C script].
    if (mode_ == 2)
        for (int p = 0; p < m.players(); ++p) {
            Flap& f = flap_[size_t(p)];
            if (m.battle(p).deathAt < 0 || m.player(p).stats().health <= 0) continue;
            if (++f.count >= 4) {
                f.count = 0;
                f.rot = std::uniform_int_distribution<int>(-7, 7)(rng_);
                f.up = !f.up;
            }
        }
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
    // The alerts (not battle / boss): co-op and p2_career show player 1's
    // only (the shared run and Star Power) [C scripts].
    if (mode_ != 2)
        for (int p = 0; p < (mode_ == 0 ? 1 : 2); ++p) {
            Alerts& al = alerts_[size_t(p)];
            const PlayerStats& st = m.player(p).stats();
            const int run = ps_[size_t(p)].run;
            const bool face = mode_ == 1;
            const float x = face ? (p == 0 ? 415.0f : 865.0f) : 640.0f;
            if (run != al.lastRun && al.lastRun >= 0 && (run == 50 || (run > 50 && run % 100 == 0)) && !al.combo.running(now)) {
                // Positions: p2_career (640,170) base 1; others (415|865,170) base 0.8, s 0.35;
                // the glow at hud_screen_elements[0].pos - (0,36) (co-op / career, scale x1) or
                // -/+ (225,37) (face-off, x0.5) [C scripts].
                const double at = al.ready.running(now) ? al.ready.start + al.ready.length() : now;
                const float cx = career2_ ? 640.0f : (p == 0 ? 415.0f : 865.0f);
                hudalert::combo(al.combo, al.glow, at, cx, 170, career2_ ? 1.0f : 0.8f, career2_ ? 0.8f : 0.35f, x, face ? 193.0f : 194.0f,
                                face ? 0.5f : 1.0f);
                al.comboN = run, ++combos_;
            }
            al.lastRun = run;
            const float a = st.starPower;
            if (st.starPowerActive) al.spOn = true;
            else if (al.spOn) al.spOn = false, al.readyOn = false;
            if (!al.spOn && !al.readyOn && a >= 50 && al.lastSp < 50) {
                al.readyOn = true, ++readies_;
                // Face-off: hud_screen_elements[0].pos -/+ (225,50), base 0.8 x1.2;
                // co-op / career: - (0,60), base 1.2 x1.5 [C script show_star_power_ready].
                const double at = al.combo.running(now) ? al.combo.start + al.combo.length() : now;
                hudalert::ready(al.ready, at, x, face ? 180.0f : 170.0f, face ? 0.8f : 1.2f, face ? 1.2f : 1.5f);
                al.lightning = at;
            }
            al.lastSp = a;
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
        // devil_finish: player 2's containers stay off screen (move_2d_elements_to_default) [C script].
        if (devilFinish_ && p == 1 && (e.parentContainer || in.parent < 0)) {
            xf[k] = Xf{-10000, -10000};
            continue;
        }
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
            const float u = float((now - startMs_ + 400.0) / 200.0);
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
    // Battle: the hand's cards in battlemode_container (screen space): the top
    // at rock_pos + card_1_off (64x64), the next + card_2_off at medium_scale
    // 0.55, the third + card_3_off at small_scale 0.4; z battle z + 19 [C
    // scripts battlemode_ready, update_battlecards_add / _remove; data battle_hud_2d_elements].
    if (mode_ == 2) {
        for (int p = 0; p < m.players(); ++p) {
            const Hand& h = hand_[size_t(p)];
            float rx = 0, ry = 0, c1x = 0, c1y = 0, c2x = 0, c2y = 0, c3x = 0, c3y = 0;
            lookup("rock_pos", p, rx, ry), lookup("card_1_off", p, c1x, c1y), lookup("card_2_off", p, c2x, c2y), lookup("card_3_off", p, c3x, c3y);
            const float px = rx + c1x, py = ry + c1y;
            auto slot = [&](int r, float& x, float& y, float& s) {
                if (r <= 0) x = px, y = py, s = 1;
                else if (r == 1) x = px + c2x, y = py + c2y, s = 0.55f;
                else x = px + c3x, y = py + c3y, s = 0.4f;
            };
            const float u = std::clamp(float((now - h.at) / 300.0), 0.0f, 1.0f);
            const int n = int(h.cards.size());
            for (int j = 0; j < n; ++j) {
                const int r = n - 1 - j, card = h.cards[size_t(j)];
                if (r >= 3 || card < 0 || card >= int(m.battleData().powerups.size())) continue;
                float x, y, s, fx, fy, fs, a = 1;
                slot(r, x, y, s);
                const int pr = j < int(h.prevRank.size()) ? h.prevRank[size_t(j)] : r;
                if (pr >= 0) slot(pr, fx, fy, fs);
                else if (h.flew) fx = h.flyX, fy = h.flyY, fs = 1, a = 0.5f + 0.5f * u;
                else fx = x, fy = y, fs = s;
                const Xf at{fx + (x - fx) * u, fy + (y - fy) * u, 0, 1, 1};
                const float sz = 64 * (fs + (s - fs) * u);
                items.push_back(Item{m.battleData().powerups[size_t(card)].cardTexture, at, sz, sz, 0, 0, 19.5f, a, false, false});
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
    // The alerts' glow burst (z 50) and lightning (800x100 at the text - (0,20), z 45) [C scripts].
    for (const Alerts& al : alerts_) {
        if (al.glow.running(now)) {
            const HudAnim::Props g = al.glow.at(now);
            float w = 64, h = 64;
            texSize(tex, K("Char_Select_Hilite1"), w, h);
            spriteAt(c, tex, K("Char_Select_Hilite1"), g.x, g.y, w * g.sx, h * g.sy, 0, 0, 0,
                     {uint8_t(g.rgba[0]), uint8_t(g.rgba[1]), uint8_t(g.rgba[2]), uint8_t(std::clamp(g.rgba[3] * g.alpha, 0.0f, 255.0f))});
        }
        const double t = now - al.lightning;
        static const char* const kBolt[4] = {"HUD_lightning_01", "HUD_lightning_03", "HUD_lightning_05", "HUD_lightning_07"};
        if (t >= 0 && t < 1000 && al.readyOn)
            for (int i = 0; i < 4; ++i) {
                const double a = t - 200.0 * i;
                const float v = a < 0 || a >= 400 ? 0.0f : a < 200 ? float(a / 200) : float(1 - (a - 200) / 200);
                spriteAt(c, tex, K(kBolt[i]), al.ready.init.x, al.ready.init.y - 20, 800, 100, 0, -1, 0, {255, 255, 255, uint8_t(255 * v)});
            }
    }
    if (mode_ == 2) drawBattle(c, tex, m, now);
    if (!text_) return;
    const std::array<uint8_t, 4> white{255, 255, 255, 255}, black{0, 0, 0, 255};
    for (int p = 0; p < 2; ++p) {
        const PlayerState& s = ps_[size_t(p)];
        if (mode_ != 2 && !(mode_ == 0 && p == 1)) {
            // Score text: num_a9 1.1 at (222,70) (co-op (226,85)), squeezed to the frame [C setup_hud].
            const Xf at = scoreXf[size_t(p)].child(mode_ == 0 ? 226.0f : 222.0f, mode_ == 0 ? 85.0f : 70.0f);
            float k = 1.1f * scale_;
            if (width_ && s.scoreText.size() > size_t(mode_ == 0 ? 6 : 5)) {
                const float w = width_(K("num_a9"), s.scoreText, 1.1f, 5);
                if (w > 0) k *= std::min(1.0f, frameWidth_ / w);
            }
            text_(c, K("num_a9"), s.scoreText, at.x + 3 * scale_, at.y + 3 * scale_, k, black, 0x44, Hud2D::TextStyle{});
            text_(c, K("num_a9"), s.scoreText, at.x, at.y, k, white, 0x44, Hud2D::TextStyle{});
            // Streak digits (222 - 37 i, 78) [C setup_hud].
            const int digits[4] = {s.run % 10, s.run / 10 % 10, s.run / 100 % 10, s.run / 1000};
            for (int i = 1; i <= 4; ++i) {
                const Xf d = noteXf[size_t(p)].child(222.0f - 37.0f * float(i), 78);
                const std::array<uint8_t, 4> col = i == 1 ? std::array<uint8_t, 4>{15, 15, 70, 200} : std::array<uint8_t, 4>{230, 230, 230, 200};
                text_(c, K("num_a7"), std::to_string(std::max(0, digits[i - 1])), d.x, d.y, scale_, col, 0x22, Hud2D::TextStyle{});
            }
        }
        if (mode_ == 2) {
            // The card's name when awarded (attack_ready_text_pos_p<n>) [C battlemode_ready].
            const Match::BattleSide& b = m.battle(p);
            const double t = now - b.textAt;
            float x = 0, y = 0;
            if (t >= 0 && t < 3000 && !b.text.empty() && lookup("attack_ready_text_pos", p, x, y)) {
                text_(c, K("text_a6"), b.text, x + 2, y + 2, 0.8f, black, 0x22, Hud2D::TextStyle{});
                text_(c, K("text_a6"), b.text, x, y, 0.8f, std::array<uint8_t, 4>{255, 230, 120, 255}, 0x22, Hud2D::TextStyle{});
                if (t < 20) ++const_cast<Hud2P*>(this)->n_.attackTexts;
            }
        }
    }
    for (int p = 0; p < 2; ++p) {
        const Alerts& al = alerts_[size_t(p)];
        char b[48];
        std::snprintf(b, sizeof b, "%d Note Streak!", al.comboN);
        hudalert::draw(c, text_, al.combo, K("text_a6"), b, now, 0x12);
        if (al.readyOn) hudalert::draw(c, text_, al.ready, K("text_a6"), "Star Power Ready", now, 0x12);
    }
    // Co-op: the raise-axe prompt on the side of the player who has not
    // raised yet: P1 raised -> P2's (865,240)/(865,270), P2 -> P1's (415,..)
    // [C show_coop_raise_axe_for_starpower, 0x82112850].
    if (mode_ == 0 && m.raiseAxeShowing(now)) {
        const float u = float((now - m.raiseAxeStart()) / 200.0);
        const float sc = std::min(1.0f, u);
        const float x = m.raiseAxeBy() == 0 ? 865.0f : 415.0f;
        text_(c, K("text_a6"), "Tilt guitar to trigger", x, 240, 0.7f * sc, white, 0x22, Hud2D::TextStyle{});
        text_(c, K("text_a6"), "Star Power", x, 270, 1.0f * sc, std::array<uint8_t, 4>{190, 225, 255, 255}, 0x22, Hud2D::TextStyle{});
    }
}

void Hud2P::drawBattle(Canvas& c, const TextureCache& tex, const Match& m, double now) const {
    const std::array<uint8_t, 4> white{255, 255, 255, 255}, black{0, 0, 0, 255};
    auto textOf = [this](const char* id, const char* fallback) -> std::string {
        // hud_screen_elements' texts (Double Notes!, ...) [C data].
        const qb::Value* a = g_ ? g_->find(K("hud_screen_elements")) : nullptr;
        if (a && a->arr)
            for (const qb::Value& e : a->arr->values)
                if (e.st)
                    if (const qb::Value* i = e.st->find(K("id")); i && i->key == K(id))
                        if (const qb::Value* t = e.st->find(K("Text")); t && !t->str.empty()) return t->str;
        return fallback;
    };
    for (int p = 0; p < m.players(); ++p) {
        const Match::BattleSide& b = m.battle(p);
        const PlayerStats& st = m.player(p).stats();
        // attack_bolt: sys_Big_Bolt01_Red at (640,230) -/+ (330,0) for player
        // 1 / 2 attacking, turned -/+ the bolt angle (60; Death Drain 80), P1
        // mirrored, just middle top, z 10, for 8 game frames [C script; red tint I].
        if (const double t = now - b.boltAt; t >= 0 && t < 8000.0 / 60) {
            float bw = 1024, bh = 512;
            texSize(tex, 0x9D12571C, bw, bh);
            const int frame = int(now / 1000.0 * 15) % 8;
            const float u0 = float(frame) / 8;
            const bool p1 = p == 0;
            spriteAt(c, tex, 0x9D12571C, p1 ? 310.0f : 970.0f, 230, (p1 ? -1.0f : 1.0f) * bw / 8, bh, 0, -1, p1 ? -b.boltAngle : b.boltAngle,
                     {255, 80, 60, 255}, Blend::Add, u0, 0, u0 + 1.0f / 8, 1);
        }
        // battle_death_lick: the death icon at the victim's side ((310,170) /
        // (981,170)), 128x128, pulsing alpha 0.95 / 0.1 every 0.15 s [C scripts; scale pulse I].
        if (b.deathAt >= 0 && st.health > 0) {
            const uint32_t icon = song_ == K("bossslash") ? K("icon_attack_deth_boss_slash")
                                  : song_ == K("bosstom") ? K("icon_attack_deth_boss_morello")
                                                          : K("icon_attack_deth_128");
            const int phase = int((now - b.deathAt) / 150.0);
            const float u = std::clamp(float((now - b.deathAt - 150.0 * phase) / 100.0), 0.0f, 1.0f);
            const bool down = phase % 2 == 0;  // pulse_on 0: -> alpha 0.1, scale 1.1
            const float a0 = down ? 0.95f : 0.1f, a1 = down ? 0.1f : 0.95f, s0 = down ? 1.2f : 1.1f, s1 = down ? 1.1f : 1.2f;
            const float al = a0 + (a1 - a0) * u, sc = s0 + (s1 - s0) * u;
            spriteAt(c, tex, icon, p == 0 ? 310.0f : 981.0f, 170, 128 * sc, 128 * sc, 0, 0, 0, {255, 255, 255, uint8_t(255 * al)});
            // death_text: battle_alert_death at (640 -/+ 225, 430), just centre
            // bottom, scale 0.9 + 0.3 x (1 - health), down 220 x (1 - health),
            // wings and "DEATH" / "DRAIN" (text_a10) [C scripts death_text, death_text_wing_flap].
            const Flap& f = flap_[size_t(p)];
            const float lost = st.health < 1 ? 1 - st.health : 0.0f;
            const float S = now - b.deathAt < 20 ? 0.7f : 0.9f + 0.3f * lost;
            const float ax = (p == 0 ? 415.0f : 865.0f), ay = 430 + 220 * lost, rot = float(f.rot);
            float w = 256, h = 128;
            texSize(tex, K("battle_alert_death"), w, h);
            const float r = rot * float(M_PI) / 180, cs = std::cos(r), sn = std::sin(r);
            auto at = [&](float lx, float ly, float& x, float& y) {  // the bg's top-left space -> screen
                const float px = (lx - w / 2) * S, py = (ly - h) * S;
                x = ax + px * cs - py * sn, y = ay + px * sn + py * cs;
            };
            spriteAt(c, tex, K("battle_alert_death"), ax, ay, w * S, h * S, 0, 1, rot, white);
            float ww = 128, wh = 64, x, y;
            texSize(tex, K("battle_alert_death_wing"), ww, wh);
            const float wr = f.up ? 40.0f : 10.0f;
            at(205, 0, x, y);
            spriteAt(c, tex, K("battle_alert_death_wing"), x, y, ww * 0.8f * S, wh * 0.8f * S, -1, -1, rot - wr, white);
            at(55, 0, x, y);
            spriteAt(c, tex, K("battle_alert_death_wing"), x, y, -ww * 0.8f * S, wh * 0.8f * S, -1, -1, rot + wr, white);  // just right + x scale -0.8: out to the left [I]
            if (text_) {
                Hud2D::TextStyle ts;
                ts.rot = rot;
                at(130, -53, x, y);
                text_(c, K("text_a10"), "DEATH", x + 3 * S, y + 3 * S, S, black, 0x42, ts);
                text_(c, K("text_a10"), "DEATH", x, y, S, white, 0x42, ts);
                at(130, 5, x, y);
                text_(c, K("text_a10"), "DRAIN", x + 3 * S, y + 3 * S, 1.6f * S, black, 0x42, ts);
                text_(c, K("text_a10"), "DRAIN", x, y, 1.6f * S, white, 0x42, ts);
            }
        }
        // add_battle_text: the attacks on this player, text_a6 scale 0.7 at
        // (380,35) / (890,35), the older ones 30 px lower [C scripts; order I].
        if (text_) {
            std::vector<std::string> on;
            if (b.shakeUntil > now) on.push_back(textOf("lightning_text", "Amp Overload!"));
            if (b.diffUpActive) on.push_back(textOf("difficulty_up_text", "Difficulty Up!"));
            if (b.doubleUntil > now) on.push_back(textOf("double_notes_text", "Double Notes!"));
            if (b.leftyUntil > now) on.push_back(textOf("lefty_notes_text", "Lefty Notes!"));
            if (b.brokenMask) on.push_back(textOf("broken_string_text", "Broken String!"));
            if (b.whammyLeft > 0) on.push_back(textOf("whammy_attack_text", "Whammy!"));
            const float x = p == 0 ? 380.0f : 890.0f;
            for (size_t i = 0; i < on.size() && i < 3; ++i)
                text_(c, K("text_a6"), on[i], x, 35.0f + 30.0f * float(on.size() - 1 - i), 0.7f, {210, 210, 210, 250}, 0x12, Hud2D::TextStyle{});
        }
    }
    // create_battle_death_meter (boss battles, not devil_finish): at the HUD's
    // move-in, battle_death_meter slides (648,900) -> (648,500) over 0.3 s;
    // its marker climbs 150 px and the wings close 40 deg by the song's
    // death-drain time, the marker pulsing green [0 255 100] / white [C scripts].
    if (m.mode() == GameMode::Boss && !devilFinish_) {
        const double t0 = startMs_ - 400;
        const double end = song_ == K("bossdevil") ? 321466 : song_ == K("bossslash") ? 226504 : song_ == K("bosstom") ? 197733 : -1;
        if (end > 0 && now >= t0) {
            const float y = float(900 - 400 * std::min(1.0, (now - t0) / 300.0));
            float w = 64, h = 256;
            texSize(tex, K("battle_death_meter"), w, h);
            const double st = t0 > end ? 0 : t0, meter = (end - st) / 1000.0;  // seconds
            const double steps = meter > 0 ? (now - st) / 1000.0 / (meter / 150) : 150;
            const float k = float(std::clamp(steps, 0.0, 150.0));
            const float k2 = float(std::clamp(meter > 0 ? (now - st) / 1000.0 / (meter / 40) : 40.0, 0.0, 40.0));
            const float ox = 648 - w / 2, oy = y - h / 2;
            spriteAt(c, tex, K("battle_death_meter"), 648, y, w, h, 0, 0, 0, white);
            float mw = 64, mh = 64, ww = 128, wh = 64;
            texSize(tex, K("battle_death_meter_marker"), mw, mh);
            texSize(tex, K("battle_alert_death_wing"), ww, wh);
            const int ph = int(steps);
            const float cu = float(std::clamp((now - st) / 1000.0 - double(ph) * meter / 150, 0.0, 1.0));
            const bool toGreen = ph % 2 == 0;
            auto mix = [cu](uint8_t a, uint8_t b) { return uint8_t(float(a) + (float(b) - float(a)) * cu); };
            const std::array<uint8_t, 4> col = toGreen ? std::array<uint8_t, 4>{mix(255, 0), 255, mix(255, 100), 255}
                                                       : std::array<uint8_t, 4>{mix(0, 255), 255, mix(100, 255), 255};
            spriteAt(c, tex, K("battle_death_meter_marker"), ox + 29, oy + 200 - k, mw * 0.9f, mh * 0.9f, 0, 0, 0, col);
            spriteAt(c, tex, K("battle_alert_death_wing"), ox + 13, oy + 7, ww * 0.5f, wh * 0.5f, -1, -1, 15 - k2, white);
            spriteAt(c, tex, K("battle_alert_death_wing"), ox + 39, oy + 7, -ww * 0.5f, wh * 0.5f, -1, -1, -15 + k2, white);
        }
    }
}
