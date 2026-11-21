// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/hud_widgets.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "data/qbkey.h"
#include "script/vm.h"
#include "ui/ui_system.h"

namespace {

using qs::Vm;
uint32_t K(const char* s) { return qbkey::key(s); }

const qb::Value* field(const qb::Struct& s, const char* name) { return s.find(K(name)); }

uint32_t keyOf(const qb::Value* v) {
    if (!v) return 0;
    if (v->type == qb::Type::String || v->type == qb::Type::WString) return qbkey::key(v->str);
    return v->key;
}

std::vector<float> numbers(const qb::Value* v) {
    std::vector<float> out;
    if (v && v->arr)
        for (const qb::Value& x : v->arr->values) out.push_back(x.number());
    return out;
}

std::vector<uint32_t> keys(const qb::Value* v) {
    std::vector<uint32_t> out;
    if (v && v->arr)
        for (const qb::Value& x : v->arr->values) out.push_back(keyOf(&x));
    return out;
}

qb::Value pairValue(float x, float y) {
    qb::Value v;
    v.type = qb::Type::Pair;
    v.f[0] = x, v.f[1] = y;
    return v;
}

qb::Value rgbaValue(const qb::Value* c) {
    qb::Value v;
    v.type = qb::Type::Array;
    v.arr = std::make_shared<qb::Array>();
    v.arr->type = qb::Type::Int;
    for (float n : numbers(c)) v.arr->values.push_back(Vm::makeInt(int32_t(n)));
    return v;
}

float clamp01(float t) { return std::clamp(t, 0.0f, 1.0f); }

// A value's place in a value_range of n points: segment index and fraction.
void segment(const std::vector<float>& vr, float v, size_t& k, float& t) {
    k = 0, t = 0;
    if (vr.size() < 2) return;
    // A falling value_range (face-off's needle: [1 -1] -> [-51.5 51.5],
    // GD10-067) read mirrored: v from the first point to the last.
    if (vr.back() < vr.front()) {
        std::vector<float> m(vr.size());
        for (size_t i = 0; i < vr.size(); ++i) m[i] = -vr[i];
        segment(m, -v, k, t);
        return;
    }
    if (v <= vr.front()) return;
    for (k = 0; k + 2 < vr.size() && v > vr[k + 1]; ++k) {
    }
    const float span = vr[k + 1] - vr[k];
    t = span != 0 ? clamp01((v - vr[k]) / span) : 1.0f;
}

std::string text(const qb::Value& v) {
    char b[96];
    switch (v.type) {
    case qb::Type::Pair: std::snprintf(b, sizeof b, "%.3f,%.3f", double(v.f[0]), double(v.f[1])); return b;
    case qb::Type::Float: std::snprintf(b, sizeof b, "%.3f", double(v.f[0])); return b;
    case qb::Type::Int: return std::to_string(v.i);
    case qb::Type::Key: return "k" + std::to_string(v.key);
    case qb::Type::Array: {
        std::string s;
        if (v.arr)
            for (const qb::Value& x : v.arr->values) s += std::to_string(int(x.number())) + ",";
        return s;
    }
    default: return v.str;
    }
}

}  // namespace

void HudWidgets::clear() {
    widgets_.clear();
    outputsSet_ = 0;
    propsSeen_.clear();
}

