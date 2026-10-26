// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/camera_director.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "game/band.h"
#include "game/world.h"
#include "render/scene_gpu.h"
#include "render/venue_view.h"
#include "scene/anim.h"
#include "scene/world_objects.h"
#include "script/tasks.h"

namespace {

constexpr float kPi = 3.14159265f;

// cam_check_shot / cam_shot_ok are functions ({cam_shot_ok $this}); the
// interpreter's builtins find the director of their interpreter here.
std::unordered_map<Interp*, CameraDirector*>& directors() {
    static std::unordered_map<Interp*, CameraDirector*> m;
    return m;
}

std::string text(Interp& in, const Value& v) {
    const Value e = in.evalValue(v);
    return e.type == Value::Type::Object && e.object ? e.object->name() : e.str();
}

using V3 = std::array<float, 3>;
V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float len(const V3& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
V3 lerp3(const V3& a, const V3& b, float t) { return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t}; }

// Rows (right, forward, up) -> quaternion (inverse of scene::quatToRot).
scene::Quat rotToQuat(const std::array<float, 9>& m) {
    // scene::quatToRot's rows are the rotated axes: m[r*3+c]. Build from the
    // column-major rotation R = transpose(rows).
    const float r00 = m[0], r11 = m[4], r22 = m[8];
    const float r01 = m[3], r10 = m[1], r02 = m[6], r20 = m[2], r12 = m[7], r21 = m[5];
    scene::Quat q;
    const float tr = r00 + r11 + r22;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2;
        q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25f * s};
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2;
        q = {0.25f * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
    } else if (r11 > r22) {
        const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2;
        q = {(r01 + r10) / s, 0.25f * s, (r12 + r21) / s, (r02 - r20) / s};
    } else {
        const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2;
        q = {(r02 + r20) / s, (r12 + r21) / s, 0.25f * s, (r10 - r01) / s};
    }
    return q;
}

// The sigmoid ease of 0x8226F988 / 0x8226F6E8: x in [0, 1] mapped to
// [-k, k], out = 0.5 + 0.5 F(x') / F(k) [I: F = tanh, an odd S curve].
float sigmoid(float x, float k) {
    if (k == 0) return x;
    x = std::clamp(x, 0.0f, 1.0f);
    return 0.5f + 0.5f * std::tanh((2 * x - 1) * k) / std::tanh(k);
}

// The camera at `pos` turned so `target` lands on screen offset `so`
// (-1..1, y up) for a 16:9 frame of vertical fov `yFov` (0x82224bd8 [I:
// solved with world up, no roll]).
void reframe(scene::Xfm& x, const V3& target, const std::array<float, 2>& so, float yFov) {
    V3 d = sub(target, x.pos);
    const float l = len(d);
    if (l < 1e-3f) return;
    for (float& c : d) c /= l;
    const float ty = std::tan(yFov * 0.5f), tx = ty * gfx::kLogicalW / gfx::kLogicalH;
    float vx = so[0] * tx, vy = 1, vz = so[1] * ty;
    const float vl = std::sqrt(vx * vx + vy * vy + vz * vz);
    vx /= vl, vy /= vl, vz /= vl;
    const float ryz = std::sqrt(vy * vy + vz * vz);
    const float p = std::asin(std::clamp(d[2] / ryz, -1.0f, 1.0f)) - std::atan2(vz, vy);
    const float w = vy * std::cos(p) - vz * std::sin(p);
    const float h = std::atan2(d[0], d[1]) - std::atan2(vx, w);
    const V3 f{std::sin(h) * std::cos(p), std::cos(h) * std::cos(p), std::sin(p)};
    const V3 r{std::cos(h), -std::sin(h), 0};
    const V3 u{r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    x.rot = {r[0], r[1], r[2], f[0], f[1], f[2], u[0], u[1], u[2]};
}

// A TransAnim's own keys at `frame` (the camera path).
scene::Xfm sampleTransAnim(const scene::TransAnim& a, float frame) {
    const scene::TransAnim* k = &a;
    if (!a.keysOwner.empty() && a.keysOwner != a.name && a.dir)
        if (auto* o = a.dir->resolve<scene::TransAnim>(a.keysOwner)) k = o;
    scene::Xfm x;
    scene::Quat q;
    if (scene::sampleRotation(k->rotKeys, frame, k->rotSlerp, q)) scene::quatToRot(q, x.rot);
    std::array<float, 3> p;
    if (scene::sampleLinear(k->transKeys, frame, p)) x.pos = p;
    return x;
}

// Bone heights above a band slot's feet until the band exists (M9) [I:
// GH2 characters are about 70 units tall].
float boneHeight(const std::string& subpart) {
    if (subpart.empty()) return 0;
    static const std::pair<const char*, float> kBones[] = {{"head", 64}, {"neck", 58}, {"spine3", 50}, {"spine2", 45},
                                                           {"spine1", 40}, {"pelvis", 35}, {"fret", 42}, {"spot", 42}};
    for (const auto& [n, h] : kBones)
        if (subpart.find(n) != std::string::npos) return h;
    return 40;
}

bool sameValue(const Value& a, const Value& b) {
    auto num = [](const Value& v, float& out) {
        if (v.isNumber()) return out = v.number(), true;
        if (v.isText() && (v.s == "TRUE" || v.s == "FALSE")) return out = v.s == "TRUE" ? 1.0f : 0.0f, true;
        return false;
    };
    float x, y;
    if (num(a, x) && num(b, y)) return x == y;
    if (a.type == Value::Type::Object || b.type == Value::Type::Object) {
        const std::string sa = a.type == Value::Type::Object && a.object ? a.object->name() : a.str();
        const std::string sb = b.type == Value::Type::Object && b.object ? b.object->name() : b.str();
        return sa == sb;
    }
    return a.str() == b.str();
}

// The categories that are always acceptable (cam_shot_ok 0x822B2538).
bool alwaysOk(const std::string& c) {
    static const std::set<std::string> s = {"WIN", "WIN_ENCORE", "WIN_GAME", "LOSE", "LIGHTER", "INTRO", "INTRO_ENCORE", "INTRO_FAST"};
    return s.count(c) != 0;
}

// Without the menus: player0 (in_star_mode) and rnd (reset_postproc).
class FnObject : public ScriptObject {
public:
    using Fn = std::function<bool(const std::string&, Value&)>;
    FnObject(const std::string& name, const std::string& cls, Fn fn) : ScriptObject(name, cls), fn_(std::move(fn)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>&, Value& r) override {
        if (msg == "get" || msg == "set" || msg == "has" || msg == "name") return false;
        r = Value::integer(0);
        return fn_(msg, r);
    }

private:
    Fn fn_;
};

}  // namespace

