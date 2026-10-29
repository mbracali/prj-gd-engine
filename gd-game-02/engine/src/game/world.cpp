// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/world.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>

#include "char/char_objects.h"
#include "data/game_data.h"
#include "audio/synth.h"
#include "game/band.h"
#include "game/crowd.h"
#include "scene/synth_objects.h"
#include "script/tasks.h"
#include "ui/sfx_args.h"

namespace {

constexpr float kHuge = 1e30f;

// config/macros.dta WORLDEVENTS (SYSTEMWORLDEVENTS) [C data]: the venue
// events; the world answers them (its EventTriggers) even when no trigger
// of this venue listens.
const std::set<std::string>& worldEvents() {
    static const std::set<std::string> s = {
        "chorus", "verse", "solo", "venue_effect", "excitement_bad", "excitement_okay", "excitement_great", "intro_skip",
        "intro_start", "intro_end", "kick_drum", "crash_symbal", "bass_hit", "post_switch_cam", "music_start", "game_over",
        "game_won", "game_lost", "campaign_won", "encore_lost", "crowd_update", "crowd_hide", "crowd_half_tempo",
        "crowd_double_tempo", "crowd_normal_tempo", "crowd_lighters_slow", "crowd_lighters_fast", "crowd_lighters_off",
        "build_streak", "single_player", "multi_player", "hit_p0_fret1", "hit_p0_fret2", "hit_p0_fret3", "hit_p0_fret4",
        "hit_p0_fret5", "hit_p1_fret1", "hit_p1_fret2", "hit_p1_fret3", "hit_p1_fret4", "hit_p1_fret5", "play_encore_fx",
        "skip_encore_fx", "encore_outro", "hit_snare", "hit_hihat", "peak_on", "peak_off", "star_power_ready",
        "star_power_start", "star_power_stop", "drummer_explode", "start",
        // Also events of the triggers on the disc that WORLDEVENTS does not list.
        "excitement_boot", "excitement_peak", "intro", "game_won_msg"};
    return s;
}

// The WorldDir's native methods not implemented yet (the camera director
// and lighting presets are game/camera_director.h and game/light_presets.h,
// through addNatives): they answer 0 and are reported as STUBs.
const std::set<std::string>& directorStubs() {
    // set_fullness / iterate_frac are the WorldCrowd's (game/crowd.h).
    static const std::set<std::string> s = {};
    return s;
}

std::string text(Interp& in, const Value& v) {
    const Value e = in.evalValue(v);
    return e.type == Value::Type::Object && e.object ? e.object->name() : e.str();
}

// A DataArray (header properties: alternating key, value) as script properties.
void propsFromHeader(Interp& in, const scene::ObjectHeader& h, std::map<std::string, Value>& props) {
    if (!h.hasProps) return;
    const auto& c = h.props.children;
    std::function<Value(const scene::DataNode&)> conv = [&](const scene::DataNode& n) -> Value {
        switch (n.type) {
        case 0: return Value::integer(int32_t(n.raw));
        case 1: return Value::real(n.asFloat());
        case 0x10: case 0x11: case 0x13: {
            std::vector<Value> v;
            for (const auto& k : n.children) v.push_back(conv(k));
            return Value::list(std::move(v));
        }
        case 0x12: return Value::string(n.str);
        default: return Value::symbol(n.str);
        }
    };
    (void)in;
    for (size_t i = 0; i + 1 < c.size(); i += 2) props[c[i].str] = conv(c[i + 1]);
}

// The (types (<type> ...)) entry for a class in an (objects ...) tree; the
// tree may hold several arrays of the class (#merge leaves them apart when
// they come from different files).
const DataNode* typeDef(const DataNode* objects, const std::string& cls, const std::string& type) {
    if (!objects || type.empty()) return nullptr;
    for (const DataNode& c : objects->children) {
        if (!c.isList() || c.children.empty() || !c.children[0].isText() || c.children[0].s != cls) continue;
        if (const DataNode* types = c.find("types"))
            if (const DataNode* t = types->find(type)) return t;
    }
    return nullptr;
}

}  // namespace

// ---- script objects --------------------------------------------------------

// `world`: the WorldDir's script type, then World::native.
class World::WorldObj : public ScriptObject {
public:
    explicit WorldObj(World& w) : ScriptObject("world", "WorldDir"), w_(w) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        bool handled = false;
        r = w_.native(msg, args, handled);
        return handled;
    }

private:
    World& w_;
};

