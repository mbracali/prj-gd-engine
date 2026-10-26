// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/standard_shader.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// The lighting model, in our words (research notes: GH2's shader sources on
// the disc, read for reference only; nothing here is copied from them):
//
// - Ambient: base * ambient, where base is the vertex colour (prelit) or the
//   material colour, and ambient is the Environ's (use_environ) or white.
// - Lights (up to 7; the projected ones come last): directional lights add
//   colour * N.L, with a Phong highlight colour * (R.V)^power for the first
//   four; point lights add colour * N.L * (1 - clamp(distance / range)) and
//   a highlight scaled the same way. The light sum is tinted by the material
//   colour, and per-vertex lighting is tinted by it once more on top (the
//   game multiplies twice there, which we keep).
// - Specular is off when an environment map is on: the reflection (cube map
//   along R = reflect(-V, N), mip bias = specular power) times the specular
//   colour replaces it.
// - Projected (spot) lights are directional lights masked by a projected
//   texture and its shadow texture, always per pixel.
// - Colour out = emissive * multiplier + (lit + ambient) * diffuse map *
//   intensify + specular; then fog (mix towards the fog colour by
//   (end - depth) / (end - start)), then the fade-out, then HDR's / 4.
// - Normal maps are two-channel (BC5): the game reads x from the second
//   channel and y from the first, and rebuilds z as 1.35 - |x| - |y|
//   (clamped), a cheap stand-in for sqrt(1 - x^2 - y^2), which we keep for
//   the same look. The normal is turned by the texture transform before the
//   tangent frame when uvs come from the mesh.
namespace gfx {
namespace {

const char* kCommon = R"(
// GD_REGULAR: the lights that are not projected (defined by the program).
#if GD_PER_PIXEL
#  ifdef GD_VERTEX
#    define GD_STAGE_REGULAR 0
#  else
#    define GD_STAGE_REGULAR GD_REGULAR
#  endif
#else
#  ifdef GD_VERTEX
#    define GD_STAGE_REGULAR GD_REGULAR
#  else
#    define GD_STAGE_REGULAR 0
#  endif
#endif
#define GD_SPEC_LIGHTS (GD_SPECULAR && !GD_ENV_MAP)
#define GD_NEEDS_VIEW (GD_ENV_MAP || (GD_PER_PIXEL && GD_LIGHTS > 0) || GD_PROJ_LIGHTS > 0)

layout(std140) uniform GdFrame {
    mat4 uViewProj;
    vec4 uCameraPos;
    vec4 uAmbient;
    vec4 uEnvAmbient;
    vec4 uLightVec[7];
    vec4 uLightColor[7];
    vec4 uFogColor;
    vec4 uFogRange;
    vec4 uFadeRange;
    mat4 uWorldToShadow;
    mat4 uSpotXfm[3];
    vec4 uDepthRange;
    vec4 uScreen;
};
layout(std140) uniform GdMaterial {
    vec4 uDiffuse;
    vec4 uSpecular;
    mat4 uTexXfm;
    vec4 uMultipliers;
    vec4 uFadeColor;
    vec4 uAlphaTest;
};
layout(std140) uniform GdObject {
    mat4 uModel[4];
};

uniform sampler2D sDiffuse;
uniform sampler2D sNormal;
uniform sampler2D sSpecular;
uniform sampler2D sGlow;
uniform samplerCube sEnviron;
uniform sampler2D sShadow0;
uniform sampler2D sShadow1;
uniform sampler2D sShadow2;
uniform sampler2D sSpot0;
uniform sampler2D sSpot1;
uniform sampler2D sSpot2;
uniform sampler2D sCustomDiffuse;
uniform sampler2D sSceneDepth;

vec3 gdLambert(vec3 n, vec3 l) {
#if GD_CUSTOM_DIFFUSE && defined(GD_FRAGMENT)
    return texture(sCustomDiffuse, vec2(0.5 * dot(n, l) + 0.5, 0.5)).rgb;
#else
    return vec3(clamp(dot(n, l), 0.0, 1.0));
#endif
}

float gdPhong(vec3 n, vec3 l, vec3 v, float power) {
    return pow(clamp(dot(reflect(-l, n), v), 0.0, 1.0), max(power, 1e-4));
}

#if GD_SHADOW && defined(GD_FRAGMENT)
// Fraction of eight taps around the point that are nearer the light than
// the shadow map's depth.
float gdShadowFactor(vec4 shadowPos) {
    vec3 p = shadowPos.xyz / shadowPos.w;
    const vec2 ring[8] = vec2[8](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0),
                                 vec2(0.0, -1.0), vec2(0.7, 0.7), vec2(-0.7, -0.7), vec2(0.7, -0.7));
    vec2 texel = 1.0 / vec2(textureSize(sShadow0, 0));
    float lit = 0.0;
    for (int i = 0; i < 8; ++i) lit += p.z < texture(sShadow0, p.xy + ring[i] * texel).r ? 1.0 : 0.0;
    return lit / 8.0;
}
#endif

void gdRegularLights(vec3 pos, vec3 n, vec3 v, vec4 specFactor, vec4 shadowPos, inout vec3 diff, inout vec3 spec) {
#if GD_STAGE_REGULAR > 0
#if GD_DIR_LIGHTS
    for (int i = 0; i < GD_STAGE_REGULAR; ++i) {
        vec3 l = uLightVec[i].xyz;
        diff += uLightColor[i].rgb * gdLambert(n, l);
#if GD_SPEC_LIGHTS
        if (i < 4) spec += uLightColor[i].rgb * gdPhong(n, l, v, specFactor.a);
#endif
#if GD_SHADOW && defined(GD_FRAGMENT)
        if (i == 0) {
            float s = gdShadowFactor(shadowPos);
            diff *= s;
#if GD_SPEC_LIGHTS
            spec *= s;
#endif
        }
#endif
    }
#else
    vec3 sum = vec3(0.0);
    for (int i = 0; i < GD_STAGE_REGULAR; ++i) {
        vec3 d = uLightVec[i].xyz - pos;
        float len = length(d);
        vec3 l = d / max(len, 1e-6);
        float falloff = 1.0 - clamp(len * uLightColor[i].w, 0.0, 1.0);
#if GD_SPEC_LIGHTS
        spec += uLightColor[i].rgb * gdPhong(n, l, v, specFactor.a) * falloff;
#endif
#if GD_SHADOW && defined(GD_FRAGMENT)
        if (i == 0) {
            float s = gdShadowFactor(shadowPos);
            diff *= s;
#if GD_SPEC_LIGHTS
            spec *= s;
#endif
        }
#endif
        sum += uLightColor[i].rgb * (gdLambert(n, l).x * falloff);
    }
    diff += sum;
#endif
    diff *= uDiffuse.rgb;
#if GD_SPEC_LIGHTS
    spec *= specFactor.rgb;
#endif
#endif
}

#if GD_PROJ_LIGHTS > 0 && defined(GD_FRAGMENT)
vec3 gdSpotMask(vec3 pos, mat4 xfm, sampler2D spot, sampler2D spotShadow) {
    vec3 q = (xfm * vec4(pos, 1.0)).xyz;
    if (q.z <= 0.0) return vec3(0.0);
    vec2 uv = q.xy / q.z * 0.5 + 0.5;
    return texture(spot, uv).rgb * (1.0 - texture(spotShadow, uv).a);
}

void gdSpotLight(int i, vec3 mask, vec3 n, vec3 v, vec4 specFactor, inout vec3 diff, inout vec3 spec) {
    vec3 l = uLightVec[i].xyz;
    diff += uLightColor[i].rgb * gdLambert(n, l) * mask;
#if GD_SPEC_LIGHTS
    spec += uLightColor[i].rgb * gdPhong(n, l, v, specFactor.a) * mask;
#endif
}

void gdProjectedLights(vec3 pos, vec3 n, vec3 v, vec4 specFactor, inout vec3 diff, inout vec3 spec) {
    gdSpotLight(GD_REGULAR, gdSpotMask(pos, uSpotXfm[0], sSpot0, sShadow0), n, v, specFactor, diff, spec);
#if GD_PROJ_LIGHTS > 1
    gdSpotLight(GD_REGULAR + 1, gdSpotMask(pos, uSpotXfm[1], sSpot1, sShadow1), n, v, specFactor, diff, spec);
#endif
#if GD_PROJ_LIGHTS > 2
    gdSpotLight(GD_REGULAR + 2, gdSpotMask(pos, uSpotXfm[2], sSpot2, sShadow2), n, v, specFactor, diff, spec);
#endif
}
#endif
)";

