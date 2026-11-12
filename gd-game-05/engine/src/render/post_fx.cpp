// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/post_fx.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "render/renderer.h"
#include "scene/world_objects.h"

namespace gfx {
namespace {

// The passes' shared constants (std140, vec4 members only).
struct PostBlock {
    float dof[4];          // x 1 / (zf - zb), y -zf / (zf - zb), z least blur, w most blur
    float bloom[4];        // x bloom intensity, y the UI plane (no post at or nearer this depth)
    float thresholds[4];   // gBloomColor: the bloom mask's weights (rgb) and offset (w)
    float colorXfm[3][4];  // rows: rgb select, w bias
    float noisePhase[4];
    float noiseScale[4];   // xy base scale, z the second layer's extra scale, w intensity
    float source[4];       // xy the source's texel size, z debug view
    float taps[16][4];     // blur taps: xy offset (uv), z weight
};

// GH2's DoPost sets the UI plane to 0.1 every frame [C] (0x82303E68: rnd
// +0x2b0 = 0x3dcccccd, then constant c28).
constexpr float kUiPlane = 0.1f;
// The DOF blur (0x82302308) [C]: 7 taps (0x827469F4) weighted 1/7, at
// these offsets times 0.666 (0x827469F0) low-resolution texels, across
// then down; the low-resolution buffer is 640x320 at 1280x720.
constexpr float kDofTaps[7] = {-2.5f, -1.5f, 0.0f, 1.5f, 2.5f, 3.5f, -3.5f};
constexpr float kDofSpread = 0.666f;
constexpr float kDofLowW = 640, kDofLowH = 320;

const char* kVertex = R"(
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Our GLSL for RB's postprocess / bloom / blur / downsample shaders.
const char* kFragment = R"(
in vec2 vUv;
out vec4 fragColor;
layout(std140) uniform GdPost {
    vec4 uDof;
    vec4 uBloom;
    vec4 uThresholds;
    vec4 uColorXfm[3];
    vec4 uNoisePhase;
    vec4 uNoiseScale;
    vec4 uSource;
    vec4 uTaps[16];
};
const vec3 kLuminance = vec3(0.2125, 0.7154, 0.0721);

#if PASS_COLLAPSE
// The HDR buffer, unpacked (x 4) and box-filtered to the DOF buffer.
uniform sampler2D sSrc;
void main() {
    vec2 d = 0.5 * uSource.xy;
    vec3 sum = texture(sSrc, vUv + vec2(-d.x, -d.y)).rgb + texture(sSrc, vUv + vec2(d.x, -d.y)).rgb +
               texture(sSrc, vUv + vec2(-d.x, d.y)).rgb + texture(sSrc, vUv + vec2(d.x, d.y)).rgb;
    fragColor = vec4(sum, 0.0);  // (sum / 4) unpacked
}
#elif PASS_BRIGHT
// RB's "bloom" pass (rgb x the bloom mask) on each full-size texel under
// this half-size pixel, averaged (RB then halves twice with a 2x2 box). The
// mask, sat(gBloomColor . (rgb, 1)), is what RB's scene shaders write in
// alpha from their unclamped colour; the stored colour is clamped [I].
uniform sampler2D sSrc;
vec3 bright(vec2 uv) {
    vec3 c = texture(sSrc, uv).rgb * 4.0;
    float m = clamp(dot(uThresholds.rgb, c) + uThresholds.w, 0.0, 1.0);
    return min(c, vec3(1.0)) * m;
}
void main() {
    vec2 d = 0.5 * uSource.xy;
    fragColor = vec4(0.25 * (bright(vUv + vec2(-d.x, -d.y)) + bright(vUv + vec2(d.x, -d.y)) +
                             bright(vUv + vec2(-d.x, d.y)) + bright(vUv + vec2(d.x, d.y))), 1.0);
}
#elif PASS_COPY
uniform sampler2D sSrc;
void main() {
    fragColor = vec4(texture(sSrc, vUv).rgb, 1.0);
}
#elif PASS_DOWNSAMPLE
// A 2x2 box: one bilinear fetch between the four texels.
uniform sampler2D sSrc;
void main() {
    fragColor = texture(sSrc, vUv);
}
#elif PASS_BLUR
uniform sampler2D sSrc;
void main() {
    vec4 sum = vec4(0.0);
    for (int i = 0; i < NUM_TAPS + 1; ++i) sum += uTaps[i].z * texture(sSrc, vUv + uTaps[i].xy);
    fragColor = sum;
}
#else
// The final pass (RB's postprocess): unpack, then the world's effects in
// RB's order.
uniform sampler2D sHdr;
uniform sampler2D sDofLow;
uniform sampler2D sDepth;
uniform sampler2D sBloom;
uniform sampler2D sLum;
uniform sampler2D sNoise;
void main() {
    vec4 c = texture(sHdr, vUv);
    c.rgb *= 4.0;
#if !HDR_OUT
    c.rgb = min(c.rgb, vec3(1.0));  // RB's world buffer holds 8 bits a channel [I]
#endif
    float z = texture(sDepth, vUv).r;
    float blur = 0.0, lumFactor = 1.0;
    if (z > uBloom.y) {
#if DOF
        blur = clamp(abs(z * uDof.x + uDof.y), uDof.z, uDof.w);
        c = mix(c, texture(sDofLow, vUv), blur);
#endif
#if BLOOM
        c.rgb += texture(sBloom, vUv).rgb * uBloom.x;
#endif
#if NOISE
        // The film grain: two layers of the noise map multiplied, then an
        // overlay blend (2cn below half, 1 - 2(1 - c)(1 - n) above) mixed
        // in by the intensity (postprocess 0x16).
        vec3 n0 = texture(sNoise, (vUv + uNoisePhase.xy) * uNoiseScale.xy).rgb;
        vec3 n1 = texture(sNoise, (vUv + uNoisePhase.zw) * uNoiseScale.xy * uNoiseScale.z).rgb;
        vec3 n = n0 * n1;
        vec3 over = mix(2.0 * c.rgb * n, 1.0 - 2.0 * (1.0 - c.rgb) * (1.0 - n), step(vec3(0.5), c.rgb));
        c.rgb = mix(c.rgb, over, uNoiseScale.w);
#endif
#if COLORXFM
        c.rgb = vec3(dot(uColorXfm[0].xyz, c.rgb) + uColorXfm[0].w, dot(uColorXfm[1].xyz, c.rgb) + uColorXfm[1].w,
                     dot(uColorXfm[2].xyz, c.rgb) + uColorXfm[2].w);
#endif
#if LUMMAP
        // A tone curve over luminance / 4; above 1 it is divided out, so
        // the brightest white stays neutral.
        float l = dot(c.rgb, kLuminance) * 0.25;
        lumFactor = texture(sLum, vec2(l, 0.5)).r;
        if (l > 1.0) lumFactor /= l;
        c.rgb *= lumFactor;
#endif
    }
#if DEBUG
    int view = int(uSource.z + 0.5);
    if (view == 1) {
        c = vec4(texture(sBloom, vUv).rgb, 1.0);
    } else if (view == 2) {
        c = vec4(vec3(blur), 1.0);
    } else if (view == 3) {
        c = vec4(vec3(lumFactor * 0.5), 1.0);
    } else {
        c = texture(sDofLow, vUv);
    }
#endif
#if HDR_OUT
    fragColor = vec4(max(c.rgb, 0.0), 1.0);  // Enhanced > HDR: highlights above 1.0 go on to the port's tone map
#else
    fragColor = vec4(clamp(c.rgb, 0.0, 1.0), 1.0);
#endif
}
#endif
)";

const VertexLayout& triangleLayout() {
    static const VertexLayout layout = [] {
        VertexLayout l;
        l.stride = 8;
        l.attribs[0] = {0, 2, AttribType::Float, 0};
        return l;
    }();
    return layout;
}

}  // namespace