// A venue object by name: animatables animate, drawables show and hide,
// EventTriggers trigger, proxy directories run their script type.
class World::VenueObj : public ScriptObject {
public:
    VenueObj(World& w, scene::Object* o) : ScriptObject(o->name, o->cls), w_(w), o_(o) {}
    scene::Object* object() const { return o_; }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        for (const ObjectHandler& h : w_.objectNatives_)
            if (msg != "get" && h(in, *o_, msg, args, r)) return true;
        if (msg == "animate") return animate(in, args), true;
        if (msg == "stop_animation") return w_.stopAnimation(o_), true;
        if (msg == "set_frame") return w_.setFrame(o_, args.empty() ? 0.0f : in.evalValue(args[0]).number()), true;
        if (msg == "frame") return r = Value::real(w_.currentFrame(o_)), true;
        if (msg == "end_frame") return r = Value::real(w_.endFrameOf(o_)), true;
        if (msg == "start_frame") return r = Value::real(w_.startFrameOf(o_)), true;
        if (msg == "is_animating") return r = Value::integer(w_.animating(o_) ? 1 : 0), true;
        if (msg == "set_showing") return w_.setShowing(o_, !args.empty() && in.evalValue(args[0]).truthy()), true;
        if (msg == "showing") return r = Value::integer(w_.pose_.showing(o_) ? 1 : 0), true;
        if (msg == "dir") return r = Value::obj(w_.script()), true;
        if (auto* t = o_->as<scene::EventTrigger>()) {
            if (msg == "trigger") return w_.trigger(t), true;
            if (msg == "enable") return w_.setTriggerEnabled(t, true), true;
            if (msg == "disable") return w_.setTriggerEnabled(t, false), true;
        }
        if (msg == "show_objects") return w_.showObjects(in, args), true;
        if (msg == "find") {
            if (w_.band)
                if (Band::Member* m = w_.band->memberOf(o_); m && m->script)
                    return r = in.send(m->script.get(), "find", args, false), true;
            scene::Object* f = args.empty() ? nullptr : w_.venue_.find(text(in, args[0]));
            return r = Value::obj(f ? w_.objectFor(f) : nullptr), true;
        }
        if ((o_->as<scene::Sfx>() || o_->as<scene::SequenceObj>()) && (msg == "play" || msg == "stop")) {
            if (w_.log) SDL_Log("world: sound %s %s", o_->name.c_str(), msg.c_str());
            if (msg == "stop") {
                if (w_.synth_) w_.synth_->stop(*o_);
                else in.onStub("world_sfx", o_->name + " " + msg, r);
                return true;
            }
            ++w_.stats_.sounds;
            // (volume {options get_fx_volume}) and the like (ui/sfx_args.h).
            const SfxArgs a = sfxArgs(in, args, 0);
            if (w_.synth_) w_.synth_->play(*o_, a.volume, a.pan, a.transpose);
            else in.onStub("world_sfx", o_->name + " " + msg, r);
            return true;
        }
        // The band's characters (M9): their methods answer 0 for now.
        if ((o_->cls == "Character" || o_->cls == "BandCharacter") && msg != "get" && msg != "set" && msg != "has" && msg != "name") {
            in.onStub(o_->name, msg, r);
            return true;
        }
        return false;
    }

    Value getProp(Interp& in, const std::string& key) override {
        std::vector<Value> a{Value::symbol(key)};
        Value r = Value::integer(0);
        for (const ObjectHandler& h : w_.objectNatives_)
            if (h(in, *o_, "get", a, r)) return r;
        return ScriptObject::getProp(in, key);
    }

private:
    // RndAnimatable `animate` options (GH2's OnAnimate [I: the option names
    // the disc's scripts use]): (range a b) (loop a b) (dest d) (period p)
    // (units u) (delay d) (blend b) (wait w) (scale s) (name n).
    void animate(Interp& in, std::vector<Value>& args) {
        AnimArgs a;
        for (const Value& raw : args) {
            const Value v = in.evalValue(raw);
            if (v.type != Value::Type::Array || !v.array || v.array->empty()) continue;
            const auto& l = *v.array;
            const std::string key = text(in, l[0]);
            auto num = [&](size_t k) { return k < l.size() ? in.evalValue(l[k]).number() : 0.0f; };
            if (key == "range" || key == "loop") {
                a.filter = true, a.start = num(1), a.end = num(2), a.type = key == "loop" ? 1 : 0;
            } else if (key == "dest") {
                a.fromCurrent = true, a.end = num(1);
            } else if (key == "period") {
                a.period = num(1);
            } else if (key == "units") {
                a.units = int(num(1));
            } else if (key == "delay") {
                a.delay = num(1);
            } else if (key == "blend") {
                a.blend = num(1);
            } else if (key == "wait") {
                a.wait = in.evalValue(l.size() > 1 ? l[1] : Value::integer(0)).truthy();
            } else if (key == "scale") {
                a.scale = num(1);
            }
        }
        w_.animate(o_, a);
    }

    World& w_;
    scene::Object* o_;
};

// One of the chain's directories (lighting, chars, geom): its RndDir type's
// handlers, and show_objects.
class DirScript : public ScriptObject {
public:
    DirScript(const std::string& name, std::function<void(Interp&, std::vector<Value>&)> show)
        : ScriptObject(name, "RndDir"), show_(std::move(show)) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "show_objects") return show_(in, args), true;
        return false;
    }

private:
    std::function<void(Interp&, std::vector<Value>&)> show_;
};

// ---- World -----------------------------------------------------------------

