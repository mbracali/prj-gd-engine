// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/band.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>

#include "char/facefx.h"
#include "char/midi_parser.h"
#include "data/dta.h"
#include "data/game_data.h"
#include "data/midi.h"
#include "game/chart.h"
#include "game/song_db.h"
#include "game/venue.h"
#include "game/world.h"
#include "render/scene_gpu.h"
#include "script/script.h"
#include "script/tasks.h"

namespace {

using chr::ClipSel;

constexpr uint32_t kTempoMask = 3;          // kTempoMedium | kTempoFast
constexpr uint32_t kSpecial = 0x20000;      // one-shots (star_power, sync_*)
constexpr uint32_t kStarPower = 0x80000;    // the SP clips
constexpr uint32_t kStarPowerFar = 0x4000;  // kStarPowerFar
constexpr uint32_t kInteract = 0x10000;

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

std::string text(Interp& in, const Value& v) {
    const Value e = in.evalValue(v);
    return e.type == Value::Type::Object && e.object ? e.object->name() : e.str();
}

// A play / play_clip argument as a clip selection (0x82165460 / 0x8219C8B0).
ClipSel selOf(Interp& in, const Value& raw) {
    const Value v = in.evalValue(raw);
    ClipSel s;
    if (v.isNumber()) {
        s.kind = ClipSel::Flags, s.flags = uint32_t(v.integerValue());
    } else if (v.type == Value::Type::Array && v.array) {
        s.kind = ClipSel::List;
        for (const Value& e : *v.array) s.list.push_back(selOf(in, e));
    } else {
        s.kind = ClipSel::Name, s.name = text(in, v);
    }
    return s;
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

// songs.dtb: a song's (band ...) and (quickplay (character_outfit X) (guitar G)).
struct SongBand {
    std::vector<std::string> band;
    std::string character, guitar;
};
SongBand songBand(const GameData& data, const std::string& id) {
    SongBand r;
    try {
        DataNode root = parseDtb(data.read("config/gen/songs.dtb"));
        resolveConditionals(root, {"HX_XBOX", "_SHIP"});
        for (const DataNode& e : root.children) {
            if (!e.isList() || e.children.empty() || !e.children[0].isText() || e.children[0].s != id) continue;
            if (const DataNode* b = e.find("band"))
                for (size_t k = 1; k < b->children.size(); ++k)
                    if (b->children[k].isText()) r.band.push_back(b->children[k].s);
            if (const DataNode* q = e.find("quickplay")) {
                if (const DataNode* c = q->find("character_outfit")) r.character = c->textAt(1);
                if (const DataNode* g = q->find("guitar")) r.guitar = g->textAt(1);
            }
        }
    } catch (const std::exception& ex) {
        SDL_Log("band: songs.dtb: %s", ex.what());
    }
    return r;
}

// gh2.dtb (default_band ...).
std::vector<std::string> defaultBand(const GameData& data) {
    std::vector<std::string> v;
    try {
        DataNode root = parseDtb(data.read("config/gen/gh2.dtb"));
        if (const DataNode* b = root.find("default_band"))
            for (size_t k = 1; k < b->children.size(); ++k)
                if (b->children[k].isText()) v.push_back(b->children[k].s);
    } catch (const std::exception& ex) {
        SDL_Log("band: gh2.dtb: %s", ex.what());
    }
    if (v.empty()) v = {"metal_singer", "metal_bass", "metal_drummer"};
    return v;
}

// guitars.dtb: (guitar (skins (skin (outfit O) (mat M)) ...)) -> outfit, mat
// (change_guitar 0x822B6250 / 0x822B21D0; the mat on guitar.mesh 0x822B2148).
std::pair<std::string, std::string> guitarOutfit(const GameData& data, const std::string& guitar, const std::string& skin) {
    try {
        DataNode root = parseDtb(data.read("config/gen/guitars.dtb"));
        for (const DataNode& g : root.children) {
            if (!g.isList() || g.children.empty() || !g.children[0].isText() || g.children[0].s != guitar) continue;
            const DataNode* skins = g.find("skins");
            if (!skins) break;
            const DataNode* chosen = nullptr;
            for (size_t k = 1; k < skins->children.size(); ++k) {
                const DataNode& s = skins->children[k];
                if (!s.isList() || s.children.empty()) continue;
                if (!chosen) chosen = &s;  // [I] the first skin when none is chosen (0x822C6270)
                if (!skin.empty() && s.children[0].isText() && s.children[0].s == skin) chosen = &s;
            }
            if (!chosen) break;
            std::string outfit, mat;
            if (const DataNode* o = chosen->find("outfit")) outfit = o->textAt(1);
            if (const DataNode* m = chosen->find("mat")) mat = m->textAt(1);
            return {outfit, mat};
        }
    } catch (const std::exception& ex) {
        SDL_Log("band: guitars.dtb: %s", ex.what());
    }
    // A guitar guitars.dtb does not list, with an outfit of its own name:
    // the Air Guitar cheat's air_guitar [I: 0x822B6250 takes $cheat_guitar
    // as the guitar].
    if (!guitar.empty() && data.exists("char/ng/guitars/gen/" + guitar + ".milo_xbox")) return {guitar, ""};
    return {"guitar_sg", ""};
}

}  // namespace

// ---- GuitarCharacter state (object +0x204..+0x2b8) ---------------------------

struct Band::GuitarState {
    std::string mode;            // +0x208 the mode group (idle normal extreme solo / win lose win_finals)
    std::string bad = "bad";     // +0x264
    bool gameOver = false;       // +0x268
    bool win = false;            // +0x269
    int post = 0;                // +0x21c 0, 1 (post-game idle queued), 2 (playing), 3
    std::string postGroup;       // +0x2b8 win_idle / lose_idle
    const scene::CharClipSamples* postClip = nullptr;  // +0x2b4
    double startAt = 1e30;       // +0x26c when the game-over clip starts (TaskMgr seconds)
    bool handsOn = true;         // +0xb8 && +0xb9
    int activePlayers = 0;       // +0x220
    std::vector<float> interactProbs;  // +0xa4 interact_probs
    // Walks (0x822D16B8).
    bool canWalk = false;        // +0x250: a kWalkSpot, one player, a CharWalk
    float tempoScale = 1;        // +0x22c: 2 when the player's track has [half_tempo]
    float walkTime = 0;          // +0x230: beats a walk between walk spots may take
    std::array<float, 5> delays{};  // +0x238: walk_delays rolled per excitement (s; 1e30 never)
    double lastWalk = -15;       // +0x24c: TaskMgr seconds of the last roll
    std::vector<std::unique_ptr<Task>> tasks;  // +0x254 (pollable +0xec), by start
    std::vector<std::string> usedInteracts;    // +0x210: clip groups interacted with
    int walkState = 0;           // the CharWalk's state last poll (arrival logs)
};

// The guitarist's timeline entries (TaskQueue 0x82197CB8 / 0x82198188 /
// 0x82197FB0): {vtable, start, end (beats), priority, mask, started}.
// Walk: 0x822E4898 (vtable 0x8204A31C: Start 0x822E4E78, Poll 0x822E48D8,
// End 0x822E4AE8, Conflict 0x822E4910); hold: 0x82046E18 (no-ops); Star
// Power: 0x822E4AF8 (vtable 0x8204A334: Start 0x822E49D8, Poll 0x822E4A30);
// interact: 0x822E4C80 (vtable 0x8204A384: Start 0x822E50D0).
struct Band::Task {
    enum Kind { Walk, Hold, StarPower, Interact } kind = Hold;
    float start = 0, end = 0, priority = -1;  // [1] [2] [3]
    uint32_t mask = 0;                        // [4]
    bool started = false;                     // [5]
    uint32_t flags = 0;                       // walk [8]: the target's waypoint flags
    bool fromSpot = false;                    // walk [9]
    scene::CharClipSamples* clip = nullptr;   // Star Power / interact [8]
    bool seen = false;                        // Star Power [9]
    std::string group;                        // interact [7]: the waypoint's clip_group
};

// ---- script objects --------------------------------------------------------

// A band member: its script type (char_objects.dta) and Character /
// GuitarCharacter natives.
class Band::CharObj : public ScriptObject {
public:
    CharObj(Band& b, Member& m, const std::string& cls) : ScriptObject(m.slot, cls), b_(b), m_(m) {
        nameScope = m.slot;
        random = &b.rng_;
    }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        chr::Character& c = *m_.character;
        if (m_.type == "guitarist" && b_.guitarMessage(m_, msg, args, r)) return true;
        if (msg == "get" || msg == "set" || msg == "has" || msg == "name") return false;
        // Character::Handle 0x82152108.
        if (msg == "teleport") {
            // 0x82151D08: to a waypoint, which the servo then regulates to.
            if (!args.empty()) {
                const Value w = in.evalValue(args[0]);
                const std::string n = text(in, w);
                if (scene::Object* o = b_.world_.venue().find(n)) {
                    if (auto* wp = o->as<scene::Waypoint>()) c.teleport(b_.spotOf(wp));
                    else c.teleport(b_.world_.pose().world(o));
                }
            }
            return true;
        }
        if (msg == "recenter") return true;  // FUN_82188DB0 (servo recenter; the clips keep the root) [I]
        if (msg == "play_clip") {
            if (chr::ClipDriver* d = c.mainDriver(); d && !args.empty()) d->play(selOf(in, args[0]), flagsArg(in, args, 1, chr::kPlayLast));
            return true;
        }
        if (msg == "set_min_lod") {
            c.minLod = args.empty() ? 0 : in.evalValue(args[0]).integerValue();
            return true;
        }
        // RndDir / ObjectDir.
        if (msg == "dir") return r = Value::obj(b_.world_.script()), true;
        if (msg == "find" || msg == "exists") {
            const std::string n = args.empty() ? std::string() : text(in, args[0]);
            ScriptObject* so = n.empty() ? nullptr : in.findObject(ScriptObject::scoped(m_.slot, n));
            if (!so && !n.empty())
                if (scene::Object* f = c.dir().find(n)) so = in.findObject(ScriptObject::scoped(m_.slot, f->name));
            if (msg == "exists") return r = Value::integer(so ? 1 : 0), true;
            r = Value::obj(so);
            return true;
        }
        if (msg == "get_type") return r = Value::symbol(m_.type), true;
        if (msg == "set_showing") return c.visible = !args.empty() && in.evalValue(args[0]).truthy(), true;
        if (msg == "showing") return r = Value::integer(c.visible ? 1 : 0), true;
        if (msg == "iterate" && args.size() >= 3) {
            // {$this iterate Mesh $mesh {body}}: every object of the class in the dir.
            const std::string cls = text(in, args[0]);
            const std::string var = args[1].s;
            std::vector<std::string> names;
            for (const auto& o : c.dir().objects)
                if (o->cls == cls) names.push_back(o->name);
            for (const std::string& n : names) {
                in.var(var.rfind('$', 0) == 0 ? var.substr(1) : var) = Value::obj(in.findObject(ScriptObject::scoped(m_.slot, n)));
                for (size_t k = 2; k < args.size(); ++k) in.evalValue(args[k]);
            }
            return true;
        }
        // Anything else: the character directory's sinks, its EventTriggers
        // (the drum kit's kick / snare / hihat / crash.trig) [I: the routing].
        b_.fireTriggers(m_, msg);
        return true;
    }

