// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/band_ragdoll.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "data/qbkey.h"

namespace {

using bandanim::Mat34;
using bandanim::Quat;
using bandanim::Vec;
using Rt = AccessorySim::Rt;

constexpr double kStepMs = 1000.0 / 60.0;  // [I] GH3's frame time on the 360 (it keyframes with 1 / frame time [C 823FE8C8])
constexpr int kIterations = 4;             // hkWorldCinfo SOLVER_TYPE_4ITERS_MEDIUM [C 82253B10: setupSolverInfo(5)]
constexpr float kTau = 0.6f;               // solverTau [C the .rag worldCinfo; 4ITERS_MEDIUM]
constexpr float kDamp = 1.0f;              // solverDamp
constexpr float kFixError = 0.05f, kFixPart = 0.8f;  // checkAndFixConstraint(0.05, 0.8) [C 823FFC28: 8200C360, 820079F8]
constexpr size_t kHistory = 64;

Vec add(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec sub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec mul(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(Vec a) { return std::sqrt(dot(a, a)); }
Vec norm(Vec a) {
    const float l = len(a);
    return l > 1e-12f ? mul(a, 1.0f / l) : Vec{0, 0, 0};
}
Quat qnorm(Quat q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 1e-12f ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{};
}

// The rotation of a 3x3 (rows r0 r1 r2, column vectors), orthonormal or nearly.
Quat quatOf(const float r[3][3]) {
    const float tr = r[0][0] + r[1][1] + r[2][2];
    Quat q;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2;
        q = {(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25f * s};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2;
        q = {0.25f * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s};
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2;
        q = {(r[0][1] + r[1][0]) / s, 0.25f * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s};
    } else {
        const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2;
        q = {(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25f * s, (r[1][0] - r[0][1]) / s};
    }
    return qnorm(q);
}

// A Havok transform (column-major 4x4: column k = axis k, column 3 = translation).
Rt rtOfHavok(const std::array<float, 16>& m) {
    float r[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i][j] = m[size_t(j * 4 + i)];
    return {quatOf(r), {m[12], m[13], m[14]}};
}

// A model-space bone matrix (its columns normalised: a pure rotation).
Rt rtOfMat(const Mat34& m) {
    float r[3][3];
    for (int j = 0; j < 3; ++j) {
        const Vec c = norm({m.m[j], m.m[4 + j], m.m[8 + j]});
        r[0][j] = c.x, r[1][j] = c.y, r[2][j] = c.z;
    }
    return {quatOf(r), {m.m[3], m.m[7], m.m[11]}};
}

Rt rtMul(const Rt& a, const Rt& b) { return {qnorm(bandanim::qmul(a.q, b.q)), add(a.t, bandanim::qrot(a.q, b.t))}; }
Rt rtInv(const Rt& a) {
    const Quat c = bandanim::qconj(a.q);
    return {c, mul(bandanim::qrot(c, a.t), -1.0f)};
}
Mat34 matOf(const Rt& a) { return bandanim::matOf(bandanim::Xf{a.q, a.t}); }

// The inverse of an affine 3x4.
Mat34 inverse(const Mat34& m) {
    const float* a = m.m;
    const float c00 = a[5] * a[10] - a[6] * a[9], c01 = a[6] * a[8] - a[4] * a[10], c02 = a[4] * a[9] - a[5] * a[8];
    const float det = a[0] * c00 + a[1] * c01 + a[2] * c02;
    const float id = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
    Mat34 r;
    r.m[0] = c00 * id, r.m[1] = (a[2] * a[9] - a[1] * a[10]) * id, r.m[2] = (a[1] * a[6] - a[2] * a[5]) * id;
    r.m[4] = c01 * id, r.m[5] = (a[0] * a[10] - a[2] * a[8]) * id, r.m[6] = (a[2] * a[4] - a[0] * a[6]) * id;
    r.m[8] = c02 * id, r.m[9] = (a[1] * a[8] - a[0] * a[9]) * id, r.m[10] = (a[0] * a[5] - a[1] * a[4]) * id;
    for (int i = 0; i < 3; ++i) r.m[i * 4 + 3] = -(r.m[i * 4] * a[3] + r.m[i * 4 + 1] * a[7] + r.m[i * 4 + 2] * a[11]);
    return r;
}

Vec axisOf(const Quat& q, int k) { return bandanim::qrot(q, k == 0 ? Vec{1, 0, 0} : k == 1 ? Vec{0, 1, 0} : Vec{0, 0, 1}); }

// The rotation by the rotation vector w*h, applied after q.
Quat integrate(const Quat& q, Vec w, float h) {
    const float a = len(w) * h;
    if (a < 1e-9f) return q;
    const Vec ax = mul(w, 1.0f / len(w));
    const float s = std::sin(a * 0.5f);
    return qnorm(bandanim::qmul(Quat{ax.x * s, ax.y * s, ax.z * s, std::cos(a * 0.5f)}, q));
}

}  // namespace

bool AccessorySim::init(const bandanim::Rig& rig, const havok::Physics& rag, const std::vector<uint32_t>& bones) {
    bodies_.clear();
    cons_.clear();
    dynamic_ = 0;
    parent_ = rig.parent;
    bodyOf_.assign(rig.size(), -1);
    std::vector<int> index(rag.bodies.size(), -1);
    for (size_t i = 0; i < rag.bodies.size(); ++i) {
        const havok::Body& hb = rag.bodies[i];
        const uint32_t key = qbkey::key(hb.name);
        const int bone = rig.find(key);
        if (bone < 0 || bodyOf_[size_t(bone)] >= 0) continue;
        Body b;
        b.bone = bone;
        // ragdoll_setaccessorybones: the accessory bodies dynamic, the others keyframed [C 821B1418, 821AFC08].
        b.dynamic = std::find(bones.begin(), bones.end(), key) != bones.end() && hb.mass > 0;
        if (b.dynamic) {
            b.invMass = 1.0f / hb.mass;
            // MOTION_STABILIZED_SPHERE_INERTIA [C 821AFC08 setMotionType(3)]: one
            // inertia for every axis, Havok's sphere motion's largest of the
            // three [I: hkSphereMotion::setInertiaLocal].
            b.invInertia = std::min({hb.invInertia[0], hb.invInertia[1], hb.invInertia[2]});
            ++dynamic_;
        }
        b.com = {hb.comLocal[0], hb.comLocal[1], hb.comLocal[2]};
        b.maxLin = hb.maxLinearVelocity > 0 ? hb.maxLinearVelocity : 200.0f;
        b.maxAng = hb.maxAngularVelocity > 0 ? hb.maxAngularVelocity : 200.0f;
        b.linDamp = hb.linearDamping;
        b.angDamp = hb.angularDamping;
        // The skeleton mapper's simple mapping [C 821AF798 hkaSkeletonMapperUtils::createMapping
        // by name]: bone = body * (body bind)^-1 * (bone bind), the .rag's bodies
        // sitting at the bind pose [C data].
        const Rt bodyBind = rtOfHavok(hb.transform);
        const Rt boneBind = rtOfMat(inverse(rig.invBind[size_t(bone)]));
        b.boneFromBody = rtMul(rtInv(bodyBind), boneBind);
        index[i] = int(bodies_.size());
        bodyOf_[size_t(bone)] = int(bodies_.size());
        bodies_.push_back(b);
        if (SDL_getenv("GD10_RAGDOLL_LOG"))
            SDL_Log("ragdoll #%d %s: %s mass %.2f invI %.2f com (%.3f %.3f %.3f) damp %.2f/%.2f maxAng %.1f; body-to-bone (%.4f %.4f %.4f)",
                    bone, hb.name.c_str(), b.dynamic ? "dynamic" : "keyframed", hb.mass, b.invInertia, b.com.x, b.com.y, b.com.z, b.linDamp,
                    b.angDamp, b.maxAng, b.boneFromBody.t.x, b.boneFromBody.t.y, b.boneFromBody.t.z);
    }
    if (!dynamic_) return false;
    for (const havok::Constraint& hc : rag.constraints) {
        if (hc.bodyA < 0 || hc.bodyB < 0 || size_t(hc.bodyA) >= index.size() || size_t(hc.bodyB) >= index.size()) continue;
        Con c;
        c.a = index[size_t(hc.bodyA)];
        c.b = index[size_t(hc.bodyB)];
        if (c.a < 0 || c.b < 0) continue;
        c.kind = hc.type == "hkRagdollConstraintData"        ? Con::Ragdoll
                 : hc.type == "hkLimitedHingeConstraintData" ? Con::Hinge
                 : hc.type == "hkPrismaticConstraintData"    ? Con::Prismatic
                                                             : Con::Other;
        // In the world only when its child is an accessory body [C 821AFC08:
        // addConstraint for the accessory bones, removeConstraint for the others].
        c.active = bodies_[size_t(c.a)].dynamic && c.kind != Con::Other;
        c.fa = rtOfHavok(hc.transformA);
        c.fb = rtOfHavok(hc.transformB);
        c.d = hc;
        if (SDL_getenv("GD10_RAGDOLL_LOG"))
            SDL_Log("ragdoll con bodies %s -> %s (bones %d %d)", rag.bodies[size_t(hc.bodyA)].name.c_str(), rag.bodies[size_t(hc.bodyB)].name.c_str(),
                    bodies_[size_t(c.a)].bone, bodies_[size_t(c.b)].bone);
        if (SDL_getenv("GD10_RAGDOLL_LOG"))
            SDL_Log("ragdoll con %s %s %s: twist %d [%.2f %.2f] tau %.2f ax %d/%d; cone %d max %.2f al %d ax %d/%d tau %.2f; planes %d [%.2f %.2f] al %d ax %d/%d; fric %.3f x%d",
                    hc.name.c_str(), hc.type.c_str(), c.active ? "active" : "off", hc.twistOn, hc.twistMin, hc.twistMax, hc.twistTau, hc.twistAxis, hc.twistRefAxis,
                    hc.coneOn, hc.coneMax, hc.coneAligned, hc.coneAxisA, hc.coneAxisB, hc.coneTau, hc.planesOn, hc.planesMin, hc.planesMax, hc.planesAligned,
                    hc.planesAxisA, hc.planesAxisB, hc.maxFriction, hc.frictionAxes);
        cons_.push_back(c);
    }
    // A dynamic body constrained to a body that is not its skeleton ancestor
    // follows its animation instead (keyframed): Rob's .rag ties
    // Bone_ACC_Head_09 (a braid bone under his head, 0.1 kg, zero cone and
    // planes) to Bone_Ankle_R, so the solver dragged it between his head and
    // his ankle, 2 m a frame [C data; I: the original's result unknown, the
    // bone keeps its animation].
    for (Con& c : cons_) {
        if (!c.active) continue;
        const int child = bodies_[size_t(c.a)].bone, par = bodies_[size_t(c.b)].bone;
        bool ancestor = false;
        for (int p = parent_[size_t(child)]; p >= 0 && !ancestor; p = parent_[size_t(p)]) ancestor = p == par;
        if (ancestor) continue;
        SDL_Log("ragdoll: %s is tied to %s, not one of its skeleton's parents: animated, not simulated", c.d.name.c_str(),
                rag.bodies[size_t(c.d.bodyB)].name.c_str());
        Body& b = bodies_[size_t(c.a)];
        if (b.dynamic) b.dynamic = false, b.invMass = b.invInertia = 0, --dynamic_;
        for (Con& o : cons_)
            if (o.a == c.a) o.active = false;
    }
    started_ = false;
    history_.clear();
    return dynamic_ > 0;
}

void AccessorySim::targets(const std::vector<Mat34>& world, std::vector<Rt>& out) const {
    out.resize(bodies_.size());
    for (size_t i = 0; i < bodies_.size(); ++i) {
        const Body& b = bodies_[i];
        out[i] = size_t(b.bone) < world.size() ? rtMul(rtOfMat(world[size_t(b.bone)]), rtInv(b.boneFromBody)) : Rt{};
    }
}

// One row of the velocity solver: J = (linA, angA, linB, angB), the
// target velocity `rhs`, the accumulated impulse clamped to [lo, hi].
struct AccessorySim::SolverRow {
    int a = -1, b = -1;
    Vec linA, angA, linB, angB;
    float rhs = 0, lo = -1e30f, hi = 1e30f;
    float k = 0, acc = 0;  // the effective mass's inverse, the impulse so far
};

void AccessorySim::rows(const std::vector<BodyState>& s, const Con& c, float h, std::vector<SolverRow>& out) const {
    const Body& A = bodies_[size_t(c.a)];
    const Body& B = bodies_[size_t(c.b)];
    const BodyState& sa = s[size_t(c.a)];
    const BodyState& sb = s[size_t(c.b)];
    const Quat qa = bandanim::qmul(sa.x.q, c.fa.q), qb = bandanim::qmul(sb.x.q, c.fb.q);
    const Vec fa[3] = {axisOf(qa, 0), axisOf(qa, 1), axisOf(qa, 2)};
    const Vec fb[3] = {axisOf(qb, 0), axisOf(qb, 1), axisOf(qb, 2)};
    const Vec pa = add(sa.x.t, bandanim::qrot(sa.x.q, c.fa.t)), pb = add(sb.x.t, bandanim::qrot(sb.x.q, c.fb.t));
    const Vec ca = add(sa.x.t, bandanim::qrot(sa.x.q, A.com)), cb = add(sb.x.t, bandanim::qrot(sb.x.q, B.com));
    const Vec ra = sub(pa, ca), rb = sub(pb, cb);
    const float inv = 1.0f / h;
    auto apply = [&](SolverRow r) {
        r.k = A.invMass * dot(r.linA, r.linA) + A.invInertia * dot(r.angA, r.angA) + B.invMass * dot(r.linB, r.linB) +
              B.invInertia * dot(r.angB, r.angB);
        if (r.k < 1e-12f) return;
        r.a = c.a, r.b = c.b;
        out.push_back(r);
    };
    // A linear row along n between the pivots, C = (pa - pb).n.
    auto linear = [&](Vec n, float err, float lo, float hi) {
        SolverRow r;
        r.linA = n, r.angA = cross(ra, n), r.linB = mul(n, -1), r.angB = mul(cross(rb, n), -1);
        r.rhs = -kTau * err * inv, r.lo = lo, r.hi = hi;
        apply(r);
    };
    // An angular row about n (the relative angle grows with (wA - wB).n).
    auto angular = [&](Vec n, float rhs, float lo, float hi) {
        SolverRow r;
        r.angA = n, r.angB = mul(n, -1), r.rhs = rhs, r.lo = lo, r.hi = hi;
        apply(r);
    };
    // A two-sided angle limit: angle in [mn, mx] with the limit's tau.
    // A limit row is in the solve whether or not the limit is reached
    // (Havok's limit atoms): inside, the joint may close the remaining gap
    // within the step and no further; beyond, it is pushed back by tau.
    // Only the rows of limits already broken (as before, GD10-044) let a
    // swinging braid sail through its cone within the frame (36 degrees past
    // it at the 95th percentile in Enter Sandman).
    auto toLimit = [&](float over, float tau) { return over > 0 ? -kTau * tau * over * inv : -over * inv; };
    auto limit = [&](Vec n, float angle, float mn, float mx, float tau) {
        angular(n, toLimit(angle - mx, tau), -1e30f, 0.0f);
        angular(n, -toLimit(mn - angle, tau), 0.0f, 1e30f);
    };
    const havok::Constraint& d = c.d;
    if (c.kind == Con::Ragdoll || c.kind == Con::Hinge) {
        // Ball socket [C atoms TYPE_BALL_SOCKET].
        const Vec e = sub(pa, pb);
        linear({1, 0, 0}, e.x, -1e30f, 1e30f);
        linear({0, 1, 0}, e.y, -1e30f, 1e30f);
        linear({0, 0, 1}, e.z, -1e30f, 1e30f);
    }
    if (c.kind == Con::Ragdoll) {
        if (d.twistOn) {
            // The twist about the mean of the twist axes, measured between the ref axes.
            const Vec tw = norm(add(fa[d.twistAxis], fb[d.twistAxis]));
            Vec ra2 = fa[d.twistRefAxis], rb2 = fb[d.twistRefAxis];
            ra2 = norm(sub(ra2, mul(tw, dot(ra2, tw))));
            rb2 = norm(sub(rb2, mul(tw, dot(rb2, tw))));
            const float ang = std::atan2(dot(cross(rb2, ra2), tw), dot(rb2, ra2));
            limit(tw, ang, d.twistMin, d.twistMax, d.twistTau);
        }
        auto coneRow = [&](int axA, int axB, bool aligned, float mn, float mx, float tau) {
            const Vec a = fa[axA], b = fb[axB];
            if (aligned) {
                // The angle between the axes: only its maximum [I: minAngle -100 on the disc].
                const Vec n = cross(b, a);
                if (len(n) < 1e-6f) return;
                const float ang = std::acos(std::clamp(dot(a, b), -1.0f, 1.0f));
                angular(norm(n), toLimit(ang - mx, tau), -1e30f, 0.0f);
            } else {
                // asin(a.b): zero when perpendicular, in [mn, mx].
                const Vec n = cross(a, b);
                if (len(n) < 1e-6f) return;
                const float ang = std::asin(std::clamp(dot(a, b), -1.0f, 1.0f));
                limit(norm(n), ang, mn, mx, tau);
            }
        };
        if (d.coneOn) coneRow(d.coneAxisA, d.coneAxisB, d.coneAligned, -1e30f, d.coneMax, d.coneTau);
        if (d.planesOn) coneRow(d.planesAxisA, d.planesAxisB, d.planesAligned, d.planesMin, d.planesMax, d.planesTau);
    } else if (c.kind == Con::Hinge) {
        // 2dAng: the hinge axes kept parallel.
        const int h0 = d.hingeAxis, h1 = (h0 + 1) % 3, h2 = (h0 + 2) % 3;
        for (int k : {h1, h2}) {
            const Vec u = fa[h0], v = fb[k];
            angular(cross(u, v), -kTau * dot(u, v) * inv, -1e30f, 1e30f);
        }
        if (d.twistOn) {
            const Vec ax = fb[h0];
            const float ang = std::atan2(dot(cross(fb[h1], fa[h1]), ax), dot(fb[h1], fa[h1]));
            limit(ax, ang, d.twistMin, d.twistMax, d.twistTau);
        }
    } else if (c.kind == Con::Prismatic) {
        // TYPE_ANG (3 axes): the frames' rotations kept equal.
        Quat dq = bandanim::qmul(qa, bandanim::qconj(qb));
        if (dq.w < 0) dq = {-dq.x, -dq.y, -dq.z, -dq.w};
        const Vec e = {2 * dq.x, 2 * dq.y, 2 * dq.z};
        angular({1, 0, 0}, -kTau * e.x * inv, -1e30f, 1e30f);
        angular({0, 1, 0}, -kTau * e.y * inv, -1e30f, 1e30f);
        angular({0, 0, 1}, -kTau * e.z * inv, -1e30f, 1e30f);
        const Vec dp = sub(pa, pb);
        linear(fb[d.linLock0], dot(dp, fb[d.linLock0]), -1e30f, 1e30f);
        linear(fb[d.linLock1], dot(dp, fb[d.linLock1]), -1e30f, 1e30f);
        const Vec ax = fb[d.linAxis];
        const float x = dot(dp, ax);
        if (x > d.linMax) linear(ax, x - d.linMax, -1e30f, 0.0f);
        else if (x < d.linMin) linear(ax, x - d.linMin, 0.0f, 1e30f);
        if (d.maxFriction > 0) linear(ax, 0.0f, -d.maxFriction * h, d.maxFriction * h);
    }
    if ((c.kind == Con::Ragdoll || c.kind == Con::Hinge) && d.maxFriction > 0) {
        // Angular friction: the relative spin held by up to maxFrictionTorque.
        const float lim = d.maxFriction * h;
        const int first = c.kind == Con::Hinge ? d.hingeAxis : 0;
        for (int k = 0; k < std::min(3, std::max(1, d.frictionAxes)); ++k) angular(fb[(first + k) % 3], 0.0f, -lim, lim);
    }
}

void AccessorySim::step(State& s, const std::vector<Mat34>& world, const Vec& gravity) const {
    const float dt = float(kStepMs / 1000.0);
    std::vector<Rt> tgt;
    targets(world, tgt);
    // Hard keyframes (hkKeyFrameUtility::applyHardKeyFrame, 1 / frame time [C 823FE8C8]):
    // the velocities that take each keyframed body to the animated pose.
    for (size_t i = 0; i < bodies_.size(); ++i) {
        const Body& b = bodies_[i];
        if (b.dynamic) continue;
        BodyState& st = s.b[i];
        const Vec c0 = add(st.x.t, bandanim::qrot(st.x.q, b.com)), c1 = add(tgt[i].t, bandanim::qrot(tgt[i].q, b.com));
        st.v = mul(sub(c1, c0), 1.0f / dt);
        Quat dq = bandanim::qmul(tgt[i].q, bandanim::qconj(st.x.q));
        if (dq.w < 0) dq = {-dq.x, -dq.y, -dq.z, -dq.w};
        const float sn = std::sqrt(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z);
        const float ang = 2.0f * std::atan2(sn, dq.w);
        st.w = sn > 1e-9f ? mul(Vec{dq.x, dq.y, dq.z}, ang / sn / dt) : Vec{0, 0, 0};
    }
    // checkAndFixConstraint(0.05, 0.8) from the last constraint until one is fixed
    // [C 823FFC28; I: the child (entity A) moved by 0.8 of the pivots' error].
    for (size_t k = cons_.size(); k-- > 0;) {
        const Con& c = cons_[k];
        BodyState& sa = s.b[size_t(c.a)];
        const BodyState& sb = s.b[size_t(c.b)];
        const Vec pa = add(sa.x.t, bandanim::qrot(sa.x.q, c.fa.t)), pb = add(sb.x.t, bandanim::qrot(sb.x.q, c.fb.t));
        const Vec e = sub(pb, pa);
        if (dot(e, e) > kFixError * kFixError) {
            sa.x.t = add(sa.x.t, mul(e, kFixPart));
            break;
        }
    }
    // Damping and gravity, then the solver: hkpWorldCinfo
    // SOLVER_TYPE_4ITERS_MEDIUM is one micro-step of four Gauss-Seidel
    // iterations (tau 0.6, damping 1) [Havok's documented solver types; C
    // 82253B10 setupSolverInfo(5)], each row's impulse accumulated and
    // clamped; the velocity caps; one integration over the frame. GD10-044
    // before: four sub-steps of one pass each with their own error
    // correction (four times the stiffness and the injected energy: hair tips
    // spun at up to 176 rad/s, 150..180 degrees off their animation).
    for (size_t i = 0; i < bodies_.size(); ++i) {
        const Body& b = bodies_[i];
        if (!b.dynamic) continue;
        s.b[i].v = mul(s.b[i].v, std::max(0.0f, 1.0f - b.linDamp * dt));
        s.b[i].w = mul(s.b[i].w, std::max(0.0f, 1.0f - b.angDamp * dt));
        s.b[i].v = add(s.b[i].v, mul(gravity, dt));
    }
    std::vector<SolverRow> rs;
    for (const Con& c : cons_)
        if (c.active) rows(s.b, c, dt, rs);
    for (int it = 0; it < kIterations; ++it)
        for (SolverRow& r : rs) {
            BodyState& sa = s.b[size_t(r.a)];
            BodyState& sb = s.b[size_t(r.b)];
            const Body& A = bodies_[size_t(r.a)];
            const Body& B = bodies_[size_t(r.b)];
            const float jv = dot(r.linA, sa.v) + dot(r.angA, sa.w) + dot(r.linB, sb.v) + dot(r.angB, sb.w);
            const float acc = std::clamp(r.acc + kDamp * (r.rhs - jv) / r.k, r.lo, r.hi);
            const float l = acc - r.acc;
            r.acc = acc;
            sa.v = add(sa.v, mul(r.linA, A.invMass * l));
            sa.w = add(sa.w, mul(r.angA, A.invInertia * l));
            sb.v = add(sb.v, mul(r.linB, B.invMass * l));
            sb.w = add(sb.w, mul(r.angB, B.invInertia * l));
        }
    for (size_t i = 0; i < bodies_.size(); ++i) {
        const Body& b = bodies_[i];
        BodyState& st = s.b[i];
        if (b.dynamic) {
            const float lv = len(st.v), av = len(st.w);
            if (lv > b.maxLin) st.v = mul(st.v, b.maxLin / lv);
            if (av > b.maxAng) st.w = mul(st.w, b.maxAng / av);
        }
        const Vec com = add(add(st.x.t, bandanim::qrot(st.x.q, b.com)), mul(st.v, dt));
        st.x.q = integrate(st.x.q, st.w, dt);
        st.x.t = sub(com, bandanim::qrot(st.x.q, b.com));
    }
}

void AccessorySim::pose(const State& s, std::vector<Mat34>& world) const {
    // The ragdoll's pose mapped to the skeleton; the accessory bones take its
    // local transforms under their (current) animated parents, their
    // descendants follow [C 823FFC28: mapPose, then model-to-local].
    const std::vector<Mat34> anim = world;
    std::vector<char> moved(world.size(), 0);
    auto ragModel = [&](int bone) -> Mat34 {
        const int bi = bone >= 0 && size_t(bone) < bodyOf_.size() ? bodyOf_[size_t(bone)] : -1;
        return bi >= 0 ? matOf(rtMul(s.b[size_t(bi)].x, bodies_[size_t(bi)].boneFromBody)) : anim[size_t(bone)];
    };
    for (size_t b = 0; b < world.size(); ++b) {
        const int p = b < parent_.size() ? parent_[b] : -1;
        const bool hasParent = p >= 0 && size_t(p) < b;
        const int bi = b < bodyOf_.size() ? bodyOf_[b] : -1;
        if (bi >= 0 && bodies_[size_t(bi)].dynamic) {
            const Mat34 m = ragModel(int(b));
            world[b] = hasParent ? bandanim::mmul(world[size_t(p)], bandanim::mmul(inverse(ragModel(p)), m)) : m;
            moved[b] = 1;
        } else if (hasParent && moved[size_t(p)]) {
            world[b] = bandanim::mmul(world[size_t(p)], bandanim::mmul(inverse(anim[size_t(p)]), anim[b]));
            moved[b] = 1;
        }
    }
}

void AccessorySim::apply(double ms, std::vector<Mat34>& world, const std::function<void(double, std::vector<Mat34>&)>& animate,
                         const Vec& gravity, const std::vector<double>* resets) {
    if (!dynamic_) return;
    // The pose read at a frame is the one before that frame's step [C 823FE140:
    // keyframe, read, then the world steps]: the state after step k - 1 at
    // step k's time. Between two step times the shown pose is interpolated
    // from the states after k - 1 and k [I: GH3 steps once per 60 Hz frame,
    // so it never shows a step held; a frame here (any refresh rate, frame
    // times that jitter around 16.7 ms) took the newest step's state as is,
    // and the accessories held a step for two frames then jumped two: the
    // hair / chains stuttered while the body moved smoothly, GD10-104].
    const double pos = ms / kStepMs;
    const int64_t want = int64_t(std::floor(pos));  // the newest step whose time is not after ms
    const float frac = float(pos - std::floor(pos));
    std::vector<Mat34> w;
    auto restart = [&](int64_t s) {
        // ragdoll setPoseModelSpace: every body at the animated pose, at rest [C 823FE8C8 / 826BCEE0].
        w.clear();
        animate(double(s) * kStepMs, w);
        std::vector<Rt> tgt;
        targets(w, tgt);
        cur_.step = s;
        cur_.b.assign(bodies_.size(), {});
        for (size_t i = 0; i < bodies_.size(); ++i) cur_.b[i].x = tgt[i];
        history_.clear();
        history_.push_back(cur_);
        started_ = true;
    };
    if (!started_ || want > cur_.step + 120 || want - 1 < history_.front().step) restart(want - 1);
    while (cur_.step < want) {
        ++cur_.step;
        w.clear();
        const double t1 = double(cur_.step) * kStepMs, t0 = t1 - kStepMs;
        animate(t1, w);
        // ragdoll_markforreset between the two steps: the bodies on the
        // animated pose, at rest (a mocap clip snaps the pose with no
        // blend: stepping through it threw the hair, the keyframed head
        // moving metres in a frame) [C scripts handle_moment_anim_blending,
        // end_moment_anim; I: the reset as setPoseModelSpace at rest].
        const bool reset = resets && std::any_of(resets->begin(), resets->end(), [&](double r) { return r > t0 && r <= t1; });
        if (reset) {
            std::vector<Rt> tgt;
            targets(w, tgt);
            for (size_t i = 0; i < bodies_.size(); ++i) cur_.b[i] = BodyState{tgt[i], {}, {}};
        } else {
            step(cur_, w, gravity);
        }
        if (cur_.step % 60 == 0 && SDL_getenv("GD10_RAGDOLL_LOG")) {
            // Development aid: the worst pivot gap of the constraints in the world,
            // the fastest accessory body, the largest swing off the animation.
            std::vector<Rt> tgt;
            targets(w, tgt);
            float gap = 0, vmax = 0, wmax = 0, swing = 0, coneOver = -10;
            for (const Con& c : cons_) {
                if (!c.active) continue;
                const BodyState &sa = cur_.b[size_t(c.a)], &sb = cur_.b[size_t(c.b)];
                gap = std::max(gap, len(sub(add(sa.x.t, bandanim::qrot(sa.x.q, c.fa.t)), add(sb.x.t, bandanim::qrot(sb.x.q, c.fb.t)))));
                if (c.kind == Con::Ragdoll && c.d.coneOn) {
                    const Quat qa = bandanim::qmul(sa.x.q, c.fa.q), qb = bandanim::qmul(sb.x.q, c.fb.q);
                    const float cone = std::acos(std::clamp(dot(axisOf(qa, c.d.coneAxisA), axisOf(qb, c.d.coneAxisB)), -1.0f, 1.0f));
                    coneOver = std::max(coneOver, cone - c.d.coneMax);
                }
            }
            int wBody = -1, sBody = -1;
            for (size_t i = 0; i < bodies_.size(); ++i) {
                if (!bodies_[i].dynamic) continue;
                vmax = std::max(vmax, len(cur_.b[i].v));
                if (len(cur_.b[i].w) > wmax) wmax = len(cur_.b[i].w), wBody = bodies_[i].bone;
                const float d = std::fabs(bandanim::qmul(cur_.b[i].x.q, bandanim::qconj(tgt[i].q)).w);
                if (2.0f * std::acos(std::min(1.0f, d)) > swing) swing = 2.0f * std::acos(std::min(1.0f, d)), sBody = bodies_[i].bone;
            }
            SDL_Log("ragdoll step %lld: gap %.4f m, |v| %.2f m/s, |w| %.2f rad/s (bone %d), swing %.1f deg (bone %d), cone over %.1f deg",
                    static_cast<long long>(cur_.step), gap, vmax, wmax, wBody, swing * 57.29578f, sBody, coneOver * 57.29578f);
        }
        history_.push_back(cur_);
        while (history_.size() > kHistory) history_.pop_front();
    }
    const State *a = nullptr, *b = nullptr;
    for (const State& h : history_) {
        if (h.step == want - 1) a = &h;
        if (h.step == want) b = &h;
    }
    if (!b) b = &cur_;
    if (!a || frac <= 0.0f) {
        pose(*(a ? a : b), world);
        return;
    }
    State mix;
    mix.step = a->step;
    mix.b.resize(a->b.size());
    for (size_t i = 0; i < a->b.size(); ++i) {
        const Rt &x0 = a->b[i].x, &x1 = b->b[i].x;
        mix.b[i].x = {bandanim::nlerp(x0.q, x1.q, frac), add(x0.t, mul(sub(x1.t, x0.t), frac))};
    }
    pose(mix, world);
}

std::string AccessorySim::selfTest() {
    bandanim::Rig rig;
    rig.parent = {-1, 0};
    rig.bind.resize(2);
    rig.bind[1].t = {0.1f, 0, 0};
    rig.invBind.resize(2);
    rig.invBind[1].m[3] = -0.1f;
    rig.index = {{qbkey::key("Bone_Head"), 0}, {qbkey::key("Bone_ACC_Head_01"), 1}};
    havok::Physics ph;
    havok::Body head, hair;
    head.name = "Bone_Head", head.mass = 10;
    hair.name = "Bone_ACC_Head_01", hair.mass = 1, hair.comLocal = {0.05f, 0, 0}, hair.invInertia = {300, 300, 300}, hair.angularDamping = 0.05f;
    for (havok::Body* b : {&head, &hair}) b->transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    hair.transform[12] = 0.1f;
    ph.bodies = {head, hair};
    havok::Constraint c;
    c.name = "Bone_ACC_Head_01", c.type = "hkRagdollConstraintData", c.bodyA = 1, c.bodyB = 0;
    c.transformA = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    c.transformB = c.transformA;
    c.transformB[12] = 0.1f;
    c.coneOn = c.twistOn = true, c.coneMax = 0.3f, c.coneTau = c.twistTau = 0.8f, c.coneAligned = true;
    ph.constraints = {c};
    AccessorySim sim;
    if (!sim.init(rig, ph, {qbkey::key("Bone_ACC_Head_01")})) return "no dynamic body";
    auto animate = [&](double, std::vector<Mat34>& w) {
        w.assign(2, Mat34{});
        w[1].m[3] = 0.1f;
    };
    float worstCone = 0, worstGap = 0;
    for (int f = 1; f <= 180; ++f) {
        std::vector<Mat34> w;
        animate(0, w);
        sim.apply(f * kStepMs, w, animate, {0, -9.8f, 0});
        const Vec x = norm({w[1].m[0], w[1].m[4], w[1].m[8]});
        worstCone = std::max(worstCone, std::acos(std::clamp(x.x, -1.0f, 1.0f)));
        worstGap = std::max(worstGap, len(sub(Vec{w[1].m[3], w[1].m[7], w[1].m[11]}, Vec{0.1f, 0, 0})));
    }
    char buf[160];
    if (worstCone > 0.3f + 0.05f || worstGap > 0.01f) {
        std::snprintf(buf, sizeof buf, "the hanging segment broke its joint: %.1f degrees off the head (cone 17.2), pivot %.3f m apart", worstCone * 57.29578f,
                      worstGap);
        return buf;
    }
    return "";
}
