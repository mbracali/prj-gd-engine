// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Renderer's drawing on gd-engine-core (core_renderer.h). Each function
// mirrors the local one of the same name in renderer.cpp; the comments say
// where the explicit API needs something GL did implicitly.

#include "render/core_renderer.h"

#include "platform/paths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gr = gd::core::render;

namespace {

// renderer.cpp's main program in the gd shader dialect: the same maths in the
// same order, its uniforms in two std140 blocks.
const char* kMainVs = R"(
GD_ATTR(0) vec3 aPos;
GD_ATTR(1) vec2 aUv;
GD_ATTR(2) vec4 aColor;
GD_ATTR(3) vec3 aNormal;
GD_ATTR(4) vec4 aWeights;
GD_UNIFORMS(0, DrawState) {
    mat4 uMvp;
    mat4 uModel;
    mat4 uBones[4];
    mat4 uTexU4;
    vec4 uTint;
    vec4 uUvOffsetJitter;
    vec4 uMatFlags;
    vec4 uFog;
    vec4 uFogColorCutout;
    vec4 uTexOColorScale;
    vec4 uEye;
    vec4 uCamRight;
    vec4 uCamUp;
    ivec4 uFlags;
    ivec4 uFlags2;
};
GD_UNIFORMS(1, Lights) {
    vec4 uAmbient;
    vec4 uLightDir[8];
    vec4 uLightColor[8];
};
GD_OUT(0) vec2 vUv;
GD_OUT(1) vec4 vColor;
GD_OUT(2) float vFog;
void main() {
    int uSkin = uFlags.y, uLit = uFlags.z, uTexgen = uFlags.w, uLightCount = uFlags2.y;
    mat3 uTexU = mat3(uTexU4);
    vUv = aUv + uUvOffsetJitter.xy;
    vec4 pos = vec4(aPos, 1.0);
    vec3 normal = aNormal;
    if (uSkin != 0) {
        pos = vec4(0.0);
        normal = vec3(0.0);
        for (int i = 0; i < 4; ++i) {
            pos += aWeights[i] * (uBones[i] * vec4(aPos, 1.0));
            normal += aWeights[i] * (mat3(uBones[i]) * aNormal);
        }
        pos.w = 1.0;
    }
    if (uTexgen == 5) {
        vec3 wp = (uModel * pos).xyz;
        vec3 N = uTexU * (mat3(uModel) * normal);
        vec3 V = uTexU * (uEye.xyz - wp);
        vec3 R = 2.0 * dot(N, V) * N - V;
        vUv = uTexOColorScale.xy + R.xy * (0.5 * inversesqrt(max(dot(V, V), 1e-12)));
    } else if (uTexgen == 2) {
        vec3 n = normalize(mat3(uModel) * normal);
        vUv = vec2(0.5) + 0.5 * vec2(dot(n, uCamRight.xyz), -dot(n, uCamUp.xyz));
    }
    if (uLit != 0) {
        vec3 n = mat3(uModel) * normal;
        vec3 wp = (uModel * pos).xyz;
        vec3 sum = vec3(0.0);
        for (int i = 0; i < uLightCount; ++i) {
            if (uLightDir[i].w > 0.5) {
                sum += uLightColor[i].rgb * max(dot(n, uLightDir[i].xyz), 0.0);
            } else {
                vec3 d = uLightDir[i].xyz - wp;
                float dist = max(length(d), 1e-4);
                float att = clamp(1.0 - dist / max(uLightColor[i].a, 1e-4), 0.0, 1.0);
                sum += uLightColor[i].rgb * att * max(dot(n, d / dist), 0.0);
            }
        }
        vec4 vtx = uSkin != 0 ? vec4(1.0) : aColor;
        vec4 amb = uMatFlags.y > 0.5 ? vtx : uTint;
        vec4 dyn = uMatFlags.z > 0.5 ? vtx : uTint;
        vec3 ambientLight = uMatFlags.x > 0.5 ? uAmbient.xyz : vec3(1.0);
        vColor = min(vec4(amb.rgb * ambientLight + dyn.rgb * sum, amb.a), vec4(1.0));
    } else {
        vColor = min(uMatFlags.x < 0.5 && uMatFlags.y < 0.5 ? uTint : aColor * uTint, vec4(1.0));
    }
    vec4 clip = uMvp * pos;
    vFog = uFog.x > 0.5 ? clamp((uFog.z - clip.w) / max(uFog.z - uFog.y, 1e-4), 0.0, 1.0) : 1.0;
    clip.xy += uUvOffsetJitter.zw * clip.w;
    gl_Position = gd_clip(clip);
}
)";

const char* kMainFs = R"(
GD_IN(0) vec2 vUv;
GD_IN(1) vec4 vColor;
GD_IN(2) float vFog;
GD_UNIFORMS(0, DrawState) {
    mat4 uMvp;
    mat4 uModel;
    mat4 uBones[4];
    mat4 uTexU4;
    vec4 uTint;
    vec4 uUvOffsetJitter;
    vec4 uMatFlags;
    vec4 uFog;
    vec4 uFogColorCutout;
    vec4 uTexOColorScale;
    vec4 uEye;
    vec4 uCamRight;
    vec4 uCamUp;
    ivec4 uFlags;
    ivec4 uFlags2;
};
GD_SAMPLER2D(0, uTex);
GD_FRAG_OUT(0) vec4 fragColor;
void main() {
    int uUseTex = uFlags.x, uTexDecal = uFlags2.x;
    float uCutout = uFogColorCutout.w, uColorScale = uTexOColorScale.z;
    vec4 c = vColor;
    if (uUseTex != 0) {
        vec4 t = texture(uTex, vUv);
        c = uTexDecal == 1 ? t
          : uTexDecal == 2 ? vec4(t.rgb, t.a * c.a)
                           : vec4(c.rgb * uColorScale, c.a) * t;
    }
    if (c.a <= uCutout) discard;
    c.rgb = mix(uFogColorCutout.rgb, c.rgb, vFog);
    fragColor = c;
}
)";

// A quad over the viewport (a vertex-id strip), at depth uParams.x, in colour uColor.
const char* kFillVs = R"(
GD_UNIFORMS(0, Fill) { vec4 uColor; vec4 uParams; };
void main() {
    vec2 p = vec2(float((GD_VERTEX_ID & 1) * 2 - 1), float((GD_VERTEX_ID >> 1) * 2 - 1));
    gl_Position = gd_clip(vec4(p, uParams.x, 1.0));
}
)";
const char* kFillFs = R"(
GD_UNIFORMS(0, Fill) { vec4 uColor; vec4 uParams; };
GD_FRAG_OUT(0) vec4 fragColor;
void main() { fragColor = uColor; }
)";

