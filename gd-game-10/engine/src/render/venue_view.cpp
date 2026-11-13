// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_view.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "data/disc.h"
#include "data/qbkey.h"
#include "render/camera.h"
#include "render/hitch.h"
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
    if (rayVb_) dev_->destroyBuffer(rayVb_);
    if (rayIb_) dev_->destroyBuffer(rayIb_);
    if (projVb_) dev_->destroyBuffer(projVb_);
    if (projIb_) dev_->destroyBuffer(projIb_);
    if (coneIb_) dev_->destroyBuffer(coneIb_);
    if (smoke_) dev_->destroyTexture(smoke_);
    smoke_ = {};
    if (lvFalloff_) dev_->destroyTexture(lvFalloff_);
    if (lvPattern_) dev_->destroyTexture(lvPattern_);
    if (vol2dVb_) dev_->destroyBuffer(vol2dVb_);
    if (vol2dIb_) dev_->destroyBuffer(vol2dIb_);
    lvFalloff_ = {}, lvPattern_ = {}, vol2dVb_ = {}, vol2dIb_ = {};
    for (auto& [k, f] : flares_) {
        if (f.a) dev_->destroyQuery(f.a);
        if (f.b) dev_->destroyQuery(f.b);
    }
    flares_.clear();
    program_ = {}, vb_ = {}, ib_ = {}, coneVb_ = {}, coneIb_ = {}, rayVb_ = {}, rayIb_ = {}, projVb_ = {}, projIb_ = {};
    rayCount_ = 0;
    cones_.clear();
    rays_.clear();
    projectors_.clear();
    vol2d_.clear();
    gh3_.shutdown();
    mats_.clear();
    for (auto& [k, t] : textures_) dev_->destroyTexture(t.id);
    textures_.clear();
    items_.clear();
    groups_.clear();
    groupsMore_.clear();
    dev_ = nullptr;
}

// The CPU side of init (any thread): the zone's textures decoded, the light
// housings' models read. initStep uploads them a slice a frame (no-stutter
// rule: a venue's ~1 s of decodes, uploads and compiles had held one frame).
struct VenueView::Prepared {
    std::vector<gh3tex::Texture> textures;  // the zone's, global_ad_tex's, then the housings' (upload order)
    std::unordered_map<uint32_t, std::unique_ptr<scene::Scene>> models;  // the housings' models by path key
    std::vector<gh3tex::Texture> smoke;     // global_gfx.tex's 0xa500318e
    std::vector<gh3tex::Texture> lv2d;      // sys_lightvolume2d's falloff and pattern (global_gfx.tex)
    size_t zoneTextures = 0;                // of `textures`, the zone's and global_ad_tex's
    std::string error;
    int step = 0;
    size_t next = 0;  // the step's next texture / program
    std::vector<std::pair<uint32_t, matlib::Technique>> programs;
    // build's progress: the next part and sector, the phase.
    size_t part = 0, sector = 0;
    int phase = 0;  // 0 the sectors, 1 the housings, 2 the vertex buffer, 3 the index buffer
    size_t uploaded = 0;  // bytes of the buffer being filled
    std::vector<Vtx> verts;
    std::vector<uint32_t> idx;
};

namespace {

// sys_lightvolume2d (global_gfx.scn): template _LightVolume2D, blend 1
// (SRC_ALPHA + ONE), no depth write (+0xcc 0x20); textures s0 0x94191135 (the
// falloff), s1 0x49263059 (the pattern), s2 0xa500318e (the smoke) [C data].
constexpr uint32_t kLvFalloff = 0x94191135u, kLvPattern = 0x49263059u;

// A 2D light volume's template: _LightVolume2D + _smoke0 (smokeeffect false)
// + _softedge1 (volume2dsoftedge) + _pattern0 (no volume2dpatterntexture), in
// the library's name order [C data: the template names; I: which node field
// picks which part, 824496D0 does not decompile]. Empty: not a 2D volume.
std::string lightVolume2DTemplate(const venue::Node& n) {
    if (!n.st) return {};
    const qb::Value* lt = n.st->find(K("lightvolumetype"));
    if (!lt) return {};
    const bool soft = lt->key == K("volume2dsoftedge"), plain = lt->key == K("volume2d");
    // volume3d (_LightVolume3D's ray-marched cone) drawn as a soft 2D volume [I: approximation].
    const bool v3 = lt->key == K("volume3d");
    if (!soft && !plain && !v3) return {};
    std::string name = "_LightVolume2D";
    const qb::Value* smoke = n.st->find(K("smokeeffect"));
    if (smoke && smoke->key == K("false")) name += "_smoke0";
    if (soft || v3) name += "_softedge1";
    const qb::Value* pat = n.st->find(K("volume2dpatterntexture"));
    if (!pat || pat->key == 0 || pat->key == K("none")) name += "_pattern0";
    return name;
}

// A light housing's model path key (lightvolumetype nodes with a Model), or 0.
uint32_t housingModelKey(const venue::Node& n) {
    if (!n.st || !n.st->find(K("lightvolumetype"))) return 0;
    const qb::Value* mv = n.st->find(K("Model"));
    if (!mv || (mv->type != qb::Type::String && mv->type != qb::Type::WString)) return 0;
    std::string path = "models\\";
    for (char c : mv->str)
        if (!(c == '\\' && path.back() == '\\')) path += c == '/' ? '\\' : c;
    return qbkey::key(path);
}

}  // namespace

void VenueView::prepare(const Disc& disc, const venue::Venue& v) {
    auto pr = std::make_unique<Prepared>();
    // The zone's textures (every .tex dictionary in its pak), with their
    // class, and the global ad textures some venue signs use [C data:
    // global_ad_tex.pak holds the ad_* sectors' textures].
    for (const std::string& pakPath : {"DATA/COMPRESSED/ZONES/" + v.zone + ".pak.xen", std::string("DATA/COMPRESSED/PAK/global_ad_tex.pak.xen")}) {
        if (!disc.exists(pakPath)) continue;
        try {
            const auto pakPtr = Pak::shared(disc, pakPath);
            const Pak& pak = *pakPtr;
            for (const PakEntry& e : pak.entries()) {
                if (e.type != K(".tex")) continue;
                auto [p, size] = pak.data(e);
                for (gh3tex::Texture& t : gh3tex::decodeTexDict(p, size))
                    if (t.supported && !t.levels.empty()) pr->textures.push_back(std::move(t));
            }
        } catch (const std::exception& ex) {
            pr->error = pakPath + ": " + ex.what();
            prep_ = std::move(pr);
            return;
        }
    }
    pr->zoneTextures = pr->textures.size();
    // The light housings' models (LightHousings/GO_LightHousing02/
    // GO_LightHousing02.mdl, in ZONES/global.pak with its .tex) [C data].
    std::shared_ptr<const Pak> global;
    for (const venue::Node& n : v.nodes) {
        const uint32_t key = housingModelKey(n);
        if (!key || pr->models.count(key)) continue;
        std::unique_ptr<scene::Scene> sc;
        try {
            if (!global) global = Pak::shared(disc, "DATA/COMPRESSED/ZONES/global.pak.xen");
            if (const PakEntry* e = global->findType(key, K(".mdl"))) {
                auto [p, size] = global->data(*e);
                sc = std::make_unique<scene::Scene>(scene::parse(p, size, scene::Kind::Mdl));
                if (e->parent)
                    if (const PakEntry* te = global->findType(e->parent, K(".tex"))) {
                        auto [tp, ts] = global->data(*te);
                        for (gh3tex::Texture& t : gh3tex::decodeTexDict(tp, ts))
                            if (t.supported && !t.levels.empty()) pr->textures.push_back(std::move(t));
                    }
            }
        } catch (const std::exception& ex) {
            SDL_Log("venue: %s: %s", qbkey::name(key).c_str(), ex.what());
        }
        pr->models.emplace(key, std::move(sc));
    }
    // The beams' smoke (build).
    try {
        const auto gpPtr = Pak::shared(disc, "DATA/COMPRESSED/ZONES/global.pak.xen");
        if (const PakEntry* te = gpPtr->findType(K("global_gfx"), K(".tex"))) {
            auto [tp, ts] = gpPtr->data(*te);
            for (gh3tex::Texture& t : gh3tex::decodeTexDict(tp, ts))
                if (t.key == 0xa500318eu && t.supported && !t.levels.empty()) pr->smoke.push_back(std::move(t));
                else if ((t.key == kLvFalloff || t.key == kLvPattern) && t.supported && !t.levels.empty()) pr->lv2d.push_back(std::move(t));
        }
    } catch (const std::exception& ex) {
        SDL_Log("venue: the beams' smoke: %s", ex.what());
    }
    prep_ = std::move(pr);
}

bool VenueView::init(gfx::RenderDevice& dev, const Disc& disc, const venue::Venue& v, const std::vector<uint32_t>& hide, std::string& error) {
    if (prep_ && prep_->step != 0) prep_.reset();  // a part-done init starts again (a prepared one is used)
    for (;;) {
        const InitStep s = initStep(dev, disc, v, hide, error);
        if (s != InitStep::More) return s == InitStep::Done;
    }
}