void HudWidgets::addNatives(qs::Vm& vm, ui::UiSystem& ui) {
    ui_ = &ui;
    // AttachHudWidget {Input = {object = player1_status value = Score}
    // output = {...} | outputs = [{...} ...]}: a widget; its id goes back as
    // observer_id. An output that is only {id = <observer>} adds this input
    // to that widget (Star Power's on / off to the tubes and the streak
    // lights) [C scripts hud_widgets.qb; I: the second input is read from the
    // same player's values here].
    vm.addNative("AttachHudWidget", [this](Vm& v, qs::ScriptRun& run, qb::Struct& p) {
        (void)v;
        const qb::Value* in = Vm::param(p, K("input"));
        if (!in || !in->st) return false;
        Widget w;
        w.value = keyOf(field(*in->st, "value"));
        {
            const std::string vn = qbkey::name(w.value);
            if (vn.rfind("band2_", 0) == 0) w.team = 1;
            else if (vn.rfind("band", 0) == 0 || vn == "faceoff_ratio") w.team = 0;
        }
        if (const uint32_t obj = keyOf(field(*in->st, "object")))
            for (int n = 1; n <= 4; ++n)
                if (obj == qbkey::key("player" + std::to_string(n) + "_status")) w.player = n - 1;
        std::vector<const qb::Struct*> outs;
        if (const qb::Value* o = Vm::param(p, K("output")); o && o->st) outs.push_back(o->st.get());
        if (const qb::Value* os = Vm::param(p, K("outputs")); os && os->arr)
            for (const qb::Value& o : os->arr->values)
                if (o.st) outs.push_back(o.st.get());
        if (outs.size() == 1 && !field(*outs[0], "type") && field(*outs[0], "id")) return true;  // a second input (above)
        for (const qb::Struct* o : outs) {
            Output out;
            out.type = keyOf(field(*o, "type"));
            out.p = *o;
            w.outs.push_back(std::move(out));
        }
        w.id = nextId_++;
        // GD10_HUDLOG=1 (development aid): each widget as it is attached.
        static const bool log = SDL_getenv("GD10_HUDLOG") != nullptr;
        if (log)
            SDL_Log("hud widget %d: player %d team %d value %s, %zu outputs (first %s)", w.id, w.player + 1, w.team, qbkey::name(w.value).c_str(), w.outs.size(),
                    w.outs.empty() ? "-" : qbkey::name(w.outs[0].type).c_str());
        if (!run.frames.empty()) {
            if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
            Vm::set(*run.frames.back().locals, K("observer_id"), Vm::makeInt(w.id));
        }
        widgets_.push_back(std::move(w));
        return true;
    });
    // GetHUDMessageParent player = <n>: player n's message container ->
    // message_parent (its HUD parent's `message` child: hud_1g's g1 600x50
    // at (50, -140), hud_2g's g1 / g2); false without one
    // (hud_create_message then returns) [C scripts hud_messages.qb; I: the
    // container the C returns].
    // GD10_HUD_MESSAGES_LEGACY=1: unhandled as before (GD10-068).
    if (!SDL_getenv("GD10_HUD_MESSAGES_LEGACY"))
        vm.addNative("GetHUDMessageParent", [this](Vm& v, qs::ScriptRun& run, qb::Struct& p) {
            const qb::Value* pl = Vm::param(p, K("player"));
            const int n = pl ? int(pl->number()) : 1;
            const qb::Value* ps = v.global(qbkey::key("player" + std::to_string(n) + "_status"));
            const qb::Value* hp = ps && ps->st ? ps->st->find(K("hud_parent")) : nullptr;
            ui::Element* root = ui_ ? ui_->find(K("hud_root")) : nullptr;
            ui::Element* parent = root && hp && hp->key ? ui_->descAlias(*root, hp->key) : nullptr;
            if (!parent) return false;
            for (const auto& c : parent->children)
                if (c->localId == K("message")) {
                    if (!run.frames.empty()) {
                        if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
                        Vm::set(*run.frames.back().locals, K("message_parent"), Vm::makeKey(c->id));
                    }
                    return true;
                }
            return false;
        });
}

double HudWidgets::input(const Widget& w, const std::array<Values, 4>& players, double nowMs, const Band& band) const {
    const uint32_t v = w.value;
    if (band.on && w.team >= 0) {
        // Two or more players: the band's values (GD10-067) [I: the C's band values not decoded].
        const Values& t = band.team[size_t(w.team)];
        if (v == K("faceoff_ratio")) return band.faceoffRatio;
        if (v == K("band1_score") || v == K("band2_score")) return t.score;
        if (v == K("band1_streak") || v == K("band2_streak")) return t.streak;
        if (v == K("band1_multiplier") || v == K("band2_multiplier")) return t.multiplier;
        if (v == K("band1_starpower") || v == K("band2_starpower")) return t.starPower;
        if (v == K("band_projected_song_stars")) return t.stars;
        if (v == K("band_has_missed_note")) return t.missedNote ? 1 : 0;
        if (v == K("band_health")) return t.health;
        return 0;
    }
    const Values& pl = players[size_t(std::clamp(w.player, 0, 3))];
    if (v == K("score") || v == K("band1_score")) return pl.score;
    if (v == K("note_streak") || v == K("band1_streak")) return pl.streak;
    if (v == K("starpower_amount")) return pl.starPower;
    if (v == K("starpower_active")) return pl.spActive ? 1 : 0;
    if (v == K("projected_song_stars") || v == K("band_projected_song_stars")) return pl.stars;
    if (v == K("has_missed_note") || v == K("band_has_missed_note")) return pl.missedNote ? 1 : 0;
    if (v == K("health") || v == K("band_health")) return pl.health;
    if (v == K("multiplier") || v == K("band1_multiplier")) return pl.multiplier;
    if (v == K("time")) return nowMs / 1000.0;
    return 0;
}