// renderer.cpp's depth-of-field pass (the frame is stored as local's, so
// gl_FragCoord and the copied texture count rows as there).
const char* kBlurVs = R"(
GD_UNIFORMS(0, Blur) { vec4 uDepthTapSize; vec4 uOrigin; };
void main() {
    vec2 p = vec2(float((GD_VERTEX_ID & 1) * 2 - 1), float((GD_VERTEX_ID >> 1) * 2 - 1));
    gl_Position = gd_clip(vec4(p, uDepthTapSize.x * 2.0 - 1.0, 1.0));
}
)";
const char* kBlurFs = R"(
GD_UNIFORMS(0, Blur) { vec4 uDepthTapSize; vec4 uOrigin; };
GD_SAMPLER2D(0, uTex);
GD_FRAG_OUT(0) vec4 fragColor;
void main() {
    vec2 uSize = uDepthTapSize.zw;
    vec2 uv = (gl_FragCoord.xy - uOrigin.xy) / uSize;
    vec2 t = vec2(uDepthTapSize.y) / uSize;
    vec3 c = vec3(0.0);
    float wsum = 0.0;
    for (int j = -2; j <= 2; ++j)
        for (int i = -2; i <= 2; ++i) {
            float w = float(3 - abs(i)) * float(3 - abs(j));
            c += w * texture(uTex, uv + vec2(float(i), float(j)) * 0.375 * t).rgb;
            wsum += w;
        }
    fragColor = vec4(c / wsum, 1.0);
}
)";

// Lens-flare depth probes: pixel i of an N x 1 target gets the frame's depth
// at point i (aPoint, texel coordinates), as 24-bit unorm in rgb.
const char* kProbeVs = R"(
GD_ATTR(0) vec2 aPos;
GD_ATTR(1) vec2 aPoint;
GD_OUT(0) vec2 vPoint;
void main() {
    vPoint = aPoint;
    gl_Position = gd_clip(vec4(aPos, 0.0, 1.0));
}
)";
const char* kProbeFs = R"(
GD_IN(0) vec2 vPoint;
GD_SAMPLER2D(0, uDepth);
GD_FRAG_OUT(0) vec4 fragColor;
void main() {
    float d = texelFetch(uDepth, ivec2(vPoint), 0).r;
    uint k = uint(clamp(d, 0.0, 1.0) * 16777215.0 + 0.5);
    fragColor = vec4(float(k >> 16u), float((k >> 8u) & 255u), float(k & 255u), 255.0) / 255.0;
}
)";

}  // namespace

// The same shaders as SPIR-V for the SDL3 GPU and Vulkan backends, compiled at
// build time (gdcore_spirv_shaders in CMakeLists.txt): k<Name>VsSpirv / k<Name>FsSpirv.
#include "core_renderer_spirv.h"

CoreRenderer::~CoreRenderer() { shutdown(); }

const char* CoreRenderer::backendId() const { return gr::backendId(backend_); }

bool CoreRenderer::init(SDL_Window* window, gr::Backend backend, const gr::GlHooks& hooks, std::string& error) {
    backend_ = backend;
    gr::DeviceCreateInfo info;
    info.backend = backend;
    info.window = window;
    info.gl = hooks;
    info.cacheDir = prefDir();  // the Vulkan pipeline cache lives with the settings
    info.hdrOutput = r_.deviceHdr_;  // True HDR on Vulkan (#80): an HDR swapchain when offered
    dev_ = gr::createDevice(info, error);
    if (!dev_) return false;
    r_.gpuName_ = dev_->caps().gpu;
    SDL_Log("graphics backend %s: %s (%s)", backendId(), r_.gpuName_.c_str(), dev_->caps().version.c_str());

    struct Src {
        const char *name, *vs, *fs, *block0, *block1, *sampler;
    } src[kPrograms] = {{"main", kMainVs, kMainFs, "DrawState", "Lights", "uTex"},
                        {"fill", kFillVs, kFillFs, "Fill", nullptr, nullptr},
                        {"depth of field", kBlurVs, kBlurFs, "Blur", nullptr, "uTex"},
                        {"depth probe", kProbeVs, kProbeFs, nullptr, nullptr, "uDepth"}};
    for (int p = 0; p < kPrograms; ++p) {
        gr::ShaderDesc d;
        d.name = src[p].name;
        d.glslVertex = src[p].vs, d.glslFragment = src[p].fs;
        d.uniformBlocks[0] = src[p].block0, d.uniformBlocks[1] = src[p].block1;
        d.samplers[0] = src[p].sampler;
        switch (p) {  // the build-time SPIR-V (SDL3 GPU, Vulkan)
        case kMain: d.spirv(kMainVsSpirv, kMainFsSpirv); break;
        case kFill: d.spirv(kFillVsSpirv, kFillFsSpirv); break;
        case kBlur: d.spirv(kBlurVsSpirv, kBlurFsSpirv); break;
        case kProbe: d.spirv(kProbeVsSpirv, kProbeFsSpirv); break;
        }
        shaders_[p] = dev_->createShader(d);
        shaderOk_[p] = bool(shaders_[p]);
        if (!shaderOk_[p] && p != kBlur) {
            error = dev_->lastError();
            dev_.reset();
            return false;
        }
    }
    gr::TextureDesc wd;
    wd.width = wd.height = 1;
    white_.tex = dev_->createTexture(wd);
    const uint8_t white[4] = {255, 255, 255, 255};
    dev_->updateTexture({white_.tex}, 1, 1, white);
    white_.sampler = sampler(gr::Filter::Nearest, gr::Filter::Nearest, false, false);
    setFrameCap(60);
    return true;
}

void CoreRenderer::shutdown() {
    if (!dev_) return;
    suspend();
    post_.reset();
    dev_.reset();  // the device frees everything it made, then its context
}

// ---------------------------------------------------------------- passes

int CoreRenderer::targetHeight() const {
    if (target_ == 0) return mainHeight();
    return textures_[size_t(target_)].h;
}

bool CoreRenderer::targetF16() const {
    if (target_ != 0) return false;  // render targets are RGBA8
    return r_.sceneActive_ ? sceneF16_ : frameF16_;
}

void CoreRenderer::ensureFrame() {
    int w = 0, h = 0;
    r_.frameSize(w, h);
    w = std::max(w, 1), h = std::max(h, 1);
    const bool f16 = r_.hdrSurface_ != 0;  // an HDR surface: the frame is RGBA16F (local's main_.f16)
    if (frameColor_ && w == frameW_ && h == frameH_ && f16 == frameF16_) return;
    suspend();
    if (frameColor_) dev_->destroyTexture(frameColor_), dev_->destroyTexture(frameDepth_);
    // Local's frame: RGBA8 colour (the window's or the internal resolution's) + 24-bit depth.
    gr::TextureDesc c;
    c.width = w, c.height = h, c.usage = gr::kSampledTexture | gr::kColorTarget;
    c.format = f16 ? gr::Format::RGBA16F : gr::Format::RGBA8;
    frameColor_ = dev_->createTexture(c);
    gr::TextureDesc d = c;
    d.format = gr::Format::D24, d.usage = gr::kSampledTexture | gr::kDepthTarget;
    frameDepth_ = dev_->createTexture(d);
    frameW_ = w, frameH_ = h, frameF16_ = f16;
}

// Local's glCopyTexSubImage2D from the bound framebuffer: an exact copy, or
// from an RGBA16F scene or frame the same conversion to RGBA8 (a 1:1 blit).
void CoreRenderer::copyFromTarget(int x, int y, Texture dst, int w, int h) {
    const Texture src = target_ == 0 ? mainColor() : textures_[size_t(target_)].tex;
    if (targetF16()) dev_->blit({src, 0, 0, x, y}, w, h, {dst}, w, h, gr::Filter::Nearest);
    else dev_->copyTexture({src, 0, 0, x, y}, {dst}, w, h);
}

