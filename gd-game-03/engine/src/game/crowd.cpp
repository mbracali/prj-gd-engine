// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/crowd.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "audio/synth.h"
#include "data/dta.h"
#include "data/game_data.h"
#include "game/venue.h"
#include "game/world.h"
#include "script/tasks.h"

namespace {

std::string textOf(Interp& in, const Value& v) {
    const Value e = in.evalValue(v);
    return e.type == Value::Type::Object && e.object ? e.object->name() : e.str();
}

uint32_t flagsArg(Interp& in, std::vector<Value>& args, size_t k, uint32_t def) {
    if (k >= args.size()) return def;
    const Value v = in.evalValue(args[k]);
    return v.isNumber() ? uint32_t(v.integerValue()) : def;
}

// (types (<type> ...)) for a class in an (objects ...) tree.
const DataNode* typeDef(const DataNode* objects, const std::string& cls, const std::string& type) {
    if (!objects || type.empty()) return nullptr;
    for (const DataNode& c : objects->children) {
        if (!c.isList() || c.children.empty() || !c.children[0].isText() || c.children[0].s != cls) continue;
        if (const DataNode* types = c.find("types"))
            if (const DataNode* t = types->find(type)) return t;
    }
    return nullptr;
}

// `rnd` for the world's own interpreter (crowd.dta's {rnd aspect}) when the
// host has none.
class RndObj : public ScriptObject {
public:
    RndObj() : ScriptObject("rnd", "Rnd") {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>&, Value& r) override {
        r = Value::integer(0);
        return msg == "aspect" || msg == "reset_postproc" || msg == "reflect";
    }
};

// `crowd_audio` (GH2's CrowdAudio script object, 0x822DFD98): turn_off,
// enabled, set_enabled, set_paused, set_volume, play_stream, excitement.
class CrowdAudioObj : public ScriptObject {
public:
    CrowdAudioObj(CrowdAudio& a, World& w, std::function<void()> sync, std::function<void(const std::string&)> react)
        : ScriptObject("crowd_audio", "CrowdAudio"), a_(a), w_(w), sync_(std::move(sync)), react_(std::move(react)) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        auto num = [&](size_t k) { return k < args.size() ? in.evalValue(args[k]).number() : 0.0f; };
        sync_();
        if (msg == "turn_off") return a_.turnOff(), true;
        if (msg == "enabled") return r = Value::integer(a_.enabled() ? 1 : 0), true;
        if (msg == "set_enabled") return react_(a_.setEnabled(num(0) != 0, w_.seconds())), true;
        if (msg == "set_paused") return a_.setPaused(num(0) != 0), true;
        if (msg == "set_volume") return a_.setVolumeDb(num(0)), true;
        if (msg == "play_stream") {
            // wingame (stone, after the game is beaten): not a venue level stream.
            in.onStub("crowd_audio", "play_stream " + (args.size() > 1 ? textOf(in, args[1]) : std::string()), r);
            return true;
        }
        if (msg == "excitement") return react_(a_.excitement(int(num(0)), w_.seconds())), true;
        return false;
    }

private:
    CrowdAudio& a_;
    World& w_;
    std::function<void()> sync_;
    std::function<void(const std::string&)> react_;
};

}  // namespace

// ---- the archetypes' script objects ------------------------------------------

