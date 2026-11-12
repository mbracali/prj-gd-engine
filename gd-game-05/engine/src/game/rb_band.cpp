// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Rock Band's band on stage (GD05-008, wave 02): the venue's four band
// slots filled with the disc's premade characters, each assembled from its
// outfit pieces, and kept moving with the base character's clips.
//
// [C data] what the disc says:
// - the slots: CompositeCharacters player_<guitar|bass|drum|vocals><n> in
//   world/<kind>/gen/<kind>_char.milo (docs/FINDINGS.md "Venues");
// - the characters: config/gen/characters.dtb (prefab (guitar0 ...)
//   (drum0 ...) (vocals0 ...)): gender, and an (outfit ...) naming one
//   piece per category (hair, heads, eyes, torso, legs, feet, ..., guitar,
//   bass, drum, mic) or none;
// - the base character: char/<gender>/gen/<gender>_<guitar|drum|vocals>.milo
//   (a bassist is the guitar one), with the skeleton, drivers and clips;
// - the pieces: char/<gender>/outfits/<category>/gen/<piece>.milo (its
//   geometry in the subdir milo it names, chr::Character::loadOutfit), the
//   instruments in char/instruments/<guitar|bass|mic>/gen/<piece>.milo and
//   the venue's drum kit in char/instruments/drum/gen/<kind>_drums_<piece>.milo.
//
// [I] what the port chooses: which premade character plays in Quickplay
// (one per instrument by the song's name; RB's rule is not read), and
// the animation (the main driver's "stand" group, a new clip as the last
// ends; RB's BandDirector and the per-part clip choice are not read).
#include <SDL3/SDL.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "data/dta.h"
#include "data/game_data.h"
#include "game/band.h"
#include "game/song_db.h"
#include "game/world.h"
#include "char/char_objects.h"
#include "char/rb_midi_parser.h"
#include "data/midi.h"
#include "script/script.h"
#include "scene/milo.h"
#include "scene/objects.h"

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// config/macros.dta, system/run/band/band_macros.dta [C data].
constexpr uint32_t kLeftArm = 1, kRightArm = 2;
constexpr uint32_t kGenreRocker = 256, kGenreDramatic = 512, kGenreBanger = 1024, kGenreSpazz = 2048;
constexpr uint32_t kPlayAndAttitudeFlags = 524032;
constexpr uint32_t kValidGuitar = 1048576, kValidBass = 2097152;
constexpr uint32_t kPlayIdleRealtime = 4096, kPlayIdle = 8192, kPlayIdleIntense = 16384, kPlayMellow = 32768, kPlayNormal = 65536,
                   kPlayIntense = 131072, kPlaySolo = 262144;
constexpr uint32_t kPlayModes = 0x7F000;  // RB 0x822E5A68 clears these before set_play
constexpr uint32_t kPlayRealTime = 0x200;

// The (prefab ...) block of characters.dtb: the full one (the file keeps a
// MINIMAL_OUTFITS variant too, a shorter list of one look).
const DataNode* prefabBlock(const DataNode& root) {
    const DataNode* best = nullptr;
    std::function<void(const DataNode&)> walk = [&](const DataNode& n) {
        for (const DataNode& c : n.children) {
            if (c.isList() && !c.children.empty() && c.children[0].isText() && c.children[0].s == "prefab") {
                if (!best || c.children.size() > best->children.size()) best = &c;
            }
        }
    };
    walk(root);
    return best;
}

std::string text(const DataNode* n, size_t i = 1) {
    return n && n->children.size() > i && n->children[i].isText() ? n->children[i].s : std::string();
}

}  // namespace

std::vector<std::string> Band::rbPrefabs(const GameData& data, const std::string& instrument) {
    std::vector<std::string> out;
    try {
        static DataNode root;
        static bool loaded = false;
        if (!loaded) root = parseDtb(data.read("config/gen/characters.dtb")), loaded = true;
        if (const DataNode* p = prefabBlock(root))
            for (size_t i = 1; i < p->children.size(); ++i)
                if (const std::string name = text(&p->children[i], 0); startsWith(name, instrument.c_str())) out.push_back(name);
    } catch (const std::exception& e) {
        SDL_Log("band: characters.dtb: %s", e.what());
    }
    return out;
}