void CoreRenderer::ensurePass() {
    if (dev_->inPass()) return;
    if (!frameColor_) ensureFrame();
    gr::PassDesc p;
    p.colorCount = 1;
    p.colors[0].load = gr::LoadOp::Load;
    p.depth.load = gr::LoadOp::Load;
    p.depth.stencilLoad = gr::LoadOp::DontCare;
    if (target_ == 0) {
        p.colors[0].texture = mainColor();
        p.depth.texture = mainDepth();
    } else {
        p.colors[0].texture = textures_[size_t(target_)].tex;
        p.depth.texture = targetDepth_[target_];
    }
    dev_->beginPass(p);
    rectsDirty_ = true;
}

void CoreRenderer::suspend() {
    if (dev_ && dev_->inPass()) dev_->endPass();
}

// Local's GL rectangles as they are: every target holds its picture GL style
// (setMvp), so the interface's rows are local's rows.
void CoreRenderer::applyRects() {
    if (!rectsDirty_) return;
    const int h = targetHeight();
    dev_->setViewport({vp_[0], vp_[1], vp_[2], vp_[3]});
    if (scissorOn_) dev_->setScissor({sc_[0], sc_[1], sc_[2], sc_[3]});
    else dev_->setScissor({0, 0, target_ == 0 ? mainWidth() : textures_[size_t(target_)].w, h});
    rectsDirty_ = false;
}

gr::Sampler CoreRenderer::sampler(gr::Filter min, gr::Filter mag, bool mips, bool repeat) {
    gr::SamplerDesc s;
    s.minFilter = min, s.magFilter = mag;
    s.mipFilter = mips ? gr::MipFilter::Linear : gr::MipFilter::None;
    s.wrapU = s.wrapV = s.wrapW = repeat ? gr::Wrap::Repeat : gr::Wrap::ClampToEdge;
    return dev_->createSampler(s);
}

uint8_t CoreRenderer::colorMask() const {
    return target_ != 0 ? gr::kWriteAll : gr::kWriteRGB;  // the frame keeps its cleared alpha (local's colour mask)
}