CameraDirector::CameraDirector(World& world, uint32_t seed) : world_(world), rng_(seed ? seed : 1) {
    for (const auto& o : world_.venue().world().root().objects)
        if (auto* c = o->as<scene::CamShot>(); c && c->decoded && !c->keyframes.empty()) shots_.push_back(c);
    world_.addNatives([this](Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) { return native(in, msg, args, r); });
    world_.addObjectNatives([this](Interp& in, scene::Object& o, const std::string& msg, std::vector<Value>& args, Value& r) {
        return objectNative(in, o, msg, args, r);
    });
    Interp& in = world_.interp();
    directors()[&in] = this;
    auto shotArg = [](Interp& i, const DataNode& c) -> const scene::CamShot* {
        auto it = directors().find(&i);
        if (it == directors().end()) return nullptr;
        const Value v = i.evalArg(c, 1);
        const std::string n = v.type == Value::Type::Object && v.object ? v.object->name() : v.str();
        return it->second->world_.venue().find<scene::CamShot>(n);
    };
    in.addBuiltin("cam_shot_ok", [shotArg](Interp& i, const DataNode& c) {
        const scene::CamShot* s = shotArg(i, c);
        auto it = directors().find(&i);
        if (!s || it == directors().end()) return Value::integer(1);
        return it->second->camShotOk(*s) ? Value::integer(1) : Value::integer(0);
    });
    in.addBuiltin("cam_check_shot", [shotArg](Interp& i, const DataNode& c) {
        const scene::CamShot* s = shotArg(i, c);
        auto it = directors().find(&i);
        if (!s || it == directors().end()) return Value::integer(1);
        return it->second->camCheckShot(*s) ? Value::integer(1) : Value::integer(0);
    });
    // `rnd reset_postproc` (the UI's rnd answers it quietly; the director
    // takes the shot's PostProc itself at start_shot).
    if (!in.findObject("rnd")) {
        rnd_ = std::make_unique<FnObject>("rnd", "Rnd", [this](const std::string& msg, Value&) {
            if (msg == "reset_postproc") postProc_ = nullptr;
            return true;
        });
        in.registerObject(rnd_.get());
    }
    if (world_.ownInterp()) {
        prevPlayer0_ = in.findObject("player0");
        player0_ = std::make_unique<FnObject>("player0", "Player", [this](const std::string& msg, Value& r) {
            if (msg == "in_star_mode") r = Value::integer(starPower_ ? 1 : 0);
            return msg == "in_star_mode";
        });
        in.registerObject(player0_.get());
    }
}

