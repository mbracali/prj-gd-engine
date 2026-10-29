// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/band_view.h"

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "data/qbkey.h"
#include "render/camera.h"
#include "render/rmath.h"

namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }
constexpr int kMaxLights = 16;

struct Vtx {
    float pos[3];
    float nrm[3];
    float uv[2];
};

const gfx::VertexLayout& layout() {
    static const gfx::VertexLayout l = [] {
        gfx::VertexLayout r;
        r.stride = sizeof(Vtx);
        r.attribs[0] = {gfx::kAttribPos, 3, gfx::AttribType::Float, uint16_t(offsetof(Vtx, pos))};
        r.attribs[1] = {gfx::kAttribNormal, 3, gfx::AttribType::Float, uint16_t(offsetof(Vtx, nrm))};
        r.attribs[2] = {gfx::kAttribUv, 2, gfx::AttribType::Float, uint16_t(offsetof(Vtx, uv))};
        return r;
    }();
    return l;
}

const char* kVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 3) in vec2 aUv;
layout(std140) uniform GdBandFrame {
    mat4 uViewProj;
    vec4 uEye;
    vec4 uParams;  // x: exposure, y: dynamic light scale, z: fill
};
out vec3 vPos;
out vec3 vNormal;
out vec2 vUv;
void main() {
    vPos = aPos;
    vNormal = aNormal;
    vUv = aUv;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

// The approximation of GH3's character templates (docs/band.md "Drawing"):
// colour map x (the group's ambient + its lights with N.L and the venue's
// fall-off and cones + a small fill).
const char* kFragment = R"(
in vec3 vPos;
in vec3 vNormal;
in vec2 vUv;
layout(std140) uniform GdBandFrame {
    mat4 uViewProj;
    vec4 uEye;
    vec4 uParams;
};
layout(std140) uniform GdBandLights {
    vec4 uInfo;
    vec4 uAmbient;
    vec4 uL[64];
};
layout(std140) uniform GdBandMat {
    vec4 uMat;  // x: alpha reference, y: unlit
};
uniform sampler2D sDiffuse;
out vec4 fragColor;
void main() {
    vec4 tex = texture(sDiffuse, vUv);
    if (tex.a < uMat.x) discard;
    if (uMat.y > 0.5) {
        fragColor = tex;
        return;
    }
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uEye.xyz - vPos);
    if (dot(n, v) < 0.0) n = -n;
    vec3 lit = uAmbient.rgb + vec3(uParams.z) * (0.6 + 0.4 * max(dot(n, v), 0.0));
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
        float ndl = max(dot(n, l), 0.0);
        if (p.w > 2.5) {
            float cs = dot(-l, d.xyz);
            att *= smoothstep(cone.y, cone.x, cs);
        }
        lit += c.rgb * (ndl * att * uParams.y);
    }
    fragColor = vec4(tex.rgb * lit * uParams.x, tex.a);
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

float envFloat(const char* name, float fallback) {
    const char* v = SDL_getenv(name);
    return v ? float(std::atof(v)) : fallback;
}

}  // namespace

BandView::~BandView() { shutdown(); }

gfx::ProgramDesc BandView::programDesc() {
    gfx::ProgramDesc d;
    d.name = "band";
    d.vertex = kVertex;
    d.fragment = kFragment;
    d.uniformBlocks = {"GdBandFrame", "GdBandLights", "GdBandMat"};
    d.samplers = {"sDiffuse"};
    return d;
}

void BandView::shutdown() {
    if (!dev_) return;
    for (Member& m : members_) {
        if (m.vb) dev_->destroyBuffer(m.vb);
        if (m.ib) dev_->destroyBuffer(m.ib);
        for (gfx::TextureId t : m.textures) dev_->destroyTexture(t);
    }
    members_.clear();
    if (program_) dev_->destroyProgram(program_);
    program_ = {};
    dev_ = nullptr;
}

