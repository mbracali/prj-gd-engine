// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/deform.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "char/character.h"
#include "char/clip.h"
#include "char/char_objects.h"

namespace chr {

namespace {

struct Pt {
    float x, y;
};
constexpr Pt kPoints[5] = {{0.5f, 0.5f}, {1, 1}, {1, 0}, {0, 1}, {0, 0}};
constexpr int kTris[4][3] = {{0, 2, 1}, {0, 1, 3}, {0, 3, 4}, {0, 4, 2}};

}  // namespace

float deformFrameWeight(float weight, float height, int frame) {
    const Pt p{weight, height};
    for (const auto& t : kTris) {
        // Inside: every edge's cross product of one sign (<= 0 counts as -1).
        int sign = 0;
        bool inside = true;
        for (int e = 0; e < 3 && inside; ++e) {
            const Pt a = kPoints[t[e]], b = kPoints[t[(e + 1) % 3]];
            const float cross = (b.y - a.y) * (p.x - a.x) - (p.y - a.y) * (b.x - a.x);
            const int s = cross <= 0 ? -1 : 1;
            if (e > 0 && s != sign) inside = false;
            sign = s;
        }
        if (!inside) continue;
        // Barycentric weights: the right angle at vertex 0.
        float w[3];
        for (int k = 1; k < 3; ++k) {
            const Pt v = kPoints[t[k]], o = kPoints[t[0]];
            const float ex = o.x - v.x, ey = o.y - v.y;
            const float tk = ((p.x - v.x) * ex + (p.y - v.y) * ey) / (ex * ex + ey * ey);
            w[k] = 1 - std::fabs(tk);
        }
        w[0] = 1 - w[1] - w[2];
        for (int k = 0; k < 3; ++k)
            if (t[k] == frame) return w[k];
        return 0;
    }
    return -1;
}

const scene::CharClipSamples* deformClip(Character& c) {
    const scene::CharClipSamples* clip = nullptr;
    c.forEachObject([&](scene::Object& o) {
        if (clip || o.name != "deform" || !o.dir) return;
        if (const auto* s = o.as<scene::CharClipSamples>()) clip = s;
    });
    return clip;
}

int applyDeform(Character& c, float weight, float height) {
    const scene::CharClipSamples* clip = deformClip(c);
    if (!clip) return 0;
    weight = std::clamp(weight, 0.0f, 1.0f), height = std::clamp(height, 0.0f, 1.0f);
    Bones b;
    b.addChannelsOf(clip->full);
    b.addChannelsOf(clip->one);
    for (int i = 0; i < 5; ++i)
        if (const float w = deformFrameWeight(weight, height, i); w > 0) b.scaleAdd(*clip, w, float(i), 0);
    Skeleton& sk = c.skeleton();
    auto boneOf = [&](const std::string& ch) {
        const std::string base = ch.substr(0, ch.rfind('.'));
        const int i = sk.find(base + ".trans");
        return i >= 0 ? i : sk.find(base + ".mesh");
    };
    int n = 0;
    // PoseMeshes (RB 0x823630F0) [C]: positions, rotations, then scales.
    for (const Bones::Channel& ch : b.channels()) {
        const int i = boneOf(ch.name);
        if (i < 0) continue;
        scene::Xfm x = sk.local(i);
        if (ch.type == scene::CharBonesSamples::kPos) {
            x.pos = {ch.v[0], ch.v[1], ch.v[2]};
        } else if (ch.type == scene::CharBonesSamples::kQuat) {
            scene::Quat q{ch.v[0], ch.v[1], ch.v[2], ch.v[3]};
            const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (l <= 1e-12f) continue;
            for (float& v : q) v /= l;
            scene::quatToRot(q, x.rot);
        } else {
            continue;
        }
        sk.setLocal(i, x), ++n;
    }
    for (const Bones::Channel& ch : b.channels()) {
        const int i = boneOf(ch.name);
        if (i < 0 || ch.type != scene::CharBonesSamples::kScale) continue;
        scene::Xfm x = sk.local(i);
        for (int r = 0; r < 3; ++r) {
            float* row = &x.rot[size_t(r * 3)];
            const float l = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
            if (l > 1e-12f)
                for (int k = 0; k < 3; ++k) row[k] *= ch.v[r] / l;
        }
        sk.setLocal(i, x), ++n;
    }
    // The deformed pose is the bones' rest from now on (the IK measures
    // its lengths from it: CharIKHand measure_lengths RB 0x8232E150).
    for (const Bones::Channel& ch : b.channels())
        if (const int i = boneOf(ch.name); i >= 0) sk.setBind(i, sk.local(i));
    return n;
}

}  // namespace chr
