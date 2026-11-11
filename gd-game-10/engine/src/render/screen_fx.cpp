// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/screen_fx.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <map>
#include <mutex>

#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "data/scene.h"
#include "data/texture.h"
#include "render/hitch.h"
#include "script/globals.h"

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

// _Uber_ScreenFX + the parts in Function_8251EC80's order (_bloom1 _dof1
// _noise1 _color1 _vignette1 _inkblot1) [C 8251EC80, the library's 64 templates].
uint32_t uberKey(std::string parts, bool dof) {
    if (dof) {
        const size_t at = parts.rfind("_bloom1", 0) == 0 ? 7 : 0;
        parts.insert(at, "_dof1");
    }
    return qbkey::key("_Uber_ScreenFX" + parts);
}

// bright_sat's matrix, column-vector form: Mix^T(b (k (Sat^T(Hue^T rgb) - 0.5) + 0.5)), rows = the output channels [C 8243E5F0].
void brightSatMatrix(const BrightSat& bs, float m[3][4]) {
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
}

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
    copy_ = dev.beginProgram(d, err);
    if (!copy_ || !quad_) {
        SDL_Log("screen fx: %s", err.c_str());
        shutdown();
        return false;
    }
    // Every pass the chain can run, compiled with the venue (no compile in
    // the first frame an effect turns on).
    // (Asked for here, ready once settle() says so.)
    for (const char* t : {"_2D_DepthOfFieldNear", "_2D_Tex_NoViewport", "_2D_Cutoff"}) gh3_.request(immediate_, t);
    for (const char* t : {"_2D_Tex_BS_NoViewport", "_2D_ComposeBloom0"}) gh3_.request(immediateAlt_, t);
    for (const char* t : {"_2D_DownSample4x", "_2D_GaussianBlur7x7H", "_2D_GaussianBlur7x7V", "_2D_GaussianBlur7x7"}) gh3_.request(filter_, t);
    // gh4's: bloom2's colour matrix and the uber passes the TOD profiles
    // turn on (tod_profiles.qb: bloom1 / noise1 / color1 with vignette1,
    // inkblot1, each with and without dof1) [C data]; any other set
    // compiles when first drawn.
    gh3_.request(qbkey::key("_Bloom_SubtractModulate"));
    for (const char* parts : {"_bloom1_noise1_color1", "_bloom1_noise1_color1_vignette1", "_bloom1_noise1_color1_inkblot1",
                              "_bloom1_noise1_color1_vignette1_inkblot1", "_noise1", "_noise1_color1", "_bloom1_noise1", "_noise1_vignette1"})
        for (bool dof : {false, true}) gh3_.request(uberKey(parts, dof));
    startSysTextures();
    return true;
}

bool ScreenFx::settle() {
    if (!dev_) return true;
    if (!pollSysTextures()) return false;
    if (!copyReady_) {
        std::string err;
        const int r = dev_->programStatus(copy_, err);
        if (r == 0) return false;
        copyReady_ = true;
        if (r < 0) SDL_Log("screen fx: %s", err.c_str()), copy_ = {};
    }
    return gh3_.settle();
}