bool PostProcState::colorXfmOn() const {
    static const std::array<float, 12> identity{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    return colorXfm != identity;
}

PostProcState postProcState(const scene::PostProc& pp, const std::function<TextureId(const std::string&)>& texture) {
    PostProcState s;
    s.bloomColor = pp.bloomColor;
    s.bloomThreshold = pp.bloomThreshold;
    s.bloomSize = pp.bloomSize;
    s.bloomIntensity = pp.bloomIntensity;
    s.luminanceMap = pp.luminanceMap.empty() ? TextureId{} : texture(pp.luminanceMap);
    s.colorXfm = pp.colorXfm;
    s.flickerIntensity = pp.flickerIntensity;
    s.flickerSecs = pp.flickerSecs;
    s.noiseBaseScale = pp.noiseBaseScale;
    s.noiseTopScale = pp.noiseTopScale;
    s.noiseIntensity = pp.noiseIntensity;
    s.noiseStationary = pp.noiseStationary;
    s.noiseMap = pp.noiseMap.empty() ? TextureId{} : texture(pp.noiseMap);
    s.emulateFps = pp.emulateFps;
    return s;
}

// PostProcBlendTask's lerp (RB 0x82209D08): every number but the bloom
// threshold, the stationary flag and the maps (the destination's).
PostProcState blendPostProc(const PostProcState& from, const PostProcState& to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](float a, float b) { return a + (b - a) * t; };
    PostProcState s = to;
    for (size_t i = 0; i < s.bloomColor.size(); ++i) s.bloomColor[i] = mix(from.bloomColor[i], to.bloomColor[i]);
    s.bloomSize = mix(from.bloomSize, to.bloomSize);
    s.bloomIntensity = mix(from.bloomIntensity, to.bloomIntensity);
    for (size_t i = 0; i < s.colorXfm.size(); ++i) s.colorXfm[i] = mix(from.colorXfm[i], to.colorXfm[i]);
    for (size_t i = 0; i < s.noiseBaseScale.size(); ++i) s.noiseBaseScale[i] = mix(from.noiseBaseScale[i], to.noiseBaseScale[i]);
    s.noiseTopScale = mix(from.noiseTopScale, to.noiseTopScale);
    s.noiseIntensity = mix(from.noiseIntensity, to.noiseIntensity);
    s.emulateFps = mix(from.emulateFps, to.emulateFps);
    return s;
}