bool Band::assembleRb(const GameData& data, const SongInfo& song) {
    std::map<std::string, const scene::CharacterObj*> slots;
    world_.venue().forEach([&](scene::Object& o) {
        auto* c = o.as<scene::CharacterObj>();
        if (c && !c->own && startsWith(c->name, "player_")) slots.emplace(c->name, c);
    });
    if (slots.empty()) return false;
    DataNode root;
    try {
        root = parseDtb(data.read("config/gen/characters.dtb"));
    } catch (const std::exception& e) {
        problems_.push_back(std::string("characters.dtb: ") + e.what());
        return true;
    }
    const DataNode* prefabs = prefabBlock(root);
    if (!prefabs) {
        problems_.push_back("characters.dtb: no prefab block");
        return true;
    }
    // The venue's kind (arena_07 -> arena) for the drum kit.
    std::string kind = venueName_;
    if (const size_t u = kind.rfind('_'); u != std::string::npos) kind = kind.substr(0, u);
    uint32_t h = 2166136261u;
    for (char ch : song.id) h = (h ^ uint8_t(ch)) * 16777619u;
    for (const char* inst : {"guitar", "bass", "drum", "vocals"}) {
        const std::string instrument = inst;
        auto it = slots.find("player_" + instrument + "0");
        if (it == slots.end()) {
            problems_.push_back("no slot player_" + instrument + "0 in " + venueName_);
            continue;
        }
        // The premade characters for the instrument (a bassist: a guitarist's look).
        const std::string pool = instrument == "bass" ? "guitar" : instrument;
        std::vector<const DataNode*> cands;
        for (size_t i = 1; i < prefabs->children.size(); ++i) {
            const DataNode& c = prefabs->children[i];
            if (c.isList() && startsWith(text(&c, 0), pool.c_str())) cands.push_back(&c);
        }
        if (cands.empty()) continue;
        const std::string want = config_.rbPrefab.count(instrument) ? config_.rbPrefab.at(instrument) : std::string();
        const DataNode* pre = cands[(h + (instrument == "bass" ? 7u : 0u)) % cands.size()];
        for (const DataNode* c : cands)
            if (!want.empty() && text(c, 0) == want) pre = c;
        const Config::RbLook* look = config_.rbLooks.count(instrument) ? &config_.rbLooks.at(instrument) : nullptr;
        const std::string gender = look ? look->gender : text(pre->find("gender")).empty() ? "male" : text(pre->find("gender"));
        const std::string baseInst = instrument == "bass" ? "guitar" : instrument;
        const std::string base = "char/" + gender + "/gen/" + gender + "_" + baseInst + ".milo_xbox";
        std::map<std::string, std::string> outfits;
        int order = 0;
        auto add = [&](const std::string& path) {
            if (data.exists(path)) {
                char key[16];
                SDL_snprintf(key, sizeof key, "+%02d", order++);
                outfits[key] = path;
            } else {
                problems_.push_back(text(pre, 0) + ": no " + path);
            }
        };
        std::vector<std::pair<std::string, std::string>> pieces;
        if (look) {
            for (const auto& [cat, name] : look->outfit) pieces.emplace_back(cat, name);
        } else if (const DataNode* o = pre->find("outfit")) {
            for (size_t i = 1; i < o->children.size(); ++i) pieces.emplace_back(text(&o->children[i], 0), text(&o->children[i], 1));
        }
        {
            for (const auto& [cat, name] : pieces) {
                if (cat.empty() || name.empty() || name == "none") continue;
                if (cat == "guitar" || cat == "bass" || cat == "mic" || cat == "drum") {
                    // The member's own instrument only (a guitarist's bass stays in the closet).
                    const bool mine = (cat == "guitar" && instrument == "guitar") || (cat == "bass" && instrument == "bass") ||
                                      (cat == "mic" && instrument == "vocals") || (cat == "drum" && instrument == "drum");
                    if (!mine) continue;
                    if (cat == "drum") add("char/instruments/drum/gen/" + kind + "_drums_" + name + ".milo_xbox");
                    else add("char/instruments/" + cat + "/gen/" + name + ".milo_xbox");
                    continue;
                }
                add("char/" + gender + "/outfits/" + cat + "/gen/" + name + ".milo_xbox");
            }
        }
        auto m = std::make_unique<Member>();
        m->slot = it->first;
        m->miloPath = base;
        m->object = it->second;
        m->type = "rb_" + instrument;
        try {
            m->character = std::make_unique<chr::Character>(data, base, *clips_, outfits);
        } catch (const std::exception& e) {
            problems_.push_back(m->slot + ": " + e.what());
            continue;
        }
        rbSkin(data, *m->character, pre, look ? look->skin : -1);
        m->character->setXfm(slotXfm(*m));
        if (!it->second->rnd.environment.empty() && it->second->dir)
            m->character->slotEnviron = it->second->dir->resolve<scene::Environ>(it->second->rnd.environment);
        if (log) {
            SDL_Log("band: %s <- %s (%s, %d pieces)", m->slot.c_str(), look ? "the player's own character" : text(pre, 0).c_str(), base.c_str(), int(outfits.size()));
            for (const std::string& b : m->character->unresolvedBones()) SDL_Log("band:   unresolved bone %s", b.c_str());
        }
        rbPrefabUsed_[instrument] = text(pre, 0);
        // The play flags' constant part: the type's clip_flags (char_objects.dta
        // CompositeCharacter guitar: kValidGuitar, as a bassist kValidBass;
        // drum / vocal 0) and the character's attitude (characters.dtb
        // (attitude kGenre*)).
        {
            uint32_t att = kGenreRocker;
            const std::string a = text(pre->find("attitude"));
            if (a == "kGenreDramatic") att = kGenreDramatic;
            else if (a == "kGenreBanger") att = kGenreBanger;
            else if (a == "kGenreSpazz") att = kGenreSpazz;
            m->rbFlags = att | (instrument == "guitar" ? kValidGuitar : instrument == "bass" ? kValidBass : 0u);
        }
        members_.push_back(std::move(m));
    }
    world_.band = this;
    return true;
}

