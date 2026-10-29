// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/band_ragdoll.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>

#include "data/qbkey.h"

namespace {

using bandanim::Mat34;
using bandanim::Quat;
using bandanim::Vec;

constexpr double kStepMs = 1000.0 / 60.0;
constexpr size_t kHistory = 64;

Vec add(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec sub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec mul(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(Vec a) { return std::sqrt(dot(a, a)); }
Vec norm(Vec a) {
    const float l = len(a);
    return l > 1e-8f ? mul(a, 1.0f / l) : Vec{0, 0, 0};
}
Vec posOf(const Mat34& m) { return {m.m[3], m.m[7], m.m[11]}; }

// The shortest rotation taking direction a to direction b.
Quat between(Vec a, Vec b) {
    a = norm(a), b = norm(b);
    const float d = dot(a, b);
    if (d > 0.999999f) return {};
    if (d < -0.999999f) {
        Vec axis = cross(a, {1, 0, 0});
        if (len(axis) < 1e-4f) axis = cross(a, {0, 1, 0});
        axis = norm(axis);
        return {axis.x, axis.y, axis.z, 0};
    }
    const Vec c = cross(a, b);
    const float s = std::sqrt((1 + d) * 2);
    return {c.x / s, c.y / s, c.z / s, s * 0.5f};
}

// A rotation by q about the point p, as a 3x4 matrix.
Mat34 about(const Quat& q, Vec p) {
    bandanim::Xf x;
    x.q = q;
    const Vec r = bandanim::qrot(q, p);
    x.t = sub(p, r);
    return bandanim::matOf(x);
}

}  // namespace

bool AccessorySim::init(const bandanim::Rig& rig, const havok::Physics& rag, const std::vector<uint32_t>& bones) {
    links_.clear();
    parent_ = rig.parent;
    linkOf_.assign(rig.size(), -1);
    for (size_t i = 0; i < rag.bodies.size(); ++i) {
        const havok::Body& body = rag.bodies[i];
        const uint32_t key = qbkey::key(body.name);
        if (std::find(bones.begin(), bones.end(), key) == bones.end()) continue;
        const int b = rig.find(key);
        if (b < 0 || body.mass <= 0) continue;
        Link l;
        l.bone = b;
        // The tip: the far end of the body's shape in its frame (the bone's
        // bind frame [I]): a capsule's farther end point, a translated box's
        // centre doubled, else the box's longest half extent.
        const havok::Shape& s = body.shape;
        Vec tip{};
        if (s.kind == havok::Shape::Capsule) {
            const Vec a{s.a[0], s.a[1], s.a[2]}, c{s.b[0], s.b[1], s.b[2]};
            tip = len(a) > len(c) ? a : c;
            tip = add(tip, mul(norm(tip), s.radius));
        } else {
            const Vec t{s.translation[0], s.translation[1], s.translation[2]};
            if (len(t) > 1e-4f) {
                tip = mul(t, 2.0f);
            } else {
                const int ax = s.a[0] >= s.a[1] && s.a[0] >= s.a[2] ? 0 : s.a[1] >= s.a[2] ? 1 : 2;
                float v[3] = {0, 0, 0};
                v[ax] = s.a[size_t(ax)] * 2;
                tip = {v[0], v[1], v[2]};
            }
        }
        l.tip = tip;
        l.len = len(tip);
        if (l.len < 1e-3f) continue;
        l.damping = std::clamp(std::max(body.linearDamping, body.angularDamping), 0.0f, 20.0f);
        for (const havok::Constraint& c : rag.constraints)
            if (c.bodyA == int(i) && c.coneMax > 0) l.cone = c.coneMax;
        if (l.cone <= 0) l.cone = 0.6f;  // a hinge or no limit: a modest swing [I]
        if (SDL_getenv("GD04_RAGDOLL_LOG")) {
            // The body's frame against the bone's bind frame (model space): the bind
            // matrix is the inverse of invBind (rigid: p = -R^T t).
            const Mat34& ib = rig.invBind[size_t(b)];
            const Vec t{ib.m[3], ib.m[7], ib.m[11]};
            const Vec bp{-(ib.m[0] * t.x + ib.m[4] * t.y + ib.m[8] * t.z), -(ib.m[1] * t.x + ib.m[5] * t.y + ib.m[9] * t.z),
                         -(ib.m[2] * t.x + ib.m[6] * t.y + ib.m[10] * t.z)};
            SDL_Log("ragdoll %s: tip (%.3f %.3f %.3f) len %.3f cone %.2f damp %.2f mass %.2f; body at (%.3f %.3f %.3f) bind at (%.3f %.3f %.3f)",
                    body.name.c_str(), tip.x, tip.y, tip.z, l.len, l.cone, l.damping, body.mass, body.transform[12], body.transform[13],
                    body.transform[14], bp.x, bp.y, bp.z);
        }
        linkOf_[size_t(b)] = int(links_.size());
        links_.push_back(l);
    }
    // Parents before children (the skeletons' order) [I: checked by the loop below].
    std::sort(links_.begin(), links_.end(), [](const Link& a, const Link& b) { return a.bone < b.bone; });
    for (size_t i = 0; i < links_.size(); ++i) linkOf_[size_t(links_[i].bone)] = int(i);
    started_ = false;
    history_.clear();
    return !links_.empty();
}

void AccessorySim::pose(std::vector<Mat34>& world, const std::vector<Vec>* x, std::vector<Vec>* restTips, std::vector<Vec>* pivots) const {
    std::vector<Mat34> delta(world.size());
    std::vector<char> moved(world.size(), 0);
    for (size_t b = 0; b < world.size(); ++b) {
        const int p = b < parent_.size() ? parent_[b] : -1;
        const bool inherit = p >= 0 && size_t(p) < b && moved[size_t(p)];
        Mat34 d = inherit ? delta[size_t(p)] : Mat34{};
        bool mv = inherit;
        Mat34 w = inherit ? bandanim::mmul(d, world[b]) : world[b];
        const int li = linkOf_[b];
        if (li >= 0) {
            const Link& l = links_[size_t(li)];
            const Vec j = posOf(w);
            const Vec rest = bandanim::xformDir(w, l.tip);
            if (restTips) (*restTips)[size_t(li)] = add(j, rest);
            if (pivots) (*pivots)[size_t(li)] = j;
            if (x) {
                const Quat q = between(rest, sub((*x)[size_t(li)], j));
                d = bandanim::mmul(about(q, j), d);
                w = bandanim::mmul(about(q, j), w);
                mv = true;
            }
        }
        if (mv) {
            delta[b] = d;
            moved[b] = 1;
            world[b] = w;
        }
    }
}

void AccessorySim::apply(double ms, std::vector<Mat34>& world, const std::function<void(double, std::vector<Mat34>&)>& animate,
                         const Vec& gravity) {
    if (links_.empty()) return;
    const int64_t step = int64_t(std::floor(ms / kStepMs));
    const size_t n = links_.size();
    std::vector<Mat34> w;
    auto restart = [&](int64_t s) {
        w.clear();
        animate(double(s) * kStepMs, w);
        cur_.step = s;
        cur_.x.assign(n, {});
        pose(w, nullptr, &cur_.x, nullptr);
        cur_.xp = cur_.x;
        history_.clear();
        history_.push_back(cur_);
        started_ = true;
    };
    // A first query, or one far from the last step: start at rest there.
    if (!started_ || step > cur_.step + 120 || step < cur_.step - int64_t(kHistory)) restart(step);
    const float dt = float(kStepMs / 1000.0);
    std::vector<Vec> rest(n), piv(n);
    while (cur_.step < step) {
        ++cur_.step;
        w.clear();
        animate(double(cur_.step) * kStepMs, w);
        // Link by link (parents first): the pivot from the simulated parent,
        // Verlet under gravity with damping, the rod, the cone.
        std::vector<Vec> next = cur_.x;
        for (size_t i = 0; i < n; ++i) {
            // The pose with the links before i at their new tips, the rest at the old.
            std::vector<Mat34> wi = w;
            pose(wi, &next, &rest, &piv);
            const Link& l = links_[i];
            const Vec j = piv[i];
            const Vec v = mul(sub(cur_.x[i], cur_.xp[i]), std::max(0.0f, 1.0f - l.damping * dt));
            Vec p = add(add(cur_.x[i], v), mul(gravity, dt * dt));
            Vec dir = norm(sub(p, j));
            const Vec rd = norm(sub(rest[i], j));
            if (len(dir) < 0.5f) dir = rd;
            const float c = std::clamp(dot(dir, rd), -1.0f, 1.0f);
            if (std::acos(c) > l.cone) {
                // Back onto the cone: rotate rd towards dir by the cone angle.
                Vec perp = norm(sub(dir, mul(rd, c)));
                if (len(perp) < 0.5f) perp = norm(cross(rd, {1, 0, 0}));
                dir = add(mul(rd, std::cos(l.cone)), mul(perp, std::sin(l.cone)));
            }
            next[i] = add(j, mul(dir, l.len));
        }
        cur_.xp = cur_.x;
        cur_.x = next;
        history_.push_back(cur_);
        while (history_.size() > kHistory) history_.pop_front();
    }
    const State* s = &cur_;
    if (step < cur_.step)
        for (const State& h : history_)
            if (h.step == step) s = &h;
    pose(world, &s->x, nullptr, nullptr);
}