World::World(Venue& venue, Interp* interp, const DataNode* objects) : venue_(venue), interp_(interp), objects_(objects) {
    const char* l = SDL_getenv("GD02_WORLDLOG");
    log = l && *l && *l != '0';
    if (!interp_) {
        own_ = std::make_unique<Interp>();
        interp_ = own_.get();
        loadOwnScripts();
    }
    for (const scene::Waypoint* w : venue_.all<scene::Waypoint>()) waypoints_.push_back(w);
    registerTriggers();
    attach();
    crowd_ = std::make_unique<Crowd>(*this);
}

void World::waypointToEnd(const scene::Waypoint* w) {
    auto it = std::find(waypoints_.begin(), waypoints_.end(), w);
    if (it == waypoints_.end()) return;
    waypoints_.erase(it);
    waypoints_.push_back(w);
}

World::~World() {
    stopHolding();
    if (synth_)
        for (const auto& p : venue_.parts()) synth_->forget(p.milo->root());
    if (synth_)
        for (const auto& p : venue_.proxies())
            if (p.milo) synth_->forget(p.milo->root());
    crowd_.reset();
    tasks_.clear();
    if (!interp_) return;
    interp_->tasks().killFor(world_.get());
    for (auto& [o, v] : objs_) interp_->tasks().killFor(v.get());
    interp_->unregisterName("world", world_.get());
    if (prevWorld_) interp_->registerAs("world", prevWorld_);
    interp_->resolver = prevResolver_;
}

void World::loadOwnScripts() {
    try {
        ScriptLoader loader(venue_.data());
        objects_ = &interp_->keep(loader.load("world/world_objects.dta"));
    } catch (const std::exception& e) {
        SDL_Log("world: world/world_objects.dta: %s", e.what());
    }
    // Objects the world's scripts ask for that only the game has: stubs
    // (the band, M9; player0's Star Power) so the scripts run.
    for (const char* name : {"player0", "player1"}) {
        auto s = std::make_unique<StubObject>(name, "Player");
        interp_->registerObject(s.get());
        dirObjs_.push_back(std::move(s));
    }
}

void World::attach() {
    world_ = std::make_unique<WorldObj>(*this);
    const scene::DirObj* wd = venue_.worldDir();
    // The venue's type is its WorldDir header type (arena, small1...).
    const std::string type = wd && !wd->header.type.empty() ? wd->header.type : venue_.name();
    if (const DataNode* def = typeDef(objects_, "WorldDir", type)) world_->setTypeDef(def, 1);
    else SDL_Log("world: no WorldDir type '%s' in the scripts", type.c_str());
    if (wd) propsFromHeader(*interp_, wd->header, world_->props);
    // The chain's RndDir types (lighting, chars, geom).
    for (const auto& p : venue_.parts()) {
        if (p.milo == &venue_.world()) continue;
        const auto* own = p.milo->root().self ? p.milo->root().self->as<scene::DirObj>() : nullptr;
        if (!own || own->header.type.empty()) continue;
        auto o = std::make_unique<DirScript>(p.milo->root().name, [this](Interp& in, std::vector<Value>& a) { showObjects(in, a); });
        if (const DataNode* def = typeDef(objects_, "RndDir", own->header.type)) o->setTypeDef(def, 1);
        propsFromHeader(*interp_, own->header, o->props);
        dirObjs_.push_back(std::move(o));
    }
    prevWorld_ = interp_->findObject("world");
    interp_->registerObject(world_.get());
    prevResolver_ = interp_->resolver;
    interp_->resolver = [this](const std::string& name) -> ScriptObject* {
        if (prevResolver_)
            if (ScriptObject* o = prevResolver_(name)) return o;
        return resolveName(name);
    };
    // {waypoint_find flags} (0x821956C8 -> 0x821954F8): the first waypoint
    // of the global list with any of the flags.
    interp_->addBuiltin("waypoint_find", [this](Interp& in, const DataNode& c) {
        const uint32_t flags = uint32_t(in.evalArg(c, 1).integerValue());
        for (const scene::Waypoint* w : waypoints_)
            if (w->flags & flags) return Value::obj(objectFor(const_cast<scene::Waypoint*>(w)));
        return Value::integer(0);
    });
    // {waypoint_nearest object flags} (0x82195738 -> Waypoint::Nearest
    // 0x82195548): the waypoint with any of the flags nearest the object.
    interp_->addBuiltin("waypoint_nearest", [this](Interp& in, const DataNode& c) {
        const std::string name = text(in, in.evalArg(c, 1));
        const uint32_t flags = uint32_t(in.evalArg(c, 2).integerValue());
        scene::Xfm x;
        bool got = band && band->objectXfm(name, "", x);
        if (!got)
            if (const scene::Object* o = venue_.find(name)) x = pose_.world(o), got = true;
        if (!got) return Value::integer(0);
        const scene::Waypoint* best = nullptr;
        float bd = 1e30f;
        for (const scene::Waypoint* w : waypoints_) {
            if (!(w->flags & flags)) continue;
            const auto q = pose_.world(w).pos;
            const float dx = x.pos[0] - q[0], dy = x.pos[1] - q[1], dz = x.pos[2] - q[2];
            const float d = dy * dy + dx * dx + dz * dz;
            if (d < bd) bd = d, best = w;
        }
        return best ? Value::obj(objectFor(const_cast<scene::Waypoint*>(best))) : Value::integer(0);
    });
    // {waypoint_last w} (0x821957C0): w to the end of the global list (the
    // start waypoint after a teleport: later walks prefer the others).
    interp_->addBuiltin("waypoint_last", [this](Interp& in, const DataNode& c) {
        const std::string name = text(in, in.evalArg(c, 1));
        if (const scene::Waypoint* w = venue_.find<scene::Waypoint>(name)) waypointToEnd(w);
        return Value::integer(0);
    });
}

