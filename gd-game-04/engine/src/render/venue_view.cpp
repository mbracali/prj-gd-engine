// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_view.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "data/disc.h"
#include "data/qbkey.h"
#include "render/camera.h"
#include "render/rmath.h"
#include "data/pak.h"
#include "data/texture.h"

namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }
constexpr int kMaxLights = 16;
constexpr uint8_t kAttribUv2 = 6;

struct Vtx {
    float pos[3];
    float nrm[3];
    uint8_t col[4];
    float uv[2];
    float uv2[2];
};

const gfx::VertexLayout& layout() {
    static const gfx::VertexLayout l = [] {
        gfx::VertexLayout r;
        r.stride = sizeof(Vtx);
        r.attribs[0] = {gfx::kAttribPos, 3, gfx::AttribType::Float, uint16_t(offsetof(Vtx, pos))};
        r.attribs[1] = {gfx::kAttribNormal, 3, gfx::AttribType::Float, uint16_t(offsetof(Vtx, nrm))};
        r.attribs[2] = {gfx::kAttribColor, 4, gfx::AttribType::UNorm8, uint16_t(offsetof(Vtx, col))};
        r.attribs[3] = {gfx::kAttribUv, 2, gfx::AttribType::Float, uint16_t(offsetof(Vtx, uv))};
        r.attribs[4] = {kAttribUv2, 2, gfx::AttribType::Float, uint16_t(offsetof(Vtx, uv2))};
        return r;
    }();
    return l;
}

const char* kVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aUv;
layout(location = 6) in vec2 aUv2;
layout(std140) uniform GdVenueFrame {
    mat4 uViewProj;
    vec4 uEye;
    vec4 uParams;  // x: vertex colour scale, y: dynamic light scale
};
out vec3 vPos;
out vec3 vNormal;
out vec4 vColor;
out vec2 vUv;
out vec2 vUv2;
void main() {
    vPos = aPos;
    vNormal = aNormal;
    vColor = aColor;
    vUv = aUv;
    vUv2 = aUv2;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

// The approximation of GH3's material templates (docs/venues.md "Shading").
const char* kFragment = R"(
in vec3 vPos;
in vec3 vNormal;
in vec4 vColor;
in vec2 vUv;
in vec2 vUv2;
layout(std140) uniform GdVenueFrame {
    mat4 uViewProj;
    vec4 uEye;
    vec4 uParams;
};
layout(std140) uniform GdVenueLights {
    vec4 uInfo;     // x: lights
    vec4 uAmbient;  // ambient lights of the group, summed
    vec4 uL[64];    // per light: pos+type, dir+atten start, colour*intensity+atten end, cone
};
layout(std140) uniform GdVenueMat {
    vec4 uMat;      // x: alpha reference, y: unlit, z: has lightmap
};
uniform sampler2D sDiffuse;
uniform sampler2D sLightmap;
out vec4 fragColor;
void main() {
    vec4 tex = texture(sDiffuse, vUv);
    float alpha = tex.a * vColor.a;
    if (alpha < uMat.x) discard;
    vec3 base = tex.rgb;
    if (uMat.z > 0.5) base *= texture(sLightmap, vUv2).rgb;
    if (uMat.y > 0.5) {
        fragColor = vec4(base * vColor.rgb, alpha);
        return;
    }
    vec3 n = vNormal;
    float nl = length(n);
    bool hasNormal = nl > 0.1;
    n = hasNormal ? n / nl : vec3(0.0, 1.0, 0.0);
    vec3 lit = vColor.rgb * uParams.x + uAmbient.rgb * uParams.y;
    int count = int(uInfo.x);
    for (int i = 0; i < 16; ++i) {
        if (i >= count) break;
        vec4 p = uL[i * 4];
        vec4 d = uL[i * 4 + 1];
        vec4 c = uL[i * 4 + 2];
        vec4 cone = uL[i * 4 + 3];
        vec3 toL = p.xyz - vPos;
        float dist = length(toL);
        float att = 1.0 - smoothstep(d.w, max(c.w, d.w + 0.001), dist);
        vec3 l = p.w < 1.5 ? -d.xyz : toL / max(dist, 0.0001);
        float ndl = hasNormal ? max(dot(n, l), 0.0) : 0.6;
        if (p.w > 2.5) {
            float cs = dot(-l, d.xyz);
            att *= smoothstep(cone.y, cone.x, cs);
        }
        lit += c.rgb * (ndl * att * uParams.y);
    }
    fragColor = vec4(base * lit, alpha);
}
)";