    Value getProp(Interp& in, const std::string& key) override {
        if (key == "player_num") return Value::integer(std::max(0, m_.player));
        return ScriptObject::getProp(in, key);
    }

private:
    Band& b_;
    Member& m_;
};

// An object of a member's directory by name (main.drv, left_hand.drv,
// fret.ik, meshes, lip.servo...), registered in the member's name scope.
class Band::PartObj : public ScriptObject {
public:
    PartObj(Band& b, Member& m, scene::Object* o) : ScriptObject(ScriptObject::scoped(m.slot, o->name), o->cls), b_(b), m_(m), o_(o) {
        drv_ = m.character->driver(o->name);
        random = &b.rng_;
    }
    const std::string& objName() const { return o_->name; }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        chr::Character& c = *m_.character;
        if (msg == "name") return r = Value::symbol(o_->name), true;
        if (msg == "get" || msg == "has") {
            if (!args.empty() && text(in, args[0]) == "showing") return r = Value::integer(c.showing(o_) ? 1 : 0), true;
            return false;
        }
        if (drv_) return driver(in, msg, args, r);
        if (auto* ik = o_->as<scene::CharIKMidi>()) {
            if ((msg == "midi_parser" || msg == "new_spot") && args.size() >= 2) {
                const std::string spot = text(in, args[0]);
                const double s = args.size() > 2 ? in.evalValue(args[1]).number() : b_.beat_;
                const double e = args.size() > 2 ? in.evalValue(args[2]).number() : in.evalValue(args[1]).number();
                c.rig().ikMidi(ik->name, spot, s, e, b_.beat_);
                return true;
            }
            return true;
        }
        if (msg == "set" && args.size() >= 2 && text(in, args[0]) == "showing") {
            c.setShowing(o_, in.evalValue(args[1]).truthy());
            return true;
        }
        if (msg == "set_showing") {
            c.setShowing(o_, !args.empty() && in.evalValue(args[0]).truthy());
            return true;
        }
        if (msg == "showing") return r = Value::integer(c.showing(o_) ? 1 : 0), true;
        if (msg == "find" || msg == "exists") {
            const std::string n = args.empty() ? std::string() : text(in, args[0]);
            ScriptObject* so = n.empty() ? nullptr : in.findObject(ScriptObject::scoped(m_.slot, n));
            if (!so && !n.empty()) {
                const scene::Directory* from = nullptr;
                for (const auto& [owner, d] : c.dir().subdirs)
                    if (owner == o_->name) from = d.get();
                if (scene::Object* f = (from ? from : &c.dir())->find(n))
                    so = in.findObject(ScriptObject::scoped(m_.slot, f->name));
            }
            if (msg == "exists") return r = Value::integer(so ? 1 : 0), true;
            r = Value::obj(so);
            return true;
        }
        if (msg == "iterate" && args.size() >= 3) {
            const std::string cls = text(in, args[0]);
            const std::string var = args[1].s;
            const scene::Directory* from = nullptr;
            for (const auto& [owner, d] : c.dir().subdirs)
                if (owner == o_->name) from = d.get();
            std::vector<std::string> names;
            std::function<void(const scene::Directory&)> collect = [&](const scene::Directory& d) {
                for (const auto& o : d.objects)
                    if (o->cls == cls) names.push_back(o->name);
                for (const auto& [_, sub] : d.subdirs)
                    if (sub) collect(*sub);
            };
            if (from) collect(*from);
            for (const std::string& n : names) {
                in.var(var.rfind('$', 0) == 0 ? var.substr(1) : var) = Value::obj(in.findObject(ScriptObject::scoped(m_.slot, n)));
                for (size_t k = 2; k < args.size(); ++k) in.evalValue(args[k]);
            }
            return true;
        }
        if (msg == "set_trans_parent") {
            const std::string p = args.empty() ? std::string() : text(in, args[0]);
            if (auto* mesh = o_->as<scene::Mesh>()) mesh->trans.parent = p;
            else if (auto* dir = o_->as<scene::DirObj>()) dir->rnd.trans.parent = p;
            return true;
        }
        if (o_->as<scene::FaceFxLipSyncServo>() && face(in, msg, args, r)) return true;
        if (o_->as<scene::CharServoBone>() && msg == "regulate_override") {
            // 0x82189A40 -> 0x82189928: clip frame waypoint bone ('' 0 '' '': off).
            const std::string clipName = args.size() > 0 ? text(in, args[0]) : std::string();
            const float frame = args.size() > 1 ? in.evalValue(args[1]).number() : 0.0f;
            const std::string way = args.size() > 2 ? text(in, args[2]) : std::string();
            const std::string bone = args.size() > 3 ? text(in, args[3]) : std::string();
            chr::ClipDriver* d = c.mainDriver();
            const scene::CharClipSamples* clip = d && d->clipSet() && !clipName.empty() ? d->clipSet()->clip(clipName) : nullptr;
            const scene::Waypoint* wp = way.empty() ? nullptr : b_.world_.venue().find<scene::Waypoint>(way);
            if (wp) {
                const chr::Spot sp = b_.spotOf(wp);
                c.setRegulateOverride(clip, frame, &sp, bone);
            } else {
                c.setRegulateOverride(clip, frame, nullptr, bone);
            }
            return true;
        }
        // The rest of the rig's and effects' messages come in later phases
        // (M9.6 walks: bone.servo regulate_override; the proxies' particles:
        // start / stop): reported as STUBs.
        in.onStub(m_.slot + "::" + o_->name, msg, r);
        return true;
    }

