// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/band_anim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "data/qbkey.h"
#include "script/globals.h"

namespace bandanim {
namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }
constexpr double kFrameMs = 1000.0 / 60.0;  // a game frame [I: 60 Hz]

uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0;
}

bool hasFlag(const qb::Struct& st, uint32_t flag) {
    for (const qb::Item& it : st.items)
        if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && it.value.key == flag) return true;
    return false;
}

const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr; }

// ExtendCRC: the QB key of name + suffix, continued from the name's key
// (QB keys have no final XOR) [C: ExtendCRC <name> '_Info'].
uint32_t extendKey(uint32_t key, std::string_view suffix) {
    uint32_t c = key;
    for (char ch : suffix) c = qbkey::detail::kTable[(c ^ uint8_t(qbkey::detail::lower(ch))) & 0xFF] ^ (c >> 8);
    return c;
}

float numOf(const Globals& g, const char* name, float fallback) { return qbNumber(g, K(name), fallback); }

Quat quatOf(const anim::Quat& q) { return {q[0], q[1], q[2], q[3]}; }

Quat qnorm(Quat q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n <= 0) return {};
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

Quat qpow(const Quat& q, float s) { return nlerp(Quat{}, q, s); }

Vec vadd(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec vsub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec vmul(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float vdot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec vcross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float vlen(Vec a) { return std::sqrt(vdot(a, a)); }
Vec vnorm(Vec a) {
    const float l = vlen(a);
    return l > 1e-12f ? vmul(a, 1.0f / l) : Vec{};
}

// The rotation taking direction a to direction b (shortest arc).
Quat between(Vec a, Vec b) {
    a = vnorm(a), b = vnorm(b);
    const float d = vdot(a, b);
    if (d < -0.99999f) {
        Vec axis = vcross({1, 0, 0}, a);
        if (vlen(axis) < 1e-4f) axis = vcross({0, 1, 0}, a);
        axis = vnorm(axis);
        return {axis.x, axis.y, axis.z, 0};
    }
    const Vec c = vcross(a, b);
    return qnorm({c.x, c.y, c.z, 1 + d});
}

Quat rotOf(const Mat34& m) {
    const float* a = m.m;
    // Columns may carry a uniform scale: normalise.
    const float sx = std::sqrt(a[0] * a[0] + a[4] * a[4] + a[8] * a[8]);
    const float s = sx > 1e-12f ? 1.0f / sx : 1.0f;
    const float r00 = a[0] * s, r01 = a[1] * s, r02 = a[2] * s, r10 = a[4] * s, r11 = a[5] * s, r12 = a[6] * s, r20 = a[8] * s,
                r21 = a[9] * s, r22 = a[10] * s;
    Quat q;
    const float tr = r00 + r11 + r22;
    if (tr > 0) {
        const float k = 0.5f / std::sqrt(tr + 1.0f);
        q = {(r21 - r12) * k, (r02 - r20) * k, (r10 - r01) * k, 0.25f / k};
    } else if (r00 > r11 && r00 > r22) {
        const float k = 2.0f * std::sqrt(1.0f + r00 - r11 - r22);
        q = {0.25f * k, (r01 + r10) / k, (r02 + r20) / k, (r21 - r12) / k};
    } else if (r11 > r22) {
        const float k = 2.0f * std::sqrt(1.0f + r11 - r00 - r22);
        q = {(r01 + r10) / k, 0.25f * k, (r12 + r21) / k, (r02 - r20) / k};
    } else {
        const float k = 2.0f * std::sqrt(1.0f + r22 - r00 - r11);
        q = {(r02 + r20) / k, (r12 + r21) / k, 0.25f * k, (r10 - r01) / k};
    }
    return qnorm(q);
}

Vec posOf(const Mat34& m) { return {m.m[3], m.m[7], m.m[11]}; }

// The clip's pose of bone b in this rig's local terms. Rotations are
// absolute (file convention, conjugated here); translations are absolute in
// single-pose clips (the rig defaults hold the bind values) and offsets from
// the bind translation in key-compressed ones [C data: bone lengths are 0 in
// every compressed track, e.g. bone_neck 0.0 vs bind 0.311]. Difference
// clips (flags 0x200, the hands' "_D" anims) hold a rotation and a
// translation to compose onto the pose below [C data: finger bones' deltas
// with zero translation; I: the composition order].
bool isDifference(const anim::Clip& c) { return (c.flags & 0x200) != 0; }

void sampleClip(const Rig& rig, const anim::Clip& c, float sec, std::vector<anim::BonePose>& tmp) {
    anim::sample(c, sec, tmp);
    if (!c.mask.empty())
        for (size_t b = 0; b < tmp.size() && b < c.mask.size(); ++b)
            if (!c.mask[b]) tmp[b].hasRot = tmp[b].hasPos = false;
    (void)rig;
}

void overrideInto(const Rig& rig, const anim::Clip& c, float sec, Pose& pose, std::vector<anim::BonePose>& tmp) {
    sampleClip(rig, c, sec, tmp);
    const size_t n = std::min(tmp.size(), rig.size());
    for (size_t b = 0; b < n; ++b) {
        if (tmp[b].hasRot) pose[b].q = qconj(quatOf(tmp[b].rot));
        if (tmp[b].hasPos) {
            const anim::Vec3& v = tmp[b].pos;
            pose[b].t = c.singlePose ? Vec{v[0], v[1], v[2]} : Vec{rig.bind[b].t.x + v[0], rig.bind[b].t.y + v[1], rig.bind[b].t.z + v[2]};
        }
    }
}

void deltaInto(const Rig& rig, const anim::Clip& c, float sec, Pose& delta, std::vector<anim::BonePose>& tmp) {
    sampleClip(rig, c, sec, tmp);
    const size_t n = std::min(tmp.size(), rig.size());
    for (size_t b = 0; b < n; ++b) {
        if (tmp[b].hasRot) delta[b].q = qconj(quatOf(tmp[b].rot));
        if (tmp[b].hasPos) delta[b].t = {tmp[b].pos[0], tmp[b].pos[1], tmp[b].pos[2]};
    }
}

Xf lerpXf(const Xf& a, const Xf& b, float w) {
    return {nlerp(a.q, b.q, w), {a.t.x + (b.t.x - a.t.x) * w, a.t.y + (b.t.y - a.t.y) * w, a.t.z + (b.t.z - a.t.z) * w}};
}

float weightAt(const Play& p, double t) {
    if (p.blend <= 0) return 1.0f;
    return float(std::clamp((t - p.start) / (double(p.blend) * 1000.0), 0.0, 1.0));
}

float clipTime(const Play& p, double t) {
    return float(std::clamp((t - p.start) / 1000.0, 0.0, double(p.clip ? p.clip->duration : 0.0f)));
}

// A degenerateblend node over `pose`: each clip's pose (over the bind pose for
// the body, over what lies below for a partial node) cross-faded in from the
// previous one [I: linear weights].
void evalNode(const Rig& rig, const Node& n, double t, Pose& pose, bool fromBind, std::vector<anim::BonePose>& tmp) {
    if (n.plays.empty()) return;
    const Pose below = fromBind ? rig.bind : pose;
    Pose cur = below, target;
    bool first = true;
    for (const Play& p : n.plays) {
        if (!p.clip) continue;
        target = below;
        overrideInto(rig, *p.clip, clipTime(p, t), target, tmp);
        const float w = first && !fromBind ? weightAt(p, t) : (first ? 1.0f : weightAt(p, t));
        for (size_t b = 0; b < cur.size(); ++b) cur[b] = lerpXf(cur[b], target[b], w);
        first = false;
    }
    pose = std::move(cur);
}

// A node of difference clips: the cross-faded delta, composed onto `pose`
// with the strength of its modulate node.
void evalDelta(const Rig& rig, const Node& n, double t, float strength, Pose& pose, std::vector<anim::BonePose>& tmp) {
    if (n.plays.empty() || strength <= 0) return;
    Pose cur(rig.size()), target;
    for (const Play& p : n.plays) {
        if (!p.clip) continue;
        target.assign(rig.size(), Xf{});
        if (isDifference(*p.clip)) {
            deltaInto(rig, *p.clip, clipTime(p, t), target, tmp);
        } else {
            // A plain clip in a difference branch: its pose relative to the bind pose [I].
            Pose abs = rig.bind;
            overrideInto(rig, *p.clip, clipTime(p, t), abs, tmp);
            for (size_t b = 0; b < abs.size(); ++b)
                target[b] = {qmul(qconj(rig.bind[b].q), abs[b].q), vsub(abs[b].t, rig.bind[b].t)};
        }
        const float w = weightAt(p, t);
        for (size_t b = 0; b < cur.size(); ++b) cur[b] = lerpXf(cur[b], target[b], w);
    }
    for (size_t b = 0; b < pose.size(); ++b) {
        const Xf& d = cur[b];
        pose[b].q = qmul(pose[b].q, qpow(d.q, strength));
        pose[b].t = vadd(pose[b].t, vmul(d.t, strength));
    }
}

void worldOf(const Rig& rig, const Pose& pose, std::vector<Mat34>& world) {
    world.resize(rig.size());
    for (size_t b = 0; b < rig.size(); ++b) {
        const Mat34 local = matOf(pose[b]);
        world[b] = rig.parent[b] >= 0 ? mmul(world[size_t(rig.parent[b])], local) : local;
    }
}

}  // namespace

Quat qmul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat qconj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

Vec qrot(const Quat& q, const Vec& v) {
    const Quat p = qmul(qmul(q, {v.x, v.y, v.z, 0}), qconj(q));
    return {p.x, p.y, p.z};
}

Quat nlerp(const Quat& a, const Quat& b, float t) {
    const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = d < 0 ? -1.0f : 1.0f;
    return qnorm({a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t});
}

Mat34 matOf(const Xf& x) {
    const Quat& q = x.q;
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x,
                wy = q.w * q.y, wz = q.w * q.z;
    Mat34 m;
    m.m[0] = 1 - 2 * (yy + zz), m.m[1] = 2 * (xy - wz), m.m[2] = 2 * (xz + wy), m.m[3] = x.t.x;
    m.m[4] = 2 * (xy + wz), m.m[5] = 1 - 2 * (xx + zz), m.m[6] = 2 * (yz - wx), m.m[7] = x.t.y;
    m.m[8] = 2 * (xz - wy), m.m[9] = 2 * (yz + wx), m.m[10] = 1 - 2 * (xx + yy), m.m[11] = x.t.z;
    return m;
}

Mat34 mmul(const Mat34& a, const Mat34& b) {
    Mat34 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            float s = a.m[i * 4 + 0] * b.m[0 + j] + a.m[i * 4 + 1] * b.m[4 + j] + a.m[i * 4 + 2] * b.m[8 + j];
            if (j == 3) s += a.m[i * 4 + 3];
            r.m[i * 4 + j] = s;
        }
    }
    return r;
}

