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
    std::vector<float> interactProbs;
};

// ---- script objects --------------------------------------------------------

// A band member: its script type (char_objects.dta) and Character /
// GuitarCharacter natives.
class Band::CharObj : public ScriptObject {
public:
    CharObj(Band& b, Member& m, const std::string& cls) : ScriptObject(m.slot, cls), b_(b), m_(m) { nameScope = m.slot; }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        chr::Character& c = *m_.character;
        if (m_.type == "guitarist" && b_.guitarMessage(m_, msg, args, r)) return true;
        if (msg == "get" || msg == "set" || msg == "has" || msg == "name") return false;
        // Character::Handle 0x82152108.
        if (msg == "teleport") {
            if (!args.empty()) {
                const Value w = in.evalValue(args[0]);
                const std::string n = text(in, w);
                if (scene::Object* o = b_.world_.venue().find(n)) c.teleport(b_.world_.pose().world(o));
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
        auto excitement = [&]() -> std::pair<bool, int> {
            ScriptObject* g = in.findObject("game");
            int e = 0;
            if (g) {
                Value v = in.send(g, "get", {Value::symbol("excitement")}, false);
                if (v.isNumber()) e = v.integerValue();
            }
            return {g != nullptr, e};
        };
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
    PlayerObj(Band& b, int p) : ScriptObject("band_player" + std::to_string(p), "Player"), b_(b), p_(p) {}
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
    clips_ = std::make_unique<chr::ClipLibrary>(data);
    assemble(data, song);
    makeScripts(objects);
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
    m.expr->randomInt = [this](int lo, int hi) { return world_.interp().randomInt(lo, hi); };
    m.expr->randomFloat = [this](float lo, float hi) { return world_.interp().randomFloat(lo, hi); };
    try {
        const std::string fac = facefx::resolveFacePath(data_, m.character->path(), s->faceGraph);
        std::vector<std::string> entries;
        for (const auto& e : s->entries) entries.push_back(e.node);
        m.lip->setup(facefx::loadFaceGraph(data_, fac), facefx::loadServoVisemes(data_, s->header.type), entries);
        if (s->header.type != "singer" && data_.exists(facefx::kBlinkTrackPath))
            m.lip->play(std::make_shared<facefx::Anim>(facefx::loadAnim(data_, facefx::kBlinkTrackPath)));
    } catch (const std::exception& e) {
        SDL_Log("band: %s lip.servo: %s", m.slot.c_str(), e.what());
    }
}

void Band::pollFaces() {
    Interp& in = world_.interp();
    const float song = float(in.tasks().time(kTaskSeconds));
    const float ui = float(in.tasks().time(kTaskUISeconds));
    ScriptObject* g = in.findObject("game");
    int excitement = 0;
    if (g) {
        Value v = in.send(g, "get", {Value::symbol("excitement")}, false);
        if (v.isNumber()) excitement = v.integerValue();
    }
    for (auto& mem : members_) {
        if (!mem->lip || !mem->expr || !mem->character || !mem->character->visible) continue;
        mem->expr->update(song, g != nullptr, excitement, *mem->lip);
        facefx::FacePose pose;
        mem->lip->poll(song, ui, nullptr, 0, pose);
    }
}

void Band::loadSong(const MidiFile& midi, const Song* song) {
    Interp& in = world_.interp();
    std::vector<std::string> names = {"bassist_parser", "drummer_parser", "singer_parser", "keyboard_parser"};
    for (int p = 0; p < config_.players && p < 2; ++p)
        for (const char* k : {"_fret", "_strum", "_fret_pos", "_parser"}) names.push_back("player" + std::to_string(p) + k);
    if (config_.bass[0] || config_.bass[1]) names.push_back("bass_strum");
    parsers_ = std::make_unique<MidiParserSet>(in, data_, names);
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
    for (auto& m : members_) {
        if (m->type == "guitarist") guitarEnter(*m);
        in.send(m->script.get(), "enter", {}, false);
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
    in.var("dude") = Value::obj(m.script.get());
    for (const DataNode& n : it->second.children) in.eval(n);
    in.var("dude") = saved;
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
    if (!d || actuallyWalking(m.slot)) return;
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
    // 0x822D2780.
    if (actuallyWalking(m.slot) || flagWeight(m.slot, kStarPower) > 0) return;
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
    const bool post = config_.players > 1;  // or an encore follows a win (0x822CA6E0) [C]; no encores here
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
        // 0x822D1F80: the game-over group, no blend.
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
    if (!d || actuallyWalking(m.slot)) return;
    if (!d->settled()) {
        // An interact queued too close to its start in Star Power is dropped.
        const auto& head = d->chain().back();
        const float x = head.delay - 4.0f;
        if (0 <= x && x < dBeats && (head.clip->flags & kInteract)) d->dropUnstarted();
    }
    if (d->settled()) {
        const std::string g = currentGroup(m);
        uint32_t flags = chr::kPlayLast;
        if (g == "idle" && !d->chain().empty() && (d->chain().back().flags & 0xf600) == chr::kPlayRealTime) flags = 0x204;
        // [M9.6] interacts: interact_probs[excitement] at the nearest interact waypoint.
        d->playGroup(g, flags);
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
        // 0x822D28E8: a singer (or keyboardist) within 72 units: to the nearest walk spot.
        if (Member* s = member("singer")) {
            const auto a = m.character->xfm().pos, b = s->character->xfm().pos;
            const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
            if (dx * dx + dy * dy + dz * dz < 72.0f * 72.0f) {
                const scene::Waypoint* best = nullptr;
                float bd = 1e30f;
                for (const scene::Waypoint* w : world_.venue().all<scene::Waypoint>()) {
                    if (!(w->flags & 0x40)) continue;
                    const auto p = world_.pose().world(w).pos;
                    const float d = (p[0] - a[0]) * (p[0] - a[0]) + (p[1] - a[1]) * (p[1] - a[1]) + (p[2] - a[2]) * (p[2] - a[2]);
                    if (d < bd) bd = d, best = w;
                }
                if (best) m.character->teleport(world_.pose().world(best));
            }
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
    // 0x822D25A8: unless a one-shot is blending, a task plays the next
    // star_power clip at once with kPlayFirst (0x822E4AF8 / 0x822E49D8; it
    // ends when that clip is no longer the first playing).
    for (auto& m : members_) {
        if (m->type != "guitarist" || m->player != player) continue;
        chr::ClipDriver* d = m->character->mainDriver();
        if (!d || !d->clipSet() || d->flagWeight(kSpecial) != 0) continue;
        if (scene::CharClipGroup* g = d->clipSet()->group("star_power"))
            if (scene::CharClipSamples* c = d->clipSet()->next(*g)) d->playClip(c, chr::kPlayFirst);
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

bool Band::actuallyWalking(const std::string&) const { return false; }

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
    seconds_ = seconds, beat_ = beats;
    if (parsers_ && entered_) parsers_->poll(beats);
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
        if (camera) ctx.hasCamera = true, ctx.camera = *camera;
        c.poll(ctx);
    }
    if (entered_) pollFaces();
}