VenueView::InitStep VenueView::initStep(gfx::RenderDevice& dev, const Disc& disc, const venue::Venue& v, const std::vector<uint32_t>& hide,
                                        std::string& error) {
    if (!prep_ || prep_->step == 0) {
        hitch::Scope hs("venue prepare");
        if (!prep_) prepare(disc, v);  // (not prepared on a worker: now)
        shutdown();
        dev_ = &dev;
        v_ = &v;
        if (!prep_->error.empty()) {
            error = prep_->error;
            prep_.reset();
            return InitStep::Failed;
        }
        program_ = dev.beginProgram(programDesc(), error);  // ready by step 3 (below)
        if (!program_) {
            prep_.reset();
            return InitStep::Failed;
        }
        // GH3's own shaders (GD10_GH3_SHADERS=0: the approximation only, a development aid).
        if (!(SDL_getenv("GD10_GH3_SHADERS") && SDL_getenv("GD10_GH3_SHADERS")[0] == '0')) gh3_.init(dev, gh3shader::library(disc));
        stats_ = Stats{};
        prep_->step = 1, prep_->next = 0;
        zoneTextureCount_ = 0;
        return InitStep::More;
    }
    Prepared& pr = *prep_;
    if (pr.step == 1) {
        // The textures, a slice a frame (the first of a call always).
        hitch::Scope hs("venue textures");
        for (; pr.next < pr.textures.size() && hitch::slice(); ++pr.next) {
            if (pr.next == pr.zoneTextures) zoneTextureCount_ = textures_.size();
            const gh3tex::Texture& t = pr.textures[pr.next];
            if (textures_.count(t.key)) continue;
            std::string err;
            auto levels = t.texLevels();
            const gfx::TextureId id = dev.createTexture(t.desc(), levels.data(), err);
            if (id) textures_[t.key] = Tex{id, t.usage, t.kind == gfx::TexKind::Cube};
        }
        if (pr.next < pr.textures.size()) return InitStep::More;
        if (pr.zoneTextures == pr.textures.size()) zoneTextureCount_ = textures_.size();
        pr.textures.clear(), pr.textures.shrink_to_fit();
        // The programs the meshes draw with (their template's Default; an
        // opaque one's DynamicShadowReciever too), compiled next.
        auto want = [&](const scene::Scene& sc) {
            for (const scene::Mesh& m : sc.meshes)
                for (const scene::Material& mm : sc.materials)
                    if (mm.key == m.material) {
                        pr.programs.emplace_back(mm.shader, matlib::Default);
                        if (mm.d8 == 0) pr.programs.emplace_back(mm.shader, matlib::DynamicShadowReceiver);
                        break;
                    }
        };
        if (gh3_.ready()) {
            for (const venue::Part& part : v.parts) want(part.scene);
            for (const auto& [k, sc] : pr.models)
                if (sc) want(*sc);
            for (const venue::Node& n : v.nodes)
                if (const std::string t = lightVolume2DTemplate(n); !t.empty()) pr.programs.emplace_back(K(t.c_str()), matlib::Default);
        }
        std::sort(pr.programs.begin(), pr.programs.end());
        pr.programs.erase(std::unique(pr.programs.begin(), pr.programs.end()), pr.programs.end());
        pr.step = 2, pr.next = 0;
        return InitStep::More;
    }
    if (pr.step == 2) {
        // The programs, compiled before the first frame that draws them: begun
        // a slice a frame (OpenGL compiles them in the driver's threads, the
        // others now), then waited for frame by frame.
        hitch::Scope hs("venue programs");
        for (; pr.next < pr.programs.size() && hitch::slice(); ++pr.next)
            gh3_.request(pr.programs[pr.next].first, pr.programs[pr.next].second);
        if (pr.next < pr.programs.size() || !gh3_.settle()) return InitStep::More;
        const int ps = dev.programStatus(program_, error);  // the approximation's
        if (ps == 0) return InitStep::More;
        if (ps < 0) {
            program_ = {};
            prep_.reset();
            return InitStep::Failed;
        }
        pr.step = 3;
        return InitStep::More;
    }
    hitch::Scope hs("venue build");
    const InitStep r = build(dev, v, hide, error);
    if (r != InitStep::More) prep_.reset();
    return r;
}