// An archetype: $this in iterate_frac's scripts and $dude in its clips'
// events, with char_objects.dta's Character type crowd (enter, set_hand).
class Crowd::ArchetypeObj : public ScriptObject {
public:
    ArchetypeObj(Crowd& c, Actor& a, const std::string& scope) : ScriptObject(scope, "Character"), c_(c), a_(a) {
        nameScope = scope;
        random = &c.rng_;
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "get" || msg == "set" || msg == "has" || msg == "name") return false;
        chr::Character* ch = a_.character.get();
        if (msg == "find" || msg == "exists") {
            const std::string n = args.empty() ? std::string() : textOf(in, args[0]);
            ScriptObject* so = n.empty() ? nullptr : in.findObject(ScriptObject::scoped(nameScope, n));
            if (msg == "exists") return r = Value::integer(so ? 1 : 0), true;
            r = Value::obj(so);
            return true;
        }
        if (msg == "dir") return r = Value::obj(c_.world_.script()), true;
        if (msg == "get_type") return r = Value::symbol("crowd"), true;
        if (msg == "set_showing") return ch && (ch->visible = !args.empty() && in.evalValue(args[0]).truthy()), true;
        if (msg == "showing") return r = Value::integer(ch && ch->visible ? 1 : 0), true;
        if (msg == "teleport" || msg == "recenter" || msg == "set_min_lod") return true;
        // Its dta handler ran and left nothing to do (set_hand with the hand
        // it already shows).
        if (handler(msg)) return true;
        in.onStub(name(), msg, r);
        return true;
    }

private:
    Crowd& c_;
    Actor& a_;
};

// An object of an archetype's directory (hand_L-clap.mesh, lighter_flame,
// main.drv...), in the archetype's name scope.
class Crowd::PartObj : public ScriptObject {
public:
    PartObj(Crowd& c, Actor& a, const std::string& scope, scene::Object* o)
        : ScriptObject(ScriptObject::scoped(scope, o->name), o->cls), c_(c), a_(a), o_(o) {
        drv_ = a.character->driver(o->name);
        random = &c.rng_;
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        chr::Character& ch = *a_.character;
        if (msg == "name") return r = Value::symbol(o_->name), true;
        if (drv_) return driver(in, msg, args, r);
        if (msg == "get" || msg == "has") {
            if (!args.empty() && textOf(in, args[0]) == "showing") return r = Value::integer(ch.showing(o_) ? 1 : 0), true;
            return false;
        }
        if (msg == "set" && args.size() >= 2 && textOf(in, args[0]) == "showing") return ch.setShowing(o_, in.evalValue(args[1]).truthy()), true;
        if (msg == "set_showing") return ch.setShowing(o_, !args.empty() && in.evalValue(args[0]).truthy()), true;
        if (msg == "showing") return r = Value::integer(ch.showing(o_) ? 1 : 0), true;
        // lighter_flame, a RndDir of type world_fx (rnd_objects.dta) [C data]:
        // enter / exit {$this stop}; start {$this animate (range 0
        // kHugeFloat)} {$this set_showing TRUE}; stop {$this stop_animation}
        // {$this set_showing FALSE} (and frees its particles: it has none).
        // So the flame shows only while started, its filter running from
        // frame 0 at each start (render/crowd_view.h).
        // On this disc the lighter is a WorldFx object (class revision 1).
        if ((o_->as<scene::DirObj>() || o_->cls == "WorldFx") && (msg == "start" || msg == "stop")) {
            a_.lighter = msg == "start";
            if (a_.lighter) a_.lighterStart = c_.world_.seconds();
            return true;
        }
        in.onStub(name(), msg, r);
        return true;
    }

private:
    // CharDriver Handle 0x8216CAB0: what crowd.dta sends main.drv.
    bool driver(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
        chr::ClipDriver& d = *drv_;
        if (msg == "play_group" && !args.empty()) {
            a_.group = textOf(in, args[0]);
            d.playGroup(a_.group, flagsArg(in, args, 1, chr::kPlayLast));
            return true;
        }
        if (msg == "set" && args.size() >= 2) {
            const std::string k = textOf(in, args[0]);
            if (k == "beat_scale") d.beatScale = a_.beatScale = in.evalValue(args[1]).number();
            else if (k == "realign") d.realign = in.evalValue(args[1]).truthy();
            else in.onStub(name(), "set " + k, r);
            return true;
        }
        if (msg == "set_beat_scale") {
            if (!args.empty()) d.setBeatScale(a_.beatScale = in.evalValue(args[0]).number());
            return true;
        }
        if (msg == "get" && !args.empty() && textOf(in, args[0]) == "beat_scale") return r = Value::real(d.beatScale), true;
        if (msg == "enter") return true;
        in.onStub(name(), msg, r);
        return true;
    }

