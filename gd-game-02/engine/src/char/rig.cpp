// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/rig.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "char/char_driver.h"

// The pollables of a character directory, ported from the Xbox 360 code
// (docs/band.md "Pose"). Transforms are scene::Xfm (row vectors, rows are
// the local axes, p' = p rot + pos), the same layout as GH2's Hmx::Matrix3 /
// Transform: GH2's Multiply(a, b, out) (0x8216DB48) is compose(a, b).
// "Local" below is RndTransformable +0x1c, "world" its WorldXfm (0x821AF988);
// SetWorldXfm (0x821AF870) is Skeleton::setWorld.
namespace chr {

namespace {

using V3 = std::array<float, 3>;
using Q4 = std::array<float, 4>;  // x y z w
using M3 = std::array<float, 9>;

constexpr float kPi = 3.14159265f;
constexpr float kDeg = 0.017453292f;
constexpr float kHuge = 1e30f;

V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 add(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V3 mul(const V3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
float len(const V3& a) { return std::sqrt(dot(a, a)); }
// FUN_8214F728: normalise (unchanged when zero [I]).
V3 normalize(const V3& a) {
    const float l = len(a);
    return l > 0 ? mul(a, 1.0f / l) : a;
}
// FUN_82122100: a + (b - a) t (exact a / b at t = 0 / 1).
V3 lerp(const V3& a, const V3& b, float t) {
    if (t == 0) return a;
    if (t == 1) return b;
    return {(b[0] - a[0]) * t + a[0], (b[1] - a[1]) * t + a[1], (b[2] - a[2]) * t + a[2]};
}
V3 row(const M3& m, int r) { return {m[size_t(r * 3)], m[size_t(r * 3 + 1)], m[size_t(r * 3 + 2)]}; }
void setRow(M3& m, int r, const V3& v) {
    for (int k = 0; k < 3; ++k) m[size_t(r * 3 + k)] = v[size_t(k)];
}
// compose of rotations: a then b (0x8216DB48).
M3 mulRot(const M3& a, const M3& b) {
    M3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0;
            for (int k = 0; k < 3; ++k) s += a[size_t(i * 3 + k)] * b[size_t(k * 3 + j)];
            r[size_t(i * 3 + j)] = s;
        }
    return r;
}
// v rot (row vector).
V3 vecRot(const V3& v, const M3& m) {
    return {v[0] * m[0] + v[1] * m[3] + v[2] * m[6], v[0] * m[1] + v[1] * m[4] + v[2] * m[7], v[0] * m[2] + v[1] * m[5] + v[2] * m[8]};
}
// v rot^T (0x82122258 without the translation: into the frame's axes).
V3 vecRotT(const V3& v, const M3& m) { return {dot(v, row(m, 0)), dot(v, row(m, 1)), dot(v, row(m, 2))}; }

// FUN_8226A190: quaternion -> rotation (rows), for row vectors.
M3 quatToM(const Q4& q) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    return {1 - 2 * y * y - 2 * z * z, 2 * x * y + 2 * w * z, 2 * x * z - 2 * w * y,
            2 * x * y - 2 * w * z, 1 - 2 * x * x - 2 * z * z, 2 * y * z + 2 * w * x,
            2 * x * z + 2 * w * y, 2 * y * z - 2 * w * x, 1 - 2 * x * x - 2 * y * y};
}
// 0x82269B20: rotation -> quaternion (the inverse of quatToM) [I: the branch order].
Q4 mToQuat(const M3& m) {
    Q4 q;
    const float tr = m[0] + m[4] + m[8];
    if (tr > 0) {
        const float s = std::sqrt(tr + 1) * 2;
        q = {(m[5] - m[7]) / s, (m[6] - m[2]) / s, (m[1] - m[3]) / s, 0.25f * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1 + m[0] - m[4] - m[8]) * 2;
        q = {0.25f * s, (m[1] + m[3]) / s, (m[6] + m[2]) / s, (m[5] - m[7]) / s};
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1 + m[4] - m[0] - m[8]) * 2;
        q = {(m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s, (m[6] - m[2]) / s};
    } else {
        const float s = std::sqrt(1 + m[8] - m[0] - m[4]) * 2;
        q = {(m[6] + m[2]) / s, (m[5] + m[7]) / s, 0.25f * s, (m[1] - m[3]) / s};
    }
    return q;
}
Q4 qnormalize(Q4 q) {
    const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l > 0)
        for (float& c : q) c /= l;
    return q;
}
// FUN_82269E48: slerp along the shorter arc [I: GH2's exact slerp].
Q4 slerp(const Q4& a, Q4 b, float t) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0) {
        for (float& c : b) c = -c;
        d = -d;
    }
    float ka = 1 - t, kb = t;
    if (d < 0.9995f) {
        const float th = std::acos(std::min(d, 1.0f)), s = std::sin(th);
        ka = std::sin((1 - t) * th) / s, kb = std::sin(t * th) / s;
    }
    return qnormalize({a[0] * ka + b[0] * kb, a[1] * ka + b[1] * kb, a[2] * ka + b[2] * kb, a[3] * ka + b[3] * kb});
}
// FUN_8226A448: the shortest rotation taking direction a to direction b.
Q4 rotBetween(const V3& a, const V3& b) {
    const float l = std::sqrt(dot(a, a) * dot(b, b));
    if (l <= 0) return {0, 0, 0, 1};
    const float w = std::sqrt((dot(a, b) / l + 1) * 0.5f);
    if (w <= 1e-7f) return {0, 0, 1, 0};
    const float k = 0.5f / (w * l);
    const V3 c = cross(a, b);
    return {c[0] * k, c[1] * k, c[2] * k, w};
}
// FUN_82172C70: q += w x r, sign-aligned (|w| on x y z, w on w).
void quatAccumulate(Q4& q, const Q4& r, float w) {
    const float aw = std::fabs(w);
    const Q4 s = {r[0] * aw, r[1] * aw, r[2] * aw, r[3] * w};
    const float d = q[0] * s[0] + q[1] * s[1] + q[2] * s[2] + q[3] * s[3];
    for (int k = 0; k < 4; ++k) q[size_t(k)] += d < 0 ? -s[size_t(k)] : s[size_t(k)];
}
// 0x8217B1C0: a rotation about x (the whole matrix).
M3 rotX(float a) {
    const float c = std::sin(a + kPi * 0.5f), s = std::sin(a);
    return {1, 0, 0, 0, c, s, 0, -s, c};
}
// Function_82161C70: an angle into (-pi, pi] [I: the range].
float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a <= -kPi) a += 2 * kPi;
    return a;
}
// The twist of a rotation about its own x axis (0x82182738 / 0x82181980):
// FUN_8226A528 makes the swing taking row 0 back onto x, Function_8226A5A0
// turns row 1 by it, and the twist is atan2(z, y) of that (FUN_8239E050).
float twistAboutX(const M3& m) {
    const V3 r0 = row(m, 0);
    Q4 q;
    const float s = std::sqrt((r0[0] + 1) * 0.5f);
    if (s <= 1e-7f) q = {0, 0, 1, 0};
    else q = {0, r0[2] * (0.5f / s), -(r0[1] * (0.5f / s)), s};
    const V3 v = vecRot(row(m, 1), quatToM(q));
    return std::atan2(v[2], v[1]);
}
// Function_8214F7F0: an orthonormal frame keeping row 1's direction, row 2
// as the hint: row0 = normalize(row1 x hint), row2 = row0 x row1.
M3 orthoY(const V3& y, const V3& zHint) {
    M3 m{};
    const V3 r1 = normalize(y);
    const V3 r0 = normalize(cross(r1, zHint));
    setRow(m, 0, r0);
    setRow(m, 1, r1);
    setRow(m, 2, cross(r0, r1));
    return m;
}

}  // namespace

