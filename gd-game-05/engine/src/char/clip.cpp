// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/clip.h"

#include <algorithm>
#include <cmath>

namespace chr {

namespace {

using Set = scene::CharBonesSamples;

int floatsOf(int type) { return type <= Set::kScale ? 3 : type == Set::kQuat ? 4 : 1; }

// 0x821A1938: the sample index and fraction for t in 0..1.
uint32_t sampleIndex(const Set& s, float t, float& frac) {
    frac = 0;
    if (s.count < 2) return 0;
    t = std::clamp(t, 0.0f, 1.0f);
    const float x = float(s.count - 1) * t;
    const uint32_t i = uint32_t(x);
    if (i >= s.count - 1) return s.count - 1;
    frac = x - float(i);
    return i;
}

// 0x82161C70: an angle wrapped into (-pi, pi] [I: the wrap range].
float wrapAngle(float a) {
    constexpr float kPi = 3.14159265f;
    while (a > kPi) a -= 2 * kPi;
    while (a <= -kPi) a += 2 * kPi;
    return a;
}

}  // namespace

float clipT(const scene::CharClipSamples& clip, float frame) {
    float len = clip.endFrame - clip.startFrame;
    if (len == 0) len = 1;
    return (frame - clip.startFrame) / len;
}

void sampleSet(const Set& s, float t, std::vector<float>& out) {
    out.assign(size_t(s.width), 0.0f);
    if (s.count == 0 || s.width == 0) return;
    float f;
    const uint32_t i = sampleIndex(s, t, f);
    const float* a = s.sample(i);
    const float* b = f > 0 ? s.sample(i + 1) : a;
    for (int k = 0; k < s.width; ++k) out[size_t(k)] = a[k] + (b[k] - a[k]) * f;
}

int Bones::add(const std::string& name) {
    auto it = index_.find(name);
    if (it != index_.end()) return it->second;
    Channel c;
    c.name = name;
    c.type = Set::typeOf(name);
    if (c.type == Set::kQuat) c.v[3] = 1;
    ch_.push_back(c);
    const int i = int(ch_.size()) - 1;
    index_.emplace(name, i);
    maps_.clear();
    facingDeltaPos_ = facingDeltaRot_ = -2;
    return i;
}

int Bones::find(const std::string& name) const {
    auto it = index_.find(name);
    return it == index_.end() ? -1 : it->second;
}

void Bones::addChannelsOf(const Set& set) {
    for (const auto& b : set.bones) add(b.name);
}

const std::vector<int>& Bones::map(const Set& set) {
    auto it = maps_.find(&set);
    if (it != maps_.end()) return it->second;
    std::vector<int> v;
    v.reserve(set.bones.size());
    for (const auto& b : set.bones) v.push_back(find(b.name));
    return maps_.emplace(&set, std::move(v)).first->second;
}

// 0x821A1CC8 -> 0x8215DF28: adds sample `sample` of `set` times `w`.
void Bones::addSample(const Set& set, const std::vector<int>& idx, uint32_t sample, float w) {
    if (set.count == 0) return;
    const float* s = set.sample(std::min(sample, set.count - 1));
    for (size_t b = 0; b < set.bones.size(); ++b) {
        const int c = idx[b];
        if (c < 0) continue;
        Channel& ch = ch_[size_t(c)];
        const float* x = s + set.offset[b];
        switch (set.bones[b].type) {
        case Set::kPos:
        case Set::kScale:
            for (int k = 0; k < 3; ++k) ch.v[k] += w * x[k];
            break;
        case Set::kQuat: {
            // |w| on x y z, w on w; added or subtracted by the sign of the
            // dot product with the accumulator.
            const float aw = std::fabs(w);
            const float q[4] = {x[0] * aw, x[1] * aw, x[2] * aw, x[3] * w};
            const float dot = ch.v[0] * q[0] + ch.v[1] * q[1] + ch.v[2] * q[2] + ch.v[3] * q[3];
            const float sgn = dot >= 0 ? 1.0f : -1.0f;
            for (int k = 0; k < 4; ++k) ch.v[k] += sgn * q[k];
            break;
        }
        default:
            ch.v[0] += w * x[0];
            break;
        }
        ch.weight += set.bones[b].weight * w;
    }
}

void Bones::scaleAdd(const scene::CharClipSamples& clip, float w, float frame, float dframe) {
    // The static set: its one sample.
    if (!clip.one.bones.empty()) addSample(clip.one, map(clip.one), 0, w);
    float len = clip.endFrame - clip.startFrame;
    if (len == 0) len = 1;
    const float t = (frame - clip.startFrame) / len;
    // The full set at t: floor and ceil samples.
    if (!clip.full.bones.empty() && clip.full.count > 0) {
        float f;
        const uint32_t i = sampleIndex(clip.full, t, f);
        const auto& idx = map(clip.full);
        addSample(clip.full, idx, i, (1 - f) * w);
        if (f > 0) addSample(clip.full, idx, i + 1, f * w);
    }
    // The delta set (no channels on the disc) is skipped.
    // The root delta: bone_facing between t0 and t (0x82162378).
    if (facingDeltaPos_ == -2) {
        facingDeltaPos_ = find("bone_facing_delta.pos");
        facingDeltaRot_ = find("bone_facing_delta.rotz");
    }
    if (facingDeltaPos_ < 0 && facingDeltaRot_ < 0) return;
    const Set& s = clip.full;
    int pos = -1, rot = -1;
    for (size_t b = 0; b < s.bones.size(); ++b) {
        if (s.bones[b].name == "bone_facing.pos") pos = int(b);
        if (s.bones[b].name == "bone_facing.rotz") rot = int(b);
    }
    if (pos < 0 || s.count == 0) return;
    const float t1 = std::clamp(t, 0.0f, 1.0f);
    const float t0 = std::clamp(t - dframe / len, 0.0f, 1.0f);
    std::vector<float> a, b;
    sampleSet(s, t1, a);
    sampleSet(s, t0, b);
    float d[3] = {a[size_t(s.offset[size_t(pos)])] - b[size_t(s.offset[size_t(pos)])],
                  a[size_t(s.offset[size_t(pos)]) + 1] - b[size_t(s.offset[size_t(pos)]) + 1],
                  a[size_t(s.offset[size_t(pos)]) + 2] - b[size_t(s.offset[size_t(pos)]) + 2]};
    float drot = 0;
    if (rot >= 0) {
        const float r1 = a[size_t(s.offset[size_t(rot)])], r0 = b[size_t(s.offset[size_t(rot)])];
        drot = wrapAngle(r1 - r0);
        // 0x82161CD0: the position delta turned by -rot(t0) about z.
        const float c = std::cos(-r0), sn = std::sin(-r0);
        const float x = d[0] * c - d[1] * sn, y = d[0] * sn + d[1] * c;
        d[0] = x, d[1] = y;
    }
    if (facingDeltaPos_ >= 0) {
        Channel& ch = ch_[size_t(facingDeltaPos_)];
        for (int k = 0; k < 3; ++k) ch.v[k] += w * d[k];
        ch.weight += w;
    }
    if (facingDeltaRot_ >= 0 && rot >= 0) {
        Channel& ch = ch_[size_t(facingDeltaRot_)];
        ch.v[0] += w * drot;
        ch.weight += w;
    }
}

void Bones::scaleDown(const scene::CharClipSamples& clip, float f) {
    for (const Set* set : {&clip.one, &clip.full}) {
        const auto& idx = map(*set);
        for (size_t b = 0; b < set->bones.size(); ++b) {
            if (idx[b] < 0) continue;
            Channel& ch = ch_[size_t(idx[b])];
            for (int k = 0; k < floatsOf(ch.type); ++k) ch.v[k] *= f;
            ch.weight *= f;
        }
    }
}

void Bones::rotateBy(const scene::CharClipSamples& clip, float frame) {
    // 0x82161F98: t clamped to [0, 1] for the full set, 0 for the static one.
    const float t = std::clamp(clipT(clip, frame), 0.0f, 1.0f);
    std::vector<float> v;
    for (const Set* set : {&clip.full, &clip.one}) {
        if (set->bones.empty() || set->count == 0) continue;
        sampleSet(*set, set == &clip.full ? t : 0.0f, v);
        const auto& idx = map(*set);
        for (size_t b = 0; b < set->bones.size(); ++b) {
            if (idx[b] < 0) continue;
            Channel& ch = ch_[size_t(idx[b])];
            const float* x = v.data() + set->offset[b];
            switch (set->bones[b].type) {
            case Set::kPos:
            case Set::kScale:  // the entries before the quaternions (none scale on the disc)
                for (int k = 0; k < 3; ++k) ch.v[k] += x[k];
                break;
            case Set::kQuat: {
                // n * q (Hamilton, x y z w).
                const float nx = x[0], ny = x[1], nz = x[2], nw = x[3];
                const float qx = ch.v[0], qy = ch.v[1], qz = ch.v[2], qw = ch.v[3];
                ch.v[0] = nw * qx + nx * qw + ny * qz - nz * qy;
                ch.v[1] = nw * qy - nx * qz + ny * qw + nz * qx;
                ch.v[2] = nw * qz + nx * qy - ny * qx + nz * qw;
                ch.v[3] = nw * qw - nx * qx - ny * qy - nz * qz;
                break;
            }
            default:
                ch.v[0] += x[0];
                break;
            }
        }
    }
}

}  // namespace chr