ScriptObject* World::script() { return world_.get(); }

ScriptObject* World::objectFor(scene::Object* o) {
    if (!o) return nullptr;
    auto& slot = objs_[o];
    if (!slot) {
        slot = std::make_unique<VenueObj>(*this, o);
        // A proxy directory runs its own milo's type (small1's bottle_throw).
        if (const Venue::Proxy* p = venue_.proxyOf(o)) {
            slot->nameScope = o->name;  // its own names (each bottle's throw_task)
            const auto* own = p->milo->root().self ? p->milo->root().self->as<scene::DirObj>() : nullptr;
            if (own && !own->header.type.empty())
                if (const DataNode* def = typeDef(objects_, "RndDir", own->header.type)) slot->setTypeDef(def, 1);
        }
        // Its class's script type by its header type (CamShot band: camshot.dta) [C data].
        if (!slot->typeDef() && !o->header.type.empty())
            if (const DataNode* def = typeDef(objects_, o->cls, o->header.type)) slot->setTypeDef(def, 1);
        propsFromHeader(*interp_, o->header, slot->props);
    }
    return slot.get();
}

ScriptObject* World::resolveName(const std::string& name) {
    if (name.empty()) return nullptr;
    for (const auto& d : dirObjs_)
        if (d->name() == name) return d.get();
    return objectFor(venue_.find(name));
}

void World::setClock(double seconds, double beats) {
    seconds_ = seconds;
    beats_ = beats;
    if (own_) {
        own_->tasks().setTime(kTaskSeconds, seconds);
        own_->tasks().setTime(kTaskBeats, beats);
        own_->tasks().setTime(kTaskUISeconds, seconds);
        own_->tasks().setTime(kTaskTutorialSeconds, seconds);
    } else if (interp_) {
        // The menus' interpreter: the game sets the TaskMgr's seconds from
        // the song before the world and the band poll (0x822CDEB8) [C]; the
        // UI keeps it there for the frame (UiManager::taskSeconds).
        interp_->tasks().setTime(kTaskSeconds, seconds);
    }
}

double World::now(int units) const { return units == kTaskBeats ? beats_ : seconds_; }

float World::framesPerUnit(int rate) {
    switch (rate) {
    case 1: return 480;  // k480_fpb
    case 3: return 1;    // k1_fpb
    default: return 30;  // k30_fps, k30_fps_ui, k30_fps_tutorial
    }
}

int World::unitsOf(int rate) { return rate == 1 || rate == 3 ? kTaskBeats : kTaskSeconds; }

void World::update() {
    if (own_) own_->tasks().poll();
    crowd_->update();
    // runTask only sets frames, so the list stays as it is while it runs.
    for (AnimTask& t : tasks_) runTask(t);
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [](const AnimTask& t) { return t.anim == nullptr; }), tasks_.end());
    for (size_t i = 0; i < hides_.size();) {
        if (seconds_ >= hides_[i].at) {
            pose_.setShowing(hides_[i].object, false);
            hides_.erase(hides_.begin() + std::ptrdiff_t(i));
        } else {
            ++i;
        }
    }
}

// One step of an AnimTask: frames advance by fpu x scale per unit from its
// start; range tasks end on `to`, loop / shuttle tasks run until replaced.
void World::runTask(AnimTask& t) {
    const double n = now(t.units);
    if (n < t.startTime) return;
    const float len = std::fabs(t.to - t.from);
    const float dir = t.to >= t.from ? 1.0f : -1.0f;
    const float f = float((n - t.startTime) * double(t.fpu) * double(t.scale));
    float frame;
    bool done = false;
    if (t.type == 1 && len > 0) {
        frame = t.from + dir * std::fmod(f, len);
    } else if (t.type == 2 && len > 0) {
        float p = std::fmod(f, 2 * len);
        frame = t.from + dir * (p <= len ? p : 2 * len - p);
    } else if (f >= len) {
        frame = t.to;
        done = true;
    } else {
        frame = t.from + dir * f;
    }
    const scene::Object* anim = t.anim;
    setFrame(anim, frame);
    if (done) {
        if (log) SDL_Log("world: %s done at frame %g", anim->name.c_str(), double(frame));
        t.anim = nullptr;
    }
}

