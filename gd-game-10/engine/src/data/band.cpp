// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/band.h"

#include <list>
#include <map>
#include <cstring>
#include <mutex>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <stdexcept>

#include "data/disc.h"
#include "data/inflate.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "script/globals.h"

#include <SDL3/SDL_log.h>

namespace band {
namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }

uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0;
}

std::string strOf(const qb::Value* v) {
    return v && (v->type == qb::Type::String || v->type == qb::Type::WString) ? v->str : std::string();
}

const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr; }

int intOf(const qb::Value* v, int fallback) {
    if (!v) return fallback;
    if (v->type == qb::Type::Int) return v->i;
    if (v->type == qb::Type::Float) return int(v->f[0]);
    return fallback;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const qb::Array* globalArray(const Globals& g, uint32_t name) {
    const qb::Value* v = g.find(name);
    return v && v->type == qb::Type::Array && v->arr ? v->arr.get() : nullptr;
}

const qb::Struct* globalStruct(const Globals& g, uint32_t name) {
    const qb::Value* v = g.find(name);
    return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr;
}

// A desc array's entry by desc_id (musician_body, musician_instrument; the
// download_* arrays are empty on the disc).
const qb::Struct* findDesc(const Globals& g, uint32_t array, uint32_t id) {
    if (!id) return nullptr;
    if (const qb::Array* a = globalArray(g, array))
        for (const qb::Value& e : a->values)
            if (e.type == qb::Type::Struct && e.st && keyOf(e.st->find(K("desc_id"))) == id) return e.st.get();
    return nullptr;
}

uint32_t profileField(const qb::Struct* p, const char* field) {
    if (!p) return 0;
    const qb::Value* v = p->find(K(field));
    if (v && v->type == qb::Type::Struct && v->st) return keyOf(v->st->find(K("desc_id")));
    return keyOf(v);
}

Member makeMember(const Globals& g, Role role, uint32_t object, const qb::Struct* profile) {
    (void)g;
    Member m;
    m.role = role;
    m.object = object;
    m.profile = profile;
    m.profileName = strOf(profile->find(K("name")));
    m.body = profileField(profile, "musician_body");
    m.instrument = profileField(profile, "musician_instrument");
    m.skeleton = keyOf(profile->find(K("skeleton")));
    // A uniform scale (create_guitarist / create_bassist assert uniformity) [C script].
    if (const qb::Value* s = profile->find(K("scale")); s && s->type == qb::Type::Vector) m.scale = s->f[0];
    return m;
}

ClipMap readClips(const Pak& pak) {
    ClipMap out;
    for (const PakEntry& e : pak.entries()) {
        if (e.type != K(".ska")) continue;
        auto [p, size] = pak.data(e);
        auto clip = std::make_shared<anim::Clip>(anim::parseClip(p, size));
        out.emplace(e.name, clip);
        if (e.fullName && e.fullName != e.name) out.emplace(e.fullName, clip);
    }
    return out;
}

}  // namespace

ClipMap readPakClips(const Pak& pak) { return readClips(pak); }

const char* roleName(Role r) {
    switch (r) {
    case Role::Guitarist: return "guitarist";
    case Role::Bassist: return "bassist";
    case Role::Drummer: return "drummer";
    case Role::Vocalist: return "vocalist";
    }
    return "?";
}

std::string pakPath(const std::string& qbPath) {
    const size_t slash = qbPath.find_last_of("\\/");
    return "DATA/COMPRESSED/PAK/" + (slash == std::string::npos ? qbPath : qbPath.substr(slash + 1)) + ".xen";
}

const qb::Struct* findProfile(const Globals& g, const std::string& name) {
    const std::string want = lower(name);
    if (const qb::Array* a = globalArray(g, K("musician_profiles")))
        for (const qb::Value& e : a->values)
            if (e.type == qb::Type::Struct && e.st && lower(strOf(e.st->find(K("name")))) == want) return e.st.get();
    return nullptr;
}

const qb::Struct* bodyDesc(const Globals& g, uint32_t id) { return findDesc(g, K("musician_body"), id); }
const qb::Struct* instrumentDesc(const Globals& g, uint32_t id) { return findDesc(g, K("musician_instrument"), id); }