Vec xform(const Mat34& m, const Vec& p) {
    return {m.m[0] * p.x + m.m[1] * p.y + m.m[2] * p.z + m.m[3], m.m[4] * p.x + m.m[5] * p.y + m.m[6] * p.z + m.m[7],
            m.m[8] * p.x + m.m[9] * p.y + m.m[10] * p.z + m.m[11]};
}

Vec xformDir(const Mat34& m, const Vec& d) {
    return {m.m[0] * d.x + m.m[1] * d.y + m.m[2] * d.z, m.m[4] * d.x + m.m[5] * d.y + m.m[6] * d.z,
            m.m[8] * d.x + m.m[9] * d.y + m.m[10] * d.z};
}

void Rig::init(const anim::Skeleton& s) {
    const size_t n = s.bones.size();
    parent.resize(n);
    bind.resize(n);
    invBind.resize(n);
    index.clear();
    for (size_t i = 0; i < n; ++i) {
        const anim::Bone& b = s.bones[i];
        parent[i] = b.parent;
        bind[i] = {qconj(quatOf(b.rot)), {b.pos[0], b.pos[1], b.pos[2]}};
        // The file's row-vector inverse bind B (v' = v B), transposed.
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) invBind[i].m[r * 4 + c] = b.invBind[size_t(c * 4 + r)];
            invBind[i].m[r * 4 + 3] = b.invBind[size_t(12 + r)];
        }
        index.emplace(b.name, int(i));
    }
}

