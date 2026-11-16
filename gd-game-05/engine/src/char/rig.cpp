// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/rig.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <SDL3/SDL.h>

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

bool envSwitch(const char* name) {
    const char* e = SDL_getenv(name);
    return e && *e && *e != '0';
}
// GD05_EYES_LEGACY=1: an eye look-at with no source does nothing (the port
// before GD05-106); GD05_GAZE_LEGACY=1: GH2's CharEyes timing (8 s, the right
// lid's blinks with a 1 in 3 draw) instead of Rock Band's.
bool eyesLegacy() {
    static const bool v = envSwitch("GD05_EYES_LEGACY");
    return v;
}
bool gazeLegacy() {
    static const bool v = envSwitch("GD05_GAZE_LEGACY");
    return v;
}
// GD05_LIDS_LEGACY=1: the eyelids do not follow the eyes (the port before
// GD05-112).
bool lidsLegacy() {
    static const bool v = envSwitch("GD05_LIDS_LEGACY");
    return v;
}

// The hair (GD05-113), each with its own switch to the port before it:
// GD05_HAIRCOLLIDE_LEGACY=1: no Rock Band hookup / collision (GH2's per-point
// collide fields, always empty on RB's disc); GD05_HAIRGRAVITY_LEGACY=1:
// GH2's 60 Hz gravity per step whatever the step rate;
// GD05_HAIRSLACK_LEGACY=1: no neighbour-strand slack; GD05_HAIRRESET_LEGACY=1:
// no settle step after a reset.
bool hairCollideLegacy() {
    static const bool v = envSwitch("GD05_HAIRCOLLIDE_LEGACY");
    return v;
}
bool hairGravityLegacy() {
    static const bool v = envSwitch("GD05_HAIRGRAVITY_LEGACY");
    return v;
}
bool hairSlackLegacy() {
    static const bool v = envSwitch("GD05_HAIRSLACK_LEGACY");
    return v;
}
bool hairResetLegacy() {
    static const bool v = envSwitch("GD05_HAIRRESET_LEGACY");
    return v;
}

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
    float blinkValue = -1;  // the face servo's Blink weight this poll (Rock Band's eyes); < 0: none

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

    // CharWeightSetter (Rock Band, Poll RB 0x8234A320) [C]: with a driver
    // its base weight = offset + scale x the driver's eased weight of clips
    // with its flags (RB 0x82311D68); without one, the base weight scripts
    // set (singalong, guitar_lookat, drummer_play). The weight follows the
    // base at most 1 / beats_per_weight per beat (at once when 0).
    struct Setter {
        std::string name, driver;
        uint32_t flags = 0;
        float offset = 0, scale = 1, base = 0, beatsPerWeight = 0;
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
        float lengthScale = 0;  // RB rev 2: twist2 at hand x x this (none when <= 0)
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
        int lid = -1;              // right_lid (+0x38): its blinks retarget (GD02-050 G3)
        float lidLast = 0;         // +0x5c (Enter: 0)
        float lidDelta = 0;        // +0x60 (Enter: 0)
        bool blinkArmed = false;   // RB: the servo's Blink went under 0.3
        float blinkLast = 0;       // RB: the servo's Blink last poll
        int retargets = 0;         // diagnostics (bandcheck)
        // RB rev 7: each eye's upper and lower lid (bones, -1 for none), and
        // the lid's local as the follow last left it with the pose before
        // it (a lid no servo wrote again this poll starts from that pose).
        struct Lid {
            int bone = -1;
            scene::Xfm before, after;
            bool has = false;
        };
        std::vector<std::array<Lid, 2>> lids;  // per look-at of `lookats`: upper, lower
        std::vector<std::pair<int, float>> interests;  // (object, radius); none on the disc
    };
    // CharHair (Poll 0x8217B898, reset 0x8217B6F0).
    struct Hair {
        std::string name;
        // +0x08..: stiffness, torsion, inertia, gravity, weight, friction,
        // min_slack, max_slack (RB rev 9)
        std::array<float, 8> p{};
        struct Point {
            V3 pos{}, vel{}, prevVel{}, up{};
            int bone = -1, collide = -1, type = 0;
            float length = 0, radius = 0, band = 0;  // GH2's fields (the legacy collision)
            // RB rev 9 (RB 0x823372A0): radius (+0x64), outer_radius
            // (+0x68), side_length (+0x6c) [C]; the collides the hookup
            // gave it (+0x50, indexes into Impl::collides).
            float rbRadius = 0, outer = 0, side = -1;
            std::vector<int> hooks;
            V3 rel{};  // pos - the strand root's, last step (bandcheck motion)
        };
        struct Strand {
            int root = -1;
            M3 base{};
            uint32_t hookupFlags = 0;  // +0x80
            std::vector<Point> points;
        };
        std::vector<Strand> strands;
        bool simulate = false, reset = true, hooked = false;
        bool relValid = false;  // the points' rel is from a step (bandcheck)
        double clock = 0;  // unstepped seconds: the hair steps at GH2's 60 Hz (GD02-050 G1)
    };
    // CharCollide (RB load 0x8232D9D0, compute_radius 0x8232DB30) [C]:
    // placed under its parent bone (the outfit's exo_* bones); worldRadius
    // = radius x the collide's world y scale, set at the hookup.
    struct Collide {
        std::string name;
        int parent = -1;
        scene::Xfm local;
        uint32_t shape = 0, flags = 0;
        float radius = 0, length = 0, worldRadius = 0;
    };
    std::vector<Collide> collides;
    std::vector<scene::Xfm> collideWorld;  // this step's
    RigHairStats hairStats;  // bandcheck (GD05-113)
    bool hairMeasuring = false, hairSettling = false;
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

    enum Kind { kDriver, kServo, kBlender, kFace, kWalk, kSetter, kHand, kMidi, kUpper, kFore, kLookAt, kEyes, kHair, kConstraint, kRod };
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
    void followLids(Eyes& e);
    void retarget(Eyes& e);
    void pollEyes(Eyes& e, const RigContext& ctx);
    void resetHair(Hair& h);
    void hookupHair(Hair& h);
    void placeCollides();
    void measureHair(Hair& h, double rate);
    void pollHair(Hair& h, double rate);
    void stepHair(Hair& h, double dSeconds);
    void placeHair(Hair& h);
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
        } else if (auto* bb = o->as<scene::CharBonesBlender>()) {
            // RB's CharBonesBlender (PollDeps RB 0x8234DE60) [C]: changes its dest.
            p.kind = kBlender, p.index = 0;
            p.writes = {bb->dest};
        } else if (o->as<scene::CharServoBone>()) {
            p.kind = kServo, p.index = 0;
            p.writesAllBones = true;
        } else if (o->as<scene::CharWalk>()) {
            // CharWalk (PollDeps 0x8218B2F8) [C]: changes the main driver and the
            // character's servo; no dependency.
            p.kind = kWalk, p.index = 0;
            for (const auto& q : dir.objects) {
                if (q->as<scene::CharDriver>() && q->name == "main.drv") p.writes.push_back(q->name);
                if (q->as<scene::CharServoBone>()) p.writes.push_back(q->name);
            }
        } else if (auto* f = o->as<scene::FaceFxLipSyncServo>()) {
            // The lip servo (a CharServoBone: PollDeps 0x821887F8) [C]: changes
            // its bones (0x821A5A18), reads nothing.
            weights[o->name] = {f->weightable.weight, f->weightable.weightOwner};
            p.kind = kFace, p.index = 0;
            for (const auto& e : f->entries) p.writes.push_back(e.object);
        } else if (auto* s = o->as<scene::CharWeightSetter>()) {
            weights[o->name] = {s->weightable.weight, s->weightable.weightOwner};
            setters.push_back({o->name, s->driver, s->flags, s->offset, s->scale, s->baseWeight, s->beatsPerWeight});
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
            fores.push_back({o->name, f->offset, bone(f->hand), bone(f->twist2), f->lengthScale});
            p.kind = kFore, p.index = fores.size() - 1;
            p.reads = {f->hand};  // 0x82182398
            p.writes = {f->twist2, parentName(bone(f->twist2))};
        } else if (auto* l = o->as<scene::CharLookAt>()) {
            weights[o->name] = {l->weightable.weight, l->weightable.weightOwner};
            LookAt k;
            k.name = o->name;
            k.source = bone(l->source), k.pivot = bone(l->pivot), k.dest = bone(l->dest);
            // Rock Band's CharLookAt Poll (RB 0x82321A48) [C]: the source if
            // set, else the pivot, is the bone turned; every eye look-at on the
            // disc has source "" (GD05-106: the port returned at once, the
            // eyes never moved).
            const bool pivotIsSource = !eyesLegacy() && l->source.empty() && k.pivot >= 0;
            if (pivotIsSource) k.source = k.pivot;
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
            p.writes = {pivotIsSource ? l->pivot : l->source};
        } else if (auto* e = o->as<scene::CharEyes>()) {
            Eyes k;
            k.name = o->name;
            for (const auto& [obj, v] : e->interests)
                if (int b = bone(obj); b >= 0) k.interests.emplace_back(b, float(v));
            eyes.push_back(k);
            p.kind = kEyes, p.index = eyes.size() - 1;
            // 0x82170FD0: changed by its look-ats and the head.
            p.reads = e->eyes;
            // RB (0x82329588): the lids follow the eyes after the face servo
            // posed them (GD05-112).
            if (!lidsLegacy()) {
                for (const auto& n : e->upperLids) if (!n.empty()) p.reads.push_back(n), p.writes.push_back(n);
                for (const auto& n : e->lowerLids) if (!n.empty()) p.reads.push_back(n), p.writes.push_back(n);
            }
        } else if (auto* h = o->as<scene::CharHair>()) {
            Hair k;
            k.name = o->name;
            std::copy_n(h->params.begin(), k.p.size(), k.p.begin());
            k.simulate = h->simulate;
            for (const auto& s : h->strands) {
                Hair::Strand st;
                st.root = bone(s.root);
                st.hookupFlags = s.hookupFlags;
                // The strand frame (+0x50 of the 0x80-byte strand) [I: the
                // second stored matrix].
                for (size_t i = 0; i < 9; ++i) st.base[i] = s.baseMat2[i];
                for (const auto& pt : s.points) {
                    Hair::Point q;
                    q.pos = pt.pos;
                    q.bone = bone(pt.bone);
                    q.length = pt.length;
                    q.type = pt.type;
                    q.collide = bone(pt.collide);
                    q.radius = pt.f60;
                    q.band = pt.f64;
                    q.rbRadius = pt.radius;
                    q.outer = pt.f60;
                    q.side = pt.f64;
                    st.points.push_back(q);
                }
                k.strands.push_back(st);
                p.reads.push_back(s.root);  // 0x8217CB50
                p.writes.push_back(s.root);
                for (const auto& pt : s.points) p.writes.push_back(pt.bone);
            }
            hairs.push_back(k);
            p.kind = kHair, p.index = hairs.size() - 1;
        } else if (auto* cc = o->as<scene::CharCollide>()) {
            Collide k;
            k.name = o->name;
            k.parent = bone(cc->trans.parent);
            k.local = cc->trans.local;
            k.shape = cc->shape, k.flags = cc->flags;
            k.radius = cc->radius, k.length = cc->length;
            if (k.parent < 0 && !cc->trans.parent.empty() && cc->trans.parent != dir.name)
                SDL_Log("rig %s: %s's parent %s is not a bone", dir.name.c_str(), o->name.c_str(), cc->trans.parent.c_str());
            collides.push_back(k);
            continue;
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
        for (size_t j = 0; j < def->eyes.size(); ++j)
            for (size_t i = 0; i < lookats.size(); ++i)
                if (lookats[i].name == def->eyes[j]) {
                    e.lookats.push_back(i);
                    std::array<Eyes::Lid, 2> lp;
                    lp[0].bone = j < def->upperLids.size() ? bone(def->upperLids[j]) : -1;
                    lp[1].bone = j < def->lowerLids.size() ? bone(def->lowerLids[j]) : -1;
                    e.lids.push_back(lp);
                }
        if (!e.lookats.empty()) {
            const LookAt& l = lookats[e.lookats[0]];
            const int piv = l.pivot >= 0 ? l.pivot : l.source;
            e.head = piv >= 0 ? sk.parent(piv) : -1;
        }
        e.lid = bone(def->rightLid);
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
    // Rock Band's CharPollGroup (RB 0x82345C38: hand_iks = left_hand.ikhand,
    // fret.ikhand, right_hand.ikhand, strum.ikhand ...) polls its members in
    // its own order [C data]: they swap among the places the sort gave them,
    // so the strum IK runs after the right hand's hip / mouth IK.
    for (const auto& o : dir.objects) {
        const auto* g = o->as<scene::CharPollGroup>();
        if (!g) continue;
        std::vector<size_t> slots;
        std::vector<Pollable> members;
        // [I] Only its IK hands swap: the drivers it also lists (expression.drv,
        // face_accent.dmidi) write the face and keep the sort's places.
        for (const std::string& name : g->polls)
            for (size_t i = 0; i < pollables.size(); ++i)
                if (pollables[i].name == name && pollables[i].kind == kHand) slots.push_back(i), members.push_back(pollables[i]);
        std::vector<size_t> sorted = slots;
        std::sort(sorted.begin(), sorted.end());
        for (size_t k = 0; k < sorted.size(); ++k) pollables[sorted[k]] = members[k];
    }
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
        // A cycle (the hand IKs: left_hand.ikhand and fret.ikhand change and
        // read the same hand): the one left with the fewest dependencies
        // still to run, the first in directory order among them (GD05-110).
        // RB polls the hand IKs as one CharPollGroup (hand_iks, RB 0x82345C38),
        // so the twists that read the arm (CharUpperTwist RB 0x8233EAD0,
        // CharForeTwist RB 0x8233DE58) run after all of them [I: the group's
        // place in RB's sort not read]. GD05_POLLSORT_LEGACY=1: the first left
        // in directory order, which put upperTwist_L / foreTwist_L before the
        // left arm's IKs: the twist bones followed the arm before the IK moved
        // it, the forearm skin twisted into an S.
        static const bool legacy = [] {
            const char* e = SDL_getenv("GD05_POLLSORT_LEGACY");
            return e && *e && *e != '0';
        }();
        if (pick == n && legacy)
            for (size_t i = 0; i < n && pick == n; ++i)
                if (!done[i]) pick = i;
        if (pick == n)
            for (size_t i = 0; i < n; ++i)
                if (!done[i] && (pick == n || indeg[i] < indeg[pick])) pick = i;
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
        case kBlender:
            if (ctx.pollBlender) ctx.pollBlender(p.name);
            break;
        case kFace:
            if (ctx.pollFace) ctx.pollFace(p.name);
            break;
        case kWalk:
            if (ctx.pollWalk) ctx.pollWalk(p.name);
            break;
        case kSetter: pollSetter(setters[p.index], ctx); break;
        case kHand: pollHand(hands[p.index]); break;
        case kMidi: pollMidi(midis[p.index], ctx); break;
        case kUpper: pollUpper(uppers[p.index]); break;
        case kFore: pollFore(fores[p.index]); break;
        case kLookAt: pollLookAt(lookats[p.index], ctx); break;
        case kEyes: pollEyes(eyes[p.index], ctx); break;
        case kHair: stepHair(hairs[p.index], ctx.dSeconds); break;
        case kConstraint: pollConstraint(constraints[p.index]); break;
        case kRod: pollRod(rods[p.index]); break;
        }
    }
}

