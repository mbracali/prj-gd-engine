// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/highway_renderer.h"

#include <cmath>
#include <cstring>

namespace gr = gd::core::render;

namespace gd24 {

namespace {

const char* kMeshVs = R"(
GD_ATTR(0) vec3 aPos;
GD_ATTR(1) vec2 aUv;
GD_ATTR(2) vec4 aColor;
GD_UNIFORMS(0, Draw) { mat4 uMvp; vec4 uTint; vec4 uParams; };
GD_OUT(0) vec2 vUv;
GD_OUT(1) vec4 vColor;
void main() {
    vUv = aUv + uParams.xy;
    vColor = aColor * uTint;
    gl_Position = gd_clip(uMvp * vec4(aPos, 1.0));
}
)";
const char* kMeshFs = R"(
GD_IN(0) vec2 vUv;
GD_IN(1) vec4 vColor;
GD_UNIFORMS(0, Draw) { mat4 uMvp; vec4 uTint; vec4 uParams; };
GD_SAMPLER2D(0, uTex);
GD_FRAG_OUT(0) vec4 fragColor;
void main() {
    vec4 c = texture(uTex, vUv) * vColor;
    if (c.a < uParams.z) discard;
    fragColor = c;
}
)";
// The canvas over the picture: a full-target quad from the vertex id.
const char* kQuadVs = R"(
GD_OUT(0) vec2 vUv;
void main() {
    vec2 p = vec2(float(GD_VERTEX_ID & 1), float((GD_VERTEX_ID >> 1) & 1));
    vUv = vec2(p.x, 1.0 - p.y);
    gl_Position = gd_clip(vec4(p * 2.0 - 1.0, 0.0, 1.0));
}
)";
const char* kQuadFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_FRAG_OUT(0) vec4 fragColor;
void main() { fragColor = texture(uTex, vUv); }
)";

struct Vertex {
    float pos[3];
    float uv[2];
    uint8_t color[4];
};

struct DrawUniforms {
    float mvp[16];
    float tint[4];
    float params[4];
};

// GE primitives to a triangle list (3 triangles, 4 strip, 5 fan; others skipped).
std::vector<Vertex> triangles(const PspMesh& m) {
    std::vector<Vertex> out;
    auto vert = [&](size_t i) {
        const PspVertex& p = m.vertices[i];
        Vertex v{{p.x, p.y, p.z}, {p.u, p.v}, {uint8_t(p.color), uint8_t(p.color >> 8), uint8_t(p.color >> 16), uint8_t(p.color >> 24)}};
        return v;
    };
    size_t first = 0;
    for (const PspMesh::Prim& pr : m.prims) {
        const size_t n = size_t(pr.count);
        if (first + n > m.vertices.size()) break;
        if (pr.type == 3) {
            for (size_t i = 0; i + 2 < n; i += 3) out.push_back(vert(first + i)), out.push_back(vert(first + i + 1)), out.push_back(vert(first + i + 2));
        } else if (pr.type == 4) {
            for (size_t i = 0; i + 2 < n; ++i) {
                out.push_back(vert(first + i));
                out.push_back(vert(first + i + 1 + (i & 1)));
                out.push_back(vert(first + i + 2 - (i & 1)));
            }
        } else if (pr.type == 5) {
            for (size_t i = 1; i + 1 < n; ++i) out.push_back(vert(first)), out.push_back(vert(first + i)), out.push_back(vert(first + i + 1));
        }
        first += n;
    }
    return out;
}

}  // namespace

Mat4 Mat4::translate(float x, float y, float z) {
    Mat4 r;
    r.m[12] = x, r.m[13] = y, r.m[14] = z;
    return r;
}

Mat4 Mat4::scale(float x, float y, float z) {
    Mat4 r;
    r.m[0] = x, r.m[5] = y, r.m[10] = z;
    return r;
}

Mat4 Mat4::perspective(float fovYDeg, float aspect, float n, float f) {
    Mat4 r;
    const float t = 1.0f / std::tan(fovYDeg * 3.14159265f / 360.0f);
    std::memset(r.m, 0, sizeof r.m);
    r.m[0] = t / aspect, r.m[5] = t, r.m[10] = (f + n) / (n - f), r.m[11] = -1, r.m[14] = 2 * f * n / (n - f);
    return r;
}

