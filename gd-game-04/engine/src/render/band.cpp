// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/band.h"

#include <SDL3/SDL_log.h>

#include <exception>

#include "data/disc.h"
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
    members_.clear();
    view_.reset();
    songFiles_.clear();
    try {
        lib_.load(disc);
    } catch (const std::exception& e) {
        error = std::string("band: ") + e.what();
        return false;
    }
    // The song's performance events (<song>_performance in songs\<song>.mid.qb).
    std::vector<const qb::Struct*> perfEvents;
    const std::string songPak = "DATA/COMPRESSED/SONGS/" + chart.song + "_song.pak.xen";
    if (!chart.song.empty() && disc.exists(songPak)) {
        try {
            const Pak pak = Pak::load(disc, songPak);
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

    members_.reserve(chosen.size());
    for (const band::Member& m : chosen) {
        Entry e;
        e.model = band::loadModel(disc, g, lib_, m, chart.song, error);
        if (!e.model) return false;
        bandanim::Animator::Setup s;
        s.model = e.model.get();
        s.g = &g;
        s.chart = &chart;
        s.perf = perf;
        s.performance = perfEvents;
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
        // create_band_member: lightgroup band; the guitarist alt_band [C script; I: alt_band lights add to band's].
        e.lightGroups = {K("band")};
        if (m.role == band::Role::Guitarist) e.lightGroups.push_back(K("alt_band"));
        members_.push_back(std::move(e));
    }
    return true;
}

bool Band::attach(gfx::RenderDevice& device, std::string& error) {
    if (view_) return true;
    view_ = std::make_unique<BandView>();
    for (Entry& e : members_) {
        e.view = view_->add(device, *e.model, error);
        if (e.view < 0) {
            view_.reset();
            return false;
        }
    }
    return true;
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
    for (Entry& e : members_) {
        if (e.evaluatedAt == ms) continue;
        e.anim.evaluate(ms, e.world, e.skin);
        e.evaluatedAt = ms;
    }
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

void Band::draw(double ms, const venue::CamPose& cam, const venue::Venue& v, const std::vector<venue::LightState>& lights,
                const venue::LightShow& show) {
    if (!view_) return;
    evaluate(ms);
    std::vector<BandView::Instance> inst;
    for (const Entry& e : members_) {
        if (!e.placed) continue;
        BandView::Instance i;
        i.member = e.view;
        i.skin = &e.skin;
        i.root = e.root;
        i.lightGroups = e.lightGroups;
        inst.push_back(std::move(i));
    }
    view_->draw(cam, inst, v, lights, show);
}