// ---- CharWeightSetter ------------------------------------------------------

bool Rig::legacySetters = false;

void Rig::Impl::pollSetter(Setter& s, const RigContext& ctx) {
    Weight& w = weights[s.name];
    if (Rig::legacySetters) {
        if (!s.driver.empty() && ctx.driverFlagWeight) w.weight = ctx.driverFlagWeight(s.driver, s.flags);
        return;
    }
    if (!s.driver.empty() && ctx.driverFlagWeight) s.base = ctx.driverFlagWeight(s.driver, s.flags) * s.scale + s.offset;
    if (s.base == w.weight) return;
    if (s.beatsPerWeight > 0) {
        const float step = ctx.dBeats / s.beatsPerWeight;
        if (step <= 0) return;
        w.weight += std::clamp(s.base - w.weight, -step, step);
    } else {
        w.weight = s.base;
    }
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
    float w = weightOf(h.name);
    if (w == 0 || h.hand < 0 || h.targets.empty()) return;
    // Rock Band (Poll RB 0x8232EB48) [C]: several targets weigh 144 / their
    // squared local offset (0.001 at least) each; under a sum of 1 the IK's
    // weight shrinks by that sum. A clip calls a target by bringing it onto
    // its parent (bone_R-hand_mouth for the mic, _R-hip for a hand on the
    // hip); a target left at its rest offset (tens of units) barely pulls.
    std::vector<float> rbWeights;
    if (h.targets.size() > 1) {
        float sum = 0;
        for (int t : h.targets) {
            const scene::Xfm& l = sk.local(t);
            const float d2 = l.pos[0] * l.pos[0] + l.pos[1] * l.pos[1] + l.pos[2] * l.pos[2];
            rbWeights.push_back(144.0f / (d2 <= 0.001f ? 0.001f : d2));
            sum += rbWeights.back();
        }
        if (sum < 1) w *= sum;
        for (float& k : rbWeights) k /= sum;
    }
    // The target: one target's world position (and orientation with
    // orient), or the targets' average weighted by 1 / |their local offset|.
    V3 target{0, 0, 0};
    Q4 rot{0, 0, 0, 0};
    if (h.targets.size() == 1) {
        const scene::Xfm& t = sk.world(h.targets[0]);
        target = t.pos;
        if (h.orient) rot = mToQuat(orthoY(row(t.rot, 1), row(t.rot, 2)));
    } else {
        for (size_t i = 0; i < h.targets.size(); ++i) {
            const float k = rbWeights[i];
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
        // to [-1, 1] (Rock Band, RB 0x8232EB48 [C]: the arm or leg straightens
        // to reach; GH2's [-0.985, 0.88] kept it bent, and a stretch hand
        // then pulled the hand off the forearm: GD05-066 / 067); the
        // forearm's local rotation becomes that bend about z (whole matrix,
        // position kept).
        const V3 d = sub(sk.world(upper).pos, goal);
        const float c = std::clamp((dot(d, d) - h.a2b2) * h.inv2ab, -1.0f, 1.0f);
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
    // Rock Band (Poll RB 0x8233DE58) [C]: twist2's x = the hand's x x
    // length_scale, left alone without one (GH2 put it halfway).
    if (f.lengthScale > 0) b.pos[0] = hand.pos[0] * f.lengthScale;
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
    ++e.retargets;
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
    // A blink (0x82170BA8) [C]: the right lid's local m00 [I] below -0.1 and
    // just turning to open; one in three (RandomFloat, drawn only then, as
    // GH2's short-circuit) moves the eyes to a new point after it.
    bool blink = false;
    // Rock Band's CharEyes Poll (RB 0x82329588) [C]: the blink is the face
    // servo's Blink weight (servo +0x10c) falling from above 0.8 after it
    // went under 0.3; a new point after 3 s, or after 1 s on a blink or a
    // head turn; no random draw. GD05_GAZE_LEGACY=1: GH2's below.
    const bool rb = !gazeLegacy() && blinkValue >= 0;
    if (rb) {
        const float v = blinkValue;
        if (v < 0.3f) e.blinkArmed = true;
        if (e.blinkArmed && e.blinkLast > 0.8f && v < e.blinkLast) blink = true, e.blinkArmed = false;
        e.blinkLast = v;
    } else if (e.lid >= 0) {
        const float v = row(sk.local(e.lid).rot, 0)[0];
        const float dv = v - e.lidLast;
        blink = v < -0.1f && dv > 0 && e.lidDelta <= 0 && rng.real(0.0f, 1.0f) < 0.33f;
        e.lidLast = v, e.lidDelta = dv;
    }
    if (e.lastDot != kHuge) {
        e.smoothed = ((d - e.lastDot) - e.smoothed) * 0.1f + e.smoothed;
        const float hold = rb ? 3.0f : 8.0f;
        if (hold < e.timer || (1 < e.timer && (blink || (e.threshold < d && 0 < e.smoothed)))) retarget(e);
    }
    e.lastDot = d;
    e.lastForward = row(sk.world(e.head).rot, 1);
    if (!lidsLegacy()) followLids(e);
}

// Rock Band's CharEyes Poll (RB 0x82329588) [C], after each eye's look-at:
// a = the eye's look (its local y) x component x (1 - the face servo's Blink
// weight); the upper lid's local turned by -a about its z (0x8238ED60: each
// row (x, y) -> (x cos - y sin, x sin + y cos)), the lower lid's local x
// moved by 0.75 a. The port polls the look-ats before the eyes [I: RB polls
// them from here]; a lid no servo wrote since the last follow starts again
// from its pose before it.
void Rig::Impl::followLids(Eyes& e) {
    const float blink = blinkValue >= 0 ? blinkValue : 0.0f;
    for (size_t i = 0; i < e.lookats.size() && i < e.lids.size(); ++i) {
        const LookAt& l = lookats[e.lookats[i]];
        if (l.source < 0) continue;
        const float a = sk.local(l.source).rot[3] * (1 - blink);
        for (int k = 0; k < 2; ++k) {
            Eyes::Lid& lid = e.lids[i][size_t(k)];
            if (lid.bone < 0) continue;
            scene::Xfm x = sk.local(lid.bone);
            if (lid.has && x.rot == lid.after.rot && x.pos == lid.after.pos) x = lid.before;
            lid.before = x;
            if (k == 0) {
                const float c = std::cos(-a), sn = std::sin(-a);
                for (int r = 0; r < 3; ++r) {
                    const float x0 = x.rot[size_t(r * 3)], y0 = x.rot[size_t(r * 3 + 1)];
                    x.rot[size_t(r * 3)] = x0 * c - y0 * sn;
                    x.rot[size_t(r * 3 + 1)] = x0 * sn + y0 * c;
                }
            } else {
                x.pos[0] += a * 0.75f;
            }
            sk.setLocal(lid.bone, x);
            lid.after = x, lid.has = true;
        }
    }
}

// ---- CharHair --------------------------------------------------------------

// 0x8217B6F0: each point at the end of its bone (the bones' chain from the
// root's parent), velocities zero, the up vector its bone's z.
void Rig::Impl::resetHair(Hair& h) {
    h.relValid = false;
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

// Each CharCollide's world this step: its local under its parent bone.
void Rig::Impl::placeCollides() {
    collideWorld.resize(collides.size());
    for (size_t i = 0; i < collides.size(); ++i)
        collideWorld[i] = scene::compose(collides[i].local, collides[i].parent >= 0 ? sk.world(collides[i].parent) : sk.root());
}

// Rock Band's hookup (CharHair 0x823392A0, from the character's
// sync_objects) [C]: each strand with a root clears its points' collide
// lists; for each CharCollide of the character (its outfits merged) whose
// flags (+0xc4) share a bit with the strand's hookup_flags (+0x80), walking
// the points with L += length, a point gets the collide when
// c.worldRadius + max(radius, outer_radius) + L > |c.worldPos - root.worldPos|
// (0x823393E4..0x823394B4). The world radius (compute_radius 0x8232DB30,
// sent after the body deform) is |collideWorld x (0, radius, 0) - its
// position|: the radius x the collide's world y scale [C]. Run at the first
// poll (after the deform and the slot placement) [I: RB at sync_objects];
// also with GD05_HAIRCOLLIDE_LEGACY (the bandcheck measures RB's pairs), but
// then the step does not test them.
void Rig::Impl::hookupHair(Hair& h) {
    h.hooked = true;
    placeCollides();
    for (size_t i = 0; i < collides.size(); ++i) collides[i].worldRadius = collides[i].radius * len(row(collideWorld[i].rot, 1));
    int pairs = 0;
    for (auto& s : h.strands) {
        for (auto& p : s.points) p.hooks.clear();
        if (s.root < 0) continue;
        const V3 r = sk.world(s.root).pos;
        for (size_t ci = 0; ci < collides.size(); ++ci) {
            const Collide& c = collides[ci];
            if (!(s.hookupFlags & c.flags)) continue;
            const float d = len(sub(collideWorld[ci].pos, r));
            float l = 0;
            for (auto& p : s.points) {
                l += p.length;
                if (c.worldRadius + std::max(p.rbRadius, p.outer) + l > d) p.hooks.push_back(int(ci)), ++pairs;
            }
        }
    }
    static const bool log = SDL_getenv("GD05_HAIRLOG") != nullptr;
    if (log) {
        SDL_Log("hair %s/%s: %zu strands, %zu collides, %d pairs", dir.name.c_str(), h.name.c_str(), h.strands.size(), collides.size(), pairs);
        for (size_t ci = 0; ci < collides.size(); ++ci) {
            const Collide& c = collides[ci];
            SDL_Log("hair   collide %s shape %u flags %u r %.2f (world %.2f) length %.2f parent %s", c.name.c_str(), c.shape, c.flags,
                    double(c.radius), double(c.worldRadius), double(c.length), nameOf(c.parent).c_str());
        }
    }
}

namespace {
// A point's penetration into a collide in Rock Band's terms (> 0 inside):
// the hard radius (point radius + world radius) less the distance to the
// centre (shapes 1 / 2) or to the x segment [0, length] (3 / 4); the inside
// shapes (2 / 4) the other way round.
float hairPen(uint32_t shape, float worldRadius, float length, const scene::Xfm& w, const V3& pos, float r) {
    V3 o = sub(pos, w.pos);
    if (shape == 3 || shape == 4) {
        const V3 ax = normalize(row(w.rot, 0));
        o = sub(o, mul(ax, std::clamp(dot(o, ax), 0.0f, length)));
    } else if (shape != 1 && shape != 2) {
        return 0;
    }
    const float d = len(o);
    return shape == 1 || shape == 3 ? r + worldRadius - d : d - (worldRadius - r);
}
}  // namespace

// The bandcheck's measure after a step (GD05-113).
void Rig::Impl::measureHair(Hair& h, double rate) {
    RigHairStats& st = hairStats;
    ++st.steps;
    for (auto& s : h.strands) {
        if (s.root < 0) continue;
        const V3 r = sk.world(s.root).pos;
        V3 prev = r;
        for (auto& p : s.points) {
            for (int ci : p.hooks) {
                const Collide& c = collides[size_t(ci)];
                ++st.pairSamples;
                const float e = hairPen(c.shape, c.worldRadius, c.length, collideWorld[size_t(ci)], p.pos, p.rbRadius);
                if (e <= 1e-4f) continue;
                // Pushed out of a later collide of its list into this one: the
                // point sits on that collide's hard radius (RB's one ordered
                // pass leaves it there too).
                bool between = false;
                for (int cj : p.hooks)
                    if (cj != ci) {
                        const Collide& o = collides[size_t(cj)];
                        between = between || std::fabs(hairPen(o.shape, o.worldRadius, o.length, collideWorld[size_t(cj)], p.pos, p.rbRadius)) < 1e-3f;
                    }
                (between ? st.penBetween : st.pen).push_back(e);
            }
            float all = 0, mid = 0;
            const V3 m = mul(add(prev, p.pos), 0.5f);
            for (size_t ci = 0; ci < collides.size(); ++ci) {
                const Collide& c = collides[ci];
                all = std::max(all, hairPen(c.shape, c.worldRadius, c.length, collideWorld[ci], p.pos, p.rbRadius));
                mid = std::max(mid, hairPen(c.shape, c.worldRadius, c.length, collideWorld[ci], m, p.rbRadius));
            }
            ++st.pointSamples;
            if (all > 0) st.penAll.push_back(all);
            if (mid > 0) st.penMid.push_back(mid);
            const V3 rel = sub(p.pos, r);
            if (h.relValid) st.motion += len(sub(rel, p.rel)), st.motionSeconds += 1.0 / rate;
            p.rel = rel;
            prev = p.pos;
        }
    }
    h.relValid = true;
}

// 0x8217B898 (per poll, no time step: GH2 runs it once a frame):
// for each point, from the root: Verlet-like step (pos += vel, z -= 0.0643 x
// gravity), the segment held at its length (the parent point's velocity
// pulled by -0.5 x weight of the correction), collision (type 0: a plane
// through the collide bone, normal its y, at `radius`), the bone placed at
// the previous point aiming its y at this one (z from the frame's z,
// smoothed), then vel = stiffness x (rest - pos) - friction x (that change)
// + inertia x (pos - old pos), rest = the previous point + the frame's y x length.
// Rock Band's step (RB 0x82335B10, one a poll) [C]: the same order with the
// neighbour slack between the integration and the length, gravity
// g x -3.8582678 x 60 / rate^2 (rate: the PostProc's emulate_fps, else 60;
// GH2's -0.0643 is that at 60), and the collision against the point's
// hooked collides (hookupHair) after the length: it wins over the length.
void Rig::Impl::pollHair(Hair& h, double rate) {
    if (!h.simulate) return;
    if (h.reset) {
        if (!h.hooked) hookupHair(h);
        resetHair(h), h.reset = false;
        // RB's reset (0x82336F70) [C]: the chain rebuilt, then one step at
        // rate 15 with inertia (+0x18) and friction (+0x24) zeroed, restored.
        if (!hairResetLegacy()) {
            const float in = h.p[2], fr = h.p[5];
            h.p[2] = h.p[5] = 0;
            hairSettling = true;
            pollHair(h, 15);
            hairSettling = false;
            h.p[2] = in, h.p[5] = fr;
        }
    }
    const bool rbCollide = !hairCollideLegacy();
    const bool measure = hairMeasuring && !hairSettling;
    if (rbCollide || measure) placeCollides();
    const float gravity = hairGravityLegacy() ? h.p[3] * -0.06430447f : float(h.p[3] * -3.8582678 * 60.0 / (rate * rate));
    const float pull = h.p[4] * -0.5f;
    const bool slack = !hairSlackLegacy();
    const size_t ns = h.strands.size();
    for (size_t si = 0; si < ns; ++si) {
        auto& s = h.strands[si];
        if (s.root < 0) continue;
        const int rp = sk.parent(s.root);
        M3 frame = mulRot(s.base, rp >= 0 ? sk.world(rp).rot : sk.root().rot);
        V3 prev = sk.world(s.root).pos;
        for (size_t i = 0; i < s.points.size(); ++i) {
            auto& p = s.points[i];
            const V3 old = p.pos;
            p.pos = add(p.pos, p.vel);
            p.pos[2] += gravity;
            // RB's neighbour slack [C]: only with side_length >= 0, against
            // the same point of strand (s + 1) % n: out of [side - min_slack,
            // side + max_slack] this point moves by k x delta and the
            // neighbour's velocity by -k x delta, k = side^2 / (side^2 + d^2) - 0.5.
            if (slack && p.side >= 0 && ns > 1) {
                auto& nb = h.strands[(si + 1) % ns];
                if (i < nb.points.size()) {
                    auto& q = nb.points[i];
                    const V3 dd = sub(p.pos, q.pos);
                    const float d2 = dot(dd, dd), t2 = p.side * p.side;
                    const float lo = p.side - h.p[6], hi = p.side + h.p[7];
                    if ((d2 < lo * lo || d2 > hi * hi) && t2 + d2 > 0) {
                        const float k = t2 / (t2 + d2) - 0.5f;
                        p.pos = add(p.pos, mul(dd, k));
                        q.vel = sub(q.vel, mul(dd, k));
                    }
                }
            }
            V3 d = sub(p.pos, prev);
            const float dl = len(d);
            const float k = dl > 0 ? p.length / dl - 1 : 0;
            if (i > 0) s.points[i - 1].vel = add(s.points[i - 1].vel, mul(d, k * pull));
            p.pos = add(p.pos, mul(d, k));
            const V3 rest = add(prev, mul(row(frame, 1), p.length));
            V3 up = lerp(p.up, row(frame, 2), h.p[1]);
            if (rbCollide) {
                // RB's collision (0x82335B10) [C], per hooked collide, o = pos
                // - the collide's centre; 3 / 4 (capsule): o less x t, t =
                // o.x clamped to [0, length]; 1 / 3 stay outside: inner =
                // radius + c.r, outer = outer_radius + c.r; 2 / 4 inside:
                // c.r - radius / c.r - outer_radius. With no band (outer <=
                // inner) a hard push to inner; else past inner the push and
                // the up vector takes the push's direction, inside the band
                // only the up vector turns towards it by (outer - d) /
                // (outer - inner). Shape 0: RB notifies; over 4: skipped.
                // [I] the inside shapes (none on the disc) only push.
                for (int ci : p.hooks) {
                    const Collide& c = collides[size_t(ci)];
                    const scene::Xfm& w = collideWorld[size_t(ci)];
                    V3 o = sub(p.pos, w.pos);
                    if (c.shape == 3 || c.shape == 4) {
                        const V3 ax = normalize(row(w.rot, 0));
                        o = sub(o, mul(ax, std::clamp(dot(o, ax), 0.0f, c.length)));
                    } else if (c.shape != 1 && c.shape != 2) {
                        static bool once = false;
                        if (c.shape == 0 && !once) once = true, SDL_Log("hair %s: collide %s has shape 0", h.name.c_str(), c.name.c_str());
                        continue;
                    }
                    const bool outside = c.shape == 1 || c.shape == 3;
                    const float inner = outside ? p.rbRadius + c.worldRadius : c.worldRadius - p.rbRadius;
                    const float outer = outside ? p.outer + c.worldRadius : c.worldRadius - p.outer;
                    const float dist = len(o);
                    if (dist <= 0) continue;
                    const V3 dir = mul(o, 1 / dist);
                    const bool past = outside ? dist < inner : dist > inner;
                    if (!outside || outer <= inner) {
                        if (past) p.pos = add(p.pos, mul(dir, inner - dist));
                    } else if (past) {
                        p.pos = add(p.pos, mul(dir, inner - dist)), up = dir;
                    } else if (dist < outer) {
                        up = lerp(up, dir, (outer - dist) / (outer - inner));
                    }
                }
            } else if (p.collide >= 0) {
                // The collision (0x8217B898) against the collide bone [C]. Type 0:
                // a plane (its y row, at radius); 1: stay outside a sphere; 2: stay
                // inside a sphere; 3: stay outside a cylinder round its x row; 4:
                // stay inside that cylinder. The type is an int (it read as a
                // float once, so every strand was a plane and blew up). With a
                // soft band (+0x64, types 2 and 3) the position is corrected only
                // past the band's hard edge, the radius; inside the band the up
                // vector blends towards the collision's direction (0x82122100),
                // and past the edge it takes that direction.
                const scene::Xfm& c = sk.world(p.collide);
                const V3 o = sub(p.pos, c.pos);
                if (p.type == 0) {
                    const float n = dot(row(c.rot, 1), o);
                    if (n < p.radius && n != 0) p.pos = add(p.pos, mul(o, p.radius / n - 1));
                } else if (p.type == 1) {
                    const float l2 = dot(o, o);
                    if (l2 < p.radius * p.radius && l2 > 0) p.pos = add(p.pos, mul(o, p.radius / std::sqrt(l2) - 1));
                } else if (p.type == 2) {
                    const float l2 = dot(o, o);
                    if (p.band > 0) {
                        const float inner = p.radius - p.band;
                        if (l2 > inner * inner) {
                            const float d = std::sqrt(l2);
                            const V3 dir = mul(o, 1 / d);
                            if (p.radius < d) p.pos = add(p.pos, mul(dir, p.radius - d)), up = dir;
                            else up = lerp(up, dir, (d - inner) / p.band);
                        }
                    } else if (l2 > p.radius * p.radius) {
                        p.pos = add(p.pos, mul(o, p.radius / std::sqrt(l2) - 1));
                    }
                } else if (p.type == 3 || p.type == 4) {
                    const V3 axis = row(c.rot, 0);
                    const V3 perp = sub(o, mul(axis, dot(axis, o)));
                    const float l2 = dot(perp, perp);
                    const float outer = p.radius + p.band;
                    if (p.type == 3 && p.band > 0) {
                        if (l2 < outer * outer && l2 > 0) {
                            const float d = std::sqrt(l2);
                            const V3 dir = mul(perp, -1 / d);
                            if (p.radius <= d) up = lerp(up, dir, (outer - d) / p.band);
                            else p.pos = add(p.pos, mul(dir, d - p.radius)), up = dir;
                        }
                    } else {
                        const bool push = p.type == 3 ? l2 < p.radius * p.radius && l2 > 0 : l2 > p.radius * p.radius;
                        if (push) p.pos = add(p.pos, mul(perp, p.radius / std::sqrt(l2) - 1));
                    }
                }
            }
            frame = orthoY(sub(p.pos, prev), up);
            p.up = row(frame, 2);  // the new segment's z (0x8214F7F0 writes the frame first) [C]
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
    if (measure) measureHair(h, rate);
}

// GH2 polls the hair once a frame at 60 Hz, with no time step in the maths
// (0x8217B898) [C], so the port steps it on a 60 Hz clock whatever the frame
// rate (at most 4 steps a frame, so a hitch cannot fling the strands). A
// frame without a step still places the bones from the points, so the hair
// follows the head (the segments aimed as the last step left them) [I].
namespace {
double gHairStepHz = 60;
}  // namespace
void setHairStepHz(double hz) { gHairStepHz = hz > 0 ? hz : 60; }

void Rig::Impl::stepHair(Hair& h, double dSeconds) {
    const double kStep = 1.0 / gHairStepHz;
    h.clock += std::max(0.0, dSeconds);
    int n = int(h.clock / kStep);
    h.clock -= n * kStep;
    if (n > 4) n = 4, h.clock = 0;
    if (n == 0 || h.reset) {
        if (h.reset) return pollHair(h, gHairStepHz);
        return placeHair(h);
    }
    for (int i = 0; i < n; ++i) pollHair(h, gHairStepHz);
}

void Rig::Impl::placeHair(Hair& h) {
    if (!h.simulate) return;
    for (auto& s : h.strands) {
        if (s.root < 0) continue;
        V3 prev = sk.world(s.root).pos;
        for (auto& p : s.points) {
            if (p.bone >= 0) {
                scene::Xfm b;
                b.rot = orthoY(sub(p.pos, prev), p.up);
                b.pos = prev;
                sk.setWorld(p.bone, b);
            }
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

bool Rig::setSetter(const std::string& name, const std::string& key, float v) {
    for (auto& st : impl_->setters)
        if (st.name == name) {
            if (key == "base_weight") st.base = v;
            else if (key == "beats_per_weight") st.beatsPerWeight = v;
            else if (key == "offset") st.offset = v;
            else if (key == "scale") st.scale = v;
            else return false;
            return true;
        }
    return false;
}

bool Rig::newSpot(const std::string& ik, const std::string& spot, double beats) {
    for (auto& m : impl_->midis)
        if (m.name == ik) {
            const int s = impl_->sk.find(spot);
            if (s < 0) return false;
            impl_->newSpot(m, s, beats);
            return true;
        }
    return false;
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

void Rig::setBlinkValue(float v) { impl_->blinkValue = v; }

int Rig::eyeRetargets() const {
    int n = 0;
    for (const auto& e : impl_->eyes) n += e.retargets;
    return n;
}

void Rig::resetHair() {
    for (auto& h : impl_->hairs) h.reset = true;
}

RigHairStats Rig::takeHairStats() {
    Impl& m = *impl_;
    m.hairMeasuring = true;
    RigHairStats r = std::move(m.hairStats);
    m.hairStats = RigHairStats();
    r.collides = int(m.collides.size());
    r.points = r.rbPairs = 0;
    for (const auto& h : m.hairs) {
        if (!h.simulate) continue;
        for (const auto& s : h.strands) {
            if (s.root < 0) continue;
            r.points += int(s.points.size());
            for (const auto& p : s.points) r.rbPairs += int(p.hooks.size());
        }
    }
    r.usedPairs = hairCollideLegacy() ? 0 : r.rbPairs;
    return r;
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
