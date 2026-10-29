// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "render/rmath.h"

// A camera as GH2's Cam objects described it (kept from gd-game-02; GH3's camera conventions replace it in M5): a world transform (Rnd axes:
// the camera looks down its +y, with +z up and +x right), a vertical field
// of view, near and far planes. The frame is 16:9 (GH3 renders 1280x720).
namespace gfx {

constexpr float kLogicalW = 1280, kLogicalH = 720;

struct Camera {
    Mat4 world;
    float yFov = 0.8f;  // radians
    float nearPlane = 1, farPlane = 10000;
    float aspect = kLogicalW / kLogicalH;

    Vec3 position() const { return world.translationPart(); }
    Mat4 view() const { return world.inverseAffine(); }
    // Rnd camera space -> clip space. `zeroToOne`: the device's clip depth.
    Mat4 projection(bool zeroToOne) const {
        return Mat4::perspective(yFov, aspect, nearPlane, farPlane, zeroToOne) * Mat4::rndToEye();
    }
    Mat4 viewProj(bool zeroToOne) const { return projection(zeroToOne) * view(); }

    // A camera at `eye` looking at `target` with world +z up.
    static Camera lookAt(Vec3 eye, Vec3 target) {
        Camera c;
        const Vec3 fwd = normalize(target - eye);
        Vec3 right = cross(fwd, Vec3{0, 0, 1});
        if (dot(right, right) < 1e-8f) right = {1, 0, 0};
        right = normalize(right);
        const Vec3 up = cross(right, fwd);
        const float rows[9] = {right.x, right.y, right.z, fwd.x, fwd.y, fwd.z, up.x, up.y, up.z};
        const float pos[3] = {eye.x, eye.y, eye.z};
        c.world = Mat4::fromRnd(rows, pos);
        return c;
    }
};

}  // namespace gfx
