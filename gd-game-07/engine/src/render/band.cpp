// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/band.h"

#include <cstring>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <unordered_map>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

uint32_t keyOfValue(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0u; }

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
    std::shared_ptr<const Pak> songArchive;
    if (!chart.song.empty() && disc.exists(songPak)) {
        try {
            songArchive = Pak::shared(disc, songPak);
            const Pak& pak = *songArchive;
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
    for (band::Member& m : picked) {
        band::songAnimPart(g, songFiles_, qbkey::key(chart.song), m);
        // Some song performance clips live in <song>_song.pak, alongside the
        // performance QB, while the body / mocap clips live in
        // <song>_anims.pak. Load that bank for members whose performance
        // events actually reference its SKAs (lip-sync and solo clips) [C data].
        bool needsSongArchive = false;
        if (songArchive) {
            auto songClip = [&](uint32_t key) { return key && songArchive->findType(key, K(".ska")); };
            auto structOf = [](const qb::Value* value) -> const qb::Struct* {
                return value && value->type == qb::Type::Struct && value->st ? value->st.get() : nullptr;
            };
            for (const qb::Struct* event : perfEvents) {
                const uint32_t script = keyOfValue(event->find(K("scr")));
                const qb::Struct* params = structOf(event->find(K("params")));
                if (!params) continue;
                if (script == K("band_playfacialanim")) {
                    if (keyOfValue(params->find(K("name"))) == m.object && songClip(keyOfValue(params->find(K("anim"))))) {
                        needsSongArchive = true;
                        break;
                    }
                } else if (script == K("band_playclip")) {
                    const uint32_t clipKey = keyOfValue(params->find(K("clip")));
                    const qb::Struct* clip = structOf(g.find(clipKey));
                    if (!clip)
                        for (const qb::File& file : songFiles_)
                            if ((clip = structOf(file.find(clipKey)))) break;
                    const qb::Struct* anims = clip ? structOf(clip->find(K("anims"))) : nullptr;
                    const uint32_t anim = anims ? keyOfValue(anims->find(m.object)) : 0;
                    if (anim && anim != K("none") && songClip(anim)) {
                        needsSongArchive = true;
                        break;
                    }
                }
            }
        }
        if (needsSongArchive && std::find(m.animPaks.begin(), m.animPaks.end(), songPak) == m.animPaks.end()) {
            m.animPaks.push_back(songPak);
            if (std::getenv("GD07_ANIMLOG"))
                SDL_Log("band: loading %s animation clips for %s", songPak.c_str(), m.profileName.c_str());
        }
    }
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
        e.venue = v;
        e.scale = m.scale;
        e.placed = nodeRoot(v, waypointName(m.role), m.scale, e.root);
        e.home = e.root;
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
std::unique_ptr<AccessorySim> Band::accessorySim(const Disc& disc, const Globals& g, const band::Member& m, const bandanim::Rig& rig,
                                                 std::shared_ptr<const Pak>& globalPak) {
    if (SDL_getenv("GD07_NO_RAGDOLL")) return nullptr;
    // gh4: the full body desc's ragdoll (.rag in PAK/global_rag_skel, its
    // entry's short name; ragdoll_path names it) and accessory_bones (Rocker_accessory_bones: the
    // hair, torso and legs ACC bones) [C data; I: the same accessory ragdoll
    // as GH3's, the vocalist_static_tree's ragdoll node].
    if (m.gh4) {
        // A CAS body: cas_create_a_rocker_male / _female's ragdoll, each
        // accessory part's ragdoll merged in below.
        const qb::Struct* body = m.fullBody ? m.fullBody : m.casBody;
        if (!body) return nullptr;
        const qb::Value* rp = body->find(K("ragdoll_path"));
        const qb::Value* ab = g.find(keyOfValue(body->find(K("accessory_bones"))));
        if (!rp || rp->type != qb::Type::String || !ab || ab->type != qb::Type::Array || !ab->arr) return nullptr;
        std::vector<uint32_t> bones;
        for (const qb::Value& b : ab->arr->values) bones.push_back(b.key);
        // gh4's .rag: "LZSS", u32 size, Neversoft's LZSS of a Havok 5.5.0 packfile [C data].
        auto parseRag = [](const uint8_t* p, size_t s) {
            Bytes unpacked;
            if (s >= 8 && std::memcmp(p, "LZSS", 4) == 0) {
                const size_t n = size_t(p[4]) << 24 | size_t(p[5]) << 16 | size_t(p[6]) << 8 | size_t(p[7]);
                unpacked = qb::lzss(p + 8, s - 8, n);
                p = unpacked.data(), s = unpacked.size();
            }
            return havok::parse(p, s);
        };
        try {
            if (!globalPak) globalPak = Pak::shared(disc, "DATA/COMPRESSED/PAK/global_rag_skel.pak.xen");
            const PakEntry* re = globalPak->findType(keyOfValue(body->find(K("ragdoll"))), K(".rag"));
            if (!re) {
                // The CAS bodies' ragdoll ($ragdoll_Rocker_male) is under its file's name
                // (gh_rocker_male_ragdoll) [C data global_rag_skel.pak].
                std::string base = rp->str;
                if (const size_t sl = base.find_last_of("\\/"); sl != std::string::npos) base = base.substr(sl + 1);
                if (const size_t dot = base.find('.'); dot != std::string::npos) base = base.substr(0, dot);
                re = globalPak->findType(qbkey::key(base), K(".rag"));
            }
            if (re) {
                auto [p, s] = globalPak->data(*re);
                havok::Physics phys = parseRag(p, s);
                // ragdoll_mergeragdoll: each accessory part's acc_bones as its
                // acc_ragdoll has them: the bone's rigid body (shape, mass,
                // place) and its constraint to its parent body, over the
                // body's ragdoll [C script cas_process_accessory_part; I:
                // by bone name]. The accessory bones no part brings keep
                // the body's stock bodies: nothing is skinned on them, and
                // they stay keyframed (they are no part's hair or cloth).
                const bool merge = !m.fullBody && !SDL_getenv("GD07_NO_ACC_MERGE");
                if (!m.fullBody) bones.clear();
                for (size_t pi = 0; merge && pi < m.accParts.size(); ++pi) {
                    const band::Member::AccPart& ap = m.accParts[pi];
                    const Bytes rb = ap.ragdoll ? band::casFile(disc, ap.ragdoll) : Bytes{};
                    if (rb.empty()) {
                        SDL_Log("band: accessory ragdoll %s is not in the CAS paks", qbkey::names().get(ap.ragdoll).c_str());
                        continue;
                    }
                    try {
                        const havok::Physics acc = parseRag(rb.data(), rb.size());
                        auto bodyOf = [](const havok::Physics& ph, uint32_t key) {
                            for (size_t i = 0; i < ph.bodies.size(); ++i)
                                if (qbkey::key(ph.bodies[i].name) == key) return int(i);
                            return -1;
                        };
                        std::vector<std::pair<int, int>> moved;  // the body in phys, in acc
                        for (uint32_t k : ap.bones) {
                            const int ai = bodyOf(acc, k);
                            if (ai < 0) continue;
                            int bi = bodyOf(phys, k);
                            if (bi < 0) bi = int(phys.bodies.size()), phys.bodies.push_back(acc.bodies[size_t(ai)]);
                            else phys.bodies[size_t(bi)] = acc.bodies[size_t(ai)];
                            moved.push_back({bi, ai});
                            bones.push_back(k);
                        }
                        for (const auto& [bi, ai] : moved) {
                            phys.constraints.erase(std::remove_if(phys.constraints.begin(), phys.constraints.end(),
                                                                  [bi = bi](const havok::Constraint& c) { return c.bodyA == bi; }),
                                                   phys.constraints.end());
                            for (const havok::Constraint& c : acc.constraints) {
                                if (c.bodyA != ai || c.bodyB < 0 || size_t(c.bodyB) >= acc.bodies.size()) continue;
                                havok::Constraint n = c;
                                n.bodyA = bi;
                                n.bodyB = bodyOf(phys, qbkey::key(acc.bodies[size_t(c.bodyB)].name));
                                if (n.bodyB >= 0) phys.constraints.push_back(std::move(n));
                            }
                        }
                    } catch (const std::exception& ex) {
                        SDL_Log("band: accessory ragdoll %s: %s", qbkey::names().get(ap.ragdoll).c_str(), ex.what());
                    }
                }
                // ragdoll_collision_group: lists of bone names [C data].
                std::vector<std::vector<uint32_t>> groups;
                if (const qb::Value* cg = g.find(keyOfValue(body->find(K("ragdoll_collision_group")))); cg && cg->type == qb::Type::Array && cg->arr)
                    for (const qb::Value& l : cg->arr->values) {
                        groups.emplace_back();
                        if (l.type == qb::Type::Array && l.arr)
                            for (const qb::Value& b : l.arr->values) groups.back().push_back(b.key);
                    }
                auto acc = std::make_unique<AccessorySim>();
                if (acc->init(rig, phys, bones, groups.empty() || SDL_getenv("GD07_NO_RAGDOLL_GROUPS") ? nullptr : &groups)) return acc;
                if (m.fullBody) SDL_Log("band: ragdoll %s: no accessory body", rp->str.c_str());
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

bool Band::nodeRoot(const venue::Venue* v, const std::string& suffix, float scale, bandanim::Mat34& out) {
    bandanim::Xf root;
    bool found = false;
    if (v)
        if (const venue::Node* n = v->node(v->key(suffix))) {
            root.t = {n->pos[0], n->pos[1], n->pos[2]};
            root.q = {n->rot[0], n->rot[1], n->rot[2], n->rot[3]};
            found = true;
        }
    out = bandanim::matOf(root);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) out.m[i * 4 + j] *= scale;
    return found;
}

void Band::evaluateEntry(Entry& e, double ms) {
    if (e.evaluatedAt == ms) return;
    // The node it stands on now: its own start node, or a playing clip's
    // startnodes.<member> (Animator::startNodeAt; GD10-065).
    if (const std::string& node = e.anim.startNodeAt(ms); node != e.rootNode) {
        e.rootNode = node;
        e.root = e.home;
        bandanim::Mat34 r;
        if (!node.empty()) {
            if (nodeRoot(e.venue, "_TRG_Waypoint_" + node, e.scale, r)) e.root = r;
            else SDL_Log("band: %s: no start node %s", e.model->member.profileName.c_str(), node.c_str());
        }
    }
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
    // Development aid: GD07_ARMLOG=<m>: a bone jumping more than m metres
    // (model space) between two evaluations under 40 ms apart (the owner's
    // "flickering arms").
    if (static const char* al = SDL_getenv("GD07_ARMLOG"); al) {
        static std::unordered_map<const Entry*, std::pair<double, std::vector<bandanim::Mat34>>> prev;
        auto& pv = prev[&e];
        const float lim = float(std::atof(al));
        if (pv.second.size() == e.world.size() && ms > pv.first && ms - pv.first < 40) {
            float worst = 0;
            int wb = -1;
            for (size_t b = 0; b < e.world.size(); ++b) {
                const float dx = e.world[b].m[3] - pv.second[b].m[3], dy = e.world[b].m[7] - pv.second[b].m[7], dz = e.world[b].m[11] - pv.second[b].m[11];
                const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > worst) worst = d, wb = int(b);
            }
            if (worst > lim) {
                std::string bn = "?";
                for (const auto& [k, i] : e.anim.rig().index)
                    if (i == wb) bn = qbkey::name(k);
                SDL_Log("armlog: %s at %.0f ms: %s jumped %.3f m in %.1f ms", e.model->member.profileName.c_str(), ms, bn.c_str(), worst, ms - pv.first);
            }
        }
        pv = {ms, e.world};
        if (SDL_getenv("GD07_ARMTRACE") && e.model->member.profileName == SDL_getenv("GD07_ARMTRACE")) {
            const int b = e.anim.rig().find(qbkey::key("Bone_Forearm_R"));
            int pb = b >= 0 ? e.anim.rig().parent[size_t(b)] : -1;
            if (b >= 0)
                SDL_Log("armtrace %.0f forearm %.3f %.3f %.3f parent %.3f %.3f %.3f", ms, e.world[size_t(b)].m[3], e.world[size_t(b)].m[7], e.world[size_t(b)].m[11],
                        pb >= 0 ? e.world[size_t(pb)].m[3] : 0.f, pb >= 0 ? e.world[size_t(pb)].m[7] : 0.f, pb >= 0 ? e.world[size_t(pb)].m[11] : 0.f);
        }
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
