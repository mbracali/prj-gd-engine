// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/band.h"

#include <cstring>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>

#include "data/disc.h"
#include "data/havok.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "data/venue.h"
#include "game/chart.h"
#include "script/globals.h"

namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }

// get_start_node_id: <zone>_TRG_Waypoint_<member>_start (one player:
// guitarist, bassist, drummer, vocalist) [C script].
const char* waypointName(band::Role r) {
    switch (r) {
    case band::Role::Guitarist: return "_TRG_Waypoint_guitarist_start";
    case band::Role::Bassist: return "_TRG_Waypoint_bassist_start";
    case band::Role::Drummer: return "_TRG_Waypoint_drummer_start";
    case band::Role::Vocalist: return "_TRG_Waypoint_vocalist_start";
    }
    return "";
}

}  // namespace

bool Band::start(const Disc& disc, const Globals& g, const venue::Venue& v, const Chart& chart, uint32_t songKey,
                 const venue::PerfTimeline* perf, double startMs, std::string& error) {
    return startWith(disc, g, &v, chart, band::chooseBand(g, v.zone, songKey), perf, startMs, error);
}

bool Band::startWith(const Disc& disc, const Globals& g, const venue::Venue* v, const Chart& chart, const std::vector<band::Member>& chosen,
                     const venue::PerfTimeline* perf, double startMs, std::string& error) {
    disc_ = &disc, g_ = &g, venue_ = v, chart_ = &chart, perf_ = perf, chosen_ = chosen, startMs_ = startMs;
    members_.clear();
    view_.reset();
    songFiles_.clear();
    try {
        lib_ = band::Library::shared(disc);
        shaderLib_ = gh3shader::library(disc);
    } catch (const std::exception& e) {
        error = std::string("band: ") + e.what();
        return false;
    }
    // The song's performance events (<song>_performance in songs\<song>.mid.qb).
    std::vector<const qb::Struct*> perfEvents;
    const std::string songPak = "DATA/COMPRESSED/SONGS/" + chart.song + "_song.pak.xen";
    if (!chart.song.empty() && disc.exists(songPak)) {
        try {
            const auto pakShared = Pak::shared(disc, songPak);
            const Pak& pak = *pakShared;
            for (const PakEntry& e : pak.entries()) {
                if (e.type != K(".qb")) continue;
                auto [p, size] = pak.data(e);
                songFiles_.push_back(qb::decode(p, size));
            }
        } catch (const std::exception& e) {
            SDL_Log("band: %s: %s", songPak.c_str(), e.what());
        }
        for (const qb::File& f : songFiles_)
            if (const qb::Value* pv = f.find(qbkey::key(chart.song + "_performance")); pv && pv->type == qb::Type::Array && pv->arr)
                for (const qb::Value& e : pv->arr->values)
                    if (e.type == qb::Type::Struct && e.st) perfEvents.push_back(e.st.get());
    }
    // The bassist plays the guitar part when the song's bassist is Morello or Slash [C script].
    bool bassGuitar = false;
    for (const band::Member& m : chosen)
        if (m.role == band::Role::Bassist && (m.profileName == "Morello" || m.profileName == "Slash")) bassGuitar = true;

    // The song's anim struct (car_male_anim_struct_<song>[_alt]) lives in
    // the song pak, not yet in the globals at this point [C data].
    std::vector<band::Member> picked = chosen;
    for (band::Member& m : picked) band::songAnimPart(g, songFiles_, qbkey::key(chart.song), m);
    members_.reserve(picked.size());
    for (const band::Member& m : picked) {
        Entry e;
        e.model = band::loadModel(disc, g, *lib_, m, chart.song, error);
        if (!e.model) return false;
        bandanim::Animator::Setup s;
        s.model = e.model.get();
        s.g = &g;
        s.chart = &chart;
        s.perf = perf;
        s.performance = perfEvents;
        s.songFiles = &songFiles_;
        s.seed = m.object ^ qbkey::key(chart.song) ^ m.body;
        s.startMs = startMs;
        s.guitarPart = bassGuitar;
        if (!e.anim.init(s, error)) return false;
        // The waypoint: position and facing (obj_setorientation dir =
        // GetWaypointDir) [C script create_band_member; I: the model faces its +z].
        bandanim::Xf root;
        if (v)
            if (const venue::Node* n = v->node(v->key(waypointName(m.role)))) {
                root.t = {n->pos[0], n->pos[1], n->pos[2]};
                root.q = {n->rot[0], n->rot[1], n->rot[2], n->rot[3]};
                e.placed = true;
            }
        e.root = bandanim::matOf(root);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) e.root.m[i * 4 + j] *= m.scale;
        // The physics world's gravity (0, -9.8, 0) [C 82253B10: 8200E4E4] in
        // model space: R^T (0, -9.8, 0). GH3 places the ragdoll with the
        // object's rotation and position at scale 1 [C 823FF100], so only the
        // rotation counts.
        {
            const float s = std::max(1e-6f, m.scale);
            e.gravity = {e.root.m[4] * -9.8f / s, e.root.m[5] * -9.8f / s, e.root.m[6] * -9.8f / s};
        }
        e.acc = accessorySim(disc, g, m, e.anim.rig(), globalPak_);
        // create_band_member: lightgroup band; the guitarist alt_band [C script; I: alt_band lights add to band's].
        e.lightGroups = {K("band")};
        if (m.role == band::Role::Guitarist) e.lightGroups.push_back(K("alt_band"));
        members_.push_back(std::move(e));
    }
    return true;
}

