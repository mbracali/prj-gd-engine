// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/venue_script.h"

#include <algorithm>
#include <cmath>

#include "data/game_data.h"
#include "scene/anim.h"

namespace {

// config/gen/arena.dta
constexpr float kDefaultScale = 1;    // anim_default_scale
constexpr float kDefaultBlend = 240;  // anim_default_blend

const char* const kExcitementNames[] = {"boot", "bad", "okay", "great", "peak"};

bool isSym(const DataNode& n, const char* s) { return n.isText() && n.s == s; }

}  // namespace

// `arena`: the anim slots and tasks, set_singer_env, and the venue's handler
// blocks (every {arena add_handlers ...} block runs for its message).
class VenueScript::ArenaObj : public ScriptObject {
public:
    explicit ArenaObj(VenueScript& vs) : ScriptObject("arena"), vs_(vs) {}
    std::map<std::string, std::vector<const DataNode*>> blocks;  // message -> (name body...)

    bool run(const std::string& msg) {
        auto it = blocks.find(msg);
        if (it == blocks.end()) return false;
        for (const DataNode* b : it->second)
            for (size_t i = 1; i < b->children.size(); ++i) vs_.interp_.eval(b->children[i]);
        return true;
    }

    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if (msg == "switch_anim") return vs_.switchAnim(a, false), true;
        if (msg == "switch_anim_rt") return vs_.switchAnim(a, true), true;
        if (msg == "set_singer_env") return vs_.singerEnv_ = a.empty() ? "" : a[0].str(), true;
        if (msg == "delay_task" && !a.empty()) {  // ticks, then the deferred commands
            Delayed d{vs_.ticks_ + a[0].number(), {}};
            for (size_t i = 1; i < a.size(); ++i)
                if (a[i].type == Value::Type::Code) d.body.push_back(a[i].code);
            vs_.delayed_.push_back(std::move(d));
            return true;
        }
        if (msg == "cam_msg") return r = Value::string(vs_.cameraShot ? vs_.cameraShot() : ""), true;
        if (msg == "venue_name") return r = Value::symbol(vs_.venue_), true;
        if (msg == "load_section" || msg == "add_handlers") return true;  // collected at load
        run(msg);
        return true;
    }

private:
    VenueScript& vs_;
};

// A scene object as a script object.
class VenueScript::SceneProxy : public ScriptObject {
public:
    SceneProxy(VenueScript& vs, const std::string& name) : ScriptObject(name), vs_(vs) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        AnimPlayer* anims = vs_.anims_;
        SceneObject* o = anims ? anims->find(name()) : nullptr;
        if (!o) return true;
        auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
        if (msg == "set_showing") {
            if (Drawable* d = o->drawable()) d->showing = arg(0).truthy();
        } else if (msg == "showing") {
            const Drawable* d = o->drawable();
            r = Value::integer(d && d->showing);
        } else if (msg == "set_frame") {
            anims->setFrame(name(), arg(0).number());
        } else if (msg == "remove_anim") {
            if (o->view) std::erase(o->view->anim.anims, arg(0).str());
            else if (o->anim) std::erase(o->anim->anim.anims, arg(0).str());
        } else if (msg == "add_trans") {
            anims->attach(arg(0).str(), name());
        } else if (msg == "unhook_anim_parents") {
            anims->unhook(name());
        } else if (msg == "set_steps") {
            if (vs_.setFlareSteps) vs_.setFlareSteps(name(), arg(0).integerValue());
        }
        return true;
    }

private:
    VenueScript& vs_;
};

namespace {

// game anim_task <anim> <from> <to> <ms> (AnimateTaskPeriod).
struct GameObj : ScriptObject {
    std::function<void(const std::vector<Value>&)> animTask;
    GameObj() : ScriptObject("game") {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if (msg == "anim_task" && a.size() >= 4 && animTask) animTask(a);
        return true;
    }
};

// char_sys get_spot <character>: the nearest walk spot (FUN_0018ef80).
struct CharSysObj : ScriptObject {
    std::function<int(const std::string&)> getSpot;
    CharSysObj() : ScriptObject("char_sys") {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if (msg == "get_spot" && getSpot) r = Value::integer(getSpot(a.empty() ? "" : a[0].str()));
        return true;
    }
};

}  // namespace