// Rnd::SetDOF, DX 0x82302728 [C]: on when max_blur > 0; blur_depth at least
// 0.001; the focus and the point focus * (1 - blur_depth) become depth
// buffer values (0 when nearer than the near plane).
DofState DofState::fromShot(float focusDistance, float blurDepth, float maxBlur, float nearPlane, float farPlane) {
    DofState d;
    d.on = maxBlur > 0;
    d.focusDistance = focusDistance;
    d.blurDepth = std::max(blurDepth, 0.001f);
    d.maxBlur = maxBlur;
    d.nearPlane = nearPlane, d.farPlane = farPlane;
    return d;
}

PostFx::PostFx(Renderer& renderer) : renderer_(renderer), device_(renderer.device()) {
    const char* off = SDL_getenv("GD05_POST");  // development aid: GD05_POST=0 draws the world without post-processing
    disabled_ = off && std::strcmp(off, "0") == 0;
    // Development aid: GD05_POST_DEBUG=bloom|dof|lum|doflow shows the bloom
    // buffer, the DOF blend, the luminance map's factor (x 0.5) or the DOF
    // buffer instead of the world.
    if (const char* d = SDL_getenv("GD05_POST_DEBUG")) {
        const std::string v = d;
        debug_ = v == "bloom" ? 1 : v == "dof" ? 2 : v == "lum" ? 3 : v == "doflow" ? 4 : 0;
    }
    const float tri[6] = {-1, -1, 3, -1, -1, 3};
    triangle_ = device_.createBuffer(BufferKind::Vertex, sizeof tri, tri, false);
    // The renderer's noise map (GH2's own, rnd +0x29c, is not on the disc
    // [I]): 64x64 white noise.
    std::vector<uint8_t> px(64 * 64 * 4);
    uint32_t r = 0x9e3779b9u;
    for (uint8_t& v : px) {
        r ^= r << 13, r ^= r >> 17, r ^= r << 5;
        v = uint8_t(r >> 24);
    }
    stationaryPhase_ = {random(), random()};
    TexLevel level{px.data(), px.size()};
    std::string error;
    noise_ = device_.createTexture({64, 64, 1, TexFormat::RGBA8, TexKind::Tex2D}, &level, error);
}

PostFx::~PostFx() {
    destroyTargets();
    for (auto& [key, p] : programs_)
        if (p) device_.destroyProgram(p);
    if (triangle_) device_.destroyBuffer(triangle_);
    if (noise_) device_.destroyTexture(noise_);
}

void PostFx::destroyTargets() {
    for (TargetId* t : {&hdr_, &dofA_, &dofB_, &bloomHalf_, &bloomA_, &bloomB_, &ldr_}) {
        if (*t) device_.destroyTarget(*t);
        *t = {};
    }
    w_ = h_ = 0;
}

