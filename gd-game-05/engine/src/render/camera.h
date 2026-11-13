// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "render/rmath.h"

// A camera as GH2's Cam objects describe it: a world transform (Rnd axes:
// the camera looks down its +y, with +z up and +x right), a vertical field
// of view, near and far planes. The frame is 16:9 (GH2 renders 1280x720).
namespace gfx {

constexpr float kLogicalW = 1280, kLogicalH = 720;

struct Camera {
    Mat4 world;
    float yFov = 0.8f;  // radians
    float nearPlane = 1, farPlane = 10000;
    float aspect = kLogicalW / kLogicalH;
    // TAA's sub-pixel offset of the picture (NDC), 0 otherwise (render/enhanced_fx.h).
    float jitterX = 0, jitterY = 0;
    // RndCam with y_fov 0 (UpdateLocal RB 0x8218CAE0) [C]: a parallel
    // projection, camera x -1..1 across the picture, z -1..1 scaled by the
    // picture's height / width (aspect) up it; near / far as usual.
    bool ortho = false;

    Vec3 position() const { return world.translationPart(); }
    Mat4 view() const { return world.inverseAffine(); }
    // Rnd camera space -> clip space. `zeroToOne`: the device's clip depth.
    Mat4 projection(bool zeroToOne) const {
        if (ortho) {
            Mat4 o;
            o.at(1, 1) = aspect;
            const float d = farPlane - nearPlane;
            o.at(2, 2) = (zeroToOne ? -1.0f : -2.0f) / d;
            o.at(2, 3) = zeroToOne ? -nearPlane / d : -(farPlane + nearPlane) / d;
            return o * Mat4::rndToEye();
        }
        const Mat4 p = Mat4::perspective(yFov, aspect, nearPlane, farPlane, zeroToOne) * Mat4::rndToEye();
        return jitterX == 0 && jitterY == 0 ? p : Mat4::translation(jitterX, jitterY, 0) * p;
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