Mat4 Mat4::lookAt(const float eye[3], const float at[3], const float up[3]) {
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    auto norm = [](float* v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0) v[0] /= l, v[1] /= l, v[2] /= l;
    };
    norm(f);
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    norm(s);
    const float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    Mat4 r;
    r.m[0] = s[0], r.m[4] = s[1], r.m[8] = s[2];
    r.m[1] = u[0], r.m[5] = u[1], r.m[9] = u[2];
    r.m[2] = -f[0], r.m[6] = -f[1], r.m[10] = -f[2];
    r.m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    r.m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    r.m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float v = 0;
            for (int k = 0; k < 4; ++k) v += m[k * 4 + row] * o.m[c * 4 + k];
            r.m[c * 4 + row] = v;
        }
    return r;
}

bool HighwayRenderer::init(gr::Device& dev, const HighwayAssets& assets, std::string& error) {
    shutdown();
    dev_ = &dev;
    for (const PspTexture& t : assets.textures) {
        gr::TextureDesc td;
        td.width = t.width, td.height = t.height, td.format = gr::Format::RGBA8, td.usage = gr::kSampledTexture;
        td.label = "gd24 psp texture";
        gr::Texture tex = dev.createTexture(td);
        if (tex) dev.updateTexture(gr::TextureRegion{tex}, t.width, t.height, t.rgba.data(), size_t(t.width) * 4);
        textures_.push_back(tex);
    }
    {
        gr::TextureDesc td;
        td.width = td.height = 1, td.format = gr::Format::RGBA8;
        white_ = dev.createTexture(td);
        const uint8_t w[4] = {255, 255, 255, 255};
        dev.updateTexture(gr::TextureRegion{white_}, 1, 1, w, 4);
        td.width = kW, td.height = kH, td.usage = gr::kColorTarget | gr::kSampledTexture, td.label = "gd24 3d colour";
        color_ = dev.createTexture(td);
        td.format = gr::Format::D24, td.usage = gr::kDepthTarget, td.label = "gd24 3d depth";
        depth_ = dev.createTexture(td);
    }
    for (const auto& [name, m] : assets.models) {
        Model gm;
        gm.name = name;
        for (const HwSubMesh& s : m.subs) {
            std::vector<Vertex> v = triangles(s.mesh);
            Sub sub;
            sub.count = uint32_t(v.size());
            if (!v.empty()) sub.vb = dev.createBuffer(gr::BufferDesc{v.size() * sizeof(Vertex), gr::kVertexBuffer, false, "gd24 mesh"}, v.data());
            sub.texture = s.textureIndex, sub.blended = s.additive(), sub.overdrive = s.overdriveLayer(), sub.material = s.material;
            gm.subs.push_back(sub);
        }
        models_.push_back(std::move(gm));
    }
    gr::SamplerDesc sd;
    sd.mipFilter = gr::MipFilter::None;
    sampler_ = dev.createSampler(sd);
    sd.wrapU = sd.wrapV = gr::Wrap::ClampToEdge;
    clampSampler_ = dev.createSampler(sd);
    gr::ShaderDesc sh;
    sh.name = "gd24 psp mesh";
    sh.glslVertex = kMeshVs, sh.glslFragment = kMeshFs;
    sh.uniformBlocks[0] = "Draw";
    sh.samplers[0] = "uTex";
    shader_ = dev.createShader(sh);
    gr::ShaderDesc qd;
    qd.name = "gd24 overlay";
    qd.glslVertex = kQuadVs, qd.glslFragment = kQuadFs;
    qd.samplers[0] = "uTex";
    quadShader_ = dev.createShader(qd);
    if (!shader_ || !quadShader_ || !color_ || !depth_) {
        error = "highway renderer: " + dev.lastError();
        shutdown();
        return false;
    }
    gr::PipelineDesc p;
    p.shader = shader_;
    p.layout.bufferCount = 1;
    p.layout.buffers[0].stride = sizeof(Vertex);
    p.layout.attributeCount = 3;
    p.layout.attributes[0] = {0, 0, gr::VertexFormat::Float3, 0};
    p.layout.attributes[1] = {1, 0, gr::VertexFormat::Float2, 12};
    p.layout.attributes[2] = {2, 0, gr::VertexFormat::UByte4Norm, 20};
    p.depthFormat = gr::Format::D24;
    p.depthStencil.depthTest = true, p.depthStencil.depthWrite = true;
    p.depthStencil.depthCompare = gr::CompareOp::LessEqual;
    p.blend[0].enable = true;
    p.blend[0].srcColor = gr::BlendFactor::SrcAlpha, p.blend[0].dstColor = gr::BlendFactor::OneMinusSrcAlpha;
    p.blend[0].srcAlpha = gr::BlendFactor::One, p.blend[0].dstAlpha = gr::BlendFactor::OneMinusSrcAlpha;
    p.label = "gd24 alpha";
    opaque_ = dev.createPipeline(p);
    p.depthStencil.depthWrite = false;
    p.blend[0].srcColor = gr::BlendFactor::SrcAlpha, p.blend[0].dstColor = gr::BlendFactor::One;
    p.blend[0].srcAlpha = gr::BlendFactor::Zero, p.blend[0].dstAlpha = gr::BlendFactor::One;
    p.label = "gd24 additive";
    blended_ = dev.createPipeline(p);
    gr::PipelineDesc q;
    q.shader = quadShader_;
    q.primitive = gr::Primitive::TriangleStrip;
    q.depthFormat = gr::Format::D24;
    q.blend[0].enable = true;
    q.blend[0].srcColor = gr::BlendFactor::One, q.blend[0].dstColor = gr::BlendFactor::OneMinusSrcAlpha;
    q.blend[0].srcAlpha = gr::BlendFactor::One, q.blend[0].dstAlpha = gr::BlendFactor::OneMinusSrcAlpha;
    q.label = "gd24 overlay";
    overlay_ = dev.createPipeline(q);
    if (!opaque_ || !blended_ || !overlay_) {
        error = "highway renderer pipelines: " + dev.lastError();
        shutdown();
        return false;
    }
    return true;
}