std::vector<Member> chooseBand(const Globals& g, const std::string& zone, uint32_t songKey) {
    if (globalArray(g, K("Preset_Musician_Profiles_Locked"))) return chooseBandGh4(g, songKey);
    std::vector<Member> out;
    if (const qb::Value* d = g.find(K("disable_band")); intOf(d, 0) == 1) return out;
    // Two players (create_band, current_num_players = 2): Guitarist from the
    // lead player (player 1; co-op: player 2 when player 1 plays rhythm),
    // Bassist from the other, both through create_guitarist_profile; the
    // drummer; no vocalist [C scripts create_band, create_guitarist_profile].
    // A boss battle's player 2 is the boss's character_profile (start_gem_scroller).
    const bool two = intOf(g.find(K("current_num_players")), 1) == 2;
    bool p2Lead = false;
    if (two) {
        const uint32_t mode = keyOf(g.find(K("game_mode")));
        if (mode == K("p2_career") || mode == K("p2_coop"))
            if (const qb::Struct* p1 = globalStruct(g, K("player1_status")); p1 && keyOf(p1->find(K("part"))) != K("guitar")) p2Lead = true;
    }
    auto guitarist = [&](const qb::Struct* ps, Role role, uint32_t object) {

        // The guitarist: player1_status.character_id (find_profile_by_id: the
        // profile whose name's key is the id; else Axel), outfit, style ->
        // body 'Guitarist_%n_Outfit%o_Style%s'; the instrument from instrument_id
        // [C script create_guitarist_profile].
        const qb::Struct* gp = nullptr;
        const uint32_t charId = ps ? keyOf(ps->find(K("character_id"))) : 0;
        if (const qb::Array* a = globalArray(g, K("musician_profiles")))
            for (const qb::Value& e : a->values)
                if (e.type == qb::Type::Struct && e.st && qbkey::key(strOf(e.st->find(K("name")))) == charId) {
                    gp = e.st.get();
                    break;
                }
        if (!gp) gp = findProfile(g, "axel");
        if (gp) {
            Member m = makeMember(g, role, object, gp);
            m.outfit = std::max(1, ps ? intOf(ps->find(K("outfit")), 1) : 1);
            m.style = std::max(1, ps ? intOf(ps->find(K("style")), 1) : 1);
            char body[160];
            std::snprintf(body, sizeof body, "Guitarist_%s_Outfit%d_Style%d", m.profileName.c_str(), m.outfit, m.style);
            if (bodyDesc(g, qbkey::key(body))) m.body = qbkey::key(body);
            if (const uint32_t inst = ps ? keyOf(ps->find(K("instrument_id"))) : 0; inst && instrumentDesc(g, inst)) m.instrument = inst;
            if (intOf(g.find(K("cheat_airguitar")), 0) == 1) m.instrument = 0;
            // create_band_member: outfit 2 uses skeleton2 [C script].
            if (m.outfit == 2)
                if (const uint32_t s2 = keyOf(gp->find(K("skeleton2")))) m.skeleton = s2;
            out.push_back(m);
        }
    };
    if (two && zone != "z_credits") {
        guitarist(globalStruct(g, K(p2Lead ? "player2_status" : "player1_status")), Role::Guitarist, K("guitarist"));
        guitarist(globalStruct(g, K(p2Lead ? "player1_status" : "player2_status")), Role::Bassist, K("bassist"));
        if (const qb::Struct* p = findProfile(g, "drummer")) out.push_back(makeMember(g, Role::Drummer, K("drummer"), p));
        return out;
    }
    guitarist(globalStruct(g, K("player1_status")), Role::Guitarist, K("guitarist"));
    if (zone == "z_credits") return out;

    if (const qb::Struct* p = findProfile(g, "drummer")) out.push_back(makeMember(g, Role::Drummer, K("drummer"), p));

    const qb::Struct* songSt = nullptr;
    if (const qb::Struct* props = globalStruct(g, K("gh3_songlist_props")))
        if (const qb::Value* s = g.field(*props, songKey); s && s->type == qb::Type::Struct && s->st) songSt = s->st.get();
    std::string bassist = "bassist";
    if (songSt)
        if (const std::string b = strOf(songSt->find(K("bassist"))); !b.empty()) bassist = b;
    if (const qb::Struct* p = findProfile(g, bassist)) {
        Member m = makeMember(g, Role::Bassist, K("bassist"), p);
        // current_bass_model overrides the instrument when set [C script create_bassist].
        if (const uint32_t cb = keyOf(g.find(K("current_bass_model"))); cb && cb != K("none")) m.instrument = cb;
        out.push_back(m);
    }

    std::string singer = "singer";
    const bool bret = intOf(g.find(K("cheat_bretmichaels")), 0) == 1;
    bool none = false;
    if (songSt && songSt->find(K("singer"))) {
        const uint32_t s = keyOf(songSt->find(K("singer")));
        const bool alt = zone == "z_wikker" || zone == "z_budokan" || zone == "z_hell";
        none = s == K("none");
        singer = s == K("female") ? "singer_female" : s == K("bret") ? "singer_bret" : "singer";
        if (bret && s != K("female")) singer = "singer_bret";
        if (alt) singer += "_alt";
    } else if (bret) {
        singer = "singer_bret";
    }
    if (!none)
        if (const qb::Struct* p = findProfile(g, singer)) out.push_back(makeMember(g, Role::Vocalist, K("vocalist"), p));
    return out;
}

// ---- gh4 (World Tour engine) ----