void CameraDirector::provideGameObjects(bool multiplayer, const std::string& mode) {
    Interp& in = world_.interp();
    if (!game_ && !in.findObject("game")) {
        game_ = std::make_unique<FnObject>("game", "Game", [multiplayer](const std::string& msg, Value& r) {
            if (msg == "multiplayer") r = Value::integer(multiplayer ? 1 : 0);
            else if (msg == "get_track_sym") r = Value::symbol("guitar");
            else if (msg == "is_up") r = Value::integer(1);
            return true;  // add_sink, remove_sink, want_encore_fx, set_outro_complete...: 0
        });
        in.registerObject(game_.get());
    }
    if (!gamecfg_ && !in.findObject("gamecfg")) {
        gamecfg_ = std::make_unique<FnObject>("gamecfg", "GameConfig", [](const std::string&, Value&) { return true; });
        gamecfg_->props["mode"] = Value::symbol(mode);
        in.registerObject(gamecfg_.get());
    }
}

CameraDirector::~CameraDirector() {
    Interp& in = world_.interp();
    if (game_) in.unregisterName("game", game_.get());
    if (gamecfg_) in.unregisterName("gamecfg", gamecfg_.get());
    if (auto it = directors().find(&in); it != directors().end() && it->second == this) directors().erase(it);
    if (rnd_) in.unregisterName("rnd", rnd_.get());
    if (player0_) {
        in.unregisterName("player0", player0_.get());
        if (prevPlayer0_) in.registerAs("player0", prevPlayer0_);
    }
}

uint32_t CameraDirector::random() {
    rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
    return rng_;
}

float CameraDirector::random01() { return float(random() >> 8) / float(1u << 24); }

double CameraDirector::now(const scene::CamShot& shot) const {
    return World::unitsOf(int(shot.anim.rate)) == kTaskBeats ? world_.beats() : world_.seconds();
}

Value CameraDirector::prop(const scene::CamShot& shot, const std::string& key) {
    ScriptObject* o = world_.scriptFor(const_cast<scene::CamShot*>(&shot));
    return o ? o->getProp(world_.interp(), key) : Value::integer(0);
}

// ---- natives -------------------------------------------------------------------

bool CameraDirector::native(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    r = Value::integer(0);
    auto shotValue = [&](const scene::CamShot* s) { return s ? Value::obj(world_.scriptFor(const_cast<scene::CamShot*>(s))) : Value::integer(0); };
    auto shotByName = [&](size_t k) -> const scene::CamShot* {
        return k < args.size() ? world_.venue().find<scene::CamShot>(text(in, args[k])) : nullptr;
    };
    if (msg == "pick_shot") {
        std::vector<std::string> cats;
        std::vector<std::pair<std::string, Value>> filter;
        if (!args.empty()) {
            const Value c = in.evalValue(args[0]);
            if (c.type == Value::Type::Array && c.array)
                for (const Value& e : *c.array) cats.push_back(text(in, e));
            else
                cats.push_back(text(in, c));
        }
        if (args.size() > 1) {
            const Value f = in.evalValue(args[1]);
            if (f.type == Value::Type::Array && f.array)
                for (const Value& e : *f.array)
                    if (e.type == Value::Type::Array && e.array && e.array->size() >= 2)
                        filter.emplace_back(text(in, (*e.array)[0]), in.evalValue((*e.array)[1]));
        }
        r = shotValue(pickShot(cats, filter));
        return true;
    }
    if (msg == "force_shot") {
        if (const scene::CamShot* s = shotByName(0)) forceShot(s);
        r = shotValue(current_);
        return true;
    }
    if (msg == "current_shot") return r = shotValue(current_), true;
    if (msg == "find_shot") return r = shotValue(shotByName(0)), true;
    if (msg == "cycle_shot") {
        // 0x82266958: the next shot by name ("Next Alphabetical Camera").
        std::vector<const scene::CamShot*> s = shots_;
        std::sort(s.begin(), s.end(), [](auto* a, auto* b) { return a->name < b->name; });
        if (!s.empty()) {
            auto it = std::upper_bound(s.begin(), s.end(), current_ ? current_->name : std::string(),
                                       [](const std::string& n, const scene::CamShot* c) { return n < c->name; });
            forceShot(it == s.end() ? s.front() : *it);
        }
        return r = shotValue(current_), true;
    }
    if (msg == "set_pick_shot") return pickEnabled_ = args.empty() || in.evalValue(args[0]).truthy(), true;
    if (msg == "toggle_pick_shot") return pickEnabled_ = !pickEnabled_, r = Value::integer(pickEnabled_), true;
    if (msg == "toggle_free_cam") return freeCam_ = !freeCam_, true;  // no free camera in the port: picking pauses
    if (msg == "play_camera_sequence") {
        sequence_.clear();
        for (size_t k = 0; k < args.size(); ++k) {
            const Value v = in.evalValue(args[k]);
            if (v.type == Value::Type::Array && v.array) {
                for (const Value& e : *v.array)
                    if (auto* s = world_.venue().find<scene::CamShot>(text(in, e))) sequence_.push_back(s);
            } else if (auto* s = world_.venue().find<scene::CamShot>(text(in, v))) {
                sequence_.push_back(s);
            }
        }
        if (!sequence_.empty()) {
            const scene::CamShot* first = sequence_.front();
            sequence_.erase(sequence_.begin());
            forceShot(first);
        }
        return true;
    }
    if (msg == "cam_shot_ok" || msg == "cam_check_shot") {
        const scene::CamShot* s = shotByName(0);
        r = Value::integer(!s || (msg == "cam_shot_ok" ? camShotOk(*s) : camCheckShot(*s)) ? 1 : 0);
        return true;
    }
    if (msg == "reset_postproc") return postProc_ = nullptr, true;
    if (msg == "set_min_lod") {
        // The band's characters (Character set_min_lod, 0x82152108).
        if (world_.band)
            for (const auto& m : world_.band->members())
                if (m->character) m->character->minLod = args.empty() ? 0 : in.evalValue(args[0]).integerValue();
        return true;
    }
    return false;
}