private:
    bool face(Interp& in, const std::string& msg, std::vector<Value>& args, Value&) {
        if (!m_.lip || !m_.expr) return true;
        facefx::LipSync& lip = *m_.lip;
        facefx::ExpressionScript& expr = *m_.expr;
        const float now = float(in.tasks().time(kTaskSeconds));
        auto excitement = [&]() -> std::pair<bool, int> { return {in.findObject("game") != nullptr, b_.currentExcitement()}; };
        if (msg == "pick_expression") return expr.pickExpression(now), true;
        if (msg == "do_pick_expression") {
            const auto [ge, e] = excitement();
            expr.doPickExpression(now, ge, e, lip);
            return true;
        }
        if (msg == "override_expression") {
            expr.overrideExpression(args.empty() ? std::string() : text(in, args[0]), now, lip);
            return true;
        }
        if (msg == "resume_random_expression") {
            const auto [ge, e] = excitement();
            expr.resumeRandomExpression(now, ge, e, lip);
            return true;
        }
        if (msg == "do_expression") {
            const std::string n = args.empty() ? std::string() : text(in, args[0]);
            const float blend = args.size() > 1 ? in.evalValue(args[1]).number() : 0.25f;
            lip.doExpression(n, blend, now);
            return true;
        }
        if (msg == "set_register" && args.size() >= 2) {
            lip.setRegister(text(in, args[0]), in.evalValue(args[1]).number());
            return true;
        }
        if (msg == "set_song") {
            std::string song = args.empty() ? std::string() : text(in, args[0]);
            std::string path = (song.empty() || song == "_blinktrack") ? facefx::kBlinkTrackPath : facefx::unknownSongVocPath(song);
            if (!b_.data_.exists(path) && song.find('/') != std::string::npos) path = song;
            try {
                if (b_.data_.exists(path)) lip.play(std::make_shared<facefx::Anim>(facefx::loadAnim(b_.data_, path)));
            } catch (const std::exception& e) {
                SDL_Log("band: %s set_song %s: %s", m_.slot.c_str(), path.c_str(), e.what());
            }
            return true;
        }
        return false;
    }

    // CharDriver Handle 0x8216CAB0 / CharDriverMidi 0x82185320.
    bool driver(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
        chr::ClipDriver& d = *drv_;
        if (msg == "play" && !args.empty()) {
            d.play(selOf(in, args[0]), flagsArg(in, args, 1, chr::kPlayLast));
            return true;
        }
        if (msg == "play_group" && !args.empty()) {
            d.playGroup(text(in, args[0]), flagsArg(in, args, 1, chr::kPlayLast));
            return true;
        }
        if (msg == "play_if_safe" && args.size() >= 4) {
            d.playIfSafe(flagsArg(in, args, 0, 0), flagsArg(in, args, 1, chr::kPlayLast), flagsArg(in, args, 2, 0),
                         in.evalValue(args[3]).number());
            return true;
        }
        if (msg == "midi_parser" && args.size() >= 3) {
            // 0x82184B28: the clip starts (e - s) before its frame 0 and
            // blends in over that span, reaching full weight on the note;
            // a looping clip already current is left alone.
            scene::CharClipSamples* clip = nullptr;
            const ClipSel sel = selOf(in, args[0]);
            if (d.clipSet()) {
                if (sel.kind == ClipSel::Flags) clip = d.clipSet()->lruByFlags(sel.flags);
                else if (sel.kind == ClipSel::Name) clip = d.clipSet()->clip(sel.name);
                else
                    for (const ClipSel& s : sel.list)
                        if (!clip && s.kind == ClipSel::Name) clip = d.clipSet()->clip(s.name);
            }
            if (!clip) return true;
            if ((clip->playFlags & 0xf0) == chr::kPlayLoop && clip == d.newest()) return true;
            const float span = in.evalValue(args[2]).number() - in.evalValue(args[1]).number();
            d.playAt(clip, 0, -span, 0, span);
            return true;
        }
        if (msg == "set_inactive_clip") return true;  // +0x74 [I: used while the hands are off]
        if (msg == "set_starved") {
            d.starved = args.empty() ? std::string() : text(in, args[0]);
            return true;
        }
        if (msg == "set_beat_scale") {
            if (!args.empty()) d.setBeatScale(in.evalValue(args[0]).number());
            return true;
        }
        if (msg == "get_first_flags" || msg == "get_first_playing_flags") return r = Value::integer(int(d.firstFlags())), true;
        if (msg == "first_clip") {
            const scene::CharClipSamples* f = d.firstPlaying();
            r = f ? Value::symbol(f->name) : Value::integer(0);
            return true;
        }
        if (msg == "offset") {
            if (!args.empty()) d.offset(in.evalValue(args[0]).number());
            return true;
        }
        if (msg == "set" && args.size() >= 2) {
            const std::string k = text(in, args[0]);
            if (k == "realign") d.realign = in.evalValue(args[1]).truthy();
            else if (k == "beat_scale") d.beatScale = in.evalValue(args[1]).number();
            return true;
        }
        if (msg == "enter") return true;
        in.onStub(m_.slot + "::" + o_->name, msg, r);
        return true;
    }

    Band& b_;
    Member& m_;
    scene::Object* o_;
    chr::ClipDriver* drv_ = nullptr;
};

// {$this player}: GH2's Player for the guitarist's game_over ({{$this player}
// won}); other messages go to the host's player object when there is one.
class Band::PlayerObj : public ScriptObject {
public:
    PlayerObj(Band& b, int p) : ScriptObject("band_player" + std::to_string(p), "Player"), b_(b), p_(p) { random = &b.rng_; }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "won") return r = Value::integer(b_.won_ ? 1 : 0), true;
        if (msg == "get" || msg == "set" || msg == "has" || msg == "name") return false;
        if (ScriptObject* host = in.findObject("player" + std::to_string(p_)); host && host != this) {
            r = in.send(host, msg, args, false);
            return true;
        }
        return false;
    }

private:
    Band& b_;
    int p_;
};

// ---- Band --------------------------------------------------------------------

Band::Band(World& world, const GameData& data, const SongInfo& song, Config config, const DataNode* objects)
    : world_(world), data_(data), config_(std::move(config)), rng_(config_.seed) {
    const char* l = SDL_getenv("GD02_BANDLOG");
    log = l && *l && *l != '0';
    venueName_ = world.venue().name();
    animTempo_ = song.animTempo;
    midiFile_ = song.audio.midiPath;
    clips_ = std::make_unique<chr::ClipLibrary>(data);
    assemble(data, song);
    makeScripts(objects);
    // CharWalk types guitarist (path_radius 12).
    float radius = 12;
    for (const DataNode& c : objects_ ? objects_->children : std::vector<DataNode>{})
        if (c.isList() && !c.children.empty() && c.children[0].isText() && c.children[0].s == "CharWalk")
            if (const DataNode* t = c.find("types"))
                if (const DataNode* g = t->find("guitarist"))
                    if (const DataNode* r = g->find("path_radius"); r && r->children.size() > 1)
                        radius = r->children[1].type == DataNode::Type::Float ? r->children[1].f : float(r->children[1].i);
    for (auto& m : members_)
        if (chr::CharWalk* w = m->character->walk()) w->pathRadius = radius;
}

Band::~Band() {
    Interp& in = world_.interp();
    auto drop = [&](ScriptObject* o) {
        if (!o) return;
        in.tasks().killFor(o);
        world_.removeSink(o);
        in.unregisterObject(o);
    };
    for (auto& m : members_) drop(m->script.get());
    for (auto& p : parts_) drop(p.get());
    for (auto& p : players_) drop(p.get());
    parsers_.reset();
    if (world_.band == this) world_.band = nullptr;
}

Band::Member* Band::member(const std::string& slot) const {
    for (const auto& m : members_)
        if (m->slot == slot) return m.get();
    return nullptr;
}

chr::Character* Band::character(const std::string& slot) const {
    Member* m = member(slot);
    return m ? m->character.get() : nullptr;
}

Band::Member* Band::memberOf(const scene::Object* o) const {
    for (const auto& m : members_)
        if (m->object == o) return m.get();
    return nullptr;
}

// game change_chars 0x822B7670.
void Band::assemble(const GameData& data, const SongInfo& song) {
    const SongBand sb = songBand(data, song.id);
    bandList_ = sb.band.empty() ? defaultBand(data) : sb.band;
    // The venue's slots: Character / BandCharacter objects other than the crowd's.
    std::map<std::string, const scene::CharacterObj*> slots;
    world_.venue().forEach([&](scene::Object& o) {
        auto* c = o.as<scene::CharacterObj>();
        if (!c || c->own || c->header.type == "crowd" || startsWith(c->name, "crowd")) return;
        slots.emplace(c->name, c);
    });
    struct Want {
        std::string slot, character;
        int player = -1;
    };
    std::vector<Want> wants;
    for (int p = 0; p < config_.players && p < 2; ++p) {
        std::string ch = config_.character[size_t(p)];
        if (ch.empty()) ch = sb.character.empty() ? "alterna1" : sb.character;  // [C] set_quickplay 0x822B86D0
        if (config_.guitar[size_t(p)].empty()) config_.guitar[size_t(p)] = sb.guitar.empty() ? "sg" : sb.guitar;
        wants.push_back({"guitarist" + std::to_string(p), ch, p});
    }
    for (const std::string& b : bandList_) {
        std::string slot;
        if (b.find("sing") != std::string::npos || b.find("key") != std::string::npos) slot = "singer";
        else if (b.find("drum") != std::string::npos) slot = "drummer";
        else if (b.find("bass") != std::string::npos) slot = "bassist";
        if (slot.empty()) continue;
        if (config_.players > 1 && slot != "drummer") continue;  // two players: the drummer only
        wants.push_back({slot, b, -1});
    }
    for (const Want& w : wants) {
        auto it = slots.find(w.slot);
        if (it == slots.end()) {
            problems_.push_back("no slot " + w.slot + " in " + venueName_);
            continue;
        }
        const std::string path = scene::findMiloRelative(data, "char/x/x.milo_xbox", "../" + w.character + "/og/" + w.character + ".milo");
        auto m = std::make_unique<Member>();
        m->slot = w.slot;
        m->object = it->second;
        m->player = w.player;
        // Outfits: the guitar (change_guitar) and the venue's kit (change_drums).
        std::map<std::string, std::string> outfits;
        std::string guitarMat;
        if (w.player >= 0) {
            auto [outfit, mat] = guitarOutfit(data, config_.guitar[size_t(w.player)], config_.skin[size_t(w.player)]);
            guitarMat = mat;
            outfits["guitar.outfit"] = "char/ng/guitars/gen/" + outfit + ".milo_xbox";
        }
        if (w.slot == "drummer") outfits["drums.outfit"] = "char/ng/drums/gen/dw_" + venueName_ + "_drums.milo_xbox";
        try {
            m->character = std::make_unique<chr::Character>(data, path, *clips_, outfits);
        } catch (const std::exception& e) {
            problems_.push_back(w.slot + ": " + e.what());
            SDL_Log("band: %s: %s", w.slot.c_str(), e.what());
            continue;
        }
        chr::Character& c = *m->character;
        m->type = c.type();
        if (!guitarMat.empty())
            if (auto* gm = c.dir().get("guitar.mesh"))
                if (auto* mesh = gm->as<scene::Mesh>(); mesh && c.dir().get(guitarMat)) mesh->mat = guitarMat;
        c.setXfm(slotXfm(*m));
        if (!it->second->rnd.environment.empty() && it->second->dir)
            c.slotEnviron = it->second->dir->resolve<scene::Environ>(it->second->rnd.environment);
        members_.push_back(std::move(m));
    }
    world_.band = this;
}