namespace {

const qb::Struct* songStruct(const Globals& g, uint32_t songKey) {
    for (const char* name : {"gh_songlist_props", "gh3_songlist_props", "permanent_songlist_props"})
        if (const qb::Struct* props = globalStruct(g, K(name)))
            if (const qb::Value* s = g.field(*props, songKey); s && s->type == qb::Type::Struct && s->st) return s->st.get();
    return nullptr;
}

// A CAS part array's desc by desc_id (CAS_Full_Body, CAS_Guitar_Body, ...).
const qb::Struct* casDesc(const Globals& g, uint32_t partArray, uint32_t id) {
    if (!id || id == K("none")) return nullptr;
    if (const qb::Array* a = globalArray(g, partArray))
        for (const qb::Value& e : a->values)
            if (e.type == qb::Type::Struct && e.st && keyOf(g.field(*e.st, K("desc_id"))) == id) return e.st.get();
    return nullptr;
}

std::string meshOf(const Globals& g, const qb::Struct& desc) { return strOf(g.field(desc, K("mesh"))); }

// The instrument parts a part plays with [C data modifiable_preset_musician_instrument_parts; I: by part].
const std::vector<const char*>& instrumentParts(uint32_t part) {
    static const std::vector<const char*> guitar = {"CAS_Guitar_Body", "CAS_Guitar_Neck", "CAS_Guitar_Head", "CAS_Guitar_Pickguards",
                                                    "cas_guitar_bridges", "CAS_Guitar_Knobs", "CAS_Guitar_Pickups"};
    static const std::vector<const char*> bass = {"CAS_Bass_Body", "CAS_Bass_Neck", "CAS_Bass_Head", "CAS_Bass_Pickguards",
                                                  "cas_bass_bridges", "CAS_Bass_Knobs", "CAS_Bass_Pickups"};
    static const std::vector<const char*> drum = {"CAS_Drums", "CAS_Drums_Sticks"};
    static const std::vector<const char*> vocals = {"CAS_Mic", "CAS_Mic_Stand"};
    static const std::vector<const char*> none;
    if (part == K("guitar") || part == K("rhythm")) return guitar;
    if (part == K("bass")) return bass;
    if (part == K("drum")) return drum;
    if (part == K("vocals")) return vocals;
    return none;
}

bool isInstrumentPart(uint32_t key) {
    for (uint32_t p : {K("guitar"), K("bass"), K("drum"), K("vocals")})
        for (const char* n : instrumentParts(p))
            if (K(n) == key) return true;
    return false;
}

std::string pakOfName(const Disc* disc, const std::string& name) {
    (void)disc;
    return "DATA/COMPRESSED/PAK/" + name + ".pak.xen";
}

}  // namespace

const qb::Struct* findProfileGh4(const Globals& g, uint32_t id) {
    if (!id) return nullptr;
    for (const char* arr : {"Preset_Musician_Profiles_Locked", "Preset_Musician_Profiles_Modifiable"})
        if (const qb::Array* a = globalArray(g, K(arr)))
            for (const qb::Value& e : a->values)
                if (e.type == qb::Type::Struct && e.st && keyOf(e.st->find(K("name"))) == id) return e.st.get();
    return nullptr;
}

bool memberGh4(const Globals& g, const qb::Struct& profile, uint32_t object, uint32_t part, uint32_t songKey, Member& m) {
    const qb::Struct* app = structOf(profile.find(K("appearance")));
    if (!app) return false;
    m = Member{};
    m.gh4 = true;
    m.object = object;
    m.part = part;
    m.profile = &profile;
    const uint32_t nameKey = keyOf(profile.find(K("name")));
    m.profileName = qbkey::names().get(nameKey);
    m.role = part == K("bass") ? Role::Bassist : part == K("drum") ? Role::Drummer : part == K("vocals") ? Role::Vocalist : Role::Guitarist;
    if (object == K("bassist")) m.role = Role::Bassist;
    if (object == K("drummer")) m.role = Role::Drummer;
    if (object == K("vocalist")) m.role = Role::Vocalist;
    // The body: CAS_Full_Body (the talent: one mesh with its skeleton), else
    // every body part with a mesh [C data car_rocker_full_body.qb, scripts
    // get_body_key_from_appearance].
    const qb::Struct* body = nullptr;
    if (const qb::Struct* fb = structOf(app->find(K("CAS_Full_Body")))) body = casDesc(g, K("CAS_Full_Body"), keyOf(fb->find(K("desc_id"))));
    bool female = false;
    std::string skeleton;
    if (body) {
        m.meshes.push_back(meshOf(g, *body));
        skeleton = qbkey::names().get(keyOf(body->find(K("skeleton"))));
        female = intOf(body->find(K("is_female")), 0) == 1;
    } else {
        if (const qb::Struct* ph = structOf(app->find(K("cas_physique")))) female = keyOf(ph->find(K("desc_id"))) == K("FemalePhysique");
        skeleton = female ? "GH_Rocker_Female" : "GH_Rocker_Male";
        for (const qb::Item& it : app->items) {
            if (!it.name || isInstrumentPart(it.name) || it.value.type != qb::Type::Struct || !it.value.st) continue;
            if (const qb::Struct* d = casDesc(g, it.name, keyOf(it.value.st->find(K("desc_id")))))
                if (std::string mesh = meshOf(g, *d); !mesh.empty()) m.meshes.push_back(mesh);
        }
    }
    if (m.meshes.empty() || m.meshes.front().empty()) return false;
    // The skeleton: the body's, as the global pak keeps it (`<name>_original`;
    // a drummer's `<name>_Drummer`) [C data ZONES/global.pak; I: the accessory
    // bones merged on top at run time are not added].
    m.skeleton = 0;
    if (m.role == Role::Drummer) m.skeleton = qbkey::key(skeleton + "_Drummer");
    m.body = qbkey::key(skeleton + "_original");
    if (!m.skeleton) m.skeleton = m.body;
    // The instrument: the appearance's parts for the part, song_specific_instrument_data.<song>.<name> over them [C data].
    std::unordered_map<uint32_t, const qb::Struct*> parts;
    for (const qb::Item& it : app->items)
        if (it.name && it.value.type == qb::Type::Struct && it.value.st) parts[it.name] = it.value.st.get();
    const qb::Struct* songSt = songKey ? songStruct(g, songKey) : nullptr;
    if (const qb::Struct* ssid = globalStruct(g, K("song_specific_instrument_data")))
        if (const qb::Struct* forSong = structOf(g.field(*ssid, songKey)))
            if (const qb::Struct* forMe = structOf(g.field(*forSong, nameKey)))
                for (const qb::Item& it : g.expand(*forMe))
                    if (it.name && it.value.type == qb::Type::Struct && it.value.st) parts[it.name] = it.value.st.get();
    for (const char* n : instrumentParts(part)) {
        auto it = parts.find(K(n));
        if (it == parts.end()) continue;
        if (const qb::Struct* d = casDesc(g, K(n), keyOf(it->second->find(K("desc_id")))))
            if (std::string mesh = meshOf(g, *d); !mesh.empty()) m.meshes.push_back(mesh);
    }
    // The anims: the body's anim_struct, the song's version of it
    // (<struct>_<song name>, its _alt when the song uses alt anim structs)
    // and its member for the part (bass / rhythm drop to guitar) [C script
    // get_anim_struct_member].
    const uint32_t animStruct = body ? keyOf(body->find(K("anim_struct"))) : K(female ? "car_female_anim_struct" : "car_male_anim_struct");
    const std::string asName = qbkey::names().get(animStruct);
    const uint32_t member = part ? part : K("frontend");
    auto pick = [&](const qb::Struct* st) -> const qb::Struct* {
        if (!st) return nullptr;
        if (const qb::Struct* x = structOf(st->find(member))) return x;
        if (member == K("bass") || member == K("rhythm")) return structOf(st->find(K("guitar")));
        return nullptr;
    };
    if (songSt && part) {
        const std::string songName = strOf(songSt->find(K("name")));
        uint32_t k = qbkey::key(asName + "_" + songName);
        if (intOf(songSt->find(K("use_alt_anim_structs")), 0) == 1 && globalStruct(g, qbkey::key(asName + "_" + songName + "_alt")))
            k = qbkey::key(asName + "_" + songName + "_alt");
        m.animPart = pick(globalStruct(g, k));
        if (const std::string mocap = strOf(songSt->find(K("mocap_anim_pak"))); !mocap.empty()) m.animPaks.push_back(pakPath(mocap));
    }
    if (!m.animPart) m.animPart = pick(globalStruct(g, animStruct));
    if (m.animPart)
        if (const uint32_t pk = keyOf(m.animPart->find(K("pak")))) m.animPaks.push_back(pakOfName(nullptr, qbkey::names().get(pk)));
    return true;
}