gr::Pipeline CoreRenderer::pipeline(Program program, Blend blend, bool depthTest, bool depthWrite, int cull, bool decal, uint8_t writeMask,
                                    bool f16) {
    const uint32_t key = uint32_t(program) | uint32_t(blend) << 3 | uint32_t(depthTest) << 6 | uint32_t(depthWrite) << 7 |
                         uint32_t(cull) << 8 | uint32_t(decal) << 10 | uint32_t(writeMask) << 11 | uint32_t(f16) << 15;
    if (auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;
    gr::PipelineDesc p;
    p.shader = shaders_[program];
    if (program == kMain) {
        p.layout.bufferCount = 1;
        p.layout.buffers[0].stride = sizeof(Vertex);
        p.layout.attributeCount = 5;
        p.layout.attributes[0] = {0, 0, gr::VertexFormat::Float3, uint32_t(offsetof(Vertex, x))};
        p.layout.attributes[1] = {1, 0, gr::VertexFormat::Float2, uint32_t(offsetof(Vertex, u))};
        p.layout.attributes[2] = {2, 0, gr::VertexFormat::UByte4Norm, uint32_t(offsetof(Vertex, r))};
        p.layout.attributes[3] = {3, 0, gr::VertexFormat::Float3, uint32_t(offsetof(Vertex, nx))};
        p.layout.attributes[4] = {4, 0, gr::VertexFormat::Float4, uint32_t(offsetof(Vertex, w))};
    } else if (program == kProbe) {
        p.layout.bufferCount = 1;
        p.layout.buffers[0].stride = 16;
        p.layout.attributeCount = 2;
        p.layout.attributes[0] = {0, 0, gr::VertexFormat::Float2, 0};
        p.layout.attributes[1] = {1, 0, gr::VertexFormat::Float2, 8};
    }
    p.primitive = program == kMain || program == kProbe ? gr::Primitive::Triangles : gr::Primitive::TriangleStrip;
    p.raster.cull = cull == 1 ? gr::CullMode::Back : cull == 2 ? gr::CullMode::Front : gr::CullMode::None;
    if (decal) p.raster.depthBiasConstant = -1, p.raster.depthBiasSlope = -1;  // glPolygonOffset(-1, -1)
    p.depthStencil.depthTest = depthTest;
    p.depthStencil.depthWrite = depthWrite;
    // LEQUAL as local; the depth "clear" (a fill) writes 1 everywhere it covers.
    p.depthStencil.depthCompare = program == kFill ? gr::CompareOp::Always : gr::CompareOp::LessEqual;
    gr::BlendState& b = p.blend[0];
    using F = gr::BlendFactor;
    auto both = [&](F s, F d) {
        b.enable = true;
        b.srcColor = b.srcAlpha = s;
        b.dstColor = b.dstAlpha = d;
    };
    switch (blend) {
    case Blend::Opaque: break;
    case Blend::Alpha: both(F::SrcAlpha, F::OneMinusSrcAlpha); break;
    case Blend::Add: both(F::One, F::One); break;
    case Blend::AddAlpha: both(F::SrcAlpha, F::One); break;
    case Blend::Multiply: both(F::DstColor, F::Zero); break;
    case Blend::Subtract:
        both(F::SrcAlpha, F::One);
        b.colorOp = b.alphaOp = gr::BlendOp::ReverseSubtract;
        break;
    case Blend::None: both(F::Zero, F::One); break;
    }
    b.writeMask = writeMask;
    p.colorFormats[0] = f16 ? gr::Format::RGBA16F : gr::Format::RGBA8;  // an HDR scene or frame
    p.depthFormat = program == kProbe ? gr::Format::Undefined : gr::Format::D24;
    gr::Pipeline pl = dev_->createPipeline(p);
    pipelines_[key] = pl;
    return pl;
}

// ---------------------------------------------------------------- frames

void CoreRenderer::beginFrame(SDL_Color clear) {
    dev_->beginFrame();
    if (r_.deviceHdr_ && dev_->hdrSurface() != r_.hdrSurface_) {
        // The swapchain was made again with another colour space (the display's HDR turned off or on).
        SDL_Log("hdr: the Vulkan swapchain is now %s", dev_->hdrSurface() ? "HDR" : ("SDR: " + dev_->hdrReason()).c_str());
        r_.setHdrSurface(dev_->hdrSurface());
    }
    if (r_.hdrSurface_) r_.updateHdr();  // the window's HDR state (paper white, headroom), as local
    ensureFrame();
    const int w = frameW_, h = frameH_;
    int* viewport = r_.viewport_;
    float scale = std::min(w / kScreenW, h / kScreenH);
    int vw = int(kScreenW * scale), vh = int(kScreenH * scale);
    viewport[0] = (w - vw) / 2;
    viewport[1] = (h - vh) / 2;
    viewport[2] = vw;
    viewport[3] = vh;
    // Local clears the frame to opaque black, then the viewport to `clear`
    // (scissored). An explicit API clears whole attachments at the start of a
    // pass: the scissored clear is a draw.
    suspend();
    target_ = 0;
    gr::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = frameColor_;
    p.colors[0].clear[3] = 1;
    p.depth.texture = frameDepth_;
    p.depth.stencilLoad = gr::LoadOp::DontCare;
    dev_->beginPass(p);
    for (int k = 0; k < 4; ++k) vp_[k] = sc_[k] = viewport[k];
    scissorOn_ = true, rectsDirty_ = true;
    const float rgba[4] = {clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1};
    fillRectPixels(viewport, rgba, false);
    const float m[16] = {2 / kScreenW, 0, 0, 0, 0, -2 / kScreenH, 0, 0, 0, 0, -1, 0, -1, 1, 0, 1};
    std::copy_n(m, 16, r_.screenMvp_);
}

// A rectangle (GL style) filled with a colour (alpha written, as a clear), or
// its depth set to 1 (depthOnly) within the scissor.
void CoreRenderer::fillRectPixels(const int rect[4], const float rgba[4], bool depthOnly) {
    ensurePass();
    if (depthOnly) {
        applyRects();
        dev_->bindPipeline(pipeline(kFill, Blend::Opaque, true, true, 0, false, 0, targetF16()));
    } else {
        dev_->bindPipeline(pipeline(kFill, Blend::Opaque, false, false, 0, false, gr::kWriteAll, targetF16()));  // a clear writes alpha
        dev_->setViewport({rect[0], rect[1], rect[2], rect[3]});
        dev_->setScissor({rect[0], rect[1], rect[2], rect[3]});
        rectsDirty_ = true;
    }
    const float u[8] = {rgba ? rgba[0] : 0, rgba ? rgba[1] : 0, rgba ? rgba[2] : 0, rgba ? rgba[3] : 0, 1, 0, 0, 0};
    dev_->pushUniforms(0, u, sizeof u);
    gr::TextureBinding none{white_.tex, white_.sampler};
    dev_->bindTextures(0, &none, 1);
    dev_->draw(4);
}

void CoreRenderer::endFrame() {
    suspend();
    if (paceNs_) {
        uint64_t now = SDL_GetTicksNS();
        if (nextFrameNs_ > now && nextFrameNs_ - now <= paceNs_) SDL_DelayPrecise(nextFrameNs_ - now);
        now = SDL_GetTicksNS();
        nextFrameNs_ = nextFrameNs_ + paceNs_ >= now ? nextFrameNs_ + paceNs_ : now + paceNs_;
    }
    if (frameColor_ && frameF16_) {
        // True HDR (local's outputHdr): the frame converted for the surface at
        // the window's size, then shown 1:1. Never a black window: without the
        // conversion's program the frame shows unconverted, as local's.
        int ww = 0, wh = 0;
        dev_->drawableSize(ww, wh);
        ww = std::max(ww, 1), wh = std::max(wh, 1);
        if (!hdrOut_ || hdrOutW_ != ww || hdrOutH_ != wh) {
            if (hdrOut_) dev_->destroyTexture(hdrOut_);
            gr::TextureDesc d;
            d.format = dev_->caps().supports(gr::Format::RGBA32F, gr::kRenderable) ? gr::Format::RGBA32F : gr::Format::RGBA16F;
            d.width = ww, d.height = wh, d.usage = gr::kSampledTexture | gr::kColorTarget;
            hdrOut_ = dev_->createTexture(d);
            hdrOutW_ = ww, hdrOutH_ = wh;
        }
        if (!post_) post_ = std::make_unique<CorePostFx>(*dev_);
        if (hdrOut_ && post_->hdrOutput(frameColor_, hdrOut_, ww, wh, r_.hdrWhite_, r_.hdrSurface_ == 2)) {
            dev_->present(hdrOut_, {0, 0, ww, wh}, gr::Filter::Nearest, true);  // GL style storage
        } else {
            if (!toldHdr_) SDL_Log("hdr: output shader: %s; showing the frame unconverted", dev_->lastError().c_str());
            toldHdr_ = true;
            dev_->present(frameColor_, {0, 0, frameW_, frameH_}, gr::Filter::Linear, true);
        }
    } else if (frameColor_) {
        dev_->present(frameColor_, {0, 0, frameW_, frameH_}, gr::Filter::Linear, true);  // GL style storage
    }
    dev_->endFrame();
}

void CoreRenderer::setFrameCap(int fps) {
    if (fps <= 0) {
        dev_->setPresentMode(gr::PresentMode::Immediate);
        paceNs_ = 0, nextFrameNs_ = 0;
        return;
    }
    fps = std::max(fps, 30);
    int interval = 0;
    if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(r_.window_)); m && m->refresh_rate > 0) {
        float n = m->refresh_rate / float(fps);
        if (n >= 0.95f && std::fabs(n - std::round(n)) < 0.05f) interval = int(std::round(n));
    }
    bool headless = SDL_GetCurrentVideoDriver() && std::string(SDL_GetCurrentVideoDriver()) == "offscreen";
    const bool vsync = interval > 0;
    dev_->setPresentMode(vsync ? gr::PresentMode::Vsync : gr::PresentMode::Immediate);
    paceNs_ = headless || (vsync && interval == 1) ? 0 : 1000000000ull / uint64_t(fps);
    nextFrameNs_ = 0;
}

float CoreRenderer::beginWide(float maxAspect) {
    int w = 0, h = 0;
    r_.frameSize(w, h);
    int* viewport = r_.viewport_;
    for (int k = 0; k < 4; ++k) r_.saved_[k] = viewport[k];
    const int vh = viewport[3];
    int vw = std::min(w, int(float(vh) * maxAspect));
    vw = std::max(vw, viewport[2]);
    viewport[0] = (w - vw) / 2;
    viewport[2] = vw;
    for (int k = 0; k < 4; ++k) vp_[k] = sc_[k] = viewport[k];
    rectsDirty_ = true;
    return float(vw) / float(vh);
}

void CoreRenderer::endWide() {
    for (int k = 0; k < 4; ++k) vp_[k] = sc_[k] = r_.viewport_[k] = r_.saved_[k];
    rectsDirty_ = true;
}

void CoreRenderer::clip43() {
    const int* viewport = r_.viewport_;
    int w = std::min(viewport[2], viewport[3] * 4 / 3);
    sc_[0] = viewport[0] + (viewport[2] - w) / 2, sc_[1] = viewport[1], sc_[2] = w, sc_[3] = viewport[3];
    rectsDirty_ = true;
}

void CoreRenderer::clipEnd() {
    for (int k = 0; k < 4; ++k) sc_[k] = r_.viewport_[k];
    rectsDirty_ = true;
}

void CoreRenderer::clearDepth() {
    // Local's glClear(DEPTH) inside the scissor: depth set to 1 by a draw (no pass break).
    fillRectPixels(nullptr, nullptr, true);
}

// ---------------------------------------------------------------- textures