void ScreenFx::shutdown() {
    if (!dev_) return;
    for (gfx::TextureId* t : {&noiseTex_, &vignetteTex_, &inkTex_, &black_}) {
        if (*t) dev_->destroyTexture(*t);
        *t = {};
    }
    sys_.reset(), sysDone_ = false, sysVignette_ = false;
    vignetteFor_[0] = -1;
    for (Target* t : {&scene_, &quarterA_, &quarterB_, &copyA_, &copyB_, &depthCopy_, &sceneQ_, &halves_[0], &halves_[1], &halves_[2], &halves_[3], &sixteenA_, &sixteenB_, &bloomA_, &bloomB_}) {
        if (t->id) dev_->destroyTarget(t->id);
        *t = {};
    }
    if (quad_) dev_->destroyBuffer(quad_);
    if (copy_) dev_->destroyProgram(copy_);
    quad_ = {}, copy_ = {}, copyReady_ = false;
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

bool ScreenFx::begin(float scale) {
    if (!dev_) return false;
    outer_ = dev_->currentTarget();
    dev_->currentViewport(ox_, oy_, ow_, oh_);
    if (ow_ <= 0 || oh_ <= 0) return false;
    // The venue viewport's own colour + depth-stencil (A8R8G8B8, D24S8) [C 0.4];
    // `scale` (Enhanced: Internal Resolution, SSAA, FSR 1) sizes it against the viewport [I].
    const int sw = std::clamp(int(std::lround(float(ow_) * scale)), 16, 8192), sh = std::clamp(int(std::lround(float(oh_) * scale)), 16, 8192);
    if (!scene_.id || scene_.w != sw || scene_.h != sh) {
        if (scene_.id) dev_->destroyTarget(scene_.id);
        scene_ = {};
        std::string err;
        gfx::TargetDesc d;
        d.width = sw, d.height = sh, d.depth = true, d.stencil = true;
        scene_.id = dev_->createTarget(d, err);
        if (!scene_.id) {
            SDL_Log("screen fx: no scene target: %s", err.c_str());
            return false;
        }
        scene_.w = sw, scene_.h = sh;
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

void ScreenFx::resample(gfx::TextureId src, int w, int h, const Target& dst) {
    gh3shader::Globals none;
    int k = 0;
    while (k < 4 && (w >= 2 * dst.w || h >= 2 * dst.h) && ensure(halves_[k], std::max(dst.w, w / 2), std::max(dst.h, h / 2), false)) {
        pass(nullptr, &halves_[k], src, {}, none, nullptr, 0, Blend::Replace);
        src = dev_->targetTexture(halves_[k].id), w = halves_[k].w, h = halves_[k].h, ++k;
    }
    pass(nullptr, &dst, src, {}, none, nullptr, 0, Blend::Replace);
}

void ScreenFx::toQuarter(gfx::TextureId sceneColor) {
    // GH3 draws the venue at 1280 x 720 and its effects at a quarter of
    // that (320 x 180), their blurs fixed in those texels [I: the 360's
    // frame buffer]: the port's quarter is that size whatever the window,
    // so a blur covers the same part of the picture at any resolution.
    if (sceneQ_.w == quarterA_.w && sceneQ_.h == quarterA_.h) {
        downsample4x(sceneColor, quarterA_);
        return;
    }
    downsample4x(sceneColor, sceneQ_);
    resample(dev_->targetTexture(sceneQ_.id), sceneQ_.w, sceneQ_.h, quarterA_);
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
    // m_filterSampleWeights: gh4's c64..c70 (GH3: c0..c6) [C data].
    gh3shader::Globals g;
    for (int i = 0; i < 7; ++i) {
        const float wi = gain * g7[i] / sum;
        g.set(64 + i, wi, wi, wi, wi);
    }
    pass(h, &tmp, dev_->targetTexture(t.id), {}, g, nullptr, 0, Blend::Replace, true);
    pass(v, &t, dev_->targetTexture(tmp.id), {}, g, nullptr, 0, Blend::Replace, true);
}

void ScreenFx::dofBlur(const Target& t, const Target& tmp, float radius) {
    // 823CAD48: _2D_GaussianBlur7x7 (m_filterSampleOffsets c168..174,
    // m_filterSampleWeights c0..6) H then V; r = max(radius, 0.5), rho =
    // sqrt(r / 3); the centre G(0) and three merged pairs (2k - 1, 2k):
    // weight G(2k-1) + G(2k) at +-(2k - 1 + G(2k) / w) texels; not
    // normalised; written ONE, ZERO. Then (ceil(r) + 5) / 6 - 1 further
    // passes, each 6 texels further out (pairs 6p + 2k - 1, 6p + 2k, no
    // centre), added with SRC_ALPHA, ONE: only Enhanced's stronger bokeh
    // (radius > 6) needs them [C; I: FUN_82490b70 as ceil].
    const gh3shader::Program* p = gh3_.get(filter_, "_2D_GaussianBlur7x7");
    if (!p) return;
    const float r = std::max(radius, 0.5f), rho = std::sqrt(r / 3.0f);
    const int passes = std::max(1, (int(std::ceil(r)) + 5) / 6);
    for (int dir = 0; dir < 2; ++dir) {
        const Target& src = dir == 0 ? t : tmp;
        const Target& dst = dir == 0 ? tmp : t;
        for (int k = 0; k < passes; ++k) {
            const float base = 6.0f * float(k);
            float off[7];
            off[0] = 0;
            float wt[7];
            wt[0] = k == 0 ? gauss(0, rho) : 0.0f;
            for (int j = 1; j <= 3; ++j) {
                const float x1 = base + float(2 * j - 1), x2 = base + float(2 * j), ww = gauss(x1, rho) + gauss(x2, rho);
                const float o = ww > 0 ? x1 + gauss(x2, rho) / ww : x1;
                off[2 * j - 1] = o, off[2 * j] = -o;
                wt[2 * j - 1] = wt[2 * j] = ww;
            }
            // gh4: m_filterSampleWeights c64.., m_filterSampleOffsets c80.. (GH3: c0.., c168..) [C data].
            gh3shader::Globals g;
            for (int i = 0; i < 7; ++i) {
                const float ox = dir == 0 ? off[i] / float(src.w) : 0.0f, oy = dir == 1 ? off[i] / float(src.h) : 0.0f;
                g.set(80 + i, ox, oy, 0, 0);
                g.set(64 + i, wt[i], wt[i], wt[i], wt[i]);
            }
            pass(p, &dst, dev_->targetTexture(src.id), {}, g, nullptr, 0, k == 0 ? Blend::Replace : Blend::Add);
        }
    }
}

void ScreenFx::end(const Params& p, gfx::TargetId into) {
    if (!dev_) return;
    // GD10_SCREENFX_TIME=1 (development aid): the chain's CPU time a frame, averaged over 600 frames.
    static const bool timeLog = SDL_getenv("GD10_SCREENFX_TIME") != nullptr;
    struct Timer {
        bool on;
        uint64_t t0;
        ~Timer() {
            if (!on) return;
            static uint64_t sum = 0, n = 0;
            sum += SDL_GetTicksNS() - t0;
            if (++n == 600) SDL_Log("screen fx: %.3f ms CPU a frame (600 frames)", double(sum) / 600e6), sum = 0, n = 0;
        }
    } timer{timeLog, timeLog ? SDL_GetTicksNS() : 0};
    // Into the port's buffer (Enhanced's chain reads it), scene-sized; else the viewport begin() saw.
    const gfx::TargetId realOuter = outer_;
    const int rx = ox_, ry = oy_, rw = ow_, rh = oh_;
    if (into) outer_ = into, ox_ = oy_ = 0, ow_ = scene_.w, oh_ = scene_.h;
    // 823DE658: the instances in the order the scripts added them (venue_DOF,
    // the TOD's BnS, sbloom), each reading the previous one's output [C].
    gfx::TextureId cur = dev_->targetTexture(scene_.id);
    const Target* curT = &scene_;
    // The effects' quarter: 180 rows (a quarter of GH3's 720), the scene's aspect [I: see toQuarter].
    const int qh = 180, qw = std::max(1, int(std::lround(180.0 * scene_.w / std::max(1, scene_.h))));
    const bool quarters = ensure(quarterA_, qw, qh, false) && ensure(quarterB_, qw, qh, false) &&
                          ensure(sceneQ_, std::max(1, scene_.w / 4), std::max(1, scene_.h / 4), false);
    const bool copies = ensure(copyA_, scene_.w, scene_.h, false) && ensure(copyB_, scene_.w, scene_.h, false);
    auto next = [&]() -> const Target& { return curT == &copyA_ ? copyB_ : copyA_; };
    gh3shader::Globals none;

    if (p.uber) {
        endUber(p, cur, curT, quarters, copies);
        dev_->setTarget(outer_);
        dev_->setViewport(ox_, oy_, ow_, oh_);
        outer_ = realOuter, ox_ = rx, oy_ = ry, ow_ = rw, oh_ = rh;
        return;
    }
    // dof (824440F8): the colour blurred at a quarter of the size (two
    // _2D_DownSample4x for sixteenth; half: GH3's own half pass, 823CB7C8,
    // here the quarter's [I]: no camera the scripts reach uses it), then
    // lerp(scene, blur, a), a = strength x saturate((z - front x far) /
    // ((back - front) x far)) (_2D_DepthOfFieldNear: the depth point
    // sampled, the blur bilinear; SRC_ALPHA, ONE_MINUS_SRC_ALPHA) [C].
    const Dof& d = p.dof;
    if (d.on && d.strength > 0 && quarters && copies) {
        toQuarter(cur);
        const Target* blurA = &quarterA_;
        const Target* blurB = &quarterB_;
        if (d.resolution >= 2 && ensure(sixteenA_, std::max(1, qw / 4), std::max(1, qh / 4), false) &&
            ensure(sixteenB_, std::max(1, qw / 4), std::max(1, qh / 4), false)) {
            downsample4x(dev_->targetTexture(quarterA_.id), sixteenA_);
            blurA = &sixteenA_, blurB = &sixteenB_;
        }
        dofBlur(*blurA, *blurB, d.blurRadius);
        const Target& out = next();
        pass(nullptr, &out, cur, {}, none, nullptr, 0, Blend::Replace);
        const gh3shader::Program* comp = gh3_.get(immediate_, "_2D_DepthOfFieldNear");
        float pc[5][4] = {};
        // m_vInvProjMatT (gh4 c0; GH3 c53): viewZ = 1 / (depth x z + w) [C]; this depth buffer's projection.
        const float n = p.nearZ, f = p.farZ;
        pc[0][2] = -(f - n) / (f * n), pc[0][3] = 1.0f / n;
        // m_vDepthOfFieldParams (gh4 c4; GH3 c54): (-, strength, (back - front) x far, front x far) [C].
        const float scale = d.metres ? 1.0f : p.cameraFar;
        pc[4][1] = d.strength, pc[4][2] = std::max(0.01f, d.backDist - d.frontDist) * scale, pc[4][3] = d.frontDist * scale;
        if (comp) pass(comp, &out, dev_->targetDepth(scene_.id), dev_->targetTexture(blurA->id), none, pc, 5, Blend::Alpha, true);
        cur = dev_->targetTexture(out.id), curT = &out;
    }
    // bright_sat (8243E5F0): out = (rgb, 1) x H x S x C x B x M as three
    // dot products (_2D_Tex_BS_NoViewport), alpha = the red result [C].
    for (const BrightSat& bs : p.brightSat) {
        if (!bs.on || !copies) continue;
        const gh3shader::Program* prog = gh3_.get(immediateAlt_, "_2D_Tex_BS_NoViewport");
        if (!prog) continue;
        float m[3][4];
        brightSatMatrix(bs, m);
        // m_ColorTransform0..2: the pixel shader's c0..c2 in gh4 (GH3: c128..c130) [C data].
        const Target& out = next();
        pass(prog, &out, cur, {}, none, m, 3, Blend::Replace);
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
            toQuarter(dev_->targetTexture(scene_.id));
        } else {
            // _2D_Cutoff: the 16-texel mean where its rgb sum exceeds the cutoff colour's [C #2330].
            float pc[1][4] = {{b.cutoff[0], b.cutoff[1], b.cutoff[2], 0}};  // m_sCutoffColor: gh4 c0 (GH3 c54) [C data]
            const bool direct = sceneQ_.w == quarterA_.w && sceneQ_.h == quarterA_.h;
            pass(gh3_.get(immediate_, "_2D_Cutoff"), direct ? &quarterA_ : &sceneQ_, cur, {}, none, pc, 1, Blend::Replace);
            if (!direct) resample(dev_->targetTexture(sceneQ_.id), sceneQ_.w, sceneQ_.h, quarterA_);
        }
        bloomBlur(quarterA_, quarterB_, b.falloff1);
        bloomBlur(quarterA_, quarterB_, 1.0f);
    }
    // The result where the venue would have drawn.
    pass(nullptr, nullptr, cur, {}, none, nullptr, 0, Blend::Replace);
    if (bloom) {
        const gh3shader::Program* comp = gh3_.get(immediateAlt_, "_2D_ComposeBloom0");
        // m_kColor, m_kColorBloom0: gh4's c0 / c1 (GH3: c128 / c129) [C data].
        const float kc[2][4] = {{b.strength[0], b.strength[1], b.strength[2], 1}, {b.bloom1, b.bloom1, b.bloom1, 1}};
        if (comp) pass(comp, nullptr, dev_->targetTexture(quarterA_.id), {}, none, kc, 2, Blend::Add);
    }
    dev_->setTarget(outer_);
    dev_->setViewport(ox_, oy_, ow_, oh_);
    outer_ = realOuter, ox_ = rx, oy_ = ry, ow_ = rw, oh_ = rh;
}

void ScreenFx::passN(const gh3shader::Program* prog, const Target* dst, const gfx::TextureId* tex, int n, const gh3shader::Globals& g,
                     const float (*vs)[4], int vsCount, const float (*ps)[4], int psCount, uint32_t pointMask) {
    if (dst) {
        dev_->setTarget(dst->id);
        dev_->setViewport(0, 0, dst->w, dst->h);
    } else {
        dev_->setTarget(outer_);
        dev_->setViewport(ox_, oy_, ow_, oh_);
    }
    gfx::DrawCall c;
    c.program = prog->id;
    c.state.alphaWrite = true;
    c.layout = &gh3shader::vertexLayout();
    c.vertices = quad_;
    c.count = 6;
    for (int k = 0; k < n && k < gfx::kMaxTextureSlots; ++k) {
        c.textures[k] = tex[k] ? tex[k] : black_;
        // sys_noise and sys_inkblot repeat (their UVs run past 1) [I: the sampler states are the material's].
        const gfx::Wrap wrap = k == 0 || k == 6 ? gfx::Wrap::Repeat : gfx::Wrap::Clamp;
        c.samplers[k] = {(pointMask >> k & 1) ? gfx::Filter::Nearest : gfx::Filter::Linear, wrap, 0};
    }
    static gh3shader::MaterialConstants mc;
    mc = gh3shader::MaterialConstants{};
    for (int i = 0; i < vsCount && i < 63; ++i) std::memcpy(mc.vs[i], vs[i], sizeof mc.vs[i]);
    for (int i = 0; i < psCount && i < 63; ++i) std::memcpy(mc.ps[i], ps[i], sizeof mc.ps[i]);
    mc.ps[63][0] = -1.0f, mc.vs[63][0] = -1.0f;
    static gh3shader::ObjectConstants oc;
    c.uniforms[0] = {g.c, sizeof g.c, 0};
    c.uniforms[1] = {mc.vs, sizeof mc.vs, 0};
    c.uniforms[2] = {mc.ps, sizeof mc.ps, 0};
    c.uniforms[3] = {oc.c, sizeof oc.c, 0};
    dev_->draw(c);
}

void ScreenFx::endUber(const Params& p, gfx::TextureId& cur, const Target*& curT, bool quarters, bool copies) {
    gh3shader::Globals none;
    const int qw = quarterA_.w, qh = quarterA_.h;
    // bloom2 (slot 1: the uber's bloom1): the stencil-marked materials
    // (materialfilter; _Bloom_SubtractModulateStencil reads the converted
    // stencil) or the whole picture through the colour matrix
    // colormodulate x (saturation(rgb) - subtract), at a quarter of the
    // size, blurred over `thickness` [C: the shaders, their constants'
    // names; I: the matrix's values, the blur, the size].
    const Bloom& b = p.bloom;
    bool bloom = b.on && b.gh4 && quarters && copies && ensure(bloomA_, qw, qh, false) && ensure(bloomB_, qw, qh, false);
    const gh3shader::Program* extract = bloom ? gh3_.get(qbkey::key("_Bloom_SubtractModulate")) : nullptr;
    bloom = bloom && extract;
    if (bloom) {
        gfx::TextureId src = cur;
        if (b.materialFilter) {
            if (curT == &scene_) {
                const Target& out = curT == &copyA_ ? copyB_ : copyA_;
                pass(nullptr, &out, cur, {}, none, nullptr, 0, Blend::Replace);
                cur = dev_->targetTexture(out.id), curT = &out;
            }
            dev_->setTarget(scene_.id);
            dev_->setViewport(0, 0, scene_.w, scene_.h);
            const float zero[4] = {0, 0, 0, 0};
            dev_->clear(zero, false);
            pass(gh3_.get(immediate_, "_2D_Tex_NoViewport"), &scene_, cur, {}, none, nullptr, 0, Blend::Replace, false, true);
            src = dev_->targetTexture(scene_.id);
        }
        toQuarter(src);
        // The rows: out_i = mod_i x (sum_j S_ij rgb_j - sub_i), S the
        // saturation matrix (bright_sat's weights) [I]; c0..c2 the rows,
        // c3 the constant column (the non-stencil variant's layout: out =
        // r c0 + g c1 + b c2 + c3) [C shader].
        const float sw[3] = {0.3086f, 0.6094f, 0.0820f}, sat = b.saturation;
        float m[4][4] = {};
        for (int i = 0; i < 3; ++i) {  // output channel
            const float mod = b.strength[i];
            for (int j = 0; j < 3; ++j) m[j][i] = mod * ((1 - sat) * sw[j] + (i == j ? sat : 0));
            m[3][i] = -mod * b.cutoff[i];
        }
        m[3][3] = 1;
        const gfx::TextureId q = dev_->targetTexture(quarterA_.id);
        passN(extract, &bloomA_, &q, 1, none, nullptr, 0, m, 4, 0);
        // thickness: the blur's reach in 1280 x 720 pixels, here quarter texels [I].
        dofBlur(bloomA_, bloomB_, std::max(0.5f, b.thickness / 4.0f));
    }
    // dof3 (slot 2: dof1): the colour blurred at a quarter (as GH3's dof) [I: the blur].
    const Dof& d = p.dof;
    const bool dof = d.on && d.strength > 0 && quarters;
    if (dof) {
        toQuarter(cur);
        dofBlur(quarterA_, quarterB_, d.blurRadius);
    }
    const bool noise = p.noise.on && p.noise.intensity > 0 && noiseTex_;
    const BrightSat& bs = p.brightSat[0];
    if (p.vignette.on && !sysVignette_) makeVignette(p.vignette);
    const bool vignette = p.vignette.on && vignetteTex_, ink = p.ink.on && inkTex_;
    std::string parts;
    if (bloom) parts += "_bloom1";
    if (noise) parts += "_noise1";
    if (bs.on) parts += "_color1";
    if (vignette) parts += "_vignette1";
    if (ink) parts += "_inkblot1";
    const gh3shader::Program* uber = gh3_.get(uberKey(parts, dof));
    if (!uber) {
        pass(nullptr, nullptr, cur, {}, none, nullptr, 0, Blend::Replace);
        return;
    }
    // Vertex: g_UV c1, g_Random c2 (noise UV = (uv + c2.xy) x c1.xy + c2.zw),
    // g_InkBlotch_Mat c13..c16 (ink UV = u c13.xy + v c14.xy + c16.xy) [C shader].
    float vs[17][4] = {};
    ++frame_;
    {
        // A new random placement every frame (film grain) [I].
        uint32_t h = frame_ * 2654435761u;
        auto rnd = [&h]() {
            h ^= h << 13, h ^= h >> 17, h ^= h << 5;
            return float(h & 0xFFFFFF) / float(0x1000000);
        };
        vs[1][0] = p.noise.uv[0], vs[1][1] = p.noise.uv[1];
        vs[2][0] = rnd(), vs[2][1] = rnd(), vs[2][2] = rnd(), vs[2][3] = rnd();
    }
    if (ink) {
        // A new placement every intervala..intervalb seconds: rotation
        // rotationa..rotationb degrees, offset random, xtiling / ytiling [I].
        const double now = double(frame_) / 60.0;
        if (now >= inkNext_) {
            uint32_t h = frame_ * 747796405u + 2891336453u;
            auto rnd = [&h]() {
                h ^= h << 13, h ^= h >> 17, h ^= h << 5;
                return float(h & 0xFFFFFF) / float(0x1000000);
            };
            const InkBlotch& k = p.ink;
            inkNext_ = now + k.intervalA + (k.intervalB - k.intervalA) * rnd();
            const float a = (k.rotA + (k.rotB - k.rotA) * rnd()) * 3.14159265f / 180.0f, c = std::cos(a), sn = std::sin(a);
            inkMat_[0][0] = c * k.xTiling, inkMat_[0][1] = sn * k.yTiling;
            inkMat_[1][0] = -sn * k.xTiling, inkMat_[1][1] = c * k.yTiling;
            inkMat_[2][0] = rnd(), inkMat_[2][1] = rnd();
        }
        for (int k = 0; k < 2; ++k) vs[13][k] = inkMat_[0][k], vs[14][k] = inkMat_[1][k], vs[16][k] = inkMat_[2][k];
    }
    // Pixel: g_Color c3, g_ColorTransform0..2 c4..c6, g_VignetteColor c8,
    // g_DOF0 c9, g_DOF_Strength c10, g_ScratchColor c11, g_BlotchColor c12;
    // g_invProjMat c152 [C shader constant tables].
    float ps[13][4] = {};
    for (int k = 0; k < 4; ++k) ps[3][k] = p.noise.color[k];
    ps[3][3] = p.noise.intensity;  // [I: the lerp toward the noise]
    if (bs.on) {
        float m[3][4];
        brightSatMatrix(bs, m);
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) ps[4 + r][k] = m[r][k];
    }
    for (int k = 0; k < 3; ++k) ps[8][k] = p.vignette.color[k];
    ps[8][3] = p.vignette.alpha;
    // near = (c9.x - z) / c9.y, far = (z - c9.z) / c9.w, a = max of them x c10.x [C shader; I: the values].
    {
        const float nr = d.nearEnd - d.nearBegin, fr = d.backDist - d.frontDist;
        ps[9][0] = d.nearEnd, ps[9][1] = std::fabs(nr) > 1e-4f ? nr : 1e-4f;
        ps[9][2] = d.frontDist, ps[9][3] = std::fabs(fr) > 1e-4f ? fr : 1e-4f;
        if (d.nearEnd <= d.nearBegin) ps[9][0] = -1e6f;  // no near blur
        ps[10][0] = d.strength;
    }
    for (int k = 0; k < 4; ++k) ps[11][k] = p.ink.scratch[k], ps[12][k] = p.ink.blotch[k];
    gh3shader::Globals g;
    const float n = p.nearZ, f = p.farZ;
    g.set(152, 0, 0, -(f - n) / (f * n), 1.0f / n);  // viewZ = 1 / (depth x z + w)
    // Samplers: g_NoiseSampler s0, g_DepthBufferSampler s1, g_BloomEffectSampler
    // s2, g_DofEffectSampler s3, g_FramebufferSampler s4, g_VignetteMaskSampler
    // s5, g_InkBlotSampler s6 [C shader].
    const gfx::TextureId tex[7] = {noiseTex_, dev_->targetDepth(scene_.id), bloom ? dev_->targetTexture(bloomA_.id) : black_,
                                   dof ? dev_->targetTexture(quarterA_.id) : black_, cur, vignetteTex_, inkTex_};
    const bool flash = p.brightSat[1].on && copies;
    const Target* dst = nullptr;
    if (flash) dst = curT == &copyA_ ? &copyB_ : &copyA_;
    passN(uber, dst, tex, 7, g, vs, 17, ps, 13, 1u << 1);
    if (flash) {
        // ScreenFlash's FlashBS: a bright_sat of its own after the TOD's [I: gh4's second bright_sat].
        cur = dev_->targetTexture(dst->id), curT = dst;
        const gh3shader::Program* prog = gh3_.get(immediateAlt_, "_2D_Tex_BS_NoViewport");
        float m[3][4];
        brightSatMatrix(p.brightSat[1], m);
        const Target& out = curT == &copyA_ ? copyB_ : copyA_;
        if (prog) {
            pass(prog, &out, cur, {}, none, m, 3, Blend::Replace);
            cur = dev_->targetTexture(out.id), curT = &out;
        }
        pass(nullptr, nullptr, cur, {}, none, nullptr, 0, Blend::Replace);
    }
}

void ScreenFx::makeVignette(const Vignette& v) {
    // sys_vignettemask has no texture on the disc (global_gfx.scn's material
    // names none) [C data]: the mask made here, alpha x saturate((r - inner)
    // / (outer - inner)), r the distance from the centre with the screen's
    // half extents 1 [I].
    const float key[4] = {v.inner, v.outer, v.alpha, v.aspect ? 1.0f : 0.0f};
    if (vignetteTex_ && std::memcmp(key, vignetteFor_, sizeof key) == 0) return;
    std::memcpy(vignetteFor_, key, sizeof key);
    if (vignetteTex_) dev_->destroyTexture(vignetteTex_);
    constexpr int kSize = 128;
    std::vector<uint8_t> px(size_t(kSize) * kSize * 4);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const float u = (x + 0.5f) / kSize * 2 - 1, w = (y + 0.5f) / kSize * 2 - 1;
            const float r = std::sqrt(u * u + w * w);
            const float a = v.alpha * std::clamp((r - v.inner) / std::max(1e-4f, v.outer - v.inner), 0.0f, 1.0f);
            const uint8_t b = uint8_t(std::lround(std::clamp(a, 0.0f, 1.0f) * 255));
            uint8_t* o = &px[(size_t(y) * kSize + size_t(x)) * 4];
            o[0] = o[1] = o[2] = o[3] = b;
        }
    gfx::TextureDesc d;
    d.width = d.height = kSize;
    gfx::TexLevel l;
    l.data = px.data(), l.size = px.size();
    std::string err;
    vignetteTex_ = dev_->createTexture(d, &l, err);
}