// ---- the components ------------------------------------------------------

struct Rig::Impl {
    Skeleton& sk;
    const scene::Directory& dir;
    Random rng{0x45594553u};

    struct Weight {
        float weight = 1;
        std::string owner;
    };
    std::unordered_map<std::string, Weight> weights;

    float weightOf(const std::string& name) const {
        std::string n = name;
        for (int depth = 0; depth < 8; ++depth) {
            auto it = weights.find(n);
            if (it == weights.end()) return 1;
            if (it->second.owner.empty() || it->second.owner == n) return it->second.weight;
            n = it->second.owner;
        }
        return 1;
    }

    // CharWeightSetter (Poll 0x8218CFB0): its weight = the driver's eased
    // weight of clips with its flags (0x8216A588), 1 - that when inverted.
    struct Setter {
        std::string name, driver;
        uint32_t flags = 0;
        bool invert = false;
    };
    // CharIKHand (Poll 0x82173308, lengths 0x82172F38).
    struct IkHand {
        std::string name;
        int hand = -1, finger = -1;
        std::vector<int> targets;
        bool orient = false, stretch = false, everyFrame = false, moveElbow = true;
        bool lengthsDirty = true;
        float inv2ab = 0, a2b2 = 0;  // +0x60 / +0x64: 1 / (2ab), a^2 + b^2 (forearm a, upper arm b)
    };
    // CharIKMidi (Handle 0x821838E8 / new spot 0x82183700, Poll 0x82183410).
    struct IkMidi {
        std::string name;
        int bone = -1, spot = -1;
        scene::Xfm rel;            // +0x20: the start, in the spot's parent frame
        scene::Xfm last;           // +0x60: the bone's world after the last poll
        bool hasLast = false;
        float frac = 0, rate = 0;  // +0xa0 / +0xa4
    };
    // CharUpperTwist (Poll 0x82182738).
    struct UpperTwist {
        std::string name;
        int upperArm = -1, twist1 = -1, twist2 = -1;
    };
    // CharForeTwist (Poll 0x82181980).
    struct ForeTwist {
        std::string name;
        float offset = 0;
        int hand = -1, twist2 = -1;
    };
    // CharLookAt (Poll 0x8216E4E8, limits 0x8216DDF0).
    struct LookAt {
        std::string name;
        int source = -1, pivot = -1, dest = -1;
        bool destPoint = false;    // CharEyes' own target (a point, 0x82170130)
        V3 point{};
        float halfTime = 0;        // +0x40 (f[0])
        float fadeMin = -1, fadeMax = -1, fadeRate = 10000;  // +0x54 / +0x58 / +0x5c (f[5..7]); off when fadeMin < 0
        V3 boxMin{}, boxMax{};     // +0x74 / +0x84: the direction's box in the parent's frame
        V3 filtered{};             // +0x60
        bool hasFiltered = false;
        float fade = 0;            // +0x70
        bool allowRoll = false;    // +0x95
    };
    // CharEyes (Poll 0x82170BA8, retarget 0x82170130, Enter 0x82170078).
    struct Eyes {
        std::string name;
        std::vector<size_t> lookats;
        int head = -1;             // the first look-at's pivot's parent
        V3 lastForward{};          // +0x3c
        float lastDot = 1;         // +0x4c (Enter: 1)
        float timer = 0;           // +0x50 seconds
        float threshold = kHuge;   // +0x54 [I: its value; never reached]
        float smoothed = 0;        // +0x58
        std::vector<std::pair<int, float>> interests;  // (object, radius); none on the disc
    };
    // CharHair (Poll 0x8217B898, reset 0x8217B6F0).
    struct Hair {
        std::string name;
        std::array<float, 6> p{};  // +0x08..+0x1c: stiffness, smoothing, inertia, gravity, weight, friction
        struct Point {
            V3 pos{}, vel{}, prevVel{}, up{};
            int bone = -1, collide = -1, type = 0;
            float length = 0, radius = 0, extra = 0;
        };
        struct Strand {
            int root = -1;
            M3 base{};
            std::vector<Point> points;
        };
        std::vector<Strand> strands;
        bool simulate = false, reset = true;
    };
    // CharPosConstraint (Poll 0x82178B28).
    struct PosConstraint {
        std::string name;
        int src = -1;
        std::vector<int> targets;
        V3 boxMin{}, boxMax{};
    };
    // CharIKRod (Poll 0x821805C0, frame 0x8217FFF0).
    struct IkRod {
        std::string name;
        int left = -1, right = -1, side = -1, dest = -1;
        float destPos = 0;
        bool vertical = false;
        scene::Xfm rel;
    };

    std::vector<Setter> setters;
    std::vector<IkHand> hands;
    std::vector<IkMidi> midis;
    std::vector<UpperTwist> uppers;
    std::vector<ForeTwist> fores;
    std::vector<LookAt> lookats;
    std::vector<Eyes> eyes;
    std::vector<Hair> hairs;
    std::vector<PosConstraint> constraints;
    std::vector<IkRod> rods;

