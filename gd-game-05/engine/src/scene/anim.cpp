// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/anim.h"

#include <cmath>

namespace scene {

Quat nlerp(const Quat& a, Quat b, float t) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0)
        for (float& v : b) v = -v;
    Quat q;
    float len = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] = a[i] + (b[i] - a[i]) * t;
        len += q[i] * q[i];
    }
    len = len > 0 ? 1.0f / std::sqrt(len) : 1;
    for (float& v : q) v *= len;
    return q;
}

Quat slerp(const Quat& a, Quat b, float t) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0) {
        for (float& v : b) v = -v;
        dot = -dot;
    }
    if (dot > 0.9995f) return nlerp(a, b, t);
    float th = std::acos(dot), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a[0] * wa + b[0] * wb, a[1] * wa + b[1] * wb, a[2] * wa + b[2] * wb, a[3] * wa + b[3] * wb};
}

bool sampleRotation(const std::vector<Key<4>>& keys, float frame, bool slerpKeys, Quat& out) {
    if (keys.empty()) return false;
    if (frame <= keys.front().frame) { out = keys.front().v; return true; }
    if (frame >= keys.back().frame) { out = keys.back().v; return true; }
    size_t i = 1;
    while (i < keys.size() && keys[i].frame < frame) ++i;
    const auto& a = keys[i - 1];
    const auto& b = keys[i];
    float span = b.frame - a.frame;
    float t = span > 0 ? (frame - a.frame) / span : 0;
    out = slerpKeys ? slerp(a.v, b.v, t) : nlerp(a.v, b.v, t);
    return true;
}

void quatToRot(const Quat& q, std::array<float, 9>& m) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m = {1 - 2 * y * y - 2 * z * z, 2 * x * y + 2 * z * w, 2 * x * z - 2 * y * w,
         2 * x * y - 2 * z * w, 1 - 2 * z * z - 2 * x * x, 2 * y * z + 2 * x * w,
         2 * x * z + 2 * y * w, 2 * y * z - 2 * x * w, 1 - 2 * x * x - 2 * y * y};
}

}  // namespace scene