struct FrameUniforms {
    float viewProj[16];
    float eye[4];
    float params[4];
};
struct LightUniforms {
    float info[4];
    float ambient[4];
    float l[kMaxLights * 4][4];
};
struct MatUniforms {
    float mat[4];
};

venue::Vec3 xf(const venue::Node* n, const venue::Vec3& p) {
    if (!n) return p;
    const venue::Vec3 r = venue::rotate(n->rot, p);
    return {n->pos[0] + r[0], n->pos[1] + r[1], n->pos[2] + r[2]};
}

}  // namespace

VenueView::VenueView() = default;
VenueView::~VenueView() { shutdown(); }

gfx::ProgramDesc VenueView::programDesc() {
    gfx::ProgramDesc d;
    d.name = "venue";
    d.vertex = kVertex;
    d.fragment = kFragment;
    d.uniformBlocks = {"GdVenueFrame", "GdVenueLights", "GdVenueMat"};
    d.samplers = {"sDiffuse", "sLightmap"};
    return d;
}

void VenueView::shutdown() {
    if (!dev_) return;
    if (program_) dev_->destroyProgram(program_);
    if (vb_) dev_->destroyBuffer(vb_);
    if (ib_) dev_->destroyBuffer(ib_);
    program_ = {}, vb_ = {}, ib_ = {};
    for (auto& [k, t] : textures_) dev_->destroyTexture(t.id);
    textures_.clear();
    items_.clear();
    groups_.clear();
    dev_ = nullptr;
}