struct ScreenFx::SysTextures {
    std::future<std::vector<gh3tex::Texture>> job;  // noise, vignette mask, inkblot (key 0: missing)
};

void ScreenFx::startSysTextures() {
    const Disc* disc = gh3shader::libraryDisc();
    sys_ = std::make_shared<SysTextures>();
    sysDone_ = false;
    if (!disc) {
        sysDone_ = true;
        return;
    }
    // global_gfx.scn's materials sys_noise_sys_noise, sys_vignettemask_sys_vignettemask,
    // sys_inkblot_sys_inkblot: their first texture in global_gfx.tex [C data].
    sys_->job = std::async(std::launch::async, [disc]() {
        std::vector<gh3tex::Texture> out(3);
        try {
            const auto pak = Pak::shared(*disc, "DATA/COMPRESSED/ZONES/global.pak.xen");
            const uint32_t want[3] = {qbkey::key("sys_noise_sys_noise"), qbkey::key("sys_vignettemask_sys_vignettemask"),
                                      qbkey::key("sys_inkblot_sys_inkblot")};
            uint32_t texKey[3] = {0, 0, 0};
            for (const PakEntry& e : pak->entries())
                if (e.type == qbkey::key(".scn")) {
                    auto [d, size] = pak->data(e);
                    try {
                        for (const scene::Material& m : scene::parse(d, size, scene::Kind::Scn).materials)
                            for (int k = 0; k < 3; ++k)
                                if (m.key == want[k] && !m.textures.empty()) texKey[k] = m.textures.front();
                    } catch (const std::exception&) {
                    }
                }
            for (const PakEntry& e : pak->entries())
                if (e.type == qbkey::key(".tex")) {
                    auto [d, size] = pak->data(e);
                    for (gh3tex::Texture& t : gh3tex::decodeTexDict(d, size))
                        for (int k = 0; k < 3; ++k)
                            if (texKey[k] && t.key == texKey[k] && t.supported) out[size_t(k)] = std::move(t);
                }
        } catch (const std::exception& ex) {
            SDL_Log("screen fx: sys textures: %s", ex.what());
        }
        return out;
    });
}