// The skin [C data: char/gen/skin.milo skin.pal; the heads' and bodies'
// diffuse textures are empty placeholders the game renders at run time
// (PatchRenderer / TexRenderer: the skin tone, tattoos and make-up)].
// The port has no render-to-texture for them yet (GD05-022): every
// material whose diffuse texture is such a placeholder is drawn in the
// prefab's skin colour (skin_color_index) instead [I].
void Band::rbSkin(const GameData& data, chr::Character& c, const DataNode* prefab, int skinIndex) {
    static std::vector<std::array<float, 4>> skin;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            auto milo = scene::loadMilo(data, "char/gen/skin.milo_xbox");
            if (auto* pal = milo->root().find<scene::ColorPalette>("skin.pal")) skin = pal->colors;
        } catch (const std::exception& e) {
            SDL_Log("band: skin.milo: %s", e.what());
        }
    }
    int idx = 0;
    if (const DataNode* n = prefab->find("skin_color_index"); n && n->children.size() > 1) idx = n->intAt(1);
    if (skinIndex >= 0) idx = skinIndex;
    std::array<float, 4> tone{0.8f, 0.62f, 0.5f, 1};
    if (idx >= 0 && size_t(idx) < skin.size()) tone = skin[size_t(idx)];
    c.forEachObject([&](scene::Object& o) {
        auto* mat = o.as<scene::Mat>();
        if (!mat || mat->diffuseTex.empty() || !mat->dir) return;
        const scene::Tex* t = mat->dir->resolve<scene::Tex>(mat->diffuseTex);
        if (t && !t->bitmap.levels.empty() && t->width > 8) return;
        mat->diffuseTex.clear();
        // Skin materials take the tone; the patch overlays (tattoos, logos,
        // stickers: alpha-blended) show nothing without a patch; other
        // clothes whose texture the port has not set: a dark cloth [I].
        const bool skinMat = mat->name.find("skin") != std::string::npos || mat->name.find("naked") != std::string::npos;
        if (skinMat) mat->color = {tone[0], tone[1], tone[2], mat->color[3]};
        else if (mat->blend == 3) mat->color[3] = 0;
        else mat->color = {0.22f, 0.22f, 0.24f, mat->color[3]};
        if (const char* red = SDL_getenv("GD05_SKINRED"); red && skinMat) mat->color = {1, 0, 0, 1};
        if (SDL_getenv("GD05_SKINLOG"))
            SDL_Log("skin %s: normal %s spec %s custom %s emissive %s env %s twoColor %d mask %s color2 %.2f %.2f %.2f ppl %d useEnv %d blend %d",
                    mat->name.c_str(), mat->normalMap.c_str(), mat->specularMap.c_str(), mat->customDiffuseMap.c_str(), mat->emissiveMap.c_str(),
                    mat->environMap.c_str(), int(mat->twoColor), mat->twoColorMask.c_str(), mat->color2[0], mat->color2[1], mat->color2[2],
                    int(mat->perPixelLit), int(mat->useEnviron), mat->blend);
    });
}