int World::rateOf(const scene::Object* o) const {
    if (auto* a = o->as<scene::TransAnim>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::MatAnim>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::AnimFilter>()) return int(a->animatable.rate);
    if (auto* a = o->as<scene::Group>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::ParticleSys>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::ParticleSysAnim>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::MeshAnim>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::LightAnim>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::EnvAnim>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::CamShot>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::LightPreset>()) return int(a->anim.rate);
    if (auto* a = o->as<scene::DirObj>(); a && a->hasRnd) return int(a->rnd.anim.rate);
    return 0;
}

// RndAnimFilter::StartFrame / EndFrame (0x82207AF8 / 0x82207B80) [C]; a
// loop or shuttle filter runs until replaced [I].
float World::startFrameOf(const scene::Object* o) const {
    if (auto* f = o->as<scene::AnimFilter>()) {
        const float fpu = framesPerUnit(int(f->animatable.rate));
        float sc = f->scaleOf(fpu);
        if (sc == 0) sc = 1;
        return (f->start - (f->offset + (f->end < f->start ? f->start - f->end : 0.0f))) / sc;
    }
    return 0;  // [I] keyed anims start at 0
}

float World::endFrameOf(const scene::Object* o) const {
    if (auto* f = o->as<scene::AnimFilter>()) {
        if (f->type != 0) return kHuge;  // loop / shuttle filters run until replaced
        return f->endFrame(framesPerUnit(int(f->animatable.rate)));
    }
    if (auto* g = o->as<scene::Group>()) {
        float e = 0;
        static thread_local int depth = 0;
        if (depth > 16) return 0;
        ++depth;
        for (const std::string& n : g->objects)
            if (const scene::Object* c = g->dir ? g->dir->resolve(n) : nullptr; c && c != g) e = std::max(e, endFrameOf(c));
        --depth;
        return e;
    }
    if (o->as<scene::ParticleSys>()) return kHuge;  // [I] runs while shown
    if (auto* d = o->as<scene::DirObj>())
        if (const Venue::Proxy* p = venue_.proxyOf(d)) {
            float e = 0;
            p->milo->root().forEach([&](scene::Object& c) {
                if (c.as<scene::TransAnim>() || c.as<scene::MatAnim>()) e = std::max(e, gfx::ScenePose::endFrame(&c));
            });
            return e;
        }
    return gfx::ScenePose::endFrame(o);
}

void World::setFrame(const scene::Object* anim, float frame) {
    if (!anim) return;
    frames_[anim] = frame;
    if (auto* f = anim->as<scene::AnimFilter>()) {
        if (const scene::Object* a = f->dir ? f->dir->resolve(f->anim) : nullptr; a && a != f) setFrame(a, f->filter(frame, framesPerUnit(int(f->animatable.rate))));
        return;
    }
    if (auto* g = anim->as<scene::Group>()) {
        static thread_local int depth = 0;
        if (depth > 16) return;
        ++depth;
        for (const std::string& n : g->objects)
            if (const scene::Object* c = g->dir ? g->dir->resolve(n) : nullptr; c && c != g) setFrame(c, frame);
        --depth;
        return;
    }
    if (auto* d = anim->as<scene::DirObj>()) {
        // A proxy directory: RndDir::SetFrame on its anims [I].
        if (const Venue::Proxy* p = venue_.proxyOf(d))
            p->milo->root().forEach([&](scene::Object& c) {
                if (c.as<scene::TransAnim>() || c.as<scene::MatAnim>() || c.as<scene::ParticleSysAnim>()) pose_.setFrame(&c, frame);
            });
        return;
    }
    pose_.setFrame(anim, frame);
}

float World::currentFrame(const scene::Object* anim) const {
    auto it = frames_.find(anim);
    return it == frames_.end() ? 0.0f : it->second;
}

void World::animate(const scene::Object* anim, const AnimArgs& a) {
    if (!anim) return;
    stopAnimation(anim);
    AnimTask t;
    t.anim = anim;
    const int rate = a.rate >= 0 ? a.rate : rateOf(anim);
    t.units = a.units >= 0 ? (a.units == kTaskBeats ? kTaskBeats : kTaskSeconds) : unitsOf(rate);
    t.fpu = framesPerUnit(rate);
    t.type = a.type;
    if (a.filter) {
        t.filtered = true;
        t.from = a.start, t.to = a.end;
        const float len = std::fabs(t.to - t.from);
        t.scale = a.period > 0 ? (len > 0 ? len / (a.period * t.fpu) : 1.0f) : (a.scale != 0 ? std::fabs(a.scale) : 1.0f);
    } else if (a.fromCurrent) {
        t.from = currentFrame(anim), t.to = a.end;
        const float len = std::fabs(t.to - t.from);
        t.scale = a.period > 0 ? (len > 0 ? len / (a.period * t.fpu) : 1.0f) : 1.0f;
        t.type = 0;
    } else {
        t.from = startFrameOf(anim), t.to = endFrameOf(anim);
        t.scale = a.scale != 0 ? std::fabs(a.scale) : 1.0f;
        t.type = 0;
    }
    t.startTime = now(t.units) + double(std::max(0.0f, a.delay));
    ++stats_.animsStarted;
    tasks_.push_back(t);
    // The first frame now (an anim started this frame shows its start).
    if (a.delay <= 0) {
        AnimTask& added = tasks_.back();
        runTask(added);
        if (!added.anim) tasks_.pop_back();
    }
}

