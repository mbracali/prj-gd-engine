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

using Vtx = gh3shader::Vertex;
const gfx::VertexLayout& layout() { return gh3shader::vertexLayout(); }

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

void VenueView::setOverride(uint32_t texture, gfx::TextureId id) {
    if (id) overrides_[texture] = id;
    else overrides_.erase(texture);
}

void VenueView::shutdown() {
    if (!dev_) return;
    if (program_) dev_->destroyProgram(program_);
    if (vb_) dev_->destroyBuffer(vb_);
    if (ib_) dev_->destroyBuffer(ib_);
    if (coneVb_) dev_->destroyBuffer(coneVb_);
    if (coneIb_) dev_->destroyBuffer(coneIb_);
    program_ = {}, vb_ = {}, ib_ = {}, coneVb_ = {}, coneIb_ = {};
    cones_.clear();
    gh3_.shutdown();
    mats_.clear();
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
    // GH3's own shaders (GD04_GH3_SHADERS=0: the approximation only, a development aid).
    if (!(SDL_getenv("GD04_GH3_SHADERS") && SDL_getenv("GD04_GH3_SHADERS")[0] == '0')) gh3_.init(dev, gh3shader::library(disc));
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
                    if (id) textures_[t.key] = Tex{id, t.usage, t.kind == gfx::TexKind::Cube};
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
    // One mesh: its vertices (moved by `place`, or left in model space for
    // a housing, which draws at its live transform), indices and item.
    auto addMesh = [&](const scene::Scene& sc, const scene::Mesh& m, const venue::Node* place, int ni, const venue::Node* node,
                       uint32_t secKey, int housing) {
        if (m.positions.empty() || m.indices.empty()) return;
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
            for (int tb = 0; tb < 2; ++tb) {
                const auto& src = tb ? m.binormals[0] : m.tangents[0];
                if (k >= src.size()) continue;
                venue::Vec3 t{src[k][0], src[k][1], src[k][2]};
                if (place) t = venue::rotate(place->rot, t);
                for (size_t j = 0; j < 3; ++j) (tb ? vt.bin : vt.tan)[j] = t[j];
            }
            if (k < m.colors.size()) std::memcpy(vt.col, m.colors[k].data(), 4);
            else vt.col[0] = vt.col[1] = vt.col[2] = vt.col[3] = 255;
            if (k < m.positions2.size()) {
                const venue::Vec3 p2 = xf(place, {m.positions2[k][0], m.positions2[k][1], m.positions2[k][2]});
                for (size_t j = 0; j < 3; ++j) vt.pos2[j] = p2[j];
            }
            // TEXCOORD0..3 by usage index; the approximation reads its lightmap from set 1.
            for (int ts = 0; ts < 7; ++ts)
                if (k < m.texcoords[ts].size()) vt.uv[ts][0] = m.texcoords[ts][k][0], vt.uv[ts][1] = m.texcoords[ts][k][1];
            // The approximation's lightmap coordinates (location 6): the last set after the first.
            if (uv2Set > 1 && !gh3_.ready()) vt.uv[1][0] = m.texcoords[uv2Set][k][0], vt.uv[1][1] = m.texcoords[uv2Set][k][1];
            verts.push_back(vt);
        }
        Item it;
        it.first = uint32_t(idx.size());
        for (const auto& t : m.triangles())
            for (uint32_t c : t) idx.push_back(base + c);
        it.count = uint32_t(idx.size()) - it.first;
        if (!it.count) return;
        stats_.triangles += it.count / 3;
        ++stats_.meshes;
        it.center = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
        it.node = ni;
        it.housing = housing;
        it.group = groupOf(node ? node->lightGroup : 0);
        // The node's lod_dist2: not drawn farther from the camera [I: lod_dist1..2 is a
        // fade or a lower LOD; every venue node has 150/151 or 400/401 m].
        if (node && node->st)
            if (const qb::Value* ld = node->st->find(K("lod_dist2")); ld && (ld->type == qb::Type::Float || ld->type == qb::Type::Int))
                it.lodDist = ld->number();
        if (mat)
            for (uint32_t k : mat->textures)
                if (std::find(hide.begin(), hide.end(), k) != hide.end()) it.card = k;
        if (it.card) ++stats_.hiddenMeshes;
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
                SDL_Log("venue: %s: no colour texture (shader %08x):%s", qbkey::names().get(secKey).c_str(), mat->shader, keys.c_str());
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
            it.flare = mat->shader == K("Flare");
            it.shaderKey = mat->shader;
            // GH3's template: material constants c0.. of both stages, textures in sampler order.
            if (gh3_.ready()) {
                if (const gh3shader::Program* gp = gh3_.get(mat->shader)) {
                    Mat mm;
                    gh3shader::fillMaterialConstants(*mat, it.alphaRef > 0 ? it.alphaRef : -1.0f, mm.consts);
                    for (size_t ti = 0; ti < mat->textures.size() && ti < 16; ++ti)
                        if (const Tex* t = tex(mat->textures[ti])) mm.tex[ti] = t->id;
                    for (int k = 0; k < 16; ++k)
                        if ((gp->samplers >> k & 1) && !mm.tex[k]) mm.tex[k] = (gp->cubes >> k & 1) ? gh3_.whiteCube() : gh3_.white();
                    it.gh3 = gp;
                    it.mat = int(mats_.size());
                    mats_.push_back(mm);
                }
            }
        }
        if (it.gh3) ++stats_.gh3Meshes;
        else ++stats_.fallbackMeshes;
        items_.push_back(it);
    };
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
            for (uint32_t mi = sec.firstMesh; mi < sec.firstMesh + sec.meshCount && mi < sc.meshes.size(); ++mi)
                addMesh(sc, sc.meshes[mi], place, ni, node, sec.key, -1);
        }
    }
    // The light housings: game objects of the _gfx node array with a
    // lightvolumetype and a Model (LightHousings/GO_LightHousing02/
    // GO_LightHousing02.mdl, in ZONES/global.pak with its .tex) [C data].
    {
        std::unordered_map<uint32_t, std::unique_ptr<scene::Scene>> models;
        std::unique_ptr<Pak> global;
        for (size_t ni = 0; ni < v.nodes.size(); ++ni) {
            const venue::Node& n = v.nodes[ni];
            if (!n.st || !n.st->find(K("lightvolumetype"))) continue;
            const qb::Value* mv = n.st->find(K("Model"));
            if (!mv || (mv->type != qb::Type::String && mv->type != qb::Type::WString)) continue;
            std::string path = "models\\";
            for (char c : mv->str)
                if (!(c == '\\' && path.back() == '\\')) path += c == '/' ? '\\' : c;
            const uint32_t key = qbkey::key(path);
            auto it = models.find(key);
            if (it == models.end()) {
                std::unique_ptr<scene::Scene> sc;
                try {
                    if (!global) global = std::make_unique<Pak>(Pak::load(disc, "DATA/COMPRESSED/ZONES/global.pak.xen"));
                    if (const PakEntry* e = global->findType(key, K(".mdl"))) {
                        auto [p, size] = global->data(*e);
                        sc = std::make_unique<scene::Scene>(scene::parse(p, size, scene::Kind::Mdl));
                        if (e->parent)
                            if (const PakEntry* te = global->findType(e->parent, K(".tex"))) {
                                auto [tp, ts] = global->data(*te);
                                for (const gh3tex::Texture& t : gh3tex::decodeTexDict(tp, ts)) {
                                    if (!t.supported || t.levels.empty() || textures_.count(t.key)) continue;
                                    std::string err;
                                    auto levels = t.texLevels();
                                    if (const gfx::TextureId id = dev.createTexture(t.desc(), levels.data(), err))
                                        textures_[t.key] = Tex{id, t.usage, t.kind == gfx::TexKind::Cube};
                                }
                            }
                    }
                } catch (const std::exception& ex) {
                    SDL_Log("venue: %s: %s", path.c_str(), ex.what());
                }
                it = models.emplace(key, std::move(sc)).first;
            }
            if (const qb::Value* lt = n.st->find(K("lightvolumetype")); lt && lt->key == K("ConeImpostor")) cones_.push_back(int(ni));
            if (!it->second) continue;
            ++stats_.housings;
            for (const scene::Mesh& m : it->second->meshes) addMesh(*it->second, m, nullptr, int(ni), &n, n.name, int(ni));
        }
    }
    if (verts.empty()) {
        error = v.zone + ": no geometry";
        return false;
    }
    // The cone impostor's quad: (x, y, z, w) = near edge (+-1, 0, 0, 1), far
    // edge (0, +-1, 1, 0); _Cone_Impostor's vertex shader places it at
    // position - z x range x dir + (x x start radius + y x end radius) x side
    // and fades by w [I: the engine's mesh; the shader's use of it is C].
    {
        const float cv[16] = {-1, 0, 0, 1, 1, 0, 0, 1, 0, -1, 1, 0, 0, 1, 1, 0};
        const uint32_t ci[6] = {0, 1, 2, 1, 3, 2};
        coneVb_ = dev.createBuffer(gfx::BufferKind::Vertex, sizeof cv, cv, false);
        coneIb_ = dev.createBuffer(gfx::BufferKind::Index, sizeof ci, ci, false);
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
            "%llu crowd-card meshes; %llu meshes with GH3's shaders, %llu approximated", v.zone.c_str(), ull(stats_.meshes),
            ull(stats_.triangles), ull(stats_.textures), ull(stats_.missingTextures), ull(v.lights.size()), ull(stats_.hiddenSectors),
            ull(stats_.hiddenMeshes), ull(stats_.gh3Meshes), ull(stats_.fallbackMeshes));
    return true;
}