    enum Kind { kDriver, kServo, kSetter, kHand, kMidi, kUpper, kFore, kLookAt, kEyes, kHair, kConstraint, kRod };
    struct Pollable {
        Kind kind;
        size_t index;
        std::string name;
        std::vector<std::string> reads, writes;  // PollDeps changedBy / change (object names)
        bool writesAllBones = false;              // the servo
        bool enabled = true;
    };
    std::vector<Pollable> pollables;  // sorted
    std::vector<std::string> driverNames;

    Impl(const scene::Directory& d, Skeleton& s) : sk(s), dir(d) {}

    int bone(const std::string& n) const { return n.empty() ? -1 : sk.find(n); }
    std::string nameOf(int i) const { return i >= 0 ? sk.name(i) : std::string(); }
    std::string parentName(int i) const { return i >= 0 && sk.parent(i) >= 0 ? sk.name(sk.parent(i)) : std::string(); }

    void build();
    void sort();
    void poll(const RigContext& ctx);

    void pollSetter(Setter& s, const RigContext& ctx);
    void lengths(IkHand& h);
    void pollHand(IkHand& h);
    void newSpot(IkMidi& m, int spot, double duration);
    void pollMidi(IkMidi& m, const RigContext& ctx);
    void pollUpper(UpperTwist& u);
    void pollFore(ForeTwist& f);
    void pollLookAt(LookAt& l, const RigContext& ctx);
    void retarget(Eyes& e);
    void pollEyes(Eyes& e, const RigContext& ctx);
    void resetHair(Hair& h);
    void pollHair(Hair& h);
    void pollConstraint(PosConstraint& c);
    void pollRod(IkRod& r);
};

