// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cmath>

// The renderer's small maths kit. Conventions, fixed here once:
// - Column vectors, column-major storage (OpenGL's): m[c * 4 + r], and a
//   point transforms as M * v. Uploaded to GLSL as-is.
// - GH2's world space (Harmonix "Rnd"): x right, y forward, z up. A Milo
//   Trans stores a 3x3 as three rows (the images of the x, y and z axes, for
//   row vectors: v * R) plus a position; Mat4::fromRnd turns that into the
//   same transform for column vectors (each stored row becomes a column).
namespace gfx {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 normalize(Vec3 a) {
    float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0f / l) : a;
}

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    static Mat4 identity() { return {}; }

    // A Milo transform: `rows` is the stored 3x3 (9 floats, row by row) and
    // `pos` the translation.
    static Mat4 fromRnd(const float rows[9], const float pos[3]) {
        Mat4 r;
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) r.at(k, c) = rows[c * 3 + k];
        r.at(0, 3) = pos[0], r.at(1, 3) = pos[1], r.at(2, 3) = pos[2];
        return r;
    }

    static Mat4 translation(float x, float y, float z) {
        Mat4 r;
        r.at(0, 3) = x, r.at(1, 3) = y, r.at(2, 3) = z;
        return r;
    }

    // Orthographic projection mapping x left..right and y top..bottom to
    // clip space (y down, as screen pixels), z ignored.
    static Mat4 ortho2D(float left, float right, float top, float bottom) {
        Mat4 r;
        r.at(0, 0) = 2 / (right - left);
        r.at(1, 1) = 2 / (top - bottom);
        r.at(2, 2) = -1;
        r.at(0, 3) = -(right + left) / (right - left);
        r.at(1, 3) = -(top + bottom) / (top - bottom);
        return r;
    }

    // Perspective projection for a camera looking down -z (OpenGL eye
    // space). `zeroToOne`: clip depth 0..1 (Vulkan, D3D) instead of -1..1.
    static Mat4 perspective(float yFov, float aspect, float zNear, float zFar, bool zeroToOne) {
        Mat4 r;
        const float f = 1.0f / std::tan(yFov * 0.5f);
        r.at(0, 0) = f / aspect;
        r.at(1, 1) = f;
        r.at(3, 2) = -1;
        r.at(3, 3) = 0;
        if (zeroToOne) {
            r.at(2, 2) = zFar / (zNear - zFar);
            r.at(2, 3) = zNear * zFar / (zNear - zFar);
        } else {
            r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
            r.at(2, 3) = 2 * zFar * zNear / (zNear - zFar);
        }
        return r;
    }

    // GH2 camera space (x right, y forward, z up) to OpenGL eye space
    // (x right, y up, looking down -z): (x, y, z) -> (x, z, -y).
    static Mat4 rndToEye() {
        Mat4 r;
        r.at(1, 1) = 0, r.at(1, 2) = 1;
        r.at(2, 1) = -1, r.at(2, 2) = 0;
        return r;
    }

    Vec3 transformPoint(Vec3 v) const {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3),
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3),
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3)};
    }
    Vec3 translationPart() const { return {at(0, 3), at(1, 3), at(2, 3)}; }

    // Inverse of a rigid or uniformly scaled affine transform (rotation,
    // scale, translation; no projection).
    Mat4 inverseAffine() const {
        Mat4 r;
        float a[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) a[i][j] = at(i, j);
        const float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
                          a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                          a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
        const float inv = det != 0 ? 1.0f / det : 0.0f;
        r.at(0, 0) = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * inv;
        r.at(0, 1) = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * inv;
        r.at(0, 2) = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * inv;
        r.at(1, 0) = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * inv;
        r.at(1, 1) = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * inv;
        r.at(1, 2) = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * inv;
        r.at(2, 0) = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * inv;
        r.at(2, 1) = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * inv;
        r.at(2, 2) = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * inv;
        const Vec3 t = translationPart();
        for (int i = 0; i < 3; ++i) r.at(i, 3) = -(r.at(i, 0) * t.x + r.at(i, 1) * t.y + r.at(i, 2) * t.z);
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < 4; ++k) {
            float s = 0;
            for (int i = 0; i < 4; ++i) s += a.at(k, i) * b.at(i, c);
            r.at(k, c) = s;
        }
    return r;
}

}  // namespace gfx