const char* kVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aUv;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in vec4 aTangent;

out vec4 vColor;
out vec4 vUv;     // xy texture coordinates, z colour fade, w alpha fade
out vec4 vWorld;  // xyz world position, w fog factor
#if GD_NEEDS_VIEW
out vec3 vView;
out vec3 vNormal;
#endif
#if GD_NORMAL_MAP
out vec3 vTangent;
out vec3 vBitangent;
#endif
#if GD_SHADOW
out vec4 vShadowPos;
#endif
#if GD_FADE == 3
out vec4 vClip;
#endif

void main() {
#if GD_WEIGHTS == 0
    mat4 model = uModel[0];
#elif GD_WEIGHTS == 1
    mat4 model = aWeights.x * uModel[0] + (1.0 - aWeights.x) * uModel[1];
#elif GD_WEIGHTS == 2
    mat4 model = aWeights.x * uModel[0] + aWeights.y * uModel[1] + (1.0 - aWeights.x - aWeights.y) * uModel[2];
#else
    mat4 model = aWeights.x * uModel[0] + aWeights.y * uModel[1] + aWeights.z * uModel[2] +
                 (1.0 - aWeights.x - aWeights.y - aWeights.z) * uModel[3];
#endif
    vec3 pos = (model * vec4(aPos, 1.0)).xyz;
    vec3 n = mat3(model) * aNormal;
    vec4 clip = uViewProj * vec4(pos, 1.0);
    gl_Position = clip;
    vec3 v = normalize(uCameraPos.xyz - pos);

    vec4 lit = vec4(0.0);
    vec3 spec = vec3(0.0);
#if !GD_PER_PIXEL && GD_REGULAR > 0
    gdRegularLights(pos, n, v, uSpecular, vec4(0.0), lit.rgb, spec);
    lit *= uDiffuse;
#endif
#if GD_PRELIT
    vec4 base = aColor;
#else
    vec4 base = uDiffuse;
#endif
#if GD_USE_ENVIRON
    lit += base * vec4(uEnvAmbient.rgb, 1.0);
#else
    lit += base * vec4(uAmbient.rgb, 1.0);
#endif
    vColor = lit + vec4(spec, 0.0);

#if GD_TEX_GEN == 1
    vec4 src = vec4(n, 1.0);
#elif GD_TEX_GEN == 2
    vec4 src = vec4(pos, 1.0);
#elif GD_TEX_GEN == 3
    vec4 src = vec4(reflect(-v, n), 1.0);
#else
    vec4 src = vec4(aUv, 0.0, 1.0);
#endif
    vUv = vec4((uTexXfm * src).xy, 1.0, 1.0);

#if GD_NEEDS_VIEW
    vView = v;
    vNormal = n;
#endif
#if GD_NORMAL_MAP
    vTangent = mat3(model) * aTangent.xyz;
    vBitangent = cross(n, vTangent) * aTangent.w;
#endif
#if GD_SHADOW
    vShadowPos = uWorldToShadow * vec4(pos, 1.0);
#endif
    float depth = clip.w;
#if GD_FOG
    vWorld = vec4(pos, clamp((uFogRange.x - depth) * uFogRange.y, 0.0, 1.0));
#else
    vWorld = vec4(pos, 0.0);
#endif
#if GD_FADE == 1
    vUv.w = clamp((uFadeRange.x - depth) * uFadeRange.y, 0.0, 1.0);
#elif GD_FADE == 2
    vUv.z = clamp((uFadeRange.x - depth) * uFadeRange.y, 0.0, 1.0);
#elif GD_FADE == 3
    vUv.zw = vec2(clamp((uFadeRange.x - depth) * uFadeRange.y, 0.0, 1.0));
    vClip = clip;
#endif
}
)";

