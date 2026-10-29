// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/screen_fx.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "data/qbkey.h"

namespace screenfx {
namespace {

const char* kCopyVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
)";

const char* kCopyFragment = R"(
in vec2 vUv;
uniform sampler2D sTex;
out vec4 fragColor;
void main() {
    fragColor = texture(sTex, vUv);
}
)";

}  // namespace

bool ScreenFx::init(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib) {
    shutdown();
    if (!lib) return false;
    dev_ = &dev;
    gh3_.init(dev, std::move(lib));
    immediate_ = qbkey::key("ImmediateMode");
    immediateAlt_ = qbkey::key("ImmediateModeAlt");
    filter_ = qbkey::key("ImmediateModeFilter");
    // A full-screen quad (two triangles): clip x, y and the texture
    // coordinates of OpenGL's targets (v = 0 at the bottom row).
    gh3shader::Vertex v[6] = {};
    const float corners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 6; ++i) {
        v[i].pos[0] = corners[i][0], v[i].pos[1] = corners[i][1], v[i].pos[2] = 0;
        v[i].uv[0][0] = corners[i][0] * 0.5f + 0.5f, v[i].uv[0][1] = corners[i][1] * 0.5f + 0.5f;
        v[i].col[0] = v[i].col[1] = v[i].col[2] = v[i].col[3] = 255;
    }
    quad_ = dev.createBuffer(gfx::BufferKind::Vertex, sizeof v, v, false);
    gfx::ProgramDesc d;
    d.name = "screenfx copy";
    d.vertex = kCopyVertex;
    d.fragment = kCopyFragment;
    d.samplers = {"sTex"};
    std::string err;
    copy_ = dev.createProgram(d, err);
    if (!copy_ || !quad_) {
        SDL_Log("screen fx: %s", err.c_str());
        shutdown();
        return false;
    }
    return true;
}

void ScreenFx::shutdown() {
    if (!dev_) return;
    for (Target* t : {&scene_, &quarterA_, &quarterB_, &copyA_, &copyB_}) {
        if (t->id) dev_->destroyTarget(t->id);
        *t = {};
    }
    if (quad_) dev_->destroyBuffer(quad_);
    if (copy_) dev_->destroyProgram(copy_);
    quad_ = {}, copy_ = {};
    gh3_.shutdown();
    dev_ = nullptr;
}

bool ScreenFx::ensure(Target& t, int w, int h, bool depth) {
    w = std::max(1, w), h = std::max(1, h);
    if (t.id && t.w == w && t.h == h) return true;
    if (t.id) dev_->destroyTarget(t.id);
    t = {};
    std::string err;
    gfx::TargetDesc d;
    d.width = w, d.height = h, d.depth = depth;
    t.id = dev_->createTarget(d, err);
    if (!t.id) {
        SDL_Log("screen fx: no %dx%d target: %s", w, h, err.c_str());
        return false;
    }
    t.w = w, t.h = h;
    return true;
}

bool ScreenFx::begin() {
    if (!dev_) return false;
    outer_ = dev_->currentTarget();
    dev_->currentViewport(ox_, oy_, ow_, oh_);
    if (ow_ <= 0 || oh_ <= 0) return false;
    // The venue viewport's own colour + depth-stencil (A8R8G8B8, D24S8) [C 0.4].
    if (!scene_.id || scene_.w != ow_ || scene_.h != oh_) {
        if (scene_.id) dev_->destroyTarget(scene_.id);
        scene_ = {};
        std::string err;
        gfx::TargetDesc d;
        d.width = ow_, d.height = oh_, d.depth = true, d.stencil = true;
        scene_.id = dev_->createTarget(d, err);
        if (!scene_.id) {
            SDL_Log("screen fx: no scene target: %s", err.c_str());
            return false;
        }
        scene_.w = ow_, scene_.h = oh_;
    }
    dev_->setTarget(scene_.id);
    dev_->setViewport(0, 0, scene_.w, scene_.h);
    const float black[4] = {0, 0, 0, 1};
    dev_->clear(black, true);
    return true;
}