bool PostFx::ensureTargets(int w, int h) {
    const bool f16 = settings_.hdr != 0;
    if (hdr_ && w == w_ && h == h_ && f16 == worldF16_) return true;
    destroyTargets();
    std::string error;
    // Enhanced > HDR: the world and depth-of-field buffers in floating point
    // (the same colours / 4, kept above 1.0 and finer).
    worldF16_ = f16;
    if (f16) {
        hdr_ = device_.createTarget(TargetDesc{w, h, true, TargetFormat::RGBA16F}, error);
        if (!hdr_) SDL_Log("post: no RGBA16F world buffer for HDR (%s); using GH2's", error.c_str());
    }
    // GH2's buffer is 2:10:10:10 [C] (colours / 4 by the HDR key); RGBA8
    // keeps the same scale where 10-bit targets fail.
    if (!hdr_) hdr_ = device_.createTarget(TargetDesc{w, h, true, TargetFormat::RGB10A2}, error);
    hdr10_ = bool(hdr_);
    if (!hdr_) {
        SDL_Log("post: no 10-bit HDR buffer (%s); using 8 bits", error.c_str());
        hdr_ = device_.createTarget(TargetDesc{w, h, true, TargetFormat::RGBA8}, error);
    }
    const int hw = std::max(1, w / 2), hh = std::max(1, h / 2), qw = std::max(1, w / 4), qh = std::max(1, h / 4);
    // The DOF and bloom buffers are 8-bit in GH2 (the screen's format, 0x821D5E30) [I]: they clamp at 1.
    const TargetFormat dofFormat = f16 ? TargetFormat::RGBA16F : TargetFormat::RGBA8;
    dofA_ = device_.createTarget(TargetDesc{hw, hh, false, dofFormat}, error);
    dofB_ = device_.createTarget(TargetDesc{hw, hh, false, dofFormat}, error);
    if (f16 && (!dofA_ || !dofB_)) {
        if (dofA_) device_.destroyTarget(dofA_);
        if (dofB_) device_.destroyTarget(dofB_);
        dofA_ = device_.createTarget(TargetDesc{hw, hh, false, TargetFormat::RGBA8}, error);
        dofB_ = device_.createTarget(TargetDesc{hw, hh, false, TargetFormat::RGBA8}, error);
    }
    bloomHalf_ = device_.createTarget(TargetDesc{hw, hh, false, TargetFormat::RGBA8}, error);
    bloomA_ = device_.createTarget(TargetDesc{qw, qh, false, TargetFormat::RGBA8}, error);
    bloomB_ = device_.createTarget(TargetDesc{qw, qh, false, TargetFormat::RGBA8}, error);
    if (!hdr_ || !dofA_ || !dofB_ || !bloomHalf_ || !bloomA_ || !bloomB_) {
        SDL_Log("post: cannot make the buffers (%s); post-processing off", error.c_str());
        destroyTargets();
        return false;
    }
    w_ = w, h_ = h;
    return true;
}

ProgramDesc PostFx::describe(Pass pass, uint32_t key) {
    ProgramDesc d;
    d.vertex = kVertex;
    d.fragment = kFragment;
    d.uniformBlocks = {"GdPost"};
    auto def = [&](const char* name, int v) { d.defines += std::string("#define ") + name + " " + std::to_string(v) + "\n"; };
    // Every switch is defined (OpenGL ES rejects undefined names in #if).
    def("PASS_COLLAPSE", pass == Pass::Collapse);
    def("PASS_BRIGHT", pass == Pass::Bright);
    def("PASS_DOWNSAMPLE", pass == Pass::Downsample);
    def("PASS_BLUR", pass == Pass::Blur7 || pass == Pass::Blur15);
    def("PASS_COPY", pass == Pass::Copy);
    switch (pass) {
    case Pass::Collapse:
        d.name = "downsample_collapse";
        d.samplers = {"sSrc"};
        return d;
    case Pass::Bright:
        d.name = "bloom";
        d.samplers = {"sSrc"};
        return d;
    case Pass::Downsample:
        d.name = "downsample";
        d.samplers = {"sSrc"};
        return d;
    case Pass::Copy:
        d.name = "post_copy";
        d.samplers = {"sSrc"};
        return d;
    case Pass::Blur7:
    case Pass::Blur15: {
        const int taps = pass == Pass::Blur7 ? 7 : 15;
        char name[32];
        SDL_snprintf(name, sizeof name, "blur_%x", unsigned(taps - 1) << postkey::NumTapsShift);
        d.name = name;
        def("NUM_TAPS", taps - 1);
        d.samplers = {"sSrc"};
        return d;
    }
    case Pass::Composite:
    case Pass::Debug:
        break;
    }
    d.name = pass == Pass::Debug ? "postprocess_debug" : "postprocess_" + keyName(key);
    def("COLORXFM", (key & postkey::ColorXfm) ? 1 : 0);
    def("NOISE", (key & postkey::Noise) ? 1 : 0);
    def("DOF", (key & postkey::Dof) ? 1 : 0);
    def("BLOOM", (key & postkey::Bloom) ? 1 : 0);
    def("LUMMAP", (key & postkey::LumMap) ? 1 : 0);
    def("DEBUG", pass == Pass::Debug ? 1 : 0);
    def("HDR_OUT", (key & postkey::HdrOut) ? 1 : 0);
    d.samplers = {"sHdr", "sDofLow", "sDepth", "sBloom", "sLum", "sNoise"};
    return d;
}

std::string PostFx::keyName(uint32_t key) {
    char name[16];
    SDL_snprintf(name, sizeof name, "%x", key);
    return name;
}