bool CameraDirector::objectNative(Interp& in, scene::Object& o, const std::string& msg, std::vector<Value>& args, Value& r) {
    if (auto* c = o.as<scene::CamShot>()) {
        if (msg == "get" && !args.empty()) {
            const std::string key = args[0].str();
            if (key == "duration") return r = Value::real(c->lengthFrames()), true;
            if (key == "category") return r = Value::symbol(c->category), true;
            if (key == "selection_weight") return r = Value::real(c->selectionWeight), true;
            return false;
        }
        return false;
    }
    if (auto* p = o.as<scene::PostProc>()) {
        if (msg == "select") return postProc_ = p, true;
        return false;
    }
    // The band slots: guitarist0's clip flags, walk and lose_teleport.
    if (o.cls == "Character" || o.cls == "BandCharacter") {
        Band* band = world_.band;
        if (msg == "playing_starpower")
            return r = Value::integer(band ? (band->playingStarpower(o.name) ? 1 : 0) : (o.name == "guitarist0" && starPower_ ? 1 : 0)), true;
        if (msg == "in_star_mode") return r = Value::integer(o.name == "guitarist0" && starPower_ ? 1 : 0), true;
        if (msg == "actually_walking") return r = Value::integer(band && band->actuallyWalking(o.name) ? 1 : 0), true;
        if (msg == "lose_teleport") {
            if (band)
                if (Band::Member* m = band->member(o.name); m && m->script) in.send(m->script.get(), "lose_teleport", args, false);
            return true;
        }
    }
    return false;
}

// ---- picking ---------------------------------------------------------------------

bool CameraDirector::passes(const scene::CamShot& shot, const std::vector<std::pair<std::string, Value>>& filter) {
    for (const auto& [key, want] : filter) {
        const Value have = prop(shot, key);
        if (want.type == Value::Type::Array && want.array) {
            bool any = false;
            for (const Value& w : *want.array) any = any || sameValue(have, w);
            if (!any) return false;
        } else if (!sameValue(have, want)) {
            return false;
        }
    }
    return true;
}