VenueScript::VenueScript(const GameData& data, const std::string& venue)
    : venue_(venue), loader_(std::make_unique<ScriptLoader>(data)) {
    sections_.push_back({"venue", venue});  // arena/gen/venue.dta: {arena load_section venue {arena venue_name}}
    arena_ = std::make_unique<ArenaObj>(*this);
    auto game = std::make_unique<GameObj>();
    game->animTask = [this](const std::vector<Value>& a) {
        tasks_.push_back({a[0].str(), a[1].number(), a[2].number(), ms_, a[3].number()});
    };
    auto charSys = std::make_unique<CharSysObj>();
    charSys->getSpot = [this](const std::string& c) { return getSpot ? getSpot(c) : 0; };
    game_ = std::move(game);
    charSys_ = std::move(charSys);
    interp_.registerObject(arena_.get());
    interp_.registerObject(game_.get());
    interp_.registerObject(charSys_.get());
    interp_.resolver = [this](const std::string& name) -> ScriptObject* {
        if (!anims_ || !anims_->find(name)) return nullptr;
        auto& p = proxies_[name];
        if (!p) p = std::make_unique<SceneProxy>(*this, name);
        return p.get();
    };
    try {
        loader_->load("config/macros.dta");
    } catch (const std::exception&) {
    }
    script_ = loader_->load("venues/" + venue + "/" + venue + ".dta");
    // The handler blocks and the sections, collected before anything runs.
    for (const DataNode& n : script_.children) {
        if (n.type != DataNode::Type::Command || n.children.size() < 2) continue;
        const auto& c = n.children;
        if (isSym(c[0], "arena") && isSym(c[1], "add_handlers")) {
            for (size_t h = 2; h < c.size(); ++h)
                if (c[h].isList() && !c[h].children.empty() && c[h].children[0].isText())
                    arena_->blocks[c[h].children[0].s].push_back(&c[h]);
        } else if (isSym(c[0], "arena") && isSym(c[1], "load_section") && c.size() >= 4 && c[2].isText() &&
                   c[3].isText()) {
            sections_.push_back({c[2].s, c[3].s});
        }
    }
}

VenueScript::~VenueScript() = default;

int VenueScript::excitementFor(float rockMeter, float bootLevel) {
    if (rockMeter < bootLevel) return Boot;
    if (rockMeter < 0.33f) return Bad;
    if (rockMeter < 0.66f) return Okay;
    return Great;
}

void VenueScript::start(AnimPlayer& anims, int excitement) {
    anims_ = &anims;
    excitement_ = lastExcitement_ = excitement;
    interp_.run(script_);  // funcs, variables, the top-level commands
    handler("finish_loading");
    message("intro_start");
    message("music_start");
}

void VenueScript::message(const std::string& name) {
    // arena/gen/venue.dta
    if (name == "intro_start") {
        lastExcitement_ = excitement_;
        handler("set_lights_bad");
    } else if (name == "music_start") {
        handler("set_lights_okay_verse");
    } else if (name == "verse" || name == "chorus" || name == "solo") {
        songPos_ = name;
        lightsNeedUpdate_ = true;
    }
    handler(name);
}

void VenueScript::hitGem(int slot) {
    interp_.var("slot") = Value::integer(slot);
    handler("hit_gem");
}

void VenueScript::lightsPerExcitement(int e) {
    if (e <= Bad) handler("set_lights_bad");
    else handler(std::string("set_lights_") + (e == Okay ? "okay_" : "great_") + songPos_);
}

void VenueScript::update(float ms, float ticks, int excitement, const std::vector<std::string>& events,
                         bool downbeat) {
    if (!anims_) return;
    ms_ = ms, ticks_ = ticks;

    for (const std::string& e : events) message(e);

    for (size_t i = 0; i < delayed_.size();) {
        if (delayed_[i].tick <= ticks_) {
            Delayed d = std::move(delayed_[i]);
            delayed_.erase(delayed_.begin() + static_cast<long>(i));
            for (const DataNode* n : d.body) interp_.eval(*n);
        } else {
            ++i;
        }
    }

    if (excitement != excitement_) {  // the "excitement" handler of venue.dta
        excitement_ = excitement;
        handler(std::string("excitement_") + kExcitementNames[excitement]);
        if (excitement == Bad)
            handler(lastExcitement_ == Boot ? "excitement_bad_rising" : "excitement_bad_falling");
        else if (excitement == Okay && lastExcitement_ != Okay)
            handler(lastExcitement_ == Bad ? "excitement_okay_rising" : "excitement_okay_falling");
        lastExcitement_ = excitement;
        lightsNeedUpdate_ = true;
    }
    if (downbeat && lightsNeedUpdate_) {
        lightsNeedUpdate_ = false;
        lightsPerExcitement(multiplayer_ ? Great : excitement_);  // venue.dta: {game multiplayer} pins Great
    }

    // Section views (FUN_00178ce8): the _rt view on song ms, then the main
    // view on song ticks.
    for (const Section& s : sections_) {
        anims_->setFrame(s.name + "_rt.view", ms_);
        anims_->setFrame(s.name + ".view", ticks_);
    }

    // Anim slots (FUN_0017a518): the current range at full weight, then the
    // previous one fading out over the blend time.
    for (auto& [name, s] : slots_) {
        float t = clock(s.rt);
        float f = slotFrame(t, s.min, s.max, s.scale, s.offset, s.mode);
        anims_->setFrameUnfiltered(s.anim, f, 1.0f);
        if (s.blendEnd > s.start) {
            float w = (s.blendEnd - t) / (s.blendEnd - s.start);
            if (w > 0 && w <= 1) {
                float pf = slotFrame(t, s.prevMin, s.prevMax, s.prevScale, s.prevOffset, s.prevMode);
                anims_->setFrameUnfiltered(s.anim, pf, w);
            } else {
                s.blendEnd = s.start;
            }
        }
    }

    // AnimateTaskPeriod (FUN_001ee988).
    for (size_t i = 0; i < tasks_.size();) {
        Task& k = tasks_[i];
        float u = k.period > 0 ? std::clamp((ms_ - k.t0) / k.period, 0.0f, 1.0f) : 1.0f;
        anims_->setFrame(k.anim, k.from + (k.to - k.from) * u);
        if (u >= 1) tasks_.erase(tasks_.begin() + static_cast<long>(i));
        else ++i;
    }

    anims_->updateTransforms();
}