scene::Xfm Band::slotXfm(const Member& m) const {
    // The slot's RndDir transform under its parent in the chars directory.
    scene::Xfm x = m.object->rnd.trans.local;
    const std::string& p = m.object->rnd.trans.parent;
    if (!p.empty() && m.object->dir)
        if (const scene::Object* po = m.object->dir->resolve(p); po && po != m.object) x = scene::compose(x, world_.pose().world(po));
    return x;
}

void Band::makeScripts(const DataNode* objects) {
    Interp& in = world_.interp();
    objects_ = objects;
    if (!objects_ || !typeDef(objects_, "BandCharacter", "guitarist")) {
        try {
            ScriptLoader loader(data_);
            objects_ = &in.keep(loader.load("char/char_objects.dta"));
        } catch (const std::exception& e) {
            SDL_Log("band: char/char_objects.dta: %s", e.what());
        }
    }
    for (auto& mp : members_) {
        Member& m = *mp;
        chr::Character& c = *m.character;
        const std::string cls = c.band() ? "BandCharacter" : "Character";
        auto obj = std::make_unique<CharObj>(*this, m, cls);
        if (const DataNode* def = typeDef(objects_, cls, m.type)) obj->setTypeDef(def, 1);
        else problems_.push_back(m.slot + ": no " + cls + " type '" + m.type + "'");
        // Its properties: the character's header (in_solo...).
        if (c.own().header.hasProps) {
            const auto& ch = c.own().header.props.children;
            for (size_t i = 0; i + 1 < ch.size(); i += 2)
                obj->props[ch[i].str] = ch[i + 1].type == 1 ? Value::real(ch[i + 1].asFloat()) : Value::integer(int32_t(ch[i + 1].raw));
        }
        m.script = std::move(obj);
        in.registerObject(m.script.get());
        registerDir(m, c.dir());
        // Clip events and the starved message (0x8219BFF0: $dude = the character).
        c.runScript = [this, &m](const std::string& s) { runClipScript(m, s); };
        c.sendMessage = [this, &m](const std::string& msg) { world_.interp().send(m.script.get(), msg, {}, false); };
        if (chr::ClipDriver* d = c.mainDriver()) {
            d->rng = &rng_;
            d->onPlay = [this, &m](const scene::CharClipSamples& clip, uint32_t) {
                if (onClip) onClip(beat_, m.slot, clip.name);
                if (log) SDL_Log("band: %8.2f %s %s", beat_, m.slot.c_str(), clip.name.c_str());
            };
        }
    }
}

void Band::registerDir(Member& m, const scene::Directory& d) {
    Interp& in = world_.interp();
    for (const auto& o : d.objects) {
        auto p = std::make_unique<PartObj>(*this, m, o.get());
        if (o->cls == "FaceFxLipSyncServo") setupFace(m, o.get());
        in.registerObject(p.get());
        parts_.push_back(std::move(p));
    }
    for (const auto& [_, sub] : d.subdirs)
        if (sub) registerDir(m, *sub);
}

void Band::setupFace(Member& m, scene::Object* servo) {
    auto* s = servo->as<scene::FaceFxLipSyncServo>();
    if (!s || !m.character) return;
    m.lip = std::make_unique<facefx::LipSync>();
    m.expr = std::make_unique<facefx::ExpressionScript>();
    m.expr->randomInt = [this](int lo, int hi) { return rng_.integer(lo, hi); };
    m.expr->randomFloat = [this](float lo, float hi) { return rng_.real(lo, hi); };
    try {
        const std::string fac = facefx::resolveFacePath(data_, m.character->path(), s->faceGraph);
        std::vector<std::string> entries;
        for (const auto& e : s->entries) entries.push_back(e.node);
        m.lip->setup(facefx::loadFaceGraph(data_, fac), facefx::loadServoVisemes(data_, s->header.type), entries);
        // change_song (the end of PreLoad): gh2 {$this set_song _blinktrack};
        // singer {game set_lipsync_song $this} (0x822B6A18): the song's
        // <midi_file folder>/<base>.voc. No file (instrumentals): no song,
        // the servo never poses.
        const std::string voc = s->header.type == "singer" ? facefx::singerVocPath(midiFile_) : facefx::kBlinkTrackPath;
        if (data_.exists(voc)) m.lip->play(std::make_shared<facefx::Anim>(facefx::loadAnim(data_, voc)));
        m.character->attachFace(m.lip.get());
    } catch (const std::exception& e) {
        SDL_Log("band: %s lip.servo: %s", m.slot.c_str(), e.what());
    }
}

// The gh2 servos' expression_task (a kTaskSeconds task: it runs whether the
// character shows or not); the faces pose in the characters' polls.
void Band::pollFaces() {
    Interp& in = world_.interp();
    const float song = float(in.tasks().time(kTaskSeconds));
    ScriptObject* g = in.findObject("game");
    const int excitement = currentExcitement();
    for (auto& mem : members_)
        if (mem->lip && mem->expr && mem->character) mem->expr->update(song, g != nullptr, excitement, *mem->lip);
}

void Band::loadSong(const MidiFile& midi, const Song* song) {
    Interp& in = world_.interp();
    std::vector<std::string> names = {"bassist_parser", "drummer_parser", "singer_parser", "keyboard_parser"};
    for (int p = 0; p < config_.players && p < 2; ++p)
        for (const char* k : {"_fret", "_strum", "_fret_pos", "_parser"}) names.push_back("player" + std::to_string(p) + k);
    if (config_.bass[0] || config_.bass[1]) names.push_back("bass_strum");
    song_ = song;
    parsers_ = std::make_unique<MidiParserSet>(in, data_, names);
    for (const auto& p : parsers_->parsers()) p->random = &rng_;  // their random picks (0x8233EB88)
    parsers_->load(midi, song);
}

// filter_clips: the guitarist's (game 0x822B6650) and the band's
// (char_objects.dta: delete_domain_any song_tempo kTempo* TRUE).
void Band::filterClips(Member& m) {
    chr::ClipDriver* d = m.character->mainDriver();
    chr::ClipSet* set = d ? d->clipSet() : nullptr;
    if (!set) return;
    const uint32_t tempo = uint32_t(animTempo_);
    if (m.type != "guitarist") {
        set->deleteDomainAny(tempo, kTempoMask, true);
        return;
    }
    static chr::ClipSet* firstShared = nullptr;  // _DAT_8280DA7C
    set->shuffleGroups(rng_);  // 0x821663A0
    const bool mp = config_.players > 1;
    set->deleteGroupClips(venueName_ + (mp ? "_multi_exclude" : "_exclude"));
    set->deleteDomainAny(tempo, kTempoMask, true);
    if (!mp) {
        set->trimGroup(config_.campaignFinal ? "win" : "win_finals", 0);
        set->trimGroup("sync_head_bang", 0);
        set->trimGroup("sync_wag", 0);
    } else {
        set->trimGroup("multi_exclude", 0);
        if (!firstShared) {
            firstShared = set;
        } else {
            // The clips the other guitarist's set shares are dropped here.
            for (scene::CharClipSamples* c : firstShared->groupClips("shared"))
                if (scene::CharClipSamples* mine = set->clip(c->name); mine && set->group("shared") && set->groupHas(*set->group("shared"), mine))
                    set->deleteClip(mine);
            firstShared = nullptr;
        }
        for (const char* g : {"walk_walk", "walk_turn", "walk_stop", "win_finals", "interact_stage", "interact_right", "interact_back"})
            set->trimGroup(g, 0);
    }
    set->trimGroup("intro", 1);
    set->trimGroup("lose", 1);
    set->trimGroup("normal", 5);
    set->trimGroup("extreme", 5);
    set->trimGroup("win", 1);
    set->trimGroup("win_idle", 1);
    set->trimGroup("lose_idle", 1);
    set->trimGroup("star_power", 3);
}

