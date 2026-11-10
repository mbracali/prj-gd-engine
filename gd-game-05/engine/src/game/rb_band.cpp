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
#include "scene/milo.h"
#include "scene/objects.h"

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

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

// The members' motion [I]: the main driver keeps a clip of the "stand"
// group playing (another one as the last nears its end).
void Band::rbShotTargets(const std::vector<std::pair<std::string, std::string>>& groups, const std::vector<std::pair<std::string, scene::Xfm>>& moves) {
    for (const auto& [slot, group] : groups)
        if (Member* m = member(slot); m && m->character && m->shotGroup != group) {
            m->shotGroup = group;
            if (chr::ClipDriver* d = m->character->mainDriver())
                if (chr::ClipSet* set = d->clipSet(); set && set->group(group) && !over_) d->playGroup(group, 0);
        }
    for (const auto& [slot, to] : moves)
        if (Member* m = member(slot); m && m->character) m->character->teleport(to);
}

void Band::rbPoll(Member& m) {
    chr::ClipDriver* d = m.character->mainDriver();
    if (!d && !m.character->drivers().empty()) d = m.character->drivers().front().get();
    chr::ClipSet* set = d ? d->clipSet() : nullptr;
    if (!set) return;
    const auto& chain = d->chain();
    bool need = chain.empty();
    if (!need) {
        const auto& n = chain.back();
        if (n.clip) need = n.frame >= n.clip->endFrame - std::max(1.0f, n.clip->blendWidth);
    }
    if (!need) return;
    std::string group = over_ ? (won_ ? "win" : "lose") : m.shotGroup.empty() ? "stand" : m.shotGroup;
    if (!set->group(group)) group = "stand";
    if (!set->group(group)) group = "realtime_idle";
    if (set->group(group)) d->playGroup(group, 0);
}