const scene::CamShot* CameraDirector::pickShot(const std::vector<std::string>& categories,
                                               const std::vector<std::pair<std::string, Value>>& filter) {
    if (!pickEnabled_ || freeCam_ || !sequence_.empty()) return nullptr;
    Interp& in = world_.interp();
    const Value prev = current_ ? Value::obj(world_.scriptFor(const_cast<scene::CamShot*>(current_))) : Value::integer(0);
    auto ok = [&](const scene::CamShot* s) {
        if (!passes(*s, filter)) return false;
        ScriptObject* o = world_.scriptFor(const_cast<scene::CamShot*>(s));
        if (!o->handler("shot_ok")) return camShotOk(*s);
        const Value v = in.send(o, "shot_ok", {prev}, false);
        return v.isNumber() && v.truthy();  // a string answer is a reason to refuse
    };
    std::vector<std::pair<const scene::CamShot*, float>> bag;
    float total = 0;
    for (const std::string& cat : categories) {
        std::vector<const scene::CamShot*> list;
        for (const scene::CamShot* s : shots_)
            if (s->category == cat) list.push_back(s);
        bool any = false;
        for (const scene::CamShot* s : list)
            if (!state_[s].used && ok(s)) {
                total += s->selectionWeight;
                bag.emplace_back(s, total);
                any = true;
            }
        if (!any)
            for (const scene::CamShot* s : list)
                if (ok(s)) {
                    state_[s].used = false;
                    total += s->selectionWeight;
                    bag.emplace_back(s, total);
                }
    }
    if (bag.empty()) {
        ++failedPicks_;
        std::string c;
        for (const auto& s : categories) c += (c.empty() ? "" : " ") + s;
        SDL_Log("world: No acceptable camera shot: cats(%s)", c.c_str());
        return nullptr;
    }
    const float x = random01() * total;
    const scene::CamShot* picked = bag.back().first;
    for (const auto& [s, cum] : bag)
        if (x <= cum) {
            picked = s;
            break;
        }
    state_[picked].used = true;
    if (picked != current_) startShot(picked);
    return picked;
}

void CameraDirector::forceShot(const scene::CamShot* shot) {
    if (shot) startShot(shot);
}

bool CameraDirector::camShotOk(const scene::CamShot& shot) {
    if (alwaysOk(shot.category)) return true;
    if (prop(shot, "special").truthy()) return false;
    return camCheckShot(shot);
}

// 0x822B2240: player 0's character (the band's guitarist0; without a band,
// player 0's Star Power and the start waypoint stand in): no Star Power
// clips (flag 0x80000 weight > 0) unless starpower_ok, no far ones (0x4000)
// unless far_starpower_ok, and not when the waypoint nearest the character
// with a kWalkSpot | kSoloWalkSpot flag (Waypoint::Nearest 0x82195548: mask
// 0xc0, squared distance, no radius; looked up once per beat) is one of
// the shot's bad_waypoints.
bool CameraDirector::camCheckShot(const scene::CamShot& shot) {
    Band* band = world_.band;
    const bool sp = band && band->character("guitarist0") ? band->flagWeight("guitarist0", 0x80000) > 0 : starPower_;
    if (sp && !prop(shot, "starpower_ok").truthy()) return false;
    if (band && band->flagWeight("guitarist0", 0x4000) > 0 && !prop(shot, "far_starpower_ok").truthy()) return false;
    const Value bad = prop(shot, "bad_waypoints");
    if (bad.type != Value::Type::Array || !bad.array || bad.array->empty()) return true;
    if (world_.beats() != nearestBeat_) {
        nearestBeat_ = world_.beats();
        nearest_ = nullptr;
        const bool mp = world_.venue().variant() != VenueVariant::Single;
        scene::Xfm g;
        const bool got = (band && band->objectXfm("guitarist0", "", g)) || venueObjectXfm(world_, "guitarist0", mp, g);
        if (got) {
            float best = 0;
            for (const scene::Waypoint* w : world_.venue().all<scene::Waypoint>()) {
                if (!(w->flags & 0xc0)) continue;  // kWalkSpot | kSoloWalkSpot [C 0x82195548]
                const V3 d = sub(world_.pose().world(w).pos, g.pos);
                const float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                if (!nearest_ || d2 < best) best = d2, nearest_ = w;
            }
        }
    }
    if (!nearest_) return true;
    for (const Value& v : *bad.array) {
        const std::string n = v.type == Value::Type::Object && v.object ? v.object->name() : v.str();
        if (n == nearest_->name) return false;
    }
    return true;
}

// ---- start / end -------------------------------------------------------------------

// CamShot StartAnim 0x82226120 (start_shot, the hide list, state reset) after
// the old shot's EndAnim 0x82226338 (stop_shot, its hide list shown again).
void CameraDirector::startShot(const scene::CamShot* shot) {
    endShot();
    current_ = shot;
    ShotState& st = state_[shot];
    const bool used = st.used;
    st = ShotState{};
    st.used = used;
    st.start = now(*shot);
    ++started_;
    for (const std::string& h : shot->hideList)
        if (const scene::Object* o = world_.venue().find(h)) world_.setShowing(o, false);
    // The shot's PostProc (camshot.dta start_shot: [postprocess] select, else
    // rnd reset_postproc).
    postProc_ = world_.venue().find<scene::PostProc>(shot->flag("postprocess"));
    if (onShot) onShot("shot " + shot->name + " (" + shot->category + ")");
    if (world_.log) SDL_Log("world: shot %s (%s)", shot->name.c_str(), shot->category.c_str());
    world_.interp().send(world_.scriptFor(const_cast<scene::CamShot*>(shot)), "start_shot", {}, false);
    if (onShotStarted && current_ == shot) onShotStarted(*shot);
}

