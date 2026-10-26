// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/crowd.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "data/game_data.h"
#include "game/world.h"
#include "script/tasks.h"

namespace {

std::string textOf(Interp& in, const Value& v) {
    const Value e = in.evalValue(v);
    return e.type == Value::Type::Object && e.object ? e.object->name() : e.str();
}

// An archetype in iterate_frac's scripts: $this; its `main.drv`.
class ArchetypeObj : public ScriptObject {
public:
    explicit ArchetypeObj(const std::string& name) : ScriptObject(name, "Character") {}
};

// The archetype's CharDriver (main.drv): the clip group it is told to play
// and its beat_scale, kept for the band's clips (M9).
class DriverObj : public ScriptObject {
public:
    explicit DriverObj(Crowd::Archetype& a) : ScriptObject("main.drv", "CharDriver"), a_(a) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "play_group") {
            if (!args.empty()) a_.group = textOf(in, args[0]);
            return true;
        }
        if (msg == "set" && args.size() >= 2 && textOf(in, args[0]) == "beat_scale") {
            a_.beatScale = in.evalValue(args[1]).number();
            return true;
        }
        return false;
    }

private:
    Crowd::Archetype& a_;
};

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
// enabled, set_enabled, set_paused, set_volume, play_stream.
class CrowdAudioObj : public ScriptObject {
public:
    CrowdAudioObj(CrowdAudio& a, World& w) : ScriptObject("crowd_audio", "CrowdAudio"), a_(a), w_(w) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        auto num = [&](size_t k) { return k < args.size() ? in.evalValue(args[k]).number() : 0.0f; };
        if (msg == "turn_off") return a_.turnOff(), true;
        if (msg == "enabled") return r = Value::integer(a_.enabled() ? 1 : 0), true;
        if (msg == "set_enabled") return a_.setEnabled(num(0) != 0), true;
        if (msg == "set_paused") return a_.setPaused(num(0) != 0), true;
        if (msg == "set_volume") return a_.setVolume(std::pow(10.0f, num(0) / 20.0f)), true;
        if (msg == "play_stream") {
            // wingame (stone, after the game is beaten): not a venue level stream.
            in.onStub("crowd_audio", "play_stream " + (args.size() > 1 ? textOf(in, args[1]) : std::string()), r);
            return true;
        }
        if (msg == "excitement") return a_.excitement(int(num(0)), w_.seconds()), true;
        return false;
    }

private:
    CrowdAudio& a_;
    World& w_;
};

}  // namespace

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
            // char/crowd/og/<name>.milo; "og" folders resolve to "ng" (0x821c9480).
            const std::string base = a.name.find('/') == std::string::npos ? "char/crowd/ng/" + a.name + ".milo" : a.name;
            try {
                a.milo = &v.library().load(base);
            } catch (const std::exception& e) {
                SDL_Log("crowd %s: %s: %s", c->name.c_str(), base.c_str(), e.what());
            }
            s.archetypes.push_back(std::move(a));
        }
        for (Archetype& a : s.archetypes) {
            a.object = std::make_unique<ArchetypeObj>(a.name);
            a.driver = std::make_unique<DriverObj>(a);
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
    audioObj_ = std::make_unique<CrowdAudioObj>(*audio_, world);
    in.registerAs("crowd_audio", audioObj_.get());
}

Crowd::~Crowd() {
    Interp& in = world_.interp();
    in.tasks().killFor(audioObj_.get());
    in.unregisterName("crowd_audio", audioObj_.get());
    if (prevAudioObj_) in.registerAs("crowd_audio", prevAudioObj_);
    for (auto& o : owned_) in.unregisterName(o->name(), o.get());
}

Crowd::State* Crowd::find(const std::string& name) {
    for (State& s : crowds_)
        if (s.crowd->name == name) return &s;
    return nullptr;
}

void Crowd::enter() {
    for (State& s : crowds_)
        if (s.script) world_.interp().send(s.script, "enter", {}, false);
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

void Crowd::update() { audio_->update(world_.seconds()); }

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
    std::vector<Archetype*> chars;
    for (Archetype& a : s.archetypes)
        if (a.milo) chars.push_back(&a);
    if (chars.empty()) return;
    // Shuffled (Fisher-Yates from the end, the interpreter's random numbers).
    for (int i = int(chars.size()) - 1; i > 0; --i) std::swap(chars[size_t(i)], chars[size_t(in.randomInt(0, i + 1))]);
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
            Archetype& a = *chars[next++];
            in.var("this") = Value::obj(a.object.get());
            in.registerAs("main.drv", a.driver.get());
            in.evalValue(e.script);
        }
    }
    for (Archetype* a : chars) in.unregisterName("main.drv", a->driver.get());
    if (savedDrv) in.registerAs("main.drv", savedDrv);
    in.var("this") = savedThis;
    ++revision_;
}

// World messages the crowd audio follows (GH2's CrowdAudio is a game sink).
void Crowd::listen(Interp& in, const std::string& msg, const std::vector<Value>& args) {
    auto arg = [&](size_t k) { return k < args.size() ? in.evalValue(args[k]) : Value::integer(0); };
    const double now = world_.seconds();
    if (msg == "intro_start_msg") {
        const bool fast = arg(0).truthy(), encore = arg(1).truthy();
        audio_->introStart(fast, encore);
        if (!encore) in.onStub("world_sfx", "crowd_begin play", Value::integer(0));
        if (!in.findObject("game"))
            for (State& s : crowds_) setFullness(s, encore ? 1.0f : 0.5f, encore ? 1.0f : 0.5f);
    } else if (msg == "music_start") {
        audio_->musicStart(excitement_, now);
    } else if (msg == "excitement") {
        const int level = arg(0).integerValue();
        if (level != excitement_) in.onStub("world_sfx", CrowdAudio::reaction(excitement_, level) + " play", Value::integer(0));
        excitement_ = level;
        audio_->excitement(level, now);
        if (!in.findObject("game"))
            for (State& s : crowds_) setFullness(s, fullnessFor(level), fullnessFor(level));
    } else if (msg == "game_won_msg") {
        in.onStub("world_sfx", "crowd_win play", Value::integer(0));
    } else if (msg == "game_lost") {
        audio_->turnOff();
    } else if (msg == "encore_outro") {
        audio_->encoreOutro();
    }
}