bool ScreenFx::pollSysTextures() {
    if (sysDone_) return true;
    if (!sys_ || !sys_->job.valid()) return sysDone_ = true;
    if (sys_->job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    std::vector<gh3tex::Texture> t = sys_->job.get();
    std::string err;
    gfx::TextureId* slot[3] = {&noiseTex_, &vignetteTex_, &inkTex_};
    for (size_t k = 0; k < 3; ++k) {
        if (!t[k].key || t[k].levels.empty()) {
            if (k != 1) SDL_Log("screen fx: sys texture %zu missing", k);
            continue;
        }
        const std::vector<gfx::TexLevel> lv = t[k].texLevels();
        *slot[k] = dev_->createTexture(t[k].desc(), lv.data(), err);
        if (k == 1) sysVignette_ = bool(vignetteTex_);
    }
    {
        const uint8_t px[4] = {0, 0, 0, 0};
        gfx::TextureDesc d;
        d.width = d.height = 1;
        gfx::TexLevel l{};
        l.data = px, l.size = 4;
        black_ = dev_->createTexture(d, &l, err);
    }
    return sysDone_ = true;
}

gfx::TextureId ScreenFx::depthSnapshot() {
    if (!dev_ || !scene_.id) return {};
    if (!depthCopy_.id || depthCopy_.w != scene_.w || depthCopy_.h != scene_.h) {
        if (depthCopy_.id) dev_->destroyTarget(depthCopy_.id);
        depthCopy_ = {};
        std::string err;
        gfx::TargetDesc d;
        d.width = scene_.w, d.height = scene_.h, d.depth = true, d.stencil = true;  // the scene's D24S8
        depthCopy_.id = dev_->createTarget(d, err);
        if (!depthCopy_.id) return {};
        depthCopy_.w = scene_.w, depthCopy_.h = scene_.h;
        dev_->setTarget(scene_.id);  // createTarget unbinds
        dev_->setViewport(0, 0, scene_.w, scene_.h);
    }
    dev_->copyDepth(scene_.id, depthCopy_.id);
    return dev_->targetDepth(depthCopy_.id);
}

namespace {

float num(const qb::Struct& s, const char* name, float def) {
    const qb::Value* v = s.find(qbkey::key(name));
    return v && (v->type == qb::Type::Float || v->type == qb::Type::Int) ? v->number() : def;
}

// An [r, g, b] array of 0..255 as 0..1 (Function_8241BC20: x DAT_82007b38) [C].
void rgb(const qb::Struct& s, const char* name, float out[3], float scale) {
    const qb::Value* v = s.find(qbkey::key(name));
    if (!v || v->type != qb::Type::Array || !v->arr) return;
    for (size_t k = 0; k < 3 && k < v->arr->values.size(); ++k) out[k] = v->arr->values[k].number() * scale;
}

const qb::Value* screenFx(const Globals& g, uint32_t manager) {
    const qb::Value* m = g.find(manager);
    if (!m || m->type != qb::Type::Struct || !m->st) return nullptr;
    const qb::Value* fx = m->st->find(qbkey::key("screen_FX"));
    return fx && fx->type == qb::Type::Array && fx->arr ? fx : nullptr;
}

}  // namespace

// The field offsets: Function_8241BC20 (simple_bloom), Function_8241EE68 (dof) [C data tod_profiles.qb].
void readEntry(const qb::Struct& e, Params& p, int& brightSlot) {
    const qb::Value* type = e.find(qbkey::key("Type"));  // gh4 writes `type`: the same key
    if (!type) return;
    const bool on = num(e, "ON", 1) > 0;
    if (type->key == qbkey::key("simple_bloom")) {
        Bloom& b = p.bloom;
        b.on = on;
        rgb(e, "color", b.cutoff, 1.0f / 255.0f);
        rgb(e, "strength", b.strength, 1.0f / 255.0f);
        b.bloom1 = num(e, "bloom1", 0), b.bloom2 = num(e, "bloom2", 0);
        b.falloff1 = num(e, "falloff1", 0), b.falloff2 = num(e, "falloff2", 0);
        b.materialFilter = num(e, "materialfilter", 0) > 0;
        b.autoLuminance = num(e, "autoluminancemod", 0) > 0;
        b.distanceMod = num(e, "distancemod", 0);
    } else if (type->key == qbkey::key("dof")) {
        Dof& d = p.dof;
        d.on = on;
        d.backDist = num(e, "backdist", 1), d.frontDist = num(e, "frontdist", 0);
        d.strength = num(e, "strength", 0), d.blurRadius = num(e, "blurradius", 0);
        // blurresolution: half 0, sixteenth 2, quarter 1 (8241F160) [C].
        if (const qb::Value* r = e.find(qbkey::key("blurresolution")))
            d.resolution = r->key == qbkey::key("half") ? 0 : r->key == qbkey::key("sixteenth") ? 2 : r->key == qbkey::key("quarter") ? 1 : d.resolution;
    } else if (type->key == qbkey::key("dof3")) {
        // gh4 (tod_profiles.qb's Default_TOD_Manager and the DOF_*_tod_manager
        // cameras' entries) [C data]: the uber pass's dof1 (near and far) or,
        // without an uber entry, GH3's far blur; the blur GH3's quarter-size
        // 7x7 at 3 texels [I].
        Dof& d = p.dof;
        d.on = on;
        d.metres = true;
        d.strength = num(e, "strength", 0);
        d.frontDist = num(e, "farblurbegin", 10), d.backDist = num(e, "farblurend", 20);
        d.nearBegin = num(e, "nearblurbegin", 0), d.nearEnd = num(e, "nearblurend", 0);
        d.blurRadius = 3.0f, d.resolution = 1;
    } else if (type->key == qbkey::key("bloom2")) {
        // gh4's bloom2 (parsed by 825074F8 [C: the keys]): materialfilter,
        // subtract, colormodulate, saturation, thickness, equation [C data];
        // the uber pass's bloom1 (ScreenFx::endUber). colormodulate's fourth
        // value scales the three [I]; equation (eq0 everywhere) not applied.
        Bloom& b = p.bloom;
        b.on = on;
        b.gh4 = true;
        rgb(e, "subtract", b.cutoff, 1.0f);
        rgb(e, "colormodulate", b.strength, 1.0f);
        if (const qb::Value* cm = e.find(qbkey::key("colormodulate")); cm && cm->type == qb::Type::Array && cm->arr && cm->arr->values.size() > 3)
            for (float& v : b.strength) v *= cm->arr->values[3].number();
        b.bloom1 = 1, b.bloom2 = 0, b.falloff1 = 1, b.falloff2 = 0;
        b.materialFilter = num(e, "materialfilter", 0) > 0;
        b.saturation = num(e, "saturation", 1), b.thickness = num(e, "thickness", 0);
    } else if (type->key == qbkey::key("noise2")) {
        p.noise.on = on;
        p.noise.intensity = num(e, "intensity", 0);
        float c[4] = {0.5f, 0.5f, 0.5f, 0.5f};
        if (const qb::Value* v = e.find(qbkey::key("color")); v && v->type == qb::Type::Array && v->arr)
            for (size_t k = 0; k < 4 && k < v->arr->values.size(); ++k) c[k] = v->arr->values[k].number();
        std::memcpy(p.noise.color, c, sizeof c);
        if (const qb::Value* v = e.find(qbkey::key("uv")); v && v->type == qb::Type::Array && v->arr && v->arr->values.size() >= 2)
            p.noise.uv[0] = v->arr->values[0].number(), p.noise.uv[1] = v->arr->values[1].number();
    } else if (type->key == qbkey::key("vignette")) {
        Vignette& v = p.vignette;
        v.on = on;
        v.aspect = num(e, "aspectcorrection", 0) > 0;
        v.inner = num(e, "inner_radius", 0.5f), v.outer = num(e, "outer_radius", 1), v.alpha = num(e, "alpha", 0.5f);
        rgb(e, "color", v.color, 1.0f);
    } else if (type->key == qbkey::key("inkblotch")) {
        InkBlotch& k = p.ink;
        k.on = on;
        auto four = [&](const char* n, float out[4]) {
            if (const qb::Value* v = e.find(qbkey::key(n)); v && v->type == qb::Type::Array && v->arr)
                for (size_t i = 0; i < 4 && i < v->arr->values.size(); ++i) out[i] = v->arr->values[i].number();
        };
        four("scratchcolor", k.scratch), four("blotchcolor", k.blotch);
        k.intervalA = num(e, "intervala", 0.06f), k.intervalB = num(e, "intervalb", 0.09f);
        k.xTiling = num(e, "xtiling", 1), k.yTiling = num(e, "ytiling", 1);
        k.rotA = num(e, "rotationa", 0), k.rotB = num(e, "rotationb", 0);
    } else if (type->key == qbkey::key("godray")) {
        p.godray = on;
    } else if (type->key == qbkey::key("uberscreenfx")) {
        p.uber = true;
    } else if (type->key == qbkey::key("bright_sat") && brightSlot < 2) {
        BrightSat& bs = p.brightSat[brightSlot++];
        bs.on = on;
        bs.contrast = num(e, "Contrast", 1), bs.brightness = num(e, "Brightness", 1);
        bs.saturation = num(e, "saturation", 1), bs.hue = num(e, "hue", 1);
        rgb(e, "red_mix", bs.mix[0], 1), rgb(e, "green_mix", bs.mix[1], 1), rgb(e, "blue_mix", bs.mix[2], 1);
        // gh4 writes each mix as three floats (red_mix_r, red_mix_g, ...) [C data].
        const char* rows[3] = {"red_mix_", "green_mix_", "blue_mix_"};
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3; ++k) bs.mix[r][k] = num(e, (std::string(rows[r]) + "rgb"[k]).c_str(), bs.mix[r][k]);
    }
}