// GuitarCharacter Enter 0x822D3B18 (after the Character enter).
void Band::guitarEnter(Member& m) {
    auto& st = guitar_[&m];
    st = std::make_unique<GuitarState>();
    st->mode = "idle";
    st->bad = "bad";
    st->handsOn = true;
    if (const DataNode* def = m.script->typeDef())
        if (const DataNode* p = def->find("interact_probs"))
            for (size_t k = 1; k < p->children.size(); ++k) st->interactProbs.push_back(p->children[k].type == DataNode::Type::Float ? p->children[k].f : float(p->children[k].i));
    chr::ClipDriver* d = m.character->mainDriver();
    walkSchedule(m);
    if (chr::ClipSet* set = d ? d->clipSet() : nullptr) {
        // 0x82175C58: the run-time flags.
        for (const char* g : {"interact_stage", "interact_right", "interact_back"}) set->orGroupFlags(g, 0x30000);
        for (const char* g : {"star_power", "sync_jump", "sync_wag", "sync_head_bang"}) set->orGroupFlags(g, kSpecial);
        for (const char* g : {"win", "lose", "win_finals"}) set->orGroupFlags(g, 0x40000);
        set->orGroupFlags("star_power", kStarPower);
    }
}

void Band::enter() {
    if (entered_) return;
    Interp& in = world_.interp();
    // Clip sets enter first (filter_clips).
    std::set<const chr::ClipSet*> filtered;
    for (auto& m : members_)
        if (chr::ClipDriver* d = m->character->mainDriver(); d && d->clipSet() && filtered.insert(d->clipSet()).second) filterClips(*m);
    // Character::Enter (0x82153050 -> the pollable's enter: the dta `enter`
    // handler), then GuitarCharacter's own (0x822D3B18) [I: the base enter
    // runs the script first, so the walk schedule's solo teleport wins].
    for (auto& m : members_) {
        in.send(m->script.get(), "enter", {}, false);
        if (m->type == "guitarist") guitarEnter(*m);
    }
    if (parsers_) parsers_->enter();
    entered_ = true;
}

void Band::runClipScript(Member& m, const std::string& script) {
    if (script.empty()) return;
    auto it = scripts_.find(script);
    if (it == scripts_.end()) {
        DataNode tree;
        try {
            tree = parseDtaText(script);
        } catch (const std::exception& e) {
            SDL_Log("band: clip script %s: %s", script.c_str(), e.what());
        }
        it = scripts_.emplace(script, std::move(tree)).first;
    }
    Interp& in = world_.interp();
    Value saved = in.var("dude");
    Value savedThis = in.var("this");
    in.var("dude") = Value::obj(m.script.get());
    // 0x8219BFF0: $this is the clip ({$dude start_interact $this 4}); the
    // band's random numbers stay the character's.
    chr::ClipDriver* d = m.character->mainDriver();
    if (d && d->scriptClip) in.var("this") = Value::symbol(d->scriptClip->name);
    else in.var("this") = Value::obj(m.script.get());
    for (const DataNode& n : it->second.children) in.eval(n);
    in.var("dude") = saved;
    in.var("this") = savedThis;
}

void Band::fireTriggers(Member& m, const std::string& event) {
    for (const auto& o : m.character->dir().objects)
        if (auto* t = o->as<scene::EventTrigger>())
            if (std::find(t->triggerEvents.begin(), t->triggerEvents.end(), event) != t->triggerEvents.end()) {
                if (log) SDL_Log("band: %s %s -> %s", m.slot.c_str(), event.c_str(), t->name.c_str());
                ++triggerCounts_[event];
                world_.trigger(t);
            }
}

// ---- GuitarCharacter -------------------------------------------------------------

std::string Band::currentGroup(Member& m) {
    // 0x822D14B8: "bad" while the player does badly.
    const auto& st = guitar_[&m];
    return doingBadly(m) ? st->bad : st->mode;
}

bool Band::doingBadly(const Member& m) const {
    // 0x822C6DC0: rock meter under 0.2 [C]; the face-off rule (score lead)
    // [C structure] is left to the multiplayer phase.
    if (!rating || m.player < 0) return false;
    return rating(m.player) < 0.2f;
}

void Band::setMode(Member& m, const std::string& group) {
    // 0x822D24A0.
    auto& st = guitar_[&m];
    st->mode = group;
    chr::ClipDriver* d = m.character->mainDriver();
    if (!d || (m.character->walk() && m.character->walk()->state() != 0)) return;
    uint32_t flags = chr::kPlayNow;
    if (d->flagWeight(kSpecial) > 0) flags = chr::kPlayLast;
    else if (d->newest() && (d->newest()->flags & kSpecial)) return;
    d->playGroup(currentGroup(m), flags);
}

void Band::clearMode(Member& m, const std::string& group) {
    // 0x822D26A8: back to normal when the mode is `group` (the clip carries on).
    auto& st = guitar_[&m];
    if (st->mode == group) st->mode = "normal";
}

void Band::oneShot(Member& m, const std::string& group) {
    // 0x822D2780: not while walking (CharWalk state 1) or in Star Power.
    if ((m.character->walk() && m.character->walk()->state() == 1) || flagWeight(m.slot, kStarPower) > 0) return;
    if (config_.players < 2 && (group == "sync_head_bang" || group == "sync_wag")) return;
    chr::ClipDriver* d = m.character->mainDriver();
    if (!d || !d->clipSet() || d->clipSet()->groupClips(group).empty()) return;
    d->playGroup(group, chr::kPlayDirty);
}

void Band::setGameOver(Member& m, const std::string& group) {
    // 0x822D22E0.
    auto& st = guitar_[&m];
    st->gameOver = true;
    st->mode = group;
    st->win = group != "lose";
    const bool post = config_.players > 1 || (config_.encore && group != "lose");  // 0x822CA6E0 [C]
    st->post = post ? 1 : 0;
    st->postGroup = post ? (st->win ? "win_idle" : "lose_idle") : "";
    st->startAt = seconds_ + (st->win ? 0.75 : 0.0);
}

void Band::gameOverPoll(Member& m) {
    // 0x822D2038.
    auto& st = guitar_[&m];
    chr::ClipDriver* d = m.character->mainDriver();
    if (!d) return;
    if (st->startAt != 1e30) {
        if (seconds_ < st->startAt) return;
        // 0x822D1F80: the walk dropped; a winner faces his regulate
        // waypoint's way (its rotation rows); no regulating; the game-over
        // group, no blend.
        chr::Character& c = *m.character;
        if (c.walk()) c.walk()->reset();
        if (st->win && c.regulate()) {
            scene::Xfm x = c.xfm();
            x.rot = c.regulate()->xfm.rot;
            c.setXfm(x);
        }
        c.setRegulate(nullptr);
        d->playGroup(st->mode, chr::kPlayNoBlend);
        st->startAt = 1e30;
    }
    if (d->settled() && st->post == 1) {
        st->postClip = d->playGroup(st->postGroup, 0x204);
        st->post = 2;
    }
    if (st->post == 2 && d->firstPlaying() && d->firstPlaying() == st->postClip) st->post = 3;
}

