// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/band.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <stdexcept>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "script/globals.h"

namespace band {
namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }

uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0;
}

std::string strOf(const qb::Value* v) {
    return v && (v->type == qb::Type::String || v->type == qb::Type::WString) ? v->str : std::string();
}

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
    std::vector<Member> out;
    if (const qb::Value* d = g.find(K("disable_band")); intOf(d, 0) == 1) return out;
    const qb::Struct* ps = globalStruct(g, K("player1_status"));

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
        Member m = makeMember(g, Role::Guitarist, K("guitarist"), gp);
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
    const Pak global = Pak::load(disc, "DATA/COMPRESSED/ZONES/global.pak.xen");
    for (const PakEntry& e : global.entries()) {
        if (e.type != K(".ske")) continue;
        auto [p, size] = global.data(e);
        skeletons_.emplace(e.name, anim::parseSkeleton(p, size));
    }
    perm_ = readClips(Pak::load(disc, "DATA/COMPRESSED/PAK/perm_anims.pak.xen"));
}

const ClipMap& Library::songClips(const Disc& disc, const std::string& song) {
    auto it = songs_.find(song);
    if (it != songs_.end()) return it->second;
    ClipMap clips;
    const std::string path = "DATA/COMPRESSED/SONGS/" + song + "_song.pak.xen";
    if (!song.empty() && disc.exists(path)) {
        try {
            clips = readClips(Pak::load(disc, path));
        } catch (const std::exception&) {
            clips.clear();
        }
    }
    return songs_.emplace(song, std::move(clips)).first->second;
}

std::unique_ptr<Model> loadModel(const Disc& disc, const Globals& g, Library& lib, const Member& m, const std::string& song,
                                 std::string& error) {
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
            const Pak pak = Pak::load(disc, path);
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

}  // namespace band