    Crowd& c_;
    Actor& a_;
    scene::Object* o_;
    chr::ClipDriver* drv_ = nullptr;
};

// ---- State --------------------------------------------------------------------

std::vector<std::pair<int, int>> Crowd::State::shown() const {
    std::vector<std::pair<int, int>> v;
    for (size_t a = 0; a < archetypes.size(); ++a) {
        const Archetype& ar = archetypes[a];
        if (!ar.milo) continue;
        if (!show3dOnly) {
            const int n = std::min(int(ar.cards.size()), int(float(ar.cards.size()) * full2d));
            for (int i = 0; i < n; ++i) v.push_back({int(a), ar.cards[size_t(i)]});
        }
        const int n3 = std::min(int(ar.threeD.size()), int(float(ar.threeD.size()) * full3d));
        for (int i = 0; i < n3; ++i) v.push_back({int(a), ar.threeD[size_t(i)]});
    }
    return v;
}

int Crowd::State::shownCount() const { return int(shown().size()); }

int Crowd::State::total() const {
    int n = 0;
    for (const Archetype& a : archetypes) n += int(a.placements.size());
    return n;
}

// ---- Crowd --------------------------------------------------------------------

Crowd::Crowd(World& world) : world_(world) {
    Venue& v = world.venue();
    clips_ = std::make_unique<chr::ClipLibrary>(v.data());
    if (!v.all<scene::WorldCrowd>().empty()) {
        try {
            ScriptLoader loader(v.data());
            objects_ = &world.interp().keep(loader.load("char/char_objects.dta"));
        } catch (const std::exception& e) {
            SDL_Log("crowd: char/char_objects.dta: %s", e.what());
        }
    }
    for (scene::WorldCrowd* c : v.all<scene::WorldCrowd>()) {
        if (!c->decoded) continue;
        State s;
        s.crowd = c;
        s.show3dOnly = c->show3dOnly;
        s.rotate = c->rotate;
        for (const auto& ch : c->characters) {
            Archetype a;
            a.name = ch.character;
            a.height = ch.height;
            a.placements = ch.placements;
            for (int i = 0; i < int(a.placements.size()); ++i) a.cards.push_back(i);
            if ((a.actor = actor(a.name))) a.milo = &a.actor->character->milo();
            s.archetypes.push_back(std::move(a));
        }
        s.script = world.scriptFor(c);
        crowds_.push_back(std::move(s));
    }

    // The venue's sound: the WorldDir type's (sound ...).
    const DataNode* sound = nullptr;
    if (ScriptObject* w = world.script(); w && w->typeDef()) sound = w->typeDef()->find("sound");
    audio_ = std::make_unique<CrowdAudio>(v.data(), v.name(), sound);

    Interp& in = world.interp();
    world.addObjectNatives([this](Interp& i, scene::Object& o, const std::string& msg, std::vector<Value>& args, Value& r) {
        return objectNative(i, o, msg, args, r);
    });
    world.addListener([this](Interp& i, const std::string& msg, const std::vector<Value>& args) { listen(i, msg, args); });
    if (!in.findObject("rnd")) {
        auto rnd = std::make_unique<RndObj>();
        in.registerObject(rnd.get());
        owned_.push_back(std::move(rnd));
    }
    prevAudioObj_ = in.findObject("crowd_audio");
    audioObj_ = std::make_unique<CrowdAudioObj>(
        *audio_, world, [this] { syncAudio(world_.interp()); }, [this](const std::string& r) { reactionSound(r); });
    in.registerAs("crowd_audio", audioObj_.get());
}