// The ragdoll: the profile's ragdoll (ragdoll2 for outfit 2) from
// global.pak, its accessory bodies simulated (ragdoll_setaccessorybones
// accessory_bones = $Guitarist_accessory_bones) [C scripts].
namespace {
uint32_t keyOfValue(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0u; }
}  // namespace

std::unique_ptr<AccessorySim> Band::accessorySim(const Disc& disc, const Globals& g, const band::Member& m, const bandanim::Rig& rig,
                                                 std::shared_ptr<const Pak>& globalPak) {
    if (SDL_getenv("GD10_NO_RAGDOLL")) return nullptr;
    // gh4: the full body desc's ragdoll (.rag in PAK/global_rag_skel, its
    // entry's short name; ragdoll_path names it) and accessory_bones (Rocker_accessory_bones: the
    // hair, torso and legs ACC bones) [C data; I: the same accessory ragdoll
    // as GH3's, the vocalist_static_tree's ragdoll node].
    if (m.gh4) {
        if (!m.fullBody) return nullptr;
        const qb::Value* rp = m.fullBody->find(K("ragdoll_path"));
        const qb::Value* ab = g.find(keyOfValue(m.fullBody->find(K("accessory_bones"))));
        if (!rp || rp->type != qb::Type::String || !ab || ab->type != qb::Type::Array || !ab->arr) return nullptr;
        std::vector<uint32_t> bones;
        for (const qb::Value& b : ab->arr->values) bones.push_back(b.key);
        try {
            if (!globalPak) globalPak = Pak::shared(disc, "DATA/COMPRESSED/PAK/global_rag_skel.pak.xen");
            if (const PakEntry* re = globalPak->findType(keyOfValue(m.fullBody->find(K("ragdoll"))), K(".rag"))) {
                auto [p, s] = globalPak->data(*re);
                // gh4's .rag: "LZSS", u32 size, Neversoft's LZSS of a Havok 5.5.0 packfile [C data].
                Bytes unpacked;
                if (s >= 8 && std::memcmp(p, "LZSS", 4) == 0) {
                    const size_t n = size_t(p[4]) << 24 | size_t(p[5]) << 16 | size_t(p[6]) << 8 | size_t(p[7]);
                    unpacked = qb::lzss(p + 8, s - 8, n);
                    p = unpacked.data(), s = unpacked.size();
                }
                const havok::Physics phys = havok::parse(p, s);
                auto acc = std::make_unique<AccessorySim>();
                if (acc->init(rig, phys, bones)) return acc;
                SDL_Log("band: ragdoll %s: no accessory body", rp->str.c_str());
            } else {
                SDL_Log("band: ragdoll %s: not in global_rag_skel", rp->str.c_str());
            }
        } catch (const std::exception& ex) {
            SDL_Log("band: ragdoll %s: %s", rp->str.c_str(), ex.what());
        }
        return nullptr;
    }
    if (!m.profile) return nullptr;
    uint32_t ragKey = 0;
    if (m.outfit == 2)
        if (const qb::Value* r2 = m.profile->find(K("ragdoll2")); r2 && (r2->type == qb::Type::Key || r2->type == qb::Type::KeyPtr)) ragKey = r2->key;
    if (!ragKey)
        if (const qb::Value* r = m.profile->find(K("ragdoll")); r && (r->type == qb::Type::Key || r->type == qb::Type::KeyPtr)) ragKey = r->key;
    std::vector<uint32_t> accBones;
    if (const qb::Value* ab = g.find(K("Guitarist_accessory_bones")); ab && ab->type == qb::Type::Array && ab->arr)
        for (const qb::Value& b : ab->arr->values) accBones.push_back(b.key);
    if (ragKey && !accBones.empty()) {
        try {
            if (!globalPak) globalPak = Pak::shared(disc, "DATA/COMPRESSED/ZONES/global.pak.xen");
            if (const PakEntry* re = globalPak->findType(ragKey, K(".rag"))) {
                auto [rp, rs] = globalPak->data(*re);
                const havok::Physics phys = havok::parse(rp, rs);
                auto acc = std::make_unique<AccessorySim>();
                if (acc->init(rig, phys, accBones)) return acc;
            }
        } catch (const std::exception& ex) {
            SDL_Log("band: ragdoll %s: %s", qbkey::names().get(ragKey).c_str(), ex.what());
        }
    }
    return nullptr;
}

