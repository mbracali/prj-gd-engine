// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// gdcore_render_selftest [--backend opengl3|sdl3|vulkan]: draws into off-screen
// targets through gd::core::render and checks the pixels (docs/render.md
// section 7). Headless: SDL_VIDEO_DRIVER=offscreen. Exit status: the number of
// failed checks; 77 when the SDL3 GPU or Vulkan backend cannot start here (no
// loader or device; SDL3 GPU on the offscreen video driver, which cannot
// present: ctest counts it as skipped).

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "common/spirv_reflect.h"
#include "gd/core/render/device.h"

using namespace gd::core::render;

namespace {

int g_failed = 0, g_passed = 0;

void check(bool ok, const char* what) {
    if (ok) ++g_passed;
    else ++g_failed;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
}

bool near(const uint8_t* px, int r, int g, int b, int a, int tol = 2) {
    return std::abs(px[0] - r) <= tol && std::abs(px[1] - g) <= tol && std::abs(px[2] - b) <= tol && (a < 0 || std::abs(px[3] - a) <= tol);
}

// A position (clip space) and a colour per vertex; a uniform offset and tint.
const char* kColorVs = R"(
GD_ATTR(0) vec2 aPos;
GD_ATTR(1) vec4 aColor;
GD_UNIFORMS(0, Params) { vec4 uOffset; vec4 uTint; };
GD_OUT(0) vec4 vColor;
void main() {
    vColor = aColor * uTint;
    gl_Position = gd_clip(vec4(aPos + uOffset.xy, uOffset.z, 1.0));
}
)";
const char* kColorFs = R"(
GD_IN(0) vec4 vColor;
GD_UNIFORMS(1, Extra) { vec4 uAdd; };
GD_FRAG_OUT(0) vec4 fragColor;
void main() { fragColor = vColor + uAdd; }
)";

// A full-target quad from the vertex id, sampling slot 0 with uv (0,0) at the top left.
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
const char* kCubeFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLERCUBE(0, uCube);
GD_FRAG_OUT(0) vec4 fragColor;
void main() { fragColor = texture(uCube, vec3(1.0, vUv.y * 0.1, vUv.x * 0.1)); }
)";
// The depth texture, read texel by texel (a depth probe).
const char* kDepthFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uDepth);
GD_FRAG_OUT(0) vec4 fragColor;
void main() { fragColor = vec4(texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r); }
)";

}  // namespace

// The same shaders as SPIR-V, compiled at build time by gdcore_shaderc
// (gdcore_spirv_shaders in CMakeLists.txt): k<Name>VsSpirv / k<Name>FsSpirv.
#include "render_selftest_spirv.h"

