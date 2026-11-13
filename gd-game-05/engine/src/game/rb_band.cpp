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
#include <cmath>
#include <set>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "data/dta.h"
#include "data/game_data.h"
#include "game/band.h"
#include "char/deform.h"
#include "game/song_db.h"
#include "game/world.h"
#include "char/char_objects.h"
#include "char/rb_midi_parser.h"
#include "data/midi.h"
#include "game/chart.h"
#include "script/script.h"
#include "scene/milo.h"
#include "scene/objects.h"
#include "scene/ui_objects.h"

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
        std::string want = config_.rbPrefab.count(instrument) ? config_.rbPrefab.at(instrument) : std::string();
        // Development aid: GD05_PREFAB_<INSTRUMENT>=<premade> (GD05_PREFAB_VOCALS=vocals2) picks the premade.
        std::string envName = "GD05_PREFAB_" + instrument;
        for (char& ch : envName) ch = char(std::toupper(uint8_t(ch)));
        if (const char* e = SDL_getenv(envName.c_str()); e && *e) want = e;
        const DataNode* pre = cands[(h + (instrument == "bass" ? 7u : 0u)) % cands.size()];
        for (const DataNode* c : cands)
            if (!want.empty() && text(c, 0) == want) pre = c;
        const Config::RbLook* look = config_.rbLooks.count(instrument) ? &config_.rbLooks.at(instrument) : nullptr;
        const std::string gender = look ? look->gender : text(pre->find("gender")).empty() ? "male" : text(pre->find("gender"));
        const std::string baseInst = instrument == "bass" ? "guitar" : instrument;
        const std::string base = "char/" + gender + "/gen/" + gender + "_" + baseInst + ".milo_xbox";
        std::vector<std::pair<std::string, std::string>> pieces;
        if (look) {
            for (const auto& [cat, name] : look->outfit) pieces.emplace_back(cat, name);
        } else if (const DataNode* o = pre->find("outfit")) {
            for (size_t i = 1; i < o->children.size(); ++i) pieces.emplace_back(text(&o->children[i], 0), text(&o->children[i], 1));
        }
        std::vector<std::string> missing;
        const std::map<std::string, std::string> outfits = rbOutfitMilos(data, gender, instrument, pieces, kind, &missing);
        for (const std::string& path : missing) problems_.push_back(text(pre, 0) + ": no " + path);
        auto m = std::make_unique<Member>();
        m->slot = it->first;
        m->miloPath = base;
        m->object = it->second;
        m->type = "rb_" + instrument;
        for (const auto& [k, path] : outfits) m->pieces.push_back(path);
        try {
            m->character = std::make_unique<chr::Character>(data, base, *clips_, outfits);
        } catch (const std::exception& e) {
            problems_.push_back(m->slot + ": " + e.what());
            continue;
        }
        rbSkin(data, *m->character, pre, look ? look->skin : -1);
        if (look) rbPatches(data, *m->character, look->patches);
        {
            // The body's height and weight (char/deform.h, GD05-084): the
            // player's character's, else the premade's (characters.dtb) [C data].
            auto f = [&](const char* k) {
                const DataNode* n = pre->find(k);
                return n && n->children.size() > 1 ? n->floatAt(1) : 0.5f;
            };
            chr::applyDeform(*m->character, look ? look->weight : f("weight"), look ? look->height : f("height"));
        }
        m->character->setXfm(slotXfm(*m));
        if (!it->second->rnd.environment.empty() && it->second->dir)
            m->character->slotEnviron = it->second->dir->resolve<scene::Environ>(it->second->rnd.environment);
        if (log) {
            SDL_Log("band: %s <- %s (%s, %d pieces)", m->slot.c_str(), look ? "the player's own character" : text(pre, 0).c_str(), base.c_str(), int(outfits.size()));
            std::string order;
            for (const std::string& n : m->character->rig().order()) {
                char w[32];
                SDL_snprintf(w, sizeof w, "=%.2f", m->character->rig().weightOf(n));
                order += " " + n + w;
            }
            SDL_Log("band:   rig order:%s", order.c_str());
            chr::Skeleton& sk = m->character->skeleton();
            for (const char* b : {"bone_R-hand.mesh", "bone_L-hand.mesh", "bone_pick.mesh", "bone_pick_strum.mesh", "bone_tip.mesh", "bone_mic.mesh", "bone_L-tip.mesh"}) {
                std::string chain;
                for (int i = sk.find(b), n = 0; i >= 0 && n < 8; i = sk.parent(i), ++n) {
                    char e[96];
                    SDL_snprintf(e, sizeof e, " <- %s(%.1f)", sk.name(i).c_str(), std::sqrt(sk.bind(i).pos[0] * sk.bind(i).pos[0] + sk.bind(i).pos[1] * sk.bind(i).pos[1] + sk.bind(i).pos[2] * sk.bind(i).pos[2]));
                    chain += e;
                }
                if (!chain.empty()) SDL_Log("band:   %s", chain.c_str());
            }
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
            if (look && look->attitude) att = look->attitude;  // the player's character's own
            m->rbFlags = att | (instrument == "guitar" ? kValidGuitar : instrument == "bass" ? kValidBass : 0u);
        }
        members_.push_back(std::move(m));
    }
    world_.band = this;
    chr::Rig::legacySetters = rbLegacy();
    chr::Character::ignoreDrawOnly = rbLegacy();
    if (const char* c = SDL_getenv("GD05_BANDCHECK"); c && *c && *c != '0') rbCheck_ = std::make_shared<RbCheck>();
    return true;
}