uint32_t PostFx::compositeKey(const PostProcState& s, bool dof) {
    uint32_t k = 0;
    // NgPostProc::DoPost (RB 0x827DA600) [C]: NOISE with a noise map and an
    // intensity != 0, LUMMAP with a luminance texture, BLOOM as bloomOn, DOF
    // from the camera; an identity ColorXfm is skipped here (same result).
    if (s.colorXfmOn()) k |= postkey::ColorXfm;
    if (s.noiseOn()) k |= postkey::Noise;
    if (dof) k |= postkey::Dof;
    if (s.bloomOn()) k |= postkey::Bloom;
    if (s.luminanceMap) k |= postkey::LumMap;
    return k;
}

std::vector<ProgramDesc> PostFx::allPrograms() {
    std::vector<ProgramDesc> all;
    for (Pass p : {Pass::Collapse, Pass::Bright, Pass::Downsample, Pass::Blur7, Pass::Blur15, Pass::Debug, Pass::Copy})
        all.push_back(describe(p, postkey::All));
    const uint32_t bits[6] = {postkey::ColorXfm, postkey::Noise, postkey::Dof, postkey::Bloom, postkey::LumMap, postkey::HdrOut};
    for (uint32_t m = 0; m < 64; ++m) {
        uint32_t key = 0;
        for (int b = 0; b < 6; ++b)
            if (m & (1u << b)) key |= bits[b];
        all.push_back(describe(Pass::Composite, key));
    }
    return all;
}

// A program, compiled on first use; a composite that fails falls back to
// fewer effects (logged once), so the world never goes black.
ProgramId PostFx::program(Pass pass, uint32_t key) {
    const uint32_t id = (uint32_t(pass) << 24) | key;
    if (auto it = programs_.find(id); it != programs_.end()) return it->second;
    std::string error;
    ProgramId p = device_.createProgram(describe(pass, key), error);
    if (!p) {
        SDL_Log("post: %s does not compile: %s", describe(pass, key).name.c_str(), error.c_str());
        if (pass == Pass::Composite && key) {
            for (uint32_t bit : {postkey::Noise, postkey::LumMap, postkey::ColorXfm, postkey::Dof, postkey::Bloom})
                if (key & bit) {
                    p = program(pass, key & ~bit);
                    break;
                }
        }
    }
    programs_[id] = p;
    return p;
}

void PostFx::quad(TargetId target, ProgramId p, const TextureId* tex, const SamplerState* samp, int slots, const void* block, size_t size) {
    if (!p) return;
    if (target) device_.setTarget(target);
    DrawCall c;
    c.program = p;
    c.state.alphaWrite = bool(target);
    c.layout = &triangleLayout();
    c.vertices = triangle_;
    c.count = 3;
    for (int i = 0; i < slots; ++i) c.textures[i] = tex[i], c.samplers[i] = samp[i];
    c.uniforms[0] = UniformData{block, size, 0};
    device_.draw(c);
}

bool PostFx::begin(const float clear[4]) {
    capturing_ = false;
    if (disabled_ || !triangle_) return false;
    int x, y, w, h;
    renderer_.pictureRect(x, y, w, h);
    // The port's chain draws the world at its render scale (SSAA, FSR 1).
    if (settings_.any() && !(enhanced_ && enhanced_->failed())) {
        const float k = settings_.renderScale();
        w = std::max(1, int(std::lround(float(w) * k))), h = std::max(1, int(std::lround(float(h) * k)));
    }
    if (!ensureTargets(w, h) || !program(Pass::Composite, 0)) {
        disabled_ = true;
        return false;
    }
    device_.setTarget(hdr_);
    const float c[4] = {clear[0] * 0.25f, clear[1] * 0.25f, clear[2] * 0.25f, clear[3]};  // the clear colour / 4 [C] (0x823055E8)
    device_.clear(c, true);
    capturing_ = true;
    return true;
}