void readDof(const Globals& g, uint32_t manager, Params& p) {
    const qb::Value* fx = screenFx(g, manager ? manager : qbkey::key("DOF_Off_TOD_Manager"));
    if (!fx || fx->arr->values.empty()) return;
    if (const qb::Value& e = fx->arr->values.front(); e.type == qb::Type::Struct && e.st) {
        int slot = 2;
        readEntry(*e.st, p, slot);
    }
}

void readViewport(const Globals& g, uint32_t manager, Params& p) {
    p = Params{};
    readDof(g, 0, p);
    if (const qb::Value* fx = screenFx(g, manager)) {
        int slot = 0;
        for (const qb::Value& e : fx->arr->values)
            if (e.type == qb::Type::Struct && e.st) readEntry(*e.st, p, slot);
    }
}

uint32_t effectManager(const Globals& g, uint32_t effect) {
    // ApplyScreenFx's switch -> TOD_Proxim_Apply_<effect> -> its fxParam;
    // BW_Contrast and vignette give Metal_Vignette with
    // METALLICA_WORSTCASE_SCREENFX = 1, GH4_OldFX with APPLY_GH4_FX = 1;
    // anything else (none, default) TOD_Proxim_Reapply_MetallicaFX:
    // Default_TOD_Manager [C scripts].
    static const std::map<uint32_t, const char*> kManagers = [] {
        std::map<uint32_t, const char*> m;
        const char* pairs[][2] = {{"BW_Contrast", "Metal_BW_Contrast_tod_manager"},
                                  {"BW_Grainy", "Metal_BW_Grainy_tod_manager"},
                                  {"vignette", "Metal_Vignette_tod_manager"},
                                  {"CoolBlue", "Metal_BlueTint_tod_manager"},
                                  {"HotBlue", "Metal_HotBlue_tod_manager"},
                                  {"Sepia", "Metal_Sepia_tod_manager"},
                                  {"yellow", "Metal_Yellow_tod_manager"},
                                  {"green", "Metal_Green_tod_manager"},
                                  {"Forest", "Metal_Forest_tod_manager"},
                                  {"DesatRed", "Metal_DesatRed_tod_manager"},
                                  {"InkBlotch_Red", "Metal_InkBlotch_tod_manager"},
                                  {"InkBlotch_TurqPink", "Metal_TurqPink_tod_manager"},
                                  {"BloomSat", "Metal_BloomSat_tod_manager"},
                                  {"InkBlotch2", "Metal_BW_InkBlotch_tod_manager"},
                                  {"Noisy", "Metal_NoisyDefault_tod_manager"},
                                  {"Inkblotch3", "Metal_InkBlotch3_tod_manager"},
                                  {"WarmStreaks", "Metal_SatRedStreaks_tod_manager"},
                                  {"CoolStreaks", "Metal_TurqPinkStreaks_tod_manager"},
                                  {"BW_Splots", "Metal_BW_Splots_tod_manager"},
                                  {"BW_Streaks", "Metal_BW_Streaks_tod_manager"},
                                  {"DesatSplots_01", "Metal_DesatSplots_01_tod_manager"},
                                  {"ContrastyVignette", "Metal_ContrastyVignette_tod_manager"},
                                  {"BW_Grungy", "Metal_BW_Grungy_tod_manager"},
                                  {"BW_Grainy2", "Metal_BW_Grainy2_tod_manager"}};
        for (const auto& pr : pairs) m[qbkey::key(pr[0])] = pr[1];
        return m;
    }();
    auto it = kManagers.find(effect);
    if (it == kManagers.end()) return qbkey::key("Default_TOD_Manager");
    if (effect == qbkey::key("BW_Contrast") || effect == qbkey::key("vignette")) {
        auto flag = [&g](const char* n) {
            const qb::Value* v = g.find(qbkey::key(n));
            return v && (v->type == qb::Type::Int || v->type == qb::Type::Float) && v->number() == 1;
        };
        if (flag("METALLICA_WORSTCASE_SCREENFX")) return qbkey::key("Metal_Vignette_tod_manager");
        if (flag("APPLY_GH4_FX")) return qbkey::key("GH4_OldFX_tod_manager");
    }
    return qbkey::key(it->second);
}