void World::stopAnimation(const scene::Object* anim) {
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [&](const AnimTask& t) { return t.anim == anim; }), tasks_.end());
}

void World::stopAnimations(const std::unordered_set<const scene::Object*>& anims) {
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [&](const AnimTask& t) { return anims.count(t.anim) != 0; }), tasks_.end());
    for (const scene::Object* a : anims) frames_.erase(a);
}

bool World::animating(const scene::Object* anim) const {
    return std::any_of(tasks_.begin(), tasks_.end(), [&](const AnimTask& t) { return t.anim == anim; });
}

// ---- EventTriggers ----------------------------------------------------------

void World::registerTriggers() {
    // The chain's triggers answer world events; a proxy's answer its proxy calls.
    for (const auto& p : venue_.parts())
        p.milo->root().forEach([&](scene::Object& o) {
            auto* t = o.as<scene::EventTrigger>();
            if (!t || !t->decoded) return;
            triggers_[t] = {};
            for (const auto& e : t->triggerEvents) byEvent_.emplace(e, t);
            for (const auto& e : t->enableEvents) byEnable_.emplace(e, t);
            for (const auto& e : t->disableEvents) byDisable_.emplace(e, t);
            for (const auto& e : t->waitForEvents) byWaitFor_.emplace(e, t);
        });
}

void World::fire(const std::string& event) {
    ++stats_.eventsFired;
    ++stats_.events[event];
    for (auto [it, end] = byEnable_.equal_range(event); it != end; ++it) triggers_[it->second].enabled = true;
    for (auto [it, end] = byDisable_.equal_range(event); it != end; ++it) triggers_[it->second].enabled = false;
    std::vector<const scene::EventTrigger*> run;
    for (auto [it, end] = byWaitFor_.equal_range(event); it != end; ++it)
        if (triggers_[it->second].armed) {
            triggers_[it->second].armed = false;
            run.push_back(it->second);
        }
    for (auto [it, end] = byEvent_.equal_range(event); it != end; ++it) {
        const scene::EventTrigger* t = it->second;
        TriggerState& st = triggers_[t];
        if (!st.enabled) continue;
        if (!t->waitForEvents.empty()) {
            st.armed = true;
            continue;
        }
        run.push_back(t);
    }
    for (const scene::EventTrigger* t : run) {
        if (log) SDL_Log("world: %s -> %s", event.c_str(), t->name.c_str());
        trigger(t);
    }
}

void World::setTriggerEnabled(const scene::EventTrigger* t, bool on) { triggers_[t].enabled = on; }

// EventTrigger::Trigger (0x82257d00) [I: order of the parts].
bool World::playSound(const std::string& name, const scene::Directory* from, float volumeDb, float pan, float transpose) {
    const scene::Object* o = from ? from->resolve(name) : nullptr;
    if (!o) o = venue_.find(name);
    if (!o || (!o->as<scene::Sfx>() && !o->as<scene::SequenceObj>())) {
        interp_->onStub("world_sfx", name + " play (not in the bank)", Value::integer(0));
        return false;
    }
    ++stats_.sounds;
    if (synth_) synth_->play(*o, volumeDb, pan, transpose);
    else interp_->onStub("world_sfx", name + " play", Value::integer(0));
    return true;
}

void World::stopSound(const std::string& name) {
    if (const scene::Object* o = venue_.find(name); o && synth_) synth_->stop(*o);
}

void World::trigger(const scene::EventTrigger* t) {
    if (!t || !t->dir) return;
    if (auto it = triggers_.find(t); it != triggers_.end() && !it->second.enabled) return;
    ++stats_.triggersFired;
    const scene::Directory& d = *t->dir;
    for (const auto& a : t->anims) {
        if (a.anim.empty()) continue;
        const scene::Object* o = d.resolve(a.anim);
        if (!o) o = venue_.find(a.anim);
        if (!o) continue;
        AnimArgs args;
        args.blend = a.blend, args.wait = a.wait, args.delay = a.delay;
        if (a.enable) {
            args.filter = true;
            args.rate = int(a.rate);
            args.start = a.start, args.end = a.end, args.scale = a.scale, args.period = a.period;
            args.type = a.type == "loop" ? 1 : a.type == "shuttle" ? 2 : 0;
        }
        animate(o, args);
    }
    for (const auto& s : t->sounds) {
        if (log) SDL_Log("world: %s: sound %s", t->name.c_str(), s.c_str());
        playSound(s, &d);
    }
    for (const auto& s : t->shows)
        if (const scene::Object* o = d.resolve(s) ? d.resolve(s) : venue_.find(s)) {
            pose_.setShowing(o, true);
            ++stats_.shows;
            // A hide waiting on it is cancelled [I].
            hides_.erase(std::remove_if(hides_.begin(), hides_.end(), [&](const DelayedHide& h) { return h.object == o; }), hides_.end());
        }
    for (const auto& [name, delay] : t->hideDelays)
        if (const scene::Object* o = d.resolve(name) ? d.resolve(name) : venue_.find(name)) {
            ++stats_.hides;
            if (delay <= 0) pose_.setShowing(o, false);
            else hides_.push_back({o, seconds_ + double(delay)});
        }
    for (const auto& [proxy, call] : t->proxyCalls) proxyCall(proxy, call);
    if (!t->nextRandom.empty())
        if (auto* n = venue_.find<scene::EventTrigger>(t->nextRandom); n && n != t) trigger(n);
}