void VenueView::cameraGlobals(const venue::CamPose& cam, bool clipZeroToOne, double seconds, gh3shader::Globals& g, gfx::Mat4* viewProjOut,
                              float aspect, bool flipY, gfx::Mat4* viewOut) {
    (void)clipZeroToOne;
    // Direct3D conventions as GH3's shaders expect them: view space x right,
    // y up, z forward (the camera's +z), projection to depth 0..1 (the
    // shaders' clip depth is converted to OpenGL's when exported).
    const venue::Vec3 fwd = venue::rotate(cam.rot, {0, 0, 1});
    const venue::Vec3 up = venue::rotate(cam.rot, {0, 1, 0});
    const venue::Vec3 right = venue::rotate(cam.rot, {-1, 0, 0});
    gfx::Mat4 view;
    const venue::Vec3 rows[3] = {right, up, fwd};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) view.at(r, c) = rows[r][size_t(c)];
        view.at(r, 3) = -(rows[r][0] * cam.pos[0] + rows[r][1] * cam.pos[1] + rows[r][2] * cam.pos[2]);
    }
    gfx::Mat4 inv;  // camera to world
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) inv.at(r, c) = rows[c][size_t(r)];
        inv.at(r, 3) = cam.pos[size_t(r)];
    }
    const float hfov = cam.fov * 3.14159265f / 180.0f;
    const float sx = 1.0f / std::tan(hfov * 0.5f), sy = sx * aspect;
    const float zn = 0.05f, zf = 4000.0f;
    gfx::Mat4 proj;
    proj.at(0, 0) = sx, proj.at(1, 1) = flipY ? -sy : sy;
    proj.at(2, 2) = zf / (zf - zn), proj.at(2, 3) = -zn * zf / (zf - zn);
    proj.at(3, 2) = 1, proj.at(3, 3) = 0;
    const gfx::Mat4 vp = proj * view;
    g = gh3shader::Globals{};
    g.setMatrix(gh3shader::kRegView, view, 4);
    g.setMatrix(gh3shader::kRegProjection, proj, 4);
    g.setMatrix(gh3shader::kRegInvView, inv, 4);
    g.setMatrix(gh3shader::kRegViewProj, vp, 4);
    g.setMatrix(gh3shader::kRegTexProj0, gfx::Mat4::identity(), 4);
    g.set(gh3shader::kRegInvProj, 1.0f / sx, 1.0f / sy, 0, 0);
    const float t = float(seconds);
    g.set(gh3shader::kRegTime, t, t, t, t);
    g.set(gh3shader::kRegViewport, gfx::kLogicalW, gfx::kLogicalH, 1.0f / gfx::kLogicalW, 1.0f / gfx::kLogicalH);
    // g_distanceLODScaler: max(x, w * (y - distance)) scales the normal and
    // specular detail; x = 1 keeps the full detail [I].
    g.set(gh3shader::kRegDistanceLodScaler, 1, 0, 0, 0);
    // The static shadow (light) map applies fully to the SH and key lights [I].
    g.set(gh3shader::kRegShadowLightIntensity, 1, 1, 1, 1);
    g.set(gh3shader::kRegShadowMapSize, 1024, 1024, 1.0f / 1024, 1.0f / 1024);
    g.set(gh3shader::kRegShadowLevel, 0, 0, 0, 0);
    g.set(gh3shader::kRegShadowMapConstants, 1, 0, 0, 0);
    if (viewProjOut) *viewProjOut = vp;
    if (viewOut) *viewOut = view;
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
    std::vector<std::vector<gh3shader::LightIn>> groupIn(groups_.size());
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
            {
                gh3shader::LightIn li;
                li.type = l.type == venue::LightType::Ambient ? gh3shader::LightIn::Ambient
                          : l.type == venue::LightType::Directional ? gh3shader::LightIn::Directional
                          : l.type == venue::LightType::Point ? gh3shader::LightIn::Point : gh3shader::LightIn::Spot;
                li.pos = {l.pos[0], l.pos[1], l.pos[2]};
                li.dir = {l.dir[0], l.dir[1], l.dir[2]};
                li.color = gfx::Vec3{s.color[0], s.color[1], s.color[2]} * s.intensity;
                li.attenStart = s.attenStart, li.attenEnd = s.attenEnd;
                li.specular = s.specular;
                li.cosHotspot = std::cos(l.hotspot * 0.5f * 3.14159265f / 180.0f);
                li.cosField = std::cos(std::max(l.field, l.hotspot + 0.1f) * 0.5f * 3.14159265f / 180.0f);
                groupIn[gi].push_back(li);
            }
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
    // The light housings' live transforms (the snapshots move them).
    const double nowMs = seconds_ * 1000.0;
    std::unordered_map<int, gfx::Mat4> housingWorld;
    auto worldOfHousing = [&](int node) -> const gfx::Mat4& {
        auto it = housingWorld.find(node);
        if (it != housingWorld.end()) return it->second;
        const venue::Node& n = v_->nodes[size_t(node)];
        venue::HousingState h;
        if (!show.housing(n.name, nowMs, h)) h.pos = n.pos, h.rot = n.rot;
        gfx::Mat4 w;
        const venue::Vec3 ax = venue::rotate(h.rot, {1, 0, 0}), ay = venue::rotate(h.rot, {0, 1, 0}), az = venue::rotate(h.rot, {0, 0, 1});
        for (int r = 0; r < 3; ++r) {
            w.at(r, 0) = ax[size_t(r)], w.at(r, 1) = ay[size_t(r)], w.at(r, 2) = az[size_t(r)];
            w.at(r, 3) = h.pos[size_t(r)];
        }
        return housingWorld.emplace(node, w).first->second;
    };
    auto centreOf = [&](const Item* it) -> venue::Vec3 {
        if (it->housing < 0) return it->center;
        const gfx::Vec3 c = worldOfHousing(it->housing).transformPoint({it->center[0], it->center[1], it->center[2]});
        return {c.x, c.y, c.z};
    };
    std::vector<const Item*> order;
    order.reserve(items_.size());
    for (const Item& it : items_) {
        if (it.node >= 0) {
            const venue::Node& n = v_->nodes[size_t(it.node)];
            if (!n.createdAtStart && !(n.createdOn && show.flag(n.createdOn))) continue;
        }
        if (it.card && !overrides_.count(it.card)) continue;
        if (it.housing >= 0 && !it.gh3) continue;  // model space: GH3's shaders only
        if (it.lodDist > 0) {
            const venue::Vec3 cc = centreOf(&it);
            const float dx = cc[0] - cam.pos[0], dy = cc[1] - cam.pos[1], dz = cc[2] - cam.pos[2];
            if (dx * dx + dy * dy + dz * dz > it.lodDist * it.lodDist) continue;
        }
        order.push_back(&it);
    }
    auto depth = [&](const Item* it) {
        const venue::Vec3 c = centreOf(it);
        return (c[0] - cam.pos[0]) * fwd[0] + (c[1] - cam.pos[1]) * fwd[1] + (c[2] - cam.pos[2]) * fwd[2];
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
    // GH3's shaders: the camera's engine constants once, each material's
    // constants once, each mesh's lights (its centre's).
    gh3shader::Globals globals;
    gfx::Mat4 viewM, viewProjM;
    cameraGlobals(cam, d.info().clipDepthZeroToOne, seconds_, globals, &viewProjM, 16.0f / 9.0f, false, &viewM);
    if (shadowTex_) {
        // g_ViewToLightProjection0/1 (view space -> the light's clip space;
        // both cascades the one map [I]), g_shadowMapConstants (z, w: the
        // cascade fade's start and end distance: none), g_shadowLevel (z, w:
        // light = i + lit x (1 - i), i = SetDynamicShadowIntensity 0.35 [C
        // script tod_manager_create_perm_lights; I: the register's use, the
        // shadows as dark as the reference captures']),
        // g_shadowMapSize (x, y: the maps' sizes, tod_profiles' 1024 x 512 [C]).
        const gfx::Mat4 v2l = shadowVP_ * viewM.inverseAffine();
        globals.setMatrix(gh3shader::kRegViewToLight0, v2l, 4);
        globals.setMatrix(gh3shader::kRegViewToLight1, v2l, 4);
        globals.set(gh3shader::kRegShadowMapConstants, 0, 0, 0, 1e9f);
        globals.set(gh3shader::kRegShadowLevel, 0, 0, 1.0f - shadowIntensity_, shadowIntensity_);
        const float sz = float(shadowSize_);
        globals.set(gh3shader::kRegShadowMapSize, sz, sz, 1.0f / sz, 1.0f / sz);
    }
    const gfx::UniformData gframe = d.stageUniforms(&globals, sizeof globals);
    std::vector<gfx::UniformData> matVs(mats_.size()), matPs(mats_.size());
    gh3shader::ObjectConstants obj;
    obj.setMatrix(gh3shader::kRegWorld, gfx::Mat4::identity(), 4);
    obj.setMatrix(gh3shader::kRegWorldView, gfx::Mat4{}, 4);
    {
        // World = identity: WorldView = View, WorldViewProj = ViewProj.
        for (int r = 0; r < 4; ++r) {
            std::memcpy(obj.reg(gh3shader::kRegWorldView + r), globals.reg(gh3shader::kRegView + r), 16);
            std::memcpy(obj.reg(gh3shader::kRegWorldViewProj + r), globals.reg(gh3shader::kRegViewProj + r), 16);
        }
    }
    gfx::DrawCall gc;
    gc.layout = &layout();
    gc.vertices = vb_;
    gc.indices = ib_;
    gc.indices32 = true;
    gc.uniforms[0] = gframe;
    for (int k = 0; k < 16; ++k) gc.samplers[k] = {gfx::Filter::Trilinear, gfx::Wrap::Repeat, 0};

    size_t draws = 0;
    for (const Item* it : order) {
        c.first = int(it->first);
        c.count = int(it->count);
        if (it->gh3) {
            gc.program = it->gh3->id;
            const gh3shader::Program* rcv = nullptr;
            if (shadowTex_ && it->mode == Mode::Opaque && it->housing < 0 && !it->card) {
                rcv = gh3_.get(it->shaderKey, matlib::DynamicShadowReceiver);
                if (rcv) gc.program = rcv->id;
            }
            gc.first = c.first, gc.count = c.count;
            const size_t mi = size_t(it->mat);
            if (!matVs[mi].staged) {
                matVs[mi] = d.stageUniforms(mats_[mi].consts.vs, sizeof mats_[mi].consts.vs);
                matPs[mi] = d.stageUniforms(mats_[mi].consts.ps, sizeof mats_[mi].consts.ps);
            }
            gc.uniforms[1] = matVs[mi];
            gc.uniforms[2] = matPs[mi];
            // A housing's flare takes the housing's volume colour (the snapshots
            // set it: orange flares, 0 = off) [I: the engine's own use of it].
            if (it->housing >= 0 && it->flare) {
                venue::HousingState h;
                if (show.housing(v_->nodes[size_t(it->housing)].name, nowMs, h)) {
                    float ps[64][4];
                    std::memcpy(ps, mats_[mi].consts.ps, sizeof ps);
                    for (int k = 0; k < 3; ++k) ps[0][k] *= h.volumeColor[size_t(k)];
                    gc.uniforms[2] = d.stageUniforms(ps, sizeof ps);
                }
            }
            const auto& gl = groupIn[size_t(it->group)];
            const venue::Vec3 cc = centreOf(it);
            if (it->housing >= 0) {
                gh3shader::ObjectConstants ho = obj;
                const gfx::Mat4& w = worldOfHousing(it->housing);
                ho.setMatrix(gh3shader::kRegWorld, w, 4);
                ho.setMatrix(gh3shader::kRegWorldView, viewM * w, 4);
                ho.setMatrix(gh3shader::kRegWorldViewProj, viewProjM * w, 4);
                gh3shader::lightObject(gl.data(), gl.size(), {cc[0], cc[1], cc[2]}, ho);
                gc.uniforms[3] = d.stageUniforms(&ho, sizeof ho);
            } else {
                gh3shader::lightObject(gl.data(), gl.size(), {cc[0], cc[1], cc[2]}, obj);
                gc.uniforms[3] = {&obj, sizeof obj, 0};
            }
            for (int k = 0; k < 16; ++k) gc.textures[k] = mats_[mi].tex[k];
            if (rcv) {
                for (int k = 0; k < 14; ++k)
                    if ((rcv->samplers >> k & 1) && !gc.textures[k]) gc.textures[k] = gh3_.white();
                gc.textures[14] = gc.textures[15] = shadowTex_;  // g_sampShadowMap0 / 1
                gc.samplers[14] = gc.samplers[15] = {gfx::Filter::Nearest, gfx::Wrap::Clamp, 0};
            } else {
                gc.samplers[14] = gc.samplers[15] = {gfx::Filter::Trilinear, gfx::Wrap::Repeat, 0};
            }
            if (it->card) gc.textures[0] = overrides_[it->card];  // the card's texture is its first [C data: Billboard_Fast, one texture]
            gc.state = c.state;
            {
                // Direct3D's default culling, counter-clockwise faces [I: the
                // templates' render states are not decoded; checked on every
                // venue's shots: the stage-rear cameras (z_dive Stage_Rear01)
                // stand behind the back wall and see through its back faces].
                // GD04_CULL=0 (development aid) draws both sides.
                static const bool noCull = SDL_getenv("GD04_CULL") && SDL_getenv("GD04_CULL")[0] == '0';
                gc.state.cull = noCull ? gfx::Cull::None : gfx::Cull::CounterClockwise;
            }
            gc.state.depthTest = true;
            gc.state.depthFunc = gfx::CompareFunc::LessEqual;
            switch (it->mode) {
            case Mode::Opaque: gc.state.blend = false, gc.state.depthWrite = true; break;
            case Mode::Blend:
                gc.state.blend = true, gc.state.depthWrite = false;
                gc.state.src = gfx::BlendFactor::SrcAlpha, gc.state.dst = gfx::BlendFactor::InvSrcAlpha, gc.state.op = gfx::BlendOp::Add;
                break;
            case Mode::Add:
                gc.state.blend = true, gc.state.depthWrite = false;
                gc.state.src = gfx::BlendFactor::SrcAlpha, gc.state.dst = gfx::BlendFactor::One, gc.state.op = gfx::BlendOp::Add;
                break;
            case Mode::Subtract:
                gc.state.blend = true, gc.state.depthWrite = false;
                gc.state.src = gfx::BlendFactor::SrcAlpha, gc.state.dst = gfx::BlendFactor::One, gc.state.op = gfx::BlendOp::ReverseSubtract;
                break;
            }
            d.draw(gc);
            ++draws;
            continue;
        }
        c.textures[0] = it->card ? overrides_[it->card] : it->diffuse;
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

void VenueView::drawVolumes(const venue::CamPose& cam, const venue::LightShow& show) {
    if (!dev_ || cones_.empty() || !gh3_.ready()) return;
    const gh3shader::Program* prog = gh3_.get(K("_Cone_Impostor"));
    if (!prog) return;
    gfx::RenderDevice& d = *dev_;
    gh3shader::Globals globals;
    cameraGlobals(cam, d.info().clipDepthZeroToOne, seconds_, globals);
    const gfx::UniformData frame = d.stageUniforms(&globals, sizeof globals);
    gh3shader::ObjectConstants obj;
    obj.setMatrix(gh3shader::kRegWorld, gfx::Mat4::identity(), 4);
    const gfx::UniformData objData = d.stageUniforms(&obj, sizeof obj);
    static const gfx::VertexLayout layout = [] {
        gfx::VertexLayout l;
        l.stride = 16;
        l.attribs[0] = {0, 4, gfx::AttribType::Float, 0};
        return l;
    }();
    gfx::DrawCall c;
    c.program = prog->id;
    c.layout = &layout;
    c.vertices = coneVb_;
    c.indices = coneIb_;
    c.indices32 = true;
    c.first = 0, c.count = 6;
    c.uniforms[0] = frame;
    c.uniforms[3] = objData;
    for (int k = 0; k < 16; ++k) {
        c.textures[k] = (prog->samplers >> k & 1) ? gh3_.white() : gfx::TextureId{};  // m_SmokeTextureSamp: the engine's smoke [I: none]
        c.samplers[k] = {gfx::Filter::Linear, gfx::Wrap::Repeat, 0};
    }
    c.state.blend = true;
    c.state.src = gfx::BlendFactor::One, c.state.dst = gfx::BlendFactor::One, c.state.op = gfx::BlendOp::Add;
    c.state.depthTest = true, c.state.depthWrite = false;
    c.state.depthFunc = gfx::CompareFunc::LessEqual;
    c.state.cull = gfx::Cull::None;
    const double ms = seconds_ * 1000.0;
    for (int ni : cones_) {
        const venue::Node& n = v_->nodes[size_t(ni)];
        if (!n.createdAtStart && !(n.createdOn && show.flag(n.createdOn))) continue;
        venue::HousingState h;
        if (!show.housing(n.name, ms, h)) continue;
        const float lum = h.volumeColor[0] + h.volumeColor[1] + h.volumeColor[2];
        if (h.density <= 0 || lum <= 0 || h.range <= 0) continue;
        // The beam leaves the lens along the housing's -z (the flare quads sit at -z) [C data: the housing models].
        const venue::Vec3 z = venue::rotate(h.rot, {0, 0, 1});
        const venue::Vec3 mid{h.pos[0] - z[0] * h.range * 0.5f, h.pos[1] - z[1] * h.range * 0.5f, h.pos[2] - z[2] * h.range * 0.5f};
        const venue::Vec3 toCam{cam.pos[0] - mid[0], cam.pos[1] - mid[1], cam.pos[2] - mid[2]};
        // g_Rotated_Vector: across the beam, facing the camera.
        venue::Vec3 side{z[1] * toCam[2] - z[2] * toCam[1], z[2] * toCam[0] - z[0] * toCam[2], z[0] * toCam[1] - z[1] * toCam[0]};
        const float sl = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (sl < 1e-5f) continue;
        for (float& v : side) v /= sl;
        float vs[64][4] = {};
        float ps[64][4] = {};
        vs[0][0] = h.startRadius, vs[0][1] = h.endRadius, vs[0][2] = h.range;                 // g_ConeSize
        vs[1][0] = z[0], vs[1][1] = z[1], vs[1][2] = z[2];                                    // g_SpotlightDir
        vs[2][0] = h.pos[0], vs[2][1] = h.pos[1], vs[2][2] = h.pos[2], vs[2][3] = 1;          // g_SpotlightPosition
        vs[3][0] = side[0], vs[3][1] = side[1], vs[3][2] = side[2];                           // g_Rotated_Vector
        const float t = float(seconds_);
        vs[4][0] = t * 0.02f, vs[4][1] = t * 0.03f, vs[4][2] = t * 0.01f;                     // g_UVOffset [I]
        vs[5][0] = vs[5][1] = vs[5][2] = 0.25f;                                               // g_UVScale [I]
        vs[63][0] = -1;
        // g_SpotlightInfo: x the inner core's share of the radius, y 1 [I];
        // g_ConeColor: the volume colour x density x 0.25 (without the
        // engine's smoke texture the beams are otherwise solid) [I].
        ps[0][0] = h.endRadius > 0 ? std::clamp(h.innerRadius / h.endRadius, 0.05f, 1.0f) : 1.0f;
        ps[0][1] = 1.0f;
        for (int k = 0; k < 3; ++k) ps[1][k] = h.volumeColor[size_t(k)] * h.density * 0.25f;
        ps[1][3] = 1;
        ps[63][0] = -1;
        c.uniforms[1] = d.stageUniforms(vs, sizeof vs);
        c.uniforms[2] = d.stageUniforms(ps, sizeof ps);
        d.draw(c);
    }
}