const char* kFragment = R"(
in vec4 vColor;
in vec4 vUv;
in vec4 vWorld;
#if GD_NEEDS_VIEW
in vec3 vView;
in vec3 vNormal;
#endif
#if GD_NORMAL_MAP
in vec3 vTangent;
in vec3 vBitangent;
#endif
#if GD_SHADOW
in vec4 vShadowPos;
#endif
#if GD_FADE == 3
in vec4 vClip;
#endif

out vec4 fragColor;

#if GD_FADE == 3
// Fades towards black as the surface nears the scene behind it (soft
// particles), from the scene's depth buffer.
float gdDepthFade(vec4 clip) {
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    float z = texture(sSceneDepth, uv).r * 2.0 - 1.0;
    float zn = uDepthRange.x, zf = uDepthRange.y;
    float sceneW = 2.0 * zn * zf / (zf + zn - z * (zf - zn));
    return clamp((sceneW - clip.w) * uDepthRange.z, 0.0, 1.0);
}
#endif

void main() {
    vec2 uv = vUv.xy;
#if GD_NEEDS_VIEW
#if GD_NORMAL_MAP
    vec3 N = vNormal, T = vTangent, B = vBitangent;
#if GD_NORMALIZE
    N = normalize(N);
    T = normalize(T);
    B = normalize(B);
#endif
    vec2 xy = texture(sNormal, uv).rg * 2.0 - 1.0;
    vec3 tn = vec3(xy.y, xy.x, clamp(1.35 - abs(xy.x) - abs(xy.y), 0.0, 1.0));
#if GD_TEX_GEN == 0
    tn = mat3(uTexXfm) * tn;
#endif
    vec3 n = normalize(tn.x * T + tn.y * B + tn.z * N);
#else
    vec3 n = normalize(vNormal);
#endif
    vec3 v = normalize(vView);
#endif

#if GD_SPECULAR_MAP
    vec4 specFactor = texture(sSpecular, uv);
    specFactor.a = max(specFactor.a * uSpecular.a, 0.1);
#else
    vec4 specFactor = uSpecular;
#endif

    vec4 glow = vec4(0.0);
#if GD_GLOW_MAP
    glow.rgb = texture(sGlow, uv).rgb;
#endif

    vec4 lit = vec4(0.0);
    vec3 spec = vec3(0.0);
#if (GD_PER_PIXEL && GD_LIGHTS > 0) || GD_PROJ_LIGHTS > 0
#if GD_SHADOW
    vec4 shadowPos = vShadowPos;
#else
    vec4 shadowPos = vec4(0.0);
#endif
    gdRegularLights(vWorld.xyz, n, v, specFactor, shadowPos, lit.rgb, spec);
#if GD_PROJ_LIGHTS > 0
    gdProjectedLights(vWorld.xyz, n, v, specFactor, lit.rgb, spec);
#endif
    lit *= uDiffuse;
#endif
    lit += vColor;
#if GD_DIFFUSE_MAP
    lit *= texture(sDiffuse, uv) * uMultipliers.y;
#endif

#if GD_ENV_MAP
    // GH2's world is z-up; the cube map's faces are laid out y-up.
    vec3 r = reflect(-v, n);
    spec = texture(sEnviron, r.xzy, specFactor.a).rgb * specFactor.rgb;
#endif

    vec4 color = glow * uMultipliers.x + lit + vec4(spec, 0.0);
#if GD_FOG
    color = mix(uFogColor, color, vWorld.w);
#endif
#if GD_FADE != 0
    color = mix(uFadeColor, color, vUv.zzzw);
#endif
#if GD_FADE == 3
    color.rgb *= gdDepthFade(vClip);
#endif
    if (uAlphaTest.x > 0.5 && color.a <= uAlphaTest.y) discard;
#if GD_HDR
    color.rgb *= 0.25;
#endif
    fragColor = color;
}
)";

}  // namespace