// ---- the band at run time ----------------------------------------------------
//
// Rock Band moves its band with the song's MIDI (char_objects.dta, the
// CompositeCharacter types guitar / drum / vocal, and config/midi_parsers.dta)
// [C data unless noted]:
// - the character's parser (guitarist_parser, bassist_parser, drummer_parser,
//   vocal_parser: the part's [idle] / [play] / [mellow] / [intense] /
//   [play_solo] / [idle_realtime] text events, a beat early) changes the play
//   mode; the main driver plays the start group's (stand, the drummer's sit)
//   clip whose flags hold the mode, the attitude and the instrument
//   (RB 0x822E5908 / 0x822E4A80 / CharClipGroup FindClip 0x8231BF10);
// - each CharDriverMidi (fret, strum, string_hi / _low, face_accent; the
//   drummer's kick, snare, hands, feet, toms, cymbals, ride, hihat) hears its
//   parser and plays the named clip, or a clip of the named group, starting
//   `length` before its first frame and blending in over that length, so the
//   hit lands on the note (RB 0x82342168);
// - fret.ikmidi takes the fret hand's spot along the neck (new_spot);
// - the weight setters (left_hand.weight = 1 - main.drv's kLeftArm weight
//   ...) hand the arms between the main clips and the hand drivers / IK.

namespace {

// An object's (key value) property (a CharDriverMidi's (parser drums_kick)).
std::string headerProp(const scene::ObjectHeader& h, const std::string& key) {
    if (!h.hasProps) return {};
    const auto& kids = h.props.children;
    for (size_t i = 0; i < kids.size(); ++i) {
        const auto& c = kids[i];
        if (c.children.size() > 1 && c.children[0].str == key) return c.children[1].str;
        if (c.str == key && i + 1 < kids.size()) return kids[i + 1].str;
    }
    return {};
}

// char_objects.dta CharWeightSetter drummer_play get_hand_weight [C data].
float drumHandWeight(const std::string& g) {
    static const std::map<std::string, float> w = {
        {"hihat_fast", 0.8f}, {"snare_fast", 0.5f}, {"cymbal_r_grab", 0.0f}, {"cymbal_l_grab", 0.0f}, {"cymbal_l_crash", 0.5f},
        {"cymbal_l_crash_big", 0.2f}, {"cymbal_r_crash", 0.5f}, {"cymbal_l_crash_fast", 0.5f}, {"floortom", 0.5f}, {"floortom_fast", 0.5f},
        {"cymbal_l_side", 0.2f}, {"cymbal_l_side_fast", 0.1f}, {"cymbal_r_side", 0.2f}, {"cymbal_r_side_fast", 0.1f}, {"ride", 0.65f},
        {"ride_fast", 0.45f}, {"ride_side_l", 0.25f}, {"ride_side_r", 0.25f}, {"cymbal_l", 0.5f}, {"cymbal_r", 0.5f}, {"tom_l", 0.75f},
        {"tom_r", 0.75f}, {"tom_r_fast", 0.5f}, {"tom_l_fast", 0.5f}};
    auto it = w.find(g);
    return it != w.end() ? it->second : 1.0f;
}

}  // namespace