// FUN_0017a0b0: frame = t * scale + offset, clamped, wrapped or bounced.
float VenueScript::slotFrame(float t, float min, float max, float& scale, float& offset, int mode) {
    float f = t * scale + offset;
    if (mode == 0) return std::clamp(f, min, max);
    if (mode == 1) {
        float span = max - min, r = 0;
        if (span != 0) {
            r = std::fmod(f - min, span);
            if (r < 0) r += span;
        }
        return min + r;
    }
    float r;
    if (f > max) {
        r = max - (f - max);
        offset = 2 * max - offset;
    } else if (f < min) {
        r = min + (min - f);
        offset = 2 * min - offset;
    } else {
        return f;
    }
    scale = -scale;
    return r;
}

// FUN_0017a1d0, for {arena switch_anim <anim> (loop|shuttle|range a b) (scale s) (blend b)}.
void VenueScript::switchAnim(const std::vector<Value>& args, bool rt) {
    if (args.empty() || !args[0].isText()) return;
    const std::string name = args[0].str();
    if (!anims_ || !anims_->exists(name)) return;  // anim_switch_strict

    float scale = kDefaultScale, blend = kDefaultBlend;
    std::vector<float> range;
    int mode = 0;
    // Each option is a list; a macro (OFF, ...) may expand to a list of them.
    std::vector<const Value*> items;
    for (size_t i = 1; i < args.size(); ++i) {
        const Value& v = args[i];
        if (v.type != Value::Type::Array || !v.array) continue;
        if (!v.array->empty() && (*v.array)[0].type == Value::Type::Array) {
            for (const Value& e : *v.array) items.push_back(&e);
        } else {
            items.push_back(&v);
        }
    }
    for (const Value* a : items) {
        if (a->type != Value::Type::Array || !a->array || a->array->size() < 2 || !(*a->array)[0].isText()) continue;
        const auto& l = *a->array;
        const std::string key = l[0].str();
        if (key == "scale") scale = interp_.evalValue(l[1]).number();
        else if (key == "blend") blend = interp_.evalValue(l[1]).number();
        else if ((key == "loop" || key == "shuttle" || key == "range") && l.size() >= 3) {
            range = {interp_.evalValue(l[1]).number(), interp_.evalValue(l[2]).number()};
            mode = key == "loop" ? 1 : key == "shuttle" ? 2 : 0;
        }
    }

    Slot& s = slots_[name];
    bool fresh = s.anim.empty();
    s.rt = rt;
    float t = clock(rt);
    s.start = t;
    s.blendEnd = fresh ? t : t + blend;
    s.anim = name;
    s.prevScale = s.scale, s.prevOffset = s.offset;
    s.prevMin = s.min, s.prevMax = s.max;
    s.scale = scale;
    s.prevMode = s.mode;
    if (!range.empty()) {
        float a = range[0], b = range[1];
        s.mode = a == b ? 0 : mode;
        s.min = std::min(a, b), s.max = std::max(a, b);
        s.offset = -t * scale + anims_->filter(name, 0) + s.min;
    } else {  // keep the current frame, continue at the new scale
        float f = slotFrame(t, s.prevMin, s.prevMax, s.prevScale, s.prevOffset, s.mode);
        s.offset = f - t * s.scale;
    }
}

bool VenueScript::handler(const std::string& name) { return arena_->run(name); }