Crowd::~Crowd() {
    Interp& in = world_.interp();
    in.tasks().killFor(audioObj_.get());
    in.unregisterName("crowd_audio", audioObj_.get());
    if (prevAudioObj_) in.registerAs("crowd_audio", prevAudioObj_);
    for (auto& o : owned_) in.unregisterName(o->name(), o.get());
    for (auto& a : actors_) {
        for (auto& p : a->parts) in.tasks().killFor(p.get()), in.unregisterObject(p.get());
        if (a->object) in.tasks().killFor(a->object.get()), in.unregisterObject(a->object.get());
    }
}

// An archetype's character, loaded once per name: the <v>_chars proxy's
// file (../../../char/crowd/og/<name>.milo -> char/crowd/og/gen/<name>.
// milo_ps2), its environ, the script objects of it and its directory, its
// clips' events.
Crowd::Actor* Crowd::actor(const std::string& name) {
    for (auto& a : actors_)
        if (a->name == name) return a->character ? a.get() : nullptr;
    auto own = std::make_unique<Actor>();
    Actor& a = *own;
    a.name = name;
    actors_.push_back(std::move(own));
    Venue& v = world_.venue();
    const GameData& data = v.data();
    const scene::CharacterObj* proxy = v.find<scene::CharacterObj>(name);
    const std::string from = "world/" + v.name() + "/gen/" + v.name() + "_chars.milo_ps2";
    const std::string rel = proxy && !proxy->objectDir.proxyFile.empty() ? proxy->objectDir.proxyFile : "../../../char/crowd/og/" + name + ".milo";
    const std::string path = scene::findMiloRelative(data, from, rel);
    try {
        a.character = std::make_unique<chr::Character>(data, path, *clips_);
    } catch (const std::exception& e) {
        SDL_Log("crowd: %s: %s: %s", name.c_str(), path.c_str(), e.what());
        return nullptr;
    }
    chr::Character& c = *a.character;
    if (proxy && !proxy->rnd.environment.empty() && proxy->dir) a.env = proxy->dir->resolve<scene::Environ>(proxy->rnd.environment);
    Interp& in = world_.interp();
    const std::string scope = "crowd:" + name;
    auto obj = std::make_unique<ArchetypeObj>(*this, a, scope);
    if (const DataNode* def = typeDef(objects_, "Character", c.type())) obj->setTypeDef(def, 1);
    else SDL_Log("crowd: %s: no Character type '%s'", name.c_str(), c.type().c_str());
    a.object = std::move(obj);
    in.registerObject(a.object.get());
    std::function<void(const scene::Directory&)> parts = [&](const scene::Directory& d) {
        for (const auto& o : d.objects) {
            auto p = std::make_unique<PartObj>(*this, a, scope, o.get());
            in.registerObject(p.get());
            a.parts.push_back(std::move(p));
        }
        for (const auto& [_, sub] : d.subdirs)
            if (sub) parts(*sub);
    };
    parts(c.dir());
    // Clip events ($dude = the archetype, 0x8219BFF0).
    c.runScript = [this, &a](const std::string& script) { runClipScript(a, script); };
    if (chr::ClipDriver* d = c.mainDriver()) {
        d->rng = &rng_;
        d->onPlay = [this, &a](const scene::CharClipSamples& clip, uint32_t) {
            ++a.clipsStarted;
            if (onClip) onClip(beats_, a.name, clip.name);
        };
    }
    return &a;
}

void Crowd::runClipScript(Actor& a, const std::string& script) {
    if (script.empty() || !a.object) return;
    auto it = scripts_.find(script);
    if (it == scripts_.end()) {
        DataNode tree;
        try {
            tree = parseDtaText(script);
        } catch (const std::exception& e) {
            SDL_Log("crowd: clip script %s: %s", script.c_str(), e.what());
        }
        it = scripts_.emplace(script, std::move(tree)).first;
    }
    Interp& in = world_.interp();
    const Value savedDude = in.var("dude"), savedThis = in.var("this");
    in.var("dude") = Value::obj(a.object.get());
    in.var("this") = Value::obj(a.object.get());
    const std::string hand = a.object->getProp(in, "hand").str();
    for (const DataNode& n : it->second.children) in.eval(n);
    const std::string now = a.object->getProp(in, "hand").str();
    if (now != hand && onHand) onHand(beats_, a.name, now);
    a.hand = now;
    in.var("dude") = savedDude;
    in.var("this") = savedThis;
}

