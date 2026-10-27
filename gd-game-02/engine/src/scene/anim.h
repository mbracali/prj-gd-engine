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