StandardShaders::~StandardShaders() {
    for (ProgramId p : owned_) device_.destroyProgram(p);
}

ProgramDesc StandardShaders::describe(uint32_t key) {
    using namespace stdkey;
    ProgramDesc d;
    char buf[32];
    std::snprintf(buf, sizeof buf, "standard_%x", key);
    d.name = buf;
    auto def = [&](const char* name, int value) { d.defines += std::string("#define ") + name + " " + std::to_string(value) + "\n"; };
    def("GD_PER_PIXEL", (key & PerPixel) != 0);
    def("GD_SPECULAR_MAP", (key & SpecularMap) != 0);
    def("GD_SPECULAR", (key & Specular) != 0);
    def("GD_ENV_MAP", (key & EnvironMap) != 0);
    def("GD_DIFFUSE_MAP", (key & DiffuseMap) != 0);
    def("GD_NORMAL_MAP", (key & NormalMap) != 0);
    def("GD_NORMALIZE", (key & Normalize) != 0);
    def("GD_GLOW_MAP", (key & GlowMap) != 0);
    def("GD_PRELIT", (key & Prelit) != 0);
    def("GD_CUSTOM_DIFFUSE", (key & CustomDiffuse) != 0);
    def("GD_TEX_GEN", texGen(key));
    def("GD_WEIGHTS", weights(key));
    def("GD_LIGHTS", lights(key));
    def("GD_DIR_LIGHTS", (key & DirLights) != 0);
    def("GD_FOG", (key & Fog) != 0);
    def("GD_SHADOW", (key & ShadowBuffer) != 0);
    def("GD_HDR", (key & Hdr) != 0);
    def("GD_FADE", fade(key));
    def("GD_PROJ_LIGHTS", projLights(key));
    def("GD_REGULAR", std::max(0, lights(key) - projLights(key)));
    def("GD_USE_ENVIRON", (key & UseEnviron) != 0);
    // Development aid: GD02_SHADER_FAIL=<hex bits> breaks every variant
    // that has all those bits, to watch the fallback at work.
    if (const char* fail = SDL_getenv("GD02_SHADER_FAIL"); fail && key != 0) {
        const uint32_t bits = uint32_t(std::strtoul(fail, nullptr, 16));
        if (bits && (key & bits) == bits) d.defines += "#error GD02_SHADER_FAIL\n";
    }
    d.vertex = std::string(kCommon) + kVertex;
    d.fragment = std::string(kCommon) + kFragment;
    d.uniformBlocks = {"GdFrame", "GdMaterial", "GdObject"};
    d.samplers.assign(kMaxTextureSlots, "");
    d.samplers[kSlotDiffuse] = "sDiffuse";
    d.samplers[kSlotNormal] = "sNormal";
    d.samplers[kSlotSpecular] = "sSpecular";
    d.samplers[kSlotGlow] = "sGlow";
    d.samplers[kSlotEnviron] = "sEnviron";
    d.samplers[kSlotShadow0] = "sShadow0";
    d.samplers[kSlotShadow1] = "sShadow1";
    d.samplers[kSlotShadow2] = "sShadow2";
    d.samplers[kSlotSpot0] = "sSpot0";
    d.samplers[kSlotSpot1] = "sSpot1";
    d.samplers[kSlotSpot2] = "sSpot2";
    d.samplers[kSlotCustomDiffuse] = "sCustomDiffuse";
    d.samplers[kSlotSceneDepth] = "sSceneDepth";
    return d;
}