bool Band::attach(gfx::RenderDevice& device, std::string& error) {
    int r;
    while ((r = attachStep(device, error)) == 0) {
    }
    return r > 0;
}

int Band::attachStep(gfx::RenderDevice& device, std::string& error) {
    if (view_ && attached_ >= members_.size()) return 1;
    if (!view_) {
        view_ = std::make_unique<BandView>();
        view_->setLibrary(shaderLib_);
        attached_ = 0;
    }
    if (attached_ < members_.size()) {
        Entry& e = members_[attached_];
        // Its textures and programs a few ms a frame, then the member.
        if (!view_->prepare(device, *e.model, made_, 6.0)) return 0;
        e.view = view_->add(device, *e.model, error, &made_);
        made_.clear();
        if (e.view < 0) {
            view_.reset();
            attached_ = 0;
            return -1;
        }
        ++attached_;
    }
    return attached_ >= members_.size() ? 1 : 0;
}

void Band::advance(double ms) {
    for (Entry& e : members_) e.anim.advance(ms);
}

void Band::playerNote(double ms, bool hit) {
    // player1_status.band_member: the guitarist in one-player games [C script create_guitarist].
    for (Entry& e : members_)
        if (e.model->member.role == band::Role::Guitarist) e.anim.playerNote(ms, hit);
}

void Band::evaluate(double ms) {
    for (Entry& e : members_) evaluateEntry(e, ms);
}

void Band::evaluateEntry(Entry& e, double ms) {
    if (e.evaluatedAt == ms) return;
    e.anim.evaluate(ms, e.world, e.skin);
    if (e.acc) {
        const bandanim::Animator& an = e.anim;
        e.acc->apply(ms, e.world, [&an](double t, std::vector<bandanim::Mat34>& w) {
            std::vector<bandanim::Mat34> sk;
            an.evaluate(t, w, sk);
        }, e.gravity, &an.ragdollResets());
        const bandanim::Rig& rig = e.anim.rig();
        for (size_t b = 0; b < e.world.size() && b < rig.invBind.size(); ++b) e.skin[b] = bandanim::mmul(e.world[b], rig.invBind[b]);
    }
    e.evaluatedAt = ms;
}

bool Band::bonePos(uint32_t member, uint32_t bone, venue::Vec3& out) const {
    for (const Entry& e : members_) {
        if (e.model->member.object != member || e.world.empty()) continue;
        const int b = e.anim.rig().find(bone);
        if (b < 0) return false;
        const bandanim::Vec p = bandanim::xform(e.root, bandanim::xform(e.world[size_t(b)], {0, 0, 0}));
        out = {p.x, p.y, p.z};
        return true;
    }
    return false;
}

void Band::drawShadow(double ms, const gh3shader::Globals& light, const venue::Venue& v, const std::vector<venue::LightState>& lights,
                      const venue::LightShow& show) {
    if (!view_) return;
    evaluate(ms);
    std::vector<BandView::Instance> inst;
    for (const Entry& e : members_) {
        if (!e.placed || e.hidden || e.view < 0) continue;
        BandView::Instance i;
        i.member = e.view;
        i.skin = &e.skin;
        i.root = e.root;
        i.lightGroups = e.lightGroups;
        inst.push_back(std::move(i));
    }
    view_->setShadowPass(&light);
    view_->setReuseSkinning(skinnedAt_ == ms);  // the second cascade: the first's vertices
    view_->draw(venue::CamPose{}, inst, v, lights, show);
    view_->setReuseSkinning(false);
    view_->setShadowPass(nullptr);
    skinnedAt_ = ms;
}

void Band::draw(double ms, const venue::CamPose& cam, const venue::Venue& v, const std::vector<venue::LightState>& lights,
                const venue::LightShow& show) {
    if (!view_) return;
    evaluate(ms);
    view_->setReuseSkinning(skinnedAt_ == ms);
    std::vector<BandView::Instance> inst;
    for (const Entry& e : members_) {
        if (!e.placed || e.hidden || e.view < 0) continue;
        BandView::Instance i;
        i.member = e.view;
        i.skin = &e.skin;
        i.root = e.root;
        i.lightGroups = e.lightGroups;
        inst.push_back(std::move(i));
    }
    view_->setTime(ms / 1000.0);
    view_->draw(cam, inst, v, lights, show);
    view_->setReuseSkinning(false);
}