void Rig::Impl::build() {
    for (const auto& o : dir.objects) {
        Pollable p;
        p.name = o->name;
        if (auto* d = o->as<scene::CharDriver>()) {
            weights[o->name] = {d->weightable.weight, d->weightable.weightOwner};
            // 0x8216C668: changes its bones (the servo).
            p.kind = kDriver, p.index = driverNames.size();
            driverNames.push_back(o->name);
            p.writes = {d->bones};
        } else if (o->as<scene::CharServoBone>()) {
            p.kind = kServo, p.index = 0;
            p.writesAllBones = true;
        } else if (auto* s = o->as<scene::CharWeightSetter>()) {
            weights[o->name] = {s->weightable.weight, s->weightable.weightOwner};
            setters.push_back({o->name, s->driver, s->flags, s->invert});
            p.kind = kSetter, p.index = setters.size() - 1;
            // 0x8218DE98: changed by its driver; changes the weightables it owns.
            p.reads = {s->driver};
        } else if (auto* h = o->as<scene::CharIKHand>()) {
            weights[o->name] = {h->weightable.weight, h->weightable.weightOwner};
            IkHand k;
            k.name = o->name;
            k.hand = bone(h->hand), k.finger = bone(h->finger);
            for (const auto& t : h->targets)
                if (int b = bone(t); b >= 0) k.targets.push_back(b);
            k.orient = h->orient, k.stretch = h->stretch, k.everyFrame = h->scaleX, k.moveElbow = h->moveElbow;
            hands.push_back(k);
            p.kind = kHand, p.index = hands.size() - 1;
            // 0x82174BD8: changes hand and finger, changed by them and the
            // targets; the elbow chain it also moves is added [I: GH2 lists
            // only the hand, the twists reading the upper arm must follow].
            p.reads = {h->hand, h->finger};
            p.reads.insert(p.reads.end(), h->targets.begin(), h->targets.end());
            p.writes = {h->hand, h->finger, parentName(k.hand), parentName(sk.parent(k.hand) >= 0 ? sk.parent(k.hand) : -1)};
        } else if (auto* m = o->as<scene::CharIKMidi>()) {
            IkMidi k;
            k.name = o->name;
            k.bone = bone(m->bone);
            midis.push_back(k);
            p.kind = kMidi, p.index = midis.size() - 1;
            // 0x82184200: changes the bone, changed by the current spot [I:
            // the spots' parent stands in for it, no spot is set at sort time].
            p.writes = {m->bone};
            p.reads = {parentName(k.bone)};
        } else if (auto* u = o->as<scene::CharUpperTwist>()) {
            uppers.push_back({o->name, bone(u->upperArm), bone(u->twist1), bone(u->twist2)});
            p.kind = kUpper, p.index = uppers.size() - 1;
            p.reads = {u->upperArm};  // 0x82182F80
            p.writes = {u->twist1, u->twist2};
        } else if (auto* f = o->as<scene::CharForeTwist>()) {
            fores.push_back({o->name, f->offset, bone(f->hand), bone(f->twist2)});
            p.kind = kFore, p.index = fores.size() - 1;
            p.reads = {f->hand};  // 0x82182398
            p.writes = {f->twist2, parentName(bone(f->twist2))};
        } else if (auto* l = o->as<scene::CharLookAt>()) {
            weights[o->name] = {l->weightable.weight, l->weightable.weightOwner};
            LookAt k;
            k.name = o->name;
            k.source = bone(l->source), k.pivot = bone(l->pivot), k.dest = bone(l->dest);
            k.halfTime = l->values[0];
            k.fadeMin = l->values[5], k.fadeMax = l->values[6], k.fadeRate = l->values[7];
            k.allowRoll = l->allowRoll;
            // 0x8216DDF0: yaw / pitch limits clamped to +-80 degrees; c =
            // cos of the largest; the box is c tan(limit) on x (pitch) and z
            // (yaw), y >= c [C structure; I: FUN_8239DE90 cos, FUN_8239D8C0 tan].
            float lim[4];
            for (int i = 0; i < 4; ++i) lim[i] = std::clamp(l->values[size_t(1 + i)], -80.0f, 80.0f);
            const float most = std::max(std::max(std::fabs(lim[0]), std::fabs(lim[1])), std::max(std::fabs(lim[2]), std::fabs(lim[3])));
            const float c = std::cos(most * kDeg);
            k.boxMin = {c * std::tan(lim[2] * kDeg), c, c * std::tan(lim[0] * kDeg)};
            k.boxMax = {c * std::tan(lim[3] * kDeg), kHuge, c * std::tan(lim[1] * kDeg)};
            lookats.push_back(k);
            p.kind = kLookAt, p.index = lookats.size() - 1;
            // 0x8216FA08: changed by the pivot (else the source), changes the source.
            p.reads = {l->pivot.empty() ? l->source : l->pivot};
            p.writes = {l->source};
        } else if (auto* e = o->as<scene::CharEyes>()) {
            Eyes k;
            k.name = o->name;
            for (const auto& [obj, v] : e->interests)
                if (int b = bone(obj); b >= 0) k.interests.emplace_back(b, float(v));
            eyes.push_back(k);
            p.kind = kEyes, p.index = eyes.size() - 1;
            // 0x82170FD0: changed by its look-ats and the head.
            p.reads = e->eyes;
        } else if (auto* h = o->as<scene::CharHair>()) {
            Hair k;
            k.name = o->name;
            k.p = h->params;
            k.simulate = h->simulate;
            for (const auto& s : h->strands) {
                Hair::Strand st;
                st.root = bone(s.root);
                // The strand frame (+0x50 of the 0x80-byte strand) [I: the
                // second stored matrix].
                for (size_t i = 0; i < 9; ++i) st.base[i] = s.baseMat2[i];
                for (const auto& pt : s.points) {
                    Hair::Point q;
                    q.pos = pt.pos;
                    q.bone = bone(pt.bone);
                    q.length = pt.length;
                    q.type = int(pt.f50);
                    q.collide = bone(pt.collide);
                    q.radius = pt.f60;
                    q.extra = pt.f64;
                    st.points.push_back(q);
                }
                k.strands.push_back(st);
                p.reads.push_back(s.root);  // 0x8217CB50
                p.writes.push_back(s.root);
                for (const auto& pt : s.points) p.writes.push_back(pt.bone);
            }
            hairs.push_back(k);
            p.kind = kHair, p.index = hairs.size() - 1;
        } else if (auto* c = o->as<scene::CharPosConstraint>()) {
            PosConstraint k;
            k.name = o->name;
            k.src = bone(c->src);
            for (const auto& t : c->targets)
                if (int b = bone(t); b >= 0) k.targets.push_back(b);
            k.boxMin = c->boxMin, k.boxMax = c->boxMax;
            constraints.push_back(k);
            p.kind = kConstraint, p.index = constraints.size() - 1;
            p.reads = {c->src};  // 0x82179490
            p.reads.insert(p.reads.end(), c->targets.begin(), c->targets.end());
            p.writes = c->targets;
        } else if (auto* r = o->as<scene::CharIKRod>()) {
            IkRod k;
            k.name = o->name;
            k.left = bone(r->leftEnd), k.right = bone(r->rightEnd), k.side = bone(r->sideAxis), k.dest = bone(r->dest);
            k.destPos = r->destPos;
            k.vertical = r->vertical;
            for (size_t i = 0; i < 9; ++i) k.rel.rot[i] = r->xfm[i];
            for (size_t i = 0; i < 3; ++i) k.rel.pos[i] = r->xfm[9 + i];
            rods.push_back(k);
            p.kind = kRod, p.index = rods.size() - 1;
            p.reads = {r->leftEnd, r->rightEnd, r->sideAxis};  // 0x82180C98
            p.writes = {r->dest};
        } else {
            continue;
        }
        pollables.push_back(std::move(p));
    }
    // CharEyes: the head is the first look-at's pivot's parent; each eye's
    // look-at index.
    for (Eyes& e : eyes) {
        const auto* def = dir.get(e.name) ? dir.get(e.name)->as<scene::CharEyes>() : nullptr;
        if (!def) continue;
        for (const auto& n : def->eyes)
            for (size_t i = 0; i < lookats.size(); ++i)
                if (lookats[i].name == n) e.lookats.push_back(i);
        if (!e.lookats.empty()) {
            const LookAt& l = lookats[e.lookats[0]];
            const int piv = l.pivot >= 0 ? l.pivot : l.source;
            e.head = piv >= 0 ? sk.parent(piv) : -1;
        }
        // The look-ats' changes feed the eyes; the eyes also read the head.
        for (Pollable& p : pollables)
            if (p.kind == kEyes && p.name == e.name) p.reads.push_back(nameOf(e.head));
    }
    // The weight setters change the weightables they own (0x8218DE98).
    for (Pollable& p : pollables) {
        if (p.kind != kSetter) continue;
        for (const auto& [n, w] : weights)
            if (w.owner == p.name && n != p.name) p.writes.push_back(n);
    }
    sort();
}

// 0x82156B78 / 0x821563C8: the directory's pollables ordered so that each
// runs after the ones that change what it is changed by. A pollable named
// in another's change list (the weight setters' drivers) or changed-by list
// (a setter's driver) is a dependency itself; a changed transform also
// changes its subtree [I: GH2's graph keeps parents through the transforms'
// own dependencies]. Ties (and cycles) keep the directory's order.
void Rig::Impl::sort() {
    const size_t n = pollables.size();
    auto isAncestorOrSelf = [&](int a, int b) {
        for (int i = b, d = 0; i >= 0 && d < 64; i = sk.parent(i), ++d)
            if (i == a) return true;
        return false;
    };
    auto writesAffect = [&](const Pollable& w, const std::string& readName) {
        if (readName.empty()) return false;
        const int rb = sk.find(readName);
        if (w.writesAllBones) return rb >= 0;
        for (const auto& wn : w.writes) {
            if (wn.empty()) continue;
            if (wn == readName) return true;
            const int wb = sk.find(wn);
            if (wb >= 0 && rb >= 0 && isAncestorOrSelf(wb, rb)) return true;
        }
        return false;
    };
    std::vector<std::vector<size_t>> after(n);  // edges a -> b: a before b
    std::vector<int> indeg(n, 0);
    auto edge = [&](size_t a, size_t b) {
        if (a == b || std::find(after[a].begin(), after[a].end(), b) != after[a].end()) return;
        after[a].push_back(b);
        ++indeg[b];
    };
    for (size_t a = 0; a < n; ++a)
        for (size_t b = 0; b < n; ++b) {
            if (a == b) continue;
            const Pollable& A = pollables[a];
            const Pollable& B = pollables[b];
            bool before = std::find(A.writes.begin(), A.writes.end(), B.name) != A.writes.end() ||
                          std::find(B.reads.begin(), B.reads.end(), A.name) != B.reads.end();
            for (const auto& r : B.reads)
                if (!before && writesAffect(A, r)) before = true;
            // Hand IK and its own elbow chain: not a dependency on itself.
            if (before) edge(a, b);
        }
    std::vector<Pollable> out;
    std::vector<bool> done(n, false);
    for (size_t k = 0; k < n; ++k) {
        size_t pick = n;
        for (size_t i = 0; i < n && pick == n; ++i)
            if (!done[i] && indeg[i] == 0) pick = i;
        if (pick == n)  // a cycle: the first left in directory order
            for (size_t i = 0; i < n && pick == n; ++i)
                if (!done[i]) pick = i;
        done[pick] = true;
        for (size_t b : after[pick]) --indeg[b];
        out.push_back(pollables[pick]);
    }
    pollables = std::move(out);
}