// A parser's sink: the character itself or one of its objects.
class Band::RbPart : public ScriptObject {
public:
    enum Kind { kChar, kDriverMidi, kIkMidi, kDrumPlay, kLookat, kSingalong };
    RbPart(Band& band, Member& m, Kind kind, std::string object)
        : ScriptObject(m.slot + ":" + object, "RbPart"), band_(band), m_(m), kind_(kind), object_(std::move(object)) {}

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        auto val = [&](size_t k) { return k < args.size() ? in.evalValue(args[k]) : Value::integer(0); };
        auto str = [&](size_t k) {
            const Value v = val(k);
            return v.isText() ? v.s : v.type == Value::Type::Object && v.object ? v.object->name() : v.str();
        };
        auto num = [&](size_t k) {
            const Value v = val(k);
            return v.isNumber() ? v.number() : 0.0f;
        };
        r = Value::integer(0);
        chr::Character& c = *m_.character;
        switch (kind_) {
        case kChar: {
            // CHAR_COMMON's play modes (char_objects.dta) [C data].
            static const std::map<std::string, uint32_t> modes = {{"idle_realtime", kPlayIdleRealtime}, {"idle", kPlayIdle},
                                                                  {"idle_intense", kPlayIdleIntense}, {"mellow", kPlayMellow},
                                                                  {"play", kPlayNormal},           {"intense", kPlayIntense},
                                                                  {"play_solo", kPlaySolo}};
            if (auto it = modes.find(msg); it != modes.end()) {
                band_.rbChangePlay(m_, it->second);
                return true;
            }
            return false;
        }
        case kDriverMidi: {
            chr::ClipDriver* d = c.driver(object_);
            chr::ClipSet* set = d ? d->clipSet() : nullptr;
            if (!set) return true;
            if (msg == "midi_parser") {
                // RB 0x82342168: the clip named (a group: its next clip [I]).
                const std::string name = str(0);
                scene::CharClipSamples* clip = set->clip(name);
                if (!clip)
                    if (scene::CharClipGroup* g = set->group(name)) clip = set->next(*g);
                if (!clip) {
                    if (band_.log) SDL_Log("band: %s %s: no clip %s", m_.slot.c_str(), object_.c_str(), name.c_str());
                    return true;
                }
                band_.rbDriverClip(m_, object_, clip, num(1));
                return true;
            }
            if (msg == "strum" || msg == "drum_lhand" || msg == "drum_rhand") {
                // CharDriverMidi musician strum (char_objects.dta): the group's clip
                // with the character's play and attitude flags, else its next.
                scene::CharClipGroup* g = set->group(str(0));
                if (!g) return true;
                scene::CharClipSamples* clip = set->nextMatching(*g, m_.rbFlags & kPlayAndAttitudeFlags);
                if (!clip) clip = set->next(*g);
                if (!clip) return true;
                float length = num(1);
                if (clip->playFlags & kPlayRealTime) {
                    const Value p = in.var("mp.parser");
                    if (p.type == Value::Type::Object && p.object) length = in.send(p.object, "beat_to_sec_length", {Value::real(length)}, false).number();
                }
                band_.rbDriverClip(m_, object_, clip, length);
                return true;
            }
            return false;
        }
        case kIkMidi:
            if (msg == "new_spot") {
                c.rig().newSpot(object_, str(0), num(1));
                return true;
            }
            return false;
        case kDrumPlay:
            if (msg == "drum_lhand" || msg == "drum_rhand") {
                (msg == "drum_lhand" ? lhand_ : rhand_) = drumHandWeight(str(0));
                c.rig().setSetter(object_, "base_weight", lhand_ * rhand_);
                return true;
            }
            return false;
        case kLookat:
            if (msg == "lookat_in" || msg == "lookat_out") {
                c.rig().setSetter(object_, "base_weight", msg == "lookat_in" ? 1.0f : 0.0f);
                return true;
            }
            return false;
        case kSingalong:
            if (msg == "set" && str(0) == "weight") {
                c.rig().setWeight(object_, num(1));
                return true;
            }
            return false;
        }
        return false;
    }

private:
    Band& band_;
    Member& m_;
    Kind kind_;
    std::string object_;
    float lhand_ = 1, rhand_ = 1;
};

