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

V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
float dot3(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
bool normalize3(V3& v) {
    const float l = len(v);
    if (l == 0) return false;
    for (float& c : v) c /= l;
    return true;
}

// The camera at x.pos turned to look at `target` (0x82224BD8 [C]): forward
// = target - pos, the up hint `up` (the keyframe's own up row), then
// 0x8214F7F0's orthonormalisation: right = forward x up, up = right x
// forward. The target lands at the centre; the screen offset is a move of
// the camera (CameraDirector::frameShot).
void lookAt(scene::Xfm& x, const V3& target, const V3& up) {
    V3 f = sub(target, x.pos);
    if (!normalize3(f)) return;
    V3 r = cross(f, up);
    if (!normalize3(r)) return;
    const V3 u = cross(r, f);
    x.rot = {r[0], r[1], r[2], f[0], f[1], f[2], u[0], u[1], u[2]};
}

V3 upRow(const scene::Xfm& x) { return {x.rot[6], x.rot[7], x.rot[8]}; }

// The camera path's transform at `frame` (RndTransAnim 0x821B6B00 into the
// shot's own matrix [C]): the keys owner's position keys (trans_spline:
// the Hermite of scene::sampleTrans) or the origin, its rotation keys or
// none, its scale keys scaling the rows. repeat_trans and follow_path are
// unused by the disc's camera paths [C data] and not done.
scene::Xfm evalPath(const scene::TransAnim& a, float frame) {
    const scene::TransAnim* k = &a;
    if (!a.keysOwner.empty() && a.keysOwner != a.name && a.dir)
        if (auto* o = a.dir->resolve<scene::TransAnim>(a.keysOwner)) k = o;
    scene::Xfm x;
    scene::Quat q;
    if (scene::sampleRotation(k->rotKeys, frame, k->rotSlerp, q)) scene::quatToRot(q, x.rot);
    std::array<float, 3> p;
    if (scene::sampleTrans(k->transKeys, frame, k->transSpline, p)) x.pos = p;
    std::array<float, 3> sc;
    if (scene::sampleTrans(k->scaleKeys, frame, k->scaleSpline, sc))
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) x.rot[size_t(r * 3 + c)] *= sc[size_t(r)];
    return x;
}

// Two keyframes aim at the same targets (0x822266F0 [C]: as many, each of
// the first's (object, subpart) among the second's).
bool sameTargets(const scene::CamShot::Keyframe& a, const scene::CamShot::Keyframe& b) {
    if (a.targets.size() != b.targets.size()) return false;
    for (const auto& t : a.targets) {
        bool found = false;
        for (const auto& u : b.targets) found = found || (t.object == u.object && t.subpart == u.subpart);
        if (!found) return false;
    }
    return true;
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
    using ArgsFn = std::function<bool(const std::string&, const std::vector<Value>&, Value&)>;
    FnObject(const std::string& name, const std::string& cls, Fn fn) : ScriptObject(name, cls), fn_(std::move(fn)) {}
    FnObject(const std::string& name, const std::string& cls, ArgsFn fn) : ScriptObject(name, cls), argsFn_(std::move(fn)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "get" || msg == "set" || msg == "has" || msg == "name") return false;
        r = Value::integer(0);
        return argsFn_ ? argsFn_(msg, args, r) : fn_(msg, r);
    }

private:
    Fn fn_;
    ArgsFn argsFn_;
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

