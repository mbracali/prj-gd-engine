// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

// Animation keys as GH2's anim classes store them: a value then its frame
// (Key<Vector3> @ 0x821b4558, Key<Quat> @ 0x821b4510, Key<Color> @ 0x821fea68,
// Key<float> @ 0x821feab0, Key<ObjPtr<Tex>> @ 0x82202358).

namespace scene {

template <size_t N> struct Key {
    std::array<float, N> v{};
    float frame = 0;
};

struct TexKey {
    std::string tex;
    float frame = 0;
};

using Quat = std::array<float, 4>;  // x y z w

// Linear interpolation between the keys around `frame`; clamps outside the
// range. Returns false (out untouched) when there are no keys.
template <size_t N> bool sampleLinear(const std::vector<Key<N>>& keys, float frame, std::array<float, N>& out) {
    if (keys.empty()) return false;
    if (frame <= keys.front().frame) { out = keys.front().v; return true; }
    if (frame >= keys.back().frame) { out = keys.back().v; return true; }
    size_t i = 1;
    while (i < keys.size() && keys[i].frame < frame) ++i;
    const auto& a = keys[i - 1];
    const auto& b = keys[i];
    float span = b.frame - a.frame;
    float t = span > 0 ? (frame - a.frame) / span : 0;
    for (size_t k = 0; k < N; ++k) out[k] = a.v[k] + (b.v[k] - a.v[k]) * t;
    return true;
}

// Position keys as RndTransAnim reads them (0x8226E560) [C]: linear, or
// with trans_spline a cubic Hermite between the two keys around `frame`
// (t their fraction) whose tangents (0x8226E368) are p1 - p0 for two keys,
// (p[i+1] - p[i-1]) / 2 inside, and 1.5 (p1 - p0) - 0.25 (p2 - p0) at the
// first key (the mirror at the last). Clamps outside the keys' range.
template <size_t N> bool sampleTrans(const std::vector<Key<N>>& keys, float frame, bool spline, std::array<float, N>& out) {
    if (!spline || keys.size() < 2 || frame <= keys.front().frame || frame >= keys.back().frame) return sampleLinear(keys, frame, out);
    size_t i = 1;
    while (i < keys.size() && keys[i].frame < frame) ++i;
    const auto& a = keys[i - 1];
    const auto& b = keys[i];
    const float span = b.frame - a.frame;
    const float t = span > 0 ? (frame - a.frame) / span : 0;
    const size_t n = keys.size();
    auto tangent = [&](size_t j, size_t c) {
        if (n == 2) return keys[1].v[c] - keys[0].v[c];
        if (j == 0) return 1.5f * (keys[1].v[c] - keys[0].v[c]) - 0.25f * (keys[2].v[c] - keys[0].v[c]);
        if (j == n - 1) return 1.5f * (keys[n - 1].v[c] - keys[n - 2].v[c]) - 0.25f * (keys[n - 1].v[c] - keys[n - 3].v[c]);
        return 0.5f * (keys[j + 1].v[c] - keys[j - 1].v[c]);
    };
    const float t2 = t * t, t3 = t2 * t;
    const float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = 3 * t2 - 2 * t3, h11 = t3 - t2;
    for (size_t c = 0; c < N; ++c) out[c] = a.v[c] * h00 + tangent(i - 1, c) * h10 + b.v[c] * h01 + tangent(i, c) * h11;
    return true;
}

// The key in effect at `frame` (the last one at or before it): for texture
// keys and stepped channels.
template <class K> const K* sampleStep(const std::vector<K>& keys, float frame) {
    if (keys.empty()) return nullptr;
    const K* k = &keys.front();
    for (const K& x : keys) {
        if (x.frame > frame) break;
        k = &x;
    }
    return k;
}

// Normalised lerp / spherical lerp along the shorter arc.
Quat nlerp(const Quat& a, Quat b, float t);
Quat slerp(const Quat& a, Quat b, float t);
bool sampleRotation(const std::vector<Key<4>>& keys, float frame, bool slerpKeys, Quat& out);

// Quaternion -> row-major rotation (rows are the rotated axes, for p * rot).
void quatToRot(const Quat& q, std::array<float, 9>& m);

// The last key's frame of a set of key lists (0 when all are empty).
template <class... L> float lastFrame(const L&... lists) {
    float f = 0;
    ((f = lists.empty() ? f : (lists.back().frame > f ? lists.back().frame : f)), ...);
    return f;
}

}  // namespace scene