bool VenueView::init(gfx::RenderDevice& dev, const Disc& disc, const venue::Venue& v, const std::vector<uint32_t>& hide, std::string& error) {
    shutdown();
    dev_ = &dev;
    v_ = &v;
    program_ = dev.createProgram(programDesc(), error);
    if (!program_) return false;
    stats_ = Stats{};
    // The zone's textures (every .tex dictionary in its pak), with their
    // class, and the global ad textures some venue signs use [C data:
    // global_ad_tex.pak holds the ad_* sectors' textures].
    for (const std::string& pakPath : {"DATA/COMPRESSED/ZONES/" + v.zone + ".pak.xen", std::string("DATA/COMPRESSED/PAK/global_ad_tex.pak.xen")}) {
        if (!disc.exists(pakPath)) continue;
        try {
            const Pak pak = Pak::load(disc, pakPath);
            for (const PakEntry& e : pak.entries()) {
                if (e.type != K(".tex")) continue;
                auto [p, size] = pak.data(e);
                for (const gh3tex::Texture& t : gh3tex::decodeTexDict(p, size)) {
                    if (!t.supported || t.levels.empty() || textures_.count(t.key)) continue;
                    std::string err;
                    auto levels = t.texLevels();
                    const gfx::TextureId id = dev.createTexture(t.desc(), levels.data(), err);
                    if (id) textures_[t.key] = Tex{id, t.usage};
                }
            }
        } catch (const std::exception& ex) {
            error = pakPath + ": " + ex.what();
            return false;
        }
    }
    stats_.textures = textures_.size();
    auto tex = [&](uint32_t key) {
        auto it = textures_.find(key);
        return it == textures_.end() ? nullptr : &it->second;
    };

    std::vector<Vtx> verts;
    std::vector<uint32_t> idx;
    auto groupOf = [&](uint32_t key) {
        if (!key) key = K("none");
        for (size_t i = 0; i < groups_.size(); ++i)
            if (groups_[i] == key) return int(i);
        groups_.push_back(key);
        return int(groups_.size() - 1);
    };
    groupOf(K("none"));
    for (const venue::Part& part : v.parts) {
        const scene::Scene& sc = part.scene;
        for (size_t si = 0; si < sc.sectors.size(); ++si) {
            const scene::Sector& sec = sc.sectors[si];
            const int ni = part.sectorNode[si];
            const venue::Node* node = ni >= 0 ? &v.nodes[size_t(ni)] : nullptr;
            // A sector shows when its node is created (at start or by a
            // node flag); local sectors without a node are never placed [I].
            if (part.sectorLocal[si] && !node) {
                ++stats_.hiddenSectors;
                continue;
            }
            const venue::Node* place = part.sectorLocal[si] ? node : nullptr;
            for (uint32_t mi = sec.firstMesh; mi < sec.firstMesh + sec.meshCount && mi < sc.meshes.size(); ++mi) {
                const scene::Mesh& m = sc.meshes[mi];
                if (m.positions.empty() || m.indices.empty()) continue;
                const scene::Material* mat = nullptr;
                for (const scene::Material& mm : sc.materials)
                    if (mm.key == m.material) mat = &mm;
                const uint32_t base = uint32_t(verts.size());
                // The lightmap's coordinates: the last texcoord set after the first [C data: lightmapped meshes carry two sets].
                int uv2Set = -1;
                for (int s = 7; s >= 1; --s)
                    if (m.texcoords[s].size() == m.positions.size()) {
                        uv2Set = s;
                        break;
                    }
                venue::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
                for (size_t k = 0; k < m.positions.size(); ++k) {
                    Vtx vt{};
                    const venue::Vec3 p = xf(place, {m.positions[k][0], m.positions[k][1], m.positions[k][2]});
                    for (size_t j = 0; j < 3; ++j) {
                        vt.pos[j] = p[j];
                        lo[j] = std::min(lo[j], p[j]), hi[j] = std::max(hi[j], p[j]);
                    }
                    if (k < m.normals.size()) {
                        venue::Vec3 n{m.normals[k][0], m.normals[k][1], m.normals[k][2]};
                        if (place) n = venue::rotate(place->rot, n);
                        for (size_t j = 0; j < 3; ++j) vt.nrm[j] = n[j];
                    }
                    if (k < m.colors.size()) std::memcpy(vt.col, m.colors[k].data(), 4);
                    else vt.col[0] = vt.col[1] = vt.col[2] = vt.col[3] = 255;
                    if (k < m.texcoords[0].size()) vt.uv[0] = m.texcoords[0][k][0], vt.uv[1] = m.texcoords[0][k][1];
                    if (uv2Set > 0) vt.uv2[0] = m.texcoords[uv2Set][k][0], vt.uv2[1] = m.texcoords[uv2Set][k][1];
                    verts.push_back(vt);
                }
                Item it;
                it.first = uint32_t(idx.size());
                for (const auto& t : m.triangles())
                    for (uint32_t c : t) idx.push_back(base + c);
                it.count = uint32_t(idx.size()) - it.first;
                if (!it.count) continue;
                stats_.triangles += it.count / 3;
                ++stats_.meshes;
                it.center = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
                it.node = ni;
                it.group = groupOf(node ? node->lightGroup : 0);
                if (mat && std::any_of(mat->textures.begin(), mat->textures.end(),
                                       [&](uint32_t k) { return std::find(hide.begin(), hide.end(), k) != hide.end(); })) {
                    ++stats_.hiddenMeshes;
                    continue;
                }
                if (mat) {
                    // The diffuse: the first colour texture (templates put normal maps
                    // first in some) [I]; the lightmap: the last texture [C data: 2048x2048
                    // single-level maps or a white 32x32].
                    bool missing = !mat->textures.empty();
                    for (size_t ti = 0; ti < mat->textures.size(); ++ti) {
                        const Tex* t = tex(mat->textures[ti]);
                        if (t) missing = false;
                        if (t && t->usage == 0 && (ti + 1 < mat->textures.size() || mat->textures.size() == 1)) {
                            it.diffuse = t->id;
                            break;
                        }
                    }
                    if (missing) ++stats_.missingTextures;
                    if (!it.diffuse && SDL_getenv("GD04_VENUE_DEBUG")) {
                        std::string keys;
                        for (uint32_t k : mat->textures) keys += " " + qbkey::names().get(k);
                        SDL_Log("venue: %s: no colour texture (shader %08x):%s", qbkey::names().get(sec.key).c_str(), mat->shader, keys.c_str());
                    }
                    if (uv2Set > 0 && mat->textures.size() >= 2)
                        if (const Tex* t = tex(mat->textures.back()); t && t->usage == 0) it.lightmap = t->id;
                    // Material words [I, docs/venues.md "Shading"]: +0xe8's top byte the
                    // alpha cut-off, +0xd8 the blend mode (0 opaque, 1 add, 2 subtract,
                    // 3 blend), +0xd4 the draw order, +0xcc bit 3 a blended surface.
                    it.alphaRef = float(mat->e8 >> 24) / 255.0f;
                    it.drawOrder = mat->d4;
                    switch (mat->d8) {
                    case 0: it.mode = (mat->cc & 8) ? Mode::Blend : Mode::Opaque; break;
                    case 1: it.mode = Mode::Add; break;
                    case 2: it.mode = Mode::Subtract; break;
                    default: it.mode = Mode::Blend; break;
                    }
                    it.unlit = it.mode == Mode::Add;
                }
                items_.push_back(it);
            }
        }
    }
    if (verts.empty()) {
        error = v.zone + ": no geometry";
        return false;
    }
    vb_ = dev.createBuffer(gfx::BufferKind::Vertex, verts.size() * sizeof(Vtx), verts.data(), false);
    ib_ = dev.createBuffer(gfx::BufferKind::Index, idx.size() * sizeof(uint32_t), idx.data(), false);
    // Opaque first, then the blended ones by draw order (sorted again by depth each frame).
    std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
        const bool ta = a.mode != Mode::Opaque, tb = b.mode != Mode::Opaque;
        if (ta != tb) return !ta;
        return ta && a.drawOrder < b.drawOrder;
    });
    using ull = unsigned long long;  // (MinGW's SDL_Log has no %zu)
    SDL_Log("venue %s: %llu meshes, %llu triangles, %llu textures (%llu missing), %llu lights, %llu local sectors without a node, "
            "%llu crowd-card meshes hidden", v.zone.c_str(), ull(stats_.meshes), ull(stats_.triangles), ull(stats_.textures),
            ull(stats_.missingTextures), ull(v.lights.size()), ull(stats_.hiddenSectors), ull(stats_.hiddenMeshes));
    return true;
}