std::string StandardShaders::name(uint32_t key) {
    using namespace stdkey;
    std::string s;
    auto add = [&](uint32_t bit, const char* n) {
        if (key & bit) s += std::string(s.empty() ? "" : " ") + n;
    };
    add(DiffuseMap, "diffuse_map");
    add(NormalMap, "normal_map");
    add(Normalize, "normalize");
    add(Specular, "specular");
    add(SpecularMap, "specular_map");
    add(EnvironMap, "environ_map");
    add(GlowMap, "glow_map");
    add(CustomDiffuse, "custom_diffuse");
    add(PerPixel, "per_pixel");
    add(Prelit, "prelit");
    add(UseEnviron, "use_environ");
    add(Fog, "fog");
    add(ShadowBuffer, "shadow");
    add(Hdr, "hdr");
    auto num = [&](const char* n, int v) {
        if (v) s += std::string(s.empty() ? "" : " ") + n + "=" + std::to_string(v);
    };
    num("tex_gen", texGen(key));
    num("weights", weights(key));
    if (lights(key)) num((key & DirLights) ? "dir_lights" : "point_lights", lights(key));
    num("proj_lights", projLights(key));
    num("fade", fade(key));
    return s.empty() ? "plain" : s;
}

bool StandardShaders::compile(uint32_t key, std::string& error) {
    ProgramId p = device_.createProgram(describe(key), error);
    if (!p) return false;
    device_.destroyProgram(p);
    return true;
}

