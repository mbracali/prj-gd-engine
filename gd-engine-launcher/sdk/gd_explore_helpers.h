// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Helpers for a module's model explorer (gd_game_explore, gd_host.h),
// header only, plain arrays: nothing here knows a game's formats.
#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace gdexplore {

using Vec = std::array<float, 3>;

// The rotation that stands a guitar up for the viewer: models lie as a hand
// holds them, so from the points' principal axes the longest (the neck)
// turns to +z, the headstock up, and the thinnest (the face normal) to -y,
// the front. Which end is the headstock and which side the front is cannot
// be told from the points: `headHint` and `frontHint` are directions the
// game's guitars have in common (they hang from the same hand bone), and
// each axis takes the sign that agrees with its hint. Z-up worlds; a camera
// at -y looks at the front. Returns the rotation as three rows, the images
// of the x, y and z axes (row vectors: p' = p * rows); identity when the
// points are too few.
inline std::array<float, 9> uprightRows(const std::vector<Vec>& pts, const Vec& headHint, const Vec& frontHint) {
    std::array<float, 9> id{1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (pts.size() < 8) return id;
    double mean[3] = {0, 0, 0};
    for (const Vec& p : pts)
        for (int i = 0; i < 3; ++i) mean[i] += p[size_t(i)];
    for (double& m : mean) m /= double(pts.size());
    double c[3][3] = {};
    for (const Vec& p : pts)
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) c[i][j] += (p[size_t(i)] - mean[i]) * (p[size_t(j)] - mean[j]);
    // Jacobi rotations: v's columns become the eigenvectors.
    double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int sweep = 0; sweep < 30; ++sweep) {
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::fabs(c[p][q]) < 1e-12) continue;
                const double theta = (c[q][q] - c[p][p]) / (2 * c[p][q]);
                const double t = (theta >= 0 ? 1 : -1) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
                const double cs = 1 / std::sqrt(t * t + 1), sn = t * cs;
                for (int k = 0; k < 3; ++k) {  // c = J^T c J
                    const double a = c[k][p], b = c[k][q];
                    c[k][p] = cs * a - sn * b, c[k][q] = sn * a + cs * b;
                }
                for (int k = 0; k < 3; ++k) {
                    const double a = c[p][k], b = c[q][k];
                    c[p][k] = cs * a - sn * b, c[q][k] = sn * a + cs * b;
                }
                for (int k = 0; k < 3; ++k) {
                    const double a = v[k][p], b = v[k][q];
                    v[k][p] = cs * a - sn * b, v[k][q] = sn * a + cs * b;
                }
            }
    }
    int big = 0, small = 0;
    for (int i = 1; i < 3; ++i) {
        if (c[i][i] > c[big][big]) big = i;
        if (c[i][i] < c[small][small]) small = i;
    }
    if (big == small) return id;
    double neck[3] = {v[0][big], v[1][big], v[2][big]}, face[3] = {v[0][small], v[1][small], v[2][small]};
    if (neck[0] * headHint[0] + neck[1] * headHint[1] + neck[2] * headHint[2] < 0)
        for (double& x : neck) x = -x;
    if (face[0] * frontHint[0] + face[1] * frontHint[1] + face[2] * frontHint[2] < 0)
        for (double& x : face) x = -x;
    const double side[3] = {neck[1] * face[2] - neck[2] * face[1], neck[2] * face[0] - neck[0] * face[2],
                            neck[0] * face[1] - neck[1] * face[0]};
    // R(neck) = +z, R(face) = -y, R(neck x face) = +z x -y = +x.
    std::array<float, 9> rows{};
    for (int axis = 0; axis < 3; ++axis)  // the image of unit axis `axis`
        for (int k = 0; k < 3; ++k) {
            const double tz = neck[axis], ty = -face[axis], tx = side[axis];
            const double out[3] = {tx, ty, tz};
            rows[size_t(axis * 3 + k)] = float(out[k]);
        }
    return rows;
}

}  // namespace gdexplore