void Band::guitarPoll(Member& m, float dBeats) {
    // 0x822D2B90.
    auto it = guitar_.find(&m);
    if (it == guitar_.end() || !it->second) return;
    auto& st = *it->second;
    if (st.mode.empty()) return;
    if (st.gameOver) {
        gameOverPoll(m);
        return;
    }
    chr::ClipDriver* d = m.character->mainDriver();
    chr::CharWalk* walk = m.character->walk();
    if (!d) return;
    pollTasks(m);
    if (walk && walk->state() != st.walkState) {
        if (walk->state() == 0 && st.walkState != 0) {
            // Arrived: how far from the target spot (the servo regulates the rest).
            const auto p = m.character->xfm().pos;
            const auto& t = walk->target().xfm.pos;
            const float d = std::hypot(p[0] - t[0], p[1] - t[1]);
            walkArrivals_.push_back(d);
            if (log) SDL_Log("band: %8.2f %s arrived at %s, %.1f off (%.2f s)", beat_, m.slot.c_str(), walk->target().name.c_str(), double(d), seconds_);
        }
        st.walkState = walk->state();
    }
    const int ex = currentExcitement();
    // 1. The walk timer (0x822D1288): walk_delays[excitement] since the last
    //    roll; in "normal" a walk to a kWalkSpot (0x822D11D0).
    if (st.canWalk && walk && walk->state() == 0) {
        const float delay = ex >= 0 && ex < 5 ? st.delays[size_t(ex)] : 1e30f;
        if (delay - float(seconds_ - st.lastWalk) < 0 && st.mode == "normal")
            addTask(m, newWalk(m, float(beat_), float(beat_) + st.walkTime, 1.0f, 0x40));
    }
    if (walk && walk->state() == 1) return;
    // 2. An interact queued to start within this poll is dropped below Okay.
    if (!d->settled()) {
        const auto& head = d->chain().back();
        const float x = head.delay - 4.0f;
        if (0 <= x && x < dBeats && (head.clip->flags & kInteract) && ex < 2) d->dropUnstarted();
    }
    // 3. Settled: the mode's (or "bad") group next, kPlayLast (0x204 for
    //    idle after a realtime clip); before it, interact_probs[excitement]
    //    for an interact at the nearest waypoint with a clip_group.
    if (d->settled()) {
        const std::string g = currentGroup(m);
        uint32_t flags = chr::kPlayLast;
        const int ns = d->newestStarted();
        if (g == "idle" && ns >= 0 && (d->chain()[size_t(ns)].flags & 0xf600) == chr::kPlayRealTime) {
            flags = 0x204;
        } else {
            const float prob = ex >= 0 && size_t(ex) < st.interactProbs.size() ? st.interactProbs[size_t(ex)] : 0.0f;
            if (rng_.real01() < prob) {
                const auto pos = m.character->xfm().pos;
                if (const scene::Waypoint* w = nearestWaypoint({pos[0], pos[1], pos[2]}, ~0u)) {
                    std::string group;
                    if (w->header.type == "interact") group = "interact_back";  // the type's default
                    if (w->header.hasProps) {
                        const auto& ch = w->header.props.children;
                        for (size_t i = 0; i + 1 < ch.size(); i += 2)
                            if (ch[i].str == "clip_group" && !ch[i + 1].str.empty()) group = ch[i + 1].str;
                    }
                    if (!group.empty() && std::find(st.usedInteracts.begin(), st.usedInteracts.end(), group) == st.usedInteracts.end() &&
                        d->flagWeight(kSpecial) == 0) {
                        scene::CharClipGroup* cg = d->clipSet() ? d->clipSet()->group(group) : nullptr;
                        scene::CharClipSamples* clip = cg ? d->clipSet()->next(*cg) : nullptr;
                        auto t = newTimed(m, Task::Interact, clip, 1.0f);
                        t->group = group;
                        addTask(m, std::move(t));
                    }
                }
            }
            if (!d->settled()) return;
        }
        d->playGroup(g, flags);
    }
}

// ---- walks (M9.6) ------------------------------------------------------------------

chr::Spot Band::spotOf(const scene::Waypoint* w) const {
    chr::Spot s;
    s.id = w;
    s.name = w->name;
    s.xfm = world_.pose().world(w);
    s.flags = w->flags;
    s.radius = w->radius;
    s.yRadius = w->unknown[0];
    s.angle = w->unknown[1];
    return s;
}

const scene::Waypoint* Band::nearestWaypoint(const std::array<float, 3>& p, uint32_t mask) const {
    const scene::Waypoint* best = nullptr;
    float bd = 1e30f;
    for (const scene::Waypoint* w : world_.waypoints()) {
        if (!(w->flags & mask)) continue;
        const auto q = world_.pose().world(w).pos;
        const float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
        const float d = dy * dy + dx * dx + dz * dz;
        if (d < bd) bd = d, best = w;
    }
    return best;
}

const scene::Waypoint* Band::findWaypoint(uint32_t mask) const {
    for (const scene::Waypoint* w : world_.waypoints())
        if (w->flags & mask) return w;
    return nullptr;
}

std::vector<const scene::Waypoint*> Band::connections(const scene::Waypoint* w) const {
    std::vector<const scene::Waypoint*> v;
    for (const std::string& n : w->connections)
        if (const scene::Waypoint* c = world_.venue().find<scene::Waypoint>(n)) v.push_back(c);
    return v;
}

std::vector<chr::Spot> Band::searchPath(const scene::Waypoint* from, uint32_t target, uint32_t pass) {
    // 0x82196940: the first waypoint of the global list with `target` flags
    // (not `from`) that a depth-first search through the connections reaches
    // without crossing a waypoint with `pass` flags (0x82196430); it moves to
    // the list's end.
    std::vector<const scene::Waypoint*> path;
    std::map<const scene::Waypoint*, bool> seen;
    const scene::Waypoint* goal = nullptr;
    std::function<bool(const scene::Waypoint*)> dfs = [&](const scene::Waypoint* n) {
        if (n == goal) return true;
        seen[n] = true;
        for (const scene::Waypoint* c : connections(n)) {
            if (seen[c]) continue;
            if (c != goal && (c->flags & pass)) continue;
            path.push_back(c);
            if (dfs(c)) return true;
            path.pop_back();
        }
        return false;
    };
    const std::vector<const scene::Waypoint*> order = world_.waypoints();
    for (size_t i = 0; i < order.size(); ++i) {
        const scene::Waypoint* w = order[i];
        if (!(w->flags & target) || w == from) continue;
        seen.clear();
        path.clear();
        goal = w;
        if (dfs(from)) {
            world_.waypointToEnd(w);
            std::vector<chr::Spot> out;
            for (const scene::Waypoint* p : path) out.push_back(spotOf(p));
            return out;
        }
    }
    return {};
}

float Band::maxDistance(uint32_t from, uint32_t to) const {
    // 0x82195618 / 0x82194EA0: the longest connection path from a `from`
    // waypoint to a `to` one through waypoints with neither.
    float best = 0;
    std::map<const scene::Waypoint*, bool> seen;
    std::function<void(float, const scene::Waypoint*)> walkFrom = [&](float d, const scene::Waypoint* n) {
        for (const scene::Waypoint* c : connections(n)) {
            if (seen[c]) continue;
            seen[c] = true;
            const auto a = world_.pose().world(c).pos, b = world_.pose().world(n).pos;
            const float dist = std::sqrt((b[1] - a[1]) * (b[1] - a[1]) + (b[0] - a[0]) * (b[0] - a[0]) + (b[2] - a[2]) * (b[2] - a[2]));
            if (c->flags & to) best = std::max(best, dist + d);
            else if (!(c->flags & from)) walkFrom(dist + d, c);
        }
    };
    for (const scene::Waypoint* w : world_.waypoints()) {
        if (!(w->flags & from)) continue;
        seen.clear();
        seen[w] = true;
        walkFrom(0, w);
    }
    return best;
}

int Band::currentExcitement() const {
    if (excitement) return excitement();
    Interp& in = world_.interp();
    if (ScriptObject* g = in.findObject("game")) {
        Value v = in.send(g, "get", {Value::symbol("excitement")}, false);
        if (v.isNumber()) return v.integerValue();
    }
    return -1;
}

double Band::beatOfMs(double ms) const {
    if (!song_) return ms / 500.0;
    return double(song_->tempo.msToTick(float(ms))) / 480.0;
}

float Band::clipBeats(float frames, const scene::CharClipSamples* clip, const GuitarState& st) const {
    // 0x822D1C88: frames as beats (a realtime clip's through the tempo map
    // from now), times the tempo scale.
    float x = std::max(0.0f, frames);
    if (clip && (clip->playFlags & chr::kPlayRealTime) && song_) {
        const double nowMs = song_->tempo.tickToMs(float(beat_ * 480.0));
        x = float(beatOfMs(nowMs + double(x / clip->framesPerSec) * 1000.0) - beat_);
    }
    return st.tempoScale * x;
}

void Band::rollWalkDelays(GuitarState& st) {
    // 0x822D12E8: walk_delays per excitement, a range -> RandomFloat(lo, hi), else never.
    st.lastWalk = seconds_;
    const DataNode* wd = nullptr;
    for (auto& m : members_)
        if (guitar_.count(m.get()) && guitar_.at(m.get()).get() == &st && m->script && m->script->typeDef())
            wd = m->script->typeDef()->find("walk_delays");
    for (size_t i = 0; i < st.delays.size(); ++i) {
        const DataNode* e = wd && i + 1 < wd->children.size() ? &wd->children[i + 1] : nullptr;
        if (e && e->isList() && e->children.size() >= 2) {
            const float lo = e->children[0].type == DataNode::Type::Float ? e->children[0].f : float(e->children[0].i);
            const float hi = e->children[1].type == DataNode::Type::Float ? e->children[1].f : float(e->children[1].i);
            st.delays[i] = rng_.real(lo, hi);
        } else {
            st.delays[i] = 1e30f;
        }
    }
}

float Band::nextSoloEvent(const Member& m, float before) const {
    // 0x822D1518: from the player parser's next event, the first solo_on /
    // solo_off before `before`; 1e30 when none.
    MidiParser* p = parsers_ ? parsers_->find("player" + std::to_string(std::max(0, m.player)) + "_parser") : nullptr;
    if (!p) return 1e30f;
    for (const auto& e : p->events()) {
        if (e.start < float(beat_)) continue;
        if (before <= e.start) return 1e30f;
        if (e.value == "solo_on" || e.value == "solo_off") return e.start;
    }
    return 1e30f;
}