int CoreRenderer::createTexture(const Image& image, bool repeat) {
    suspend();
    Tex t;
    gr::TextureDesc d;
    d.width = std::max(image.width, 1), d.height = std::max(image.height, 1), d.mips = 0;
    t.tex = dev_->createTexture(d);
    if (image.width > 0 && image.height > 0) dev_->updateTexture({t.tex}, image.width, image.height, image.rgba.data());
    dev_->generateMipmaps(t.tex);
    t.w = d.width, t.h = d.height, t.repeat = repeat;
    t.sampler = sampler(gr::Filter::Linear, gr::Filter::Linear, true, repeat);  // LINEAR_MIPMAP_LINEAR / LINEAR
    textures_.push_back(t);
    return int(textures_.size()) - 1;
}

void CoreRenderer::updateTexture(int texture, const Image& image) {
    if (texture <= 0 || size_t(texture) >= textures_.size() || image.width <= 0 || image.height <= 0) return;
    suspend();
    Tex& t = textures_[size_t(texture)];
    if (t.w != image.width || t.h != image.height) {
        // A new size: a new single-level texture (local re-specifies level 0 and samples only it).
        dev_->destroyTexture(t.tex);
        gr::TextureDesc d;
        d.width = image.width, d.height = image.height;
        t.tex = dev_->createTexture(d);
        t.w = image.width, t.h = image.height;
    }
    dev_->updateTexture({t.tex}, image.width, image.height, image.rgba.data());
    t.sampler = sampler(gr::Filter::Linear, gr::Filter::Linear, false, t.repeat);  // MIN_FILTER LINEAR: level 0 only
}

void CoreRenderer::setTextureNearest(int texture) {
    if (texture <= 0 || size_t(texture) >= textures_.size()) return;
    textures_[size_t(texture)].sampler = sampler(gr::Filter::Nearest, gr::Filter::Nearest, false, textures_[size_t(texture)].repeat);
}

// ---------------------------------------------------------------- drawing

// gd01 is a GL game: it samples its render targets (the crowd's impostor
// cards) with row 0 at the bottom, and its rectangles (viewport, scissor,
// copies, depth reads) count rows from the bottom. So every target here holds
// its picture GL style: clip y is negated (exact), the interface's rows are
// then local's rows, and the frame is flipped once when it is presented. On
// OpenGL3 this also rasterises exactly as local (the backend's own flip and
// this one cancel), so even edges through pixel centres match.
void CoreRenderer::setMvp(const float* m) {
    std::copy_n(m, 16, ds_.mvp);
    for (int c = 0; c < 4; ++c) ds_.mvp[c * 4 + 1] = -ds_.mvp[c * 4 + 1];
}

// Local adds uJitter * w to clip x and y; with clip y negated (setMvp) the y
// offset is negated too, so after the backend's flip the sum is local's, bit
// for bit (IEEE negation is exact).
void CoreRenderer::setJitter(const std::array<float, 2>& j) {
    ds_.uvOffsetJitter[2] = j[0];
    ds_.uvOffsetJitter[3] = -j[1];
}

void CoreRenderer::drawMain(int texture, Blend blend, bool depthTest, bool depthWrite, int cull, bool decal) {
    ensurePass();
    applyRects();
    // GL writes no depth while its depth test is off, whatever the mask says.
    dev_->bindPipeline(pipeline(kMain, blend, depthTest, depthTest && depthWrite, cull, decal, colorMask(), targetF16()));
    ds_.flags[0] = texture > 0;
    dev_->pushUniforms(0, &ds_, sizeof ds_);
    dev_->pushUniforms(1, &ls_, sizeof ls_);
    const Tex& t = texture > 0 && size_t(texture) < textures_.size() && textures_[size_t(texture)].tex ? textures_[size_t(texture)] : white_;
    gr::TextureBinding b{t.tex, t.sampler};
    dev_->bindTextures(0, &b, 1);
}

void CoreRenderer::draw(const Vertex* v, size_t count, int texture, Blend blend, const float* mvp, bool depthTest, bool depthWrite) {
    if (count == 0) return;
    setMvp(mvp ? mvp : r_.screenMvp_);
    ds_.fogColorCutout[3] = 0.0f;
    ds_.uvOffsetJitter[0] = ds_.uvOffsetJitter[1] = 0;
    for (float& f : ds_.tint) f = 1;
    ds_.flags[2] = 0, ds_.flags[1] = 0;
    ds_.texOColorScale[2] = 1;
    ds_.fog[0] = ds_.fog[1] = ds_.fog[2] = 0;
    ds_.matFlags[0] = 1, ds_.matFlags[1] = 0, ds_.matFlags[2] = 0;
    ds_.flags[3] = 0;
    ds_.flags2[0] = 0;
    // The vertices first: data uploads may not happen inside a pass on every backend; transient data may.
    ensurePass();
    gr::BufferRange vb = dev_->allocTransient(gr::kVertexBuffer, v, count * sizeof(Vertex));
    drawMain(texture, blend, depthTest, depthWrite, 0, false);
    dev_->bindVertexBuffers(0, &vb, 1);
    dev_->draw(uint32_t(count));
}

int CoreRenderer::createMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    suspend();
    Mesh m;
    m.vbBytes = std::max<size_t>(vertices.size() * sizeof(Vertex), 1);
    m.ibBytes = std::max<size_t>(indices.size() * sizeof(uint32_t), 1);
    m.vb = dev_->createBuffer(gr::BufferDesc{m.vbBytes, gr::kVertexBuffer}, vertices.empty() ? nullptr : vertices.data());
    m.ib = dev_->createBuffer(gr::BufferDesc{m.ibBytes, gr::kIndexBuffer}, indices.empty() ? nullptr : indices.data());
    m.count = int(indices.size());
    meshes_.push_back(m);
    return int(meshes_.size()) - 1;
}

void CoreRenderer::updateMesh(int mesh, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (mesh <= 0 || size_t(mesh) >= meshes_.size()) return;
    suspend();
    Mesh& m = meshes_[size_t(mesh)];
    const size_t vb = vertices.size() * sizeof(Vertex), ib = indices.size() * sizeof(uint32_t);
    if (vb > m.vbBytes) {
        dev_->destroyBuffer(m.vb);
        m.vbBytes = vb;
        m.vb = dev_->createBuffer(gr::BufferDesc{vb, gr::kVertexBuffer, true}, vertices.data());
    } else if (vb) {
        dev_->updateBuffer(m.vb, 0, vertices.data(), vb);
    }
    if (ib > m.ibBytes) {
        dev_->destroyBuffer(m.ib);
        m.ibBytes = ib;
        m.ib = dev_->createBuffer(gr::BufferDesc{ib, gr::kIndexBuffer, true}, indices.data());
    } else if (ib) {
        dev_->updateBuffer(m.ib, 0, indices.data(), ib);
    }
    m.count = int(indices.size());
}

