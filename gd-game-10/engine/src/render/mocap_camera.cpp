// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/mocap_camera.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "data/qbkey.h"
#include "script/globals.h"

namespace venue {
namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }
const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct ? v->st.get() : nullptr; }
uint32_t keyOf(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0; }
std::string strOf(const qb::Value* v) { return v && v->type == qb::Type::String ? v->str : std::string(); }

uint32_t target(const char* fmt, size_t i) {
    char nm[64];
    std::snprintf(nm, sizeof nm, fmt, int(i));
    return qbkey::key(nm);
}

}  // namespace

void MocapCameras::init(const Venue& v, const Globals& g, const std::vector<qb::File>* songFiles,
                        const std::vector<const qb::Struct*>& performance, const anim::Skeleton* skeleton, const ClipLookup& clips,
                        double fromMs) {
    segs_.clear();
    haveRig_ = skeleton != nullptr;
    if (!haveRig_ || !clips) return;
    rig_.init(*skeleton);
    auto add = [&](uint32_t tgt, double t, float offset, bool cycle, uint32_t anim, const Node* n) {
        const anim::Clip* c = anim ? clips(anim) : nullptr;
        if (!c || !n) {
            if (std::getenv("GD10_CAMLOG"))
                SDL_Log("camera: mocap %s: %s%s", qbkey::names().get(tgt).c_str(), qbkey::names().get(anim).c_str(), !c ? " (no anim)" : " (no node)");
            return;
        }
        Seg s;
        s.target = tgt, s.t = t, s.offset = offset, s.cycle = cycle, s.clip = c;
        s.pos = n->pos, s.rot = n->rot;
        segs_.push_back(s);
    };
    const Node* drumStart = v.node(v.key("_TRG_Waypoint_Drummer_Start"));
    // start_drummer_cameras (play_intro): Drum_Andy_StandardLP_C01 / _C02 on
    // drummer_mocap_lock_target_01 / 02 at the drummer's start, cycling [C script].
    add(K("drummer_mocap_lock_target_01"), fromMs, 0, true, K("Drum_Andy_StandardLP_C01"), drumStart);
    add(K("drummer_mocap_lock_target_02"), fromMs, 0, true, K("Drum_Andy_StandardLP_C02"), drumStart);
    // Band_PlayClip {clip startframe}: the clip struct's cameras[i] {name anim}
    // on moment_cam_lock_target_<i+1> at <zone>_<name>, its drummercameras[i]
    // {anim} on drummer_mocap_lock_target_<i+1> at the drummer's start
    // (clipplaydrummercamera index = i+1, drumCam_Params) [C scripts and data;
    // I: Band_PlayClip_CFunc's mapping of the arrays to the targets, the
    // anims from the clip's start frame as the members'].
    for (const qb::Struct* p : performance) {
        const qb::Value* tv = p->find(K("time"));
        const qb::Struct* params = structOf(p->find(K("params")));
        if (!tv || !params || keyOf(p->find(K("scr"))) != K("band_playclip")) continue;
        // The clip struct: a global, or in the song's own QB files.
        const uint32_t clipKey = keyOf(params->find(K("clip")));
        const qb::Struct* cs = structOf(g.find(clipKey));
        for (size_t i = 0; !cs && songFiles && i < songFiles->size(); ++i) cs = structOf((*songFiles)[i].find(clipKey));
        if (!cs) continue;
        const qb::Value* sf = params->find(K("startframe"));
        // startframe in 1/30 s [C 0x8219F088], as the members' clips.
        const float offset = sf ? float(sf->number()) / 30.0f : 0.0f;
        const double t = tv->number();
        if (const qb::Value* cams = cs->find(K("cameras")); cams && cams->type == qb::Type::Array && cams->arr)
            for (size_t i = 0; i < cams->arr->values.size() && i < 10; ++i) {
                const qb::Struct* c = structOf(&cams->arr->values[i]);
                if (!c) continue;
                const std::string node = strOf(c->find(K("name")));
                add(target("moment_cam_lock_target_%02d", i + 1), t, offset, false, keyOf(c->find(K("anim"))),
                    node.empty() ? nullptr : v.node(v.key("_" + node)));
            }
        if (const qb::Value* cams = cs->find(K("drummercameras")); cams && cams->type == qb::Type::Array && cams->arr)
            for (size_t i = 0; i < cams->arr->values.size() && i < 9; ++i)
                if (const qb::Struct* c = structOf(&cams->arr->values[i]))
                    add(target("drummer_mocap_lock_target_%02d", i + 1), t, offset, false, keyOf(c->find(K("anim"))), drumStart);
    }
    std::stable_sort(segs_.begin(), segs_.end(), [](const Seg& a, const Seg& b) { return a.t < b.t; });
    if (std::getenv("GD10_CAMLOG")) SDL_Log("camera: %zu mocap camera anims", segs_.size());
}