std::map<std::string, std::string> Band::rbOutfitMilos(const GameData& data, const std::string& gender, const std::string& instrument,
                                                     const std::vector<std::pair<std::string, std::string>>& pieces, const std::string& kind,
                                                     std::vector<std::string>* missing) {
    std::map<std::string, std::string> outfits;
    int order = 0;
    auto add = [&](const std::string& path) {
        if (data.exists(path)) {
            char key[16];
            SDL_snprintf(key, sizeof key, "+%02d", order++);
            outfits[key] = path;
        } else if (missing) {
            missing->push_back(path);
        }
    };
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
    return outfits;
}

// The skin (GD05-022 / GD05-084, DECISIONS D84), as CompositeCharacter's
// sync after its outfits load (RB 0x822E94B8) [C]:
// - every "skin.mat" of the character (the skin proxies' base: the skin
//   texture tinted) takes the skin palette's colour at skin_color_index
//   (set_skin_color RB 0x822E5110: the mat's colour, rgb), and every skin
//   proxy (an RndDir of type skin_proxy, world_objects.dta) its
//   tattoo_overlay_color, skin_tattoo_overlay.pal's colour at the same
//   index (when the palettes are the same size), on its
//   tattoo_overlay_mat(2, 3) [C data: skin_proxy's set_color];
// - make-up: in head_skin_proxy, makeup_eyes.mask and makeup_eyes_overlay.mask
//   draw the character's makeup_eyes.tex (the eyes outfit's) through
//   makeup_eyes.mat / makeup_eyes_overlay.mat, makeup_lips.mask the
//   lips'; a mask shows only while its texture and material exist [C]
//   (RB looks for makeup_lips_overlay.mask; the data's is named
//   makeup_lips_ovelay.mask, so that one stays as loaded [C data]);
// - render/char_skin.h renders the TexRenderers (head_skin.rndtex,
//   torso_skin.rndtex) from their proxies into the heads' and torsos'
//   placeholder textures.
// A torso proxy's ObjectDir subdir (outfits/torso/naked/
// torso_skin_subdir.milo: the base skin texture) is loaded and linked
// (RB 0x823C2C10 names <gender>/outfits/torso/naked/torso_skin_subdir.milo).
// Other placeholder textures nothing renders (the legs' naked parts) keep
// wave 05's stand-in: the skin colour, nothing for an overlay, a dark cloth
// else [I].
void Band::rbSkin(const GameData& data, chr::Character& c, const DataNode* prefab, int skinIndex) {
    static std::vector<std::array<float, 4>> skin, tattoo;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            auto milo = scene::loadMilo(data, "char/gen/skin.milo_xbox");
            if (auto* pal = milo->root().find<scene::ColorPalette>("skin.pal")) skin = pal->colors;
            if (auto* pal = milo->root().find<scene::ColorPalette>("skin_tattoo_overlay.pal")) tattoo = pal->colors;
        } catch (const std::exception& e) {
            SDL_Log("band: skin.milo: %s", e.what());
        }
    }
    int idx = 0;
    if (const DataNode* n = prefab ? prefab->find("skin_color_index") : nullptr; n && n->children.size() > 1) idx = n->intAt(1);
    if (skinIndex >= 0) idx = skinIndex;
    // RB 0x822E5110: the index clamped to the palette (out of range: 0).
    if (idx < 0 || size_t(idx) >= skin.size()) idx = 0;
    std::array<float, 4> tone{0.8f, 0.62f, 0.5f, 1};
    if (size_t(idx) < skin.size()) tone = skin[size_t(idx)];
    std::array<float, 4> overlay{1, 1, 1, 1};
    if (tattoo.size() == skin.size() && size_t(idx) < tattoo.size()) overlay = tattoo[size_t(idx)];

    // The TexRenderers' proxies and outputs.
    std::set<const scene::Directory*> proxies;
    std::set<const scene::Tex*> rendered;
    std::vector<std::pair<const scene::DirObj*, const scene::Directory*>> skinProxies;
    c.forEachObject([&](scene::Object& o) {
        const auto* t = o.as<scene::TexRenderer>();
        if (!t || !o.dir) return;
        if (const scene::Tex* out = o.dir->resolve<scene::Tex>(t->outputTexture)) rendered.insert(out);
        if (o.as<scene::PatchRenderer>() || t->drawName.empty()) return;
        const scene::Directory* proxy = c.proxyDir(t->drawName);
        if (!proxy) return;
        proxies.insert(proxy);
        const auto* own = o.dir->resolve<scene::DirObj>(t->drawName);
        if (own && own->header.type == "skin_proxy") skinProxies.emplace_back(own, proxy);
        // Its ObjectDir subdirs (torso_skin_subdir.milo), relative to the
        // character's milo.
        if (own)
            for (const std::string& sub : own->objectDir.subdirs)
                if (!sub.empty()) c.linkMilo(data, const_cast<scene::Directory&>(*proxy), scene::findMiloRelative(data, c.path(), sub));
    });
    auto prop = [](const scene::ObjectHeader& h, const std::string& key) -> std::string {
        if (!h.hasProps) return {};
        for (const auto& kv : h.props.children)
            if (kv.children.size() > 1 && kv.children[0].str == key) return kv.children[1].str;
        return {};
    };
    c.forEachObject([&](scene::Object& o) {
        auto* mat = o.as<scene::Mat>();
        if (mat && mat->name == "skin.mat") mat->color = {tone[0], tone[1], tone[2], mat->color[3]};
    });
    for (const auto& [own, proxy] : skinProxies) {
        if (auto* m = proxy->find<scene::Mat>(prop(own->header, "mat"))) m->color = {tone[0], tone[1], tone[2], m->color[3]};
        for (const char* k : {"tattoo_overlay_mat", "tattoo_overlay_mat2", "tattoo_overlay_mat3"})
            if (auto* m = proxy->find<scene::Mat>(prop(own->header, k))) m->color = {overlay[0], overlay[1], overlay[2], m->color[3]};
    }
    // Make-up (RB 0x822E94B8).
    if (const scene::Directory* head = c.proxyDir("head_skin_proxy")) {
        auto findTex = [&](const std::string& name) {
            const scene::Tex* t = nullptr;
            c.forEachObject([&](scene::Object& o) {
                if (!t && o.name == name && !proxies.count(o.dir)) t = o.as<scene::Tex>();
            });
            return t;
        };
        static const char* const kMakeup[][3] = {{"makeup_eyes.mask", "makeup_eyes.tex", "makeup_eyes.mat"},
                                                 {"makeup_eyes_overlay.mask", "makeup_eyes.tex", "makeup_eyes_overlay.mat"},
                                                 {"makeup_lips.mask", "makeup_lips.tex", "makeup_lips.mat"},
                                                 {"makeup_lips_overlay.mask", "makeup_lips.tex", "makeup_lips_overlay.mat"}};
        for (const auto& m : kMakeup) {
            auto* mask = head->find<scene::ScreenMask>(m[0]);
            if (!mask) continue;
            const scene::Tex* tex = findTex(m[1]);
            auto* mat = head->find<scene::Mat>(m[2]);
            mask->draw.showing = tex && mat;
            if (tex && mat) mat->diffuseTex = tex->name;
        }
    }
    // The placeholders nothing renders (wave 05's stand-in) [I].
    c.forEachObject([&](scene::Object& o) {
        auto* mat = o.as<scene::Mat>();
        if (!mat || mat->diffuseTex.empty() || !mat->dir || proxies.count(mat->dir)) return;
        const scene::Tex* t = mat->dir->resolve<scene::Tex>(mat->diffuseTex);
        if (t && !t->bitmap.levels.empty() && t->width > 8) return;
        if (t && rendered.count(t)) return;
        mat->diffuseTex.clear();
        const bool skinMat = mat->name.find("skin") != std::string::npos || mat->name.find("naked") != std::string::npos;
        if (skinMat) mat->color = {tone[0], tone[1], tone[2], mat->color[3]};
        else if (mat->blend == 3) mat->color[3] = 0;
        else mat->color = {0.22f, 0.22f, 0.24f, mat->color[3]};
    });
    if (SDL_getenv("GD05_SKINLOG"))
        c.forEachObject([&](scene::Object& o) {
            const auto* mat = o.as<scene::Mat>();
            if (!mat || !proxies.count(mat->dir)) return;
            const scene::Tex* t = mat->dir->resolve<scene::Tex>(mat->diffuseTex);
            SDL_Log("skin %s/%s: tex %s (%s %dx%d) colour %.2f %.2f %.2f %.2f blend %d z %d useEnv %d prelit %d", mat->dir->name.c_str(),
                    mat->name.c_str(), mat->diffuseTex.c_str(), t ? (t->bitmap.levels.empty() ? "placeholder" : "bitmap") : "missing",
                    t ? t->width : 0, t ? t->height : 0, mat->color[0], mat->color[1], mat->color[2], mat->color[3], mat->blend, mat->zMode,
                    int(mat->useEnviron), int(mat->prelit));
        });
    ++c.skinSerial;
}

