// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/crowd_view.h"

#include <algorithm>
#include <cmath>

#include "game/crowd.h"
#include "game/world.h"

using gfx::Mat4;
using gfx::Vec3;

CrowdView::CrowdView(World& world, gfx::SceneCache& cache) : world_(world), cache_(cache) {}

CrowdView::~CrowdView() {
    for (gfx::BufferId b : buffers_) cache_.device().destroyBuffer(b);
}

namespace {

Vec3 mul3(const Mat4& m, Vec3 v) {
    return {m.at(0, 0) * v.x + m.at(0, 1) * v.y + m.at(0, 2) * v.z, m.at(1, 0) * v.x + m.at(1, 1) * v.y + m.at(1, 2) * v.z,
            m.at(2, 0) * v.x + m.at(2, 1) * v.y + m.at(2, 2) * v.z};
}

Mat4 blend(const Mat4* m, const float* w, int n) {
    Mat4 r;
    for (int k = 0; k < 16; ++k) {
        float s = 0;
        for (int i = 0; i < n; ++i) s += m[i].m[k] * w[i];
        r.m[k] = s;
    }
    return r;
}

}  // namespace

// The archetype's meshes in their bind pose, merged per material, x4.
const std::vector<CrowdView::Batch>& CrowdView::batches(const scene::Milo* milo) {
    auto it = batches_.find(milo);
    if (it != batches_.end()) return it->second;
    auto& out = batches_[milo];
    gfx::ScenePose pose;  // bind pose: the stored local transforms
    struct Acc {
        std::vector<float> verts;  // one copy, 16 floats a vertex
        std::vector<uint32_t> idx;
    };
    std::map<const scene::Mat*, Acc> acc;
    std::vector<const scene::Mat*> order;
    milo->root().forEach([&](scene::Object& o) {
        auto* m = o.as<scene::Mesh>();
        if (!m || !m->decoded || !m->dir || !pose.showing(m)) return;
        const scene::Mesh* geom = m;
        if (m->verts.empty() && !m->geomOwner.empty())
            if (auto* g = m->dir->resolve<scene::Mesh>(m->geomOwner)) geom = g;
        if (geom->verts.empty() || geom->faces.empty()) return;
        const scene::Mat* mat = m->dir->resolve<scene::Mat>(m->mat);
        // The bone matrices (as VenueView skins: offset then the bone's world).
        Mat4 bones[4];
        int nb = 0;
        const Mat4 world = gfx::toMat4(pose.world(m));
        if (!geom->bones.empty()) {
            nb = int(std::min<size_t>(geom->bones.size(), 4));
            for (int i = 0; i < nb; ++i) {
                const auto& b = geom->bones[size_t(i)];
                const scene::Object* bo = m->dir->resolve(b.name);
                bones[i] = bo && gfx::transOf(bo) ? gfx::toMat4(scene::compose(b.offset, pose.world(bo))) : world;
            }
        }
        const std::vector<float> src = gfx::packVertices(*geom);
        if (!acc.count(mat)) order.push_back(mat);
        Acc& a = acc[mat];
        const uint32_t base = uint32_t(a.verts.size() / 16);
        for (size_t v = 0; v + 16 <= src.size(); v += 16) {
            const float* s = &src[v];
            Mat4 M = world;
            if (nb > 0) {
                float w[4] = {0, 0, 0, 0};
                float sum = 0;
                for (int i = 0; i < nb - 1; ++i) w[i] = s[6 + i], sum += w[i];
                w[nb - 1] = 1 - sum;
                M = blend(bones, w, nb);
            }
            const Vec3 p = M.transformPoint({s[0], s[1], s[2]});
            Vec3 n = mul3(M, {s[3], s[4], s[5]});
            Vec3 t = mul3(M, {s[12], s[13], s[14]});
            if (gfx::dot(n, n) > 1e-12f) n = gfx::normalize(n);
            if (gfx::dot(t, t) > 1e-12f) t = gfx::normalize(t);
            a.verts.insert(a.verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z, 0, 0, 0, 0, s[10], s[11], t.x, t.y, t.z, s[15]});
        }
        for (const auto& f : geom->faces) a.idx.insert(a.idx.end(), {base + f[0], base + f[1], base + f[2]});
    });
    for (const scene::Mat* mat : order) {
        const Acc& a = acc[mat];
        const size_t nv = a.verts.size() / 16;
        std::vector<float> v;
        std::vector<uint32_t> idx;
        v.reserve(a.verts.size() * 4);
        idx.reserve(a.idx.size() * 4);
        for (int k = 0; k < 4; ++k) {
            // Copy k is weighted fully to object matrix k (the fourth by 1 - x - y - z).
            for (size_t i = 0; i < nv; ++i) {
                const float* s = &a.verts[i * 16];
                v.insert(v.end(), s, s + 16);
                float* w = &v[v.size() - 10];
                w[0] = k == 0 ? 1.0f : 0.0f, w[1] = k == 1 ? 1.0f : 0.0f, w[2] = k == 2 ? 1.0f : 0.0f, w[3] = 0;
            }
            for (uint32_t i : a.idx) idx.push_back(i + uint32_t(nv) * uint32_t(k));
        }
        Batch b;
        b.mat = mat;
        b.indicesPerCopy = int(a.idx.size());
        b.mesh.vertices = cache_.device().createBuffer(gfx::BufferKind::Vertex, v.size() * sizeof(float), v.data(), false);
        b.mesh.indices = cache_.device().createBuffer(gfx::BufferKind::Index, idx.size() * sizeof(uint32_t), idx.data(), false);
        buffers_.push_back(b.mesh.vertices), buffers_.push_back(b.mesh.indices);
        b.mesh.indexCount = int(idx.size());
        b.mesh.indices32 = true;
        b.mesh.skinned = true;
        b.mesh.tangents = true;
        out.push_back(b);
    }
    return out;
}