void Band::rbLoadSong(const MidiFile& midi, const Song* song) {
    song_ = song;
    // Each member's sinks and the parsers they hear (world_configure,
    // the musician types' (parser ...), CompositeCharacter (parser ...)).
    struct Hook {
        Member* m;
        RbPart::Kind kind;
        std::string object, parser;
    };
    std::vector<Hook> hooks;
    for (auto& mp : members_) {
        Member& m = *mp;
        const std::string inst = m.type.substr(3);  // guitar / bass / drum / vocals
        const scene::Directory& d = m.character->dir();
        const std::string pre = inst == "bass" ? "bass" : "guitar";
        hooks.push_back({&m, RbPart::kChar, "character",
                         inst == "guitar" ? "guitarist_parser" : inst == "bass" ? "bassist_parser" : inst == "drum" ? "drummer_parser" : "vocal_parser"});
        for (const auto& o : d.objects) {
            if (auto* dm = o->as<scene::CharDriverMidi>()) {
                std::string parser = headerProp(dm->header, "parser");
                if (inst == "guitar" || inst == "bass") {
                    // world_configure: the guitar's or the bass's parsers.
                    if (o->name == "fret.dmidi") parser = pre + "_fret";
                    else if (o->name == "strum.dmidi") parser = pre + "_strum";
                    else if (o->name == "face_accent.dmidi") parser = pre + "_face_accent";
                    else if (o->name == "string_hi.dmidi") parser = pre + "_string_hi";
                    else if (o->name == "string_low.dmidi") parser = pre + "_string_low";
                }
                if (!parser.empty()) hooks.push_back({&m, RbPart::kDriverMidi, o->name, parser});
            } else if (o->as<scene::CharIKMidi>()) {
                std::string parser = headerProp(o->header, "parser");
                if (inst == "guitar" || inst == "bass") parser = pre + "_fret_pos";
                if (!parser.empty()) hooks.push_back({&m, RbPart::kIkMidi, o->name, parser});
            } else if (o->as<scene::CharWeightSetter>()) {
                const std::string& t = o->header.type;
                if (t == "drummer_play") {
                    hooks.push_back({&m, RbPart::kDrumPlay, o->name, "drums_left_hand"});
                    hooks.push_back({&m, RbPart::kDrumPlay, o->name, "drums_right_hand"});
                } else if (t == "guitar_lookat" && inst == "guitar") {
                    hooks.push_back({&m, RbPart::kLookat, o->name, "guitar_streak_parser"});
                } else if (t == "singalong") {
                    std::string parser = headerProp(o->header, "parser");
                    if (inst == "guitar" || inst == "bass") parser = pre + "_singalong_parser";
                    if (!parser.empty()) hooks.push_back({&m, RbPart::kSingalong, o->name, parser});
                }
            }
        }
    }
    std::vector<std::string> names;
    for (const Hook& h : hooks)
        if (std::find(names.begin(), names.end(), h.parser) == names.end()) names.push_back(h.parser);
    rbParsers_ = std::make_unique<RbMidiParserSet>(world_.interp(), data_, names);
    rbParsers_->load(midi, song);
    // One sink per (member, object); the drummer_play setter hears two parsers.
    for (const Hook& h : hooks) {
        RbMidiParser* p = rbParsers_->find(h.parser);
        if (!p) {
            problems_.push_back(h.m->slot + ": no parser " + h.parser);
            continue;
        }
        RbPart* part = nullptr;
        for (auto& existing : h.m->rbParts)
            if (existing->name() == h.m->slot + ":" + h.object) part = static_cast<RbPart*>(existing.get());
        if (!part) {
            h.m->rbParts.push_back(std::make_unique<RbPart>(*this, *h.m, h.kind, h.object));
            part = static_cast<RbPart*>(h.m->rbParts.back().get());
        }
        p->addSink(part);
        if (log) SDL_Log("band: %s %s <- %s (%zu events)", h.m->slot.c_str(), h.object.c_str(), h.parser.c_str(), p->events().size());
    }
}