std::vector<Member> chooseBandGh4(const Globals& g, uint32_t songKey) {
    std::vector<Member> out;
    if (!globalArray(g, K("Preset_Musician_Profiles_Locked"))) return out;
    if (const qb::Value* d = g.find(K("disable_band")); intOf(d, 0) == 1) return out;
    // band_builder_get_band_global / get_band_name: the song's band (cheats
    // not read), else default_band (random characters: none drawn) [C scripts].
    const qb::Struct* songSt = songStruct(g, songKey);
    const qb::Struct* band = nullptr;
    if (songSt)
        if (const uint32_t b = keyOf(songSt->find(K("band")))) band = globalStruct(g, b);
    if (!band) band = globalStruct(g, K("default_band"));
    if (!band) return out;
    // band_builder_add_players: player 1's character on its part (guitar
    // unless drums / vocals); band_builder_add_cpu_characters: the drummer,
    // the vocalist (James sings with a rhythm guitar, Lemmy with a bass), the
    // bassist, the guitarist [C scripts].
    const qb::Struct* p1 = globalStruct(g, K("player1_status"));
    uint32_t p1Part = p1 ? keyOf(p1->find(K("part"))) : K("guitar");
    if (p1Part != K("drum") && p1Part != K("vocals") && p1Part != K("bass")) p1Part = K("guitar");
    uint32_t p1Char = p1 ? keyOf(p1->find(K("character_id"))) : 0;
    auto bandOf = [&](const char* role) { return keyOf(band->find(K(role))); };
    struct Want {
        uint32_t object, part, id;
    };
    std::vector<Want> want;
    const uint32_t partRole = p1Part == K("guitar") ? K("guitarist") : p1Part == K("bass") ? K("bassist") : p1Part == K("drum") ? K("drummer")
                                                                                                                               : K("vocalist");
    if (!findProfileGh4(g, p1Char)) p1Char = bandOf(p1Part == K("guitar") ? "guitarist" : p1Part == K("bass") ? "bassist"
                                                       : p1Part == K("drum")                    ? "drummer"
                                                                                                : "vocalist");
    want.push_back({partRole, p1Part, p1Char});
    auto has = [&](uint32_t object) {
        for (const Want& w : want)
            if (w.object == object) return true;
        return false;
    };
    if (!has(K("drummer"))) want.push_back({K("drummer"), K("drum"), bandOf("drummer")});
    if (!has(K("vocalist"))) {
        const uint32_t v = bandOf("vocalist");
        uint32_t real = K("vocals");
        if (v == K("James") || v == K("James_ALT") || v == K("James_ZomB")) real = K("rhythm");
        else if (v == K("lemmy")) real = K("bass");
        want.push_back({K("vocalist"), real, v});
    }
    if (!has(K("bassist"))) want.push_back({K("bassist"), K("bass"), bandOf("bassist")});
    if (!has(K("guitarist"))) want.push_back({K("guitarist"), K("guitar"), bandOf("guitarist")});
    // RandomCharacter: band_builder_new_random_character makes a random CAS
    // appearance (cas_set_random_appearance, by genre); the port takes a
    // Preset_Musician_Profiles_Modifiable rocker allowed on the part (not a
    // Metallica member), picked by the song and the part, each once
    // [C script; I: the presets stand in for the randomizer, GD10-020].
    std::vector<uint32_t> used;
    auto randomFor = [&](uint32_t part) -> const qb::Struct* {
        const qb::Array* a = globalArray(g, K("Preset_Musician_Profiles_Modifiable"));
        if (!a) return nullptr;
        std::vector<const qb::Struct*> c;
        for (const qb::Value& e : a->values) {
            const qb::Struct* p = structOf(&e);
            if (!p) continue;
            const uint32_t n = keyOf(p->find(K("name")));
            const std::string nm = lower(qbkey::names().get(n));
            if (nm.rfind("james", 0) == 0 || nm.rfind("rob", 0) == 0 || nm.rfind("kirk", 0) == 0 || nm.rfind("larsulrich", 0) == 0) continue;
            if (std::find(used.begin(), used.end(), n) != used.end()) continue;
            bool ok = false;
            if (const qb::Value* ap = p->find(K("allowed_parts")); ap && ap->type == qb::Type::Array && ap->arr)
                for (const qb::Value& x : ap->arr->values) ok = ok || x.key == (part == K("rhythm") ? K("guitar") : part);
            const qb::Struct* app = structOf(p->find(K("appearance")));
            if (ok && app && !app->items.empty()) c.push_back(p);
        }
        if (c.empty()) return nullptr;
        uint32_t h = songKey ^ (part * 2654435761u);
        h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
        return c[h % c.size()];
    };
    for (const Want& w : want) {
        const qb::Struct* prof = findProfileGh4(g, w.id);
        if (!prof && w.id == K("RandomCharacter")) prof = randomFor(w.part);
        if (!prof) continue;
        used.push_back(keyOf(prof->find(K("name"))));
        Member m;
        if (memberGh4(g, *prof, w.object, w.part, songKey, m)) out.push_back(std::move(m));
    }
    return out;
}