void CoreRenderer::drawMesh(int mesh, const float* mvp, const Renderer::RenderState& st) {
    if (mesh <= 0 || size_t(mesh) >= meshes_.size() || meshes_[size_t(mesh)].count == 0) return;
    // The uniforms as local's drawMesh sets them; what it leaves alone keeps its last value.
    setMvp(mvp ? mvp : r_.screenMvp_);
    ds_.fogColorCutout[3] = st.alphaCutout ? 0.5f : -1.0f;
    ds_.uvOffsetJitter[0] = st.uvOffset[0], ds_.uvOffsetJitter[1] = st.uvOffset[1];
    std::copy_n(st.tint, 4, ds_.tint);
    if (st.bones) {
        ds_.flags[1] = 1;
        std::copy_n(st.bones, 64, ds_.bones);
    } else {
        ds_.flags[1] = 0;
    }
    ds_.texOColorScale[2] = st.colorScale;
    ds_.flags2[0] = st.texDecal;
    const bool texgen = st.model && (st.texgen == 5 || st.texgen == 2);
    ds_.flags[3] = texgen ? st.texgen : 0;
    if (texgen) {
        std::copy_n(st.model, 16, ds_.model);
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r) ds_.texU[c * 4 + r] = st.texU[c * 3 + r];
        ds_.texOColorScale[0] = st.texO[0], ds_.texOColorScale[1] = st.texO[1];
        std::copy_n(st.eye, 3, ds_.eye);
        std::copy_n(st.camRight, 3, ds_.camRight);
        std::copy_n(st.camUp, 3, ds_.camUp);
    }
    ds_.matFlags[0] = st.useEnviron ? 1.0f : 0.0f, ds_.matFlags[1] = st.vertAmb ? 1.0f : 0.0f, ds_.matFlags[2] = st.vertDyn ? 1.0f : 0.0f;
    if (st.fog) {
        ds_.fog[0] = 1, ds_.fog[1] = st.fogStart, ds_.fog[2] = st.fogEnd;
        std::copy_n(st.fogColor, 3, ds_.fogColorCutout);
    } else {
        ds_.fog[0] = ds_.fog[1] = ds_.fog[2] = 0;
    }
    if (st.lighting && st.model) {
        const Lighting& l = *st.lighting;
        ds_.flags[2] = 1;
        std::copy_n(st.model, 16, ds_.model);
        std::copy_n(l.ambient, 3, ls_.ambient);
        ds_.flags2[1] = l.count;
        for (int i = 0; i < l.count && i < Lighting::kMaxLights; ++i) {
            std::copy_n(l.dir[i], 4, ls_.dir[i]);
            std::copy_n(l.color[i], 4, ls_.color[i]);
        }
    } else {
        ds_.flags[2] = 0;
    }
    // The picture is stored GL style (setMvp negates y), which swaps the culled face.
    const int cull = st.cull ? (st.mirrored ? 1 : 2) : 0;
    drawMain(st.texture, st.blend, st.depthTest, st.depthWrite, cull, st.decal);
    const Mesh& m = meshes_[size_t(mesh)];
    gr::BufferRange vb{m.vb, 0, m.vbBytes};
    dev_->bindVertexBuffers(0, &vb, 1);
    dev_->bindIndexBuffer(gr::BufferRange{m.ib, 0, m.ibBytes}, gr::IndexType::U32);
    dev_->drawIndexed(uint32_t(m.count));
}

// ---------------------------------------------------------------- render targets

int CoreRenderer::createRenderTarget(int width, int height) {
    suspend();
    gr::TextureDesc c;
    c.width = width, c.height = height, c.usage = gr::kSampledTexture | gr::kColorTarget;
    Tex t;
    t.tex = dev_->createTexture(c);
    gr::TextureDesc d = c;
    d.format = gr::Format::D24, d.usage = gr::kDepthTarget;
    Texture depth = dev_->createTexture(d);
    if (!t.tex || !depth) return 0;
    t.w = width, t.h = height;
    t.sampler = sampler(gr::Filter::Linear, gr::Filter::Linear, false, false);
    textures_.push_back(t);
    const int handle = int(textures_.size()) - 1;
    targetDepth_[handle] = depth;
    return handle;
}

void CoreRenderer::beginRenderTarget(int texture) {
    auto it = targetDepth_.find(texture);
    if (it == targetDepth_.end()) return;
    suspend();
    setJitter({0, 0});  // impostors and other targets are not part of the TAA frame
    target_ = texture;
    gr::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = textures_[size_t(texture)].tex;  // cleared to transparent
    p.depth.texture = it->second;
    p.depth.stencilLoad = gr::LoadOp::DontCare;
    dev_->beginPass(p);
    const Tex& t = textures_[size_t(texture)];
    vp_[0] = vp_[1] = 0, vp_[2] = t.w, vp_[3] = t.h;
    scissorOn_ = false, rectsDirty_ = true;
}

void CoreRenderer::endRenderTarget() {
    suspend();
    if (r_.sceneActive_) setJitter(r_.sceneJitter_);
    target_ = 0;
    for (int k = 0; k < 4; ++k) vp_[k] = sc_[k] = r_.viewport_[k];
    scissorOn_ = true, rectsDirty_ = true;
}

// ---------------------------------------------------------------- depth of field, track fade

bool CoreRenderer::ensureBlurProgram() { return shaderOk_[kBlur]; }

void CoreRenderer::depthBlur(float depth, float tapPx) {
    if (depth <= 0 || depth >= 1 || !ensureBlurProgram()) return;
    const int* viewport = r_.viewport_;
    const int x = viewport[0], y = viewport[1], w = viewport[2], h = viewport[3];
    const int top = y;  // rows as local's (GL style storage)
    suspend();
    if (!blurTex_ || blurW_ != w || blurH_ != h) {
        if (blurTex_) dev_->destroyTexture(blurTex_);
        gr::TextureDesc d;
        d.width = w, d.height = h, d.usage = gr::kSampledTexture | gr::kColorTarget;
        blurTex_ = dev_->createTexture(d);
        blurW_ = w, blurH_ = h;
    }
    // Local's glCopyTexSubImage2D of the viewport.
    copyFromTarget(x, top, blurTex_, w, h);
    ensurePass();
    applyRects();
    dev_->bindPipeline(pipeline(kBlur, Blend::Opaque, true, false, 0, false, colorMask(), targetF16()));
    const float u[8] = {depth, tapPx, float(w), float(h), float(x), float(top), 0, 0};
    dev_->pushUniforms(0, u, sizeof u);
    gr::TextureBinding b{blurTex_, sampler(gr::Filter::Linear, gr::Filter::Linear, false, false)};
    dev_->bindTextures(0, &b, 1);
    dev_->draw(4);
}

void CoreRenderer::grabNdc(int slot, float x0, float y0, float x1, float y1) {
    Grab& g = grabs_[slot & 1];
    g.valid = false;
    const int* viewport = r_.viewport_;
    int px0 = viewport[0] + int(std::floor((x0 + 1) * 0.5f * float(viewport[2])));
    int px1 = viewport[0] + int(std::ceil((x1 + 1) * 0.5f * float(viewport[2])));
    int py0 = viewport[1] + int(std::floor((y1 + 1) * 0.5f * float(viewport[3])));  // bottom (GL style)
    int py1 = viewport[1] + int(std::ceil((y0 + 1) * 0.5f * float(viewport[3])));   // top
    int w = px1 - px0, h = py1 - py0;
    if (w <= 0 || h <= 0) return;
    if (!g.texture || g.w != w || g.h != h) {
        Image blank;
        blank.width = w, blank.height = h;
        blank.rgba.assign(size_t(w) * size_t(h) * 4, 0);
        if (!g.texture) g.texture = createTexture(blank, false);
        else updateTexture(g.texture, blank);
        g.w = w, g.h = h;
    }
    setTextureNearest(g.texture);
    suspend();
    const int top = py0;  // rows as local's (GL style storage)
    copyFromTarget(px0, top, textures_[size_t(g.texture)].tex, w, h);
    g.vx0 = float(px0 - viewport[0]) / float(viewport[2]) * kScreenW;
    g.vx1 = float(px1 - viewport[0]) / float(viewport[2]) * kScreenW;
    g.vy0 = (1 - float(py1 - viewport[1]) / float(viewport[3])) * kScreenH;
    g.vy1 = (1 - float(py0 - viewport[1]) / float(viewport[3])) * kScreenH;
    g.valid = true;
}