// A proxy call: the message to the proxy directory's script type, and to
// the triggers of its milo listening for it [I].
void World::proxyCall(const std::string& proxy, const std::string& call) {
    ++stats_.proxyCalls;
    scene::Object* o = venue_.find(proxy);
    if (log) SDL_Log("world: proxy call %s %s%s", proxy.c_str(), call.c_str(), o ? "" : " (no such proxy)");
    if (!o) return;
    interp_->send(objectFor(o), call, {}, false);
    if (const Venue::Proxy* p = venue_.proxyOf(o))
        p->milo->root().forEach([&](scene::Object& c) {
            if (auto* t = c.as<scene::EventTrigger>())
                if (std::find(t->triggerEvents.begin(), t->triggerEvents.end(), call) != t->triggerEvents.end()) trigger(t);
        });
}

// ---- messages ---------------------------------------------------------------

Value World::message(const std::string& msg, std::vector<Value> args) { return interp_->send(world_.get(), msg, std::move(args), true); }

void World::start(bool multiplayer, bool players) {
    interp_->send(world_.get(), "enter", {}, false);
    crowd_->enter();
    // The band's characters enter with the directory (Character / GuitarCharacter Enter).
    started_ = true;
    if (band) band->enter();
    message("start");
    if (players) message(multiplayer ? "multi_player" : "single_player");
}

void World::showObjects(Interp& in, std::vector<Value>& args) {
    if (args.empty()) return;
    const Value list = in.evalValue(args[0]);
    const bool on = args.size() > 1 && in.evalValue(args[1]).truthy();
    auto one = [&](const Value& v) {
        const std::string n = text(in, v);
        if (const scene::Object* o = venue_.find(n)) pose_.setShowing(o, on);
    };
    if (list.type == Value::Type::Array && list.array)
        for (const Value& v : *list.array) one(v);
    else
        one(list);
}

// WorldDir's natives: the events (EventTriggers), the forwarding to the
// chain's directories, and the director stubs.
Value World::native(const std::string& msg, std::vector<Value>& args, bool& handled) {
    handled = true;
    for (const Listener& l : listeners_) l(*interp_, msg, args);
    // The sinks that asked for this message (MsgSource export [I]).
    if (sinks_.count(msg)) {
        std::vector<ScriptObject*> to;
        for (auto [it, end] = sinks_.equal_range(msg); it != end; ++it) to.push_back(it->second);
        for (ScriptObject* s : to) {
            bool held = false;  // a sink removed or deleted by an earlier one's handler is skipped
            for (auto [it, end] = sinks_.equal_range(msg); it != end && !held; ++it) held = it->second == s;
            if (held) interp_->send(s, msg, args, false);
        }
    }
    // The chain's RndDir types first (set_multiplayer, set_player1_bass...).
    bool forwarded = false;
    for (const auto& d : dirObjs_)
        if (d->handler(msg)) {
            interp_->send(d.get(), msg, args, false);
            forwarded = true;
        }
    const bool event = worldEvents().count(msg) || byEvent_.count(msg) || byEnable_.count(msg) || byDisable_.count(msg) ||
                       byWaitFor_.count(msg);
    if (event) {
        fire(msg);
        return Value::integer(0);
    }
    if (forwarded) return Value::integer(0);
    if (msg == "show_objects") {
        showObjects(*interp_, args);
        return Value::integer(0);
    }
    if (msg == "find") {
        scene::Object* f = args.empty() ? nullptr : venue_.find(text(*interp_, args[0]));
        return Value::obj(f ? objectFor(f) : nullptr);
    }
    if (msg == "add_sinks" && !args.empty()) {
        // {world add_sinks obj msg...}: the object and the messages it wants.
        const Value o = interp_->evalValue(args[0]);
        std::vector<std::string> msgs;
        for (size_t k = 1; k < args.size(); ++k) msgs.push_back(text(*interp_, args[k]));
        if (o.type == Value::Type::Object && o.object) addSink(o.object, msgs);
        return Value::integer(0);
    }
    if (msg == "remove_sink" && !args.empty()) {
        const Value o = interp_->evalValue(args[0]);
        if (o.type == Value::Type::Object && o.object) removeSink(o.object);
        return Value::integer(0);
    }
    // {dir exists name}: a venue object, less the band slots left empty
    // (game/band.h) [C 0x822B7670: empty slots are deleted].
    if (msg == "exists" && !args.empty()) {
        const std::string n = text(*interp_, args[0]);
        scene::Object* o = venue_.find(n);
        bool there = o != nullptr;
        if (band && o && o->as<scene::CharacterObj>()) there = band->member(n) != nullptr;
        return Value::integer(there ? 1 : 0);
    }
    if (msg == "add_sink" || msg == "remove_sink" || msg == "add_sinks" || msg == "set_name") return Value::integer(0);
    for (const NativeHandler& h : natives_) {
        Value r = Value::integer(0);
        if (h(*interp_, msg, args, r)) return r;
    }
    if (directorStubs().count(msg)) {
        interp_->onStub("world", msg, Value::integer(0));
        return Value::integer(0);
    }
    handled = false;
    return Value::unhandled();
}