Crowd::State* Crowd::find(const std::string& name) {
    for (State& s : crowds_)
        if (s.crowd->name == name) return &s;
    return nullptr;
}

void Crowd::enter() {
    Interp& in = world_.interp();
    // CrowdAudio ctor 0x822E0928 -> set_enabled with the mode's crowd_audio
    // (modes.dta: FALSE in practice and tutorial) [C].
    if (ScriptObject* cfg = in.findObject("gamecfg")) {
        std::vector<Value> key{Value::symbol("crowd_audio")};
        Value on = Value::integer(1);
        if (cfg->handleNative(in, "get", key, on)) audio_->setEnabled(in.evalValue(on).truthy(), world_.seconds());
    }
    for (State& s : crowds_)
        if (s.script) in.send(s.script, "enter", {}, false);
    // The archetypes enter with the directory (Character Enter: the dta
    // `enter`, clap hands on and the others off).
    for (auto& a : actors_)
        if (a->object) {
            in.send(a->object.get(), "enter", {}, false);
            a->hand = a->object->getProp(in, "hand").str();
        }
    entered_ = true;
}

// WorldCrowd::SetFullness (0x82247C08): only the counts are kept; shown()
// takes the first int(n x fraction) of each list.
void Crowd::setFullness(State& s, float a, float b) {
    if (s.full2d != a || s.full3d != b) ++revision_;
    s.full2d = std::clamp(a, 0.0f, 1.0f);
    s.full3d = std::clamp(b, 0.0f, 1.0f);
}

// crowd.dta crowd_update's fullness for a level, for the hosts without the
// game object (the script's {exists game} branch) [port: the dev modes].
static float fullnessFor(int excitement) {
    switch (excitement) {
    case 0: return 0.1f;
    case 1: return 0.25f;
    case 2: return 0.5f;
    default: return 1.0f;
    }
}

void Crowd::startShot(const scene::CamShot* shot, bool runScript) {
    if (!shot || shot->crowd.empty()) return;
    State* s = find(shot->crowd);
    if (!s) return;
    // StartAnim 0x82226120 sends start_shot first (camshot.dta: crowd_update,
    // rotate), then sets the crowd's 3D list.
    if (runScript && s->script) {
        Interp& in = world_.interp();
        in.send(s->script, "crowd_update", {}, false);
        in.send(s->script, "set", {Value::symbol("rotate"), Value::integer(shot->flag("crowd_face_camera") == "1" ? 1 : 0)}, false);
    }
    if (!world_.interp().findObject("game")) setFullness(*s, fullnessFor(excitement_), fullnessFor(excitement_));
    // 0x82247FD0: the old 3D placements go back to the cards (in their
    // place), then each pair (archetype, card index) moves to the 3D list
    // (out-of-range pairs are reported and skipped), the 3D list sorted;
    // the fullness is applied again.
    for (Archetype& a : s->archetypes) {
        a.cards.clear();
        for (int i = 0; i < int(a.placements.size()); ++i) a.cards.push_back(i);
        a.threeD.clear();
    }
    std::vector<std::pair<int, int>> moved;
    for (const auto& [ai, ci] : shot->crowd3d) {
        if (ai >= s->archetypes.size() || ci >= s->archetypes[ai].cards.size()) {
            SDL_Log("crowd: %s: crowd3d (%u %u) out of range", shot->name.c_str(), ai, ci);
            continue;
        }
        moved.push_back({int(ai), s->archetypes[ai].cards[ci]});
    }
    for (const auto& [ai, p] : moved) {
        Archetype& a = s->archetypes[size_t(ai)];
        a.cards.erase(std::remove(a.cards.begin(), a.cards.end(), p), a.cards.end());
        if (std::find(a.threeD.begin(), a.threeD.end(), p) == a.threeD.end()) a.threeD.push_back(p);
    }
    for (Archetype& a : s->archetypes) std::sort(a.threeD.begin(), a.threeD.end());
    ++revision_;
}