// The geometry: the sectors a slice a frame, then the housings and the
// vertex buffer, then the index buffer (each ~15..40 ms for z_tushino).
VenueView::InitStep VenueView::build(gfx::RenderDevice& dev, const venue::Venue& v, const std::vector<uint32_t>& hide, std::string& error) {
    Prepared& pr = *prep_;
    stats_.textures = zoneTextureCount_;
    auto tex = [&](uint32_t key) {
        auto it = textures_.find(key);
        return it == textures_.end() ? nullptr : &it->second;
    };

    std::vector<Vtx>& verts = pr.verts;
    std::vector<uint32_t>& idx = pr.idx;
    // An object's light groups (823C9FD8 [C]): `lightgroup`, then
    // lightgroup2, lightgroup3, ... while the node has them (at most 32);
    // `empty` names no group [I: no light carries it].
    auto groupOf = [&](const venue::Node* node) {
        uint32_t key = node ? node->lightGroup : 0;
        if (!key) key = K("none");
        std::array<uint32_t, 3> more{};
        if (node && node->st)
            for (int k = 0; k < 3; ++k) {
                const qb::Value* g = node->st->find(qbkey::key("lightgroup" + std::to_string(k + 2)));
                if (!g || (g->type != qb::Type::Key && g->type != qb::Type::KeyPtr)) break;
                more[size_t(k)] = g->key == K("empty") ? 0 : g->key;
            }
        for (size_t i = 0; i < groups_.size(); ++i)
            if (groups_[i] == key && groupsMore_[i] == more) return int(i);
        groups_.push_back(key);
        groupsMore_.push_back(more);
        return int(groups_.size() - 1);
    };
    if (groups_.empty()) groupOf(nullptr);
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
        it.group = groupOf(node);
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
            if (!it.diffuse && SDL_getenv("GD10_VENUE_DEBUG")) {
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
            it.stencil = (mat->cc & 0x80) ? -1 : int8_t(mat->dc & 1);
            // 823DC180: the blend from +0xd8 (0 opaque, 1 SRC_ALPHA / ONE, 2 its
            // reverse subtract, 3 SRC_ALPHA / ONE_MINUS_SRC_ALPHA, 7 ONE / ONE);
            // +0xcc: 8 cull none, 0x20 no z write, 0x40 no alpha test (else
            // the reference +0xe8) [C].
            switch (mat->d8) {
            case 0: it.mode = Mode::Opaque; break;
            case 1: it.mode = Mode::Add; break;
            case 2: it.mode = Mode::Subtract; break;
            case 7: it.mode = Mode::AddOne; break;
            default: it.mode = Mode::Blend; break;
            }
            it.twoSided = (mat->cc & 8) != 0;
            it.noDepthWrite = (mat->cc & 0x20) != 0;
            it.blendMode = mat->d8, it.matFlags = mat->cc;
            if (mat->cc & 0x40) it.alphaRef = 0;
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
                    // The per-slot address bytes from +0xea: u and v both clamped -> clamp
                    // (one axis alone stays wrapped [I: one wrap mode a sampler here]).
                    for (int k = 0; k < 4; ++k) {
                        const uint32_t word = k < 2 ? mat->e8 : mat->ec;
                        const int shift = k < 2 ? 8 - 8 * k : 24 - 8 * (k - 2);
                        if (((word >> shift) & 3) == 3) mm.clamp |= uint16_t(1 << k);
                    }
                    for (int k = 0; k < 16; ++k)
                        if ((gp->samplers >> k & 1) && !mm.tex[k]) mm.tex[k] = (gp->cubes >> k & 1) ? gh3_.whiteCube() : gh3_.white();
                    it.gh3 = gp;
                    // The dynamic shadow's receiver variant compiled now, not
                    // in the first frame that draws it (no compile stutter).
                    if (it.mode == Mode::Opaque) (void)gh3_.get(mat->shader, matlib::DynamicShadowReceiver);
                    it.mat = int(mats_.size());
                    mats_.push_back(mm);
                }
            }
        }
        if (it.gh3) ++stats_.gh3Meshes;
        else ++stats_.fallbackMeshes;
        // Development aids: GD10_VENUE_MESHLOG=1 logs each mesh's template and
        // blend; GD10_VENUE_SKIP=<template>,... leaves those templates out.
        if (mat && gh3_.library()) {
            const matlib::Template* tp = gh3_.library()->find(mat->shader);
            const std::string tn = tp ? tp->name : std::string("?");
            if (SDL_getenv("GD10_VENUE_MESHLOG"))
                SDL_Log("venue mesh: %s blend %u flags 0x%x tris %u textures %u%s", tn.c_str(), mat->d8, mat->cc, it.count / 3,
                        unsigned(mat->textures.size()), it.gh3 ? "" : " (approximated)");
            if (const char* skip = SDL_getenv("GD10_VENUE_SKIP"); skip && ("," + std::string(skip) + ",").find("," + tn + ",") != std::string::npos)
                return;
        }
        items_.push_back(it);
    };
    if (pr.phase >= 2) {
        // The vertex, then the index buffer, 4 MB at a time (z_tushino's
        // 50 MB of vertices had held a frame 40 ms).
        constexpr size_t kChunk = 4u << 20;
        const bool vtx = pr.phase == 2;
        const uint8_t* src = vtx ? reinterpret_cast<const uint8_t*>(verts.data()) : reinterpret_cast<const uint8_t*>(idx.data());
        const size_t total = vtx ? verts.size() * sizeof(Vtx) : idx.size() * sizeof(uint32_t);
        while (pr.uploaded < total && hitch::slice()) {
            const size_t n = std::min(kChunk, total - pr.uploaded);
            dev.updateBuffer(vtx ? vb_ : ib_, pr.uploaded, src + pr.uploaded, n);
            pr.uploaded += n;
        }
        if (pr.uploaded < total) return InitStep::More;
        pr.uploaded = 0;
        if (vtx) {
            verts.clear(), verts.shrink_to_fit();
            ib_ = dev.createBuffer(gfx::BufferKind::Index, idx.size() * sizeof(uint32_t), nullptr, false);
            pr.phase = 3;
            return InitStep::More;
        }
        // The items in draw order.
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
        return InitStep::Done;
    }
    for (; pr.part < v.parts.size(); ++pr.part, pr.sector = 0) {
        const venue::Part& part = v.parts[pr.part];
        const scene::Scene& sc = part.scene;
        for (; pr.sector < sc.sectors.size(); ++pr.sector) {
            if (!hitch::slice()) return InitStep::More;  // more sectors next frame
            const size_t si = pr.sector;
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
    if (pr.phase == 0) {
        pr.phase = 1;
        if (!hitch::slice()) return InitStep::More;  // the housings and the vertex buffer next frame
    }
    // The light housings: game objects of the _gfx node array with a
    // lightvolumetype and a Model (LightHousings/GO_LightHousing02/
    // GO_LightHousing02.mdl, in ZONES/global.pak with its .tex) [C data].
    {
        std::unordered_map<uint32_t, std::unique_ptr<scene::Scene>>& models = pr.models;
        for (size_t ni = 0; ni < v.nodes.size(); ++ni) {
            const venue::Node& n = v.nodes[ni];
            const uint32_t key = housingModelKey(n);
            if (!key) continue;
            auto it = models.find(key);
            if (it == models.end()) continue;
            // ConeImpostor's beam; `volume`: GH3's _Raytrace_cone_volume (drawRaytraced).
            const qb::Value* lt = n.st->find(K("lightvolumetype"));
            const qb::Value* pt = n.st->find(K("projectortype"));
            const bool everything = pt && pt->key == K("everything");
            if (lt && lt->key == K("ConeImpostor")) cones_.push_back(int(ni));
            if (!lightVolume2DTemplate(n).empty()) vol2d_.push_back(int(ni));
            // Raytraced: the `volume` beams, and the `everything` projectors (light on
            // whatever the cone meets, through the depth buffer).
            if ((lt && lt->key == K("volume")) || everything) rays_.push_back(int(ni));
            if (pt && pt->key == K("stageonly")) projectors_.push_back(int(ni));
            if (!it->second) continue;
            ++stats_.housings;
            for (const scene::Mesh& m : it->second->meshes) addMesh(*it->second, m, nullptr, int(ni), &n, n.name, int(ni));
        }
    }
    // The beams' smoke: global_gfx.tex's 0xa500318e, a 128 x 128 grey noise
    // (DXT1; sys_coneimpostor's texture, Function_823B49D0) [C]. The
    // dictionary's pak entry is named by its short name, global_gfx: looked up
    // by the full path it was never found and the beams drew with white for
    // their smoke, far brighter and without the noise.
    if ((!cones_.empty() || !rays_.empty()) && !smoke_) {
        for (const gh3tex::Texture& t : pr.smoke) {
            std::string err;
            auto levels = t.texLevels();
            smoke_ = dev.createTexture(t.desc(), levels.data(), err);
        }
        if (!smoke_) SDL_Log("venue: the beams' smoke (global_gfx.tex 0xa500318e) not found; drawn with white");
    }
    if (!vol2d_.empty() && !smoke_)
        for (const gh3tex::Texture& t : pr.smoke) {
            std::string err;
            auto levels = t.texLevels();
            smoke_ = dev.createTexture(t.desc(), levels.data(), err);
        }
    if (!vol2d_.empty()) {
        for (const gh3tex::Texture& t : pr.lv2d) {
            std::string err;
            auto levels = t.texLevels();
            (t.key == kLvFalloff ? lvFalloff_ : lvPattern_) = dev.createTexture(t.desc(), levels.data(), err);
        }
        if (!lvFalloff_ || !lvPattern_) SDL_Log("venue: sys_lightvolume2d's falloff / pattern textures not found; drawn with white");
        // A trapezoid per volume (position, TEXCOORD0 xyz), rewritten every frame.
        std::vector<float> vv(vol2d_.size() * 4 * 7, 0.0f);
        std::vector<uint32_t> vi;
        for (uint32_t q = 0; q < vol2d_.size(); ++q)
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) vi.push_back(q * 4 + k);
        vol2dVb_ = dev.createBuffer(gfx::BufferKind::Vertex, vv.size() * sizeof(float), vv.data(), true);
        vol2dIb_ = dev.createBuffer(gfx::BufferKind::Index, vi.size() * sizeof(uint32_t), vi.data(), false);
    }
    if (verts.empty()) {
        error = v.zone + ": no geometry";
        return InitStep::Failed;
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
    // The raytraced cone's mesh: 20 segments (Function_823B4368's count) [C];
    // a vertex is (cx, cy, t): the VS reads it .zxy as (t, cx, cy), radius
    // start + (end - start) t, z = t x range [C: the VS]. Closed with its
    // caps, wound counter-clockwise outwards [I: GH3's own buffers not read].
    if (!rays_.empty()) {
        constexpr int kSeg = 20;
        std::vector<float> rv;
        std::vector<uint32_t> ri;
        for (int ring = 0; ring < 2; ++ring)
            for (int i = 0; i < kSeg; ++i) {
                const float a = 2.0f * float(M_PI) * float(i) / kSeg;
                rv.insert(rv.end(), {std::cos(a), std::sin(a), float(ring), 1});
            }
        rv.insert(rv.end(), {0, 0, 0, 1, 0, 0, 1, 1});  // the caps' centres: 2 kSeg, 2 kSeg + 1
        const uint32_t cn = 2 * kSeg, cf = 2 * kSeg + 1;
        for (uint32_t i = 0; i < kSeg; ++i) {
            const uint32_t j = (i + 1) % kSeg, ni = i, nj = j, fi = kSeg + i, fj = kSeg + j;
            ri.insert(ri.end(), {ni, nj, fi, nj, fj, fi, cf, fi, fj, cn, nj, ni});
        }
        rayVb_ = dev.createBuffer(gfx::BufferKind::Vertex, rv.size() * sizeof(float), rv.data(), false);
        rayIb_ = dev.createBuffer(gfx::BufferKind::Index, ri.size() * sizeof(uint32_t), ri.data(), false);
        rayCount_ = uint32_t(ri.size());
    }
    // The projectors' quads: 4 vertices each (position, TEXCOORD0), rewritten every frame.
    if (!projectors_.empty()) {
        std::vector<float> pv(projectors_.size() * 4 * 6, 0.0f);
        std::vector<uint32_t> pi;
        for (uint32_t q = 0; q < projectors_.size(); ++q)
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) pi.push_back(q * 4 + k);
        projVb_ = dev.createBuffer(gfx::BufferKind::Vertex, pv.size() * sizeof(float), pv.data(), true);
        projIb_ = dev.createBuffer(gfx::BufferKind::Index, pi.size() * sizeof(uint32_t), pi.data(), false);
    }
    vb_ = dev.createBuffer(gfx::BufferKind::Vertex, verts.size() * sizeof(Vtx), nullptr, false);
    pr.phase = 2;  // its contents and the index buffer from the next frame
    return InitStep::More;
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
    proj.at(0, 2) += jitterX + lensShiftX, proj.at(1, 2) += jitterY;  // Enhanced TAA's sub-pixel jitter, the lens shift (0 otherwise)
    const gfx::Mat4 vp = proj * view;
    g = gh3shader::Globals{};
    g.setMatrix(gh3shader::kRegView, view, 4);
    g.setMatrix(gh3shader::kRegProjection, proj, 4);
    g.setMatrix(gh3shader::kRegInvView, inv, 4);
    g.setMatrix(gh3shader::kRegViewProj, vp, 4);
    g.setMatrix(gh3shader::kRegTextureProjection0, gfx::Mat4::identity(), 4);
    g.set(gh3shader::kRegInvProj, 1.0f / sx, 1.0f / sy, 0, 0);
    const float t = float(seconds);
    g.set(gh3shader::kRegTime, t, t, t, t);
    g.set(gh3shader::kRegViewport, gfx::kLogicalW, gfx::kLogicalH, 1.0f / gfx::kLogicalW, 1.0f / gfx::kLogicalH);
    // g_distanceLODScaler: max(x, w * (y - distance)) scales the normal and
    // specular detail; x = 1 keeps the full detail [I].
    g.set(gh3shader::kRegDistanceLodScaler, 1, 0, 0, 0);
    g.set(gh3shader::kRegShadowMapSize, 1024, 1024, 1.0f / 1024, 1.0f / 1024);
    g.set(gh3shader::kRegShadowLevel, 0, 0, 0, 0);
    g.set(gh3shader::kRegShadowMapConstants, 1, 0, 0, 0);
    if (viewProjOut) *viewProjOut = vp;
    if (viewOut) *viewOut = view;
}