void ScreenFx::pass(const gh3shader::Program* prog, const Target* dst, gfx::TextureId s0, gfx::TextureId s1, const gh3shader::Globals& g,
                    const float (*ps)[4], int psCount, Blend blend, bool s0Point, bool stencilEqual1) {
    if (dst) {
        dev_->setTarget(dst->id);
        dev_->setViewport(0, 0, dst->w, dst->h);
    } else {
        dev_->setTarget(outer_);
        dev_->setViewport(ox_, oy_, ow_, oh_);
    }
    gfx::DrawCall c;
    c.program = prog ? prog->id : copy_;
    c.state.alphaWrite = true;
    if (blend != Blend::Replace) {
        c.state.blend = true, c.state.src = gfx::BlendFactor::SrcAlpha;
        c.state.dst = blend == Blend::Add ? gfx::BlendFactor::One : gfx::BlendFactor::InvSrcAlpha;
    }
    if (stencilEqual1) c.state.stencilTest = true, c.state.stencilFunc = gfx::CompareFunc::Equal, c.state.stencilRef = 1;
    c.layout = &gh3shader::vertexLayout();
    c.vertices = quad_;
    c.count = 6;
    const gfx::SamplerState lin{gfx::Filter::Linear, gfx::Wrap::Clamp, 0}, pt{gfx::Filter::Nearest, gfx::Wrap::Clamp, 0};
    c.textures[0] = s0, c.samplers[0] = s0Point ? pt : lin;
    c.textures[1] = s1 ? s1 : s0, c.samplers[1] = lin;
    for (int k = 2; k < 4; ++k) c.textures[k] = s0, c.samplers[k] = lin;
    static gh3shader::MaterialConstants mc;
    mc = gh3shader::MaterialConstants{};
    for (int i = 0; i < psCount && i < 63; ++i) std::memcpy(mc.ps[i], ps[i], sizeof mc.ps[i]);
    mc.ps[63][0] = -1.0f, mc.vs[63][0] = -1.0f;
    static gh3shader::ObjectConstants oc;
    if (prog) {
        c.uniforms[0] = {g.c, sizeof g.c, 0};
        c.uniforms[1] = {mc.vs, sizeof mc.vs, 0};
        c.uniforms[2] = {mc.ps, sizeof mc.ps, 0};
        c.uniforms[3] = {oc.c, sizeof oc.c, 0};
    }
    dev_->draw(c);
}

void ScreenFx::downsample4x(gfx::TextureId src, const Target& dst) {
    // _2D_DownSample4x: the mean of the 16 source texels at (+-0.5, +-1.5)^2 [C #2516].
    const gh3shader::Program* p = gh3_.get(filter_, "_2D_DownSample4x");
    gh3shader::Globals g;
    pass(p, &dst, src, {}, g, nullptr, 0, Blend::Replace);
}

namespace {
// Function_823C9160: exp(-(x^2 + y^2) / (2 rho^2)) / sqrt(2 pi rho^2) [C shape; I: -1 / pi].
float gauss(float x, float rho) { return std::exp(-x * x / (2 * rho * rho)) / std::sqrt(2 * 3.14159265f * rho * rho); }
}  // namespace

void ScreenFx::bloomBlur(const Target& t, const Target& tmp, float gain) {
    // 823CBF20: _2D_GaussianBlur7x7H then V, seven taps one texel apart
    // (fixed offsets -3..3), weights gain x g(i) / sum g, rho = 1; each pass
    // into the 8-bit target (clamped) [C].
    const gh3shader::Program* h = gh3_.get(filter_, "_2D_GaussianBlur7x7H");
    const gh3shader::Program* v = gh3_.get(filter_, "_2D_GaussianBlur7x7V");
    if (!h || !v) return;
    float g7[7], sum = 0;
    for (int i = 0; i < 7; ++i) sum += (g7[i] = gauss(float(i - 3), 1.0f));
    float w[7][4] = {};
    for (int i = 0; i < 7; ++i) w[i][0] = w[i][1] = w[i][2] = w[i][3] = gain * g7[i] / sum;
    gh3shader::Globals g;
    pass(h, &tmp, dev_->targetTexture(t.id), {}, g, w, 7, Blend::Replace, true);
    pass(v, &t, dev_->targetTexture(tmp.id), {}, g, w, 7, Blend::Replace, true);
}