void VenueView::draw(const venue::CamPose& cam, const std::vector<venue::LightState>& lights, const venue::LightShow& show) {
    if (!dev_ || !program_) return;
    gfx::RenderDevice& d = *dev_;
    const venue::Vec3 fwd = venue::rotate(cam.rot, {0, 0, 1});
    const venue::Vec3 up = venue::rotate(cam.rot, {0, 1, 0});
    // Screen right is the camera's local -x (a right-handed world seen down +z) [I: checked on the venues' signs].
    const venue::Vec3 right = venue::rotate(cam.rot, {-1, 0, 0});
    gfx::Mat4 view;
    const venue::Vec3 rows[3] = {right, up, {-fwd[0], -fwd[1], -fwd[2]}};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) view.at(r, c) = rows[r][size_t(c)];
        view.at(r, 3) = -(rows[r][0] * cam.pos[0] + rows[r][1] * cam.pos[1] + rows[r][2] * cam.pos[2]);
    }
    const float aspect = gfx::kLogicalW / gfx::kLogicalH;
    const float hfov = cam.fov * 3.14159265f / 180.0f;
    const float yfov = 2.0f * std::atan(std::tan(hfov * 0.5f) / aspect);
    const gfx::Mat4 proj = gfx::Mat4::perspective(yfov, aspect, 0.05f, 4000.0f, d.info().clipDepthZeroToOne);
    const gfx::Mat4 vp = proj * view;

    FrameUniforms fu{};
    std::memcpy(fu.viewProj, vp.m, sizeof fu.viewProj);
    fu.eye[0] = cam.pos[0], fu.eye[1] = cam.pos[1], fu.eye[2] = cam.pos[2];
    static const float vcScale = SDL_getenv("GD04_VENUE_VC") ? float(std::atof(SDL_getenv("GD04_VENUE_VC"))) : 1.0f;
    static const float dynScale = SDL_getenv("GD04_VENUE_DYN") ? float(std::atof(SDL_getenv("GD04_VENUE_DYN"))) : 0.6f;
    fu.params[0] = vcScale, fu.params[1] = dynScale;
    const gfx::UniformData frame = d.stageUniforms(&fu, sizeof fu);

    // Per light group: the group's created, non-baked lights [I: "_VC_" lights only bake vertex colours].
    std::vector<gfx::UniformData> groupLights(groups_.size());
    for (size_t gi = 0; gi < groups_.size(); ++gi) {
        LightUniforms lu{};
        int n = 0;
        for (size_t i = 0; i < v_->lights.size() && i < lights.size(); ++i) {
            const venue::Light& l = v_->lights[i];
            const uint32_t grp = l.group ? l.group : K("none");
            if (grp != groups_[gi] || l.vertexBake) continue;
            if (!l.createdAtStart && !(l.createdOn && show.flag(l.createdOn))) continue;
            const venue::LightState& s = lights[i];
            if (s.intensity <= 0) continue;
            if (l.type == venue::LightType::Ambient) {
                for (size_t k = 0; k < 3; ++k) lu.ambient[k] += s.color[k] * s.intensity;
                continue;
            }
            if (n >= kMaxLights) continue;
            float* p = lu.l[n * 4];
            p[0] = l.pos[0], p[1] = l.pos[1], p[2] = l.pos[2];
            p[3] = l.type == venue::LightType::Directional ? 1.0f : l.type == venue::LightType::Point ? 2.0f : 3.0f;
            float* dd = lu.l[n * 4 + 1];
            dd[0] = l.dir[0], dd[1] = l.dir[1], dd[2] = l.dir[2], dd[3] = s.attenStart;
            float* c = lu.l[n * 4 + 2];
            c[0] = s.color[0] * s.intensity, c[1] = s.color[1] * s.intensity, c[2] = s.color[2] * s.intensity, c[3] = s.attenEnd;
            float* cone = lu.l[n * 4 + 3];
            // hotspot / field: the cone's full angles in degrees [I].
            cone[0] = std::cos(l.hotspot * 0.5f * 3.14159265f / 180.0f);
            cone[1] = std::cos(std::max(l.field, l.hotspot + 0.1f) * 0.5f * 3.14159265f / 180.0f);
            ++n;
        }
        lu.info[0] = float(n);
        groupLights[gi] = d.stageUniforms(&lu, sizeof lu);
    }

    // The blended items back to front within their draw order.
    std::vector<const Item*> order;
    order.reserve(items_.size());
    for (const Item& it : items_) {
        if (it.node >= 0) {
            const venue::Node& n = v_->nodes[size_t(it.node)];
            if (!n.createdAtStart && !(n.createdOn && show.flag(n.createdOn))) continue;
        }
        order.push_back(&it);
    }
    auto depth = [&](const Item* it) {
        return (it->center[0] - cam.pos[0]) * fwd[0] + (it->center[1] - cam.pos[1]) * fwd[1] + (it->center[2] - cam.pos[2]) * fwd[2];
    };
    std::stable_sort(order.begin(), order.end(), [&](const Item* a, const Item* b) {
        const bool ta = a->mode != Mode::Opaque, tb = b->mode != Mode::Opaque;
        if (ta != tb) return !ta;
        if (!ta) return false;
        if (a->drawOrder != b->drawOrder) return a->drawOrder < b->drawOrder;
        return depth(a) > depth(b);
    });

    gfx::DrawCall c;
    c.program = program_;
    c.layout = &layout();
    c.vertices = vb_;
    c.indices = ib_;
    c.indices32 = true;
    c.uniforms[0] = frame;
    c.samplers[0] = {gfx::Filter::Trilinear, gfx::Wrap::Repeat, 0};
    c.samplers[1] = {gfx::Filter::Linear, gfx::Wrap::Clamp, 0};
    c.state.cull = gfx::Cull::None;
    size_t draws = 0;
    for (const Item* it : order) {
        c.first = int(it->first);
        c.count = int(it->count);
        c.textures[0] = it->diffuse;
        c.textures[1] = it->lightmap;
        c.uniforms[1] = groupLights[size_t(it->group)];
        MatUniforms mu{{it->alphaRef, it->unlit ? 1.0f : 0.0f, it->lightmap ? 1.0f : 0.0f, 0}};
        c.uniforms[2] = {&mu, sizeof mu};
        c.state.depthTest = true;
        c.state.depthFunc = gfx::CompareFunc::LessEqual;
        switch (it->mode) {
        case Mode::Opaque:
            c.state.blend = false, c.state.depthWrite = true;
            break;
        case Mode::Blend:
            c.state.blend = true, c.state.depthWrite = false;
            c.state.src = gfx::BlendFactor::SrcAlpha, c.state.dst = gfx::BlendFactor::InvSrcAlpha, c.state.op = gfx::BlendOp::Add;
            break;
        case Mode::Add:
            c.state.blend = true, c.state.depthWrite = false;
            c.state.src = gfx::BlendFactor::SrcAlpha, c.state.dst = gfx::BlendFactor::One, c.state.op = gfx::BlendOp::Add;
            break;
        case Mode::Subtract:
            c.state.blend = true, c.state.depthWrite = false;
            c.state.src = gfx::BlendFactor::SrcAlpha, c.state.dst = gfx::BlendFactor::One, c.state.op = gfx::BlendOp::ReverseSubtract;
            break;
        }
        d.draw(c);
        ++draws;
    }
    stats_.drawsLastFrame = draws;
}