void VenueView::lightAt(const std::vector<venue::LightState>& lights, const venue::LightShow& show, const venue::Vec3& at, float rgb[3]) const {
    if (!v_) return;
    std::vector<gh3shader::LightIn> in;
    for (size_t i = 0; i < v_->lights.size() && i < lights.size(); ++i) {
        const venue::Light& l = v_->lights[i];
        if (!l.createdAtStart && !(l.createdOn && show.flag(l.createdOn))) continue;
        in.push_back(gh3shader::lightIn(l, lights[i], v_));
    }
    if (in.empty()) return;
    gh3shader::ObjectConstants oc;
    const gfx::Vec3 p{at[0], at[1], at[2]};
    gh3shader::lightObject(in.data(), in.size(), p, oc);
    const gfx::Vec3 c = gh3shader::spriteLight(oc, p);
    rgb[0] = c.x, rgb[1] = c.y, rgb[2] = c.z;
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
    gfx::Mat4 proj = gfx::Mat4::perspective(yfov, aspect, 0.05f, 4000.0f, d.info().clipDepthZeroToOne);
    proj.at(0, 2) -= jitterX + lensShiftX, proj.at(1, 2) -= jitterY;  // GL's -z forward: the same NDC shift
    const gfx::Mat4 vp = proj * view;

    FrameUniforms fu{};
    std::memcpy(fu.viewProj, vp.m, sizeof fu.viewProj);
    fu.eye[0] = cam.pos[0], fu.eye[1] = cam.pos[1], fu.eye[2] = cam.pos[2];
    static const float vcScale = SDL_getenv("GD10_VENUE_VC") ? float(std::atof(SDL_getenv("GD10_VENUE_VC"))) : 1.0f;
    static const float dynScale = SDL_getenv("GD10_VENUE_DYN") ? float(std::atof(SDL_getenv("GD10_VENUE_DYN"))) : 0.6f;
    fu.params[0] = vcScale, fu.params[1] = dynScale;
    const gfx::UniformData frame = d.stageUniforms(&fu, sizeof fu);

    // Per light group: the group's created, non-baked lights [I: "_VC_" lights only bake vertex colours].
    std::vector<gfx::UniformData> groupLights(groups_.size());
    std::vector<std::vector<gh3shader::LightIn>> groupIn(groups_.size());
    for (size_t gi = 0; gi < groups_.size(); ++gi) {
        // GH3's light manager: every created light whose group serves this one [C 822BB680].
        for (size_t i = 0; i < v_->lights.size() && i < lights.size(); ++i) {
            const venue::Light& l = v_->lights[i];
            bool serves = gh3shader::groupLights(l.group, groups_[gi]);
            for (uint32_t g : groupsMore_[gi]) serves = serves || (g && l.group == g);
            if (!serves) continue;
            if (!l.createdAtStart && !(l.createdOn && show.flag(l.createdOn))) continue;
            groupIn[gi].push_back(gh3shader::lightIn(l, lights[i], v_));
        }
        LightUniforms lu{};
        int n = 0;
        for (size_t i = 0; i < v_->lights.size() && i < lights.size(); ++i) {
            const venue::Light& l = v_->lights[i];
            const uint32_t grp = l.group ? l.group : K("none");
            if (grp != groups_[gi] || l.vertexBake) continue;
            if (!l.createdAtStart && !(l.createdOn && show.flag(l.createdOn))) continue;
            const venue::LightState& s = lights[i];
            if (s.intensity <= 0) continue;
            if (l.type == venue::LightType::Prt) continue;  // its SH terms only reach GH3's shaders
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
    // GD10_LIGHTS_LOG=<ms> (development aid): every light's live values and
    // each group's lights, once, at the first frame past <ms>.
    if (static const char* ll = SDL_getenv("GD10_LIGHTS_LOG"); ll && seconds_ * 1000.0 >= std::atof(ll)) {
        static bool done = false;
        if (!done) {
            done = true;
            static const char* kType[] = {"amb", "dir", "point", "spot", "prt"};
            std::printf("lights at %.0f ms (snapshot %s):\n", seconds_ * 1000.0, qbkey::names().get(show.snapshot()).c_str());
            for (size_t i = 0; i < v_->lights.size() && i < lights.size(); ++i) {
                const venue::Light& l = v_->lights[i];
                const venue::LightState& s = lights[i];
                const bool on = l.createdAtStart || (l.createdOn && show.flag(l.createdOn));
                std::printf("  %-44s %-5s grp %-10s %s i %.3f rgb %.2f %.2f %.2f att %.1f..%.1f dyn %d\n", qbkey::names().get(l.name).c_str(),
                            kType[int(l.type) < 5 ? int(l.type) : 0], qbkey::names().get(l.group).c_str(), on ? "on " : "off", s.intensity,
                            s.color[0], s.color[1], s.color[2], l.attenStart, l.attenEnd, int(l.dynamic));
            }
            for (size_t gi = 0; gi < groups_.size(); ++gi) std::printf("  group %s: %zu lights\n", qbkey::names().get(groups_[gi]).c_str(), groupIn[gi].size());
        }
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
            const int vis = visible_ ? visible_(n.name) : -1;
            if (vis == 0) continue;
            if (vis < 0 && !n.createdAtStart && !(n.createdOn && show.flag(n.createdOn))) continue;
        }
        // A crowd card without its picture (a CrowdOff shot, the peds not
        // drawn yet) is not drawn: no texture would draw it black.
        if (it.card) {
            auto ov = overrides_.find(it.card);
            if (ov == overrides_.end() || !ov->second) continue;
        }
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
    // The dynamic shadow's cascades (gh3shader::DynamicShadow); g_shadowLevel
    // z, w: light = i + lit x (1 - i), i = SetDynamicShadowIntensity 0.35 [C
    // script tod_manager_create_perm_lights; I: the register's use, the
    // shadows as dark as the reference captures'].
    gh3shader::setShadowGlobals(globals, shadow_, viewM);
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
            if (shadow_ && it->mode == Mode::Opaque && it->housing < 0 && !it->card) {
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
            } else if (gfx::Mat4 mw; it->node >= 0 && motion_ && motion_(v_->nodes[size_t(it->node)].name, mw)) {
                gh3shader::ObjectConstants ho = obj;
                ho.setMatrix(gh3shader::kRegWorld, mw, 4);
                ho.setMatrix(gh3shader::kRegWorldView, viewM * mw, 4);
                ho.setMatrix(gh3shader::kRegWorldViewProj, viewProjM * mw, 4);
                const gfx::Vec3 mc = mw.transformPoint({cc[0], cc[1], cc[2]});
                gh3shader::lightObject(gl.data(), gl.size(), mc, ho);
                gc.uniforms[3] = d.stageUniforms(&ho, sizeof ho);
            } else {
                gh3shader::lightObject(gl.data(), gl.size(), {cc[0], cc[1], cc[2]}, obj);
                gc.uniforms[3] = {&obj, sizeof obj, 0};
            }
            for (int k = 0; k < 16; ++k) gc.textures[k] = mats_[mi].tex[k];
            for (int k = 0; k < 14; ++k)
                gc.samplers[k] = {gfx::Filter::Trilinear, (mats_[mi].clamp >> k & 1) ? gfx::Wrap::Clamp : gfx::Wrap::Repeat, 0};
            if (rcv) {
                for (int k = 0; k < 14; ++k)
                    if ((rcv->samplers >> k & 1) && !gc.textures[k]) gc.textures[k] = gh3_.white();
                gc.textures[14] = shadow_.tex[0], gc.textures[15] = shadow_.tex[1];  // g_sampShadowMap0 / 1
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
                // GD10_CULL=0 (development aid) draws both sides.
                static const bool noCull = SDL_getenv("GD10_CULL") && SDL_getenv("GD10_CULL")[0] == '0';
                gc.state.cull = noCull || it->twoSided ? gfx::Cull::None : gfx::Cull::CounterClockwise;
            }
            gc.state.depthTest = true;
            gc.state.depthFunc = gfx::CompareFunc::LessEqual;
            // The material's stencil: ALWAYS, REPLACE with +0xdc where the depth
            // test passes (the bloom's material filter reads it) [C 823DC180].
            gc.state.stencilTest = it->stencil >= 0;
            gc.state.stencilFunc = gfx::CompareFunc::Always;
            gc.state.stencilRef = uint8_t(std::max<int>(0, it->stencil));
            gc.state.stencilWriteMask = it->stencil >= 0 ? 0xFF : 0;
            gc.state.stencilReplace = it->stencil >= 0;
            {
                // GH3's state for the material (blend mode, depth write / test) [C 823DC180].
                gfx::PipelineState ms = gc.state;
                gh3shader::materialState(it->blendMode, it->matFlags, ms);
                gc.state.blend = ms.blend, gc.state.src = ms.src, gc.state.dst = ms.dst, gc.state.op = ms.op;
                gc.state.depthWrite = ms.depthWrite, gc.state.depthFunc = ms.depthFunc;
            }
            if (it->flare && it->housing >= 0) {
                drawFlare(*it, gc, centreOf(it), cam);
                ++draws;
                continue;
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
        {
            gfx::PipelineState ms = c.state;
            gh3shader::materialState(it->blendMode, it->matFlags, ms);
            c.state.blend = ms.blend, c.state.src = ms.src, c.state.dst = ms.dst, c.state.op = ms.op;
            c.state.depthWrite = ms.depthWrite, c.state.depthFunc = ms.depthFunc;
        }
        d.draw(c);
        ++draws;
    }
    stats_.drawsLastFrame = draws;
}

void VenueView::drawFlare(const Item& it, gfx::DrawCall gc, const venue::Vec3& centre, const venue::CamPose& cam) {
    // 823C0698 [C]: two occlusion queries a flare, read a frame later: A
    // counts every pixel of a small quad (m_vsOcclusionPass = 1: the
    // m_vsOcclusionSizeScale share of the flare) drawn with ZFUNC ALWAYS, B
    // the depth-tested ones; the flare is drawn without depth test, its size
    // m_vsSizeScale = B / A (A = 0: not drawn); colour writes off for the
    // queries; hidden nearer than NDC z 0.91, faded in up to 0.93.
    gfx::RenderDevice& d = *dev_;
    Flare& f = flares_[&it];
    if (!f.a) f.a = d.createQuery(), f.b = d.createQuery();
    uint64_t na = 0, nb = 0;
    if (f.issued && d.queryResult(f.a, na) && d.queryResult(f.b, nb)) {
        f.scale = na ? float(double(nb) / double(na)) : 0.0f;
        f.issued = false;
    }
    const size_t mi = size_t(it.mat);
    float vs[64][4];
    std::memcpy(vs, mats_[mi].consts.vs, sizeof vs);
    if (!f.issued && f.a && f.b) {
        vs[0][0] = vs[0][1] = vs[0][2] = vs[0][3] = 1;  // m_vsSizeScale
        vs[1][0] = vs[1][1] = vs[1][2] = vs[1][3] = 1;  // m_vsOcclusionPass
        gfx::DrawCall q = gc;
        q.uniforms[1] = d.stageUniforms(vs, sizeof vs);
        q.state.blend = false, q.state.colorWrite = false, q.state.alphaWrite = false, q.state.depthWrite = false;
        q.state.stencilTest = false, q.state.depthTest = true;
        q.state.depthFunc = gfx::CompareFunc::Always;
        d.beginQuery(f.a);
        d.draw(q);
        d.endQuery();
        q.state.depthFunc = gfx::CompareFunc::LessEqual;
        d.beginQuery(f.b);
        d.draw(q);
        d.endQuery();
        f.issued = true;
    }
    // The NDC depth of the centre: GH3's camera [I: near 0.1, far 500 (camera_default_far_clip)].
    const venue::Vec3 fwd = venue::rotate(cam.rot, {0, 0, 1});
    const float ze = (centre[0] - cam.pos[0]) * fwd[0] + (centre[1] - cam.pos[1]) * fwd[1] + (centre[2] - cam.pos[2]) * fwd[2];
    const float n = 0.1f, far = 500.0f;
    const float zn = ze > 1e-4f ? far / (far - n) * (1.0f - n / ze) : -1.0f;
    float scale = f.scale;
    if (zn > 1 || zn <= 0.91f) return;
    if (zn < 0.93f) scale *= std::sqrt((zn - 0.91f) * 50.0f);
    if (scale <= 0) return;
    vs[0][0] = vs[0][1] = vs[0][2] = vs[0][3] = scale;
    vs[1][0] = vs[1][1] = vs[1][2] = vs[1][3] = 0;
    gc.uniforms[1] = d.stageUniforms(vs, sizeof vs);
    gc.state.depthFunc = gfx::CompareFunc::Always, gc.state.depthWrite = false;
    gc.state.stencilTest = false;
    d.draw(gc);
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
    // sys_coneimpostor's state (Function_823B49D0: +0xd8 = 7, +0xcc |= 0xa8):
    // ONE + ONE, no depth write, cull none, depth tested [C].
    static const bool alphaWeighted = SDL_getenv("GD10_CONE_SRCALPHA") != nullptr;  // development aid: the old SRC_ALPHA + ONE
    c.state.blend = true;
    c.state.src = alphaWeighted ? gfx::BlendFactor::SrcAlpha : gfx::BlendFactor::One, c.state.dst = gfx::BlendFactor::One;
    c.state.op = gfx::BlendOp::Add;
    c.state.depthTest = true, c.state.depthWrite = false;
    c.state.depthFunc = gfx::CompareFunc::LessEqual;
    c.state.cull = gfx::Cull::None;
    if (smoke_)
        for (int k = 0; k < 16; ++k)
            if (prog->samplers >> k & 1) c.textures[k] = smoke_;
    const double ms = seconds_ * 1000.0;
    for (int ni : cones_) {
        const venue::Node& n = v_->nodes[size_t(ni)];
        if (!n.createdAtStart && !(n.createdOn && show.flag(n.createdOn))) continue;
        venue::HousingState h;
        if (!show.housing(n.name, ms, h)) continue;
        if (h.density <= 0) continue;
        // Function_823B4B68 [C]: P the lens, D the housing's +z (the beam runs
        // along -D); skipped while the camera is within endradius of the
        // axis, faded in over the next metre.
        const venue::Vec3 z = venue::rotate(h.rot, {0, 0, 1});
        const venue::Vec3 pc{h.pos[0] - cam.pos[0], h.pos[1] - cam.pos[1], h.pos[2] - cam.pos[2]};
        const float dd = std::fabs(pc[0] * z[0] + pc[1] * z[1] + pc[2] * z[2]);
        const venue::Vec3 pa{h.pos[0] - z[0] * dd, h.pos[1] - z[1] * dd, h.pos[2] - z[2] * dd};
        const venue::Vec3 ca{pa[0] - cam.pos[0], pa[1] - cam.pos[1], pa[2] - cam.pos[2]};
        const float q = std::sqrt(ca[0] * ca[0] + ca[1] * ca[1] + ca[2] * ca[2]);
        if (q - h.endRadius < 0) continue;
        const float dens = h.density * std::min(q - h.endRadius, 1.0f);
        if (dens <= 0 || q < 1e-5f) continue;
        // g_Rotated_Vector = normalize(P' - C) x D.
        const venue::Vec3 nv{ca[0] / q, ca[1] / q, ca[2] / q};
        venue::Vec3 side{nv[1] * z[2] - nv[2] * z[1], nv[2] * z[0] - nv[0] * z[2], nv[0] * z[1] - nv[1] * z[0]};
        float vs[64][4] = {};
        float ps[64][4] = {};
        vs[0][0] = h.startRadius, vs[0][1] = h.endRadius, vs[0][2] = h.range, vs[0][3] = dens;  // g_ConeSize
        vs[1][0] = z[0], vs[1][1] = z[1], vs[1][2] = z[2];                                    // g_SpotlightDir
        vs[2][0] = h.pos[0], vs[2][1] = h.pos[1], vs[2][2] = h.pos[2], vs[2][3] = 1;          // g_SpotlightPosition
        vs[3][0] = side[0], vs[3][1] = side[1], vs[3][2] = side[2];                           // g_Rotated_Vector
        // g_UVOffset: the record's fixed random offset (rand(10000) x 0.0001);
        // g_UVScale and the smoke velocity are 0 on the disc [C; I: the
        // record's draw, gd10 a hash of the node].
        for (int k = 0; k < 4; ++k) vs[4][k] = float((n.name * 2654435761u >> (k * 8)) % 10000u) * 0.0001f;
        vs[63][0] = -1;
        // g_SpotlightInfo = (inner, 1 - inner); g_ConeColor = volumecolor x density x the fade (alpha too) [C].
        ps[0][0] = h.innerRadius, ps[0][1] = 1.0f - h.innerRadius;
        for (int k = 0; k < 3; ++k) ps[1][k] = h.volumeColor[size_t(k)] * dens;
        ps[1][3] = dens;
        ps[63][0] = -1;
        c.uniforms[1] = d.stageUniforms(vs, sizeof vs);
        c.uniforms[2] = d.stageUniforms(ps, sizeof ps);
        d.draw(c);
    }
}

namespace {
// A general 4 x 4 inverse (the view-projection's, for g_ViewProjInv).
gfx::Mat4 inverse4(const gfx::Mat4& m) {
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c) a[r][c] = c < 4 ? double(m.at(r, c)) : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
        int p = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (std::fabs(a[p][c]) < 1e-12) return gfx::Mat4::identity();
        if (p != c)
            for (int k = 0; k < 8; ++k) std::swap(a[p][k], a[c][k]);
        const double d = a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] /= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = a[r][c];
            for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
        }
    }
    gfx::Mat4 out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out.at(r, c) = float(a[r][c + 4]);
    return out;
}
}  // namespace