void Crowd::update() {
    audio_->update(world_.seconds());
    const double seconds = world_.seconds(), beats = world_.beats();
    const float dS = first_ ? 0.0f : float(std::max(0.0, seconds - seconds_));
    const float dB = first_ ? 0.0f : float(std::max(0.0, beats - beats_));
    first_ = false;
    seconds_ = seconds, beats_ = beats;
    if (!entered_) return;
    // The archetypes poll with the directory (Character Poll 0x82156B78):
    // main.drv on the song's beat (x beat_scale, or seconds with
    // kPlayRealTime), the servo poses the bones.
    for (auto& a : actors_) {
        if (!a->character) continue;
        chr::RigContext ctx;
        ctx.beat = beats, ctx.dBeats = dB, ctx.dSeconds = dS, ctx.seconds = seconds;
        a->character->poll(ctx);
    }
}

// The WorldCrowd's natives, and a CamShot's [crowd].
bool Crowd::objectNative(Interp& in, scene::Object& o, const std::string& msg, std::vector<Value>& args, Value& r) {
    if (auto* shot = o.as<scene::CamShot>()) {
        if (msg == "get" && !args.empty() && textOf(in, args[0]) == "crowd") {
            State* s = shot->crowd.empty() ? nullptr : find(shot->crowd);
            r = s && s->script ? Value::obj(s->script) : Value::symbol("");
            return true;
        }
        return false;
    }
    if (!o.as<scene::WorldCrowd>()) return false;
    State* s = nullptr;
    for (State& c : crowds_)
        if (c.crowd == &o) s = &c;
    if (!s) return false;
    auto num = [&](size_t k) { return k < args.size() ? in.evalValue(args[k]).number() : 0.0f; };
    if (msg == "set_fullness") {
        setFullness(*s, num(0), args.size() > 1 ? num(1) : num(0));
        r = Value::integer(0);
        return true;
    }
    if (msg == "iterate_frac") {
        iterateFrac(in, *s, args);
        r = Value::integer(0);
        return true;
    }
    if ((msg == "set" || msg == "get") && !args.empty()) {
        const std::string key = textOf(in, args[0]);
        bool* flag = key == "show_3d_only" ? &s->show3dOnly : key == "rotate" ? &s->rotate : nullptr;
        if (!flag) return false;
        if (msg == "set") {
            const bool v = args.size() > 1 && in.evalValue(args[1]).truthy();
            if (*flag != v) ++revision_;
            *flag = v;
        }
        r = Value::integer(*flag ? 1 : 0);
        return true;
    }
    return false;
}

// iterate_frac (0x822452E0).
void Crowd::iterateFrac(Interp& in, State& s, std::vector<Value>& args) {
    std::vector<Actor*> chars;
    for (Archetype& a : s.archetypes)
        if (a.actor) chars.push_back(a.actor);
    if (chars.empty()) return;
    // Shuffled (Fisher-Yates from the end, the crowd's random numbers).
    for (int i = int(chars.size()) - 1; i > 0; --i) std::swap(chars[size_t(i)], chars[size_t(rng_.integer(0, i + 1))]);
    struct Entry {
        float frac;
        Value script;
    };
    std::vector<Entry> entries;
    float sum = 0;
    for (const Value& raw : args) {
        const Value v = in.evalValue(raw);
        if (v.type != Value::Type::Array || !v.array || v.array->size() < 2) continue;
        const float f = in.evalValue((*v.array)[0]).number();
        entries.push_back({f, (*v.array)[1]});
        if (f > 0) sum += f;
    }
    if (sum <= 0) return;
    const float scale = float(chars.size()) / sum;
    float acc = -0.5f;
    size_t next = 0;
    Value savedThis = in.var("this");
    ScriptObject* savedDrv = in.findObject("main.drv");
    for (const Entry& e : entries) {
        acc += e.frac * scale;
        while (next < chars.size() && float(next) < acc) {
            Actor& a = *chars[next++];
            in.var("this") = Value::obj(a.object.get());
            // `main.drv` is the archetype's (its name scope while it is $this).
            ScriptObject* drv = in.findObject(ScriptObject::scoped(a.object->nameScope, "main.drv"));
            if (drv) in.registerAs("main.drv", drv);
            in.evalValue(e.script);
            if (drv) in.unregisterName("main.drv", drv);
        }
    }
    if (savedDrv) in.registerAs("main.drv", savedDrv);
    in.var("this") = savedThis;
    ++revision_;
}