const DataNode* Band::rbPatchConfig(const GameData& data) {
    static DataNode root;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            root = parseDtb(data.read("ui/gen/ui.dtb"));
        } catch (const std::exception& e) {
            SDL_Log("band: ui.dtb: %s", e.what());
        }
    }
    return root.find("patch");
}

std::string Band::rbStickerMilo(const GameData& data, const std::string& category) {
    const DataNode* cfg = rbPatchConfig(data);
    const DataNode* st = cfg ? cfg->find("stickers") : nullptr;
    const DataNode* cat = st ? st->find(category) : nullptr;
    if (!cat || cat->children.size() < 2) return {};
    // ../patchcreator/stickers_sailor_jerry.milo, relative to ui/ [C data].
    return scene::findMiloRelative(data, "ui/gen/ui.dtb", text(cat, 1));
}

// The character's patches (GD05-084, D93): each layer's sticker (the
// category milo's Tex objects of type sticker, in table order), its half
// extents (the sticker's sizex / sizey x the layer's scale), its colour
// (patch_warpmesh.milo's sticker.pal at the layer's index when the sticker
// allows a colour, else white) [C data; I: the layer's geometry].
void Band::rbPatches(const GameData& data, chr::Character& c, const std::map<std::string, chr::Patch>& patches) {
    c.patches.clear();
    std::vector<std::array<float, 4>> pal;
    if (const DataNode* cfg = rbPatchConfig(data))
        if (const DataNode* pl = cfg->find("patch_layer"); pl && pl->children.size() > 1)
            if (const scene::Directory* d = c.keepMilo(data, scene::findMiloRelative(data, "ui/gen/ui.dtb", text(pl, 1))))
                if (const auto* p = d->find<scene::ColorPalette>("sticker.pal")) pal = p->colors;
    auto prop = [](const scene::ObjectHeader& h, const char* key, float def) {
        if (!h.hasProps) return def;
        for (const auto& kv : h.props.children)
            if (kv.children.size() > 1 && kv.children[0].str == key) return kv.children[1].asFloat();
        return def;
    };
    for (const auto& [loc, layers] : patches) {
        std::vector<chr::PatchDraw>& out = c.patches[loc];
        for (const chr::PatchLayer& l : layers) {
            if (l.category.empty() || l.sticker < 0) continue;
            const scene::Directory* d = c.keepMilo(data, rbStickerMilo(data, l.category));
            if (!d) continue;
            const scene::Tex* tex = nullptr;
            int n = 0;
            for (const auto& o : d->objects)
                if (const auto* t = o->as<scene::Tex>(); t && t->header.type == "sticker" && n++ == l.sticker) tex = t;
            if (!tex) continue;
            chr::PatchDraw pd;
            pd.tex = tex;
            pd.halfW = 0.5f * prop(tex->header, "sizex", 1) * l.sx, pd.halfH = 0.5f * prop(tex->header, "sizey", 1) * l.sy;
            pd.x = l.x, pd.y = l.y, pd.rot = l.rot, pd.flipX = l.flipX, pd.flipY = l.flipY;
            if (prop(tex->header, "allow_color", 0) != 0 && l.color >= 0 && size_t(l.color) < pal.size()) pd.color = pal[size_t(l.color)];
            out.push_back(pd);
        }
    }
    ++c.skinSerial;
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

// ---- measurements (GD05_BANDCHECK=1) -----------------------------------------
//
// Objective checks of the band, frame by frame (the owner's rule: measured,
// not eyeballed), printed as "bandcheck:" lines when the band goes:
// - stretch: every body bone that skins a mesh (not the face's, which the
//   visemes move, nor CharHair strands) keeps its distance to its parent within
//   1.2 x its bind length (the "bubble gum" arms);
// - reach: with the hand IK fully on, the guitarist's fret finger (bone_tip)
//   on its target on the neck (bone_tip_fret) and the pick (bone_pick) on
//   the strum target (bone_pick_strum), in world units;
// - drums: the share of the drum chart's notes for which a hand or the kick
//   foot moves over 2 units in the 250 ms up to the note;
// - lips: the singer's jaw / lip bones away from their bind pose (degrees /
//   units) while the song's .voc drives the face;
// - mic: the mic bone's distance to the nearer hand, and hands.grp drawing
//   only mic.grp;
// - channels: every position channel of the servo's bones (not the root's
//   bone_facing*) within the sum over the drivers of the largest value their
//   clip sets hold for it (x 1.01): a channel added into every frame without
//   being scaled down or blended out grows past it (GD05-088).
struct Band::RbCheck {
    struct Hit {
        double ms;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        std::array<std::array<float, 3>, 3> first{};
        bool seen = false;
        float move = 0;
    };
    int frames = 0;
    std::map<std::string, float> maxStretch;
    std::map<std::string, std::string> worstBone;
    std::map<std::string, int> stretchFrames;
    std::map<std::string, float> minStretch;  // informative: bones the clips shorten
    std::map<std::string, std::string> shortBone;
    std::map<std::string, std::vector<float>> reachFret, reachPick;
    std::map<std::string, int> ikFrames;
    std::vector<Hit> hits;
    bool hitsBuilt = false;
    float jawMax = 0, lipMax = 0;
    int faceFrames = 0, faceMoving = 0;
    float prevJaw = 0;
    std::vector<float> micDist;
    std::string micDrawOnly;
    std::map<std::string, std::vector<float>> channelBound;  // per servo channel (0: not checked)
    std::map<std::string, float> channelWorst;               // |value| / bound
    std::map<std::string, std::string> channelWorstName;
    std::map<std::string, int> channelFrames;                // frames with a channel past 1.01
    // The face's rest (GD05-097): each member's jaw from its bind (degrees),
    // the most over the run.
    std::map<std::string, float> jawWorst;
};

bool Band::rbLegacy() {
    const char* l = SDL_getenv("GD05_BAND_LEGACY");
    return l && *l && *l != '0';
}

namespace {

float dist3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
float len3(const std::array<float, 3>& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }

// The angle (degrees) between two rotations (rows), through the trace.
float angleBetween(const std::array<float, 9>& a, const std::array<float, 9>& b) {
    float tr = 0;
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) tr += a[size_t(i * 3 + k)] * b[size_t(i * 3 + k)];
    return std::acos(std::clamp((tr - 1) * 0.5f, -1.0f, 1.0f)) * 57.29578f;
}

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(p * float(v.size() - 1) + 0.5f))];
}

}  // namespace