void PostFx::end() {
    if (!capturing_) return;
    capturing_ = false;
    ++worldDraws_;
    // Remove All Effects: none of GH2's own either (its default PostProc
    // draws the world as it is). Bokeh: None skips GH2's depth of field;
    // Medium is the original; Light blurs less; Strong and Stronger focus
    // nearer, blur sooner and wider (gd01's levels).
    const PostProcState saved = state_;
    const DofState savedDof = dof_;
    if (!settings_.original) state_ = PostProcState{};
    float dofSpread = kDofSpread;
    if (settings_.bokeh != 2) {
        static constexpr float kBlur[5] = {1, 0.65f, 1, 1.25f, 1.5f}, kDepth[5] = {1, 1.3f, 1, 0.8f, 0.6f}, kFocus[5] = {1, 1, 1, 0.92f, 0.85f},
                               kSpread[5] = {1, 0.7f, 1, 1.4f, 1.8f};
        const size_t lv = size_t(std::clamp(settings_.bokeh, 0, 4));
        dof_.maxBlur = std::min(1.0f, dof_.maxBlur * kBlur[lv]);
        dof_.blurDepth = std::max(0.001f, dof_.blurDepth * kDepth[lv]);
        dof_.focusDistance *= kFocus[lv];
        dofSpread *= kSpread[lv];
    }
    const bool dof = settings_.original && settings_.bokeh > 0 && dof_.on && dof_.maxBlur > 0 && dof_.farPlane > dof_.nearPlane;
    const bool bloom = state_.bloomOn();
    const SamplerState point{Filter::Nearest, Wrap::Clamp, 0}, linear{Filter::Linear, Wrap::Clamp, 0};

    PostBlock b{};
    // Depth of field (DoPost's constant c24 from Rnd::SetDOF's depths).
    if (dof) {
        const float n = dof_.nearPlane, f = dof_.farPlane;
        auto depthAt = [&](float d) { return d < n ? 0.0f : (f - f * n / d) / (f - n); };
        const float zf = depthAt(dof_.focusDistance), zb = depthAt(dof_.focusDistance * (1 - std::max(dof_.blurDepth, 0.001f)));
        float span = zf - zb;
        if (std::fabs(span) < 1e-7f) span = 1e-7f;
        b.dof[0] = 1 / span, b.dof[1] = -zf / span, b.dof[2] = 0, b.dof[3] = dof_.maxBlur;
    }
    b.bloom[0] = state_.bloomIntensity;
    b.bloom[1] = kUiPlane;
    // gBloomColor (RB 0x827D8C18) [C]: bloom_color / (4 - threshold), offset
    // -threshold / (4 - threshold); all 0 from a threshold of 4.
    {
        const float thr = state_.bloomThreshold, k = thr >= 4 ? 0.0f : 1.0f / (4 - thr);
        for (int i = 0; i < 3; ++i) b.thresholds[i] = state_.bloomColor[size_t(i)] * k;
        b.thresholds[3] = -thr * k;
    }
    // The ColorXfm, its 3x3 times the flicker (RB 0x827D93D8) [C].
    const uint64_t nowNs = SDL_GetTicksNS();
    // A test aid (the rb_look walk's pause check, GD05-089): GD05_POST_FREEZE=1
    // holds the flicker and the grain's phases, so two pictures of a paused
    // world differ only by what is drawn over it.
    const char* fz = SDL_getenv("GD05_POST_FREEZE");
    const bool frozen = fz && *fz == '1';
    updateFlicker(lastNs_ && !frozen ? float(double(nowNs - lastNs_) / 1e9) : 0.0f);
    lastNs_ = nowNs;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) b.colorXfm[r][c] = state_.colorXfm[size_t(r * 3 + c)] * flicker_;
        b.colorXfm[r][3] = state_.colorXfm[size_t(9 + r)];
    }
    // The grain (RB 0x827D9190) [C]: four random phases each time the world
    // is drawn (c112) and the scales (c113: base x / y, top, intensity); a
    // stationary PostProc keeps its own phase and no top scale.
    if (state_.noiseStationary) {
        b.noisePhase[0] = b.noisePhase[2] = stationaryPhase_[0];
        b.noisePhase[1] = b.noisePhase[3] = stationaryPhase_[1];
    } else if (frozen && frozenPhaseSet_) {
        for (int i = 0; i < 4; ++i) b.noisePhase[i] = frozenPhase_[size_t(i)];
    } else {
        for (float& v : b.noisePhase) v = random();
        for (int i = 0; i < 4; ++i) frozenPhase_[size_t(i)] = b.noisePhase[i];
        frozenPhaseSet_ = true;
    }
    b.noiseScale[0] = state_.noiseBaseScale[0], b.noiseScale[1] = state_.noiseBaseScale[1];
    b.noiseScale[2] = state_.noiseStationary ? 1.0f : state_.noiseTopScale, b.noiseScale[3] = state_.noiseIntensity;
    b.source[2] = float(debug_);

    const TextureId hdrTex = device_.targetTexture(hdr_);
    if (dof) {
        // The HDR buffer, unpacked, to the low-resolution buffer, blurred
        // across then down.
        PostBlock s = b;
        s.source[0] = 1.0f / float(w_), s.source[1] = 1.0f / float(h_);
        quad(dofA_, program(Pass::Collapse, 0), &hdrTex, &linear, 1, &s, sizeof s);
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < 7; ++i) {
                const float o = kDofTaps[i] * dofSpread;
                s.taps[i][0] = pass == 0 ? o / kDofLowW : 0;
                s.taps[i][1] = pass == 0 ? 0 : o / kDofLowH;
                s.taps[i][2] = 1.0f / 7;
            }
            const TextureId src = device_.targetTexture(pass == 0 ? dofA_ : dofB_);
            quad(pass == 0 ? dofB_ : dofA_, program(Pass::Blur7, 0), &src, &linear, 1, &s, sizeof s);
        }
    }
    if (bloom) {
        // RB's bloom pass (to half size), a 2x2 box to a quarter, then the
        // 15-tap blur (0x827D8970) [C]: taps at +-(i + 0.5) * bloom_size / 7
        // of 1/180 of the picture's height (the first pass, down) or 1/320
        // of its width (across), weight exp(ln 0.1 * i^2 / 49); the centre
        // tap and the normalisation [I].
        PostBlock s = b;
        s.source[0] = 1.0f / float(w_), s.source[1] = 1.0f / float(h_);
        quad(bloomHalf_, program(Pass::Bright, 0), &hdrTex, &point, 1, &s, sizeof s);
        const TextureId half = device_.targetTexture(bloomHalf_);
        quad(bloomA_, program(Pass::Downsample, 0), &half, &linear, 1, &s, sizeof s);
        const float step = state_.bloomSize / 7.0f, fall = std::log(0.1f) / 49.0f;
        float sum = 1;
        for (int i = 1; i < 8; ++i) sum += 2 * std::exp(fall * float(i * i));
        for (int pass = 0; pass < 2; ++pass) {
            const float dx = pass == 0 ? 0 : step / 320.0f, dy = pass == 0 ? step / 180.0f : 0;
            s.taps[0][0] = s.taps[0][1] = 0;
            s.taps[0][2] = 1 / sum;
            for (int i = 1; i < 8; ++i) {
                const float w = std::exp(fall * float(i * i)) / sum, o = float(i) + 0.5f;
                s.taps[i][0] = o * dx, s.taps[i][1] = o * dy, s.taps[i][2] = w;
                s.taps[i + 7][0] = -o * dx, s.taps[i + 7][1] = -o * dy, s.taps[i + 7][2] = w;
            }
            const TextureId src = device_.targetTexture(pass == 0 ? bloomA_ : bloomB_);
            quad(pass == 0 ? bloomB_ : bloomA_, program(Pass::Blur15, 0), &src, &linear, 1, &s, sizeof s);
        }
    }

    // The final pass into the picture, or into the buffer the port's chain
    // takes (Enhanced Options) or the paced world is held in.
    int x, y, pw, ph;
    renderer_.pictureRect(x, y, pw, ph);
    const TextureId tex[6] = {hdrTex, device_.targetTexture(dofA_), device_.targetDepth(hdr_), device_.targetTexture(bloomA_),
                              state_.luminanceMap, state_.noiseMap ? state_.noiseMap : noise_};
    const SamplerState samp[6] = {point, linear, point, linear, linear, SamplerState{Filter::Linear, Wrap::Repeat, 0}};
    PostProcState keyed = state_;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) keyed.colorXfm[size_t(r * 3 + c)] *= flicker_;
    lastKey_ = compositeKey(keyed, dof), lastDof_ = dof;
    const ProgramId p = debug_ ? program(Pass::Debug, postkey::All) : program(Pass::Composite, lastKey_);
    state_ = saved, dof_ = savedDof;
    const bool chain = settings_.any() && !debug_ && !(enhanced_ && enhanced_->failed());
    if (chain && !enhanced_) enhanced_ = std::make_unique<EnhancedFx>(device_);
    held_ = false;
    if (chain || paced_) {
        std::string error;
        // Enhanced > HDR: the pass writes RGBA16F, unclamped (HDR_OUT), for the port's tone map.
        const bool hdr = chain && settings_.hdr != 0;
        if (ldr_ && ldrF16_ != hdr) device_.destroyTarget(ldr_), ldr_ = {};
        if (!ldr_) {
            if (hdr) ldr_ = device_.createTarget(TargetDesc{w_, h_, false, TargetFormat::RGBA16F}, error);
            ldrF16_ = bool(ldr_) && hdr;
            if (!ldr_) ldr_ = device_.createTarget(TargetDesc{w_, h_, false, TargetFormat::RGBA8}, error);
        }
        if (ldr_) {
            const ProgramId pc = ldrF16_ && !debug_ ? program(Pass::Composite, lastKey_ | postkey::HdrOut) : p;
            quad(ldr_, pc ? pc : p, tex, samp, 6, &b, sizeof b);
            held_ = true;
            present(x, y, pw, ph);
        }
    }
    if (!held_) {
        device_.setTarget({});
        device_.setViewport(x, y, pw, ph);
        quad({}, p, tex, samp, 6, &b, sizeof b);
    }
    device_.setTarget({});
    device_.setViewport(x, y, pw, ph);
}