std::string Band::plausible(double ms) const {
    char buf[256];
    for (const Entry& e : members_) {
        const bandanim::Rig& rig = e.anim.rig();
        const std::string who = e.model->member.profileName;
        if (e.world.size() != rig.size()) continue;
        for (size_t b = 0; b < e.world.size(); ++b)
            for (float v : e.world[b].m)
                if (!std::isfinite(v)) return who + ": a bone is not finite at " + std::to_string(int(ms)) + " ms";
        for (size_t b = 1; b < e.world.size(); ++b) {
            const int p = rig.parent[b];
            if (p <= 0) continue;  // the root's children move freely (the clips' root motion)
            // Props and helpers slide by design (the fret position along the
            // neck, the IK targets, the mic, the sticks): the body's bones only.
            std::string n = qbkey::name(e.model->skeleton->bones[b].name);
            for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            bool prop = false;
            for (const char* w : {"guitar", "ik_", "fret", "string", "mic", "stick", "camera", "prop", "drum", "bass"}) prop = prop || n.find(w) != std::string::npos;
            if (prop) continue;
            const bandanim::Vec d = {e.world[b].m[3] - e.world[size_t(p)].m[3], e.world[b].m[7] - e.world[size_t(p)].m[7],
                                     e.world[b].m[11] - e.world[size_t(p)].m[11]};
            const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            const bandanim::Vec& bt = rig.bind[b].t;
            const float bindLen = std::sqrt(bt.x * bt.x + bt.y * bt.y + bt.z * bt.z);
            // An accessory joint may open by checkAndFixConstraint's 5 cm
            // before it is fixed, plus a frame's swing [C 823FFC28: 0.05].
            const bool acc = e.acc && e.acc->isDynamic(int(b));
            if (std::fabs(len - bindLen) > std::max(acc ? 0.08f : 0.05f, 0.15f * bindLen)) {
                std::snprintf(buf, sizeof buf, "%s: bone %s is %.3f m from its parent at %d ms (bind %.3f m)%s", who.c_str(),
                              qbkey::name(e.model->skeleton->bones[b].name).c_str(), double(len), int(ms), double(bindLen),
                              acc ? " (an accessory)" : "");
                return buf;
            }
        }
        const float miss = e.anim.ikMiss(ms, e.world);
        if (miss > 0.10f) {
            std::snprintf(buf, sizeof buf, "%s: a hand %.2f m off its IK target at %d ms", who.c_str(), double(miss), int(ms));
            return buf;
        }
    }
    return "";
}

std::string Band::frameRateCheck(const std::vector<double>& times, int& checked) const {
    checked = 0;
    if (!disc_ || times.empty()) return "";
    Band a, b;
    std::string err;
    if (!a.startWith(*disc_, *g_, venue_, *chart_, chosen_, perf_, startMs_, err) || !b.startWith(*disc_, *g_, venue_, *chart_, chosen_, perf_, startMs_, err))
        return "the band does not start again: " + err;
    // Frame times at `fps` from the start, each check time also a frame.
    auto play = [&](Band& band, int fps, double until, double& at) {
        const double step = 1000.0 / fps;
        while (at + step < until) {
            at += step;
            band.advance(at);
            band.evaluate(at);
        }
        at = until;
        band.advance(at);
        band.evaluate(at);
    };
    double ta = startMs_, tb = startMs_;
    char buf[256];
    for (double t : times) {
        if (t <= startMs_) continue;
        play(a, 60, t, ta);
        play(b, 240, t, tb);
        if (std::string p = a.plausible(t); !p.empty()) return p;
        for (size_t m = 0; m < a.members_.size() && m < b.members_.size(); ++m) {
            const Entry &x = a.members_[m], &y = b.members_[m];
            for (size_t k = 0; k < x.world.size() && k < y.world.size(); ++k) {
                float d = 0;
                for (int i : {3, 7, 11}) d = std::max(d, std::fabs(x.world[k].m[i] - y.world[k].m[i]));
                if (d > 0.001f) {
                    std::snprintf(buf, sizeof buf, "%s: bone %s %.4f m apart at 60 and 240 fps at %d ms", x.model->member.profileName.c_str(),
                                  qbkey::name(x.model->skeleton->bones[k].name).c_str(), double(d), int(t));
                    return buf;
                }
            }
        }
        ++checked;
    }
    return "";
}