void ScreenFx::dofBlur(const Target& t, const Target& tmp, float radius) {
    // 823CAD48: _2D_GaussianBlur7x7 (m_filterSampleOffsets c168..174,
    // m_filterSampleWeights c0..6) H then V; rho = sqrt(r / 3); the centre
    // G(0) and three merged pairs (2k - 1, 2k): weight G(2k-1) + G(2k) at
    // 2k - 1 + G(2k) / w texels; not normalised. The further passes (taps
    // 6 (p - 1) texels further out) weigh under 1e-6 for the shipped radii
    // and are left out [C; I: their omission].
    const gh3shader::Program* p = gh3_.get(filter_, "_2D_GaussianBlur7x7");
    if (!p) return;
    const float r = std::max(radius, 0.5f), rho = std::sqrt(r / 3.0f);
    float off[7], w[7][4] = {};
    off[0] = 0;
    float wt[7];
    wt[0] = gauss(0, rho);
    for (int k = 1; k <= 3; ++k) {
        const float x1 = float(2 * k - 1), x2 = float(2 * k), ww = gauss(x1, rho) + gauss(x2, rho);
        const float o = x1 + gauss(x2, rho) / ww;
        off[2 * k - 1] = o, off[2 * k] = -o;
        wt[2 * k - 1] = wt[2 * k] = ww;
    }
    for (int i = 0; i < 7; ++i) w[i][0] = w[i][1] = w[i][2] = w[i][3] = wt[i];
    for (int dir = 0; dir < 2; ++dir) {
        gh3shader::Globals g;
        for (int i = 0; i < 7; ++i) {
            const float ox = dir == 0 ? off[i] / float(t.w) : 0.0f, oy = dir == 1 ? off[i] / float(t.h) : 0.0f;
            g.set(168 + i, ox, oy, 0, 0);
        }
        if (dir == 0) pass(p, &tmp, dev_->targetTexture(t.id), {}, g, w, 7, Blend::Replace);
        else pass(p, &t, dev_->targetTexture(tmp.id), {}, g, w, 7, Blend::Replace);
    }
}