namespace {

// What the build-time SPIR-V binds (docs/render.md 3.2): SDL_GPU's sets, the
// slots the dialect's macros name.
void checkSpirv() {
    struct Expect {
        const char* name;
        const uint32_t* words;
        size_t bytes;
        SpirvStage stage;
        uint32_t samplers, uniforms;
    } expect[] = {
        {"kColorVs", kColorVsSpirv, sizeof kColorVsSpirv, SpirvStage::Vertex, 0, 1u << 0},
        {"kColorFs", kColorFsSpirv, sizeof kColorFsSpirv, SpirvStage::Fragment, 0, 1u << 1},
        {"kQuadVs", kQuadVsSpirv, sizeof kQuadVsSpirv, SpirvStage::Vertex, 0, 0},
        {"kQuadFs", kQuadFsSpirv, sizeof kQuadFsSpirv, SpirvStage::Fragment, 1u << 0, 0},
        {"kCubeFs", kCubeFsSpirv, sizeof kCubeFsSpirv, SpirvStage::Fragment, 1u << 0, 0},
        {"kDepthFs", kDepthFsSpirv, sizeof kDepthFsSpirv, SpirvStage::Fragment, 1u << 0, 0},
    };
    bool ok = true;
    for (const Expect& e : expect) {
        SpirvStageInfo info;
        std::string why;
        const bool good = reflectSpirv(e.words, e.bytes, info, why) && info.stage == e.stage && info.samplerMask == e.samplers &&
                          info.uniformMask == e.uniforms;
        if (!good) std::printf("  %s: %s (samplers %x, uniforms %x)\n", e.name, why.c_str(), info.samplerMask, info.uniformMask);
        ok = ok && good;
    }
    SpirvStageInfo color;
    std::string why;
    reflectSpirv(kColorFsSpirv, sizeof kColorFsSpirv, color, why);
    check(ok && color.numUniformBuffers() == 2 && color.numSamplers() == 0,
          "SPIR-V (build time): every shader, SDL_GPU's sets and the dialect's slots");
    uint32_t bad[] = {0x12345678u, 0, 0, 0, 0};
    SpirvStageInfo none;
    check(!reflectSpirv(bad, sizeof bad, none, why), "SPIR-V reflection refuses what is not SPIR-V");
}

struct V {
    float x, y;
    uint8_t r, g, b, a;
};

VertexLayout colorLayout() {
    VertexLayout l;
    l.bufferCount = 1;
    l.buffers[0].stride = sizeof(V);
    l.attributeCount = 2;
    l.attributes[0] = {0, 0, VertexFormat::Float2, 0};
    l.attributes[1] = {1, 0, VertexFormat::UByte4Norm, 8};
    return l;
}

std::vector<uint8_t> read(Device& d, Texture t, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    Readback r = d.readTexture(t, {0, 0, w, h});
    if (!r || !d.readbackData(r, px.data(), px.size())) px.clear();
    return px;
}
const uint8_t* at(const std::vector<uint8_t>& px, int w, int x, int y) { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; }

}  // namespace