// The held picture (ldr_, the size of the world buffer) into the picture:
// through the port's chain when it is on, else a copy.
void PostFx::present(int x, int y, int pw, int ph) {
    bool chained = false;
    if (settings_.any() && !debug_ && enhanced_ && !enhanced_->failed()) {
        // True HDR: the display's headroom while it shows HDR (else tone mapped as Emulated).
        PostSettings ps = settings_;
        ps.hdrPeak = ps.hdr == 2 ? device_.hdrHeadroom() : 1.0f;
        chained = enhanced_->process(device_.targetTexture(ldr_), device_.targetDepth(hdr_), w_, h_, x, y, pw, ph, ps, frame_);
    }
    if (!chained) {
        device_.setTarget({});
        device_.setViewport(x, y, pw, ph);
        const TextureId t = device_.targetTexture(ldr_);
        const SamplerState linear{Filter::Linear, Wrap::Clamp, 0};
        PostBlock b{};
        quad({}, program(Pass::Copy, 0), &t, &linear, 1, &b, sizeof b);
    }
    device_.setTarget({});
    device_.setViewport(x, y, pw, ph);
}

float PostFx::random() {
    seed_ ^= seed_ << 13, seed_ ^= seed_ >> 17, seed_ ^= seed_ << 5;
    return float(seed_ >> 8) / 16777216.0f;
}