void Band::rbCheckFrame() {
    RbCheck& k = *rbCheck_;
    ++k.frames;
    const double ms = song_ ? song_->tempo.tickToMs(beat_ * song_->tempo.division()) : beat_ * 500.0;
    if (!k.hitsBuilt && song_) {
        k.hitsBuilt = true;
        for (const Gem& g : song_->chart(Part::Drums, Difficulty::Expert).gems) k.hits.push_back({g.ms});
    }
    for (auto& mp : members_) {
        Member& m = *mp;
        chr::Character& c = *m.character;
        chr::Skeleton& sk = c.skeleton();
        // Stretch: the skinned bones.
        static std::map<const chr::Character*, std::vector<int>> skinned;
        auto& bones = skinned[&c];
        if (bones.empty()) {
            std::set<int> b;
            std::set<std::string> strands;  // CharHair points: the hair sim moves them
            c.forEachObject([&](scene::Object& o) {
                if (auto* h = o.as<scene::CharHair>())
                    for (const auto& st : h->strands)
                        for (const auto& pt : st.points) strands.insert(pt.bone);
            });
            c.forEachObject([&](scene::Object& o) {
                if (auto* mesh = o.as<scene::Mesh>())
                    for (const auto& mb : mesh->bones)
                        if (int i = sk.find(mb.name); i >= 0 && sk.parent(i) >= 0) {
                            // The body: not the face (the visemes move its
                            // bones by design) nor the hair strands.
                            bool body = sk.name(i).find("hair") == std::string::npos && !strands.count(sk.name(i));
                            for (int p = i; p >= 0 && body; p = sk.parent(p)) body = sk.name(p) != "bone_head.mesh";
                            if (body) b.insert(i);
                        }
            });
            bones.assign(b.begin(), b.end());
        }
        bool bad = false;
        for (int i : bones) {
            const float rest = len3(sk.bind(i).pos);
            if (rest < 0.5f) continue;
            const float r = len3(sk.local(i).pos) / rest;
            if (r > k.maxStretch[m.slot]) k.maxStretch[m.slot] = r, k.worstBone[m.slot] = sk.name(i);
            if (r > 1.2f) bad = true;
            if (auto it = k.minStretch.find(m.slot); it == k.minStretch.end() || r < it->second) k.minStretch[m.slot] = r, k.shortBone[m.slot] = sk.name(i);
        }
        if (bad) ++k.stretchFrames[m.slot];
        // Channels against their clips' largest values.
        auto& ch = c.bones().channels();
        auto& bound = k.channelBound[m.slot];
        if (bound.size() != ch.size()) {
            bound.assign(ch.size(), 0.0f);
            for (const auto& d : c.drivers()) {
                if (!d->clipSet()) continue;
                std::vector<float> most(ch.size(), 0.0f);
                for (const scene::CharClipSamples* clip : d->clipSet()->clips())
                    for (const scene::CharBonesSamples* set : {&clip->one, &clip->full})
                        for (size_t b = 0; b < set->bones.size(); ++b) {
                            const int i = c.bones().find(set->bones[b].name);
                            if (i < 0 || set->bones[b].type != scene::CharBonesSamples::kPos) continue;
                            for (uint32_t n = 0; n < set->count; ++n)
                                for (int x = 0; x < 3; ++x)
                                    most[size_t(i)] = std::max(most[size_t(i)], std::fabs(set->sample(n)[set->offset[b] + x]));
                        }
                for (size_t i = 0; i < ch.size(); ++i) bound[i] += most[i];
            }
            for (size_t i = 0; i < ch.size(); ++i)
                if (ch[i].name.rfind("bone_facing", 0) == 0) bound[i] = 0;
        }
        bool over = false;
        for (size_t i = 0; i < ch.size(); ++i) {
            if (bound[i] <= 0 || ch[i].type != scene::CharBonesSamples::kPos) continue;
            const float v = std::max({std::fabs(ch[i].v[0]), std::fabs(ch[i].v[1]), std::fabs(ch[i].v[2])});
            const float r = v / bound[i];
            if (r > k.channelWorst[m.slot]) k.channelWorst[m.slot] = r, k.channelWorstName[m.slot] = ch[i].name;
            if (r > 1.01f) over = true;
        }
        if (over) ++k.channelFrames[m.slot];
        if (const int jaw = sk.find("bone_jaw.mesh"); jaw >= 0 && c.rbFace())
            k.jawWorst[m.slot] = std::max(k.jawWorst[m.slot], angleBetween(sk.local(jaw).rot, sk.bind(jaw).rot));
        auto at = [&](const char* n, std::array<float, 3>& out) {
            const int i = sk.find(n);
            if (i < 0) return false;
            out = sk.world(i).pos;
            return true;
        };
        std::array<float, 3> a{}, b{};
        if (m.type == "rb_guitar" || m.type == "rb_bass") {
            if (c.rig().weightOf("left_hand.weight") >= 0.99f && at("bone_tip.mesh", a) && at("bone_tip_fret.mesh", b)) k.reachFret[m.slot].push_back(dist3(a, b)), ++k.ikFrames[m.slot];
            if (c.rig().weightOf("right_hand.weight") >= 0.99f && at("bone_pick.mesh", a) && at("bone_pick_strum.mesh", b)) k.reachPick[m.slot].push_back(dist3(a, b));
        } else if (m.type == "rb_drum") {
            const char* limbs[3] = {"bone_R-hand.mesh", "bone_L-hand.mesh", "bone_R-ankle.mesh"};
            for (auto& h : k.hits) {
                if (ms < h.ms - 250 || ms > h.ms) continue;
                for (int l = 0; l < 3; ++l)
                    if (at(limbs[l], a)) {
                        if (!h.seen) h.first[size_t(l)] = a;
                        h.move = std::max(h.move, dist3(a, h.first[size_t(l)]));
                    }
                h.seen = true;
            }
        } else if (m.type == "rb_vocals") {
            const int jaw = sk.find("bone_jaw.mesh");
            const int lip = sk.find("bone_lowlip_mid.mesh");
            if (jaw >= 0 && ms > 0) {
                const float ang = angleBetween(sk.local(jaw).rot, sk.bind(jaw).rot);
                k.jawMax = std::max(k.jawMax, ang);
                ++k.faceFrames;
                if (std::fabs(ang - k.prevJaw) > 0.2f) ++k.faceMoving;
                k.prevJaw = ang;
            }
            if (lip >= 0) k.lipMax = std::max(k.lipMax, dist3(sk.local(lip).pos, sk.bind(lip).pos));
            if (at("bone_mic.mesh", a)) {
                float d = 1e30f;
                if (at("bone_R-hand.mesh", b)) d = std::min(d, dist3(a, b));
                if (at("bone_L-hand.mesh", b)) d = std::min(d, dist3(a, b));
                k.micDist.push_back(d);
            }
            if (const scene::Object* g = c.dir().get("hands.grp"))
                if (const auto* grp = g->as<scene::Group>()) k.micDrawOnly = c.drawOnly(*grp);
        }
    }
}