int BandView::add(gfx::RenderDevice& dev, const band::Model& model, std::string& error) {
    if (dev_ && dev_ != &dev) {
        error = "band view: another device";
        return -1;
    }
    dev_ = &dev;
    if (!program_) {
        program_ = dev.createProgram(programDesc(), error);
        if (!program_) return -1;
    }
    Member mem;
    std::unordered_map<uint32_t, gfx::TextureId> tex;
    for (const auto& [key, t] : model.textures) {
        if (t.usage != 0) continue;  // colour maps only [I: class 0]
        std::string err;
        auto levels = t.texLevels();
        if (const gfx::TextureId id = dev.createTexture(t.desc(), levels.data(), err)) {
            tex.emplace(key, id);
            mem.textures.push_back(id);
        }
    }
    stats_.textures += tex.size();
    std::vector<uint32_t> idx;
    for (const band::Part& part : model.parts) {
        const scene::Scene& sc = part.scene;
        for (const scene::Mesh& m : sc.meshes) {
            if (m.positions.empty() || m.indices.empty()) continue;
            const uint32_t base = uint32_t(mem.src.size());
            for (size_t k = 0; k < m.positions.size(); ++k) {
                SrcVertex v{};
                for (int j = 0; j < 3; ++j) {
                    v.pos[j] = m.positions[k][size_t(j)];
                    v.nrm[j] = k < m.normals.size() ? m.normals[k][size_t(j)] : 0.0f;
                }
                if (k < m.texcoords[0].size()) v.uv[0] = m.texcoords[0][k][0], v.uv[1] = m.texcoords[0][k][1];
                const auto& bi = k < m.blendIndices.size() ? m.blendIndices[k] : std::array<uint8_t, 4>{0, 0, 0, 0};
                const auto& bw = k < m.blendWeights.size() ? m.blendWeights[k] : std::array<float, 4>{1, 0, 0, 0};
                for (int j = 0; j < 3; ++j)
                    if (bw[size_t(j)] > 0) {
                        v.bones[v.count] = bi[size_t(j)];
                        v.weights[v.count] = bw[size_t(j)];
                        ++v.count;
                    }
                mem.src.push_back(v);
            }
            Mesh out;
            out.firstIndex = uint32_t(idx.size());
            for (const auto& t : m.triangles())
                for (uint32_t c : t) idx.push_back(base + c);
            out.indexCount = uint32_t(idx.size()) - out.firstIndex;
            if (!out.indexCount) continue;
            const scene::Material* mat = nullptr;
            for (const scene::Material& mm : sc.materials)
                if (mm.key == m.material) mat = &mm;
            if (mat) {
                // The colour map: the first class-0 texture (character templates put
                // the normal map first, a class-3 map last) [C data: the class bytes].
                bool any = false;
                for (uint32_t k : mat->textures) {
                    auto it = tex.find(k);
                    if (it != tex.end()) {
                        out.diffuse = it->second;
                        break;
                    }
                    if (model.textures.count(k)) any = true;
                }
                if (!out.diffuse && !any && !mat->textures.empty()) ++stats_.missingTextures;
                out.alphaRef = float(mat->e8 >> 24) / 255.0f;
                out.mode = mat->d8 == 1 ? Mode::Add : (mat->d8 == 3 || (mat->cc & 8)) ? Mode::Blend : Mode::Opaque;
            }
            stats_.triangles += out.indexCount / 3;
            ++stats_.meshes;
            mem.meshes.push_back(out);
        }
    }
    if (mem.src.empty() || idx.empty()) {
        error = "band view: no geometry";
        return -1;
    }
    stats_.vertices += mem.src.size();
    mem.vb = dev.createBuffer(gfx::BufferKind::Vertex, mem.src.size() * sizeof(Vtx), nullptr, true);
    mem.ib = dev.createBuffer(gfx::BufferKind::Index, idx.size() * sizeof(uint32_t), idx.data(), false);
    // Opaque meshes first, then the blended ones (hair, lashes).
    std::stable_sort(mem.meshes.begin(), mem.meshes.end(), [](const Mesh& a, const Mesh& b) {
        return (a.mode != Mode::Opaque) < (b.mode != Mode::Opaque);
    });
    members_.push_back(std::move(mem));
    return int(members_.size() - 1);
}