void Rig::Impl::poll(const RigContext& ctx) {
    bool driversDone = false;
    for (Pollable& p : pollables) {
        if (!p.enabled) continue;
        switch (p.kind) {
        case kDriver:
            if (ctx.pollDriver) ctx.pollDriver(p.name);
            else if (!driversDone && ctx.pollDrivers) ctx.pollDrivers(), driversDone = true;
            break;
        case kServo:
            if (ctx.pollServo) ctx.pollServo();
            break;
        case kSetter: pollSetter(setters[p.index], ctx); break;
        case kHand: pollHand(hands[p.index]); break;
        case kMidi: pollMidi(midis[p.index], ctx); break;
        case kUpper: pollUpper(uppers[p.index]); break;
        case kFore: pollFore(fores[p.index]); break;
        case kLookAt: pollLookAt(lookats[p.index], ctx); break;
        case kEyes: pollEyes(eyes[p.index], ctx); break;
        case kHair: pollHair(hairs[p.index]); break;
        case kConstraint: pollConstraint(constraints[p.index]); break;
        case kRod: pollRod(rods[p.index]); break;
        }
    }
}

// ---- CharWeightSetter ------------------------------------------------------

void Rig::Impl::pollSetter(Setter& s, const RigContext& ctx) {
    Weight& w = weights[s.name];
    if (!s.driver.empty() && ctx.driverFlagWeight) w.weight = ctx.driverFlagWeight(s.driver, s.flags);
    if (s.invert) w.weight = 1 - w.weight;
}

// ---- CharIKHand ------------------------------------------------------------

// 0x82172F38: the arm lengths from the bind offsets (forearm a = |hand
// local pos|, upper arm b = |forearm local pos|): 1 / (2ab) and a^2 + b^2.
// Once, or every poll with the third u8 (+0x4a; 0 on the disc).
void Rig::Impl::lengths(IkHand& h) {
    if (!h.lengthsDirty && !h.everyFrame) return;
    h.lengthsDirty = false;
    if (h.hand < 0 || sk.parent(h.hand) < 0 || sk.parent(sk.parent(h.hand)) < 0) return;
    const float a = len(sk.local(h.hand).pos);
    const float b = len(sk.local(sk.parent(h.hand)).pos);
    h.a2b2 = b * b + a * a;
    h.inv2ab = a * b > 0 ? 1.0f / (a * 2 * b) : 0;
}

void Rig::Impl::pollHand(IkHand& h) {
    lengths(h);
    const float w = weightOf(h.name);
    if (w == 0 || h.hand < 0 || h.targets.empty()) return;
    // The target: one target's world position (and orientation with
    // orient), or the targets' average weighted by 1 / |their local offset|.
    V3 target{0, 0, 0};
    Q4 rot{0, 0, 0, 0};
    if (h.targets.size() == 1) {
        const scene::Xfm& t = sk.world(h.targets[0]);
        target = t.pos;
        if (h.orient) rot = mToQuat(orthoY(row(t.rot, 1), row(t.rot, 2)));
    } else {
        std::vector<float> inv;
        float sum = 0;
        for (int t : h.targets) {
            float l = len(sk.local(t).pos);
            if (l <= 0.001f) l = 0.001f;
            inv.push_back(1.0f / l);
            sum += 1.0f / l;
        }
        for (size_t i = 0; i < h.targets.size(); ++i) {
            const float k = inv[i] / sum;
            const scene::Xfm& t = sk.world(h.targets[i]);
            target = add(target, mul(t.pos, k));
            if (h.orient) quatAccumulate(rot, mToQuat(orthoY(row(t.rot, 1), row(t.rot, 2))), k);
        }
        if (h.orient) rot = qnormalize(rot);
    }
    // A finger: the hand goes where the finger lands on the target.
    if (h.finger >= 0) {
        scene::Xfm t;
        t.rot = quatToM(h.orient ? rot : Q4{0, 0, 0, 1});
        t.pos = target;
        const scene::Xfm handInFinger = scene::compose(sk.world(h.hand), inverse(sk.world(h.finger)));
        const scene::Xfm m = scene::compose(handInFinger, t);
        target = m.pos;
        rot = mToQuat(m.rot);
    }
    // +0x50: the hand's position moved toward the target by the weight.
    const V3 goal = lerp(sk.world(h.hand).pos, target, w);
    int forearm = sk.parent(h.hand);
    if (!h.moveElbow) forearm = -1;
    const int upper = forearm >= 0 ? sk.parent(forearm) : -1;
    if (forearm >= 0 && upper >= 0) {
        // The elbow: law of cosines from the shoulder distance, cos clamped
        // to [-0.985, 0.88]; the forearm's local rotation becomes that bend
        // about z (whole matrix, position kept).
        const V3 d = sub(sk.world(upper).pos, goal);
        const float c = std::clamp((dot(d, d) - h.a2b2) * h.inv2ab, -0.985f, 0.88f);
        const float s = -std::sqrt(1 - c * c);
        scene::Xfm fl = sk.local(forearm);
        fl.rot = {c, s, 0, -s, c, 0, 0, 0, 1};
        sk.setLocal(forearm, fl);
        // The shoulder turns the hand onto the goal: both in the upper arm's frame.
        const scene::Xfm& uw = sk.world(upper);
        const V3 v1 = vecRotT(sub(sk.world(h.hand).pos, uw.pos), uw.rot);
        const V3 v2 = vecRotT(sub(goal, uw.pos), uw.rot);
        scene::Xfm ul = sk.local(upper);
        ul.rot = mulRot(quatToM(rotBetween(v1, v2)), ul.rot);
        sk.setLocal(upper, ul);
        if (!h.orient && !h.stretch) return;
    }
    scene::Xfm m = sk.world(h.hand);
    if (forearm < 0 || upper < 0 || h.stretch) m.pos = goal;
    if (h.orient) {
        Q4 q = rot;
        if (w < 1) q = slerp(mToQuat(m.rot), rot, w);
        m.rot = quatToM(q);
    }
    sk.setWorld(h.hand, m);
}