void Node::play(const anim::Clip* c, double t, float blend) {
    plays.push_back({c, t, blend});
    // Drop what a fully blended-in clip hides.
    for (size_t i = plays.size() - 1; i > 0; --i)
        if (plays[i].start + double(plays[i].blend) * 1000.0 <= t) {
            plays.erase(plays.begin(), plays.begin() + long(i));
            break;
        }
    if (plays.size() > 8) plays.erase(plays.begin(), plays.end() - 8);
}

float Ramp::at(double t) const {
    if (length <= 0 || t >= start + length) return to;
    if (t <= start) return from;
    return from + (to - from) * float((t - start) / length);
}

void Ramp::set(float target, double t, double seconds) {
    from = at(t);
    to = target;
    start = t;
    length = seconds * 1000.0;
}

void Animator::schedule(double t, Ev kind, int a, int b, uint32_t gen, const qb::Struct* params) {
    Event e;
    e.t = t, e.seq = ++seq_, e.kind = kind, e.a = a, e.b = b, e.gen = gen, e.params = params;
    queue_.push(e);
}

uint32_t Animator::random(uint32_t n) {
    // splitmix64 [I: GH3's GetRandomValue generator is not reproduced]
    rng_ += 0x9E3779B97F4A7C15ull;
    uint64_t z = rng_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return n ? uint32_t(z % n) : 0;
}

const anim::Clip* Animator::clip(uint32_t key) {
    if (!key) return nullptr;
    const anim::Clip* c = model_->clip(key);
    if (!c) {
        ++stats_.missingClips;
        if (std::find(missing_.begin(), missing_.end(), key) == missing_.end()) missing_.push_back(key);
    }
    return c;
}

const qb::Value* Animator::animSet() const {
    const qb::Value* v = model_->member.profile ? model_->member.profile->find(K("anim_set")) : nullptr;
    const uint32_t k = keyOf(v);
    return k ? g_->find(k) : nullptr;
}