// RndPostProc's flicker (RB 0x8218E948) [C]: off (1) unless both secs and
// the upper intensity are > 0; else every random(secs) seconds a new target
// random(intensity) is eased to from the last (smoothstep), and the factor
// is 1 - that.
void PostFx::updateFlicker(float dt) {
    const PostProcState& s = state_;
    if (s.flickerSecs[0] <= 0 || s.flickerSecs[1] <= 0 || s.flickerIntensity[1] <= 0) {
        flicker_ = 1;
        return;
    }
    if (flickerPeriod_ <= flickerT_) {
        flickerT_ = 0;
        flickerFrom_ = flickerTo_;
        flickerTo_ = s.flickerIntensity[0] + random() * (s.flickerIntensity[1] - s.flickerIntensity[0]);
        flickerPeriod_ = s.flickerSecs[0] + random() * (s.flickerSecs[1] - s.flickerSecs[0]);
    }
    const float f = flickerT_ / flickerPeriod_;
    flickerT_ += dt;
    flicker_ = 1 - ((3 - 2 * f) * f * f * (flickerTo_ - flickerFrom_) + flickerFrom_);
}

// Rnd's world pacing (RB 0x8218EF80 with 0x8218EEA0) [C]: from emulate_fps
// (rounded, 1..60) 120 / fps half-frames rounded: a period of n = that / 2
// frames at 60 Hz, n + r and n alternating (r its remainder); the world is
// drawn in the period's first frames and shown again for the rest; n <= 1
// (40 fps and over) or no PostProc: every frame. The port counts 60 Hz
// ticks of the clock, so the pace holds at any display rate [I].
bool PostFx::hold() {
    const uint64_t now = SDL_GetTicksNS();
    int fps = disabled_ || debug_ || !settings_.original ? 0 : int(std::lround(state_.emulateFps));
    if (const char* e = SDL_getenv("GD05_WORLD_FPS")) fps = std::atoi(e);  // development aid: 0 every frame
    paced_ = false;
    if (fps <= 0) {
        paceFps_ = 0;
        return false;
    }
    fps = std::min(fps, 60);
    if (fps != paceFps_) {
        paceFps_ = fps;
        const int half = int(std::lround(120.0 / fps));
        paceLen_ = half >> 1, paceRem_ = half & 1;
        if (paceStep_ >= paceLen_) paceStep_ = 0;
    }
    if (paceLen_ <= 1) return false;
    paced_ = true;
    if (!tickStartNs_ || now < tickStartNs_) tickStartNs_ = now, tick_ = 0;
    const uint64_t ticks = (now - tickStartNs_) * 60 / 1000000000ull;
    bool due = !held_;
    for (; tick_ < ticks; ++tick_) {
        due = due || paceStep_ == 0;
        if (++paceStep_ >= paceLen_) {
            paceStep_ = 0;
            paceLen_ += paceRem_;
            paceRem_ = -paceRem_;
        }
    }
    if (due) return false;
    ++worldHolds_;
    int x, y, pw, ph;
    renderer_.pictureRect(x, y, pw, ph);
    present(x, y, pw, ph);
    return true;
}

}  // namespace gfx