void HudWidgets::setProp(uint32_t descId, uint32_t prop, const qb::Value& v, Output& o) {
    if (!ui_ || !prop) return;
    const std::string t = text(v);
    auto it = o.last.find(prop);
    if (it != o.last.end() && it->second == t) return;
    ui::Element* e = ui_->find(descId);
    if (!e) return;
    o.last[prop] = t;
    qb::Struct one;
    Vm::set(one, prop, v);
    ui_->descSetProps(*e, one);
    ++outputsSet_;
    const std::string n = qbkey::name(prop);
    if (std::find(propsSeen_.begin(), propsSeen_.end(), n) == propsSeen_.end()) propsSeen_.push_back(n);
}

void HudWidgets::apply(Widget& w, Output& o, double value, const Values& pl, double nowMs) {
    (void)w;
    const qb::Struct& p = o.p;
    const uint32_t id = keyOf(field(p, "desc_id"));
    const uint32_t prop = keyOf(field(p, "property"));
    const float v = float(value);
    const uint32_t type = o.type;
    if (type == K("descpropertyfloat")) {
        // value_range -> range, linear, clamped (the needle: health 0..2 -> -66..66) [I].
        const std::vector<float> vr = numbers(field(p, "value_range")), r = numbers(field(p, "range"));
        if (vr.size() < 2 || r.size() < 2) return;
        size_t k;
        float t;
        segment(vr, v, k, t);
        setProp(id, prop, Vm::makeFloat(r[std::min(k, r.size() - 2)] + (r[std::min(k + 1, r.size() - 1)] - r[std::min(k, r.size() - 2)]) * t), o);
    } else if (type == K("descpropertypair") || type == K("individualhealthneedlepos")) {
        // value_range (n points) -> range (n pairs), piecewise linear [I].
        const std::vector<float> vr = numbers(field(p, "value_range"));
        const qb::Value* r = field(p, "range");
        if (vr.size() < 2 || !r || !r->arr || r->arr->values.size() < 2) return;
        size_t k;
        float t;
        segment(vr, v, k, t);
        k = std::min(k, r->arr->values.size() - 2);
        const qb::Value &a = r->arr->values[k], &b = r->arr->values[k + 1];
        setProp(id, prop, pairValue(a.f[0] + (b.f[0] - a.f[0]) * t, a.f[1] + (b.f[1] - a.f[1]) * t), o);
    } else if (type == K("screenelementconditionalfloat")) {
        // 1 inside value_range [lo, hi), else 0; the health's top (2.0) is
        // inside the last range [I: the rock meter's red / yellow / green lights and backgrounds].
        const std::vector<float> vr = numbers(field(p, "value_range"));
        if (vr.size() < 2) return;
        const bool in = v >= vr[0] && (v < vr[1] || (vr[1] >= 2.0f && v <= vr[1]));
        setProp(id, prop, Vm::makeFloat(in ? 1.0f : 0.0f), o);
    } else if (type == K("seinttostring")) {
        // The value as an integer text (divide_by_10: a vocalist's streak) [I].
        long long n = (long long)std::floor(value);
        if (const qb::Value* d = field(p, "divide_by_10"); d && keyOf(d) == K("true")) n /= 10;
        setProp(id, prop, Vm::makeString(std::to_string(n)), o);
    } else if (type == K("setubes")) {
        // Star Power 0..100 over the tubes, each 100 / n: a full tube's glow
        // hud_rock_tube_glow_full, a filling one HUD_rock_tube_glow_fill at
        // its fraction's height, the "_b" (blue) ones in Star Power, as GH3's
        // tubes [C 0x821164D0 GH3; I: gh4's widget, scales x default_scale].
        const std::vector<uint32_t> tex = keys(field(p, "textures")), sc = keys(field(p, "scales"));
        const std::vector<float> vr = numbers(field(p, "value_range"));
        const float lo = vr.size() >= 2 ? vr[0] : 0, hi = vr.size() >= 2 ? vr[1] : 100;
        float dsx = 1, dsy = 1;
        if (const qb::Value* ds = field(p, "default_scale"); ds && ds->type == qb::Type::Pair) dsx = ds->f[0], dsy = ds->f[1];
        const size_t n = tex.size();
        if (n == 0) return;
        const float per = (hi - lo) / float(n);
        for (size_t k = 0; k < n; ++k) {
            const float f = per > 0 ? clamp01((v - lo - per * float(k)) / per) : 0;
            const char* name = f >= 1 ? (pl.spActive ? "hud_rock_tube_glow_full_b" : "hud_rock_tube_glow_full")
                                      : (pl.spActive ? "HUD_rock_tube_glow_fill_b" : "HUD_rock_tube_glow_fill");
            setProp(id, tex[k], Vm::makeKey(K(name)), o);
            if (k < sc.size()) setProp(id, sc[k], pairValue(dsx, dsy * f), o);
        }
    } else if (type == K("sestarmeter")) {
        // projected_song_stars: the whole stars as the number, the rest of a
        // star as the filler's x scale (full_scale at a whole star; full at 5) [I].
        // The authored star_filler is an untextured gold quad (215 x 30, left
        // justified) scaled to 0.7 on its x axis in uidesc_solo_play_rock_meter
        // (star_filler_scale = (0.7, 1.0), rgba [249,193,34]); the widget drives
        // its x scale with progress. The one-player widget passes full_scale =
        // 0.7 but the band HUD widget passes full_scale = 1.0 (all_dis ~93285),
        // which as the five-star extent stretched the gold the full 215 px
        // behind the white number so the whole star read yellow (GM-001). The
        // gold must never exceed the authored 0.7 width, leaving the dark track
        // visible behind the number; cap the extent at that authored width for
        // both widgets. GD10_STARFILLER_LEGACY=1: use full_scale unclamped.
        static const bool starFillerLegacy = SDL_getenv("GD10_STARFILLER_LEGACY") != nullptr;
        constexpr float kAuthoredFillerX = 0.699999988079071f;
        float full = field(p, "full_scale") ? field(p, "full_scale")->number() : kAuthoredFillerX;
        if (!starFillerLegacy) full = std::min(full, kAuthoredFillerX);
        const float s = std::clamp(v, 0.0f, 5.0f);
        const int whole = int(std::floor(s));
        const float frac = s >= 5 ? 1.0f : s - float(whole);
        setProp(id, K("star_meter_num_text"), Vm::makeString(std::to_string(whole)), o);
        setProp(id, K("star_filler_scale"), pairValue(full * frac, 1.0f), o);
    } else if (type == K("screenelementalphatoggle")) {
        // has_missed_note: 1 while no note was missed, then 0 [I].
        setProp(id, prop, Vm::makeFloat(value != 0 ? 0.0f : 1.0f), o);
    } else if (type == K("screenelementrgbaswitch")) {
        // color1 / color2 are the element's rgba for value 0 / non-zero (the
        // star meter's has_missed_note flash). The band / solo star widgets
        // pass color1 == color2 (= [238,204,120]), so the "switch" never flips
        // and in the original the element keeps its authored rgba (the gold
        // star_filler [249,193,34] and the white star_meter_num [255,255,255]);
        // the port wrote [238,204,120] every frame, turning the star count
        // yellow (GM-001). When the two colours are equal there is nothing to
        // switch: leave the authored colour. GD10_RGBASWITCH_LEGACY=1: always write.
        static const bool rgbaSwitchLegacy = SDL_getenv("GD10_RGBASWITCH_LEGACY") != nullptr;
        const std::vector<float> c1 = numbers(field(p, "color1")), c2 = numbers(field(p, "color2"));
        if (rgbaSwitchLegacy || c1 != c2)
            setProp(id, prop, rgbaValue(field(p, value != 0 ? "color2" : "color1")), o);
    } else if (type == K("senotestreaklights")) {
        // The note-streak bulbs: bulb_textures' row by the multiplier (x1 /
        // x2, x3, x4, Star Power), each bulb two notes of the ten to the next
        // multiplier: _2 lit, _1 half, _0 off; ten lit past 30 [C GH3
        // 0x82117204 (the lights), 0x821172D8 (the colours); I: gh4's widget].
        const qb::Value* rows = field(p, "bulb_textures");
        const std::vector<uint32_t> props = keys(field(p, "bulb_props"));
        if (!rows || !rows->arr || rows->arr->values.size() < 4) return;
        const int run = int(value);
        const int base = run < 10 ? 1 : run < 20 ? 2 : run < 30 ? 3 : 4;
        const size_t row = pl.spActive ? 3 : base == 4 ? 2 : base == 3 ? 1 : 0;
        const std::vector<uint32_t> t = keys(&rows->arr->values[row]);
        if (t.size() < 3) return;
        const int lights = run > 30 ? 10 : run > 20 ? run - 20 : run > 10 ? run - 10 : run;
        const int full = lights / 2;
        for (size_t i = 0; i < props.size(); ++i) {
            const int b = int(i) < full ? 2 : (int(i) == full && (lights & 1)) ? 1 : 0;
            setProp(id, props[i], Vm::makeKey(t[size_t(b)]), o);
        }
    } else if (type == K("semultipliernixie")) {
        // The multiplier's nixie: textures[x - 1], sp_textures in Star Power
        // (x2 x4 x6 x8); its colour colors[x - 1], the last in Star Power;
        // the black backing on (property2 1) [I].
        const bool sp = pl.spActive;
        const int m = std::max(1, int(value));
        const int base = std::clamp(sp ? m / 2 : m, 1, 4);
        const std::vector<uint32_t> tx = keys(field(p, sp ? "sp_textures" : "textures"));
        if (size_t(base - 1) < tx.size()) setProp(id, prop, Vm::makeKey(tx[size_t(base - 1)]), o);
        if (const uint32_t p2 = keyOf(field(p, "property2"))) setProp(id, p2, Vm::makeFloat(1.0f), o);
        const qb::Value* cols = field(p, "colors");
        if (const uint32_t cp = keyOf(field(p, "color_prop")); cp && cols && cols->arr && !cols->arr->values.empty()) {
            const size_t ci = std::min(cols->arr->values.size() - 1, sp ? cols->arr->values.size() - 1 : size_t(base - 1));
            setProp(id, cp, rgbaValue(&cols->arr->values[ci]), o);
        }
    } else if (type == K("screenelementplayerhealthblink")) {
        // In the red (health under 2/3): color1 / color2 every blink_rate s, else color1 [I].
        const float rate = field(p, "blink_rate") ? field(p, "blink_rate")->number() : 0.3f;
        const bool red = pl.health < 0.6666f;
        const bool second = red && rate > 0 && int(std::floor(nowMs / 1000.0 / rate)) % 2 == 1;
        setProp(id, prop, rgbaValue(field(p, second ? "color2" : "color1")), o);
    } else {
        static std::vector<uint32_t> told;
        if (std::find(told.begin(), told.end(), type) == told.end()) {
            told.push_back(type);
            SDL_Log("hud widgets: UNHANDLED output type %s", qbkey::name(type).c_str());
        }
    }
}

void HudWidgets::update(const std::array<Values, 4>& players, int count, double nowMs, const Band& band) {
    (void)count;
    for (Widget& w : widgets_) {
        const double v = input(w, players, nowMs, band);
        const Values& pl = band.on && w.team >= 0 ? band.team[size_t(w.team)] : players[size_t(std::clamp(w.player, 0, 3))];
        for (Output& o : w.outs) apply(w, o, v, pl, nowMs);
    }
}