bool Animator::init(const Setup& s, std::string& error) {
    model_ = s.model;
    g_ = s.g;
    perf_ = s.perf;
    if (!model_ || !model_->skeleton || !g_ || !s.chart) {
        error = "band animator: incomplete setup";
        return false;
    }
    role_ = model_->member.role;
    rig_.init(*model_->skeleton);
    rng_ = 0x6A09E667F3BCC908ull ^ (uint64_t(s.seed) << 17) ^ s.seed;
    fretbars_ = s.chart->fretbars;
    const Globals& g = *g_;
    slowTempo_ = numOf(g, "slow_tempo", 75), medTempo_ = numOf(g, "med_tempo", 115), fastTempo_ = numOf(g, "fast_tempo", 145);
    shortStrum_ = qbInt(g, K("short_strum_max_gem_length"), 150), medStrum_ = qbInt(g, K("med_strum_max_gem_length"), 600);
    fingerPress_ = numOf(g, "finger_press_blend_time", 0.07f), fingerRelease_ = numOf(g, "finger_release_blend_time", 0.2f);
    fretBlend_ = numOf(g, "fret_blend_time", 0.1f), drumBlend_ = numOf(g, "drum_blend_time", 0.1f);
    armBlendOutDelay_ = numOf(g, "drummer_arm_blend_out_delay", 0.25f), twistRate_ = numOf(g, "drummer_twist_rate", 0.05f);
    const double strumLead = qbInt(g, K("strum_anim_lead_time"), 250), drumLead = qbInt(g, K("drum_anim_lead_time"), 150);

    const qb::Struct* prof = model_->member.profile;
    stance_ = nextStance_ = prof ? keyOf(prof->find(K("stance"))) : K("stance_a");
    if (!stance_) stance_ = nextStance_ = K("stance_a");
    currentAnim_ = K("idle");
    cycle_ = true;
    strumStyle_ = prof ? keyOf(prof->find(K("strum_anims"))) : 0;
    fingerAnims_ = prof ? structOf(g.find(keyOf(prof->find(K("finger_anims"))))) : nullptr;
    fretAnims_ = prof ? structOf(g.find(keyOf(prof->find(K("fret_anims"))))) : nullptr;

    // IK chains: the arms of ik_params (Hero_Ik_params) whose target is a guitar hand bone [C data].
    chains_.clear();
    if (guitarLike()) {
        uint32_t ikKey = prof ? keyOf(prof->find(K("ik_params"))) : 0;
        if (!ikKey) ikKey = K("hero_ik_params");
        if (const qb::Value* ik = g.find(ikKey); ik && ik->type == qb::Type::Array && ik->arr)
            for (const qb::Value& e : ik->arr->values) {
                const qb::Struct* st = structOf(&e);
                if (!st) continue;
                const uint32_t tgt = keyOf(st->find(K("bonetarget")));
                if (tgt != K("bone_ik_hand_guitar_l") && tgt != K("bone_ik_hand_guitar_r")) continue;
                Chain c;
                c.b0 = rig_.find(keyOf(st->find(K("bone0"))));
                c.b1 = rig_.find(keyOf(st->find(K("bone1"))));
                c.b2 = rig_.find(keyOf(st->find(K("bone2"))));
                c.target = rig_.find(tgt);
                c.right = tgt == K("bone_ik_hand_guitar_r");
                if (c.b0 >= 0 && c.b1 >= 0 && c.b2 >= 0 && c.target >= 0) chains_.push_back(c);
            }
    }
    // The song's events [C scripts Strum_iterator, FretPos_iterator,
    // FretFingers_iterator, Drum_iterator, Drum_cymbal_iterator, event_iterator].
    fixed_.clear();
    auto add = [&](double t, Ev k, int a, int b, const qb::Struct* p = nullptr) {
        if (t < s.startMs) return;
        Event e;
        e.t = t, e.kind = k, e.a = a, e.b = b, e.params = p;
        fixed_.push_back(e);
    };
    if (guitarLike()) {
        const bool guitar = role_ == band::Role::Guitarist || s.guitarPart;
        const std::vector<ChartNote>& notes = s.chart->notes(guitar ? Part::Guitar : Part::Rhythm, 3);
        for (const ChartNote& n : notes) {
            if (n.length > 0) add(n.time - strumLead, Ev::Strum, n.length, 0);
            add(n.time - strumLead, Ev::Chord, n.frets & 0x1F, n.length);
        }
        for (const ChartEvent& e : s.chart->anim)
            if (guitar ? (e.note >= 118 && e.note <= 127) : (e.note >= 101 && e.note <= 110)) add(e.time - strumLead, Ev::Fret, e.note, 0);
    }
    if (role_ == band::Role::Drummer) {
        for (const ChartEvent& e : s.chart->drums) {
            if ((e.note >= 37 && e.note <= 45) || (e.note >= 49 && e.note <= 57)) {
                add(e.time - drumLead, Ev::DrumArm, e.note, 0);
                add(e.time, Ev::Cymbal, e.note, 0);
            }
        }
    }
    for (const qb::Struct* p : s.performance) {
        const qb::Value* tv = p->find(K("time"));
        const qb::Struct* params = structOf(p->find(K("params")));
        if (!tv || !params || keyOf(params->find(K("name"))) != model_->member.object) continue;
        add(tv->number(), Ev::Perf, 0, 0, p);
    }
    std::stable_sort(fixed_.begin(), fixed_.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
    for (size_t i = 0; i < fixed_.size(); ++i) fixed_[i].seq = i;
    seq_ = fixed_.size() + 1;
    fixedAt_ = 0;

    // create_guitarist / create_bassist / create_drummer / create_vocalist
    // at the member's creation [C scripts].
    const double t0 = s.startMs;
    now_ = t0;
    if (guitarLike()) {
        const uint32_t strumSet = extendKey(role_ == band::Role::Bassist ? K("normal") : strumStyle_, "_Strums");
        if (const qb::Struct* ss = structOf(g.find(strumSet))) rightArm_.play(clip(keyOf(ss->find(K("no_strum_anim")))), t0, 0.2f);
        if (fretAnims_) leftArm_.play(clip(keyOf(fretAnims_->find(K(role_ == band::Role::Bassist ? "track_106" : "track_123")))), t0, fretBlend_);
        if (fingerAnims_) leftHand_.play(clip(keyOf(fingerAnims_->find(K("track_none")))), t0, 0.2f);
    }
    if (role_ == band::Role::Drummer) {
        twistClip_ = clip(K("test_drum_bodytwist_d"));
        schedule(t0 + kFrameMs, Ev::Twist);
    }
    bodyIteration(t0);
    facial(t0);
    return true;
}

// hero_play_tempo_anim_cfunc (0x821090B0 -> 0x82108E58): anim_set.<stance>.
// <anim>.<skill>.<speed>, a random entry other than the last one played;
// skill from player 1's health (< 0.66 bad, > 1.33 good, 0x82108C90),
// speed from the song tempo (0x82108B60: below the mean of slow_tempo and
// med_tempo slow, below the mean of med_tempo and fast_tempo med, else
// fast; the tempo is 60000 / current_song_beat_time) [C].
const anim::Clip* Animator::tempoAnim(uint32_t animKey, double t, uint32_t& key) {
    key = 0;
    const qb::Value* set = animSet();
    const qb::Struct* st = structOf(set);
    if (!st) return nullptr;
    const qb::Struct* stance = structOf(g_->field(*st, stance_));
    const qb::Struct* anims = stance ? structOf(stance->find(animKey)) : nullptr;
    if (!anims) return nullptr;
    const venue::Performance p = perf_ ? perf_->at(t) : venue::Performance::Medium;
    const qb::Struct* bySkill = structOf(anims->find(p == venue::Performance::Poor ? K("bad") : p == venue::Performance::Good ? K("good")
                                                                                                                             : K("normal")));
    if (!bySkill) return nullptr;
    // The beat at t (the fretbars around it) [I: current_song_beat_time is the fretbar spacing].
    double beat = 500;
    if (fretbars_.size() >= 2) {
        size_t i = size_t(std::upper_bound(fretbars_.begin(), fretbars_.end(), int32_t(t)) - fretbars_.begin());
        i = std::clamp<size_t>(i, 1, fretbars_.size() - 1);
        beat = std::max(1, fretbars_[i] - fretbars_[i - 1]);
    }
    const double bpm = 60000.0 / beat;
    const uint32_t speed = bpm < (slowTempo_ + medTempo_) * 0.5 ? K("slow") : bpm < (medTempo_ + fastTempo_) * 0.5 ? K("med") : K("fast");
    const qb::Value* arr = bySkill->find(speed);
    if (!arr || arr->type != qb::Type::Array || !arr->arr || arr->arr->values.empty()) return nullptr;
    const auto& v = arr->arr->values;
    size_t pick = 0;
    if (v.size() > 1) {
        pick = random(uint32_t(v.size()));
        if (keyOf(&v[pick]) == lastAnim_) {
            size_t other = random(uint32_t(v.size() - 1));
            if (other == pick) ++other;
            pick = other;
        }
    }
    key = keyOf(&v[pick]);
    lastAnim_ = key;
    return clip(key);
}

// hero_toggle_arms through hero_play_adjusting_random_anims: disable_arms 2
// both arms off, 1 the strumming arm off, 0 both on; each over 0.25 s [C scripts].
void Animator::setArms(int disable, double t, float blend) {
    if (!guitarLike()) return;
    auto toggle = [&](int num, int prev) {
        bool disL = false, enL = false, disR = false, enR = false;
        if (num == 0) {
            if (prev == 1) disR = true;
            else if (prev == 2) disL = disR = true;
        } else if (num == 1) {
            if (prev == 2) disR = enL = true;
        } else if (num == 2) {
            if (prev == 0) enL = enR = true;
            else if (prev == 1) enR = true;
        }
        if (disL) leftStrength_.set(0, t, blend);
        if (enL) leftStrength_.set(1, t, blend);
        if (disR) rightStrength_.set(0, t, blend);
        if (enR) rightStrength_.set(1, t, blend);
    };
    if (disable == 2 && armsDisabled_ != 2) {
        toggle(0, 2 - armsDisabled_);
        armsDisabled_ = 2;
    } else if (disable == 1 && armsDisabled_ != 1) {
        toggle(1, 2 - armsDisabled_);
        armsDisabled_ = 1;
    } else if (disable == 0 && armsDisabled_ != 0) {
        toggle(2, 2 - armsDisabled_);
        armsDisabled_ = 0;
    }
}

// One pass of hero_play_adjusting_random_anims' loop up to its wait.
void Animator::bodyIteration(double t) {
    setArms(disableArms_, t, 0.25f);
    uint32_t key = 0;
    const anim::Clip* c = tempoAnim(currentAnim_, t, key);
    ++bodyGen_;
    if (c) {
        body_.play(c, t, 0.2f);
        bodyKey_ = key;
        ++stats_.bodyAnims;
        schedule(t + std::max(kFrameMs, double(c->duration) * 1000.0), Ev::BodyDone, 0, 0, bodyGen_);
    } else {
        schedule(t + kFrameMs, Ev::BodyDone, 0, 0, bodyGen_);  // Wait 1 gameframe [C script]
    }
}

void Animator::bodyFinished(double t) {
    if (nextStance_ != stance_) {
        // play_stance_transition_cfunc: anim_set.<old>.transitions.<new> [I: the C function's lookup].
        const anim::Clip* tr = nullptr;
        if (const qb::Struct* st = structOf(animSet()))
            if (const qb::Struct* old = structOf(g_->field(*st, stance_)))
                if (const qb::Struct* trs = structOf(old->find(K("transitions")))) tr = clip(keyOf(trs->find(nextStance_)));
        if (tr) {
            body_.play(tr, t, 0.2f);
            ++bodyGen_;
            schedule(t + std::max(kFrameMs, double(tr->duration) * 1000.0), Ev::TransitionDone, 0, 0, bodyGen_);
            return;
        }
        stance_ = nextStance_;
        afterStance(t, true);
        return;
    }
    afterStance(t, false);
}

void Animator::afterStance(double t, bool stanceChanged) {
    const bool introish = nextStance_ == K("intro") || nextStance_ == K("intro_smstg") || nextStance_ == K("stance_frontend") ||
                          nextStance_ == K("stance_frontend_guitar");
    if (nextAnim_ == 0 && !stanceChanged) {
        if (!cycle_) {
            repeat_ -= 1;
            if (repeat_ < 1) repeat_ = 0;
        }
        if (!cycle_ && repeat_ <= 0) {
            currentAnim_ = K("idle");
            cycle_ = true;
            if (!introish) disableArms_ = 0;
        }
    } else {
        repeat_ = nextRepeat_;
        uint32_t next = nextAnim_;
        bool cycleNext = cycleNext_;
        if (next == 0) next = K("idle"), cycleNext = true;
        stance_ = nextStance_;
        currentAnim_ = next;
        cycle_ = cycleNext;
        disableArms_ = introish ? 2 : nextDisableArms_;
        nextAnim_ = 0;
        cycleNext_ = true;
        nextDisableArms_ = 0;
    }
    bodyIteration(t);
}

// handle_play_anim [C script].
void Animator::playAnimEvent(const qb::Struct& p, double t) {
    const uint32_t a = keyOf(p.find(K("anim")));
    const qb::Value* rc = p.find(K("repeat_count"));
    const int repeat = rc ? int(rc->number()) : 1;
    const bool cycle = hasFlag(p, K("cycle"));
    int arms = 0;
    if (hasFlag(p, K("disable_auto_arms"))) arms = 2;
    else if (hasFlag(p, K("disable_auto_strum"))) arms = 1;
    if (hasFlag(p, K("no_wait"))) {
        currentAnim_ = a ? a : K("idle");
        nextAnim_ = 0;
        repeat_ = repeat;
        cycle_ = cycle;
        if (guitarLike()) disableArms_ = arms;
        bodyIteration(t);  // obj_killspawnedscript + obj_spawnscriptnow
    } else {
        nextAnim_ = a ? a : K("idle");
        nextRepeat_ = repeat;
        cycleNext_ = cycle;
        if (guitarLike()) nextDisableArms_ = arms;
    }
}

// handle_change_stance [C script].
void Animator::changeStanceEvent(uint32_t stance, bool noWait, double t) {
    const bool introish = stance == K("intro") || stance == K("intro_smstg") || stance == K("stance_frontend") ||
                          stance == K("stance_frontend_guitar");
    if (noWait) {
        nextStance_ = stance;
        if (guitarLike()) disableArms_ = nextDisableArms_ = introish ? 2 : 0;
        bodyFinished(t);  // the transition now, then the idle loop
    } else {
        nextStance_ = stance;
        if (guitarLike()) nextDisableArms_ = introish ? 2 : 0;
    }
}

// hero_strum_guitar [C script].
void Animator::strum(int length, double t) {
    lastStrumLength_ = length < shortStrum_ ? 0 : length < medStrum_ ? 1 : 2;
    const uint32_t len = lastStrumLength_ == 0 ? K("short") : lastStrumLength_ == 1 ? K("med") : K("long");
    const qb::Struct* set = structOf(g_->find(extendKey(strumStyle_, "_Strums")));
    if (!set) return;
    const qb::Value* arr = set->find(len);
    ++strumGen_;
    if (!missed_ && arr && arr->type == qb::Type::Array && arr->arr && !arr->arr->values.empty()) {
        const auto& v = arr->arr->values;
        const anim::Clip* c = clip(keyOf(&v[random(uint32_t(v.size()))]));
        if (c) {
            rightArm_.play(c, t, 0.1f);
            ++stats_.strums;
        }
    }
    // hero_wait_until_anim_finished StrumTimer, then the no-strum pose [C script].
    const double end = rightArm_.plays.empty() ? t : std::max(t, rightArm_.plays.back().end());
    schedule(end, Ev::StrumDone, 0, 0, strumGen_);
}

// facial_anim_loop: anim_set.facial_anims.idle at random, one after another [C script].
void Animator::facial(double t) {
    ++faceGen_;
    const qb::Struct* set = structOf(animSet());
    const qb::Struct* fa = set ? structOf(set->find(K("facial_anims"))) : nullptr;
    const qb::Value* idle = fa ? fa->find(K("idle")) : nullptr;
    if (!idle || idle->type != qb::Type::Array || !idle->arr || idle->arr->values.empty()) return;
    const auto& v = idle->arr->values;
    const anim::Clip* c = clip(keyOf(&v[random(uint32_t(v.size()))]));
    face_.play(c, t, 0);
    ++stats_.facials;
    // hero_wait_until_anim_finished, then Wait 1 gameframe [C script].
    schedule(t + (c ? double(c->duration) * 1000.0 : 0.0) + kFrameMs, Ev::FaceDone, 0, 0, faceGen_);
}

void Animator::handle(const Event& e) {
    const double t = e.t;
    switch (e.kind) {
    case Ev::Strum: strum(e.a, t); break;
    case Ev::StrumDone:
        if (e.gen == strumGen_)
            if (const qb::Struct* set = structOf(g_->find(extendKey(strumStyle_, "_Strums"))))
                rightArm_.play(clip(keyOf(set->find(K("no_strum_anim")))), t, 0.2f);
        break;
    case Ev::Fret:
        if (fretAnims_) {
            char name[16];
            std::snprintf(name, sizeof name, "track_%d", e.a);
            if (const anim::Clip* c = clip(keyOf(fretAnims_->find(K(name))))) {
                leftArm_.play(c, t, fretBlend_);
                ++stats_.frets;
            }
        }
        break;
    case Ev::Chord: {
        // FretFingers_iterator_CFunc (0x821119F8): the gem's colours joined by
        // '_' (at most four); released finger_note_padding s after the gem's
        // length unless a new gem came [C].
        if (!fingerAnims_) break;
        static const char* kColors[5] = {"green", "red", "yellow", "blue", "orange"};
        std::string chord;
        int n = 0;
        for (int i = 0; i < 5 && n < 4; ++i)
            if (e.a & (1 << i)) chord += (n++ ? "_" : "") + std::string(kColors[i]);
        ++chordGen_;
        if (chord.empty()) break;
        const qb::Value* v = fingerAnims_->find(qbkey::key(chord));
        const anim::Clip* c = clip(keyOf(v ? v : fingerAnims_->find(K("none"))));
        leftHand_.play(c, t, v ? fingerPress_ : fingerRelease_);
        ++stats_.chords;
        schedule(t + double(e.b) + double(numOf(*g_, "finger_note_padding", 0.01f)) * 1000.0, Ev::ChordRelease, 0, 0, chordGen_);
        break;
    }
    case Ev::ChordRelease:
        if (e.gen == chordGen_ && fingerAnims_) leftHand_.play(clip(keyOf(fingerAnims_->find(K("none")))), t, fingerRelease_);
        break;
    case Ev::Perf: {
        ++stats_.perfEvents;
        const uint32_t scr = keyOf(e.params->find(K("scr")));
        const qb::Struct* p = structOf(e.params->find(K("params")));
        if (!p) break;
        if (scr == K("band_changestance")) {
            uint32_t st = keyOf(p->find(K("stance")));
            changeStanceEvent(st ? st : K("stance_a"), hasFlag(*p, K("no_wait")), t);
        } else if (scr == K("band_playanim")) {
            playAnimEvent(*p, t);
        } else if (scr == K("band_playidle")) {
            qb::Struct idle;
            idle.items.push_back({K("anim"), qb::Value{}});
            idle.items.back().value.type = qb::Type::Key;
            idle.items.back().value.key = K("idle");
            qb::Item flag;
            flag.value.type = qb::Type::Key;
            flag.value.key = K("cycle");
            idle.items.push_back(flag);
            playAnimEvent(idle, t);
        } else if (scr == K("band_playfacialanim")) {
            // play_special_facial_anim: the anim, then the loop again [C script].
            const anim::Clip* c = clip(keyOf(p->find(K("anim"))));
            ++faceGen_;
            face_.play(c, t, 0);
            ++stats_.facials;
            schedule(t + (c ? double(c->duration) * 1000.0 : 0.0), Ev::FaceDone, 1, 0, faceGen_);
        }
        // Band_WalkToNode: not reproduced (docs/band.md "Gaps").
        break;
    }
    case Ev::BodyDone:
        if (e.gen == bodyGen_) bodyFinished(t);
        break;
    case Ev::TransitionDone:
        if (e.gen == bodyGen_) {
            stance_ = nextStance_;
            afterStance(t, true);
        }
        break;
    case Ev::FaceDone:
        if (e.gen == faceGen_) facial(t);
        break;
    case Ev::DrumArm: {
        // Drum_iterator_CFunc (0x821121D8): notes 37..45 the left arm, 49..57
        // the right; Drummer_anims.track_<note>; last_<arm>_arm_note for the
        // twist [C]. play_drummer_<arm>_arm_anim: the hit, then after
        // drummer_arm_blend_out_delay the sticks down over 0.6 s [C script].
        const int arm = e.a <= 45 ? 0 : 1;
        char name[16];
        std::snprintf(name, sizeof name, "track_%d", e.a);
        const qb::Struct* da = structOf(g_->find(K("drummer_anims")));
        const qb::Value* arr = da ? da->find(K(name)) : nullptr;
        if (!arr || arr->type != qb::Type::Array || !arr->arr || arr->arr->values.empty()) break;
        const anim::Clip* c = clip(keyOf(&arr->arr->values[random(uint32_t(arr->arr->values.size()))]));
        if (!c) break;
        drumArm_[arm].play(c, t, drumBlend_);
        ++stats_.drumHits;
        lastArmNote_[arm] = e.a;
        schedule(t + double(c->duration) * 1000.0 + double(armBlendOutDelay_) * 1000.0, Ev::ArmDone, arm, 0, ++armGen_[arm]);
        break;
    }
    case Ev::ArmDone:
        if (e.gen == armGen_[e.a]) {
            const qb::Struct* da = structOf(g_->find(K("drummer_anims")));
            drumArm_[e.a].play(clip(da ? keyOf(da->find(K(e.a ? "stickdown_r" : "stickdown_l"))) : 0), t, 0.6f);
            lastArmNote_[e.a] = 0;  // drummer_clear_arm_twist_value_delay 0
        }
        break;
    case Ev::Cymbal: {
        // Drum_cymbal_iterator: 44/56 cymbal1, 45/57 cymbal2, 43/55 cymbal3,
        // 41/53 and 42/54 cymbal4, cymbal_anims[0..3] [C script].
        const int n = e.a >= 49 ? e.a - 12 : e.a;
        const int which = n == 44 ? 0 : n == 45 ? 1 : n == 43 ? 2 : (n == 41 || n == 42) ? 3 : -1;
        if (which < 0) break;
        const qb::Value* ca = g_->find(K("cymbal_anims"));
        if (!ca || ca->type != qb::Type::Array || !ca->arr || size_t(which) >= ca->arr->values.size()) break;
        if (const anim::Clip* c = clip(keyOf(&ca->arr->values[size_t(which)]))) {
            cymbal_[which].play(c, t, drumBlend_);
            ++stats_.cymbals;
        }
        break;
    }
    case Ev::Twist: {
        // drummer_autotwist, once a game frame: the twist moves
        // drummer_twist_rate toward compute_desired_drummer_twist (0x82107F48:
        // the mean of the two arms' twist factors of their last notes) [C].
        auto factor = [&](int arm) -> float {
            const int n = lastArmNote_[arm];
            if (arm == 0 ? !(n > 36 && n < 46) : !(n > 48 && n < 58)) return 0;
            const qb::Struct* f = structOf(g_->find(K(arm == 0 ? "drummer_left_arm_twist_factors" : "drummer_right_arm_twist_factors")));
            char name[16];
            std::snprintf(name, sizeof name, "track_%d", n);
            const qb::Value* v = f ? f->find(K(name)) : nullptr;
            return v ? v->number() : 0;
        };
        const float desired = (factor(0) + factor(1)) * 0.5f;
        const float diff = desired - twist_;
        if (std::fabs(diff) < twistRate_) twist_ = desired;
        else twist_ += diff > 0 ? twistRate_ : -twistRate_;
        schedule(t + kFrameMs, Ev::Twist);
        break;
    }
    }
}

void Animator::playerNote(double ms, bool hit) {
    if (!guitarLike()) return;
    advance(ms);
    if (hit) {
        missed_ = false;
        return;
    }
    if (missed_) return;
    missed_ = true;
    if (const qb::Struct* bad = structOf(g_->find(K("bad_strums")))) {
        const qb::Value* arr = bad->find(lastStrumLength_ == 0 ? K("short") : lastStrumLength_ == 1 ? K("med") : K("long"));
        if (arr && arr->type == qb::Type::Array && arr->arr && !arr->arr->values.empty())
            if (const anim::Clip* c = clip(keyOf(&arr->arr->values[0]))) rightArm_.play(c, ms, 0.1f);
    }
}

void Animator::advance(double ms) {
    for (;;) {
        const bool haveFixed = fixedAt_ < fixed_.size();
        const bool haveQueued = !queue_.empty();
        if (!haveFixed && !haveQueued) break;
        bool takeFixed = haveFixed;
        if (haveFixed && haveQueued) takeFixed = !(queue_.top() > fixed_[fixedAt_]);
        const Event e = takeFixed ? fixed_[fixedAt_] : queue_.top();
        if (e.t > ms) break;
        if (takeFixed) ++fixedAt_;
        else queue_.pop();
        handle(e);
    }
    now_ = std::max(now_, ms);
}

void Animator::evaluate(double ms, std::vector<Mat34>& world, std::vector<Mat34>& skin) const {
    std::vector<anim::BonePose> tmp;
    Pose pose = rig_.bind;
    evalNode(rig_, body_, ms, pose, true, tmp);
    evalNode(rig_, face_, ms, pose, false, tmp);
    if (guitarLike()) {
        const float l = leftStrength_.at(ms), r = rightStrength_.at(ms);
        evalDelta(rig_, leftArm_, ms, l, pose, tmp);
        evalDelta(rig_, leftHand_, ms, l, pose, tmp);
        evalDelta(rig_, rightArm_, ms, r, pose, tmp);
    }
    if (role_ == band::Role::Drummer) {
        for (const Node& n : drumArm_) evalNode(rig_, n, ms, pose, false, tmp);
        for (const Node& n : cymbal_) evalNode(rig_, n, ms, pose, false, tmp);
        if (twistClip_ && twist_ != 0) {
            Node tw;
            tw.plays.push_back({twistClip_, 0, 0});
            evalDelta(rig_, tw, ms, twist_, pose, tmp);
        }
    }
    worldOf(rig_, pose, world);

    // Two-bone IK of the arms onto the guitar's hand bones [C data
    // Hero_Ik_params; I: the bend plane through the animated elbow, the palm
    // taking the target's orientation].
    if (!chains_.empty()) {
        bool changed = false;
        for (const Chain& c : chains_) {
            const float s = c.right ? rightStrength_.at(ms) : leftStrength_.at(ms);
            if (s <= 0) continue;
            const Vec S = posOf(world[size_t(c.b0)]), E = posOf(world[size_t(c.b1)]), W = posOf(world[size_t(c.b2)]);
            const Vec T = posOf(world[size_t(c.target)]);
            const float a = vlen(vsub(E, S)), b = vlen(vsub(W, E));
            if (a < 1e-5f || b < 1e-5f) continue;
            const Vec toT = vsub(T, S);
            const float d = std::clamp(vlen(toT), std::fabs(a - b) + 1e-4f, a + b - 1e-4f);
            const Vec n = vnorm(toT);
            Vec pole = vsub(E, S);
            pole = vsub(pole, vmul(n, vdot(pole, n)));
            if (vlen(pole) < 1e-6f) pole = vcross(n, {0, 1, 0});
            pole = vnorm(pole);
            const float cosA = std::clamp((a * a + d * d - b * b) / (2 * a * d), -1.0f, 1.0f);
            const float sinA = std::sqrt(std::max(0.0f, 1 - cosA * cosA));
            const Vec E2 = vadd(S, vadd(vmul(n, a * cosA), vmul(pole, a * sinA)));
            const Vec T2 = vadd(S, vmul(n, d));
            const Quat r0 = between(vsub(E, S), vsub(E2, S));
            const Quat w0 = qmul(r0, rotOf(world[size_t(c.b0)]));
            const Vec W1 = vadd(S, qrot(r0, vsub(W, S)));
            const Quat r1 = between(vsub(W1, E2), vsub(T2, E2));
            const Quat w1 = qmul(r1, qmul(r0, rotOf(world[size_t(c.b1)])));
            const Quat w2 = rotOf(world[size_t(c.target)]);
            // Back to local rotations, blended with the animated ones by the chain's strength.
            const int p0 = rig_.parent[size_t(c.b0)];
            const Quat parentRot = p0 >= 0 ? rotOf(world[size_t(p0)]) : Quat{};
            Quat l0 = qmul(qconj(parentRot), w0), l1 = qmul(qconj(w0), w1), l2 = qmul(qconj(w1), w2);
            pose[size_t(c.b0)].q = nlerp(pose[size_t(c.b0)].q, l0, s);
            pose[size_t(c.b1)].q = nlerp(pose[size_t(c.b1)].q, l1, s);
            pose[size_t(c.b2)].q = nlerp(pose[size_t(c.b2)].q, l2, s);
            changed = true;
        }
        if (changed) worldOf(rig_, pose, world);
    }
    skin.resize(world.size());
    for (size_t b = 0; b < world.size(); ++b) skin[b] = mmul(world[b], rig_.invBind[b]);
}

}  // namespace bandanim