std::unique_ptr<Band::Task> Band::newWalk(Member&, float start, float end, float priority, uint32_t flags) {
    auto t = std::make_unique<Task>();
    t->kind = Task::Walk;
    t->start = start, t->end = end, t->priority = priority;
    t->mask = 0xffffffffu;
    t->flags = flags;
    return t;
}

std::unique_ptr<Band::Task> Band::newTimed(Member& m, int kind, scene::CharClipSamples* clip, float priority) {
    // 0x822E4AF8 (Star Power, mask 1) / 0x822E4C80 (interact, mask -1):
    // from now until the current clip's end plus the clip; priority -1
    // (refused) without a clip, and for an interact that a solo_on / off
    // would cut.
    auto t = std::make_unique<Task>();
    t->kind = Task::Kind(kind);
    t->mask = kind == Task::StarPower ? 1u : 0xffffffffu;
    t->priority = -1;
    t->clip = clip;
    if (!clip) return t;
    const auto& st = *guitar_[&m];
    chr::ClipDriver* d = m.character->mainDriver();
    t->start = float(beat_);
    float remain = 0;
    if (const int ns = d ? d->newestStarted() : -1; ns >= 0) {
        const auto& n = d->chain()[size_t(ns)];
        remain = clipBeats(n.clip->endFrame - n.frame, n.clip, st);
    }
    t->end = clipBeats(clip->endFrame - clip->startFrame, clip, st) + remain + t->start;
    if (kind == Task::StarPower || t->end <= nextSoloEvent(m, t->end)) t->priority = priority;
    return t;
}

void Band::walkSchedule(Member& m) {
    // 0x822D16B8.
    auto& st = *guitar_[&m];
    chr::CharWalk* walk = m.character->walk();
    st.canWalk = findWaypoint(0x40) && config_.players < 2 && walk;
    rollWalkDelays(st);
    st.lastWalk = -15.0;
    const double songEnd = song_ ? beatOfMs(song_->endMs) : 1e30;  // [I: FUN_82278D30, the song's end]
    st.tempoScale = 1;
    MidiParser* parser = parsers_ ? parsers_->find("player" + std::to_string(std::max(0, m.player)) + "_parser") : nullptr;
    std::vector<MidiParser::EventInfo> events;
    if (parser) events = parser->events();
    for (const auto& e : events)
        if (e.value == "half_tempo") {
            st.tempoScale = 2;
            break;
        }
    st.tasks.clear();
    if (st.canWalk) {
        float maxWait = 6;  // MAX_WALK_WAIT
        if (const DataNode* def = m.script->typeDef())
            if (const DataNode* w = def->find("max_walk_wait"); w && w->children.size() > 1)
                maxWait = w->children[1].type == DataNode::Type::Float ? w->children[1].f : float(w->children[1].i);
        float perUnit = 0;
        const float clips = walk->walkTime(perUnit);
        const float unit = st.tempoScale * perUnit;
        const float fixed = st.tempoScale * (clips + maxWait);
        st.walkTime = maxDistance(0x40, 0x40) * unit + fixed;
        const float soloLead = maxDistance(0x40, 0x80) * unit + fixed;
        const uint32_t endFlag = config_.campaignFinal ? 0x1000u : (config_.encore ? 0x400u : 0x800u);
        const float endLead = maxDistance(0x40, endFlag) * unit + fixed;
        const scene::Waypoint* soloSpot = findWaypoint(0x80);
        std::unique_ptr<Task> pending;
        for (const auto& e : events) {
            if (e.value == "solo_on") {
                if (!soloSpot) {
                    pending = std::make_unique<Task>();
                    pending->kind = Task::Hold;
                    pending->start = pending->end = e.start;
                    pending->priority = 2;
                    pending->mask = 0xfffffffeu;
                } else {
                    pending = newWalk(m, e.start - soloLead, 0, 2, 0x80);
                    if (e.start < 8) m.character->teleport(spotOf(soloSpot));
                }
            } else if (e.value == "solo_off" && pending) {
                pending->end = e.start + 4;
                addTask(m, std::move(pending));
            }
        }
        if (fixed < endLead) addTask(m, newWalk(m, float(songEnd) - endLead, float(songEnd), 4, endFlag));
    }
    auto hold = std::make_unique<Task>();
    hold->kind = Task::Hold;
    hold->start = float(songEnd);
    hold->end = float(songEnd + 1e30);
    hold->priority = 4;
    hold->mask = 0xffffffffu;
    addTask(m, std::move(hold));
}

namespace {

// 0x822E4910: a walk that replaces (or outlives) another walk takes over its
// start: an end walk a walk / solo walk's (and comes from a walk spot when
// that was a random walk), a solo walk a random walk's.
void conflict(Band::Task& self, const Band::Task& other) {
    if (self.kind != Band::Task::Walk || other.kind != Band::Task::Walk) return;
    if (!(self.flags & 0x80)) {
        if ((self.flags & 0xc00) && (other.flags & 0xc0)) {
            self.start = other.start;
            if (other.flags & 0x40) self.fromSpot = true;
        }
    } else if (other.flags & 0x40) {
        self.start = other.start;
    }
}

}  // namespace

void Band::addTask(Member& m, std::unique_ptr<Task> t) {
    // 0x82198348: Check (0x82197CB8) then Insert (0x82198188).
    auto& q = guitar_[&m]->tasks;
    if (!t) return;
    Task* nt = t.get();
    size_t idx = 0;
    Task* bumped = nullptr;
    bool refused = nt->priority < 0;
    if (!refused) {
        while (idx < q.size() && q[idx]->start < nt->start) ++idx;
        for (size_t k = idx; k-- > 0 && !refused;) {
            const Task& e = *q[k];
            if (nt->start < e.end && (e.mask & nt->mask) && nt->priority <= e.priority) refused = true;
        }
        for (size_t k = idx; k < q.size() && !refused; ++k) {
            Task& e = *q[k];
            if (nt->end <= e.start) break;
            if (nt->priority <= e.priority && (e.mask & nt->mask)) bumped = &e, refused = true;
        }
    }
    if (refused) {
        if (bumped) conflict(*bumped, *nt);
        return;
    }
    bool placed = false;
    if (idx > 0 && nt->start < q[idx - 1]->end && (q[idx - 1]->mask & nt->mask)) {
        conflict(*nt, *q[idx - 1]);
        q[idx - 1] = std::move(t);
        placed = true;
    }
    for (size_t k = idx; k < q.size(); ++k) {
        Task& e = *q[k];
        if (nt->end <= e.start) break;
        if (nt->mask != 0 && e.mask != 0) {
            if (placed) {
                // Erased; GH2 then steps over the entry that moved into its place.
                q.erase(q.begin() + std::ptrdiff_t(k));
            } else {
                q[k] = std::move(t);
                placed = true;
            }
        }
    }
    if (!placed) q.insert(q.begin() + std::ptrdiff_t(idx), std::move(t));
}

void Band::pollTasks(Member& m) {
    // 0x82197FB0 at the song beat.
    auto& q = guitar_[&m]->tasks;
    const float now = float(beat_);
    for (size_t i = 0; i < q.size(); ++i) {
        Task& e = *q[i];
        if (now < e.start) return;
        if (!e.started) {
            e.priority = 1e30f;
            startTask(m, e);
            e.started = true;
        }
        taskPoll(m, e);
        if (e.end < now) {
            endTask(m, e);
            q.erase(q.begin() + std::ptrdiff_t(i));
            --i;
        }
    }
}

void Band::startTask(Member& m, Task& t) {
    chr::ClipDriver* d = m.character->mainDriver();
    switch (t.kind) {
    case Task::Walk: walkStart(m, t); break;
    case Task::StarPower:
        // 0x822E49D8: the clip at once with kPlayFirst.
        if (d && t.clip) d->playClip(t.clip, chr::kPlayFirst);
        break;
    case Task::Interact:
        // 0x822E50D0: the clip with kPlayFirst; its group is used up.
        if (d && t.clip) d->playClip(t.clip, chr::kPlayFirst);
        guitar_[&m]->usedInteracts.push_back(t.group);
        ++interactsStarted_;
        if (log) SDL_Log("band: %8.2f %s interact %s", beat_, m.slot.c_str(), t.group.c_str());
        break;
    case Task::Hold: break;
    }
}

void Band::taskPoll(Member& m, Task& t) {
    chr::ClipDriver* d = m.character->mainDriver();
    if (t.kind == Task::Walk) {
        // 0x822E48D8: once the walk is over, a random walk ends; another
        // stops holding Star Power off (mask bit 0).
        chr::CharWalk* w = m.character->walk();
        if (w && w->state() != 0) return;
        if (t.flags & 0x40) t.end = t.start;
        else t.mask &= ~1u;
    } else if (t.kind == Task::StarPower && d) {
        // 0x822E4A30: ends once its clip is no longer the first playing.
        if (!t.seen) {
            if (d->firstPlaying() == t.clip) t.seen = true;
        } else if (d->firstPlaying() != t.clip) {
            t.end = t.start;
        }
    }
}