// The `volume` housings [C: _Raytrace_cone_volume's shaders; Function_823B4B68
// sets them up but does not decompile, so the constants are read from the
// shaders' maths]: VS g_ConeSize_VS c1 = (start, end, range, density),
// g_UVOffset c15 / g_UVScale c16 (the smoke's, as the impostor's); PS
// g_CameraPosition_ObjectSpace_VS c0, c1, g_ConeColor c2 (the volume colour;
// the shader weighs it by c1.w, the density), g_ViewProjInv c3..c6 (screen x, y, depth to the
// world), g_coeff c17 = (-, k^2, 2 a k, a^2) for the cone x^2 + y^2 = (a +
// k z)^2 (a start radius, k (end - start) / range: the shader's quadratic
// A = dx^2 + dy^2 - k^2 dz^2, B and C with 2ak and a^2), m_Texture s0 the
// smoke, g_DepthBufferTexture s1. The chord through the cone in front of the
// scene's depth x the colour x the smoke. The near faces, ONE + ONE, depth
// tested against the scene's depth buffer (the states below).
void VenueView::drawRaytraced(const venue::CamPose& cam, const venue::LightShow& show, gfx::TextureId depth) {
    if (!dev_ || rays_.empty() || !rayCount_ || !depth || !gh3_.ready()) return;
    gfx::RenderDevice& d = *dev_;
    gh3shader::Globals globals;
    gfx::Mat4 viewM, viewProjM;
    cameraGlobals(cam, d.info().clipDepthZeroToOne, seconds_, globals, &viewProjM, 16.0f / 9.0f, false, &viewM);
    const gfx::UniformData frame = d.stageUniforms(&globals, sizeof globals);
    const gfx::Mat4 vpInv = inverse4(viewProjM);
    static const gfx::VertexLayout layout = [] {
        gfx::VertexLayout l;
        l.stride = 16;
        l.attribs[0] = {0, 4, gfx::AttribType::Float, 0};
        return l;
    }();
    gfx::DrawCall c;
    c.layout = &layout;
    c.vertices = rayVb_;
    c.indices = rayIb_;
    c.indices32 = true;
    c.first = 0, c.count = rayCount_;
    c.uniforms[0] = frame;
    for (int k = 0; k < 16; ++k) c.samplers[k] = {gfx::Filter::Linear, gfx::Wrap::Repeat, 0};
    c.samplers[1] = {gfx::Filter::Nearest, gfx::Wrap::Clamp, 0};
    // sys_raytracecone's state (Function_823B49D0 sets its +0xd8 = 7 and +0xcc
    // |= 0xa0; 823DC180 applies it) [C]: blend ONE + ONE (control 0x1010101),
    // no depth write, depth tested LESSEQUAL against the scene's own depth
    // buffer. Function_823B4B68 then culls the cone's fans / strip so that one
    // side is drawn (cull 6 for the side, 2 for the cap, the buffers built by
    // Function_823B4368) [C]: the near faces, so the band and the scenery in
    // front of the beam hide it and the shader's min(exit, scene depth) - entry
    // ends it on what it meets [I: which side, from the shader: with the far
    // faces its |chord| turned a beam behind the band into one over it].
    c.state.blend = true;
    c.state.src = gfx::BlendFactor::One, c.state.dst = gfx::BlendFactor::One, c.state.op = gfx::BlendOp::Add;
    c.state.depthTest = true, c.state.depthWrite = false;
    c.state.depthFunc = gfx::CompareFunc::LessEqual;
    c.state.cull = gfx::Cull::CounterClockwise;  // keeps the near faces of the outward-wound cone
    const double ms = seconds_ * 1000.0;
    for (int ni : rays_) {
        const venue::Node& n = v_->nodes[size_t(ni)];
        if (!n.createdAtStart && !(n.createdOn && show.flag(n.createdOn))) continue;
        venue::HousingState h;
        if (!show.housing(n.name, ms, h)) continue;
        if (h.range <= 1e-4f) continue;
        // The template: _Raytrace_cone_volume (the beam), _Raytrace_cone_projector
        // (an `everything` projector's light on the scene inside the cone) or both,
        // _Raytrace_cone_projector_volume [C: the templates' constants].
        const qb::Value* lt = n.st->find(K("lightvolumetype"));
        const qb::Value* pt = n.st->find(K("projectortype"));
        const bool volume = lt && lt->key == K("volume") && h.density > 0;
        const bool projector = pt && pt->key == K("everything") && h.projectorColor[0] + h.projectorColor[1] + h.projectorColor[2] > 0;
        if (!volume && !projector) continue;
        const gh3shader::Program* prog = gh3_.get(K(volume && projector ? "_Raytrace_cone_projector_volume"
                                                   : volume            ? "_Raytrace_cone_volume"
                                                                       : "_Raytrace_cone_projector"));
        if (!prog) continue;
        c.program = prog->id;
        for (int k = 0; k < 16; ++k) c.textures[k] = (prog->samplers >> k & 1) ? (smoke_ ? smoke_ : gh3_.white()) : gfx::TextureId{};
        c.textures[1] = depth;
        // The object's +z runs along the beam, the housing's -z [C: the impostor's direction].
        const venue::Vec3 ax = venue::rotate(h.rot, {1, 0, 0}), ay = venue::rotate(h.rot, {0, 1, 0}), az = venue::rotate(h.rot, {0, 0, 1});
        gfx::Mat4 w;
        for (int r = 0; r < 3; ++r) {
            w.at(r, 0) = -ax[size_t(r)], w.at(r, 1) = ay[size_t(r)], w.at(r, 2) = -az[size_t(r)];
            w.at(r, 3) = h.pos[size_t(r)];
        }
        gh3shader::ObjectConstants obj;
        obj.setMatrix(gh3shader::kRegWorld, w, 4);
        obj.setMatrix(gh3shader::kRegWorldView, viewM * w, 4);
        obj.setMatrix(gh3shader::kRegWorldViewProj, viewProjM * w, 4);
        const gfx::Vec3 co = w.inverseAffine().transformPoint({cam.pos[0], cam.pos[1], cam.pos[2]});
        const float a = h.startRadius, k = (h.endRadius - h.startRadius) / h.range;
        static const bool rayLog = SDL_getenv("GD10_RAY_LOG") != nullptr;  // development aid
        if (rayLog)
            SDL_Log("ray %s t=%.0f pos %.2f %.2f %.2f dir %.2f %.2f %.2f r %.2f..%.2f range %.2f inner %.2f dens %.2f col %.2f %.2f %.2f cam %.2f %.2f %.2f camobj %.2f %.2f %.2f",
                    qbkey::names().get(n.name).c_str(), seconds_ * 1000.0, h.pos[0], h.pos[1], h.pos[2], -az[0], -az[1], -az[2], h.startRadius, h.endRadius,
                    h.range, h.innerRadius, h.density, h.volumeColor[0], h.volumeColor[1], h.volumeColor[2], cam.pos[0], cam.pos[1], cam.pos[2], co.x, co.y, co.z);
        float vs[64][4] = {};
        float ps[64][4] = {};
        vs[1][0] = h.startRadius, vs[1][1] = h.endRadius, vs[1][2] = h.range, vs[1][3] = h.density;  // g_ConeSize_VS
        for (int q = 0; q < 4; ++q) vs[15][q] = float((n.name * 2654435761u >> (q * 8)) % 10000u) * 0.0001f;  // g_UVOffset
        vs[63][0] = -1;
        ps[0][0] = co.x, ps[0][1] = co.y, ps[0][2] = co.z, ps[0][3] = 1;  // g_CameraPosition_ObjectSpace_VS
        std::memcpy(ps[1], vs[1], sizeof ps[1]);
        // g_ConeColor: the record's volume colour as is (823B6314: c2 = record +0x18;
        // the impostor's is that x density, 823B5E44) [C]; the shader weighs it by
        // g_ConeSize.w (the density) itself. Alpha 1 [I: the colour's fourth lane].
        for (int q = 0; q < 3; ++q) ps[2][q] = h.volumeColor[size_t(q)];
        ps[2][3] = 1;
        for (int i = 0; i < 4; ++i)  // g_ViewProjInv, rows as the shaders read them
            for (int q = 0; q < 4; ++q) ps[3 + i][q] = vpInv.at(q, i);
        ps[17][0] = 0, ps[17][1] = k * k, ps[17][2] = 2 * a * k, ps[17][3] = a * a;  // g_coeff
        // The projector variants: g_SpotLightViewProj c7..c10 takes a scene point to
        // the cone's frame as (x, y, -, a + k z), the cone's edge at |xy / w| = 1;
        // g_SpotlightColor c11 = projectorcolor, g_SpotlightInfo c12 = (inner,
        // 1 - inner) [C: the shader's maths; the fields as the stageonly projector's].
        if (projector) {
            gfx::Mat4 pr;
            for (int r = 0; r < 4; ++r)
                for (int q = 0; q < 4; ++q) pr.at(r, q) = 0;
            pr.at(0, 0) = 1, pr.at(1, 1) = 1, pr.at(3, 2) = k, pr.at(3, 3) = a;
            const gfx::Mat4 slvp = pr * w.inverseAffine();
            for (int i = 0; i < 4; ++i)
                for (int q = 0; q < 4; ++q) ps[7 + i][q] = slvp.at(q, i);
            for (int q = 0; q < 3; ++q) ps[11][q] = h.projectorColor[size_t(q)];
            ps[11][3] = 1;
            ps[12][0] = h.innerRadius, ps[12][1] = 1.0f - h.innerRadius;
        }
        ps[63][0] = -1;
        c.uniforms[1] = d.stageUniforms(vs, sizeof vs);
        c.uniforms[2] = d.stageUniforms(ps, sizeof ps);
        c.uniforms[3] = d.stageUniforms(&obj, sizeof obj);
        d.draw(c);
        ++stats_.raytraced;
    }
}