int main(int argc, char** argv) {
    Backend backend = Backend::OpenGL3;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--backend") == 0 && !parseBackend(argv[i + 1], backend)) {
            std::fprintf(stderr, "unknown backend %s\n", argv[i + 1]);
            return 2;
        }
    checkSpirv();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    prepareWindow(backend, SDL_getenv("GD_GLES") != nullptr);
    SDL_Window* window = SDL_CreateWindow("gdcore_render_selftest", 64, 64, SDL_WindowFlags(windowFlags(backend)) | SDL_WINDOW_HIDDEN);
    const int unavailable = backend == Backend::OpenGL3 ? 2 : 77;
    if (!window) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError());
        return unavailable;
    }
    DeviceCreateInfo info;
    info.backend = backend;
    info.window = window;
    std::string error;
    std::unique_ptr<Device> dev = createDevice(info, error);
    if (!dev) {
        std::fprintf(stderr, "device: %s\n", error.c_str());
        return unavailable;
    }
    Device& d = *dev;
    std::printf("backend %s: %s %s (%s)\n", backendId(backend), d.caps().api.c_str(), d.caps().version.c_str(), d.caps().gpu.c_str());
    check(d.caps().supports(Format::RGBA8, kSampled | kRenderable | kBlendable), "caps: RGBA8 sampled, renderable, blendable");

    constexpr int W = 16, H = 16;
    TextureDesc td;
    td.width = W, td.height = H, td.usage = kSampledTexture | kColorTarget;
    Texture color = d.createTexture(td);
    TextureDesc dd = td;
    dd.format = Format::D24, dd.usage = kDepthTarget | kSampledTexture;
    Texture depth = d.createTexture(dd);
    check(color && depth, "create colour + depth targets");

    ShaderDesc sd;
    sd.name = "color";
    sd.glslVertex = kColorVs, sd.glslFragment = kColorFs;
    sd.uniformBlocks[0] = "Params", sd.uniformBlocks[1] = "Extra";
    sd.spirv(kColorVsSpirv, kColorFsSpirv);
    Shader colorShader = d.createShader(sd);
    ShaderDesc qd;
    qd.name = "quad";
    qd.glslVertex = kQuadVs, qd.glslFragment = kQuadFs;
    qd.samplers[0] = "uTex";
    qd.spirv(kQuadVsSpirv, kQuadFsSpirv);
    Shader quadShader = d.createShader(qd);
    ShaderDesc cd = qd;
    cd.name = "cube", cd.glslFragment = kCubeFs, cd.samplers[0] = "uCube";
    cd.spirv(kQuadVsSpirv, kCubeFsSpirv);
    Shader cubeShader = d.createShader(cd);
    ShaderDesc pd = qd;
    pd.name = "depth probe", pd.glslFragment = kDepthFs, pd.samplers[0] = "uDepth";
    pd.spirv(kQuadVsSpirv, kDepthFsSpirv);
    Shader probeShader = d.createShader(pd);
    check(colorShader && quadShader && cubeShader && probeShader, "shaders compile from the gd dialect");
    if (!colorShader || !quadShader || !cubeShader || !probeShader) return 1;

    auto pipeline = [&](Shader s, bool blend, bool depthTest, Format colorFormat, bool withDepth, int samples = 1, Primitive prim = Primitive::Triangles,
                        bool layout = true) {
        PipelineDesc p;
        p.shader = s;
        if (layout) p.layout = colorLayout();
        p.primitive = prim;
        p.blend[0].enable = blend;
        p.blend[0].srcColor = p.blend[0].srcAlpha = BlendFactor::SrcAlpha;
        p.blend[0].dstColor = p.blend[0].dstAlpha = BlendFactor::OneMinusSrcAlpha;
        p.depthStencil.depthTest = depthTest, p.depthStencil.depthWrite = depthTest;
        p.depthStencil.depthCompare = CompareOp::Less;
        p.colorFormats[0] = colorFormat;
        p.depthFormat = withDepth ? Format::D24 : Format::Undefined;
        p.samples = samples;
        return d.createPipeline(p);
    };
    Pipeline opaque = pipeline(colorShader, false, false, Format::RGBA8, true);
    Pipeline blended = pipeline(colorShader, true, false, Format::RGBA8, true);
    Pipeline depthTested = pipeline(colorShader, false, true, Format::RGBA8, true);
    Pipeline quad = pipeline(quadShader, false, false, Format::RGBA8, false, 1, Primitive::TriangleStrip, false);
    Pipeline cubeQuad = pipeline(cubeShader, false, false, Format::RGBA8, false, 1, Primitive::TriangleStrip, false);
    Pipeline probe = pipeline(probeShader, false, false, Format::RGBA8, false, 1, Primitive::TriangleStrip, false);

    const float zero[4] = {0, 0, 0, 0};
    auto params = [&](float ox, float oy, float z, float r, float g, float b, float a) {
        const float p[8] = {ox, oy, z, 0, r, g, b, a};
        d.pushUniforms(0, p, sizeof p);
        d.pushUniforms(1, zero, sizeof zero);
    };
    // A triangle covering the top half of clip space (y > 0) in white.
    const V topTri[3] = {{-1, 0, 255, 255, 255, 255}, {3, 0, 255, 255, 255, 255}, {-1, 2, 255, 255, 255, 255}};
    // A full-target quad as two triangles, colour per call.
    auto fullQuad = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        const V q[6] = {{-1, -1, r, g, b, a}, {1, -1, r, g, b, a}, {1, 1, r, g, b, a}, {-1, -1, r, g, b, a}, {1, 1, r, g, b, a}, {-1, 1, r, g, b, a}};
        BufferRange vb = d.allocTransient(kVertexBuffer, q, sizeof q);
        d.bindVertexBuffers(0, &vb, 1);
        d.draw(6);
    };

    d.beginFrame();
    // 1. Clear and orientation: the top half is white, the bottom half the clear colour.
    {
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        p.colors[0].clear[0] = 0, p.colors[0].clear[1] = 0, p.colors[0].clear[2] = 1, p.colors[0].clear[3] = 1;
        p.depth.texture = depth;
        d.beginPass(p);
        d.bindPipeline(opaque);
        params(0, 0, 0, 1, 1, 1, 1);
        BufferRange vb = d.allocTransient(kVertexBuffer, topTri, sizeof topTri);
        d.bindVertexBuffers(0, &vb, 1);
        d.draw(3);
        d.endPass();
        auto px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 3, 2), 255, 255, 255, 255) && near(at(px, W, 3, 13), 0, 0, 255, 255),
              "clip y up = top rows of the target; clear colour below");
    }
    // 2. Uniforms in two slots and alpha blending: 50 % red over blue.
    {
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        p.colors[0].clear[2] = 1, p.colors[0].clear[3] = 1;
        d.beginPass(p);
        d.bindPipeline(blended);
        const float pr[8] = {0, 0, 0, 0, 1, 0, 0, 0.5f};
        const float add[4] = {0, 0, 0, 0};
        d.pushUniforms(0, pr, sizeof pr);
        d.pushUniforms(1, add, sizeof add);
        fullQuad(255, 255, 255, 255);
        d.endPass();
        auto px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 8, 8), 128, 0, 128, -1, 3), "alpha blend (uniform tint in slot 0)");
    }
    // 3. Depth test: near (z -0.5) green first, then far (z 0.5) red with Less: green stays.
    {
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        p.depth.texture = depth;
        d.beginPass(p);
        d.bindPipeline(depthTested);
        // Occlusion queries where the backend has them (docs/render.md 6c):
        // every sample of the near quad passes, none of the far one.
        const Query qNear = d.createQuery(), qFar = d.createQuery();
        d.beginQuery(qNear);
        params(0, 0, -0.5f, 0, 1, 0, 1);
        fullQuad(255, 255, 255, 255);
        d.endQuery();
        d.beginQuery(qFar);
        params(0, 0, 0.5f, 1, 0, 0, 1);
        fullQuad(255, 255, 255, 255);
        d.endQuery();
        d.endPass();
        auto px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 5, 5), 0, 255, 0, 255), "depth test (Less) keeps the nearer quad");
        if (qNear && qFar) {
            uint64_t nearN = 0, farN = 1;
            bool answered = false;
            for (int i = 0; i < 1000 && !answered; ++i) {
                answered = d.queryResult(qNear, nearN) && d.queryResult(qFar, farN);
                if (!answered) SDL_Delay(1);
            }
            const bool es = d.caps().es;
            check(answered && farN == 0 && (es ? nearN >= 1 : nearN == uint64_t(W) * uint64_t(H)), "occlusion queries (samples that passed)");
            d.destroyQuery(qNear), d.destroyQuery(qFar);
        } else {
            std::printf("skip: occlusion queries (none on this backend)\n");
        }
    }
    // 4. Depth probe: the depth texture sampled into colour (window depth of z -0.5 = 0.25).
    {
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        d.beginPass(p);
        d.bindPipeline(probe);
        Sampler nearest = d.createSampler(SamplerDesc{Filter::Nearest, Filter::Nearest, MipFilter::None, Wrap::ClampToEdge, Wrap::ClampToEdge,
                                                      Wrap::ClampToEdge});
        TextureBinding tb{depth, nearest};
        d.bindTextures(0, &tb, 1);
        d.draw(4);
        d.endPass();
        auto px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 7, 7), 64, 64, 64, -1, 2), "depth texture sampled in a shader (window depth 0.25)");
    }
    // 5. Scissor: only the top-left 4 x 4 is drawn.
    {
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        p.colors[0].clear[3] = 1;
        d.beginPass(p);
        d.bindPipeline(opaque);
        d.setScissor({0, 0, 4, 4});
        params(0, 0, 0, 1, 1, 0, 1);
        fullQuad(255, 255, 255, 255);
        d.endPass();
        auto px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 1, 1), 255, 255, 0, 255) && near(at(px, W, 6, 1), 0, 0, 0, 255) &&
                  near(at(px, W, 1, 6), 0, 0, 0, 255),
              "scissor rectangle, origin top-left");
    }
    // 6. Textures: uploaded row 0 is uv (0,0) = the top-left; a render target sampled the same way.
    Sampler nearestClamp = d.createSampler(SamplerDesc{Filter::Nearest, Filter::Nearest, MipFilter::None, Wrap::ClampToEdge,
                                                       Wrap::ClampToEdge, Wrap::ClampToEdge});
    {
        TextureDesc t;
        t.width = 2, t.height = 2;
        Texture tex = d.createTexture(t);
        const uint8_t pix[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};  // red green / blue white
        d.updateTexture({tex}, 2, 2, pix);
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        d.beginPass(p);
        d.bindPipeline(quad);
        TextureBinding tb{tex, nearestClamp};
        d.bindTextures(0, &tb, 1);
        d.draw(4);
        d.endPass();
        auto px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 1, 1), 255, 0, 0, 255) && near(at(px, W, 14, 1), 0, 255, 0, 255) &&
                  near(at(px, W, 1, 14), 0, 0, 255, 255),
              "uploaded texture: row 0 at the top, uv (0,0) top-left");
        // Render to texture (step 1's picture) and sample it into another target.
        Texture rt = d.createTexture(td);
        PassDesc a;
        a.colorCount = 1;
        a.colors[0].texture = rt;
        a.colors[0].clear[2] = 1, a.colors[0].clear[3] = 1;
        d.beginPass(a);
        d.bindPipeline(opaque);
        params(0, 0, 0, 1, 1, 1, 1);
        BufferRange vb = d.allocTransient(kVertexBuffer, topTri, sizeof topTri);
        d.bindVertexBuffers(0, &vb, 1);
        d.draw(3);
        d.endPass();
        d.beginPass(p);
        d.bindPipeline(quad);
        TextureBinding rb{rt, nearestClamp};
        d.bindTextures(0, &rb, 1);
        d.draw(4);
        d.endPass();
        px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 3, 2), 255, 255, 255, 255) && near(at(px, W, 3, 13), 0, 0, 255, 255),
              "render target sampled: uv (0,0) is its top-left too");
        d.destroyTexture(rt);
        d.destroyTexture(tex);
    }
    // 7. Mipmaps: a red / blue checker averages to purple at the 1 x 1 level.
    {
        TextureDesc t;
        t.width = 32, t.height = 32, t.mips = 0;
        Texture tex = d.createTexture(t);
        std::vector<uint8_t> pix(32 * 32 * 4);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                uint8_t* p = &pix[size_t(y * 32 + x) * 4];
                bool red = (x + y) & 1;
                p[0] = red ? 255 : 0, p[1] = 0, p[2] = red ? 0 : 255, p[3] = 255;
            }
        d.updateTexture({tex}, 32, 32, pix.data());
        d.generateMipmaps(tex);
        check(d.textureDesc(tex).mips == 6, "full mip chain (32 -> 6 levels)");
        TextureDesc one = td;
        one.width = one.height = 1;
        Texture small = d.createTexture(one);
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = small;
        d.beginPass(p);
        d.bindPipeline(quad);
        SamplerDesc tri;
        tri.minFilter = tri.magFilter = Filter::Linear, tri.mipFilter = MipFilter::Linear;
        TextureBinding tb{tex, d.createSampler(tri)};
        d.bindTextures(0, &tb, 1);
        d.draw(4);
        d.endPass();
        auto px = read(d, small, 1, 1);
        check(!px.empty() && near(px.data(), 128, 0, 128, 255, 4), "generated mipmaps, trilinear sampling");
        // A non-square texture: its last levels are 1 pixel high (SDL 3.4.18's
        // own mipmap generation leaves them undefined; docs/render.md 4.2).
        TextureDesc wide;
        wide.width = 32, wide.height = 4, wide.mips = 0;
        Texture wtex = d.createTexture(wide);
        std::vector<uint8_t> wpix(32 * 4 * 4);
        for (int i = 0; i < 32 * 4; ++i) {
            const bool red = ((i % 32) + (i / 32)) & 1;
            wpix[size_t(i) * 4] = red ? 255 : 0, wpix[size_t(i) * 4 + 2] = red ? 0 : 255, wpix[size_t(i) * 4 + 3] = 255;
        }
        d.updateTexture({wtex}, 32, 4, wpix.data());
        d.generateMipmaps(wtex);
        d.beginPass(p);
        d.bindPipeline(quad);
        TextureBinding wb{wtex, d.createSampler(tri)};
        d.bindTextures(0, &wb, 1);
        d.draw(4);
        d.endPass();
        px = read(d, small, 1, 1);
        check(d.textureDesc(wtex).mips == 6 && !px.empty() && near(px.data(), 128, 0, 128, 255, 4),
              "generated mipmaps of a non-square texture (32 x 4: down to 1 x 1)");
        d.destroyTexture(wtex);
        d.destroyTexture(small);
        d.destroyTexture(tex);
    }
    // 8. MSAA: a diagonal edge resolved has partly covered pixels.
    if (d.caps().maxSamples >= 4) {
        TextureDesc m = td;
        m.samples = 4, m.usage = kColorTarget;
        Texture msaa = d.createTexture(m);
        Pipeline ms = pipeline(colorShader, false, false, Format::RGBA8, false, 4);
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = msaa;
        p.colors[0].clear[3] = 1;
        p.colors[0].store = StoreOp::Resolve;
        p.colors[0].resolve = color;
        d.beginPass(p);
        d.bindPipeline(ms);
        params(0, 0, 0, 1, 1, 1, 1);
        const V tri[3] = {{-1, -1, 255, 255, 255, 255}, {1, -1, 255, 255, 255, 255}, {-1, 1, 255, 255, 255, 255}};
        BufferRange vb = d.allocTransient(kVertexBuffer, tri, sizeof tri);
        d.bindVertexBuffers(0, &vb, 1);
        d.draw(3);
        d.endPass();
        auto px = read(d, color, W, H);
        int partial = 0;
        for (int i = 0; !px.empty() && i < W * H; ++i) partial += px[size_t(i) * 4] > 20 && px[size_t(i) * 4] < 235;
        check(partial >= 8, "MSAA target resolved (partly covered edge pixels)");
        d.destroyPipeline(ms);
        d.destroyTexture(msaa);
    } else {
        std::printf("SKIP MSAA (max samples %d)\n", d.caps().maxSamples);
    }
    // 9. Cube map: +X face.
    {
        TextureDesc c;
        c.type = TextureType::Cube, c.width = c.height = 1;
        Texture cube = d.createTexture(c);
        const uint8_t faces[6][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}, {0, 255, 255, 255}, {255, 0, 255, 255}};
        for (int f = 0; f < 6; ++f) d.updateTexture({cube, 0, f}, 1, 1, faces[f]);
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        d.beginPass(p);
        d.bindPipeline(cubeQuad);
        TextureBinding tb{cube, nearestClamp};
        d.bindTextures(0, &tb, 1);
        d.draw(4);
        d.endPass();
        auto px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 8, 8), 255, 0, 0, 255), "cube map (+X face)");
        d.destroyTexture(cube);
    }
    // 10. Index buffer with a vertex offset; copy and blit.
    {
        const V verts[6] = {{0, 0, 0, 0, 0, 0},           {0, 0, 0, 0, 0, 0},           {-1, -1, 0, 255, 0, 255},
                            {1, -1, 0, 255, 0, 255},     {1, 1, 0, 255, 0, 255},       {-1, 1, 0, 255, 0, 255}};
        const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
        Buffer vb = d.createBuffer(BufferDesc{sizeof verts, kVertexBuffer}, verts);
        BufferRange ib = d.allocTransient(kIndexBuffer, idx, sizeof idx);
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        p.colors[0].clear[3] = 1;
        d.beginPass(p);
        d.bindPipeline(opaque);
        params(0, 0, 0, 1, 1, 1, 1);
        BufferRange r{vb, 0, sizeof verts};
        d.bindVertexBuffers(0, &r, 1);
        d.bindIndexBuffer(ib, IndexType::U16);
        d.drawIndexed(6, 1, 0, 2);
        d.endPass();
        auto px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 8, 8), 0, 255, 0, 255), "indexed draw with a vertex offset");
        Texture other = d.createTexture(td);
        PassDesc c;
        c.colorCount = 1;
        c.colors[0].texture = other;
        c.colors[0].clear[0] = 1, c.colors[0].clear[3] = 1;
        d.beginPass(c);
        d.endPass();
        d.copyTexture({other, 0, 0, 0, 0}, {color, 0, 0, 2, 3}, 4, 5);
        px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 3, 4), 255, 0, 0, 255) && near(at(px, W, 1, 4), 0, 255, 0, 255) &&
                  near(at(px, W, 3, 9), 0, 255, 0, 255),
              "copyTexture of a rectangle (top-left origin)");
        d.blit({other}, W, H, {color}, W / 2, H / 2, Filter::Linear);
        px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 2, 2), 255, 0, 0, 255) && near(at(px, W, 12, 12), 0, 255, 0, 255), "scaled blit");
        Readback rb = d.readTexture(color, {0, 0, 1, 1});
        uint8_t one[4] = {};
        bool waited = false;
        for (int i = 0; i < 1000 && !d.readbackReady(rb); ++i) waited = true, SDL_Delay(1);
        (void)waited;
        check(d.readbackReady(rb) && d.readbackData(rb, one, 4) && near(one, 255, 0, 0, 255), "asynchronous readback");
        d.destroyTexture(other);
        d.destroyBuffer(vb);
    }
    // 11. Culling: a counter-clockwise triangle (GL clip space, y up) is the
    // front face on every backend (docs/render.md 2.3, 3.2).
    {
        auto culled = [&](CullMode cull) {
            PipelineDesc p;
            p.shader = colorShader;
            p.layout = colorLayout();
            p.raster.cull = cull;
            p.raster.frontFace = FrontFace::CounterClockwise;
            p.colorFormats[0] = Format::RGBA8;
            return d.createPipeline(p);
        };
        Pipeline cullBack = culled(CullMode::Back), cullFront = culled(CullMode::Front);
        const V ccw[3] = {{-1, -1, 255, 255, 255, 255}, {1, -1, 255, 255, 255, 255}, {-1, 1, 255, 255, 255, 255}};
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        p.colors[0].clear[3] = 1;
        d.beginPass(p);
        d.bindPipeline(cullBack);
        params(0, 0, 0, 1, 0, 0, 1);
        BufferRange vb = d.allocTransient(kVertexBuffer, ccw, sizeof ccw);
        d.bindVertexBuffers(0, &vb, 1);
        d.draw(3);  // kept: red
        d.bindPipeline(cullFront);
        params(0, 0, 0, 0, 1, 0, 1);
        d.bindVertexBuffers(0, &vb, 1);
        d.draw(3);  // culled: no green
        d.endPass();
        auto px = read(d, color, W, H);
        check(!px.empty() && near(at(px, W, 2, 13), 255, 0, 0, 255) && near(at(px, W, 13, 2), 0, 0, 0, 255),
              "cull Back keeps a counter-clockwise triangle, cull Front drops it");
        d.destroyPipeline(cullBack);
        d.destroyPipeline(cullFront);
    }
    d.present(color, {0, 0, W, H}, Filter::Linear);
    d.endFrame();
    // A few more frames: the transient rings cycle.
    for (int f = 0; f < 4; ++f) {
        d.beginFrame();
        PassDesc p;
        p.colorCount = 1;
        p.colors[0].texture = color;
        d.beginPass(p);
        d.bindPipeline(opaque);
        params(0, 0, 0, 0, 0, 1, 1);
        fullQuad(255, 255, 255, 255);
        d.endPass();
        d.present(color, {0, 0, W, H}, Filter::Nearest);
        d.endFrame();
    }
    auto px = read(d, color, W, H);
    check(!px.empty() && near(at(px, W, 0, 0), 0, 0, 255, 255), "frames cycle the transient rings");

    dev.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("gdcore_render_selftest (%s): %d passed, %d failed\n", backendId(backend), g_passed, g_failed);
    return g_failed;
}