void Band::rbCheckReport() {
    const RbCheck& k = *rbCheck_;
    int problems = 0;
    auto line = [&](bool ok, const std::string& what) {
        if (!ok) ++problems;
        SDL_Log("bandcheck: %s %s", ok ? "ok  " : "FAIL", what.c_str());
    };
    char buf[256];
    for (auto& mp : members_) {
        const Member& m = *mp;
        const float st = k.maxStretch.count(m.slot) ? k.maxStretch.at(m.slot) : 0;
        if (k.minStretch.count(m.slot))
            SDL_Log("bandcheck: info %s shortest %.3f x bind (%s)", m.slot.c_str(), k.minStretch.at(m.slot), k.shortBone.at(m.slot).c_str());
        const int sf = k.stretchFrames.count(m.slot) ? k.stretchFrames.at(m.slot) : 0;
        SDL_snprintf(buf, sizeof buf, "%s stretch: worst %.3f x bind (%s), %d of %d frames past 1.2", m.slot.c_str(), st,
                     k.worstBone.count(m.slot) ? k.worstBone.at(m.slot).c_str() : "-", sf, k.frames);
        line(sf == 0, buf);
        const int cf = k.channelFrames.count(m.slot) ? k.channelFrames.at(m.slot) : 0;
        SDL_snprintf(buf, sizeof buf, "%s channels within their clips: worst %.3f x (%s), %d of %d frames past 1.01", m.slot.c_str(),
                     k.channelWorst.count(m.slot) ? double(k.channelWorst.at(m.slot)) : 0.0,
                     k.channelWorstName.count(m.slot) ? k.channelWorstName.at(m.slot).c_str() : "-", cf, k.frames);
        line(cf == 0, buf);
        // The face's rest (GD05-097): the jaw (its bind is the face set's
        // Base) never turns further from it than the face set's clips reach.
        if (k.jawWorst.count(m.slot)) {
            const float spread = m.character->faceRestSpread("bone_jaw.quat");
            SDL_snprintf(buf, sizeof buf, "%s face rest: jaw up to %.1f deg from bind, the face clips reach %.1f from Base", m.slot.c_str(),
                         double(k.jawWorst.at(m.slot)), double(spread));
            // Two visemes lit at once (the lip sync's blends) may add up: 1.5 x.
            line(spread >= 0 && k.jawWorst.at(m.slot) <= spread * 1.5f, buf);
        }
        if (m.type == "rb_guitar" || m.type == "rb_bass") {
            const auto fret = k.reachFret.count(m.slot) ? k.reachFret.at(m.slot) : std::vector<float>{};
            const auto pick = k.reachPick.count(m.slot) ? k.reachPick.at(m.slot) : std::vector<float>{};
            SDL_snprintf(buf, sizeof buf, "%s fret hand on the neck: IK on %zu of %d frames, finger to target p95 %.2f max %.2f", m.slot.c_str(),
                         fret.size(), k.frames, percentile(fret, 0.95f), percentile(fret, 1));
            line(fret.size() * 2 > size_t(k.frames) && percentile(fret, 0.95f) < 1.0f, buf);
            SDL_snprintf(buf, sizeof buf, "%s pick on the strings: IK on %zu of %d frames, pick to target p95 %.2f max %.2f", m.slot.c_str(),
                         pick.size(), k.frames, percentile(pick, 0.95f), percentile(pick, 1));
            line(pick.size() * 2 > size_t(k.frames) && percentile(pick, 0.95f) < 1.0f, buf);
        } else if (m.type == "rb_drum") {
            int seen = 0, moved = 0;
            for (const auto& h : k.hits)
                if (h.seen) ++seen, moved += h.move > 2.0f ? 1 : 0;
            SDL_snprintf(buf, sizeof buf, "%s moves on the drum notes: %d of %d notes (a hand or the kick foot over 2 units in the 250 ms before)",
                         m.slot.c_str(), moved, seen);
            line(seen > 0 && moved * 10 >= seen * 8, buf);
        } else if (m.type == "rb_vocals") {
            SDL_snprintf(buf, sizeof buf, "%s lips: jaw up to %.1f deg from bind, moving on %d of %d frames, low lip up to %.2f units", m.slot.c_str(),
                         k.jawMax, k.faceMoving, k.faceFrames, k.lipMax);
            line(k.jawMax > 3 && k.faceMoving * 10 > k.faceFrames, buf);
            SDL_snprintf(buf, sizeof buf, "%s mic: to the nearer hand p95 %.2f, hands.grp draws %s", m.slot.c_str(), percentile(k.micDist, 0.95f),
                         k.micDrawOnly.empty() ? "every member" : k.micDrawOnly.c_str());
            line(!k.micDist.empty() && percentile(k.micDist, 0.95f) < 8 && k.micDrawOnly == "mic.grp", buf);
        }
    }
    SDL_Log("bandcheck: %s (%d problem(s), %d frames)", problems ? "FAILED" : "passed", problems, k.frames);
}