// The stageonly projectors [C: Function_823B4B68 823B6D74..823B7354 and
// _Projector_StageOnly's shaders]: for projectortype stageonly with a range,
// the beam's frustum runs from a quad at the housing (start radius, along the
// housing's x and y) to one at `range` along the beam (end radius); each of
// its 4 edges meets the stage plane y = DAT_82a74a64 (0: EngineConfig never
// sets it on the disc), and that quad (a D3D quad list, 4 vertices with the
// unit square's UVs: the shader's round pool, uv x 2 - 1) is drawn with
// g_SpotlightColor c11 = projectorcolor and g_SpotlightInfo c12 = (inner,
// 1 - inner) (the record's fields as the impostor's), shadowed through
// g_ViewToLightProjection0/1 and g_sampShadowMap0/1 (the band's dynamic shadow,
// as the venue's receivers). An edge too flat to meet the plane skips the
// projector [C]. Additive, depth tested, no depth write [I: the states].
void VenueView::drawProjectors(const venue::CamPose& cam, const venue::LightShow& show) {
    if (!dev_ || projectors_.empty() || !projVb_ || !gh3_.ready()) return;
    const gh3shader::Program* prog = gh3_.get(K("_Projector_StageOnly"));
    if (!prog) return;
    gfx::RenderDevice& d = *dev_;
    const double ms = seconds_ * 1000.0;
    struct Quad {
        float v[4][6];
        venue::HousingState h;
    };
    std::vector<Quad> quads;
    for (int ni : projectors_) {
        const venue::Node& n = v_->nodes[size_t(ni)];
        if (!n.createdAtStart && !(n.createdOn && show.flag(n.createdOn))) continue;
        venue::HousingState h;
        if (!show.housing(n.name, ms, h)) continue;
        if (h.range <= 0) continue;
        if (h.projectorColor[0] + h.projectorColor[1] + h.projectorColor[2] <= 0) continue;
        const venue::Vec3 ax = venue::rotate(h.rot, {1, 0, 0}), ay = venue::rotate(h.rot, {0, 1, 0}), az = venue::rotate(h.rot, {0, 0, 1});
        const venue::Vec3 dir{-az[0], -az[1], -az[2]};
        const venue::Vec3 farC{h.pos[0] + dir[0] * h.range, h.pos[1] + dir[1] * h.range, h.pos[2] + dir[2] * h.range};
        // Corners in GH3's order: -x -y, -x +y, +x +y, +x -y.
        static const float kSx[4] = {-1, -1, 1, 1}, kSy[4] = {-1, 1, 1, -1};
        static const float kU[4] = {0, 0, 1, 1}, kV[4] = {0, 1, 1, 0};
        Quad q;
        q.h = h;
        bool ok = true;
        for (int i = 0; i < 4 && ok; ++i) {
            venue::Vec3 nearP, farP;
            for (int k = 0; k < 3; ++k) {
                nearP[size_t(k)] = h.pos[size_t(k)] + (ax[size_t(k)] * kSx[i] + ay[size_t(k)] * kSy[i]) * h.startRadius;
                farP[size_t(k)] = farC[size_t(k)] + (ax[size_t(k)] * kSx[i] + ay[size_t(k)] * kSy[i]) * h.endRadius;
            }
            venue::Vec3 e{farP[0] - nearP[0], farP[1] - nearP[1], farP[2] - nearP[2]};
            const float len = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            if (len < 1e-6f) {
                ok = false;
                break;
            }
            for (float& c : e) c /= len;
            if (std::fabs(e[1]) < 1e-6f) {
                ok = false;
                break;
            }
            const float t = (0.0f - nearP[1]) / e[1];
            q.v[i][0] = nearP[0] + e[0] * t, q.v[i][1] = nearP[1] + e[1] * t, q.v[i][2] = nearP[2] + e[2] * t;
            // The pool lies in the stage floor's own plane: drawn LESSEQUAL over it,
            // the two surfaces' rounding fought pixel by pixel and the light under
            // the band flickered every frame (owner 11-05). Pulled 0.1 % of the way
            // to the camera along its own sight line (the same pixels, a hair
            // nearer) it always wins [I: GH3 draws it with no depth bias
            // (823DC180 sets 0); its depth buffer's rounding is not reproduced].
            for (int k = 0; k < 3; ++k) q.v[i][k] = cam.pos[size_t(k)] + (q.v[i][k] - cam.pos[size_t(k)]) * 0.999f;
            q.v[i][3] = 1, q.v[i][4] = kU[i], q.v[i][5] = kV[i];
        }
        if (ok) quads.push_back(q);
    }
    if (quads.empty()) return;
    std::vector<float> data;
    data.reserve(quads.size() * 24);
    for (const Quad& q : quads)
        for (int i = 0; i < 4; ++i) data.insert(data.end(), q.v[i], q.v[i] + 6);
    d.updateBuffer(projVb_, 0, data.data(), data.size() * sizeof(float));

    gh3shader::Globals globals;
    gfx::Mat4 viewM, viewProjM;
    cameraGlobals(cam, d.info().clipDepthZeroToOne, seconds_, globals, &viewProjM, 16.0f / 9.0f, false, &viewM);
    gh3shader::setShadowGlobals(globals, shadow_, viewM);  // as the venue's receivers (draw)
    gh3shader::ObjectConstants obj;
    obj.setMatrix(gh3shader::kRegWorld, gfx::Mat4::identity(), 4);
    obj.setMatrix(gh3shader::kRegWorldView, viewM, 4);
    obj.setMatrix(gh3shader::kRegWorldViewProj, viewProjM, 4);
    static const gfx::VertexLayout layout = [] {
        gfx::VertexLayout l;
        l.stride = 24;
        l.attribs[0] = {0, 4, gfx::AttribType::Float, 0};
        l.attribs[1] = {3, 2, gfx::AttribType::Float, 16};  // TEXCOORD0
        return l;
    }();
    gfx::DrawCall c;
    c.program = prog->id;
    c.layout = &layout;
    c.vertices = projVb_;
    c.indices = projIb_;
    c.indices32 = true;
    c.uniforms[0] = d.stageUniforms(&globals, sizeof globals);
    c.uniforms[3] = d.stageUniforms(&obj, sizeof obj);
    for (int k = 0; k < 16; ++k) {
        c.textures[k] = (prog->samplers >> k & 1) ? gh3_.white() : gfx::TextureId{};
        c.samplers[k] = {gfx::Filter::Linear, gfx::Wrap::Clamp, 0};
    }
    c.textures[14] = shadow_ ? shadow_.tex[0] : gh3_.white();  // g_sampShadowMap0 / 1
    c.textures[15] = shadow_ ? shadow_.tex[1] : gh3_.white();
    c.samplers[14] = c.samplers[15] = {gfx::Filter::Nearest, gfx::Wrap::Clamp, 0};
    c.state.blend = true;
    c.state.src = gfx::BlendFactor::One, c.state.dst = gfx::BlendFactor::One, c.state.op = gfx::BlendOp::Add;
    c.state.depthTest = true, c.state.depthWrite = false;
    c.state.depthFunc = gfx::CompareFunc::LessEqual;
    c.state.cull = gfx::Cull::None;
    for (size_t qi = 0; qi < quads.size(); ++qi) {
        const venue::HousingState& h = quads[qi].h;
        float vs[64][4] = {};
        float ps[64][4] = {};
        vs[63][0] = -1;
        for (int k = 0; k < 3; ++k) ps[11][k] = h.projectorColor[size_t(k)];  // g_SpotlightColor
        ps[11][3] = 1;
        ps[12][0] = h.innerRadius, ps[12][1] = 1.0f - h.innerRadius;  // g_SpotlightInfo
        ps[63][0] = -1;
        c.uniforms[1] = d.stageUniforms(vs, sizeof vs);
        c.uniforms[2] = d.stageUniforms(ps, sizeof ps);
        c.first = uint32_t(qi * 6), c.count = 6;
        d.draw(c);
        ++stats_.projected;
    }
}