void CameraDirector::provideGameObjects(bool multiplayer, const std::string& mode, std::array<std::string, 2> parts) {
    Interp& in = world_.interp();
    if (!game_ && !in.findObject("game")) {
        const bool vs = multiplayer && (mode == "multi_vs" || mode == "multi_fo");
        game_ = std::make_unique<FnObject>("game", "Game", FnObject::ArgsFn([multiplayer, vs, parts](const std::string& msg, const std::vector<Value>& args, Value& r) {
            if (msg == "multiplayer") r = Value::integer(multiplayer ? 1 : 0);
            else if (msg == "multiplayer_vs") r = Value::integer(vs ? 1 : 0);
            else if (msg == "get_track_sym") r = Value::symbol(parts[!args.empty() && args[0].isNumber() && args[0].integerValue() == 1 ? 1 : 0]);
            else if (msg == "is_up") r = Value::integer(1);
            return true;  // add_sink, remove_sink, want_encore_fx, set_outro_complete...: 0
        }));
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

// The shake's draws come from their own stream, so how many 1/60 s frames a
// shot shook (which depends on where frames fall) never changes the shots
// the director picks next [I: GH2 draws both from its one global RandomGen,
// whose sequence a port cannot follow anyway (it is seeded by the clock)].
float CameraDirector::shakeRandom01() {
    shakeRng_ ^= shakeRng_ << 13, shakeRng_ ^= shakeRng_ >> 17, shakeRng_ ^= shakeRng_ << 5;
    return float(shakeRng_ >> 8) / float(1u << 24);
}

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
    const scene::CamShot* picked = drawShot(categories, filter);
    if (picked && picked != current_) startShot(picked);
    return picked;
}

const scene::CamShot* CameraDirector::drawShot(const std::vector<std::string>& categories,
                                               const std::vector<std::pair<std::string, Value>>& filter) {
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
        if (got && band) {
            nearest_ = band->nearestWaypoint(g.pos, 0xc0);  // the band keeps GH2's waypoint list order
        } else if (got) {
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
    if (world_.log) {
        std::string hides;
        for (const std::string& h : shot->hideList) hides += " " + h;
        SDL_Log("world: shot %s (%s), planes %g..%g%s%s", shot->name.c_str(), shot->category.c_str(), double(shot->nearPlane), double(shot->farPlane),
                hides.empty() ? "" : ", hides", hides.c_str());
    }
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

bool CameraDirector::targetPoint(const World& world, const scene::CamTarget& t, std::array<float, 3>& out) {
    if (t.object.empty()) return false;
    // A band member and its subpart (a bone of its directory).
    if (world.band) {
        scene::Xfm bx;
        if (world.band->objectXfm(t.object, t.subpart, bx)) {
            out = bx.pos;
            return true;
        }
    }
    const bool mp = world.venue().variant() != VenueVariant::Single;
    scene::Xfm x;
    if (!venueObjectXfm(world, t.object, mp, x)) return false;
    out = x.pos;
    // A band slot's bone: its height above the slot (no band until M9).
    if (!world.venue().find(t.object) || world.venue().find(t.object)->cls == "Character" ||
        world.venue().find(t.object)->cls == "BandCharacter")
        out[2] += boneHeight(t.subpart);
    return true;
}

// 0x82226988 [C]: one keyframe's camera.
// - Its targets' mean (0x82226800), low-passed into the keyframe's filtered
//   target: once a 60 Hz frame by filter x min(d, 1), d the distance on the
//   screen (0..1 across) between where the current camera shows the target
//   (0x821D7030) and where the keyframe wants it ((so + 1) / 2, y down).
// - The transform: the shot's path (when it has one) at end_frame x u
//   (u = frame / length, eased by path_ease) with the FIRST keyframe's xfm
//   multiplied in under it (0x8226A630 on keyframes[0] +0x18); else the
//   keyframe's own xfm.
// - The parent (+0x9c): its world transform low-passed by the same factor
//   (rotation slerped, position lerped); with use_parent_rotation the
//   camera is multiplied under it, else its position is added; clamp_height
//   with exactly one target keeps the camera above that target +
//   clamp_height.
// - `look` (the two keyframes aim at different targets): turned to the
//   filtered target with the transform's own up row (0x82224BD8).
// Other frame rates take the same per-second rate: (1 - a)^(dt x 60).
void CameraDirector::keyframeXfm(const World& world, const scene::CamShot& shot, ShotState* st, size_t k, float frame,
                                 const Framing* prev, float dt, bool look, KeyframeCam& out) {
    const auto& kf = shot.keyframes[k];
    V3 sum{0, 0, 0};
    int n = 0;
    for (const auto& t : kf.targets) {
        V3 p;
        if (targetPoint(world, t, p)) sum = {sum[0] + p[0], sum[1] + p[1], sum[2] + p[2]}, ++n;
    }
    out.hasTarget = n > 0;
    const V3 raw = n > 0 ? V3{sum[0] / float(n), sum[1] / float(n), sum[2] / float(n)} : V3{0, 0, 0};
    // The filter factor from the target's place on the current camera's screen.
    float factor = 1;
    if (prev && prev->valid) {
        const scene::Xfm& c = prev->xfm;
        const V3 d = sub(raw, c.pos);
        const float x = dot3(d, {c.rot[0], c.rot[1], c.rot[2]}), y = dot3(d, {c.rot[3], c.rot[4], c.rot[5]}),
                    z = dot3(d, {c.rot[6], c.rot[7], c.rot[8]});
        const float cot = 1.0f / std::tan(prev->fov * 0.5f), aspect = gfx::pictureAspect();  // the picture's (4:3 or 16:9)
        float sx = x * cot / aspect, sy = z * cot;
        if (y != 0) sx /= y, sy /= y;
        const float ex = 0.5f * (sx - kf.screenOffset[0]), ey = 0.5f * (sy - kf.screenOffset[1]);
        factor = std::min(std::sqrt(ex * ex + ey * ey), 1.0f);
    }
    const bool snap = !st || !st->filtered[k] || shot.filter <= 0;
    const float a = snap ? 1.0f : 1.0f - std::pow(1.0f - std::clamp(shot.filter * factor, 0.0f, 1.0f), std::max(0.0f, dt * 60.0f));
    if (st) {
        st->target[k] = snap ? raw : lerp3(st->target[k], raw, a);
        out.target = st->target[k];
    } else {
        out.target = raw;
    }
    scene::Xfm& x = out.xfm;
    x = kf.xfm;
    if (!shot.path.empty())
        if (auto* path = world.venue().find<scene::TransAnim>(shot.path)) {
            const float length = shot.lengthFrames();
            float u = length > 0 ? std::clamp(frame / length, 0.0f, 1.0f) : 0.0f;
            if (shot.pathEase != 0) u = sigmoid(u, shot.pathEase);
            x = scene::compose(shot.keyframes[0].xfm, evalPath(*path, gfx::ScenePose::endFrame(path) * u));
        }
    if (!kf.parent.object.empty()) {
        const bool mp = world.venue().variant() != VenueVariant::Single;
        scene::Xfm px;
        const bool bandParent = world.band && world.band->objectXfm(kf.parent.object, kf.parent.subpart, px);
        if (bandParent || venueObjectXfm(world, kf.parent.object, mp, px)) {
            V3 p;
            if (targetPoint(world, kf.parent, p)) px.pos = p;
            if (st) {
                scene::Xfm& f = st->parent[k];
                if (snap) {
                    f = px;
                } else {
                    scene::quatToRot(scene::slerp(rotToQuat(f.rot), rotToQuat(px.rot), a), f.rot);
                    f.pos = lerp3(f.pos, px.pos, a);
                }
                px = f;
            }
            if (kf.useParentRotation) x = scene::compose(x, px);
            else x.pos = {x.pos[0] + px.pos[0], x.pos[1] + px.pos[1], x.pos[2] + px.pos[2]};
            // clamp_height with one target: not below it + clamp_height.
            if (shot.clampHeight > 0 && kf.targets.size() == 1) {
                V3 t;
                if (targetPoint(world, kf.targets[0], t)) x.pos[2] = std::max(x.pos[2], t[2] + shot.clampHeight);
            }
        }
    }
    if (st) st->filtered[k] = true;
    if (look && out.hasTarget) lookAt(x, out.target, upRow(x));
}

// SetFrame 0x8222A068 -> keyframes 0x82226448, apply 0x82227440 [C]: the
// keyframe pair (hold i for its duration, then blend over its blend to
// i + 1, eased by i's blend_ease), each keyframe's camera (keyframeXfm),
// position lerped and rotation slerped. When both aim at the same targets
// the blended camera is turned to each keyframe's filtered target (its up
// row the hint) and those two slerped; then it is moved in its own plane so
// the target lands at the screen offset: by (-so.x d tan(fov / 2) aspect,
// 0, -so.y d tan(fov / 2)) in camera space (0x8214F950; the projection's
// +0xf4 / +0x118), d the distance to the target(s) and so the offset, both
// lerped, fov the keyframes' (without zoom_fov). Field of view: lerped,
// then zoom_fov lerped and added. Depth of field from the moved camera.
void CameraDirector::frameShot(const World& world, const scene::CamShot& shot, ShotState* st, float f, const Framing* prev, float dt,
                               Framing& out, Dof* dof) {
    const size_t n = shot.keyframes.size();
    if (st) st->target.resize(n), st->parent.resize(n), st->filtered.resize(n, false);
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
    const bool same = sameTargets(ka, kb);
    KeyframeCam ca, cb;
    keyframeXfm(world, shot, st, a, f, prev, dt, !same, ca);
    if (b != a) keyframeXfm(world, shot, st, b, f, prev, dt, !same, cb);
    else cb = ca;
    scene::Xfm x;
    x.pos = lerp3(ca.xfm.pos, cb.xfm.pos, t);
    scene::quatToRot(scene::slerp(rotToQuat(ca.xfm.rot), rotToQuat(cb.xfm.rot), t), x.rot);
    const float fovA = ka.fieldOfView > 0.01f ? ka.fieldOfView : 0.8f, fovB = kb.fieldOfView > 0.01f ? kb.fieldOfView : 0.8f;
    const float fov0 = fovA + (fovB - fovA) * t;
    if (ca.hasTarget || cb.hasTarget) {
        if (same) {
            scene::Xfm xa = x, xb = x;
            if (ca.hasTarget) lookAt(xa, ca.target, upRow(x));
            if (cb.hasTarget) lookAt(xb, cb.target, upRow(x));
            scene::quatToRot(scene::slerp(rotToQuat(xa.rot), rotToQuat(xb.rot), t), x.rot);
        }
        float d = 0;
        std::array<float, 2> so{};
        if (ca.hasTarget && cb.hasTarget) {
            const float da = len(sub(ca.target, x.pos)), db = len(sub(cb.target, x.pos));
            d = da + (db - da) * t;
            so = {ka.screenOffset[0] + (kb.screenOffset[0] - ka.screenOffset[0]) * t,
                  ka.screenOffset[1] + (kb.screenOffset[1] - ka.screenOffset[1]) * t};
        } else if (ca.hasTarget) {
            d = len(sub(ca.target, x.pos)), so = ka.screenOffset;
        } else {
            d = len(sub(cb.target, x.pos)), so = kb.screenOffset;
        }
        if (same) {
            const float th = std::tan(std::clamp(fov0, 0.05f, 2.8f) * 0.5f), aspect = gfx::pictureAspect();  // the picture's (4:3 or 16:9)
            const float ox = -so[0] * d * th * aspect, oz = -so[1] * d * th;
            for (int c = 0; c < 3; ++c) x.pos[size_t(c)] += ox * x.rot[size_t(c)] + oz * x.rot[size_t(6 + c)];
        }
    }
    float fov = fov0 + ka.zoomFov + (kb.zoomFov - ka.zoomFov) * t;
    out.xfm = x;
    out.a = a, out.b = b, out.t = t;
    out.fov = std::clamp(fov, 0.05f, 2.8f);
    out.valid = true;
    if (!dof) return;
    // Depth of field: |camera - focal target| x (1 + focus_blur_multiplier).
    *dof = {};
    if (shot.useDepthOfField) {
        V3 fa, fb;
        const bool hfa = targetPoint(world, ka.focalTarget, fa), hfb = targetPoint(world, kb.focalTarget, fb);
        if (hfa && hfb) {
            const float da = len(sub(fa, x.pos)), db = len(sub(fb, x.pos));
            const float m = ka.focusBlurMultiplier + (kb.focusBlurMultiplier - ka.focusBlurMultiplier) * t;
            const float dd = da + (db - da) * t;
            *dof = {true, dd * (1 + m), ka.blurDepth + (kb.blurDepth - ka.blurDepth) * t, ka.maxBlur + (kb.maxBlur - ka.maxBlur) * t};
        } else if (hfa) {  // only the outgoing keyframe: max_blur fades out
            const float dd = len(sub(fa, x.pos));
            *dof = {true, dd * (1 + ka.focusBlurMultiplier), ka.blurDepth, ka.maxBlur * (1 - t)};
        } else if (hfb) {  // only the incoming one: it fades in
            const float dd = len(sub(fb, x.pos));
            *dof = {true, dd * (1 + kb.focusBlurMultiplier), kb.blurDepth, kb.maxBlur * t};
        }
    }
}

gfx::Camera CameraDirector::evaluate(const World& world, const scene::CamShot& shot, float frame) {
    gfx::Camera cam;
    if (shot.keyframes.empty()) return cam;
    Framing fr;
    frameShot(world, shot, nullptr, frame, nullptr, 0, fr, nullptr);
    cam.world = gfx::toMat4(fr.xfm);
    cam.yFov = fr.fov;
    cam.nearPlane = shot.nearPlane > 0 ? shot.nearPlane : 1;
    cam.farPlane = shot.farPlane > cam.nearPlane ? shot.farPlane : 10000;
    return cam;
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

// The shot's frame (frameShot), then the shake (0x82224D18 [C]): per 1/60 s
// an impulse with probability noisefreq (random direction, size noiseamp,
// y a third; angles within +-shake_maxangle degrees about x and z) on a
// spring (0.02 x distance, velocity x 0.9); the offset is in the camera's
// own axes (0x8214F950), the angles turn it about its right and up [I].
void CameraDirector::play(const scene::CamShot& shot, ShotState& st, float f, float dt) {
    Framing fr;
    frameShot(world_, shot, &st, f, &last_, dt, fr, &dof_);
    scene::Xfm x = fr.xfm;
    const auto& ka = shot.keyframes[fr.a];
    const auto& kb = shot.keyframes[fr.b];
    const float t = fr.t;
    // Shake (0x82224D18): random impulses (probability noisefreq per 1/60 s
    // frame, size noiseamp, angles within +-maxangle degrees) followed by a
    // spring (0.02 x distance) with velocity damping 0.9.
    const float freq = ka.shakeNoiseFreq + (kb.shakeNoiseFreq - ka.shakeNoiseFreq) * t;
    const float amp = ka.shakeNoiseAmp + (kb.shakeNoiseAmp - ka.shakeNoiseAmp) * t;
    const float maxX = (ka.shakeMaxAngle[0] + (kb.shakeMaxAngle[0] - ka.shakeMaxAngle[0]) * t) * kPi / 180;
    const float maxZ = (ka.shakeMaxAngle[1] + (kb.shakeMaxAngle[1] - ka.shakeMaxAngle[1]) * t) * kPi / 180;
    // GH2's 1/60 s frames: whole ones only, the remainder carried to the next
    // frame (one impulse draw and spring step per 1/60 s at any frame rate).
    st.shakeSteps += dt * 60;
    const int steps = int(std::min(st.shakeSteps + 0.01f, 15.0f));
    st.shakeSteps -= float(steps);
    if (st.shakeSteps > 1) st.shakeSteps = 0;
    for (int step = 0; step < steps; ++step) {
        if (shakeRandom01() < freq) {
            const float ang = shakeRandom01() * 2 * kPi, mag = shakeRandom01() * amp;
            // 0x8226E050 is GH2's table sine: x and y from sin(a + pi / 2).
            st.shakeTgt[0] += std::sin(ang + kPi / 2) * mag;
            st.shakeTgt[1] += std::sin(ang + kPi / 2) * mag * 0.333f;
            st.shakeTgt[2] += std::sin(ang) * mag;
            st.shakeAngTgt[0] += (shakeRandom01() * 2 - 1) * maxX;
            st.shakeAngTgt[2] += (shakeRandom01() * 2 - 1) * maxZ;
        }
        const float l = std::sqrt(st.shakeTgt[0] * st.shakeTgt[0] + st.shakeTgt[1] * st.shakeTgt[1] + st.shakeTgt[2] * st.shakeTgt[2]);
        // Past noiseamp the excess is taken back from it (|v| -> 2 amp - |v|).
        if (l > amp && l > 0)
            for (float& c : st.shakeTgt) c *= (2 * amp - l) / l;
        // 0x82224D18's limit: the excess |a| - max taken back towards zero.
        // A clamp for max >= 0; some shots' keyframes (theatre) have a
        // negative max angle, where GH2 lands on -max with the sign flipped
        // (std::clamp with hi < lo is undefined: an abort with assertions).
        auto limit = [](float a, float max) {
            const float e = std::fabs(a) - max;
            return e > 0 ? a + (a > 0 ? -e : e) : a;
        };
        st.shakeAngTgt[0] = limit(st.shakeAngTgt[0], maxX);
        st.shakeAngTgt[2] = limit(st.shakeAngTgt[2], maxZ);
        for (int c = 0; c < 3; ++c) {
            st.shakeVel[c] += (st.shakeTgt[c] - st.shakePos[c]) * 0.02f;
            st.shakePos[c] += st.shakeVel[c];
            st.shakeVel[c] *= 0.9f;
            st.shakeAngVel[c] += (st.shakeAngTgt[c] - st.shakeAng[c]) * 0.02f;
            st.shakeAng[c] += st.shakeAngVel[c];
            st.shakeAngVel[c] *= 0.9f;
        }
    }
    // The offset in the camera's own axes (0x8214F950). Enhanced Options >
    // Disable Shaky Camera: the shake still runs (the same draws), unseen.
    if (shake_)
        for (int c = 0; c < 3; ++c)
            x.pos[size_t(c)] += st.shakePos[0] * x.rot[size_t(c)] + st.shakePos[1] * x.rot[size_t(3 + c)] + st.shakePos[2] * x.rot[size_t(6 + c)];
    if (shake_) {
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
    last_ = fr;
    last_.xfm = x;  // the next frame's filter sees this camera
    camera_.yFov = fr.fov;
    camera_.nearPlane = shot.nearPlane > 0 ? shot.nearPlane : 1;
    camera_.farPlane = shot.farPlane > camera_.nearPlane ? shot.farPlane : 10000;
    hasCamera_ = true;
}