Member memberOf(const Globals& g, uint32_t object, const qb::Struct& profile) {
    const uint32_t type = keyOf(profile.find(K("type")));
    const Role role = object == K("bassist") ? Role::Bassist : object == K("drummer") ? Role::Drummer
                      : object == K("vocalist")                                     ? Role::Vocalist
                      : object == K("guitarist")                                    ? Role::Guitarist
                      : type == K("bassist")                                        ? Role::Bassist
                      : type == K("drummer")                                        ? Role::Drummer
                      : type == K("vocalist")                                       ? Role::Vocalist
                                                                                    : Role::Guitarist;
    Member m = makeMember(g, role, object, &profile);
    m.outfit = std::max(1, intOf(profile.find(K("outfit")), 1));
    // create_band_member: outfit 2 uses skeleton2 [C script].
    if (m.outfit == 2)
        if (const uint32_t s2 = keyOf(profile.find(K("skeleton2")))) m.skeleton = s2;
    if (m.body && !bodyDesc(g, m.body)) m.body = 0;
    if (m.instrument && !instrumentDesc(g, m.instrument)) m.instrument = 0;
    return m;
}

std::vector<Member> everyBody(const Globals& g) {
    std::vector<Member> out;
    const qb::Array* profiles = globalArray(g, K("musician_profiles"));
    if (!profiles) return out;
    for (const qb::Value& e : profiles->values) {
        if (e.type != qb::Type::Struct || !e.st) continue;
        const qb::Struct* p = e.st.get();
        const uint32_t type = keyOf(p->find(K("type")));
        const Role role = type == K("bassist") ? Role::Bassist : type == K("drummer") ? Role::Drummer
                          : type == K("vocalist")                      ? Role::Vocalist
                                                                       : Role::Guitarist;
        Member base = makeMember(g, role, role == Role::Guitarist ? K("guitarist") : role == Role::Bassist ? K("bassist")
                                                                   : role == Role::Drummer                ? K("drummer")
                                                                                                          : K("vocalist"),
                                 p);
        std::vector<uint32_t> seen;
        auto add = [&](Member m) {
            if (!m.body || std::find(seen.begin(), seen.end(), m.body) != seen.end()) return;
            seen.push_back(m.body);
            out.push_back(m);
        };
        int outfits = 1;
        if (const qb::Value* o = p->find(K("outfits")); o && o->type == qb::Type::Array && o->arr) outfits = int(o->arr->values.size());
        for (int o = 1; o <= std::max(1, outfits); ++o)
            for (int s = 1; s <= 4; ++s) {
                char body[160];
                std::snprintf(body, sizeof body, "Guitarist_%s_Outfit%d_Style%d", base.profileName.c_str(), o, s);
                if (!bodyDesc(g, qbkey::key(body))) continue;
                Member m = base;
                m.body = qbkey::key(body);
                m.outfit = o, m.style = s;
                if (o == 2)
                    if (const uint32_t s2 = keyOf(p->find(K("skeleton2")))) m.skeleton = s2;
                add(m);
            }
        add(base);
    }
    return out;
}