void Band::endTask(Member& m, Task& t) {
    // 0x822E4AE8: a walk's end rolls the walk delays again.
    if (t.kind == Task::Walk) rollWalkDelays(*guitar_[&m]);
}

void Band::walkStart(Member& m, Task& t) {
    // 0x822E4E78.
    chr::Character& c = *m.character;
    chr::CharWalk* walk = c.walk();
    if (!walk) return;
    if (walk->state() != 0 && walk->state() != 2 && !walk->actuallyWalking()) walk->reset();
    const auto p = c.xfm().pos;
    const scene::Waypoint* nearest = nearestWaypoint({p[0], p[1], p[2]}, 0x40);
    if (!nearest) return;
    if (!t.fromSpot && (t.flags & 0xc00) && (t.flags & nearest->flags)) return;  // already there
    const std::vector<chr::Spot> path = searchPath(nearest, t.flags, t.flags | 0x40);
    if (path.empty()) return;
    // The way to the first waypoint in the target's frame: sideways
    // (|2x| > |y|) strafes left / right (a quarter of the time forward), else
    // forward / backward.
    const chr::Spot& first = path.front();
    const chr::Spot& last = path.back();
    const float d[3] = {first.xfm.pos[0] - p[0], first.xfm.pos[1] - p[1], first.xfm.pos[2] - p[2]};
    const auto& r = last.xfm.rot;
    const float x = r[0] * d[0] + r[1] * d[1] + r[2] * d[2];
    const float y = r[3] * d[0] + r[4] * d[1] + r[5] * d[2];
    uint32_t walkFlags, turnFilter, stopFilter;
    if (std::fabs(x * 2.0f) > std::fabs(y)) {
        const uint32_t side = x > 0 ? 0x200u : 0x100u;  // kWalkRight / kWalkLeft
        walkFlags = rng_.real01() < 0.25f ? 0x400u : side;
        turnFilter = side;
        stopFilter = 0;
    } else {
        const uint32_t dir = y > 0 ? 0x400u : 0x800u;  // kWalkForward / kWalkBackward
        walkFlags = dir;
        turnFilter = dir == 0x800u ? 0x800u : 0u;
        stopFilter = dir;
    }
    walkFlags |= currentExcitement() >= 3 ? 0x2000u : 0x1000u;  // kWalkExtreme / kWalkNormal
    if (walk->walk(path, chr::kPlayFirst, walkFlags, turnFilter, stopFilter)) {
        ++walksStarted_;
        if (log) SDL_Log("band: %8.2f %s walks to %s (%zu waypoints) at %.2f s", beat_, m.slot.c_str(), last.name.c_str(), path.size(), seconds_);
    }
}

bool Band::guitarMessage(Member& m, const std::string& msg, std::vector<Value>& args, Value& r) {
    Interp& in = world_.interp();
    if (msg == "play") return setMode(m, "normal"), true;
    if (msg == "idle") return setMode(m, "idle"), true;
    if (msg == "wail_on") return setMode(m, "extreme"), true;
    if (msg == "wail_off") return clearMode(m, "extreme"), true;
    if (msg == "gtr_solo_on") return setMode(m, "solo"), true;
    if (msg == "gtr_solo_off") return clearMode(m, "solo"), true;
    if (msg == "band_jump") return oneShot(m, "sync_jump"), true;
    if (msg == "sync_wag" || msg == "sync_head_bang") return oneShot(m, msg), true;
    if (msg == "set_game_over") return setGameOver(m, args.empty() ? "lose" : text(in, args[0])), true;
    if (msg == "lose_teleport") {
        // 0x822D28E8: a singer (or keyboardist) within 72 units (x y) ->
        // the nearest kWalkSpot (a teleport: regulated there).
        if (Member* sg = member("singer")) {
            const auto a = m.character->xfm().pos;
            const auto sph = sg->character->worldSphere();  // [I: the singer's world sphere centre]
            const float dx = sph[0] - a[0], dy = sph[1] - a[1];
            if (std::sqrt(dy * dy + dx * dx) <= 72.0f)
                if (const scene::Waypoint* w = nearestWaypoint({a[0], a[1], a[2]}, 0x40)) m.character->teleport(spotOf(w));
        }
        return true;
    }
    if (msg == "active_players_changed") {
        guitar_[&m]->activePlayers = args.empty() ? 0 : in.evalValue(args[0]).integerValue();
        return true;
    }
    if (msg == "actually_walking") return r = Value::integer(actuallyWalking(m.slot) ? 1 : 0), true;
    if (msg == "playing_starpower") return r = Value::integer(playingStarpower(m.slot) ? 1 : 0), true;
    if (msg == "playing_far_starpower") return r = Value::integer(flagWeight(m.slot, kStarPowerFar) > 0 ? 1 : 0), true;
    if (msg == "outro_complete") return true;
    if (msg == "player") {
        // The player: {{$this player} won}.
        auto& p = players_[size_t(std::clamp(m.player, 0, 1))];
        if (!p) p = std::make_unique<PlayerObj>(*this, std::clamp(m.player, 0, 1));
        r = Value::obj(p.get());
        return true;
    }
    if (msg == "won") return r = Value::integer(won_ ? 1 : 0), true;
    return false;
}

void Band::starPowerStart(int player) {
    // 0x822D25A8: unless a one-shot is blending, a Star Power task on the
    // guitarist's timeline (priority 1, mask 1: refused while walking) plays
    // the next star_power clip with kPlayFirst (0x822E4AF8 / 0x822E49D8; it
    // ends when that clip is no longer the first playing).
    for (auto& m : members_) {
        if (m->type != "guitarist" || m->player != player || !guitar_.count(m.get())) continue;
        chr::ClipDriver* d = m->character->mainDriver();
        if (!d || !d->clipSet() || d->flagWeight(kSpecial) != 0) continue;
        scene::CharClipGroup* g = d->clipSet()->group("star_power");
        scene::CharClipSamples* c = g ? d->clipSet()->next(*g) : nullptr;
        addTask(*m, newTimed(*m, Task::StarPower, c, 1.0f));
    }
}

bool Band::playingStarpower(const std::string& slot) const {
    return flagWeight(slot, kStarPower) > 0 && flagWeight(slot, kStarPowerFar) <= 0;
}

float Band::flagWeight(const std::string& slot, uint32_t flags) const {
    chr::Character* c = character(slot);
    chr::ClipDriver* d = c ? c->mainDriver() : nullptr;
    return d ? d->flagWeight(flags) : 0.0f;
}

bool Band::actuallyWalking(const std::string& slot) const {
    chr::Character* c = character(slot);
    return c && c->walk() && c->walk()->actuallyWalking();
}

bool Band::objectXfm(const std::string& name, const std::string& subpart, scene::Xfm& out) const {
    chr::Character* c = character(name);
    if (!c) return false;
    if (!subpart.empty() && c->worldOf(subpart, out)) return true;
    out = c->xfm();
    return true;
}

// ---- the frame -------------------------------------------------------------------

void Band::update(double seconds, double beats, const std::array<float, 3>* camera) {
    const float dS = first_ ? 0.0f : float(std::max(0.0, seconds - seconds_));
    const float dB = first_ ? 0.0f : float(std::max(0.0, beats - beat_));
    first_ = false;
    seconds_ = seconds, beat_ = beats, dBeats_ = dB;
    if (parsers_ && entered_) parsers_->poll(beats);
    if (entered_) pollFaces();
    Interp& in = world_.interp();
    const float faceSong = float(in.tasks().time(kTaskSeconds)), faceUi = float(in.tasks().time(kTaskUISeconds));
    for (auto& m : members_)
        if (m->type == "guitarist" && entered_) guitarPoll(*m, dB);
    for (auto& m : members_) {
        chr::Character& c = *m->character;
        chr::Skeleton& sk = c.skeleton();
        // What the world animates on the character's objects (the drum kit's
        // EventTrigger anims): their local transforms from the world's pose.
        for (int i = 0; i < sk.size(); ++i) {
            const std::string& n = sk.name(i);
            if (startsWith(n, "bone_") || startsWith(n, "spot_")) continue;
            const scene::Xfm x = world_.pose().local(sk.object(i));
            const scene::Xfm& cur = sk.local(i);
            if (x.pos != cur.pos || x.rot != cur.rot) sk.setLocal(i, x);
        }
        chr::RigContext ctx;
        ctx.beat = beats, ctx.dBeats = dB, ctx.dSeconds = dS, ctx.seconds = seconds;
        ctx.faceSongSeconds = faceSong, ctx.faceUiSeconds = faceUi;
        if (camera) ctx.hasCamera = true, ctx.camera = *camera;
        c.poll(ctx);
    }
}