void CoreRenderer::drawGrab(int slot, float add) {
    const Grab& g = grabs_[slot & 1];
    if (!g.valid) return;
    // The copy is bottom-up as local's: v 1 on the top edge.
    const Vertex v[6] = {{g.vx0, g.vy0, 0, 0, 1, 255, 255, 255, 255}, {g.vx1, g.vy0, 0, 1, 1, 255, 255, 255, 255},
                         {g.vx1, g.vy1, 0, 1, 0, 255, 255, 255, 0},   {g.vx0, g.vy0, 0, 0, 1, 255, 255, 255, 255},
                         {g.vx1, g.vy1, 0, 1, 0, 255, 255, 255, 0},   {g.vx0, g.vy1, 0, 0, 0, 255, 255, 255, 0}};
    draw(v, 6, g.texture, Blend::Alpha, nullptr, false, false);
    if (add > 0) {
        uint8_t a = uint8_t(std::lround(add * 255));
        const Vertex q[6] = {{g.vx0, g.vy0, 0, 0, 0, a, a, a, 255}, {g.vx1, g.vy0, 0, 0, 0, a, a, a, 255},
                             {g.vx1, g.vy1, 0, 0, 0, a, a, a, 255}, {g.vx0, g.vy0, 0, 0, 0, a, a, a, 255},
                             {g.vx1, g.vy1, 0, 0, 0, a, a, a, 255}, {g.vx0, g.vy1, 0, 0, 0, a, a, a, 255}};
        draw(q, 6, 0, Blend::Add, nullptr, false, false);
    }
}

// ---------------------------------------------------------------- depth reads (lens flares)

// Points in viewport pixels (x, y from the bottom), as local's readDepthsAsync.
void CoreRenderer::readDepthsAsync(unsigned& buffer, size_t& capacity, const std::vector<std::array<int, 2>>& points) {
    if (points.empty() || !shaderOk_[kProbe]) return;
    if (!buffer) buffer = nextProbe_++;
    capacity = std::max(capacity, points.size());
    const int n = int(points.size());
    suspend();
    if (!probeTex_ || probeW_ < n) {
        if (probeTex_) dev_->destroyTexture(probeTex_);
        gr::TextureDesc d;
        d.width = std::max(n, 64), d.height = 1, d.usage = gr::kSampledTexture | gr::kColorTarget;
        probeTex_ = dev_->createTexture(d);
        probeW_ = d.width;
    }
    const int* viewport = r_.viewport_;
    std::vector<float> verts;
    verts.reserve(size_t(n) * 24);
    for (int i = 0; i < n; ++i) {
        const float x0 = float(i) / float(probeW_) * 2 - 1, x1 = float(i + 1) / float(probeW_) * 2 - 1;
        const float px = float(viewport[0] + points[size_t(i)][0]) + 0.5f;
        const float py = float(viewport[1] + points[size_t(i)][1]) + 0.5f;  // rows as local's
        const float q[6][2] = {{x0, -1}, {x1, -1}, {x1, 1}, {x0, -1}, {x1, 1}, {x0, 1}};
        for (const auto& c : q) verts.insert(verts.end(), {c[0], c[1], px, py});
    }
    const int saved = target_;
    gr::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = probeTex_;
    p.colors[0].load = gr::LoadOp::DontCare;
    dev_->beginPass(p);
    dev_->bindPipeline(pipeline(kProbe, Blend::Opaque, false, false, 0, false, gr::kWriteAll));
    gr::BufferRange vb = dev_->allocTransient(gr::kVertexBuffer, verts.data(), verts.size() * sizeof(float));
    dev_->bindVertexBuffers(0, &vb, 1);
    gr::TextureBinding b{saved == 0 ? mainDepth() : targetDepth_[saved], sampler(gr::Filter::Nearest, gr::Filter::Nearest, false, false)};
    dev_->bindTextures(0, &b, 1);
    dev_->draw(uint32_t(n * 6));
    dev_->endPass();
    if (auto it = probes_.find(buffer); it != probes_.end() && it->second.first) {
        std::vector<uint8_t> drop(it->second.second * 4);
        dev_->readbackData(it->second.first, drop.data(), drop.size());  // an unread one: freed
    }
    probes_[buffer] = {dev_->readTexture(probeTex_, {0, 0, n, 1}), size_t(n)};
}

std::vector<float> CoreRenderer::mapDepths(unsigned buffer, size_t count) {
    std::vector<float> out;
    auto it = probes_.find(buffer);
    if (it == probes_.end() || !it->second.first || !count) return out;
    std::vector<uint8_t> px(it->second.second * 4);
    if (dev_->readbackData(it->second.first, px.data(), px.size())) {
        count = std::min(count, it->second.second);
        for (size_t i = 0; i < count; ++i) {
            const uint32_t k = uint32_t(px[i * 4]) << 16 | uint32_t(px[i * 4 + 1]) << 8 | px[i * 4 + 2];
            out.push_back(float(k) / 16777215.0f);
        }
    }
    it->second.first = {};
    return out;
}

float CoreRenderer::readDepth(int x, int y) {
    unsigned b = 0;
    size_t cap = 0;
    readDepthsAsync(b, cap, {{x, y}});
    std::vector<float> d = mapDepths(b, 1);
    probes_.erase(b);
    return d.empty() ? 1.0f : d[0];
}

// ---------------------------------------------------------------- post-processing (Renderer::beginScene / endScene)

std::string CoreRenderer::upscalerStatus() const { return post_ ? post_->upscalerStatus() : std::string(); }