void World::removeSink(ScriptObject* sink) {
    for (auto it = sinks_.begin(); it != sinks_.end();) it = it->second == sink ? sinks_.erase(it) : std::next(it);
}

void World::addSink(ScriptObject* sink, const std::vector<std::string>& messages) {
    for (const std::string& m : messages) {
        bool have = false;
        for (auto [it, end] = sinks_.equal_range(m); it != end; ++it) have = have || it->second == sink;
        if (!have) sinks_.emplace(m, sink);
    }
}

// ---- lighting presets (plain state) -----------------------------------------

// The preset's keyframe as plain state, weight 1 (LightPreset apply
// 0x82239BB8 [C]: a spotlight only with animate_from_preset (+0x254), an
// Environ only with its animate_from_preset (+0x74; ambient, fog colour
// and range, fog on / off 0x82238920), a Light's colour and range with
// animate_color_from_preset (+0xcc), its local transform with
// animate_position_from_preset (+0xcd; 0x822387E8)).
void World::applyPreset(const scene::LightPreset& p, size_t k) {
    if (k >= p.keyframes.size() || !p.dir) return;
    const auto& kf = p.keyframes[k];
    for (size_t i = 0; i < p.spotlights.size() && i < kf.spots.size(); ++i)
        if (auto* s = venue_.find<scene::Spotlight>(p.spotlights[i]); s && s->animateFromPreset) {
            const auto& e = kf.spots[i];
            spots_[s] = {e.intensity, e.color, e.rotation, e.target, e.flare};
        }
    for (size_t i = 0; i < p.environs.size() && i < kf.environs.size(); ++i)
        if (auto* env = venue_.find<scene::Environ>(p.environs[i]); env && env->animateFromPreset) {
            const auto& e = kf.environs[i];
            auto& st = pose_.envState(env);
            st.hasAmbient = true, st.ambient = e.ambient;
            st.hasFogEnable = true, st.fogEnable = e.fogEnable;
            if (e.fogEnable) {
                st.hasFogRange = true, st.fogStart = e.fogStart, st.fogEnd = e.fogEnd;
                st.hasFogColor = true, st.fogColor = e.fogColor;
            }
        }
    for (size_t i = 0; i < p.lights.size() && i < kf.lights.size(); ++i)
        if (auto* l = venue_.find<scene::Light>(p.lights[i])) {
            const auto& e = kf.lights[i];
            if (l->animateColorFromPreset) {
                auto& st = pose_.lightState(l);
                st.hasColor = true, st.color = e.color;
                st.hasRange = true, st.range = e.range;
            }
            if (l->animatePositionFromPreset) {
                scene::Xfm x = pose_.local(l);
                scene::quatToRot(e.rotation, x.rot);
                x.pos = e.position;
                pose_.setLocal(l, x);
            }
        }
}

const scene::CamShot* World::firstShot(const std::string& category) const {
    const scene::CamShot* any = nullptr;
    for (const auto& o : venue_.world().root().objects)
        if (auto* c = o->as<scene::CamShot>(); c && c->decoded && !c->keyframes.empty()) {
            if (c->category == category) return c;
            if (!any) any = c;
        }
    return any;
}

std::vector<const scene::CamShot*> World::shots() const {
    std::vector<const scene::CamShot*> v;
    for (const auto& o : venue_.world().root().objects)
        if (auto* c = o->as<scene::CamShot>(); c && c->decoded && !c->keyframes.empty()) v.push_back(c);
    return v;
}

const scene::LightPreset* World::firstPreset(const std::string& category) const {
    const scene::LightPreset* any = nullptr;
    for (const scene::LightPreset* p : venue_.all<scene::LightPreset>()) {
        if (!p->decoded || p->keyframes.empty()) continue;
        if (p->category == category) return p;
        if (!any) any = p;
    }
    return any;
}