void CameraDirector::endShot() {
    if (!current_) return;
    const scene::CamShot* old = current_;
    current_ = nullptr;
    for (const std::string& h : old->hideList)
        if (const scene::Object* o = world_.venue().find(h)) world_.setShowing(o, true);
    world_.interp().send(world_.scriptFor(const_cast<scene::CamShot*>(old)), "stop_shot", {}, false);
}

// ---- playback ------------------------------------------------------------------------

bool CameraDirector::targetPoint(const scene::CamTarget& t, std::array<float, 3>& out) const {
    if (t.object.empty()) return false;
    // A band member and its subpart (a bone of its directory).
    if (world_.band) {
        scene::Xfm bx;
        if (world_.band->objectXfm(t.object, t.subpart, bx)) {
            out = bx.pos;
            return true;
        }
    }
    const bool mp = world_.venue().variant() != VenueVariant::Single;
    scene::Xfm x;
    if (!venueObjectXfm(world_, t.object, mp, x)) return false;
    out = x.pos;
    // A band slot's bone: its height above the slot (no band until M9).
    if (!world_.venue().find(t.object) || world_.venue().find(t.object)->cls == "Character" ||
        world_.venue().find(t.object)->cls == "BandCharacter")
        out[2] += boneHeight(t.subpart);
    return true;
}

// 0x82226988: the keyframe's transform and its targets' mean.
bool CameraDirector::keyframeXfm(const scene::CamShot& shot, ShotState& st, size_t k, float frame, float dt, scene::Xfm& out,
                                 std::array<float, 3>& target, bool& hasTarget) {
    const auto& kf = shot.keyframes[k];
    out = kf.xfm;
    // The filter: a low-pass per 1/60 s step [I: its screen-distance factor
    // (0x821D7030) taken as 1].
    const float steps = std::max(1.0f, dt * 60.0f);
    auto lowPass = [&](std::array<float, 3>& f, const std::array<float, 3>& v) {
        if (!st.filtered[k] || shot.filter <= 0 || shot.filter >= 1) {
            f = v;
            return;
        }
        const float a = 1.0f - std::pow(1.0f - shot.filter, steps);
        f = lerp3(f, v, a);
    };
    // Targets: their mean.
    V3 sum{0, 0, 0};
    int n = 0;
    for (const auto& t : kf.targets) {
        V3 p;
        if (targetPoint(t, p)) sum = {sum[0] + p[0], sum[1] + p[1], sum[2] + p[2]}, ++n;
    }
    hasTarget = n > 0;
    if (hasTarget) {
        lowPass(st.target[k], {sum[0] / float(n), sum[1] / float(n), sum[2] / float(n)});
        target = st.target[k];
    }
    // The path TransAnim (eased by path_ease over the shot's length).
    if (!shot.path.empty())
        if (auto* path = world_.venue().find<scene::TransAnim>(shot.path)) {
            const float length = shot.lengthFrames();
            float u = length > 0 ? std::clamp(frame / length, 0.0f, 1.0f) : 0.0f;
            if (shot.pathEase != 0) u = sigmoid(u, shot.pathEase);
            out = scene::compose(out, sampleTransAnim(*path, gfx::ScenePose::endFrame(path) * u));
        }
    // The parent: translation only, unless use_parent_rotation.
    if (!kf.parent.object.empty()) {
        const bool mp = world_.venue().variant() != VenueVariant::Single;
        scene::Xfm px;
        const bool bandParent = world_.band && world_.band->objectXfm(kf.parent.object, kf.parent.subpart, px);
        if (bandParent || venueObjectXfm(world_, kf.parent.object, mp, px)) {
            V3 p;
            if (targetPoint(kf.parent, p)) px.pos = p;
            lowPass(st.parentPos[k], px.pos);
            px.pos = st.parentPos[k];
            if (kf.useParentRotation) {
                out = scene::compose(out, px);
            } else {
                out.pos = {out.pos[0] + px.pos[0], out.pos[1] + px.pos[1], out.pos[2] + px.pos[2]};
            }
            // clamp_height with one target: not below it + clamp_height.
            if (shot.clampHeight > 0 && hasTarget && kf.targets.size() == 1) out.pos[2] = std::max(out.pos[2], target[2] + shot.clampHeight);
        }
    }
    st.filtered[k] = true;
    if (hasTarget) reframe(out, target, kf.screenOffset, kf.fieldOfView > 0.01f ? kf.fieldOfView : 0.8f);
    return true;
}