// A reaction sound (0x822DF350) [C]: the one playing stops, the new one
// plays; the names the venue's bank lacks (crowd_upto_peak...) are silent
// (a null ObjPtr in GH2).
void Crowd::reactionSound(const std::string& name) {
    if (name.empty()) return;
    const scene::Object* o = world_.venue().find(name);
    if (!o) return;
    if (!reaction_.empty()) world_.stopSound(reaction_);
    // At the options' sound effects volume, 0 dB in the attract mode
    // (0x822DF140, 0x822B0F08) [C].
    world_.playSound(name, nullptr, attract_ || !world_.synth() ? 0.0f : world_.synth()->fxVolumeDb());  // crowd_reactions: TRUE (config/sound.dta) [C data]
    reaction_ = name;
}

// The options' sound effects volume for the crowd streams and reactions:
// 0 dB in the attract mode (0x822DF140, 0x822B0F08) [C].
void Crowd::syncAudio(Interp& in) {
    if (ScriptObject* cfg = in.findObject("gamecfg")) {
        std::vector<Value> key{Value::symbol("mode")};
        Value mode;
        attract_ = cfg->handleNative(in, "get", key, mode) && mode.isText() && mode.s == "attract";
    }
    audio_->setFxVolumeDb(attract_ || !world_.synth() ? 0.0f : world_.synth()->fxVolumeDb());
}

// World messages the crowd audio follows (GH2's CrowdAudio is a game sink:
// intro_start_msg, music_start, game_won_msg, game_lost, excitement;
// 0x822DF9D8).
void Crowd::listen(Interp& in, const std::string& msg, const std::vector<Value>& args) {
    auto arg = [&](size_t k) { return k < args.size() ? in.evalValue(args[k]) : Value::integer(0); };
    const double now = world_.seconds();
    syncAudio(in);
    if (msg == "intro_start_msg") {
        // 0x822DFCA8 [C]: crowd_begin (+0x64) whatever $encore, then the stream.
        const bool fast = arg(0).truthy(), encore = arg(1).truthy();
        reactionSound("crowd_begin");
        audio_->introStart(fast, encore);
        if (!in.findObject("game"))
            for (State& s : crowds_) setFullness(s, encore ? 1.0f : 0.5f, encore ? 1.0f : 0.5f);
    } else if (msg == "music_start") {
        reactionSound(audio_->musicStart(excitement_, now));
    } else if (msg == "excitement") {
        const int level = arg(0).integerValue();
        excitement_ = level;
        reactionSound(audio_->excitement(level, now));
        if (!in.findObject("game"))
            for (State& s : crowds_) setFullness(s, fullnessFor(level), fullnessFor(level));
    } else if (msg == "game_won_msg") {
        reactionSound("crowd_win");
    } else if (msg == "game_lost") {
        // Only the reaction (+0x6c) [C 0x822DFD98]: the stream plays on until
        // lose.dta's {crowd_audio turn_off}.
        reactionSound("crowd_lose");
    } else if (msg == "encore_outro") {
        audio_->encoreOutro();
    }
}