void MocapCameras::addTransition(const Venue& v, const Globals& g, const std::string& transition, double atMs, const ClipLookup& zoneClips) {
    if (!haveRig_ || !zoneClips) return;
    const uint32_t key = v.key("_" + transition + "_Transition");
    const qb::Value* tv = g.find(key);
    for (size_t i = 0; !tv && i < v.files.size(); ++i) tv = v.files[i].find(key);
    const qb::Struct* tr = structOf(tv);
    const qb::Value* tab = tr ? tr->find(K("ScriptTable")) : nullptr;
    if (!tab || tab->type != qb::Type::Array || !tab->arr) return;
    for (const qb::Value& e : tab->arr->values) {
        const qb::Struct* st = structOf(&e);
        const qb::Struct* params = st ? structOf(st->find(K("params"))) : nullptr;
        if (!params || keyOf(st->find(K("scr"))) != K("SpecialCamera_PlayAnim")) continue;
        const std::string obj = strOf(params->find(K("obj")));
        const uint32_t anim = keyOf(params->find(K("anim")));
        const uint32_t tgt = v.key("_" + obj);
        const Node* n = v.node(tgt);
        const anim::Clip* c = anim ? zoneClips(anim) : nullptr;
        const qb::Value* when = st->find(K("time"));
        if (!c || !n) {
            if (std::getenv("GD10_CAMLOG")) SDL_Log("camera: transition anim %s on %s: %s", qbkey::names().get(anim).c_str(), obj.c_str(), !c ? "no anim" : "no node");
            continue;
        }
        Seg sg;
        sg.target = tgt, sg.t = atMs + (when ? when->number() : 0.0), sg.clip = c;
        sg.pos = n->pos, sg.rot = n->rot;
        segs_.push_back(sg);
    }
    std::stable_sort(segs_.begin(), segs_.end(), [](const Seg& a, const Seg& b) { return a.t < b.t; });
}

const MocapCameras::Seg* MocapCameras::at(uint32_t tgt, double ms) const {
    const Seg* last = nullptr;
    for (const Seg& s : segs_) {
        if (s.t > ms) break;
        if (s.target == tgt) last = &s;
    }
    if (!last) return nullptr;
    // A one-shot anim holds its last frame (timer_waitanimcomplete, the target
    // stays where its anim ended) [I].
    return last;
}

bool MocapCameras::animated(uint32_t tgt, double ms) const {
    if (at(tgt, ms)) return true;
    const anim::Clip* c = nullptr;
    float sec = 0;
    Vec3 p;
    Quat q;
    return follow_ && follow_(tgt, ms, c, sec, p, q) && c;
}

bool MocapCameras::frame(uint32_t tgt, uint32_t bone, double ms, Vec3& pos, Quat& rot) const {
    if (!haveRig_) return false;
    if (const Seg* s = at(tgt, ms)) {
        float sec = s->offset + float((ms - s->t) / 1000.0);
        if (s->cycle && s->clip->duration > 0) sec = std::fmod(sec, s->clip->duration);
        return sampleBone(*s->clip, sec, bone, s->pos, s->rot, pos, rot);
    }
    const anim::Clip* c = nullptr;
    float sec = 0;
    Vec3 p;
    Quat q;
    if (follow_ && follow_(tgt, ms, c, sec, p, q) && c) return sampleBone(*c, sec, bone, p, q, pos, rot);
    return false;
}

bool MocapCameras::sampleBone(const anim::Clip& clip, float sec, uint32_t bone, const Vec3& at, const Quat& atRot, Vec3& pos, Quat& rot) const {
    const int b = rig_.find(bone);
    if (b < 0) return false;
    std::vector<anim::BonePose> tmp;
    anim::sample(clip, sec, tmp);
    // The bone's chain from the root, in the rig's column-vector terms
    // (render/band_anim.cpp overrideInto's conventions).
    std::vector<int> chain;
    for (int i = b; i >= 0; i = rig_.parent[size_t(i)]) chain.push_back(i);
    bandanim::Quat q{atRot[0], atRot[1], atRot[2], atRot[3]};
    bandanim::Vec t{at[0], at[1], at[2]};
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const size_t i = size_t(*it);
        bandanim::Xf l = rig_.bind[i];
        if (i < tmp.size()) {
            if (tmp[i].hasRot) l.q = bandanim::qconj({tmp[i].rot[0], tmp[i].rot[1], tmp[i].rot[2], tmp[i].rot[3]});
            if (tmp[i].hasPos) {
                const anim::Vec3& v = tmp[i].pos;
                l.t = clip.singlePose ? bandanim::Vec{v[0], v[1], v[2]} : bandanim::Vec{l.t.x + v[0], l.t.y + v[1], l.t.z + v[2]};
            }
        }
        const bandanim::Vec r = bandanim::qrot(q, l.t);
        t = {t.x + r.x, t.y + r.y, t.z + r.z};
        q = bandanim::qmul(q, l.q);
    }
    pos = {t.x, t.y, t.z};
    rot = {q.x, q.y, q.z, q.w};
    return true;
}

}  // namespace venue