void BandView::draw(const venue::CamPose& cam, const std::vector<Instance>& instances, const venue::Venue& v,
                    const std::vector<venue::LightState>& lights, const venue::LightShow& show) {
    if (!dev_ || !program_) return;
    gfx::RenderDevice& d = *dev_;
    const venue::Vec3 fwd = venue::rotate(cam.rot, {0, 0, 1});
    const venue::Vec3 up = venue::rotate(cam.rot, {0, 1, 0});
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
    const gfx::Mat4 vp = gfx::Mat4::perspective(yfov, aspect, 0.05f, 4000.0f, d.info().clipDepthZeroToOne) * view;
    FrameUniforms fu{};
    std::memcpy(fu.viewProj, vp.m, sizeof fu.viewProj);
    fu.eye[0] = cam.pos[0], fu.eye[1] = cam.pos[1], fu.eye[2] = cam.pos[2];
    static const float exposure = envFloat("GD04_BAND_EXPOSURE", 1.0f);
    static const float dyn = envFloat("GD04_BAND_DYN", 0.6f);
    static const float fill = envFloat("GD04_BAND_FILL", 0.25f);
    fu.params[0] = exposure, fu.params[1] = dyn, fu.params[2] = fill;
    const gfx::UniformData frame = d.stageUniforms(&fu, sizeof fu);

    gfx::DrawCall c;
    c.program = program_;
    c.layout = &layout();
    c.indices32 = true;
    c.uniforms[0] = frame;
    c.samplers[0] = {gfx::Filter::Trilinear, gfx::Wrap::Repeat, 0};
    c.state.cull = gfx::Cull::None;
    c.state.depthTest = true;
    c.state.depthFunc = gfx::CompareFunc::LessEqual;
    size_t draws = 0;
    for (const Instance& in : instances) {
        if (in.member < 0 || size_t(in.member) >= members_.size() || !in.skin) continue;
        Member& m = members_[size_t(in.member)];
        // The member's lights, packed as the venue packs a group's [I: GH3's lightgroup per model].
        LightUniforms lu{};
        int n = 0;
        for (size_t i = 0; i < v.lights.size() && i < lights.size(); ++i) {
            const venue::Light& l = v.lights[i];
            const uint32_t grp = l.group ? l.group : K("none");
            if (std::find(in.lightGroups.begin(), in.lightGroups.end(), grp) == in.lightGroups.end() || l.vertexBake) continue;
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
            float* cc = lu.l[n * 4 + 2];
            cc[0] = s.color[0] * s.intensity, cc[1] = s.color[1] * s.intensity, cc[2] = s.color[2] * s.intensity, cc[3] = s.attenEnd;
            float* cone = lu.l[n * 4 + 3];
            cone[0] = std::cos(l.hotspot * 0.5f * 3.14159265f / 180.0f);
            cone[1] = std::cos(std::max(l.field, l.hotspot + 0.1f) * 0.5f * 3.14159265f / 180.0f);
            ++n;
        }
        lu.info[0] = float(n);
        c.uniforms[1] = d.stageUniforms(&lu, sizeof lu);

        // CPU skinning into world space.
        const std::vector<bandanim::Mat34>& sk = *in.skin;
        scratch_.resize(m.src.size() * (sizeof(Vtx) / sizeof(float)));
        Vtx* out = reinterpret_cast<Vtx*>(scratch_.data());
        for (size_t i = 0; i < m.src.size(); ++i) {
            const SrcVertex& s = m.src[i];
            bandanim::Vec p{}, nn{};
            for (int j = 0; j < s.count; ++j) {
                const size_t b = s.bones[j];
                if (b >= sk.size()) continue;
                const bandanim::Vec pj = bandanim::xform(sk[b], {s.pos[0], s.pos[1], s.pos[2]});
                const bandanim::Vec nj = bandanim::xformDir(sk[b], {s.nrm[0], s.nrm[1], s.nrm[2]});
                const float w = s.weights[j];
                p.x += pj.x * w, p.y += pj.y * w, p.z += pj.z * w;
                nn.x += nj.x * w, nn.y += nj.y * w, nn.z += nj.z * w;
            }
            p = bandanim::xform(in.root, p);
            nn = bandanim::xformDir(in.root, nn);
            out[i] = {{p.x, p.y, p.z}, {nn.x, nn.y, nn.z}, {s.uv[0], s.uv[1]}};
        }
        d.updateBuffer(m.vb, 0, out, m.src.size() * sizeof(Vtx));
        c.vertices = m.vb;
        c.indices = m.ib;
        for (const Mesh& mesh : m.meshes) {
            c.first = int(mesh.firstIndex);
            c.count = int(mesh.indexCount);
            c.textures[0] = mesh.diffuse;
            MatUniforms mu{{mesh.mode == Mode::Opaque ? std::max(mesh.alphaRef, 0.0f) : std::max(mesh.alphaRef, 0.02f),
                            mesh.mode == Mode::Add ? 1.0f : 0.0f, 0, 0}};
            c.uniforms[2] = {&mu, sizeof mu};
            switch (mesh.mode) {
            case Mode::Opaque: c.state.blend = false, c.state.depthWrite = true; break;
            case Mode::Blend:
                c.state.blend = true, c.state.depthWrite = false;
                c.state.src = gfx::BlendFactor::SrcAlpha, c.state.dst = gfx::BlendFactor::InvSrcAlpha, c.state.op = gfx::BlendOp::Add;
                break;
            case Mode::Add:
                c.state.blend = true, c.state.depthWrite = false;
                c.state.src = gfx::BlendFactor::SrcAlpha, c.state.dst = gfx::BlendFactor::One, c.state.op = gfx::BlendOp::Add;
                break;
            }
            d.draw(c);
            ++draws;
        }
    }
    stats_.drawsLastFrame = draws;
}
