// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/stage_peds.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <exception>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "data/venue.h"
#include "render/particles.h"
#include "script/globals.h"

namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }

const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr; }
uint32_t keyOf(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0; }

// 'StagePeds\SPed_GoGo.skin' -> the zone pak entry's full name key,
// qbkey("models\StagePeds\SPed_GoGo.skin") [C data, as the crowd's models].
uint32_t modelKey(const std::string& model) {
    std::string s = "models\\";
    for (char c : model)
        if (!(c == '\\' && !s.empty() && s.back() == '\\')) s += c == '/' ? '\\' : c;
    return qbkey::key(s);
}

// A field of a struct or of the structs it includes (<obj_..._creation_params>).
const qb::Value* field(const Globals& g, const qb::Struct& s, uint32_t name, int depth = 0) {
    if (const qb::Value* v = s.find(name)) return v;
    if (depth > 4) return nullptr;
    for (const qb::Item& it : s.items) {
        if (it.name) continue;
        const qb::Value* inc = &it.value;
        if (inc->type == qb::Type::Key || inc->type == qb::Type::KeyPtr) inc = g.find(inc->key);
        if (const qb::Struct* is = structOf(inc))
            if (const qb::Value* v = field(g, *is, name, depth + 1)) return v;
    }
    return nullptr;
}

}  // namespace