// ---- CharIKMidi ------------------------------------------------------------

// 0x82183700: a new spot; the start is the bone's last world transform in
// the spot's parent's frame (so it rides along with the guitar), the blend
// runs over `duration` beats (at once when <= 0).
void Rig::Impl::newSpot(IkMidi& m, int spot, double duration) {
    if (spot < 0) return;
    const int sp = sk.parent(spot);
    const scene::Xfm parentWorld = sp >= 0 ? sk.world(sp) : sk.root();
    if (m.spot < 0 && m.bone >= 0) m.last = sk.world(m.bone), m.hasLast = true;
    m.rel = scene::compose(m.last, inverse(parentWorld));
    m.spot = spot;
    if (duration > 0) m.frac = 0, m.rate = float(1.0 / duration);
    else m.frac = 1, m.rate = 0;
}

// 0x82183410: frac += delta beats x rate; the bone's world = the start
// (re-attached to the spot's parent) moved to the spot by
// 0.5 - 0.5 cos(pi frac): position lerped, rotation slerped (0x8226A980).
void Rig::Impl::pollMidi(IkMidi& m, const RigContext& ctx) {
    if (m.bone < 0) return;
    if (m.spot >= 0) {
        m.frac = std::clamp(m.frac + ctx.dBeats * m.rate, 0.0f, 1.0f);
        const float e = 0.5f - 0.5f * std::sin(m.frac * kPi + kPi * 0.5f);
        const int sp = sk.parent(m.spot);
        const scene::Xfm start = scene::compose(m.rel, sp >= 0 ? sk.world(sp) : sk.root());
        const scene::Xfm& spot = sk.world(m.spot);
        scene::Xfm x;
        x.pos = lerp(start.pos, spot.pos, e);
        x.rot = quatToM(slerp(mToQuat(start.rot), mToQuat(spot.rot), e));
        sk.setWorld(m.bone, x);
    }
    m.last = sk.world(m.bone);
    m.hasLast = true;
}

// ---- twists ------------------------------------------------------------------

// 0x82182738: the upper arm's twist t about x is spread over the twist
// bones: chained (twist2 under twist1): twist1 = RotX(-0.666 t) upper arm,
// twist2 = RotX(0.333 t); else both RotX(-0.5 t) on top of the previous.
void Rig::Impl::pollUpper(UpperTwist& u) {
    if (u.upperArm < 0 || u.twist1 < 0 || u.twist2 < 0) return;
    const M3 up = sk.local(u.upperArm).rot;
    const float t = twistAboutX(up);
    scene::Xfm a = sk.local(u.twist1), b = sk.local(u.twist2);
    if (sk.parent(u.twist2) == u.twist1) {
        a.rot = mulRot(rotX(t * -0.666f), up);
        b.rot = rotX(t * 0.333f);
    } else {
        const M3 r = rotX(t * -0.5f);
        b.rot = mulRot(r, up);
        a.rot = mulRot(r, b.rot);
    }
    sk.setLocal(u.twist1, a);
    sk.setLocal(u.twist2, b);
}

// 0x82181980: the hand's twist about x plus the offset (degrees), a third
// of it: twist2 = RotX(t), its parent (the fore twist 1) = twist2 x the
// forearm's rotation, twist2 halfway down the forearm (x = hand x / 2).
void Rig::Impl::pollFore(ForeTwist& f) {
    if (f.hand < 0 || f.twist2 < 0) return;
    const int forearm = sk.parent(f.hand), twist1 = sk.parent(f.twist2);
    if (forearm < 0 || twist1 < 0) return;
    const scene::Xfm& hand = sk.local(f.hand);
    const float t = wrapAngle(f.offset * kDeg + twistAboutX(hand.rot)) * 0.333333f;
    scene::Xfm b = sk.local(f.twist2);
    b.rot = rotX(t);
    b.pos[0] = hand.pos[0] * 0.5f;
    scene::Xfm a = sk.local(twist1);
    a.rot = mulRot(b.rot, sk.local(forearm).rot);
    sk.setLocal(twist1, a);
    sk.setLocal(f.twist2, b);
}

// ---- CharLookAt / CharEyes -------------------------------------------------------