void CrowdView::draw(gfx::MaterialRenderer& mr, const scene::WorldCrowd& c, const gfx::Camera& camera, bool hdr) {
    Crowd& crowd = world_.crowd();
    const Crowd::State* s = nullptr;
    for (const Crowd::State& st : crowd.crowds())
        if (st.crowd == &c) s = &st;
    if (!s) return;
    const auto shown = s->shown();
    if (shown.empty()) return;
    const Mat4 vp = camera.viewProj(false);
    // rotate (0x82247310): x = normalize(z x f), y = z x x (= -f flattened), z up.
    const Vec3 f{camera.world.at(0, 1), camera.world.at(1, 1), camera.world.at(2, 1)};
    Vec3 fx = gfx::cross(Vec3{0, 0, 1}, f);
    const bool canFace = gfx::dot(fx, fx) > 1e-8f;
    if (canFace) fx = gfx::normalize(fx);
    const Vec3 fy = gfx::cross(Vec3{0, 0, 1}, fx);
    // Per archetype, the matrices of its visible placements.
    std::vector<std::vector<Mat4>> perArch(s->archetypes.size());
    for (const auto& [ai, pi] : shown) {
        const Crowd::Archetype& a = s->archetypes[size_t(ai)];
        scene::Xfm x = a.placements[size_t(pi)];
        // Cull by the character's middle (the placement), a generous margin.
        const Vec3 mid{x.pos[0], x.pos[1], x.pos[2]};
        const float* m = vp.m;
        const float cx = m[0] * mid.x + m[4] * mid.y + m[8] * mid.z + m[12];
        const float cy = m[1] * mid.x + m[5] * mid.y + m[9] * mid.z + m[13];
        const float cw = m[3] * mid.x + m[7] * mid.y + m[11] * mid.z + m[15];
        if (cw <= camera.nearPlane * 0.5f) continue;
        const float r = a.height * 0.75f;
        if (std::fabs(cx) > cw + r * 2 || std::fabs(cy) > cw + r * 2) continue;
        x.pos[2] -= a.height * 0.5f;
        if (s->rotate && canFace) x.rot = {fx.x, fx.y, fx.z, fy.x, fy.y, fy.z, 0, 0, 1};
        perArch[size_t(ai)].push_back(gfx::toMat4(x));
    }
    gfx::DrawContext ctx;
    ctx.bones = 4;
    ctx.hdr = hdr;
    for (size_t ai = 0; ai < perArch.size(); ++ai) {
        const auto& mats = perArch[ai];
        const Crowd::Archetype& a = s->archetypes[ai];
        if (mats.empty() || !a.milo) continue;
        characters_ += int(mats.size());
        for (const Batch& b : batches(a.milo)) {
            const gfx::Material material = b.mat ? cache_.material(*b.mat, nullptr) : gfx::Material{};
            for (size_t i = 0; i < mats.size(); i += 4) {
                const size_t n = std::min<size_t>(4, mats.size() - i);
                Mat4 bones[4];
                for (size_t k = 0; k < 4; ++k) bones[k] = mats[i + std::min(k, n - 1)];
                gfx::GpuMesh g = b.mesh;
                g.indexCount = b.indicesPerCopy * int(n);
                mr.draw(g, bones, material, ctx);
                ++drawCalls_;
            }
        }
    }
}