bool StagePeds::start(const Disc& disc, const Globals& g, const venue::Venue& v, const band::Library& lib, std::string& error) {
    shutdown();
    peds_.clear();
    byNode_.clear();
    try {
        const Pak zone = Pak::load(disc, "DATA/COMPRESSED/ZONES/" + v.zone + ".pak.xen");
        band::ClipMap zoneClips = band::readPakClips(zone);
        for (const venue::Node& n : v.nodes) {
            if (n.cls != K("gameobject") || !n.createdAtStart || !n.st) continue;
            const qb::Value* mv = n.st->find(K("Model"));
            if (!mv || mv->type != qb::Type::String || mv->str.size() < 5) continue;
            std::string lower = mv->str;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (lower.compare(lower.size() - 5, 5, ".skin") != 0) continue;
            // The crowd's own ped (Real_Crowd): the card imposters draw it (render/crowd.h) [I].
            if (lower.rfind("real_crowd", 0) == 0) continue;
            // The stage divers: Crowd_StageDiver_Hide at the song's start (Crowd_StageDiver_Jump has no caller) [C scripts].
            {
                std::string nn = qbkey::names().get(n.name);
                std::transform(nn.begin(), nn.end(), nn.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                if (nn.find("_trg_ped_stagedive") != std::string::npos) continue;
            }
            const qb::Value* pv = n.st->find(K("profile"));
            const qb::Value* prof = pv && (pv->type == qb::Type::Key || pv->type == qb::Type::KeyPtr) ? g.find(pv->key) : pv;
            const qb::Struct* ps = structOf(prof);
            if (!ps) continue;
            const uint32_t skel = keyOf(field(g, *ps, K("SkeletonName")));
            const anim::Skeleton* ske = lib.skeleton(skel);
            if (!ske) {
                SDL_Log("stage peds: %s: no skeleton %s", qbkey::names().get(n.name).c_str(), qbkey::names().get(skel).c_str());
                continue;
            }
            const PakEntry* e = zone.findType(modelKey(mv->str), K(".skin"));
            if (!e) {
                SDL_Log("stage peds: %s: no skin %s", qbkey::names().get(n.name).c_str(), mv->str.c_str());
                continue;
            }
            Ped p;
            p.node = n.name;
            p.model = std::make_unique<band::Model>();
            p.model->skeleton = ske;
            auto [data, size] = zone.data(*e);
            band::Part part;
            part.scene = scene::parse(data, size, scene::Kind::Skin);
            p.model->parts.push_back(std::move(part));
            if (e->parent)
                if (const PakEntry* t = zone.findType(e->parent, K(".tex"))) {
                    auto [tp, ts] = zone.data(*t);
                    for (gh3tex::Texture& tx : gh3tex::decodeTexDict(tp, ts))
                        if (tx.supported && !tx.levels.empty()) p.model->textures.emplace(tx.key, std::move(tx));
                }
            for (const auto& [k, c] : lib.permClips()) p.model->clips.emplace(k, c);
            for (const auto& [k, c] : zoneClips) p.model->clips.emplace(k, c);
            p.rig.init(*ske);
            bandanim::Xf root;
            root.t = {n.pos[0], n.pos[1], n.pos[2]};
            root.q = {n.rot[0], n.rot[1], n.rot[2], n.rot[3]};
            p.root = bandanim::matOf(root);
            // The profile's light group (Crowd, band, dragon, ...) [C data].
            if (const uint32_t lg = keyOf(field(g, *ps, K("lightgroup")))) p.lightGroups.push_back(lg);
            byNode_[p.node] = peds_.size();
            peds_.push_back(std::move(p));
        }
    } catch (const std::exception& ex) {
        error = std::string("stage peds: ") + ex.what();
        peds_.clear();
        byNode_.clear();
        return false;
    }
    if (!peds_.empty()) SDL_Log("stage peds %s: %llu", v.zone.c_str(), static_cast<unsigned long long>(peds_.size()));
    return !peds_.empty();
}

bool StagePeds::attach(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib, std::string& error) {
    dev_ = &dev;
    view_.setLibrary(std::move(lib));
    for (Ped& p : peds_) {
        p.view = view_.add(dev, *p.model, error);
        if (p.view < 0) return false;
    }
    return true;
}

void StagePeds::shutdown() {
    view_.shutdown();
    dev_ = nullptr;
}

double StagePeds::play(uint32_t obj, uint32_t anim, float blend, double t) {
    auto it = byNode_.find(obj);
    if (it == byNode_.end()) return -1;
    Ped& p = peds_[it->second];
    const anim::Clip* c = p.model->clip(anim);
    static const bool log = SDL_getenv("GD04_STAGEPEDS_LOG") != nullptr;
    if (!c) {
        if (log) SDL_Log("stage peds: %s: no clip %s", qbkey::names().get(obj).c_str(), qbkey::names().get(anim).c_str());
        return -1;
    }
    if (log && p.body.empty()) SDL_Log("stage peds: %s plays %s", qbkey::names().get(obj).c_str(), qbkey::names().get(anim).c_str());
    // blendduration -1: the anim tree's default [I: 0.3 s].
    p.body.play(c, t, blend < 0 ? 0.3f : blend);
    if (p.body.plays.size() > 3) p.body.plays.erase(p.body.plays.begin(), p.body.plays.end() - 3);
    return double(c->duration) * 1000.0;
}

void StagePeds::draw(double ms, const venue::CamPose& cam, const venue::Venue& v, const std::vector<venue::LightState>& lights,
                     const venue::LightShow& show, const Particles* scripts) {
    if (!dev_ || peds_.empty()) return;
    std::vector<BandView::Instance> inst;
    inst.reserve(peds_.size());
    for (Ped& p : peds_) {
        // Not drawn before its scripts played a clip (a ped in its bind pose
        // is never seen in the original) [I].
        if (p.view < 0 || p.body.empty()) continue;
        if (scripts && scripts->visibility(p.node) == 0) continue;
        bandanim::Pose pose = p.rig.bind;
        bandanim::evalBody(p.rig, p.body, ms, pose);
        bandanim::worldMatrices(p.rig, pose, p.world);
        p.skin.resize(p.world.size());
        for (size_t b = 0; b < p.world.size(); ++b) p.skin[b] = bandanim::mmul(p.world[b], p.rig.invBind[b]);
        BandView::Instance in;
        in.member = p.view;
        in.skin = &p.skin;
        in.root = p.root;
        in.lightGroups = p.lightGroups;
        inst.push_back(std::move(in));
    }
    view_.setTime(ms / 1000.0);
    view_.draw(cam, inst, v, lights, show);
}