void readShot(const Globals& g, uint32_t effect, uint32_t dof, Params& p) {
    // ApplyDeferredCameraEffects: ApplyScreenFx effect = next_screenfx (the
    // whole viewport list, TOD_Proxim_Update_LightFX_Viewport), then the
    // shot's dof updates Depth_of_Field__simplified_ [C script].
    static std::mutex mutex;
    static const Globals* cachedFor = nullptr;
    static std::map<uint32_t, Params> cache;
    // GD10_SCREENFX=<effect> (development aid): every shot takes that effect (e.g. InkBlotch_Red).
    static const char* forced = SDL_getenv("GD10_SCREENFX");
    if (forced && *forced) effect = qbkey::key(forced);
    const uint32_t manager = effectManager(g, effect);
    static uint32_t lastManager = 0;
    static const bool camLog = SDL_getenv("GD10_CAMLOG") != nullptr;
    if (camLog && manager != lastManager) SDL_Log("screen fx: %s", qbkey::names().get(manager).c_str());
    lastManager = manager;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (cachedFor != &g) cache.clear(), cachedFor = &g;
        auto it = cache.find(manager);
        if (it == cache.end()) {
            Params q;
            readViewport(g, manager, q);
            it = cache.emplace(manager, q).first;
        }
        const float n = p.nearZ, f = p.farZ, cf = p.cameraFar;
        p = it->second;
        p.nearZ = n, p.farZ = f, p.cameraFar = cf;
    }
    readDof(g, dof, p);
}

}  // namespace screenfx