ProgramId StandardShaders::program(uint32_t key) {
    if (auto it = programs_.find(key); it != programs_.end()) return it->second;
    using namespace stdkey;
    // Simpler and simpler stand-ins: keep the texture, colour source and
    // skinning, then only the skinning, then nothing.
    const uint32_t keep = (3u << WeightsShift);
    const uint32_t tries[] = {key, key & (DiffuseMap | Prelit | UseEnviron | keep), key & (DiffuseMap | keep), key & keep, 0};
    ProgramId result;
    for (uint32_t k : tries) {
        if (auto it = programs_.find(k); it != programs_.end() && it->second) {
            result = it->second;
            break;
        }
        std::string error;
        ProgramId p = device_.createProgram(describe(k), error);
        if (p) {
            owned_.push_back(p);
            programs_[k] = p;
            result = p;
            break;
        }
        if (failedLogged_.insert(k).second)
            SDL_Log("render: shader standard_%x (%s) failed, using a simpler one: %s", k, name(k).c_str(), error.c_str());
    }
    programs_[key] = result;
    return result;
}

std::vector<uint32_t> StandardShaders::testKeys() {
    using namespace stdkey;
    std::vector<uint32_t> keys;
    auto add = [&](uint32_t k) {
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    };
    const uint32_t litEnv = UseEnviron | (3u << LightsShift) | DirLights | Fog;
    // 1. Every material combination (as GH2's key builder allows them).
    for (uint32_t m = 0; m < (1u << 9); ++m) {
        const bool ppl = m & 1, normal = m & 2, normalize = m & 4, specMap = m & 8;
        if (!ppl && (normal || specMap)) continue;
        if (!normal && normalize) continue;
        uint32_t k = (ppl ? PerPixel : 0) | (normal ? NormalMap : 0) | (normalize ? Normalize : 0) |
                     (specMap ? SpecularMap | Specular : 0);
        if (m & 16) k |= DiffuseMap;
        if (m & 32) k |= Specular;
        if (m & 64) k |= EnvironMap;
        if (m & 128) k |= GlowMap;
        if (m & 256) k |= Prelit;
        add(k);           // unlit pass / no environment
        add(k | litEnv);  // lit, in a venue
    }
    // 2. The environment: light counts and kinds, projected lights.
    const uint32_t materials[] = {DiffuseMap | Specular, PerPixel | DiffuseMap | NormalMap | Specular | SpecularMap,
                                  PerPixel | DiffuseMap | CustomDiffuse | EnvironMap};
    for (uint32_t mat : materials)
        for (int n = 0; n <= 7; ++n)
            for (int dir = 0; dir < 2; ++dir)
                for (int proj = 0; proj <= std::min(n, 3); ++proj)
                    add(mat | UseEnviron | (uint32_t(n) << LightsShift) | (dir ? DirLights : 0) | (uint32_t(proj) << ProjLightsShift));
    // 3. Skinning, texture generation, fog, fade, shadow, HDR.
    for (uint32_t mat : materials) {
        for (uint32_t w = 0; w < 4; ++w) add(mat | litEnv | (w << WeightsShift));
        for (uint32_t t = 0; t < 4; ++t) add(mat | litEnv | (t << TexGenShift));
        for (uint32_t f = 0; f < 4; ++f) add(mat | litEnv | (f << FadeShift));
        add(mat | litEnv | ShadowBuffer);
        add(mat | litEnv | Hdr);
        add(mat | (2u << LightsShift) | UseEnviron | ShadowBuffer);  // point lights with a shadow
    }
    return keys;
}

}  // namespace gfx