void Band::rbEnter(Member& m) {
    chr::Character& c = *m.character;
    const std::string inst = m.type.substr(3);
    m.rbPlaying = true;
    m.rbCurPlay = 0;
    // CHAR_ENTER_COMMON: every driver's groups shuffled (randomize_groups).
    for (const auto& d : c.drivers())
        if (chr::ClipSet* set = d->clipSet()) set->shuffleGroups(rng_);
    if (inst == "guitar" || inst == "bass") {
        // CompositeCharacter guitar enter: the hands' setters start at 0, at once.
        for (const char* w : {"left_hand.weight", "right_hand.weight"}) {
            c.rig().setSetter(w, "beats_per_weight", 0);
            c.rig().setWeight(w, 0);
        }
        // world_configure: as a bassist, zBassOffset.drv plays offset (the
        // guitar held as a bass) [C data].
        if (inst == "bass")
            if (chr::ClipDriver* z = c.driver("zBassOffset.drv"); z && z->clipSet())
                if (scene::CharClipSamples* off = z->clipSet()->clip("offset")) z->playClip(off, 0);
    } else if (inst == "drum") {
        // drum enter: the sticks (singlestick.grp in a song), the hands ready.
        c.setDrawOnly("sticks.grp", "singlestick.grp");
        for (const char* h : {"right_hand.dmidi", "left_hand.dmidi"})
            if (chr::ClipDriver* d = c.driver(h); d && d->clipSet())
                if (scene::CharClipSamples* ready = d->clipSet()->clip("ready")) d->playClip(ready, 0);
    } else if (inst == "vocals") {
        // vocal enter: set_instrument mic.
        c.setDrawOnly("hands.grp", "mic.grp");
        c.setShowing("mic_trans.grp", true);
    }
    // CharDriverMidi musician enter: a parser with an idle message plays its
    // clip (a group's first) as the inactive clip.
    for (auto& part : m.rbParts) {
        const std::string obj = part->name().substr(m.slot.size() + 1);
        chr::ClipDriver* d = c.driver(obj);
        chr::ClipSet* set = d ? d->clipSet() : nullptr;
        if (!set || !rbParsers_) continue;
        for (const auto& p : rbParsers_->parsers()) {
            if (std::find(p->sinks().begin(), p->sinks().end(), part.get()) == p->sinks().end()) continue;
            const std::string idle = p->getProp(world_.interp(), "idle_message").str();
            if (idle.empty() || idle == "0") continue;
            scene::CharClipSamples* clip = set->clip(idle);
            if (!clip)
                if (scene::CharClipGroup* g = set->group(idle); g && !g->clips.empty()) clip = set->clip(g->clips.front());
            if (clip) d->playClip(clip, 0);
        }
    }
    // set_play kPlayIdleRealtime, change_group start_group.
    m.rbFlags = (m.rbFlags & ~kPlayModes) | kPlayIdleRealtime;
    rbChangeGroup(m, inst == "drum" ? "sit" : "stand", m.rbFlags, true);
}

// change_play (char_objects.dta CHAR_COMMON) [C data].
void Band::rbChangePlay(Member& m, uint32_t mode) {
    m.rbCurPlay = mode;
    if (!m.rbPlaying) {
        if (mode == kPlayIntense || mode == kPlaySolo) mode = kPlayIdleIntense;
        else if (mode == kPlayMellow || mode == kPlayNormal) mode = kPlayIdle;
    }
    if (over_) return;
    // set_play (RB 0x822E5A68): the mode bits replaced.
    rbChangeGroup(m, m.rbGroup, (m.rbFlags & ~kPlayModes) | mode, false);
}

// RB 0x822E5908 [C]: a new group or flags; the drummer plays with main_add.drv
// in his sit group while the mode is mellow .. solo; a new clip when forced,
// when the driver changed or when the playing clip lacks the flags.
void Band::rbChangeGroup(Member& m, const std::string& group, uint32_t flags, bool force) {
    if (group != m.rbGroup) m.rbGroup = group, force = true;
    m.rbFlags = flags;
    chr::Character& c = *m.character;
    chr::ClipDriver* prev = m.rbActive;
    chr::ClipDriver* add = c.driver("main_add.drv");
    m.rbActive = add && add->clipSet() && group == "sit" && (flags & 0x78000) ? add : c.mainDriver();
    if (!force && m.rbActive) {
        const scene::CharClipSamples* cur = m.rbActive->firstPlaying();
        if (m.rbActive != prev || !cur || (cur->flags & flags) != flags) force = true;
    }
    if (force) rbPlayGroup(m);
}