void CameraDirector::update() {
    const double secs = world_.seconds();
    const float dt = float(std::clamp(secs - lastSeconds_, 0.0, 0.25));
    lastSeconds_ = secs;
    if (!current_) {
        hasCamera_ = false;
        dof_ = {};
        return;
    }
    const scene::CamShot& shot = *current_;
    ShotState& st = state_[current_];
    const float frame = float((now(shot) - st.start) * double(World::framesPerUnit(int(shot.anim.rate))));
    play(shot, st, std::max(0.0f, frame), dt);
    Interp& in = world_.interp();
    ScriptObject* o = world_.scriptFor(const_cast<scene::CamShot*>(&shot));
    if (!st.startedSent) {
        st.startedSent = true;
        in.send(o, "shot_started", {}, false);
    }
    if (current_ == &shot && !st.overSent && frame >= shot.lengthFrames()) {
        st.overSent = true;
        if (!sequence_.empty()) {
            const scene::CamShot* next = sequence_.front();
            sequence_.erase(sequence_.begin());
            forceShot(next);
        } else {
            in.send(o, "shot_over", {}, false);
        }
    }
}

// SetFrame 0x8222a068 -> keyframes 0x82226448, apply 0x82227440.
void CameraDirector::play(const scene::CamShot& shot, ShotState& st, float f, float dt) {
    const size_t n = shot.keyframes.size();
    st.target.resize(n), st.parentPos.resize(n), st.filtered.resize(n, false);
    // The keyframe pair: hold i for its duration, then blend over its blend to i + 1.
    size_t a = 0, b = 0;
    float t = 0;
    float s = 0;
    const float length = shot.lengthFrames();
    if (shot.looping && length > 0) f = std::fmod(f, length);
    for (size_t i = 0; i < n; ++i) {
        const auto& k = shot.keyframes[i];
        a = b = i, t = 0;
        if (f <= s + k.duration || i + 1 == n) break;
        if (f < s + k.duration + k.blend) {
            b = i + 1;
            t = k.blend > 0 ? (f - s - k.duration) / k.blend : 1.0f;
            break;
        }
        s += k.duration + k.blend;
    }
    if (shot.looping && a + 1 == n && length > 0 && shot.keyframes[a].blend > 0 && f > s + shot.keyframes[a].duration) {
        b = 0;
        t = (f - s - shot.keyframes[a].duration) / shot.keyframes[a].blend;
    }
    const auto& ka = shot.keyframes[a];
    const auto& kb = shot.keyframes[b];
    if (a != b && ka.blendEase != 0) t = sigmoid(t, ka.blendEase);
    scene::Xfm xa, xb;
    V3 ta{}, tb{};
    bool ha = false, hb = false;
    keyframeXfm(shot, st, a, f, dt, xa, ta, ha);
    if (b != a) keyframeXfm(shot, st, b, f, dt, xb, tb, hb);
    else xb = xa;
    scene::Xfm x;
    x.pos = lerp3(xa.pos, xb.pos, t);
    scene::quatToRot(scene::slerp(rotToQuat(xa.rot), rotToQuat(xb.rot), t), x.rot);
    const float fovA = ka.fieldOfView > 0.01f ? ka.fieldOfView : 0.8f, fovB = kb.fieldOfView > 0.01f ? kb.fieldOfView : 0.8f;
    float fov = fovA + (fovB - fovA) * t + ka.zoomFov + (kb.zoomFov - ka.zoomFov) * t;
    fov = std::clamp(fov, 0.05f, 2.8f);
    // Depth of field: |camera - focal target| x (1 + focus_blur_multiplier).
    dof_ = {};
    if (shot.useDepthOfField) {
        V3 fa, fb;
        const bool hfa = targetPoint(ka.focalTarget, fa), hfb = targetPoint(kb.focalTarget, fb);
        if (hfa && hfb) {
            const float da = len(sub(fa, x.pos)), db = len(sub(fb, x.pos));
            const float m = ka.focusBlurMultiplier + (kb.focusBlurMultiplier - ka.focusBlurMultiplier) * t;
            const float d = da + (db - da) * t;
            dof_ = {true, d * (1 + m), ka.blurDepth + (kb.blurDepth - ka.blurDepth) * t, ka.maxBlur + (kb.maxBlur - ka.maxBlur) * t};
        } else if (hfa) {  // only the outgoing keyframe: max_blur fades out
            const float d = len(sub(fa, x.pos));
            dof_ = {true, d * (1 + ka.focusBlurMultiplier), ka.blurDepth, ka.maxBlur * (1 - t)};
        } else if (hfb) {  // only the incoming one: it fades in
            const float d = len(sub(fb, x.pos));
            dof_ = {true, d * (1 + kb.focusBlurMultiplier), kb.blurDepth, kb.maxBlur * t};
        }
    }
    // Shake (0x82224D18): random impulses (probability noisefreq per 1/60 s
    // frame, size noiseamp, angles within +-maxangle degrees) followed by a
    // spring (0.02 x distance) with velocity damping 0.9.
    const float freq = ka.shakeNoiseFreq + (kb.shakeNoiseFreq - ka.shakeNoiseFreq) * t;
    const float amp = ka.shakeNoiseAmp + (kb.shakeNoiseAmp - ka.shakeNoiseAmp) * t;
    const float maxX = (ka.shakeMaxAngle[0] + (kb.shakeMaxAngle[0] - ka.shakeMaxAngle[0]) * t) * kPi / 180;
    const float maxZ = (ka.shakeMaxAngle[1] + (kb.shakeMaxAngle[1] - ka.shakeMaxAngle[1]) * t) * kPi / 180;
    for (int step = 0, steps = std::max(1, int(std::lround(dt * 60))); step < steps && dt > 0; ++step) {
        if (random01() < freq) {
            const float ang = random01() * 2 * kPi, mag = random01() * amp;
            st.shakeTgt[0] += std::cos(ang + kPi / 2) * mag;
            st.shakeTgt[1] += std::cos(ang + kPi / 2) * mag * 0.333f;
            st.shakeTgt[2] += std::sin(ang) * mag;
            st.shakeAngTgt[0] += (random01() * 2 - 1) * maxX;
            st.shakeAngTgt[2] += (random01() * 2 - 1) * maxZ;
        }
        const float l = std::sqrt(st.shakeTgt[0] * st.shakeTgt[0] + st.shakeTgt[1] * st.shakeTgt[1] + st.shakeTgt[2] * st.shakeTgt[2]);
        if (l > amp && l > 0)
            for (float& c : st.shakeTgt) c *= amp / l;
        st.shakeAngTgt[0] = std::clamp(st.shakeAngTgt[0], -maxX, maxX);
        st.shakeAngTgt[2] = std::clamp(st.shakeAngTgt[2], -maxZ, maxZ);
        for (int c = 0; c < 3; ++c) {
            st.shakeVel[c] += (st.shakeTgt[c] - st.shakePos[c]) * 0.02f;
            st.shakePos[c] += st.shakeVel[c];
            st.shakeVel[c] *= 0.9f;
            st.shakeAngVel[c] += (st.shakeAngTgt[c] - st.shakeAng[c]) * 0.02f;
            st.shakeAng[c] += st.shakeAngVel[c];
            st.shakeAngVel[c] *= 0.9f;
        }
    }
    for (int c = 0; c < 3; ++c) x.pos[size_t(c)] += st.shakePos[c];
    {
        // Pitch about the camera's right axis, then roll / yaw about its up.
        scene::Xfm r;
        const float cx = std::cos(st.shakeAng[0]), sx = std::sin(st.shakeAng[0]);
        const float cz = std::cos(st.shakeAng[2]), sz = std::sin(st.shakeAng[2]);
        // Local rotation rows (right, forward, up) in camera space.
        const std::array<float, 9> rx = {1, 0, 0, 0, cx, sx, 0, -sx, cx};
        const std::array<float, 9> rz = {cz, sz, 0, -sz, cz, 0, 0, 0, 1};
        std::array<float, 9> l{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) l[size_t(i * 3 + j)] += rz[size_t(i * 3 + k)] * rx[size_t(k * 3 + j)];
        r.rot = l;
        const scene::Xfm base{x.rot, {0, 0, 0}};
        const scene::Xfm turned = scene::compose(r, base);
        x.rot = turned.rot;
    }
    camera_.world = gfx::toMat4(x);
    camera_.yFov = fov;
    camera_.nearPlane = shot.nearPlane > 0 ? shot.nearPlane : 1;
    camera_.farPlane = shot.farPlane > camera_.nearPlane ? shot.farPlane : 10000;
    hasCamera_ = true;
}