// gh4's 2D light volumes [C: the _LightVolume2D shaders' maths and constant
// tables, sys_lightvolume2d's textures and state; I: the geometry and the
// constants' values, the CPU side (8244E0F0, VMX128) not decoded]. The vertex
// shader takes world positions and a projective TEXCOORD0 (u q, v, q): the
// pixel shader reads the falloff at (u, v), the pattern at (u, v) x
// g_VolumeUVScale + g_VolumeUVOffset, the smoke three times from the world
// position x g_UVScale + g_UVOffset (yz, zx, xy weighted by
// g_TexturePlainWeight); colour = falloff x g_ConeColor x pattern x smoke,
// softened over 0.3 m in front of the scene's depth; alpha x (1 - g_Burn).
// Drawn here as a camera-facing trapezoid along the beam (2 startradius at
// the lens, 2 endradius at range, q the half-width so u stays straight).
void VenueView::drawVolumes2D(const venue::CamPose& cam, const venue::LightShow& show, gfx::TextureId depth) {
    if (!dev_ || vol2d_.empty() || !vol2dVb_ || !gh3_.ready()) return;
    static const bool off = SDL_getenv("GD10_NO_VOLUMES2D") != nullptr;  // development aid: compare without them
    if (off) return;
    gfx::RenderDevice& d = *dev_;
    const double ms = seconds_ * 1000.0;
    struct Beam {
        int node;
        venue::HousingState h;
        float fade;
    };
    std::vector<Beam> beams;
    std::vector<float> data;
    for (int ni : vol2d_) {
        const venue::Node& n = v_->nodes[size_t(ni)];
        if (!n.createdAtStart && !(n.createdOn && show.flag(n.createdOn))) continue;
        venue::HousingState h;
        if (!show.housing(n.name, ms, h)) continue;
        if (h.density <= 0 || h.range <= 1e-4f) continue;
        const venue::Vec3 az = venue::rotate(h.rot, {0, 0, 1});
        const venue::Vec3 dir{-az[0], -az[1], -az[2]};  // the beam runs along the housing's -z
        const venue::Vec3 e{h.pos[0] + dir[0] * h.range, h.pos[1] + dir[1] * h.range, h.pos[2] + dir[2] * h.range};
        // The side vector: across the beam, square to the sight line to its middle.
        const venue::Vec3 mid{(h.pos[0] + e[0]) * 0.5f - cam.pos[0], (h.pos[1] + e[1]) * 0.5f - cam.pos[1], (h.pos[2] + e[2]) * 0.5f - cam.pos[2]};
        venue::Vec3 side{mid[1] * dir[2] - mid[2] * dir[1], mid[2] * dir[0] - mid[0] * dir[2], mid[0] * dir[1] - mid[1] * dir[0]};
        const float sl = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        const float ml = std::sqrt(mid[0] * mid[0] + mid[1] * mid[1] + mid[2] * mid[2]);
        if (sl < 1e-5f || ml < 1e-5f) continue;
        for (float& c : side) c /= sl;
        // Seen down its axis the flat volume thins to nothing: faded out as the
        // sight line turns onto the beam [I].
        const float fade = std::min(1.0f, sl / ml * 2.0f);
        const float qs = std::max(h.startRadius, 1e-3f), qe = std::max(h.endRadius, 1e-3f);
        const venue::Vec3 ends[2] = {h.pos, e};
        const float rad[2] = {h.startRadius, h.endRadius}, q[2] = {qs, qe};
        // Corners: lens -side, lens +side, end +side, end -side.
        static const int kEnd[4] = {0, 0, 1, 1};
        static const float kS[4] = {-1, 1, 1, -1}, kU[4] = {0, 1, 1, 0};
        for (int i = 0; i < 4; ++i) {
            const int k = kEnd[i];
            for (int a = 0; a < 3; ++a) data.push_back(ends[k][size_t(a)] + side[size_t(a)] * kS[i] * rad[k]);
            data.push_back(1);
            data.push_back(kU[i] * q[k]), data.push_back(float(k)), data.push_back(q[k]);
        }
        beams.push_back({ni, h, fade});
    }
    static const bool vlog = SDL_getenv("GD10_VOL2D_LOG") != nullptr;  // development aid
    if (vlog) {
        SDL_Log("volumes2d t=%.0f: %zu of %zu drawn", ms, beams.size(), vol2d_.size());
        for (const Beam& b : beams)
            SDL_Log("  %s dens %.2f fade %.2f r %.2f..%.2f range %.2f col %.2f %.2f %.2f", qbkey::names().get(v_->nodes[size_t(b.node)].name).c_str(),
                    b.h.density, b.fade, b.h.startRadius, b.h.endRadius, b.h.range, b.h.volumeColor[0], b.h.volumeColor[1], b.h.volumeColor[2]);
    }
    if (beams.empty()) return;
    d.updateBuffer(vol2dVb_, 0, data.data(), data.size() * sizeof(float));
    gh3shader::Globals globals;
    gfx::Mat4 viewM, viewProjM;
    cameraGlobals(cam, d.info().clipDepthZeroToOne, seconds_, globals, &viewProjM, 16.0f / 9.0f, false, &viewM);
    {
        const float zn = 0.05f, zf = 4000.0f;  // cameraGlobals' planes
        const float* ip = globals.reg(gh3shader::kRegInvProj);
        globals.set(gh3shader::kRegInvProj, ip[0], ip[1], -(zf - zn) / (zf * zn), 1.0f / zn);  // viewZ = 1 / (depth x z + w)
    }
    static const gfx::VertexLayout layout = [] {
        gfx::VertexLayout l;
        l.stride = 28;
        l.attribs[0] = {0, 4, gfx::AttribType::Float, 0};
        l.attribs[1] = {3, 3, gfx::AttribType::Float, 16};  // TEXCOORD0 (u q, v, q)
        return l;
    }();
    gfx::DrawCall c;
    c.layout = &layout;
    c.vertices = vol2dVb_;
    c.indices = vol2dIb_;
    c.indices32 = true;
    c.uniforms[0] = d.stageUniforms(&globals, sizeof globals);
    gh3shader::ObjectConstants obj;
    obj.setMatrix(gh3shader::kRegWorld, gfx::Mat4::identity(), 4);
    c.uniforms[3] = d.stageUniforms(&obj, sizeof obj);
    // sys_lightvolume2d's blend 1: SRC_ALPHA + ONE; no depth write, depth tested [C data].
    c.state.blend = true;
    c.state.src = gfx::BlendFactor::SrcAlpha, c.state.dst = gfx::BlendFactor::One, c.state.op = gfx::BlendOp::Add;
    c.state.depthTest = true, c.state.depthWrite = false;
    c.state.depthFunc = gfx::CompareFunc::LessEqual;
    c.state.cull = gfx::Cull::None;
    const gfx::TextureId white = gh3_.white();
    for (size_t bi = 0; bi < beams.size(); ++bi) {
        const venue::Node& n = v_->nodes[size_t(beams[bi].node)];
        std::string tn = lightVolume2DTemplate(n);
        if (!depth) {  // no scene depth: the hard-edged variant
            const size_t at = tn.find("_softedge1");
            if (at != std::string::npos) tn.erase(at, 10);
        }
        const gh3shader::Program* prog = gh3_.get(K(tn.c_str()));
        if (vlog && bi == 0) SDL_Log("  template %s: %s, falloff %d pattern %d smoke %d depth %d", tn.c_str(), prog ? "ok" : "missing", lvFalloff_ ? 1 : 0, lvPattern_ ? 1 : 0, smoke_ ? 1 : 0, depth ? 1 : 0);
        if (!prog) continue;
        c.program = prog->id;
        for (int k = 0; k < 16; ++k) {
            c.textures[k] = (prog->samplers >> k & 1) ? white : gfx::TextureId{};
            c.samplers[k] = {gfx::Filter::Linear, gfx::Wrap::Repeat, 0};
        }
        c.textures[0] = lvFalloff_ ? lvFalloff_ : white;  // m_FalloffSampler
        c.samplers[0] = {gfx::Filter::Linear, gfx::Wrap::Clamp, 0};
        c.textures[1] = lvPattern_ ? lvPattern_ : white;  // m_PatternSampler
        c.textures[2] = smoke_ ? smoke_ : white;          // m_SmokeTextureSamp
        if (depth) {
            c.textures[4] = depth;  // g_DepthBufferSamp
            c.samplers[4] = {gfx::Filter::Nearest, gfx::Wrap::Clamp, 0};
        }
        const venue::HousingState& h = beams[bi].h;
        float vs[64][4] = {};
        float ps[64][4] = {};
        // g_UVOffset c2 / g_UVScale c3: the smoke's world scale and drift [I: 0.25 a metre,
        // drifting 0.02 a second, a fixed per-node offset].
        const float drift = float(seconds_) * 0.02f;
        for (int k = 0; k < 3; ++k) {
            vs[2][k] = float((n.name * 2654435761u >> (k * 8)) % 10000u) * 0.0001f + drift;
            vs[3][k] = 0.25f;
        }
        vs[63][0] = -1;
        // g_ConeColor c1 = volumecolor x density (alpha the density) as GH3's impostor [I].
        const float dens = h.density * beams[bi].fade;
        for (int k = 0; k < 3; ++k) ps[1][k] = h.volumeColor[size_t(k)] * dens;
        ps[1][3] = dens;
        static const float plane = SDL_getenv("GD10_VOL2D_PLANE") ? float(std::atof(SDL_getenv("GD10_VOL2D_PLANE"))) : 1.0f;  // development aid
        ps[2][0] = ps[2][1] = ps[2][2] = plane;  // g_TexturePlainWeight [I]
        ps[3][0] = 0;                                   // g_Burn [I]
        ps[4][0] = 0, ps[4][1] = 0;                     // g_VolumeUVOffset: lightvolumepatternuvoffset (0, 0) [C data snapshots]
        ps[5][0] = 1, ps[5][1] = 1;                     // g_VolumeUVScale: lightvolumepatternuvscale (1, 1)
        ps[63][0] = -1;
        c.uniforms[1] = d.stageUniforms(vs, sizeof vs);
        c.uniforms[2] = d.stageUniforms(ps, sizeof ps);
        c.first = uint32_t(bi * 6), c.count = 6;
        d.draw(c);
    }
}