std::vector<Member> everyInstrument(const Globals& g) {
    std::vector<Member> out;
    const qb::Array* a = globalArray(g, K("musician_instrument"));
    if (!a) return out;
    const qb::Struct* guitarist = findProfile(g, "axel");
    const qb::Struct* bassist = findProfile(g, "bassist");
    const qb::Struct* drummer = findProfile(g, "drummer");
    const qb::Struct* singer = findProfile(g, "singer");
    const qb::Struct* female = findProfile(g, "singer_female");
    for (const qb::Value& e : a->values) {
        if (e.type != qb::Type::Struct || !e.st) continue;
        const uint32_t id = keyOf(e.st->find(K("desc_id")));
        const uint32_t type = keyOf(e.st->find(K("type")));
        const std::string name = lower(qbkey::names().get(id));
        const qb::Struct* p = type == K("guitar") ? guitarist : type == K("bass") ? bassist
                              : name.find("drum") != std::string::npos ? drummer
                              : name.find("fem") != std::string::npos  ? female
                              : name.find("mic") != std::string::npos  ? singer
                                                                       : guitarist;
        if (!p || !id) continue;
        const uint32_t ptype = keyOf(p->find(K("type")));
        const Role role = ptype == K("bassist") ? Role::Bassist : ptype == K("drummer") ? Role::Drummer
                          : ptype == K("vocalist")                      ? Role::Vocalist
                                                                        : Role::Guitarist;
        Member m = makeMember(g, role, K(roleName(role)), p);
        m.instrument = id;
        out.push_back(m);
    }
    return out;
}

void Library::load(const Disc& disc) {
    const auto globalPak = Pak::shared(disc, "DATA/COMPRESSED/ZONES/global.pak.xen");
    const Pak& global = *globalPak;
    for (const PakEntry& e : global.entries()) {
        if (e.type != K(".ske")) continue;
        auto [p, size] = global.data(e);
        try {
            skeletons_.emplace(e.name, anim::parseSkeleton(p, size));
        } catch (const std::exception& ex) {
            SDL_Log("band: skeleton %s: %s", qbkey::names().get(e.name).c_str(), ex.what());
        }
    }
    // gh4: perm_anims_in_game.pak (the hands' strums, frets and chords) [C data].
    const char* perm = "DATA/COMPRESSED/PAK/perm_anims.pak.xen";
    if (!disc.exists(perm)) perm = "DATA/COMPRESSED/PAK/perm_anims_in_game.pak.xen";
    if (disc.exists(perm)) perm_ = readClips(*Pak::shared(disc, perm));
}

std::shared_ptr<Library> Library::shared(const Disc& disc) {
    static std::mutex mutex;
    static const Disc* forDisc = nullptr;
    static std::shared_ptr<Library> lib;
    std::lock_guard<std::mutex> lock(mutex);
    if (lib && forDisc == &disc) return lib;
    auto l = std::make_shared<Library>();
    l->load(disc);
    lib = l, forDisc = &disc;
    return lib;
}

const ClipMap& Library::songClips(const Disc& disc, const std::string& song) {
    std::lock_guard<std::mutex> lock(songsMutex_);
    auto it = songs_.find(song);
    if (it != songs_.end()) return it->second;
    ClipMap clips;
    const std::string path = "DATA/COMPRESSED/SONGS/" + song + "_song.pak.xen";
    if (!song.empty() && disc.exists(path)) {
        try {
            clips = readClips(*Pak::shared(disc, path));
        } catch (const std::exception&) {
            clips.clear();
        }
    }
    return songs_.emplace(song, std::move(clips)).first->second;
}