void ScreenFx::end(const Params& p) {
    if (!dev_) return;
    // 823DE658: the instances in the order the scripts added them (venue_DOF,
    // the TOD's BnS, sbloom), each reading the previous one's output [C].
    gfx::TextureId cur = dev_->targetTexture(scene_.id);
    const Target* curT = &scene_;
    const int qw = std::max(1, scene_.w / 4), qh = std::max(1, scene_.h / 4);
    const bool quarters = ensure(quarterA_, qw, qh, false) && ensure(quarterB_, qw, qh, false);
    const bool copies = ensure(copyA_, scene_.w, scene_.h, false) && ensure(copyB_, scene_.w, scene_.h, false);
    auto next = [&]() -> const Target& { return curT == &copyA_ ? copyB_ : copyA_; };
    gh3shader::Globals none;

    // dof (824440F8): the colour blurred at a quarter of the size, then
    // lerp(scene, blur, a), a = strength x saturate((z - front x far) /
    // ((back - front) x far)) (_2D_DepthOfFieldNear) [C].
    const Dof& d = p.dof;
    if (d.on && d.strength > 0 && quarters && copies) {
        downsample4x(cur, quarterA_);
        dofBlur(quarterA_, quarterB_, d.blurRadius);
        const Target& out = next();
        pass(nullptr, &out, cur, {}, none, nullptr, 0, Blend::Replace);
        const gh3shader::Program* comp = gh3_.get(immediate_, "_2D_DepthOfFieldNear");
        float pc[55][4] = {};
        // m_vInvProjMatT (c53): viewZ = 1 / (depth x z + w) [C]; this depth buffer's projection.
        const float n = p.nearZ, f = p.farZ;
        pc[53][2] = -(f - n) / (f * n), pc[53][3] = 1.0f / n;
        // m_vDepthOfFieldParams (c54): (-, strength, (back - front) x far, front x far) [C].
        pc[54][1] = d.strength, pc[54][2] = (d.backDist - d.frontDist) * p.cameraFar, pc[54][3] = d.frontDist * p.cameraFar;
        if (comp) pass(comp, &out, dev_->targetDepth(scene_.id), dev_->targetTexture(quarterA_.id), none, pc, 55, Blend::Alpha, true);
        cur = dev_->targetTexture(out.id), curT = &out;
    }
    // bright_sat (8243E5F0): out = (rgb, 1) x H x S x C x B x M as three
    // dot products (_2D_Tex_BS_NoViewport), alpha = the red result [C].
    for (const BrightSat& bs : p.brightSat) {
        if (!bs.on || !copies) continue;
        const gh3shader::Program* prog = gh3_.get(immediateAlt_, "_2D_Tex_BS_NoViewport");
        if (!prog) continue;
        // The matrix, column-vector form: Mix^T(b (k (Sat^T(Hue^T rgb) - 0.5) + 0.5)).
        float m[3][4] = {};
        {
            // Hue: rotation about (1,1,1)/sqrt(3) by hue x 2 pi (XMMatrixRotationAxis, row-vector).
            const float a = bs.hue * 6.2831855f, c = std::cos(a), s = std::sin(a), t = 1 - c, nn = 1.0f / std::sqrt(3.0f);
            float H[3][3];  // row-vector: out_j = sum_i in_i H[i][j]
            const float nx = nn, ny = nn, nz = nn;
            H[0][0] = t * nx * nx + c, H[0][1] = t * nx * ny + s * nz, H[0][2] = t * nx * nz - s * ny;
            H[1][0] = t * nx * ny - s * nz, H[1][1] = t * ny * ny + c, H[1][2] = t * ny * nz + s * nx;
            H[2][0] = t * nx * nz + s * ny, H[2][1] = t * ny * nz - s * nx, H[2][2] = t * nz * nz + c;
            // Saturation (Haeberli weights 0.3086, 0.6094, 0.0820).
            const float sw[3] = {0.3086f, 0.6094f, 0.0820f}, sat = bs.saturation;
            float S[3][3];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) S[i][j] = (1 - sat) * sw[i] + (i == j ? sat : 0);
            float HS[3][3];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) HS[i][j] = H[i][0] * S[0][j] + H[i][1] * S[1][j] + H[i][2] * S[2][j];
            // Contrast about 0.5, brightness: x' = b (k (x - 0.5) + 0.5) -> scale b k, offset b 0.5 (1 - k).
            const float sc = bs.brightness * bs.contrast, of = bs.brightness * 0.5f * (1 - bs.contrast);
            // Mix: out_j = sum_i in_i mix[i][j].
            for (int j = 0; j < 3; ++j) {      // the output channel
                for (int i = 0; i < 3; ++i) {  // the input channel
                    float v = 0;
                    for (int k = 0; k < 3; ++k) v += HS[i][k] * sc * bs.mix[k][j];
                    m[j][i] = v;
                }
                float o = 0;
                for (int k = 0; k < 3; ++k) o += of * bs.mix[k][j];
                m[j][3] = o;
            }
        }
        gh3shader::Globals g;
        for (int j = 0; j < 3; ++j) g.set(128 + j, m[j][0], m[j][1], m[j][2], m[j][3]);
        const Target& out = next();
        pass(prog, &out, cur, {}, g, nullptr, 0, Blend::Replace);
        cur = dev_->targetTexture(out.id), curT = &out;
    }
    // simple_bloom (8243D240), materialfilter: the colour where the stencil
    // is 1, box-downsampled to a quarter; blur(falloff1) then blur(1); added
    // with SRC_ALPHA, ONE as B x kColorBloom0 x kColor (_2D_ComposeBloom0) [C].
    const Bloom& b = p.bloom;
    const bool bloom = b.on && (b.bloom1 > 0 || b.bloom2 > 0) && (b.falloff1 > 0 || b.falloff2 > 0) && quarters && copies;
    if (bloom) {
        if (curT == &scene_) {
            const Target& out = next();
            pass(nullptr, &out, cur, {}, none, nullptr, 0, Blend::Replace);
            cur = dev_->targetTexture(out.id), curT = &out;
        }
        if (b.materialFilter) {
            dev_->setTarget(scene_.id);
            dev_->setViewport(0, 0, scene_.w, scene_.h);
            const float zero[4] = {0, 0, 0, 0};
            dev_->clear(zero, false);
            pass(gh3_.get(immediate_, "_2D_Tex_NoViewport"), &scene_, cur, {}, none, nullptr, 0, Blend::Replace, false, true);
            downsample4x(dev_->targetTexture(scene_.id), quarterA_);
        } else {
            // _2D_Cutoff: the 16-texel mean where its rgb sum exceeds the cutoff colour's [C #2330].
            float pc[55][4] = {};
            pc[54][0] = b.cutoff[0], pc[54][1] = b.cutoff[1], pc[54][2] = b.cutoff[2];
            pass(gh3_.get(immediate_, "_2D_Cutoff"), &quarterA_, cur, {}, none, pc, 55, Blend::Replace);
        }
        bloomBlur(quarterA_, quarterB_, b.falloff1);
        bloomBlur(quarterA_, quarterB_, 1.0f);
    }
    // The result where the venue would have drawn.
    pass(nullptr, nullptr, cur, {}, none, nullptr, 0, Blend::Replace);
    if (bloom) {
        const gh3shader::Program* comp = gh3_.get(immediateAlt_, "_2D_ComposeBloom0");
        gh3shader::Globals gc;
        gc.set(128, b.strength[0], b.strength[1], b.strength[2], 1);  // m_kColor
        gc.set(129, b.bloom1, b.bloom1, b.bloom1, 1);                  // m_kColorBloom0
        if (comp) pass(comp, nullptr, dev_->targetTexture(quarterA_.id), {}, gc, nullptr, 0, Blend::Add);
    }
    dev_->setTarget(outer_);
    dev_->setViewport(ox_, oy_, ow_, oh_);
}

}  // namespace screenfx
