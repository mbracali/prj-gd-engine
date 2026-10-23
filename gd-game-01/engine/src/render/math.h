// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cmath>

// Column-major 4x4 matrix, as OpenGL expects.
struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    friend Mat4 operator*(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += a.at(i, k) * b.at(k, j);
                r.at(i, j) = s;
            }
        return r;
    }

    static Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
        Mat4 r;
        float f = 1.0f / std::tan(fovY * 0.5f);
        r.m = {f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (zFar + zNear) / (zNear - zFar), -1,
               0, 0, 2 * zFar * zNear / (zNear - zFar), 0};
        return r;
    }

    // From the game's row-vector transform p' = p * rot + pos (rot rows = axes).
    static Mat4 fromRowXfm(const std::array<float, 9>& rot, const std::array<float, 3>& pos) {
        Mat4 r;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) r.at(col, row) = rot[row * 3 + col];
        r.at(0, 3) = pos[0], r.at(1, 3) = pos[1], r.at(2, 3) = pos[2];
        return r;
    }

    // View matrix looking from `eye` at `target` (Z up).
    static Mat4 lookAt(const std::array<float, 3>& eye, const std::array<float, 3>& target) {
        float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
        float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (float& v : f) v /= fl;
        float r[3] = {f[1], -f[0], 0};
        float rl = std::sqrt(r[0] * r[0] + r[1] * r[1]);
        r[0] /= rl, r[1] /= rl;
        float u[3] = {r[1] * f[2], -r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
        return cameraView({r[0], r[1], r[2], f[0], f[1], f[2], u[0], u[1], u[2]}, eye);
    }

    // View matrix for a game camera: its world rows are right, forward, up
    // (scenes are Z-up and cameras look along their +Y axis).
    static Mat4 cameraView(const std::array<float, 9>& rot, const std::array<float, 3>& pos) {
        const float* right = &rot[0];
        const float* fwd = &rot[3];
        const float* up = &rot[6];
        Mat4 r;
        auto row = [&](int i, const float* axis, float sign) {
            r.at(i, 0) = sign * axis[0], r.at(i, 1) = sign * axis[1], r.at(i, 2) = sign * axis[2];
            r.at(i, 3) = -sign * (axis[0] * pos[0] + axis[1] * pos[1] + axis[2] * pos[2]);
        };
        row(0, right, 1);
        row(1, up, 1);
        row(2, fwd, -1);
        return r;
    }
};
