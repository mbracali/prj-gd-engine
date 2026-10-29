// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/venuetest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "game/camera_director.h"
#include "game/venue.h"
#include "game/world.h"
#include "render/scene_gpu.h"
#include "render/venue_view.h"
#include "scene/world_objects.h"

// --venuetest's camera checks (docs/venues.md "Camera director"): every
// CamShot of the variant, framed as the director frames it
// (CameraDirector::evaluate: GH2's look-at, screen offset, path, parent;
// no filter or shake) every 5 frames of its length and at its end, with the
// band slots at their start waypoints:
// - the camera is a finite, orthonormal transform that is not upside down
//   (its up axis not below -0.5: GH2's look-at keeps the keyframe's up, so
//   only a camera looking straight down has a level up axis);
// - it is not under the venue: not more than kUnder below the band's floor
//   (the lowest waypoint) and below every floor over its spot (up-facing
//   triangles of the static meshes, found through a kCell grid) - the
//   owner's "the camera went under the venue" (arena's intro before the
//   fix: its path under the first keyframe's xfm);
// - when both keyframes of the moment aim at the same targets, the targets'
//   mean lands at the lerped screen offset (0x82227440's move of the
//   camera) within kOffsetTolerance.

namespace {

constexpr float kCell = 25;
constexpr float kUnder = 10;
constexpr float kOffsetTolerance = 0.02f;

using V3 = std::array<float, 3>;

struct Floor {
    struct Tri {
        V3 a, b, c;
    };
    std::vector<Tri> tris;
    std::map<std::pair<int, int>, std::vector<uint32_t>> cells;  // the triangles whose bounds touch a cell
    std::vector<uint32_t> big;                                   // ground planes and domes: tested everywhere
    static int cell(float v) { return int(std::floor(v / kCell)); }
    // Floors only: triangles whose visible side (counter-clockwise about the
    // normal, docs/highway_and_hud.md) faces up.
    void add(const V3& a, const V3& b, const V3& c) {
        const V3 e1{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const V3 nrm{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const float nl = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        if (nl <= 0 || nrm[2] < 0.5f * nl) return;
        const uint32_t i = uint32_t(tris.size());
        tris.push_back({a, b, c});
        const int x0 = cell(std::min({a[0], b[0], c[0]})), x1 = cell(std::max({a[0], b[0], c[0]}));
        const int y0 = cell(std::min({a[1], b[1], c[1]})), y1 = cell(std::max({a[1], b[1], c[1]}));
        if (int64_t(x1 - x0 + 1) * (y1 - y0 + 1) > 4096) {
            big.push_back(i);
            return;
        }
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y) cells[{x, y}].push_back(i);
    }
    // The lowest floor surface straight above or below p (false: none).
    bool lowestAt(const V3& p, float& z) const {
        bool found = false;
        auto test = [&](uint32_t i) {
            const Tri& t = tris[i];
            const float det = (t.b[1] - t.c[1]) * (t.a[0] - t.c[0]) + (t.c[0] - t.b[0]) * (t.a[1] - t.c[1]);
            if (std::fabs(det) < 1e-6f) return;
            const float l0 = ((t.b[1] - t.c[1]) * (p[0] - t.c[0]) + (t.c[0] - t.b[0]) * (p[1] - t.c[1])) / det;
            const float l1 = ((t.c[1] - t.a[1]) * (p[0] - t.c[0]) + (t.a[0] - t.c[0]) * (p[1] - t.c[1])) / det;
            const float l2 = 1 - l0 - l1;
            if (l0 < 0 || l1 < 0 || l2 < 0) return;
            const float h = l0 * t.a[2] + l1 * t.b[2] + l2 * t.c[2];
            z = found ? std::min(z, h) : h;
            found = true;
        };
        if (auto it = cells.find({cell(p[0]), cell(p[1])}); it != cells.end())
            for (uint32_t i : it->second) test(i);
        for (uint32_t i : big) test(i);
        return found;
    }
    // Under the venue: below every floor over its spot, by more than kUnder.
    bool below(const V3& p, float& floorZ) const { return lowestAt(p, floorZ) && p[2] < floorZ - kUnder; }
};

Floor floorOf(const World& w) {
    Floor f;
    for (const scene::Mesh* m : w.venue().all<scene::Mesh>()) {
        const scene::Mesh* g = m;
        if (m->verts.empty() && !m->geomOwner.empty() && m->dir)
            if (auto* o = m->dir->resolve<scene::Mesh>(m->geomOwner)) g = o;
        if (!g->bones.empty() || g->faces.empty()) continue;
        const scene::Xfm x = w.pose().world(m);
        auto at = [&](uint16_t i) {
            const float* p = g->verts[i].pos;
            V3 r = x.pos;
            for (int c = 0; c < 3; ++c) r[size_t(c)] += p[0] * x.rot[size_t(c)] + p[1] * x.rot[size_t(3 + c)] + p[2] * x.rot[size_t(6 + c)];
            return r;
        };
        for (const auto& face : g->faces)
            if (face[0] < g->verts.size() && face[1] < g->verts.size() && face[2] < g->verts.size()) f.add(at(face[0]), at(face[1]), at(face[2]));
    }
    return f;
}

// The column-major camera matrix's axes (gfx::toMat4 of the rows right, forward, up).
V3 axis(const gfx::Mat4& m, int i) { return {m.m[i * 4 + 0], m.m[i * 4 + 1], m.m[i * 4 + 2]}; }
float dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

}  // namespace

int cameraRun(Venue& v, std::string& report) {
    World w(v);
    w.setClock(0, 0);
    w.update();
    const Floor floor = floorOf(w);
    // The band's floor: the lowest start waypoint (the stage).
    float bandFloor = 1e30f;
    for (const scene::Waypoint* wp : v.all<scene::Waypoint>()) bandFloor = std::min(bandFloor, w.pose().world(wp).pos[2]);
    int shots = 0, samples = 0;
    std::map<std::string, int> failed;  // shot -> failing samples
    for (const auto& o : v.world().root().objects) {
        const auto* shot = o->as<scene::CamShot>();
        if (!shot || !shot->decoded || shot->keyframes.empty()) continue;
        ++shots;
        const float length = shot->lengthFrames();
        for (float f = 0;; f += 5) {
            const float frame = std::min(f, length);
            ++samples;
            const gfx::Camera cam = CameraDirector::evaluate(w, *shot, frame);
            const V3 r = axis(cam.world, 0), fw = axis(cam.world, 1), up = axis(cam.world, 2), pos = axis(cam.world, 3);
            std::string why;
            bool finite = true;
            for (const V3* a : {&r, &fw, &up, &pos})
                for (float c : *a) finite = finite && std::isfinite(c);
            char buf[200];
            if (!finite || std::fabs(dot(r, r) - 1) > 2e-2f || std::fabs(dot(fw, fw) - 1) > 2e-2f || std::fabs(dot(r, fw)) > 2e-2f) {
                std::snprintf(buf, sizeof buf, "not an orthonormal camera (right %.2f %.2f %.2f, forward %.2f %.2f %.2f, at %.0f %.0f %.0f)", double(r[0]),
                              double(r[1]), double(r[2]), double(fw[0]), double(fw[1]), double(fw[2]), double(pos[0]), double(pos[1]), double(pos[2]));
                why = buf;
            } else if (up[2] < -0.5f) {
                std::snprintf(buf, sizeof buf, "upside down (up %.2f %.2f %.2f)", double(up[0]), double(up[1]), double(up[2]));
                why = buf;
            } else if (float fz = 0; pos[2] < bandFloor - kUnder && floor.below(pos, fz)) {
                std::snprintf(buf, sizeof buf, "under the venue (z %.0f, geometry from %.0f)", double(pos[2]), double(fz));
                why = buf;
            }
            if (why.empty()) {
                // The screen offset with the same targets in both keyframes.
                float s = 0;
                size_t a = 0, b = 0;
                float t = 0;
                const size_t n = shot->keyframes.size();
                for (size_t i = 0; i < n; ++i) {
                    const auto& k = shot->keyframes[i];
                    a = b = i, t = 0;
                    if (frame <= s + k.duration || i + 1 == n) break;
                    if (frame < s + k.duration + k.blend) {
                        b = i + 1, t = k.blend > 0 ? (frame - s - k.duration) / k.blend : 1.0f;
                        break;
                    }
                    s += k.duration + k.blend;
                }
                const auto& ka = shot->keyframes[a];
                const auto& kb = shot->keyframes[b];
                bool same = ka.targets.size() == kb.targets.size() && !ka.targets.empty();
                for (size_t i = 0; same && i < ka.targets.size(); ++i)
                    same = ka.targets[i].object == kb.targets[i].object && ka.targets[i].subpart == kb.targets[i].subpart;
                if (same && !shot->looping && (a == b || ka.blendEase == 0)) {
                    V3 sum{0, 0, 0};
                    int m = 0;
                    const bool mp = v.variant() != VenueVariant::Single;
                    for (const auto& tg : ka.targets) {
                        scene::Xfm tx;
                        if (!venueObjectXfm(w, tg.object, mp, tx)) continue;
                        for (int c = 0; c < 3; ++c) sum[size_t(c)] += tx.pos[size_t(c)];
                        ++m;
                    }
                    // A band slot's bone is its height above the slot (the director's
                    // stand-in until the band runs): only plain targets are compared.
                    bool bones = false;
                    for (const auto& tg : ka.targets) bones = bones || !tg.subpart.empty() || !v.find(tg.object);
                    if (m > 0 && !bones) {
                        const V3 tp{sum[0] / float(m), sum[1] / float(m), sum[2] / float(m)};
                        const V3 d{tp[0] - pos[0], tp[1] - pos[1], tp[2] - pos[2]};
                        const float y = dot(d, fw);
                        const float th = std::tan((ka.fieldOfView + (kb.fieldOfView - ka.fieldOfView) * t) * 0.5f);
                        const float sx = dot(d, r) / y / (th * float(gfx::kLogicalW) / float(gfx::kLogicalH)), sy = dot(d, up) / y / th;
                        const float ex = sx - (ka.screenOffset[0] + (kb.screenOffset[0] - ka.screenOffset[0]) * t);
                        const float ey = sy - (ka.screenOffset[1] + (kb.screenOffset[1] - ka.screenOffset[1]) * t);
                        if (y > 0 && std::sqrt(ex * ex + ey * ey) > kOffsetTolerance) {
                            std::snprintf(buf, sizeof buf, "target at %.2f %.2f, not at its screen offset", double(sx), double(sy));
                            why = buf;
                        }
                    }
                }
            }
            if (!why.empty()) {
                if (!failed[shot->name]++) {
                    std::snprintf(buf, sizeof buf, "\n    FAIL %s (%s) frame %.0f: ", shot->name.c_str(), shot->category.c_str(), double(frame));
                    report += buf + why;
                }
            }
            if (frame >= length) break;
        }
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "%d shots, %d camera samples over %zu floor triangles, %zu shots failing", shots, samples, floor.tris.size(),
                  failed.size());
    report = buf + report;
    return int(failed.size());
}