// RB 0x822E4A80 [C]: the group's clip with every play flag (FindClip, round
// robin from the current one); with main_add.drv the other driver plays still.
void Band::rbPlayGroup(Member& m) {
    chr::Character& c = *m.character;
    chr::ClipDriver* d = m.rbActive;
    chr::ClipSet* set = d ? d->clipSet() : nullptr;
    scene::CharClipGroup* g = set ? set->group(m.rbGroup) : nullptr;
    if (!g) return;
    scene::CharClipSamples* clip = set->nextMatching(*g, m.rbFlags);
    // [I] no clip has every flag (a part the set lacks): the mode alone.
    if (!clip) clip = set->nextMatching(*g, m.rbFlags & kPlayModes);
    if (!clip) clip = set->next(*g);
    if (!clip) return;
    if (chr::ClipDriver* add = c.driver("main_add.drv"); add && add->clipSet()) {
        chr::ClipDriver* other = d == c.mainDriver() ? add : c.mainDriver();
        if (other && other->clipSet())
            if (scene::CharClipSamples* still = other->clipSet()->clip("still")) other->playClip(still, 0);
    }
    d->playClip(clip, 1);  // kPlayNow
    if (onClip) onClip(beat_, m.slot, clip->name);
    if (log) SDL_Log("band: %8.2f %s %s %s (flags %#x)", beat_, m.slot.c_str(), d->name().c_str(), clip->name.c_str(), m.rbFlags);
}

// CharDriverMidi midi_parser (RB 0x82342168) [C]: the clip starts `length`
// before its first frame (seconds x its rate for a realtime clip) and
// blends in over that length.
bool Band::rbDriverClip(Member& m, const std::string& driver, scene::CharClipSamples* clip, float length) {
    chr::ClipDriver* d = m.character->driver(driver);
    if (!d || !clip) return false;
    float frames = length;
    if (clip->playFlags & kPlayRealTime) frames *= clip->framesPerSec;
    if (frames < 0) frames = 0;
    d->playAt(clip, 0, clip->startFrame - frames, 0, frames);
    if (log) SDL_Log("band: %8.2f %s %s %s over %.2f", beat_, m.slot.c_str(), driver.c_str(), clip->name.c_str(), frames);
    return true;
}

// Each frame: a new clip of the group as the playing one ends [I: RB's
// clips loop through their own transitions; the next clip of the group
// keeps the variety the port had].
void Band::rbPoll(Member& m) {
    chr::ClipDriver* d = m.rbActive;
    if (!d || !d->clipSet()) return;
    if (over_ && m.rbGroup != (won_ ? "win" : "lose")) {
        if (chr::ClipSet* set = m.character->mainDriver() ? m.character->mainDriver()->clipSet() : nullptr; set && set->group(won_ ? "win" : "lose")) {
            m.rbActive = m.character->mainDriver();
            m.rbGroup = won_ ? "win" : "lose";
            rbPlayGroup(m);
        }
        return;
    }
    const auto& chain = d->chain();
    bool need = chain.empty();
    if (!need) {
        const auto& n = chain.back();
        if (n.clip) need = n.frame >= n.clip->endFrame - std::max(1.0f, n.clip->blendWidth);
    }
    if (need) rbPlayGroup(m);
}

// Camera shots' band targets (BandCamShot start RB 0x822D8108).
void Band::rbShotTargets(const std::vector<std::pair<std::string, std::string>>& groups, const std::vector<std::pair<std::string, scene::Xfm>>& moves) {
    for (const auto& [slot, group] : groups)
        if (Member* m = member(slot); m && m->character && m->shotGroup != group) {
            m->shotGroup = group;
            if (chr::ClipDriver* d = m->character->mainDriver())
                if (chr::ClipSet* set = d->clipSet(); set && set->group(group) && !over_) {
                    m->rbActive = d;
                    m->rbGroup = group;
                    rbPlayGroup(*m);
                }
        }
    for (const auto& [slot, to] : moves)
        if (Member* m = member(slot); m && m->character) m->character->teleport(to);
}