namespace {

// The CAS archive paks (cas_archive_paks: DATA/PAK/CAS/<name>.pak.xen, not
// compressed): their entry tables, read once (a few KB each), so that a
// part's .skin and its parent .tex are read alone (the paks hold 375 MB)
// [C data cas_paks.qb; I: the engine's lookup is by name across them].
struct CasEntry {
    std::string pak;
    uint64_t at = 0;  // file offset of the data
    uint32_t size = 0, type = 0, parent = 0;
};
class CasArchive {
public:
    static CasArchive& shared(const Disc& disc) {
        static std::mutex m;
        static std::map<const Disc*, std::unique_ptr<CasArchive>> all;
        std::lock_guard<std::mutex> lock(m);
        auto& a = all[&disc];
        if (!a) {
            a = std::make_unique<CasArchive>();
            a->index(disc);
        }
        return *a;
    }
    // The .skin (by its short name: the mesh path's base name) or the .tex (by full name key).
    const CasEntry* skin(const std::string& meshPath) const {
        std::string base = lower(meshPath);
        if (const size_t sl = base.find_last_of("\\/"); sl != std::string::npos) base = base.substr(sl + 1);
        if (const size_t dot = base.find('.'); dot != std::string::npos) base = base.substr(0, dot);
        auto it = skins_.find(qbkey::key(base));
        return it == skins_.end() ? nullptr : &it->second;
    }
    const CasEntry* byFull(uint32_t full) const {
        auto it = full_.find(full);
        return it == full_.end() ? nullptr : &it->second;
    }
    // Every .tex of the archive by full name (the guitar finishes).
    const std::unordered_map<uint32_t, CasEntry>& all() const { return full_; }
    static Bytes read(const Disc& disc, const CasEntry& e) {
        Bytes b(e.size);
        disc.readRange(e.pak, e.at, b.data(), b.size());
        if (b.size() >= 16 && std::memcmp(b.data(), "GZ\0\0", 4) == 0) {
            const uint32_t want = uint32_t(b[4]) << 24 | uint32_t(b[5]) << 16 | uint32_t(b[6]) << 8 | b[7];
            Bytes out = inflateRaw(b.data() + 16, b.size() - 16);
            if (out.size() != want) throw std::runtime_error(e.pak + ": GZ entry inflates to the wrong size");
            return out;
        }
        return b;
    }

private:
    void index(const Disc& disc) {
        const qb::Value* dummy = nullptr;
        (void)dummy;
        static const char* const paks[] = {"cas_full_parts", "cas_globalpak", "cas_female_bodypak", "cas_male_bodypak", "cas_female_accpak", "cas_male_accpak",
                                           "cas_female_torsopak", "cas_male_torsopak", "cas_female_legspak", "cas_male_legspak",
                                           "cas_female_shoespak", "cas_male_shoespak", "cas_guitarpak", "cas_guitarpak_finishes_0",
                                           "cas_guitarpak_finishes_1", "cas_guitarpak_finishes_2", "cas_guitarpak_nh", "cas_basspak",
                                           "cas_basspak_finishes_0", "cas_basspak_finishes_1", "cas_basspak_finishes_2", "cas_basspak_nh",
                                           "cas_drumspak", "cas_vocalspak", "cas_preset_texswaps", "cas_acc_bones", "cas_deform"};
        for (const char* name : paks) {
            const std::string path = std::string("DATA/PAK/CAS/") + name + ".pak.xen";
            if (!disc.exists(path)) continue;
            const uint64_t fileSize = disc.size(path).value_or(0);
            Bytes head;
            uint64_t pos = 0;
            bool ended = false;
            while (!ended && pos + 32 <= fileSize) {
                if (pos + 32 > head.size()) {
                    const size_t chunk = size_t(std::min<uint64_t>(4096, fileSize - head.size()));
                    const size_t old = head.size();
                    head.resize(old + chunk);
                    disc.readRange(path, old, head.data() + old, chunk);
                    continue;
                }
                auto u = [&](size_t o) {
                    return uint32_t(head[o]) << 24 | uint32_t(head[o + 1]) << 16 | uint32_t(head[o + 2]) << 8 | head[o + 3];
                };
                const size_t o = size_t(pos);
                const uint32_t type = u(o);
                if (type == K(".last") || type == K("last")) break;
                CasEntry e;
                e.pak = path;
                e.at = pos + u(o + 4);
                e.size = u(o + 8);
                e.type = type;
                e.parent = u(o + 24);
                const uint32_t full = u(o + 16), shortName = u(o + 20);
                if (e.at + e.size <= fileSize) {
                    full_.emplace(full, e);
                    if (type == K(".skin")) skins_.emplace(shortName, e);
                }
                pos += 32;
            }
        }
    }
    std::unordered_map<uint32_t, CasEntry> skins_, full_;
};

std::unique_ptr<Model> loadModelGh4(const Disc& disc, Library& lib, const Member& m, std::string& error) {
    auto model = std::make_unique<Model>();
    model->member = m;
    model->skeleton = lib.skeleton(m.skeleton);
    if (!model->skeleton) model->skeleton = lib.skeleton(m.body);
    if (!model->skeleton) model->skeleton = lib.skeleton(K("GH_Rocker_Male_original"));
    const std::string who = m.profileName.empty() ? std::string(roleName(m.role)) : m.profileName;
    if (!model->skeleton) {
        error = who + ": no skeleton " + qbkey::names().get(m.body);
        return nullptr;
    }
    const CasArchive& cas = CasArchive::shared(disc);
    std::vector<uint32_t> texDone;
    for (const std::string& mesh : m.meshes) {
        const CasEntry* e = cas.skin(mesh);
        if (!e) {
            if (mesh == m.meshes.front()) {
                error = who + ": " + mesh + " is not in the CAS paks";
                return nullptr;
            }
            SDL_Log("band: %s: %s is not in the CAS paks", who.c_str(), mesh.c_str());
            continue;
        }
        try {
            const Bytes b = CasArchive::read(disc, *e);
            Part part;
            part.pak = e->pak;
            part.scene = scene::parse(b.data(), b.size(), scene::Kind::Skin);
            model->parts.push_back(std::move(part));
            if (e->parent && std::find(texDone.begin(), texDone.end(), e->parent) == texDone.end())
                if (const CasEntry* t = cas.byFull(e->parent)) {
                    texDone.push_back(e->parent);
                    const Bytes tb = CasArchive::read(disc, *t);
                    for (gh3tex::Texture& tx : gh3tex::decodeTexDict(tb.data(), tb.size()))
                        if (tx.supported && !tx.levels.empty()) model->textures.emplace(tx.key, std::move(tx));
                }
        } catch (const std::exception& ex) {
            if (mesh == m.meshes.front()) {
                error = who + ": " + mesh + ": " + ex.what();
                return nullptr;
            }
            SDL_Log("band: %s: %s: %s", who.c_str(), mesh.c_str(), ex.what());
        }
    }
    for (const std::string& path : m.animPaks) {
        if (!disc.exists(path)) {
            SDL_Log("band: %s: %s is not on the disc", who.c_str(), path.c_str());
            continue;
        }
        try {
            for (auto& [k, c] : readClips(*Pak::shared(disc, path))) model->clips.emplace(k, c);
        } catch (const std::exception& ex) {
            SDL_Log("band: %s: %s: %s", who.c_str(), path.c_str(), ex.what());
        }
    }
    for (const auto& [k, c] : lib.permClips()) model->clips.emplace(k, c);
    return model;
}

std::unique_ptr<Model> loadModelUncached(const Disc& disc, const Globals& g, Library& lib, const Member& m, const std::string& song,
                                         std::string& error);
}