bool CoreRenderer::beginScene() {
    // True HDR: the display's headroom, when the surface and the display show HDR (else tone mapped to SDR).
    PostSettings& post = r_.post_;
    post.hdrPeak = post.hdr == 2 && r_.hdrSurface_ && r_.hdrOn_ ? r_.hdrHeadroom_ : 1.0f;
    if (!post.any() || r_.sceneActive_) return false;
    if (!post_) post_ = std::make_unique<CorePostFx>(*dev_);
    // A shader the driver rejects turns the effects off (local's rule: never a black venue).
    if (!post_->ready()) {
        if (!toldPost_) SDL_Log("post-processing is off: a shader failed to compile (see above); drawing without effects");
        toldPost_ = true;
        return false;
    }
    int* viewport = r_.viewport_;
    const int vw = viewport[2], vh = viewport[3];
    const float scale = post.renderScale();
    const int sw = std::max(1, int(std::lround(vw * scale))), sh = std::max(1, int(std::lround(vh * scale)));
    const bool f16 = post.hdr != 0;
    suspend();
    if (!sceneColor_ || sceneW_ != sw || sceneH_ != sh || sceneF16_ != f16) {
        if (sceneColor_) dev_->destroyTexture(sceneColor_), dev_->destroyTexture(sceneDepth_);
        // HDR (Emulated / True): floating point, so lights and additive effects keep values above 1.0.
        gr::TextureDesc c;
        c.format = f16 ? gr::Format::RGBA16F : gr::Format::RGBA8;
        c.width = sw, c.height = sh, c.usage = gr::kSampledTexture | gr::kColorTarget;
        c.label = "post-processing scene";
        sceneColor_ = dev_->createTexture(c);
        gr::TextureDesc d = c;
        d.format = gr::Format::D24, d.usage = gr::kSampledTexture | gr::kDepthTarget;  // sampled: SSAO, TAA, Star Power blur
        sceneDepth_ = dev_->createTexture(d);
        sceneW_ = sw, sceneH_ = sh, sceneF16_ = f16;
        if (!sceneColor_ || !sceneDepth_) {
            SDL_Log("post-processing: scene target %dx%d: %s", sw, sh, dev_->lastError().c_str());
            if (sceneColor_) dev_->destroyTexture(sceneColor_);
            if (sceneDepth_) dev_->destroyTexture(sceneDepth_);
            sceneColor_ = sceneDepth_ = {};
            return false;
        }
    }
    for (int k = 0; k < 4; ++k) r_.sceneSaved_[k] = viewport[k];
    r_.sceneActive_ = true;
    target_ = 0;
    viewport[0] = viewport[1] = 0, viewport[2] = sw, viewport[3] = sh;
    for (int k = 0; k < 4; ++k) vp_[k] = sc_[k] = viewport[k];
    scissorOn_ = true, rectsDirty_ = true;
    // Local clears the whole scene (colour and depth) when it binds it: the pass's clears.
    gr::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = sceneColor_;
    p.colors[0].clear[3] = 1;  // local keeps alpha masked; the chain never reads it
    p.depth.texture = sceneDepth_;
    p.depth.stencilLoad = gr::LoadOp::DontCare;
    p.label = "scene";
    dev_->beginPass(p);
    r_.sceneJitter_ = post_->jitter(post, sw, sh);
    setJitter(r_.sceneJitter_);
    return true;
}

void CoreRenderer::endScene(const PostFrame& frame) {
    if (!r_.sceneActive_) return;
    r_.sceneJitter_ = {0, 0};
    setJitter(r_.sceneJitter_);
    suspend();
    target_ = 0;
    const Texture out = post_->process(sceneColor_, sceneDepth_, sceneW_, sceneH_, r_.sceneSaved_[2], r_.sceneSaved_[3], r_.post_, frame);
    r_.sceneActive_ = false;
    int* viewport = r_.viewport_;
    for (int k = 0; k < 4; ++k) viewport[k] = r_.sceneSaved_[k], vp_[k] = sc_[k] = viewport[k];
    scissorOn_ = true, rectsDirty_ = true;
    if (!out) return;
    // Local's PostFx::blit into the frame's viewport: no blending, no depth test, alpha kept.
    ensurePass();
    applyRects();
    post_->blit(out, frameF16_ ? gr::Format::RGBA16F : gr::Format::RGBA8, gr::kWriteRGB, gr::Format::D24);
}

int CoreRenderer::deviceHdrSurface(std::string& reason) const {
    const int s = dev_ ? dev_->hdrSurface() : 0;
    if (!s) reason = dev_ ? dev_->hdrReason() : "no graphics device";
    return s;
}

// Renderer::hdrStatus's text (the same window state, read by Renderer::updateHdr).
std::string CoreRenderer::hdrStatus() const {
    if (!r_.hdrSurface_) return "off: no HDR surface";
    if (!r_.hdrOn_) return "off: HDR is off for this display (turn it on in the system's display settings)";
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s output, paper white %.0f nits, peak %.0f nits", r_.hdrSurface_ == 2 ? "PQ (BT.2020)" : "scRGB",
                  double(r_.hdrWhite_ * 80), double(r_.hdrWhite_ * r_.hdrHeadroom_ * 80));
    return buf;  // (local's "highlights in SDR while FSR 3.1 / DLSS runs" does not apply: they do not run here)
}

// ---------------------------------------------------------------- screenshots, shader test

Image CoreRenderer::readPixels() {
    Image img;
    if (!frameColor_) return img;
    const int* viewport = r_.viewport_;
    img.width = std::max(frameW_, viewport[2]);
    img.height = viewport[3];
    img.rgba.resize(size_t(img.width) * size_t(img.height) * 4);
    const int x = img.width == viewport[2] ? viewport[0] : 0;
    const int top = viewport[1];  // rows bottom-up, as local's glReadPixels
    const bool resume = dev_->inPass();
    suspend();
    Texture src = frameColor_;
    if (frameF16_) {
        // An RGBA16F frame (True HDR): local's glReadPixels converts it to
        // RGBA8; here a 1:1 blit into an RGBA8 texture does the same conversion.
        if (!readCopy_ || readCopyW_ != frameW_ || readCopyH_ != frameH_) {
            if (readCopy_) dev_->destroyTexture(readCopy_);
            gr::TextureDesc d;
            d.width = frameW_, d.height = frameH_, d.usage = gr::kSampledTexture | gr::kColorTarget;
            readCopy_ = dev_->createTexture(d);
            readCopyW_ = frameW_, readCopyH_ = frameH_;
        }
        dev_->blit({frameColor_}, frameW_, frameH_, {readCopy_}, frameW_, frameH_, gr::Filter::Nearest);
        src = readCopy_;
    }
    gr::Readback rb = dev_->readTexture(src, {x, top, img.width, img.height});
    if (!rb || !dev_->readbackData(rb, img.rgba.data(), img.rgba.size())) img.rgba.assign(img.rgba.size(), 0);
    if (resume) ensurePass();
    std::vector<uint8_t> row(size_t(img.width) * 4);
    for (int y = 0; y < img.height / 2; ++y) {  // to top-down, as local
        uint8_t* a = &img.rgba[size_t(y) * size_t(img.width) * 4];
        uint8_t* b = &img.rgba[size_t(img.height - 1 - y) * size_t(img.width) * 4];
        std::copy_n(a, row.size(), row.data());
        std::copy_n(b, row.size(), a);
        std::copy_n(row.data(), row.size(), b);
    }
    return img;
}

std::vector<std::string> CoreRenderer::compileAllShaders(int& programs) {
    static const char* names[kPrograms] = {"core main", "core fill", "core depth of field", "core depth probe"};
    std::vector<std::string> failed;
    programs = kPrograms + CorePostFx::kPrograms;
    for (int p = 0; p < kPrograms; ++p)
        if (!shaderOk_[p]) failed.push_back(names[p]);
    if (!post_) post_ = std::make_unique<CorePostFx>(*dev_);
    post_->ready();
    for (const std::string& n : post_->failedPrograms()) failed.push_back("core " + n);
    return failed;
}