void Rig::Impl::pollLookAt(LookAt& l, const RigContext& ctx) {
    if (l.source < 0 || (l.dest < 0 && !l.destPoint)) return;
    const int pivot = l.pivot >= 0 ? l.pivot : l.source;
    const V3 dest = l.destPoint ? l.point : sk.world(l.dest).pos;
    V3 dir = sub(dest, sk.world(pivot).pos);
    float w = weightOf(l.name);
    // The fade by angle (+0x54 >= 0; -1 on the disc): weight by the
    // horizontal angle between the pivot's y and the direction, rate-limited.
    if (l.fadeMin >= 0) {
        const V3 y = row(sk.world(pivot).rot, 1);
        float c = (y[0] * dir[0] + y[1] * dir[1]) / (std::sqrt(y[0] * y[0] + y[1] * y[1]) * std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]));
        c = std::clamp(c, -1.0f, 1.0f);
        float f = std::clamp((l.fadeMax - std::acos(c) / kDeg) / (l.fadeMax - l.fadeMin), 0.0f, 1.0f);
        if (ctx.dSeconds > 0 && l.fadeRate < (f - l.fade) / ctx.dSeconds) f = l.fadeRate * ctx.dSeconds + l.fade;
        l.fade = f;
        w *= f;
    }
    if (w == 0) return;
    if (pivot == l.source) {
        dir = normalize(dir);
    } else {
        // A separate pivot: the source swung about it [I: GH2's two-step
        // version; no such look-at on the disc].
        dir = normalize(sub(dest, sk.world(l.source).pos));
    }
    // Into the source's parent's frame, clamped to the limits box (FUN_8226BE50).
    const int parent = sk.parent(l.source);
    const M3 pr = parent >= 0 ? sk.world(parent).rot : sk.root().rot;
    dir = vecRotT(dir, pr);
    for (int k = 0; k < 3; ++k) dir[size_t(k)] = std::clamp(dir[size_t(k)], l.boxMin[size_t(k)], l.boxMax[size_t(k)]);
    // The half time: a low-pass of the direction, dt / (half time + dt).
    if (l.halfTime != 0) {
        dir = normalize(dir);
        if (!l.hasFiltered) l.filtered = dir, l.hasFiltered = true;
        const float dt = ctx.dSeconds;
        l.filtered = lerp(l.filtered, dir, dt + l.halfTime > 0 ? dt / (l.halfTime + dt) : 1.0f);
        dir = l.filtered;
    }
    scene::Xfm x = sk.local(l.source);
    if (!l.allowRoll) {
        // y toward the direction (by the weight), -x kept as the up hint.
        const V3 r1 = normalize(lerp(row(x.rot, 1), dir, w));
        const V3 r2 = {-1, 0, 0};
        const V3 r0 = normalize(cross(r1, r2));
        setRow(x.rot, 0, r0);
        setRow(x.rot, 1, r1);
        setRow(x.rot, 2, cross(r0, r1));
    } else {
        const Q4 q = slerp(Q4{0, 0, 0, 1}, rotBetween(row(x.rot, 1), dir), w);
        x.rot = mulRot(x.rot, quatToM(q));
    }
    sk.setLocal(l.source, x);
}

// 0x82170130: a new point to look at, ahead of the head (its y axis plus 45x
// the change since the last pick, that change at most tan(0.458) long), at
// RandomFloat(20, 100) x 12 units, not under the character's floor; an
// interest within its radius of the line wins.
void Rig::Impl::retarget(Eyes& e) {
    if (e.head < 0) return;
    const scene::Xfm& h = sk.world(e.head);
    const V3 f = row(h.rot, 1);
    V3 d = mul(sub(f, e.lastForward), 45.0f);
    const float lim = std::tan(0.45815f), l = len(d);
    if (lim < l) d = mul(d, lim / l);
    const V3 dir = add(f, d);
    const float dist = rng.real(20.0f, 100.0f) * 12.0f;
    V3 p = add(h.pos, mul(dir, dist));
    const float floorZ = sk.root().pos[2];
    if (p[2] < floorZ && dir[2] != 0) {
        const float k = (floorZ - h.pos[2]) / (p[2] - h.pos[2]);
        p = add(h.pos, mul(mul(dir, dist), k));
    }
    int interest = -1;
    if (!e.interests.empty()) {
        const V3 nd = normalize(sub(p, h.pos));
        for (const auto& [obj, radius] : e.interests) {
            const V3 o = sub(sk.world(obj).pos, h.pos);
            const float along = dot(o, nd);
            if (along <= 0) continue;
            const V3 off = sub(add(h.pos, mul(nd, along)), sk.world(obj).pos);
            if (dot(off, off) < radius * radius) {
                interest = obj;
                break;
            }
        }
    }
    for (size_t i : e.lookats) {
        LookAt& l = lookats[i];
        if (interest >= 0) l.dest = interest, l.destPoint = false;
        else l.dest = -1, l.destPoint = true, l.point = p;
    }
    e.timer = 0;
    e.smoothed = 0;
    e.lastDot = kHuge;
}

// 0x82170BA8: the eyes keep their point until 8 s have passed, or (after
// 1 s) the head has turned toward it faster than the threshold.
void Rig::Impl::pollEyes(Eyes& e, const RigContext& ctx) {
    if (e.lookats.empty() || e.head < 0) return;
    e.timer += ctx.dSeconds;
    const scene::Xfm& h = sk.world(e.head);
    const LookAt& first = lookats[e.lookats[0]];
    V3 target = first.destPoint ? first.point : first.dest >= 0 ? sk.world(first.dest).pos : add(h.pos, row(h.rot, 1));
    const V3 dir = normalize(sub(target, h.pos));
    const float d = std::clamp(dot(dir, row(h.rot, 1)), -1.0f, 1.0f);
    if (e.lastDot != kHuge) {
        e.smoothed = ((d - e.lastDot) - e.smoothed) * 0.1f + e.smoothed;
        if (8 < e.timer || (1 < e.timer && e.threshold < d && 0 < e.smoothed)) retarget(e);
    }
    e.lastDot = d;
    e.lastForward = row(sk.world(e.head).rot, 1);
}

// ---- CharHair --------------------------------------------------------------

namespace {
bool hairCollide() {
    static const bool on = [] {
        const char* e = SDL_getenv("GD02_HAIR_COLLIDE");
        return e && *e && *e != '0';
    }();
    return on;
}
}  // namespace

// 0x8217B6F0: each point at the end of its bone (the bones' chain from the
// root's parent), velocities zero, the up vector its bone's z.
void Rig::Impl::resetHair(Hair& h) {
    for (auto& s : h.strands) {
        if (s.root < 0 || s.points.empty()) continue;
        for (size_t i = 0; i < s.points.size(); ++i) {
            auto& p = s.points[i];
            if (p.bone < 0) continue;
            const scene::Xfm& b = sk.world(p.bone);
            p.vel = p.prevVel = {0, 0, 0};
            p.up = row(b.rot, 2);
            if (i > 0) s.points[i - 1].pos = b.pos;
            if (i + 1 == s.points.size()) p.pos = add(b.pos, mul(row(b.rot, 1), p.length));
        }
    }
}