void HighwayRenderer::shutdown() {
    if (!dev_) return;
    for (Model& m : models_)
        for (Sub& s : m.subs)
            if (s.vb) dev_->destroyBuffer(s.vb);
    models_.clear();
    for (gr::Texture t : textures_)
        if (t) dev_->destroyTexture(t);
    textures_.clear();
    for (gr::Texture* t : {&white_, &color_, &depth_})
        if (*t) dev_->destroyTexture(*t), *t = {};
    for (gr::Pipeline* p : {&opaque_, &blended_, &overlay_})
        if (*p) dev_->destroyPipeline(*p), *p = {};
    for (gr::Shader* s : {&shader_, &quadShader_})
        if (*s) dev_->destroyShader(*s), *s = {};
    dev_ = nullptr;
}

void HighwayRenderer::begin(const Mat4& viewProj, const float clear[4]) {
    viewProj_ = viewProj;
    gr::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = color_;
    for (int k = 0; k < 4; ++k) p.colors[0].clear[k] = clear[k];
    p.depth.texture = depth_;
    p.label = "gd24 highway";
    dev_->beginPass(p);
    dev_->setViewport(gr::Rect{0, 0, kW, kH});
}

void HighwayRenderer::draw(const std::string& name, const Draw& d) {
    const Model* m = nullptr;
    for (const Model& x : models_)
        if (x.name == name) m = &x;
    if (!m) return;
    DrawUniforms u;
    const Mat4 mvp = viewProj_ * d.model;
    std::memcpy(u.mvp, mvp.m, sizeof u.mvp);
    std::memcpy(u.tint, d.tint, sizeof u.tint);
    for (const Sub& s : m->subs) {
        if (!s.count || s.blended != d.blendedPass || (s.overdrive && !d.overdrive)) continue;
        if (!d.onlySub.empty() && s.material != d.onlySub) continue;
        const int ti = d.textureOverride >= 0 ? d.textureOverride : s.texture;
        const gr::Texture tex = ti >= 0 && ti < int(textures_.size()) && textures_[size_t(ti)] ? textures_[size_t(ti)] : white_;
        u.params[0] = d.uvOffset[0], u.params[1] = d.uvOffset[1];
        u.params[2] = s.blended ? 0.0f : 0.02f, u.params[3] = 0;
        dev_->bindPipeline(s.blended ? blended_ : opaque_);
        const gr::TextureBinding tb{tex, sampler_};
        dev_->bindTextures(0, &tb, 1);
        dev_->pushUniforms(0, &u, sizeof u);
        const gr::BufferRange vb{s.vb, 0, size_t(s.count) * sizeof(Vertex)};
        dev_->bindVertexBuffers(0, &vb, 1);
        dev_->draw(s.count);
    }
}

gr::Texture HighwayRenderer::end(gr::Texture overlay) {
    if (overlay) {
        dev_->bindPipeline(overlay_);
        const gr::TextureBinding tb{overlay, clampSampler_};
        dev_->bindTextures(0, &tb, 1);
        dev_->draw(4);
    }
    dev_->endPass();
    return color_;
}

}  // namespace gd24