namespace {
// The models loaded before (the 8 last: character select scrolls back and
// forth, a song loads the menu's guitarist again), most recent first.
// Each part loaded took about 0.2 s (owner 1.7: character select's stutters).
struct Cached {
    const Disc* disc;
    uint32_t body, instrument, skeleton;
    int outfit, style;
    std::string song;
    std::shared_ptr<const Model> model;
    std::vector<std::string> meshes, animPaks;  // gh4
};
std::mutex cacheMutex;
std::list<Cached> modelCache;
bool sameModel(const Cached& c, const Disc& disc, const Member& m, const std::string& song) {
    return c.disc == &disc && c.body == m.body && c.instrument == m.instrument && c.skeleton == m.skeleton && c.outfit == m.outfit &&
           c.style == m.style && c.song == song && c.meshes == m.meshes && c.animPaks == m.animPaks;
}
}  // namespace

std::unique_ptr<Model> cachedModel(const Disc& disc, const Member& m, const std::string& song) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    for (auto it = modelCache.begin(); it != modelCache.end(); ++it)
        if (sameModel(*it, disc, m, song)) {
            modelCache.splice(modelCache.begin(), modelCache, it);
            auto copy = std::make_unique<Model>(*modelCache.front().model);
            copy->member = m;
            return copy;
        }
    return nullptr;
}

// A copy of a model loaded before, else loaded (and kept).
std::unique_ptr<Model> loadModel(const Disc& disc, const Globals& g, Library& lib, const Member& m, const std::string& song,
                                 std::string& error) {
    if (auto copy = cachedModel(disc, m, song)) return copy;
    auto model = loadModelUncached(disc, g, lib, m, song, error);
    if (!model) return model;
    std::lock_guard<std::mutex> lock(cacheMutex);
    for (const Cached& c : modelCache)
        if (sameModel(c, disc, m, song)) return model;  // another thread loaded it meanwhile
    modelCache.push_front(Cached{&disc, m.body, m.instrument, m.skeleton, m.outfit, m.style, song, std::make_shared<const Model>(*model), m.meshes,
                                 m.animPaks});
    if (modelCache.size() > 8) modelCache.pop_back();
    return model;
}

namespace {
std::unique_ptr<Model> loadModelUncached(const Disc& disc, const Globals& g, Library& lib, const Member& m, const std::string& song,
                                         std::string& error) {
    if (m.gh4) return loadModelGh4(disc, lib, m, error);
    (void)g;
    auto model = std::make_unique<Model>();
    model->member = m;
    model->skeleton = lib.skeleton(m.skeleton);
    const std::string who = m.profileName.empty() ? std::string(roleName(m.role)) : m.profileName;
    if (!model->skeleton) {
        error = who + ": no skeleton " + qbkey::names().get(m.skeleton);
        return nullptr;
    }
    auto loadPak = [&](const std::string& path, bool skins, bool clips) -> bool {
        if (!disc.exists(path)) {
            error = who + ": " + path + " is not on the disc";
            return false;
        }
        try {
            const auto pakShared = Pak::shared(disc, path);
            const Pak& pak = *pakShared;
            for (const PakEntry& e : pak.entries()) {
                auto [p, size] = pak.data(e);
                if (skins && e.type == K(".skin")) {
                    Part part;
                    part.pak = path;
                    part.scene = scene::parse(p, size, scene::Kind::Skin);
                    model->parts.push_back(std::move(part));
                } else if (skins && e.type == K(".tex")) {
                    for (gh3tex::Texture& t : gh3tex::decodeTexDict(p, size))
                        if (t.supported && !t.levels.empty()) model->textures.emplace(t.key, std::move(t));
                }
            }
            if (clips)
                for (auto& [k, c] : readClips(pak)) model->clips.emplace(k, c);
        } catch (const std::exception& ex) {
            error = who + ": " + path + ": " + ex.what();
            return false;
        }
        return true;
    };
    // No body: an instrument alone (the store's display guitar,
    // store_do_guitar_load) [C script].
    if (m.body || !m.instrument) {
        const qb::Struct* body = bodyDesc(g, m.body);
        if (!body) {
            error = who + ": no musician_body " + qbkey::names().get(m.body);
            return nullptr;
        }
        if (!loadPak(pakPath(strOf(body->find(K("pak")))), true, true)) return nullptr;
        if (model->parts.empty()) {
            error = who + ": the body pak has no skin";
            return nullptr;
        }
        if (const std::string anims = strOf(body->find(K("anims_pak"))); !anims.empty() && !loadPak(pakPath(anims), false, true))
            return nullptr;
    }
    if (m.instrument) {
        const qb::Struct* inst = instrumentDesc(g, m.instrument);
        if (!inst) {
            error = who + ": no musician_instrument " + qbkey::names().get(m.instrument);
            return nullptr;
        }
        if (!loadPak(pakPath(strOf(inst->find(K("pak")))), true, false)) return nullptr;
    }
    for (const auto& [k, c] : lib.permClips()) model->clips.emplace(k, c);
    for (const auto& [k, c] : lib.songClips(disc, song)) model->clips.emplace(k, c);
    return model;
}
}  // namespace

}  // namespace band