// 0x8217B898 (per poll, no time step: GH2 runs it once a frame):
// for each point, from the root: Verlet-like step (pos += vel, z -= 0.0643 x
// gravity), the segment held at its length (the parent point's velocity
// pulled by -0.5 x weight of the correction), collision (type 0: a plane
// through the collide bone, normal its y, at `radius`), the bone placed at
// the previous point aiming its y at this one (z from the frame's z,
// smoothed), then vel = stiffness x (rest - pos) - friction x (that change)
// + inertia x (pos - old pos), rest = the previous point + the frame's y x length.
void Rig::Impl::pollHair(Hair& h) {
    if (!h.simulate) return;
    if (h.reset) resetHair(h), h.reset = false;
    const float gravity = h.p[3] * -0.06430447f;
    const float pull = h.p[4] * -0.5f;
    for (auto& s : h.strands) {
        if (s.root < 0) continue;
        const int rp = sk.parent(s.root);
        M3 frame = mulRot(s.base, rp >= 0 ? sk.world(rp).rot : sk.root().rot);
        V3 prev = sk.world(s.root).pos;
        for (size_t i = 0; i < s.points.size(); ++i) {
            auto& p = s.points[i];
            const V3 old = p.pos;
            p.pos = add(p.pos, p.vel);
            p.pos[2] += gravity;
            V3 d = sub(p.pos, prev);
            const float dl = len(d);
            const float k = dl > 0 ? p.length / dl - 1 : 0;
            if (i > 0) s.points[i - 1].vel = add(s.points[i - 1].vel, mul(d, k * pull));
            p.pos = add(p.pos, mul(d, k));
            const V3 rest = add(prev, mul(row(frame, 1), p.length));
            const V3 up = lerp(p.up, row(frame, 2), h.p[1]);
            p.up = row(frame, 2);
            // The collision (0x8217B898) measures from an object the decompile
            // lost; taken as the collide bone, the plane rule (push along o by
            // radius / n - 1) throws bind-pose bangs 10+ units out and the
            // strands blow up, which GH2 does not show [I]. Off until that
            // reference is confirmed (GD02_HAIR_COLLIDE=1 turns it on).
            if (p.collide >= 0 && hairCollide()) {
                const scene::Xfm& c = sk.world(p.collide);
                const V3 o = sub(p.pos, c.pos);
                if (p.type == 0) {
                    const float n = dot(row(c.rot, 1), o);
                    if (n < p.radius && n != 0) p.pos = add(p.pos, mul(o, p.radius / n - 1));
                } else if (p.type == 1) {
                    // a sphere: kept outside
                    const float l2 = dot(o, o);
                    if (l2 < p.radius * p.radius && l2 > 0) p.pos = add(p.pos, mul(o, p.radius / std::sqrt(l2) - 1));
                } else {
                    // 2..4: capsule / cylinder variants [I: kept outside like a sphere]
                    const float l2 = dot(o, o);
                    if (l2 < p.radius * p.radius && l2 > 0) p.pos = add(p.pos, mul(o, p.radius / std::sqrt(l2) - 1));
                }
            }
            frame = orthoY(sub(p.pos, prev), up);
            if (p.bone >= 0) {
                scene::Xfm b;
                b.rot = frame;
                b.pos = prev;
                sk.setWorld(p.bone, b);
            }
            const V3 nv = sub(rest, p.pos);
            const V3 oldPrev = p.prevVel;
            p.prevVel = nv;
            V3 v = mul(nv, h.p[0]);
            v = add(v, mul(sub(oldPrev, nv), -h.p[5]));
            v = add(v, mul(sub(p.pos, old), h.p[2]));
            p.vel = v;
            prev = p.pos;
        }
    }
}

// ---- CharPosConstraint / CharIKRod -----------------------------------------

// 0x82178B28: each target's world position kept inside src + [min, max] on
// every axis whose min <= max.
void Rig::Impl::pollConstraint(PosConstraint& c) {
    if (c.src < 0) return;
    const V3 s = sk.world(c.src).pos;
    for (int t : c.targets) {
        scene::Xfm x = sk.world(t);
        for (size_t k = 0; k < 3; ++k)
            if (c.boxMin[k] <= c.boxMax[k]) x.pos[k] = s[k] + std::clamp(x.pos[k] - s[k], c.boxMin[k], c.boxMax[k]);
        sk.setWorld(t, x);
    }
}

// 0x8217FFF0 + 0x821805C0: a frame between the two ends (position lerped by
// dest_pos; x the ends' x lerped, or -z when vertical; z the side axis's z,
// else left - right; y = z x x), the dest = rel x frame.
void Rig::Impl::pollRod(IkRod& r) {
    if (r.dest < 0 || r.left < 0 || r.right < 0) return;
    const scene::Xfm& a = sk.world(r.left);
    const scene::Xfm& b = sk.world(r.right);
    scene::Xfm f;
    f.pos = lerp(a.pos, b.pos, r.destPos);
    V3 x = r.vertical ? V3{0, 0, -1} : normalize(lerp(row(a.rot, 0), row(b.rot, 0), r.destPos));
    V3 z = r.side >= 0 ? row(sk.world(r.side).rot, 2) : sub(a.pos, b.pos);
    const V3 y = normalize(cross(z, x));
    z = cross(x, y);
    setRow(f.rot, 0, x);
    setRow(f.rot, 1, y);
    setRow(f.rot, 2, z);
    sk.setWorld(r.dest, scene::compose(r.rel, f));
}

// ---- Rig -----------------------------------------------------------------------

Rig::Rig(const scene::Directory& dir, Skeleton& skeleton) : impl_(std::make_unique<Impl>(dir, skeleton)) { impl_->build(); }
Rig::~Rig() = default;

void Rig::poll(const RigContext& ctx) { impl_->poll(ctx); }

float Rig::weightOf(const std::string& name) const { return impl_->weightOf(name); }

void Rig::setWeight(const std::string& name, float w) {
    auto it = impl_->weights.find(name);
    if (it == impl_->weights.end()) impl_->weights[name] = {w, ""};
    else it->second.weight = w;
}

bool Rig::ikMidi(const std::string& ik, const std::string& spot, double start, double end, double now) {
    (void)start;
    for (auto& m : impl_->midis)
        if (m.name == ik) {
            const int s = impl_->sk.find(spot);
            if (s < 0) return false;
            // 0x821837B8: the blend runs until the event's end (the note).
            impl_->newSpot(m, s, end - now);
            return true;
        }
    return false;
}

void Rig::resetHair() {
    for (auto& h : impl_->hairs) h.reset = true;
}

void Rig::setEnabled(const std::string& name, bool on) {
    for (auto& p : impl_->pollables)
        if (p.name == name) p.enabled = on;
}

std::vector<std::string> Rig::order() const {
    std::vector<std::string> v;
    for (const auto& p : impl_->pollables) v.push_back(p.name);
    return v;
}

}  // namespace chr
