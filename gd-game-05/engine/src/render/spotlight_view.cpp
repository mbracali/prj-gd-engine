// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/spotlight_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "char/character.h"
#include "game/band.h"
#include "game/world.h"
#include "render/band_view.h"
#include "render/venue_view.h"
#include "scene/anim.h"
#include "scene/world_objects.h"

namespace gfx {
namespace {

constexpr int kMaxFlares = 128;
constexpr float kPi = 3.14159265f;

// The flares' visibility test: one texel per flare. The new value steps
// 1/steps from the previous frame's towards 1 when the scene's depth at
// the flare is behind it (most of five samples), else towards 0.
const char* kVisVertex = R"(
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";
const char* kVisFragment = R"(
layout(std140) uniform GdFlareVis {
    vec4 uFlares[128];  // xy: screen uv, z: window depth, w: tested
    vec4 uSteps[128];   // x: 1 / steps, y: in front of the camera
    vec4 uTexel;        // xy: 1 / the depth buffer's size
};
uniform sampler2D sPrev;
uniform sampler2D sDepth;
out vec4 fragColor;
void main() {
    int i = int(gl_FragCoord.x);
    float prev = texelFetch(sPrev, ivec2(i, 0), 0).r;
    vec4 f = uFlares[i];
    float visible = 1.0;
    if (f.w > 0.5) {
        float n = 0.0;
        vec2 o = uTexel.xy * 2.0;
        n += step(f.z, texture(sDepth, f.xy).r + 0.0005);
        n += step(f.z, texture(sDepth, f.xy + vec2(o.x, 0.0)).r + 0.0005);
        n += step(f.z, texture(sDepth, f.xy - vec2(o.x, 0.0)).r + 0.0005);
        n += step(f.z, texture(sDepth, f.xy + vec2(0.0, o.y)).r + 0.0005);
        n += step(f.z, texture(sDepth, f.xy - vec2(0.0, o.y)).r + 0.0005);
        visible = step(2.5, n);
    }
    visible *= uSteps[i].y;
    float v = clamp(prev + (visible > 0.5 ? uSteps[i].x : -uSteps[i].x), 0.0, 1.0);
    fragColor = vec4(v, v, v, 1.0);
}
)";

// A flare: a screen-space square at its point's projection.
const char* kFlareVertex = R"(
layout(location = 0) in vec3 aCenter;
layout(location = 1) in vec2 aCorner;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
layout(location = 4) in float aSlot;
layout(std140) uniform GdFlare {
    mat4 uViewProj;
    vec4 uParams;  // x: output scale (HDR 0.25), y: use the visibility texture
};
uniform sampler2D sTex;
uniform sampler2D sVis;
out vec2 vUv;
out vec4 vColor;
void main() {
    vec4 c = uViewProj * vec4(aCenter, 1.0);
    c.xy += aCorner * c.w;
    c.z = 0.0;
    gl_Position = c;
    float vis = uParams.y > 0.5 ? texelFetch(sVis, ivec2(int(aSlot + 0.5), 0), 0).r : 1.0;
    vColor = aColor * vis;
    vUv = aUv;
}
)";
const char* kFlareFragment = R"(
layout(std140) uniform GdFlare {
    mat4 uViewProj;
    vec4 uParams;
};
uniform sampler2D sTex;
uniform sampler2D sVis;
in vec2 vUv;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec4 c = texture(sTex, vUv) * vColor;
    c.rgb *= uParams.x;
    fragColor = c;
}
)";

struct VisBlock {
    float flares[kMaxFlares][4];
    float steps[kMaxFlares][4];
    float texel[4];
};
struct FlareBlock {
    float viewProj[16];
    float params[4];
};

// The beams per pixel (GD02-151): GH2's depthvolume.fx "render cone
// depths" branch, from the disc's own HLSL (system/run/shaders): the screen
// ramp illum = sat(1 - |(ramp.xy - ndc) * (aspect, 1)| / reach)^2, the depth
// min(scene's view depth, the sheet's) as sat((w - nearest) * scale), out
// colour x depth x illum, into a beam buffer with no depth test (add, ONE /
// ONE), as 0x82307078 [C]. Cross-section, cut-outs and the density map are
// identities on the disc [C data].
const char* kBeamVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec4 aRamp;   // light ndc xy, aspect, 1 / reach
layout(location = 3) in vec2 aRange;  // nearest view depth, scale
layout(std140) uniform GdBeam {
    mat4 uViewProj;
    vec4 uDepth;  // near, far, depth 0..1 (1) or -1..1 (0), output scale
};
uniform sampler2D sDepth;
out vec4 vClip;
out vec3 vColor;
out vec4 vRamp;
out vec2 vRange;
void main() {
    vec4 c = uViewProj * vec4(aPos, 1.0);
    gl_Position = c;
    vClip = c;
    vColor = aColor;
    vRamp = aRamp;
    vRange = aRange;
}
)";
const char* kBeamFragment = R"(
layout(std140) uniform GdBeam {
    mat4 uViewProj;
    vec4 uDepth;
};
uniform sampler2D sDepth;
in vec4 vClip;
in vec3 vColor;
in vec4 vRamp;
in vec2 vRange;
out vec4 fragColor;
void main() {
    vec2 ndc = vClip.xy / vClip.w;
    float d = length(vec2((ndc.x - vRamp.x) * vRamp.z, ndc.y - vRamp.y)) * vRamp.w;
    float illum = clamp(1.0 - d, 0.0, 1.0);
    illum *= illum;
    float z = texture(sDepth, ndc * 0.5 + 0.5).r;
    float n = uDepth.x, f = uDepth.y;
    float sceneW = uDepth.z > 0.5 ? n * f / (f - z * (f - n)) : 2.0 * n * f / ((f + n) - (z * 2.0 - 1.0) * (f - n));
    float w = min(sceneW, vClip.w);
    float depth = clamp((w - vRange.x) * vRange.y, 0.0, 1.0);
    fragColor = vec4(vColor * (depth * illum * uDepth.w), 0.0);
}
)";
// FogPixel (postprocess.fx): the beam buffer added onto the world's pixels.
const char* kBeamCompVertex = R"(
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); vUv = aPos * 0.5 + 0.5; }
)";
const char* kBeamCompFragment = R"(
layout(std140) uniform GdBeamComp {
    vec4 uGain;  // x: the buffer's gain (x the HDR output scale)
};
uniform sampler2D sBeams;
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = vec4(texture(sBeams, vUv).rgb * uGain.x, 0.0); }
)";

struct BeamBlock {
    float viewProj[16];
    float depth[4];
};
struct BeamCompBlock {
    float gain[4];
};

const VertexLayout& beamLayout() {
    static VertexLayout l = [] {
        VertexLayout v;
        v.stride = 48;
        v.attribs[0] = {0, 3, AttribType::Float, 0};
        v.attribs[1] = {1, 3, AttribType::Float, 12};
        v.attribs[2] = {2, 4, AttribType::Float, 24};
        v.attribs[3] = {3, 2, AttribType::Float, 40};
        return v;
    }();
    return l;
}

const VertexLayout& triangleLayout() {
    static VertexLayout l = [] {
        VertexLayout v;
        v.stride = 8;
        v.attribs[0] = {0, 2, AttribType::Float, 0};
        return v;
    }();
    return l;
}

const VertexLayout& flareLayout() {
    static VertexLayout l = [] {
        VertexLayout v;
        v.stride = 48;
        v.attribs[0] = {0, 3, AttribType::Float, 0};
        v.attribs[1] = {1, 2, AttribType::Float, 12};
        v.attribs[2] = {2, 2, AttribType::Float, 20};
        v.attribs[3] = {3, 4, AttribType::Float, 28};
        v.attribs[4] = {4, 1, AttribType::Float, 44};
        return v;
    }();
    return l;
}

ProgramDesc visDesc() {
    ProgramDesc d;
    d.name = "spotlight_flare_vis";
    d.vertex = kVisVertex;
    d.fragment = kVisFragment;
    d.uniformBlocks = {"GdFlareVis"};
    d.samplers = {"sPrev", "sDepth"};
    return d;
}

ProgramDesc beamDesc() {
    ProgramDesc d;
    d.name = "spotlight_beam";
    d.vertex = kBeamVertex;
    d.fragment = kBeamFragment;
    d.uniformBlocks = {"GdBeam"};
    d.samplers = {"sDepth"};
    return d;
}

ProgramDesc beamCompDesc() {
    ProgramDesc d;
    d.name = "spotlight_beam_composite";
    d.vertex = kBeamCompVertex;
    d.fragment = kBeamCompFragment;
    d.uniformBlocks = {"GdBeamComp"};
    d.samplers = {"sBeams"};
    return d;
}

// Rock Band's volume beams (NgSpotlightDrawer, GD05-093, D91): ark/
// xbox_shaders #2290 / #2291 (constants gSpotPos c25, gSpotDir c26,
// gSpotToCam c27, gConst_u c28, gRadii c29, gCamDir c30, gCamPos c10,
// gDepthHalf c9, gFogDensity c82, gXSectionStrength c86, gPlaneLeft c88,
// gPlaneRight c87, gZRange c89, gFogColor c90, gFogRange c91; samplers
// gFogDensityMap s6, gFocalBlurDepth s9, gSpotXSectionMap s11, gFogDepth
// s12), translated by gd04's Xenos translator (the microcode as it runs: a
// ray / cone intersection against the scene's depth, the density map
// and the cross-section) and kept as translated; only the fetches are the
// port's: the screen position from the clip position (the port's y-up
// targets), the scene depth turned into RB's 1 - z, no cut-outs (s12 is
// black8x8_keep.bmp, 0x822A9A58, unless a spotlight has cut-outs: none
// drawn, D91). The vertex shader (#2290): world position and clip
// position out.
const char* kRbBeamVertex = R"(
layout(location = 0) in vec3 aPos;
layout(std140) uniform GdRbBeam {
    mat4 uViewProj;
    vec4 cP[92];
};
out vec4 vClip;
out vec3 vWorld;
void main() {
    vec4 c = uViewProj * vec4(aPos, 1.0);
    gl_Position = c;
    vClip = c;
    vWorld = aPos;
}
)";
const char* kRbBeamFragment = R"GLSL(
layout(std140) uniform GdRbBeam {
    mat4 uViewProj;
    vec4 cP[92];
};
uniform sampler2D sDepth;
uniform sampler2D sDensity;
uniform sampler2D sXSection;
in vec4 vClip;
in vec3 vWorld;
out vec4 fragColor;
// Shader Model 3 / Xenos: +-0 x anything (inf, NaN) = +0 in MUL, MAD, the
// dot products (gd04's translator).
vec4 gdMul(vec4 a, vec4 b) { return mix(a * b, vec4(0.0), bvec4(ivec4(equal(a, vec4(0.0))) | ivec4(equal(b, vec4(0.0))))); }
float gdMulS(float a, float b) { return (a == 0.0 || b == 0.0) ? 0.0 : a * b; }
float gdSum3(vec4 p) { return (p.x + p.y) + p.z; }
float sclampv(float s, bool c) { return c ? clamp(s, 0.0, 1.0) : s; }
vec4 r0 = vec4(0.0);
vec4 r1 = vec4(0.0);
vec4 r2 = vec4(0.0);
vec4 r3 = vec4(0.0);
vec4 r4 = vec4(0.0);
vec4 r5 = vec4(0.0);
vec4 r6 = vec4(0.0);
vec4 r7 = vec4(0.0);
vec4 r8 = vec4(0.0);
vec4 r9 = vec4(0.0);
float pv = 0.0;
bool p0 = false;
vec4 e0 = vec4(0.0);
void gdRun(vec2 gdUv) {
    { vec4 v = gdMul((-cP[26].wwww), cP[26].zzzz) + cP[25].zzzz; float s = pv; r2.z = v.z; pv = sclampv(s, false); }
    { vec4 v = gdMul((-cP[26].wwww), cP[26].xxxy) + cP[25].xxxy; float s = pv; r3.zw = v.zw; pv = sclampv(s, false); }
    { vec4 v = gdMul(cP[26].wwww, cP[26].xyzz) + cP[25].xyzz; float s = pv; r4.xyz = v.xyz; pv = sclampv(s, false); }
    { vec4 v = r1.zxyy + (-cP[10].zxyy); float s = pv; r6.xyz = v.xyz; pv = sclampv(s, false); }
    { vec4 v = cP[25].zyxx + (-cP[10].zyxx); float s = pv; r2.xyw = v.xyw; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r2.xwyy, cP[26].zxyy))); float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r6.xyzz, cP[26].zxyy))); float s = (1.0 / r0.w); r0.z = v.z; r2.x = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r2.xxxx, r0.yxxx); float s = pv; r0.xy = v.xy; pv = sclampv(s, false); }
    { vec4 v = gdMul(r0.yxxx, vec4(0.375, 0.625, 0.5, -0.5).zwww) + vec4(0.375, 0.625, 0.5, -0.5).zzzz; float s = pv; r7.xy = v.xy; pv = sclampv(s, false); }
    { vec4 v = r1.wwww + cP[26].wwww; float s = gdMulS(cP[28].y, r0.z); r2.x = v.x; r3.y = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r0.zzzz, r0.zzzz); float s = (1.0 / r0.z); r0.y = v.y; r0.x = s; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r6.xyzz, r6.xyzz))); float s = gdMulS(cP[28].x, r0.y); r5.z = v.z; r3.x = s; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r6.xyzz, cP[27].zxyy))); float s = max(r2.x, r2.x); r5.w = v.w; pv = sclampv(s, false); }
    { vec4 v = gdMul(r1.wwww, r0.xxxx); float s = gdMulS(r0.x, pv); r5.y = v.y; r5.x = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r5.yyyy, r6.xzyy) + cP[10].zyxx; float s = pv; r2.xyw = v.xyw; pv = sclampv(s, false); }
    { vec4 v = gdMul(r5.xxxx, r6.zyyy) + cP[10].yxxx; float s = pv; r0.xy = v.xy; pv = sclampv(s, false); }
    { vec4 v = gdMul(r5.xxxx, r6.xxxx) + cP[10].zzzz; float s = pv; r0.z = v.z; pv = sclampv(s, false); }
    { vec4 v = r0.zzzz + (-r4.zzzz); float s = (r5.x > 0.0 ? 1.0 : 0.0); r8.z = v.z; r4.z = s; pv = sclampv(s, false); }
    { vec4 v = r2.xyww + (-cP[25].zyxx); float s = max(r0.y, r0.y); r9.xyz = v.xyz; pv = sclampv(s, false); }
    { vec4 v = r5.zwww + (-r3.xyyy); float s = (-r4.x) + pv; r3.xy = v.xy; r8.x = s; pv = sclampv(s, false); }
    { vec4 v = vec4(greaterThan(vec4(9.99999975e-05, 0.0, 0.0, 0.0).xxxx, abs(r3).xxxx)); float s = max(r0.x, r0.x); r1.w = v.w; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r9.xzyy, r9.xzyy))); float s = (-r4.y) + pv; r4.x = v.x; r8.y = s; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r8.zxyy, r8.zxyy))); float s = sqrt(abs(r4).x); r4.y = v.y; r4.x = s; pv = sclampv(s, false); }
    { vec4 v = vec4(greaterThanEqual(cP[29].xxxx, r4.xxxx)); float s = sqrt(abs(r4).y); r4.x = v.x; r4.y = s; pv = sclampv(s, false); }
    { vec4 v = vec4(greaterThan(cP[29].yyyy, r4.yyyy)); float s = (r5.y > 0.0 ? 1.0 : 0.0); r4.y = v.y; r4.w = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r4.zwww, r4.yxxx); float s = (r1.w == 0.0 ? 0.0 : 1.0); bool np = (r1.w == 0.0); r4.xy = v.xy; p0 = np; pv = sclampv(s, false); }
    { vec4 v = mix(r2.yyww, cP[10].yyxx, vec4(equal(r4.yyyy, vec4(0.0)))); float s = pv; r2.yw = v.yw; pv = sclampv(s, false); }
    { vec4 v = mix(r2.xxxx, cP[10].zzzz, vec4(equal(r4.yyyy, vec4(0.0)))); float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    { vec4 v = mix(r0.zzzz, r1.wwww, vec4(equal(r4.xxxx, vec4(0.0)))); float s = pv; r2.x = v.x; pv = sclampv(s, false); }
    { vec4 v = mix(r0.xxyy, r2.yyww, vec4(equal(r4.xxxx, vec4(0.0)))); float s = pv; r5.yz = v.yz; pv = sclampv(s, false); }
    { vec4 v = mix(r2.yyww, cP[10].yyxx, vec4(equal(r4.xxxx, vec4(0.0)))); float s = pv; r4.yw = v.yw; pv = sclampv(s, false); }
    { vec4 v = mix(r1.wwww, cP[10].zzzz, vec4(equal(r4.xxxx, vec4(0.0)))); float s = pv; r2.y = v.y; pv = sclampv(s, false); }
    if (p0) { float s = (1.0 / r3.x); r1.w = s; pv = sclampv(s, false); }
    if (p0) { vec4 v = gdMul(r3.yyyy, r1.wwww); float s = pv; r0.y = v.y; pv = sclampv(s, false); }
    if (p0) { float s = gdMulS(r0.y, r0.y); r0.x = s; pv = sclampv(s, false); }
    if (p0) { vec4 v = gdMul((-r1.wwww), cP[28].zzzz) + r0.xxxx; float s = pv; r0.x = v.x; pv = sclampv(s, false); }
    if (p0) {
        if (p0) { vec4 v = vec4(greaterThanEqual(r0.xxxx, vec4(0.0, 1.0, -1.0, 2.0).xxxx)); float s = sqrt(abs(r0).x); r1.w = v.w; r0.x = s; pv = sclampv(s, false); }
        if (p0) { vec4 v = gdMul(r1.wwww, r0.xxxx); float s = pv; r0.x = v.x; pv = sclampv(s, false); }
        if (p0) { vec4 v = (-r0.yyyy) + r0.xxxx; float s = pv; r3.y = v.y; pv = sclampv(s, false); }
        if (p0) { vec4 v = (-r0.yyyy) + (-r0.xxxx); float s = pv; r3.x = v.x; pv = sclampv(s, false); }
        if (p0) { vec4 v = gdMul(r3.xxxx, r6.zyyy) + cP[10].yxxx; float s = pv; r0.xy = v.xy; pv = sclampv(s, false); }
        if (p0) { vec4 v = mix(r3.zwww, r0.yxxx, vec4(greaterThan(r3.xxxx, vec4(0.0)))); float s = pv; r0.xy = v.xy; pv = sclampv(s, false); }
    }
    if (p0) { vec4 v = gdMul(r3.yyyx, r6.zyxx) + cP[10].yxzz; float s = pv; r6.xyzw = v.xyzw; pv = sclampv(s, false); }
    if (p0) { vec4 v = mix(r3.zzzw, r6.yyyx, vec4(greaterThan(r3.yyyy, vec4(0.0)))); float s = pv; r3.zw = v.zw; pv = sclampv(s, false); }
    if (p0) { vec4 v = mix(r2.zzzz, r6.zzzw, vec4(greaterThan(r3.yyyx, vec4(0.0)))); float s = pv; r2.zw = v.zw; pv = sclampv(s, false); }
    if (!p0) { float s = (1.0 / r3.y); r0.x = s; pv = sclampv(s, false); }
    if (!p0) { vec4 v = gdMul(r0.xxxx, (-cP[28].zzzz)); float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    if (!p0) { vec4 v = gdMul(r1.wwww, r6.xyzz) + r1.zxyy; float s = pv; r6.xyz = v.xyz; pv = sclampv(s, false); }
    if (!p0) { vec4 v = mix(r3.zwww, r6.yzzz, vec4(greaterThanEqual(r1.wwww, vec4(0.0)))); float s = pv; r0.xy = v.xy; pv = sclampv(s, false); }
    if (!p0) { vec4 v = mix(r2.zzzz, r6.xxxx, vec4(greaterThanEqual(r1.wwww, vec4(0.0)))); float s = pv; r2.w = v.w; pv = sclampv(s, false); }
    { vec4 v = mix(r2.wwww, r0.xyyy, vec4(equal(vec4(0.0, 1.0, -1.0, 2.0).xxyy, vec4(0.0)))); float s = pv; r6.xyz = v.xyz; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r6.xyzz, cP[26].xyzz))); float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    { vec4 v = vec4(greaterThanEqual(cP[29].wwww, r1.wwww)); float s = pv; r3.x = v.x; pv = sclampv(s, false); }
    { vec4 v = vec4(greaterThanEqual(r1.wwww, cP[29].zzzz)); float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    { vec4 v = gdMul(r1.wwww, r3.xxxx); float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    { float s = (r1.w != 0.0 ? 0.0 : 1.0); bool np = (r1.w != 0.0); p0 = np; pv = sclampv(s, false); }
    if (!p0) { float s = max(r5.z, r5.z); r5.x = s; pv = sclampv(s, false); }
    if (p0) { vec4 v = max(r2.wxxx, r2.wxxx); float s = max(r0.x, r0.x); r2.xy = v.xy; r5.x = s; pv = sclampv(s, false); }
    if (p0) { vec4 v = max(r5.yyzz, r5.yyzz); float s = max(r0.y, r0.y); r4.yw = v.yw; r5.y = s; pv = sclampv(s, false); }
    { vec4 v = mix(r2.zzzz, r3.zwww, vec4(equal(vec4(0.0, 1.0, -1.0, 2.0).xxyy, vec4(0.0)))); float s = pv; r6.xyz = v.xyz; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r6.xyzz, cP[26].xyzz))); float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    { vec4 v = vec4(greaterThanEqual(r1.wwww, cP[29].zzzz)); float s = pv; r2.w = v.w; pv = sclampv(s, false); }
    { vec4 v = vec4(greaterThanEqual(cP[29].wwww, r1.wwww)); float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    { vec4 v = gdMul(r2.wwww, r1.wwww); float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    { float s = (r1.w != 0.0 ? 0.0 : 1.0); bool np = (r1.w != 0.0); p0 = np; pv = sclampv(s, false); }
    if (!p0) { float s = max(r5.x, r5.x); r4.z = s; pv = sclampv(s, false); }
    if (p0) { vec4 v = max(r2.zxxx, r2.zxxx); float s = max(r3.z, r3.z); r2.xy = v.xy; r4.z = s; pv = sclampv(s, false); }
    if (p0) { vec4 v = max(r5.yyxx, r5.yyxx); float s = max(r3.w, r3.w); r4.yw = v.yw; r5.y = s; pv = sclampv(s, false); }
    r3.y = 0.0;  // sT12: no cut-outs, black8x8_keep.bmp
    r3.z = 1.0 - texture(sDepth, gdUv).x;  // sT9: the scene depth, RB's 1 - z
    r0.y = texture(sDensity, gdUv).y;  // sT6: gFogDensityMap
    { vec4 v = r0.wwww + (-cP[82].xxxx); float s = gdMulS(cP[89].x, cP[89].y); r0.x = v.x; r6.x = s; pv = sclampv(s, false); }
    { vec4 v = r0.yyyy + cP[9].xxxx; float s = gdMulS(cP[82].y, r0.x); r0.z = v.z; r0.w = clamp(s, 0.0, 1.0); pv = sclampv(s, true); }
    { float s = max(r5.y, r5.y); r4.x = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r4, cP[30].yyxx); float s = gdMulS(cP[30].z, r2.x); r5.xyzw = v.xyzw; r6.z = s; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r1.zxyy, cP[88].zxyy))); float s = gdMulS(cP[30].z, r2.y); r0.y = v.y; r6.w = s; pv = sclampv(s, false); }
    { vec4 v = vec4(gdSum3(gdMul(r1.zxyy, cP[87].zxyy))); float s = cP[88].w - r0.y; r0.x = v.x; r1.z = s; pv = sclampv(s, false); }
    { vec4 v = r5.zwww + r5.xyyy; float s = (1.0 / r0.z); r1.xy = v.xy; r0.y = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r0.yyyy, cP[9].xxxx); float s = (-cP[87].w) - (-r0.x); r0.z = clamp(v, 0.0, 1.0).z; r6.y = s; pv = sclampv(s, false); }
    { vec4 v = r1.zxyy + r6.yzww; float s = vec4(0.0, 1.0, -1.0, 2.0).y - r0.z; r1.xyz = v.xyz; r0.x = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r0.xxxx, cP[82].wwww) + cP[82].zzzz; float s = pv; r0.x = v.x; pv = sclampv(s, false); }
    { vec4 v = gdMul(r0.wwww, r0.xxxx); float s = cP[89].y - cP[89].x; r3.x = clamp(v, 0.0, 1.0).x; r0.w = s; pv = sclampv(s, false); }
    { vec4 v = (-r3.yzxx) + vec4(0.0, 1.0, -1.0, 2.0).yyyy; float s = (1.0 / r1.x); r0.xyz = v.xyz; r3.z = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r0.xxxy, cP[89].zzzz) + (-cP[89].wwww); float s = pv; r2.zw = v.zw; pv = sclampv(s, false); }
    { vec4 v = gdMul((-r2.wzzz), r0.wwww) + cP[89].yyyy; float s = pv; r0.xy = v.xy; pv = sclampv(s, false); }
    { float s = (1.0 / r0.y); r3.y = s; pv = sclampv(s, false); }
    { vec4 v = (-cP[86].xxxx) + vec4(0.0, 1.0, -1.0, 2.0).yyyy; float s = (1.0 / r0.x); r5.x = v.x; r3.x = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r6.xxyy, r3.xyzz); float s = pv; r3.xyz = v.xyz; pv = sclampv(s, false); }
    { vec4 v = gdMul(r3.zzzz, vec4(0.0, 1.0, -1.0, 2.0).wwww) + vec4(0.0, 1.0, -1.0, 2.0).zzzz; float s = pv; r0.w = v.w; pv = sclampv(s, false); }
    { vec4 v = mix(vec4(0.0, 1.0, -1.0, 2.0).xxxx, abs(r0).wwww, vec4(equal(vec4(0.0, 1.0, -1.0, 2.0).xxxy, vec4(0.0)))); float s = pv; r2.zw = v.zw; pv = sclampv(s, false); }
    r0.y = texture(sXSection, r2.zw).x;  // sT11: gSpotXSectionMap
    { vec4 v = r2.xyyy + (-cP[25].zzzz); float s = pv; r0.xw = v.xw; pv = sclampv(s, false); }
    { vec4 v = r4.zwxy + (-cP[25].xxyy); float s = gdMulS(cP[86].x, r0.y); r4.xyzw = v.xyzw; r2.x = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r4, r4); float s = gdMulS(r0.x, r0.x); r4.xyzw = v.xyzw; r2.y = s; pv = sclampv(s, false); }
    { vec4 v = r4.xxyy + r4.zzww; float s = gdMulS(r0.w, r0.w); r5.yz = v.yz; r2.z = s; pv = sclampv(s, false); }
    { vec4 v = r5.yxzz + r2.yxzz; float s = pv; r0.xyw = v.xyw; pv = sclampv(s, false); }
    { vec4 v = r1.yyzz + cP[30].wwww; float s = sqrt(abs(r0).w); r1.yw = v.yw; r1.z = s; pv = sclampv(s, false); }
    { vec4 v = min(r1.yyww, r3.xxxx); float s = sqrt(abs(r0).x); r1.yw = v.yw; r1.x = s; pv = sclampv(s, false); }
    { vec4 v = gdMul((-r1.xzzz), cP[25].wwww) + vec4(0.0, 1.0, -1.0, 2.0).yyyy; float s = pv; r1.xz = clamp(v, 0.0, 1.0).xz; pv = sclampv(s, false); }
    { vec4 v = min(r1.ywww, r3.yyyy); float s = r1.x + r1.z; r0.xw = v.xw; r1.z = s; pv = sclampv(s, false); }
    { vec4 v = r0.xxxx + (-r0.wwww); float s = r1.y - r1.w; r1.x = v.x; r1.y = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r1.xzyy, vec4(0.375, 0.625, 0.5, -0.5).xzyy); float s = pv; r1.xyz = v.xyz; pv = sclampv(s, false); }
    { float s = r1.z + r1.x; r1.w = s; pv = sclampv(s, false); }
    { vec4 v = abs(r1).wwww + cP[91].yyyy; float s = pv; r1.w = v.w; pv = sclampv(s, false); }
    { float s = (1.0 / r1.w); r0.x = s; pv = sclampv(s, false); }
    { float s = gdMulS(cP[91].y, r0.x); r0.x = clamp(s, 0.0, 1.0); pv = sclampv(s, true); }
    { float s = vec4(0.0, 1.0, -1.0, 2.0).y - r0.x; r1.x = s; pv = sclampv(s, false); }
    { vec4 v = gdMul(r1.yyyy, r1.xxxx); float s = pv; r1.x = v.x; pv = sclampv(s, false); }
    { vec4 v = gdMul(r1.xxxx, r0.yyyy); float s = pv; r0.w = v.w; pv = sclampv(s, false); }
    { vec4 v = gdMul(r0.wwww, r0.zzzz); float s = pv; r0.x = v.x; pv = sclampv(s, false); }
    { vec4 v = gdMul(r0.xxxx, cP[90].xyzz); float s = pv; e0.xyz = v.xyz; e0.w = 0.0; pv = sclampv(s, false); }
    return;
}

void main() {
    r0 = vClip;
    r1 = vec4(vWorld, 0.0);
    gdRun(vClip.xy / vClip.w * 0.5 + 0.5);
    fragColor = vec4(e0.xyz, 0.0);
}
)GLSL";
struct RbBeamBlock {
    float viewProj[16];
    float c[92][4];
};

ProgramDesc rbBeamDesc() {
    ProgramDesc d;
    d.name = "spotlight_rb_beam";
    d.vertex = kRbBeamVertex;
    d.fragment = kRbBeamFragment;
    d.uniformBlocks = {"GdRbBeam"};
    d.samplers = {"sDepth", "sDensity", "sXSection"};
    return d;
}

ProgramDesc flareDesc() {
    ProgramDesc d;
    d.name = "spotlight_flare";
    d.vertex = kFlareVertex;
    d.fragment = kFlareFragment;
    d.uniformBlocks = {"GdFlare"};
    d.samplers = {"sTex", "sVis"};
    return d;
}

const scene::Environ* dirEnv(const scene::Directory* d) {
    if (!d) return nullptr;
    const auto* own = d->self ? d->self->as<scene::DirObj>() : nullptr;
    if (!own || !own->hasRnd || own->rnd.environment.empty()) return nullptr;
    return d->resolve<scene::Environ>(own->rnd.environment);
}

Vec3 v3(const std::array<float, 3>& a) { return {a[0], a[1], a[2]}; }

Vec4 mul(const Mat4& m, Vec3 p) {
    return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3), m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
            m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3), m.at(3, 0) * p.x + m.at(3, 1) * p.y + m.at(3, 2) * p.z + m.at(3, 3)};
}

// The material of a beam / floor spot / lens: GH2's Mat with the colour
// from the vertices (prelit, no environment), no depth writes, added.
Material fxMaterial(SceneCache& cache, const scene::Mat& mat, const ScenePose& pose) {
    Material m = cache.material(mat, &pose);
    m.prelit = true;
    m.useEnviron = false;
    m.cull = false;
    if (m.zMode == ZMode::Normal || m.zMode == ZMode::Decal) m.zMode = ZMode::Transparent;
    if (m.blend == MatBlend::Src || m.blend == MatBlend::Dest) m.blend = MatBlend::Add;
    return m;
}

// One vertex in GH2's 16-float layout (position, normal, colour, uv, tangent).
void vertex(std::vector<float>& v, Vec3 p, const float rgba[4], float u, float t) {
    v.insert(v.end(), {p.x, p.y, p.z, 0, 0, 1, rgba[0], rgba[1], rgba[2], rgba[3], u, t, 1, 0, 0, 1});
}

struct Batch {
    Material mat;
    std::vector<float> verts;
    std::vector<uint16_t> idx;
};

void quadIdx(Batch& b, uint16_t a, uint16_t c, uint16_t d, uint16_t e) { b.idx.insert(b.idx.end(), {a, c, d, a, d, e}); }

// A beam as GH2 draws it (0x82260B40 -> 0x822601D0 with depthvolume.fx) [C
// unless marked]. The sheet (0x82235F68, is_cone 0 on all 341): top_radius to
// bottom_radius wide over `length`, starting `offset` along the light,
// BillboardZ (constraint 5). The shader ignores the sheet's vertex alpha and
// the material: the colour is
//   spot colour x intensity x 8 x brighten x depth x illum
// depth = saturate((w - nearest) x 0.125 / thickness): w the view depth,
// nearest the closest of the volume's four corners (no nearer than the near
// plane, 0x8225FF98), thickness min(length x 0.33, bottom radius / sin(angle to
// the view)); illum = (1 - |d|)^2: d the screen distance from the light over
// the distance to the end corners (aspect corrected, 0x8225E3C0). The
// cross-section map defaults to white and the density map to black, so
// neither changes it. Evaluated per vertex on a strip of rows [I: GH2 per
// pixel]. GH2 adds the result to the scene in postprocess.fx FogPixel; the
// buffer's scale (0x8225F2E8: total x 1024 x density) is not understood, so
// the gain here keeps 1 [I]. Depth test on instead of the shader's min()
// with the scene's depth [I: the same where a beam is fully hidden].
bool beamStrip(const scene::Spotlight& s, Vec3 pos, Vec3 y, Vec3 dir, const Camera& camera, const float lit[3], Batch& out,
               std::vector<float>* px) {
    const float len = s.beam.length, top = s.beam.topRadius, bottom = s.beam.bottomRadius;
    const Vec3 cam = camera.position();
    const Vec3 fwd = normalize(Vec3{camera.world.m[4], camera.world.m[5], camera.world.m[6]});
    const Vec3 a = pos + y * s.beam.offset;  // the mesh, offset along the light
    Vec3 side = cross(a - cam, dir);
    if (dot(side, side) < 1e-6f) return false;
    side = normalize(side);
    // The volume's corners (0x8225E590): towards the camera and away, at the
    // light and at the end, the sheet's radii narrowed by its borders (0x8222E9E0).
    const float rTop = top * s.beam.expand * (1 - s.beam.topSideBorder * 0.7f);
    const float rEnd = bottom * s.beam.expand * (1 - s.beam.bottomSideBorder * 0.7f);
    Vec3 toCam = cam - pos;
    toCam = toCam - y * dot(toCam, y);
    toCam = dot(toCam, toCam) > 1e-8f ? normalize(toCam) : Vec3{camera.world.m[8], camera.world.m[9], camera.world.m[10]};
    const Vec3 corners[4] = {pos + toCam * rTop, pos - toCam * rTop, pos + y * len + toCam * rEnd, pos + y * len - toCam * rEnd};
    float nearest = 1e30f;
    for (const Vec3& c : corners) nearest = std::min(nearest, dot(c - cam, fwd));
    nearest = std::max(nearest, camera.nearPlane);
    const float cosA = std::fabs(dot(fwd, y)), sinA = std::sqrt(std::max(0.0f, 1 - cosA * cosA));
    float thick = len * 0.33f;
    if (rEnd < thick * sinA) thick = rEnd / sinA;
    const float scale = 0.125f / std::max(thick, 1e-3f);
    // The screen ramp (0x8225E3C0): NDC, x in the picture's aspect.
    const Mat4 vp = camera.viewProj(false);
    const auto ndc = [&](Vec3 p, float& nx, float& ny) {
        const float cw = vp.at(3, 0) * p.x + vp.at(3, 1) * p.y + vp.at(3, 2) * p.z + vp.at(3, 3);
        nx = (vp.at(0, 0) * p.x + vp.at(0, 1) * p.y + vp.at(0, 2) * p.z + vp.at(0, 3)) / cw;
        ny = (vp.at(1, 0) * p.x + vp.at(1, 1) * p.y + vp.at(1, 2) * p.z + vp.at(1, 3)) / cw;
        return cw > 0;
    };
    float lx = 0, ly = 0, ex = 0, ey = 0;
    float reach = 1e-3f;
    if (!ndc(pos, lx, ly)) return false;
    for (int i = 2; i < 4; ++i)
        if (ndc(corners[i], ex, ey)) reach = std::max(reach, std::hypot((ex - lx) * camera.aspect, ey - ly));
    const float k = 8.0f * s.beam.brighten;
    if (px) {  // per pixel (GD02-151): the sheet's two triangles, the beam's constants on every vertex
        const Vec3 q[4] = {a - side * top, a + side * top, a + dir * len + side * bottom, a + dir * len - side * bottom};
        for (int i : {0, 1, 2, 0, 2, 3})
            px->insert(px->end(), {q[i].x, q[i].y, q[i].z, lit[0] * k, lit[1] * k, lit[2] * k, lx, ly, camera.aspect, 1.0f / reach,
                                   nearest, scale});
        return true;
    }
    const int rows = 16;
    const uint16_t base = uint16_t(out.verts.size() / 16);
    if (base > 60000) return false;
    for (int r = 0; r <= rows; ++r) {
        const float v = float(r) / rows;
        const Vec3 c = a + dir * (len * v);
        const float radius = top + (bottom - top) * v;
        for (int u = -1; u <= 1; ++u) {
            const Vec3 p = c + side * (radius * float(u));
            float nx = 0, ny = 0;
            const bool front = ndc(p, nx, ny);
            const float d = front ? std::hypot((nx - lx) * camera.aspect, ny - ly) / reach : 1.0f;
            float illum = std::clamp(1 - d, 0.0f, 1.0f);
            illum *= illum;
            const float depth = std::clamp((dot(p - cam, fwd) - nearest) * scale, 0.0f, 1.0f);
            const float m = k * depth * illum;
            const float col[4] = {lit[0] * m, lit[1] * m, lit[2] * m, 1};
            vertex(out.verts, p, col, (float(u) + 1) * 0.5f, v);
        }
    }
    for (uint16_t r = 0; r < rows; ++r)
        for (uint16_t c = 0; c < 2; ++c) {
            const uint16_t i0 = uint16_t(base + r * 3 + c);
            quadIdx(out, i0, uint16_t(i0 + 1), uint16_t(i0 + 4), uint16_t(i0 + 3));
        }
    return true;
}

}  // namespace

SpotlightView::SpotlightView(World& world, SceneCache& cache) : world_(world), cache_(cache), device_(cache.device()), mesh_(cache.device()) {
    std::string error;
    visProgram_ = device_.createProgram(visDesc(), error);
    if (!visProgram_) SDL_Log("spotlights: %s does not compile: %s", visDesc().name.c_str(), error.c_str());
    error.clear();
    flareProgram_ = device_.createProgram(flareDesc(), error);
    if (!flareProgram_) SDL_Log("spotlights: %s does not compile: %s", flareDesc().name.c_str(), error.c_str());
    error.clear();
    beamProgram_ = device_.createProgram(beamDesc(), error);
    if (!beamProgram_) SDL_Log("spotlights: %s does not compile: %s", beamDesc().name.c_str(), error.c_str());
    error.clear();
    beamCompProgram_ = device_.createProgram(beamCompDesc(), error);
    if (!beamCompProgram_) SDL_Log("spotlights: %s does not compile: %s", beamCompDesc().name.c_str(), error.c_str());
    error.clear();
    rbBeamProgram_ = device_.createProgram(rbBeamDesc(), error);
    if (!rbBeamProgram_) SDL_Log("spotlights: %s does not compile: %s", rbBeamDesc().name.c_str(), error.c_str());
    {
        // white8x8_keep.bmp (0x822A9A58): the cross-section a beam without one samples.
        TextureDesc td;
        td.width = td.height = 1;
        const uint32_t white = 0xffffffffu;
        const TexLevel lv{&white, 4};
        error.clear();
        white_ = device_.createTexture(td, &lv, error);
    }
    const float tri[6] = {-1, -1, 3, -1, -1, 3};
    triangle_ = device_.createBuffer(BufferKind::Vertex, sizeof tri, tri, false);
    for (TargetId& t : vis_) {
        error.clear();
        t = device_.createTarget(kMaxFlares, 1, false, error);
    }
}

SpotlightView::~SpotlightView() {
    for (TargetId t : vis_)
        if (t) device_.destroyTarget(t);
    if (triangle_) device_.destroyBuffer(triangle_);
    if (flareVb_) device_.destroyBuffer(flareVb_);
    if (visProgram_) device_.destroyProgram(visProgram_);
    if (flareProgram_) device_.destroyProgram(flareProgram_);
    if (beamProgram_) device_.destroyProgram(beamProgram_);
    if (beamCompProgram_) device_.destroyProgram(beamCompProgram_);
    if (beamTarget_) device_.destroyTarget(beamTarget_);
    if (beamVb_) device_.destroyBuffer(beamVb_);
    if (rbBeamProgram_) device_.destroyProgram(rbBeamProgram_);
    if (rbBeamTarget_) device_.destroyTarget(rbBeamTarget_);
    if (densityTarget_) device_.destroyTarget(densityTarget_);
    if (rbVb_) device_.destroyBuffer(rbVb_);
    if (white_) device_.destroyTexture(white_);
}

std::vector<ProgramDesc> SpotlightView::allPrograms() { return {visDesc(), flareDesc(), beamDesc(), beamCompDesc(), rbBeamDesc()}; }

bool readDrawerParams(const std::vector<unsigned char>& rec, DrawerParams& out) {
    auto u32 = [&](size_t p) {
        return uint32_t(rec[p]) << 24 | uint32_t(rec[p + 1]) << 16 | uint32_t(rec[p + 2]) << 8 | uint32_t(rec[p + 3]);
    };
    auto f32 = [&](size_t p) {
        const uint32_t u = u32(p);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    };
    if (rec.size() < 4 || u32(0) != 4) return false;
    size_t pos = 4;
    scene::Draw draw;
    try {
        scene::readDrawAt(rec, pos, draw);
    } catch (const std::exception&) {
        return false;
    }
    if (pos + 32 > rec.size()) return false;
    out.total = f32(pos), out.base = f32(pos + 4), out.smoke = f32(pos + 8), out.range = f32(pos + 12);
    for (int k = 0; k < 4; ++k) out.color[size_t(k)] = f32(pos + 16 + 4 * size_t(k));
    pos += 32;
    for (int k = 0; k < 2; ++k) {
        if (pos + 4 > rec.size()) return false;
        const uint32_t n = u32(pos);
        if (n > rec.size() - pos - 4) return false;
        out.proxy.assign(reinterpret_cast<const char*>(rec.data() + pos + 4), n);
        pos += 4 + n;
    }
    return true;
}

void SpotlightView::setDrawer(const scene::Object* drawer, const scene::Object* proxy) {
    drawer_ = drawer, proxy_ = proxy;
    drawerParams_ = {};
    if (drawer) readDrawerParams(drawer->record, drawerParams_);
}

bool SpotlightView::rbBeams() const {
    static const bool legacy = [] {
        const char* e = SDL_getenv("GD05_BEAMS_LEGACY");
        return e && *e == '1';
    }();
    return !legacy && drawer_ && rbBeamProgram_;
}

// 0x822A9E58 [C]: with a proxy, the drawer's camera (+0x64, a copy of the
// shot's camera, 0x822A9CC8) draws it into the beam system's density
// texture (+0xC: a quarter of the picture each way, 0x822A9A58), with
// the device's +0x25 flag set (what the flag changes in the particles'
// shaders is not read: the proxy's own materials are drawn, additive, onto
// black [I]). Its particles run because they are drawn (D86).
bool SpotlightView::beginDensity(int viewW, int viewH) {
    densityDrawn_ = false;
    if (!rbBeams() || !proxy_) return false;
    const int w = std::max(1, viewW / 4), h = std::max(1, viewH / 4);
    if (!densityTarget_ || densW_ != w || densH_ != h) {
        if (densityTarget_) device_.destroyTarget(densityTarget_);
        TargetDesc d;
        d.width = w, d.height = h, d.depth = true, d.format = TargetFormat::RGBA8;
        std::string error;
        densityTarget_ = device_.createTarget(d, error);
        densW_ = w, densH_ = h;
        if (!densityTarget_) {
            SDL_Log("spotlights: no density map: %s", error.c_str());
            return false;
        }
    }
    device_.setTarget(densityTarget_);
    device_.setViewport(0, 0, w, h);
    const float black[4] = {0, 0, 0, 0};
    device_.clear(black, true);
    return true;
}

void SpotlightView::endDensity(TargetId worldTarget, int viewW, int viewH) {
    densityDrawn_ = true;
    device_.setTarget(worldTarget);
    device_.setViewport(0, 0, viewW, viewH);
}

// 0x822ABDC0 -> 0x822ABC28 [C]: the beam buffer (+8: half the picture each
// way, 32 bits, 0x822A9A58) cleared; per queued spotlight with a beam
// length > 0, by its beam shape (+0x1E8): 2 -> 0x822ABA80, 3 / 4 ->
// 0x822AB930, else 0x822AB858 -> 0x822AABE0 (every beam on the disc is
// shape 0 or 1: this pass). Its constants (0x822AABE0, 0x822AA458,
// 0x822ABDC0, 0x822A8EF8) as below; the beam's mesh is the camera-facing
// sheet between the side planes 0x822AA458 builds (the beam's own mesh
// there [I]); added (ONE / ONE) [I: the pass's blend state is not read].
// Then gFogDensity (0x822A8E78) and gFogRange.x (0x822AB5B8) for the post.
void SpotlightView::drawRbBeams(const Camera& camera, int viewW, int viewH, TextureId worldDepth) {
    const int bw = std::max(1, viewW / 2), bh = std::max(1, viewH / 2);
    if (!rbBeamTarget_ || rbW_ != bw || rbH_ != bh) {
        if (rbBeamTarget_) device_.destroyTarget(rbBeamTarget_);
        TargetDesc d;
        d.width = bw, d.height = bh, d.format = TargetFormat::RGBA8;
        std::string error;
        rbBeamTarget_ = device_.createTarget(d, error);
        rbW_ = bw, rbH_ = bh;
        if (!rbBeamTarget_) {
            SDL_Log("spotlights: no beam buffer: %s", error.c_str());
            return;
        }
    }
    device_.setTarget(rbBeamTarget_);
    device_.setViewport(0, 0, bw, bh);
    const float zero[4] = {0, 0, 0, 0};
    device_.clear(zero, false);
    const bool zeroToOne = device_.info().clipDepthZeroToOne;
    const Vec3 cam = camera.position();
    const Vec3 camFwd = normalize(Vec3{camera.world.m[4], camera.world.m[5], camera.world.m[6]});
    const float n = camera.nearPlane, f = camera.farPlane, invFar = f > 0 ? 1.0f / f : 0.0f;
    // One sheet (two triangles) per beam.
    std::vector<float> v;
    v.reserve(rbList_.size() * 18);
    struct Planes {
        float c86, c87[4], c88[4];
    };
    std::vector<Planes> planes;
    for (const RbBeam& b : rbList_) {
        // 0x822AA458: the sheet across the view, its side planes through the camera.
        Vec3 V = b.pos - cam;
        V = dot(V, V) > 1e-12f ? normalize(V) : camFwd;
        Vec3 S = cross(V, b.dir);
        S = dot(S, S) > 1e-12f ? normalize(S) : Vec3{1, 0, 0};
        const Vec3 B = b.pos + b.dir * b.length;
        const Vec3 P1 = b.pos + S * b.rTop, P2 = b.pos - S * b.rTop, Q1 = B + S * b.rBottom, Q2 = B - S * b.rBottom;
        auto unit = [](Vec3 a) { return dot(a, a) > 1e-20f ? normalize(a) : a; };
        const Vec3 N1 = unit(cross(P1 - cam, Q1 - cam)), N2 = unit(cross(P2 - cam, Q2 - cam));
        const Vec3 M = N1 + N2;
        const float s1 = dot(N1, M), s2 = dot(N2, M);
        const float i1 = s1 != 0 ? 1 / s1 : 0, i2 = s2 != 0 ? 1 / s2 : 0;
        Planes pl;
        pl.c87[0] = N1.x * i1, pl.c87[1] = N1.y * i1, pl.c87[2] = N1.z * i1, pl.c87[3] = dot(cam, N1) * i1;
        pl.c88[0] = N2.x * i2, pl.c88[1] = N2.y * i2, pl.c88[2] = N2.z * i2, pl.c88[3] = dot(cam, N2) * i2;
        // 0x822A90B0 (margin 0, no length test): the camera inside the cone
        // turns the cross-section off (gXSectionStrength 0).
        bool inside = false;
        {
            const Vec3 d = cam - b.pos;
            const float t = dot(d, b.dir);
            if (t >= 0) {
                Vec3 R = d - b.dir * t;
                R = dot(R, R) > 1e-12f ? normalize(R) : S;
                const Vec3 a0 = b.pos + R * b.rTop, a1 = B + R * b.rBottom, e = a1 - a0;
                const Vec3 w = cam - a0;
                const float ee = dot(e, e), k = ee > 0 ? dot(w, e) / ee : 0;
                const Vec3 off = w - e * k;
                inside = dot(off, R) < 0;  // on the axis side of the slanted edge (|off| < 0 never holds)
            }
        }
        pl.c86 = inside ? 0.0f : 1.0f;
        planes.push_back(pl);
        const Vec3 q[6] = {P1, P2, Q2, P1, Q2, Q1};
        for (const Vec3& p : q) v.insert(v.end(), {p.x, p.y, p.z});
    }
    const size_t bytes = v.size() * sizeof(float);
    if (bytes > rbVbSize_) {
        if (rbVb_) device_.destroyBuffer(rbVb_);
        rbVbSize_ = std::max<size_t>(bytes * 2, 16 * 1024);
        rbVb_ = device_.createBuffer(BufferKind::Vertex, rbVbSize_, nullptr, true);
    }
    device_.updateBuffer(rbVb_, 0, v.data(), bytes);
    static const VertexLayout layout = [] {
        VertexLayout l;
        l.stride = 12;
        l.attribs[0] = {0, 3, AttribType::Float, 0};
        return l;
    }();
    const Mat4 vp = camera.viewProj(zeroToOne);
    TextureId density = densityDrawn_ ? device_.targetTexture(densityTarget_) : TextureId{};
    if (!density) density = white_;  // 0x822A9D88: no proxy -> the drawer's texture or black8x8 [I: none drawn]
    auto set = [](float* c, float x, float y, float z, float w) { c[0] = x, c[1] = y, c[2] = z, c[3] = w; };
    for (size_t i = 0; i < rbList_.size(); ++i) {
        const RbBeam& b = rbList_[i];
        RbBeamBlock blk{};
        std::copy(vp.m, vp.m + 16, blk.viewProj);
        set(blk.c[9], 0.125f, 0.125f, 0.125f, 0.125f);  // gDepthHalf (0x822ABDC0, 0x82887374)
        set(blk.c[10], cam.x, cam.y, cam.z, 1);            // gCamPos
        // 0x822AABE0: gSpotPos (pos, 1 / length), gSpotDir (dir, length),
        // gSpotToCam, gCamDir (the camera's y row, -dir . pos), gRadii
        // (top, bottom, the beam's start and end along dir), gConst_u.
        set(blk.c[25], b.pos.x, b.pos.y, b.pos.z, 1 / b.length);
        set(blk.c[26], b.dir.x, b.dir.y, b.dir.z, b.length);
        const Vec3 cp = cam - b.pos;
        set(blk.c[27], cp.x, cp.y, cp.z, 1);
        set(blk.c[30], camFwd.x, camFwd.y, camFwd.z, -dot(camFwd, cam));
        const float start = dot(b.dir, b.pos);
        set(blk.c[29], b.rTop, b.rBottom, start, start + b.length);
        const float k = (b.rBottom - b.rTop) / b.length;
        const float apex = b.rBottom - b.rTop != 0 ? b.length / (b.rBottom - b.rTop) * b.rTop : 0;
        const float m = dot(b.dir, cp), a = apex + m;
        set(blk.c[28], k * k + 1, a * k * k + m, dot(cp, cp) - m * m - a * a * k * k, 1);
        set(blk.c[82], 0, invFar, 0, 0);  // 0x822A8EF8
        set(blk.c[86], planes[i].c86, 0, 0, 0);
        std::copy(planes[i].c87, planes[i].c87 + 4, blk.c[87]);
        std::copy(planes[i].c88, planes[i].c88 + 4, blk.c[88]);
        set(blk.c[89], n, f, 1, 0);  // gZRange: near, far, the camera's z range 0..1
        set(blk.c[90], b.color[0], b.color[1], b.color[2], 1);
        set(blk.c[91], 0, drawerParams_.range, 0, invFar);
        DrawCall c;
        c.program = rbBeamProgram_;
        c.state.blend = true;
        c.state.src = BlendFactor::One, c.state.dst = BlendFactor::One;
        c.layout = &layout;
        c.vertices = rbVb_;
        c.first = int(i * 6), c.count = 6;
        c.textures[0] = worldDepth;
        c.samplers[0] = {Filter::Nearest, Wrap::Clamp, 0};
        c.textures[1] = density;
        c.samplers[1] = {Filter::Linear, Wrap::Clamp, 0};
        c.textures[2] = b.xsection ? b.xsection : white_;
        c.samplers[2] = {Filter::Linear, Wrap::Clamp, 0};
        c.uniforms[0] = device_.stageUniforms(&blk, sizeof blk);
        device_.draw(c);
        ++rbBeamsDrawn_;
    }
    // The composite's inputs (0x822A8E78: gFogDensity = (base x 0.01, smoke
    // x 0.01 x (1 - base x 0.01)); 0x822AB5B8: gFogRange.x = total x 32; the
    // beam buffer as s12, the density map as s6) [C].
    float total = drawerParams_.total, base = drawerParams_.base, smoke = drawerParams_.smoke;
    if (const World::DrawerState* st = world_.findDrawerState(drawer_)) total = st->total, base = st->base, smoke = st->smoke;
    if (SDL_getenv("GD05_DRAWER_ZERO")) base = smoke = 0;  // the walks' check: no smoke, no beams
    fog_.on = true;
    fog_.beams = device_.targetTexture(rbBeamTarget_);
    fog_.density = density;
    fog_.c82x = base * 0.01f;
    fog_.c82y = smoke * 0.01f * (1 - base * 0.01f);
    fog_.c91x = total * 32.0f;
    if (SDL_getenv("GD05_BEAM_PROBE")) probeBeams();
    // A development aid: GD05_BEAM_DUMP=<prefix> saves the beam buffer and the density map (BMP) each frame.
    static int dumpFrame = 0;
    if (const char* dump0 = SDL_getenv("GD05_BEAM_DUMP"); dump0 && dumpFrame++ % 120 == 0) {
        const std::string dump = std::string(dump0) + "_" + std::to_string(dumpFrame / 120);
        if (SDL_Surface* bs = device_.readTarget(rbBeamTarget_, 0, 0, rbW_, rbH_)) {
            SDL_SaveBMP(bs, (dump + "_beams.bmp").c_str());
            SDL_DestroySurface(bs);
        }
        if (densityDrawn_)
            if (SDL_Surface* ds = device_.readTarget(densityTarget_, 0, 0, densW_, densH_)) {
                SDL_SaveBMP(ds, (dump + "_density.bmp").c_str());
                SDL_DestroySurface(ds);
            }
    }
}

// The walks' measure: the pixels (of the beam buffer's size) where the
// composite adds at least 1/255 in some channel.
void SpotlightView::probeBeams() {
    beamPixels_ = -1;
    SDL_Surface* bs = device_.readTarget(rbBeamTarget_, 0, 0, rbW_, rbH_);
    SDL_Surface* ds = densityDrawn_ ? device_.readTarget(densityTarget_, 0, 0, densW_, densH_) : nullptr;
    if (bs) {
        SDL_Surface* b = SDL_ConvertSurface(bs, SDL_PIXELFORMAT_RGBA32);
        SDL_Surface* d = ds ? SDL_ConvertSurface(ds, SDL_PIXELFORMAT_RGBA32) : nullptr;
        long long count = 0;
        if (b) {
            for (int y = 0; y < b->h; ++y)
                for (int x = 0; x < b->w; ++x) {
                    const uint8_t* p = static_cast<const uint8_t*>(b->pixels) + size_t(y) * size_t(b->pitch) + size_t(x) * 4;
                    float dens = 1;
                    if (d) {
                        const int dx = std::min(d->w - 1, x * d->w / b->w), dy = std::min(d->h - 1, y * d->h / b->h);
                        dens = float(static_cast<const uint8_t*>(d->pixels)[size_t(dy) * size_t(d->pitch) + size_t(dx) * 4]) / 255.0f;
                    }
                    const float g = (fog_.c82x + fog_.c82y * dens) * fog_.c91x;
                    const int mx = std::max({p[0], p[1], p[2]});
                    if (float(mx) * g >= 1.0f) ++count;
                }
            beamPixels_ = count;
        }
        if (b) SDL_DestroySurface(b);
        if (d) SDL_DestroySurface(d);
    }
    if (bs) SDL_DestroySurface(bs);
    if (ds) SDL_DestroySurface(ds);
}

int SpotlightView::slotOf(const void* key) {
    auto it = slots_.find(key);
    if (it != slots_.end()) return it->second;
    const int s = int(slots_.size()) % kMaxFlares;
    slots_[key] = s;
    return s;
}

void SpotlightView::draw(MaterialRenderer& mr, const Camera& camera, int viewW, int viewH, bool hdr, TargetId worldTarget,
                         TextureId worldDepth) {
    beams_ = flares_ = spots_ = 0;
    const uint64_t nowNs = SDL_GetTicksNS();
    const float dt = lastDrawNs_ ? float(double(nowNs - lastDrawNs_) * 1e-9) : 0.0f;
    lastDrawNs_ = nowNs;
    flareList_.clear();
    // Development aid: GD05_SPOTS=<letters> draws only those parts (c cans,
    // f floor spots, b beams, l lenses, F flares); unset: all.
    const char* only = SDL_getenv("GD05_SPOTS");
    auto on = [&](char c) { return !only || std::strchr(only, c) != nullptr; };
    const ScenePose& pose = world_.pose();
    Venue& venue = world_.venue();
    const bool mp = venue.variant() != VenueVariant::Single;
    const Vec3 camPos = camera.position();
    const Vec3 camFwd = normalize(Vec3{camera.world.m[4], camera.world.m[5], camera.world.m[6]});
    std::map<const scene::Mat*, Batch> batches;
    Batch beamBatch;
    beamBatch.mat.prelit = true, beamBatch.mat.useEnviron = false, beamBatch.mat.cull = false;
    beamBatch.mat.blend = MatBlend::Add, beamBatch.mat.zMode = ZMode::Transparent;
    // Per pixel into the beam buffer when the world's depth is there (post-
    // processing on); else the old per-vertex sheet in the world (GD02-151).
    const bool perPixel = worldTarget && worldDepth && beamProgram_ && beamCompProgram_;
    beamPx_.clear();
    // Rock Band's volume beams instead (GD05-093): NgSpotlightDrawer's beam
    // pass (vtable +0x44) is an empty function (0x824A3138), so the beam
    // sheets with their beam material are never drawn; the beams reach the
    // picture only through the volume passes and the post composite.
    const bool rbPass = rbBeams() && worldTarget && worldDepth;
    rbList_.clear();
    fog_ = {};
    rbBeamsDrawn_ = 0;
    if (!rbPass) beamPixels_ = -1;
    auto batch = [&](const scene::Mat* m) -> Batch& {
        auto [it, added] = batches.try_emplace(m);
        if (added) it->second.mat = fxMaterial(cache_, *m, pose);
        return it->second;
    };
    auto findMat = [&](const scene::Object& o, const std::string& name) -> const scene::Mat* {
        if (name.empty()) return nullptr;
        if (o.dir)
            if (auto* m = o.dir->resolve<scene::Mat>(name)) return m;
        return venue.find<scene::Mat>(name);
    };
    struct Can {
        const scene::Mesh* mesh;
        Mat4 world;
        const scene::Environ* env;
    };
    // additional_objects (Spotlight's draw children, 0x822348F8): drawn in the
    // queue under an Environ whose ambient is the spot's colour x intensity
    // (0x8222F738 under 0x8222EC40) [C]: the circles the spots throw.
    struct Extra {
        const scene::Mesh* mesh;
        Mat4 world;
        Vec3 color;
    };
    std::vector<Extra> extras;
    std::vector<Can> cans;
    struct Shadow {
        std::string target;
        Vec3 dir;
    };
    std::vector<Shadow> shadows;
    const auto& states = world_.spots();
    for (const scene::Spotlight* s : venue.all<scene::Spotlight>()) {
        if (!s->decoded || !pose.showing(s)) continue;
        // The preset's state over the object's own.
        float intensity = s->intensity;
        std::array<float, 4> color = s->color;
        std::string target = s->target, subpart;
        bool flareOn = s->flareEnabled;
        scene::Xfm x = pose.world(s);
        if (auto it = states.find(s); it != states.end()) {
            intensity = it->second.intensity, color = it->second.color;
            target = it->second.target, subpart = it->second.subpart;
            flareOn = flareOn && it->second.flare;
            // The poll (0x82233250): the preset's quaternion is the LOCAL rotation.
            if (it->second.rotation != std::array<float, 4>{}) {
                scene::Xfm lx = pose.local(s);
                scene::quatToRot(it->second.rotation, lx.rot);
                x = pose.world(s, lx);
            }
        }
        // 0x822328E8: queued when intensity x green or x blue reads as a
        // byte (the code tests green twice, not red) [C].
        if ((int(intensity * color[1] * 255.0f) & 0xff) == 0 && (int(intensity * color[2] * 255.0f) & 0xff) == 0) continue;
        const float lit[3] = {color[0] * intensity, color[1] * intensity, color[2] * intensity};
        ++spots_;
        const Vec3 pos = v3(x.pos);
        Vec3 right{x.rot[0], x.rot[1], x.rot[2]}, fwd{x.rot[3], x.rot[4], x.rot[5]}, up{x.rot[6], x.rot[7], x.rot[8]};
        // The light shines along its y row [C: the can, beam, lens and flare
        // offsets, 0x82231AF0, and the aim]. A target turns it there (0x82231610:
        // y = target - position, z from y x (1 0 0)); a target its entry
        // names that is not in the venue leaves the spotlight off (0x8223ADE8
        // clears the flag, 0x82232698 clears +0x1ec, 0x822328E8 skips it).
        scene::Xfm rootXfm;
        bool targeted = false;
        if (!target.empty()) {
            scene::Xfm tx;
            if (!venueObjectXfm(world_, target, mp, tx, subpart)) continue;
            venueObjectXfm(world_, target, mp, rootXfm);
            targeted = true;
            const Vec3 d = v3(tx.pos) - pos;
            if (dot(d, d) > 1e-4f) {
                fwd = normalize(d);
                // damping_constant != 1 (15 spotlights): the aim eases there,
                // dt x damping of the way each poll (0x82233250) [C; a
                // normalised lerp in place of 0x8226A980's slerp, I].
                if (s->dampingConstant != 1) {
                    auto [it, fresh] = damped_.try_emplace(s, fwd);
                    if (!fresh) {
                        const float k = std::clamp(dt * s->dampingConstant, 0.0f, 1.0f);
                        it->second = normalize(it->second + (fwd - it->second) * k);
                        fwd = it->second;
                    }
                }
                Vec3 n = cross(d, Vec3{1, 0, 0});
                if (dot(n, n) < 1e-6f) n = Vec3{0, 0, 1};
                right = normalize(cross(fwd, normalize(n)));
                up = normalize(cross(right, fwd));
                x.rot = {right.x, right.y, right.z, fwd.x, fwd.y, fwd.z, up.x, up.y, up.z};
            }
        }
        Vec3 dir = fwd;
        // target_shadow (0x822312C8, drawn first by 0x8222EE58): the target
        // character's shadow group flattened along the light (Character 0x82151B98).
        if (targeted && s->targetShadow) shadows.push_back({target, dir});
        // angle_offset (degrees) tilts the beam about the light's x and z [I].
        if (s->beam.angleOffset[0] != 0 || s->beam.angleOffset[1] != 0) {
            const float ax = s->beam.angleOffset[0] * kPi / 180, ay = s->beam.angleOffset[1] * kPi / 180;
            dir = normalize(dir * std::cos(ax) + up * std::sin(ax));
            dir = normalize(dir * std::cos(ay) + right * std::sin(ay));
        }
        // The light can.
        if (!s->lightCan.empty() && on('c'))
            if (auto* can = s->dir ? s->dir->resolve<scene::Mesh>(s->lightCan) : nullptr; can || (can = venue.find<scene::Mesh>(s->lightCan))) {
                scene::Xfm cx = x;
                cx.rot = {right.x, right.y, right.z, fwd.x, fwd.y, fwd.z, up.x, up.y, up.z};
                const Vec3 cp = pos + dir * s->lightCanOffset;
                cx.pos = {cp.x, cp.y, cp.z};
                cans.push_back({can, toMat4(cx), dirEnv(can->dir)});
            }
        if (on('f'))
            for (const std::string& n : s->additionalObjects)
                if (auto* m = s->dir ? s->dir->resolve<scene::Mesh>(n) : nullptr; m && pose.showing(m))
                    extras.push_back({m, toMat4(pose.world(m)), Vec3{lit[0], lit[1], lit[2]}});
        // The beam: depthvolume.fx on the sheet of 0x82235F68 (0x822601D0, 0x82260B40).
        if (s->beam.length > 0 && on('b') && rbPass) {
            // 0x822AB858: gFogColor = colour x intensity x 8 (the colour
            // owner's), 0x822AABE0: x brighten; radii 0x82297B88: x expand,
            // a sheet's (is_cone 0) also x (1 - side border x 0.7) [C]. The
            // beam along the light's y row (the transform's +0x10, c26).
            RbBeam rb;
            rb.pos = pos, rb.dir = normalize(fwd), rb.length = s->beam.length;
            rb.rTop = s->beam.topRadius * s->beam.expand, rb.rBottom = s->beam.bottomRadius * s->beam.expand;
            if (!s->beam.isCone) rb.rTop *= 1 - s->beam.topSideBorder * 0.7f, rb.rBottom *= 1 - s->beam.bottomSideBorder * 0.7f;
            for (int k = 0; k < 3; ++k) rb.color[k] = lit[k] * 8.0f * s->beam.brighten;
            if (!s->beam.xsection.empty()) rb.xsection = cache_.texture(s, s->beam.xsection);
            rbList_.push_back(rb);
            ++beams_;
        } else if (s->beam.length > 0 && on('b') && beamStrip(*s, pos, fwd, dir, camera, lit, beamBatch, perPixel ? &beamPx_ : nullptr)) {
            ++beams_;
        }
        // The floor spot (0x82231AF0, its mesh 0x82235488: a unit quad, uv 0..1):
        // only with a target and spot_material, where the light's y axis
        // meets z = the target's z + spot_height, spot_scale wide across and
        // spot_scale x |d| / |d.z| long along the light's heading.
        if (const scene::Mat* sm = on('f') && targeted ? findMat(*s, s->spotMaterial) : nullptr; sm) {
            const Vec3 d = v3(rootXfm.pos) - pos;
            if (d.z != 0 && fwd.z != 0) {
                const float t = (rootXfm.pos[2] + s->spotHeight - pos.z) / fwd.z;
                const Vec3 c = pos + fwd * t;
                Vec3 along{fwd.x, fwd.y, 0};
                if (dot(along, along) < 1e-8f) along = Vec3{0, 1, 0};
                along = normalize(along);
                const Vec3 across = normalize(cross(along, Vec3{0, 0, 1}));
                const float hw = s->spotScale * 0.5f, hl = hw * std::sqrt(dot(d, d)) / std::fabs(d.z);
                Batch& bt = batch(sm);
                const uint16_t base = uint16_t(bt.verts.size() / 16);
                const float col[4] = {lit[0], lit[1], lit[2], 1};
                vertex(bt.verts, c + across * -hw + along * -hl, col, 0, 0);
                vertex(bt.verts, c + across * hw + along * -hl, col, 1, 0);
                vertex(bt.verts, c + across * -hw + along * hl, col, 0, 1);
                vertex(bt.verts, c + across * hw + along * hl, col, 1, 1);
                quadIdx(bt, base, uint16_t(base + 1), uint16_t(base + 3), uint16_t(base + 2));
            }
        }
        // The lens (lens_material, 0x82231AF0): the unit quad lens_size wide
        // at lens_offset along the light, square to it.
        if (const scene::Mat* lm = on('l') ? findMat(*s, s->lensMaterial) : nullptr; lm && s->lensSize > 0) {
            const Vec3 c = pos + fwd * s->lensOffset;
            Batch& bt = batch(lm);
            const uint16_t base = uint16_t(bt.verts.size() / 16);
            const float r = s->lensSize * 0.5f;
            const float col[4] = {lit[0], lit[1], lit[2], 1};
            vertex(bt.verts, c + (right * -r) + (up * -r), col, 0, 0);
            vertex(bt.verts, c + (right * r) + (up * -r), col, 1, 0);
            vertex(bt.verts, c + (right * r) + (up * r), col, 1, 1);
            vertex(bt.verts, c + (right * -r) + (up * r), col, 0, 1);
            quadIdx(bt, base, uint16_t(base + 1), uint16_t(base + 2), uint16_t(base + 3));
        }
        // The flare.
        if (flareOn && on('F'))
            if (const scene::Mat* fm = findMat(*s, s->flareMaterial)) {
                Flare f;
                f.pos = pos + dir * s->flareOffset;
                const Vec3 toCam = normalize(camPos - f.pos);
                const float face = std::clamp(dot(dir, toCam), 0.0f, 1.0f);
                f.size = s->flareSize[0] + (s->flareSize[1] - s->flareSize[0]) * face;
                const float depth = dot(f.pos - camPos, camFwd);
                float fade = 1;
                if (s->flareRange[0] != s->flareRange[1])
                    fade = std::clamp((depth - s->flareRange[1]) / (s->flareRange[0] - s->flareRange[1]), 0.0f, 1.0f);
                const float in = std::min(1.0f, intensity);
                for (int q = 0; q < 3; ++q) f.rgba[q] = std::min(1.0f, color[size_t(q)]) * in * fade;
                f.rgba[3] = fade;
                f.tex = cache_.material(*fm, &pose).diffuseTex;
                f.test = !s->flareVisibilityTest;  // stored inverted (0x822328E8 passes byte == 0)
                f.steps = int(std::max<uint32_t>(1, s->flareSteps));
                f.slot = slotOf(s);
                if (fade > 0 && depth > 0) flareList_.push_back(f);
            }
    }
    // RndFlare objects (always tested).
    for (const scene::Flare* fl : venue.all<scene::Flare>()) {
        if (!fl->decoded || !pose.showing(fl) || !on('F')) continue;
        const scene::Mat* fm = findMat(*fl, fl->mat);
        if (!fm) continue;
        const scene::Xfm x = pose.world(fl);
        Flare f;
        f.pos = v3(x.pos);
        const float face = std::clamp(-dot(Vec3{x.rot[3], x.rot[4], x.rot[5]}, camFwd), 0.0f, 1.0f);
        f.size = fl->size[0] + (fl->size[1] - fl->size[0]) * face;
        const float depth = dot(f.pos - camPos, camFwd);
        float fade = 1;
        if (fl->range[0] != fl->range[1]) fade = std::clamp((depth - fl->range[1]) / (fl->range[0] - fl->range[1]), 0.0f, 1.0f);
        f.rgba[0] = f.rgba[1] = f.rgba[2] = fade;
        f.rgba[3] = fade;
        f.tex = cache_.material(*fm, &pose).diffuseTex;
        f.test = true;
        f.steps = int(std::max<uint32_t>(1, fl->steps));
        f.slot = slotOf(fl);
        if (fade > 0 && depth > 0) flareList_.push_back(f);
    }

    if (SDL_getenv("GD05_SPOTS_LOG")) {
        static int frames = 0;
        if (frames++ % 60 == 0) {
            SDL_Log("spots: %d lit, %d beams, %d flares", spots_, beams_, int(flareList_.size()));
            for (const Flare& f : flareList_)
                SDL_Log("  flare at %.0f %.0f %.0f size %.2f rgba %.2f %.2f %.2f %.2f test %d steps %d", f.pos.x, f.pos.y, f.pos.z, f.size,
                        f.rgba[0], f.rgba[1], f.rgba[2], f.rgba[3], f.test, f.steps);
        }
    }
    DrawContext ctx;
    ctx.hdr = hdr;
    // Cans, lit by their directory's environment.
    const scene::Environ* lastEnv = nullptr;
    bool envSet = false;
    for (const Can& c : cans) {
        const GpuMesh* g = cache_.mesh(c.mesh);
        if (!g || g->skinned || !c.mesh->dir) continue;
        if (!envSet || c.env != lastEnv) {
            LightEnv le;
            if (c.env) le = cache_.lightEnv(*c.env, &pose);
            mr.setFrame(camera, le, viewW, viewH);
            lastEnv = c.env, envSet = true;
        }
        const Mat4 bones[1] = {c.world};
        const scene::Mat* mat = c.mesh->dir->resolve<scene::Mat>(c.mesh->mat);
        if (!mat) {
            mr.draw(*g, bones, Material{}, ctx);
            continue;
        }
        for (int pass = 0; mat && pass < 4; ++pass) {
            mr.draw(*g, bones, cache_.material(*mat, &pose), ctx);
            if (mat->nextPass.empty() || !mat->dir) break;
            const scene::Mat* next = mat->dir->resolve<scene::Mat>(mat->nextPass);
            mat = next != mat ? next : nullptr;
        }
    }
    // Floor spots, beams and lenses: unlit, added.
    LightEnv plain;
    plain.ambient = {1, 1, 1};
    mr.setFrame(camera, plain, viewW, viewH);
    // SpotlightDrawer's draw (0x8222EC40) starts with the shadows (0x8222EE58).
    // shadow.mat adds black and writes depth, so the flattened character,
    // just above the floor, shuts the floor spots out of its shape: the
    // shadow is a hole in the pool of light [C for the order, I for the reading].
    shadowsDrawn_ = 0;
    if (bandView_ && world_.band)
        for (const Shadow& sh : shadows)
            if (chr::Character* c = world_.band->character(sh.target); c && c->visible && sh.dir.z != 0) {
                bandView_->drawShadow(mr, *c, {sh.dir.x, sh.dir.y, sh.dir.z}, hdr);
                ++shadowsDrawn_, ++shadowsTotal_;
            }
    for (const Extra& e : extras) {
        const GpuMesh* g = cache_.mesh(e.mesh);
        const scene::Mat* mat = e.mesh->dir ? e.mesh->dir->resolve<scene::Mat>(e.mesh->mat) : nullptr;
        if (!g || g->skinned || !mat) continue;
        LightEnv le;
        le.ambient = e.color, le.envAmbient = e.color;
        mr.setFrame(camera, le, viewW, viewH);
        const Mat4 bones[1] = {e.world};
        mr.draw(*g, bones, cache_.material(*mat, &pose), ctx);
    }
    if (!extras.empty()) mr.setFrame(camera, plain, viewW, viewH);
    const Mat4 identity[1] = {Mat4::identity()};
    for (auto& [m, b] : batches) {
        if (b.idx.empty()) continue;
        const GpuMesh g = mesh_.upload(b.verts, b.idx);
        mr.draw(g, identity, b.mat, ctx);
    }
    if (!beamBatch.idx.empty()) {
        const GpuMesh g = mesh_.upload(beamBatch.verts, beamBatch.idx);
        mr.draw(g, identity, beamBatch.mat, ctx);
    }
    if (perPixel && !beamPx_.empty()) drawBeams(camera, viewW, viewH, hdr, worldTarget, worldDepth);
    if (rbPass && !rbList_.empty()) {
        drawRbBeams(camera, viewW, viewH, worldDepth);
        device_.setTarget(worldTarget);
        device_.setViewport(0, 0, viewW, viewH);
    }
    drawFlares(camera, viewW, viewH, hdr, worldTarget, worldDepth);
}

// The beam buffer (0x8225F948): its own target, cleared, every beam added
// with no depth test; half size when the picture is wider than 960 [C; the
// upsampling I]. Then FogPixel adds it to the world (before the track and
// HUD are drawn): drawer total (1) x 1024 x density 0.2 on the sampled
// value of the 360's fixed 16-bit EDRAM buffer [C]. What that read returns
// is not settled: 1/64 of the value (a gain of 3.2) turns every beam
// white, so the port keeps gain 1, the brightness its per-vertex beams had
// [I].
void SpotlightView::drawBeams(const Camera& camera, int viewW, int viewH, bool hdr, TargetId worldTarget, TextureId worldDepth) {
    const int bw = viewW > 960 ? viewW / 2 : viewW, bh = viewW > 960 ? viewH / 2 : viewH;
    if (!beamTarget_ || beamW_ != bw || beamH_ != bh) {
        if (beamTarget_) device_.destroyTarget(beamTarget_);
        TargetDesc d;
        d.width = bw, d.height = bh, d.format = TargetFormat::RGBA16F;
        std::string error;
        beamTarget_ = device_.createTarget(d, error);
        beamW_ = bw, beamH_ = bh;
        if (!beamTarget_) {
            SDL_Log("spotlights: no beam buffer: %s", error.c_str());
            return;
        }
    }
    const size_t bytes = beamPx_.size() * sizeof(float);
    if (bytes > beamVbSize_) {
        if (beamVb_) device_.destroyBuffer(beamVb_);
        beamVbSize_ = std::max<size_t>(bytes * 2, 16 * 1024);
        beamVb_ = device_.createBuffer(BufferKind::Vertex, beamVbSize_, nullptr, true);
    }
    device_.updateBuffer(beamVb_, 0, beamPx_.data(), bytes);
    device_.setTarget(beamTarget_);
    device_.setViewport(0, 0, bw, bh);
    const float zero[4] = {0, 0, 0, 0};
    device_.clear(zero, false);
    const bool zeroToOne = device_.info().clipDepthZeroToOne;
    const Mat4 vp = camera.viewProj(zeroToOne);
    BeamBlock bb;
    std::copy(vp.m, vp.m + 16, bb.viewProj);
    bb.depth[0] = camera.nearPlane, bb.depth[1] = camera.farPlane, bb.depth[2] = zeroToOne ? 1.0f : 0.0f, bb.depth[3] = 1.0f;
    DrawCall c;
    c.program = beamProgram_;
    c.state.blend = true;
    c.state.src = BlendFactor::One, c.state.dst = BlendFactor::One;
    c.layout = &beamLayout();
    c.vertices = beamVb_;
    c.count = int(beamPx_.size() / 12);
    c.textures[0] = worldDepth;
    c.samplers[0] = {Filter::Nearest, Wrap::Clamp, 0};
    c.uniforms[0] = UniformData{&bb, sizeof bb, 0};
    device_.draw(c);
    device_.setTarget(worldTarget);
    device_.setViewport(0, 0, viewW, viewH);
    BeamCompBlock cb{};
    cb.gain[0] = kBeamGain * (hdr ? 0.25f : 1.0f);
    DrawCall k;
    k.program = beamCompProgram_;
    k.state.blend = true;
    k.state.src = BlendFactor::One, k.state.dst = BlendFactor::One;
    k.layout = &triangleLayout();
    k.vertices = triangle_;
    k.count = 3;
    k.textures[0] = device_.targetTexture(beamTarget_);
    k.samplers[0] = {Filter::Linear, Wrap::Clamp, 0};
    k.uniforms[0] = UniformData{&cb, sizeof cb, 0};
    device_.draw(k);
}

void SpotlightView::drawFlares(const Camera& camera, int viewW, int viewH, bool hdr, TargetId worldTarget, TextureId worldDepth) {
    if (flareList_.empty() || !flareProgram_) return;
    const bool zeroToOne = device_.info().clipDepthZeroToOne;
    const Mat4 vp = camera.viewProj(zeroToOne);
    // The visibility pass (needs the world's depth: post-processing on).
    const bool test = worldTarget && worldDepth && visProgram_ && vis_[0] && vis_[1];
    if (test) {
        if (!visCleared_) {
            const float black[4] = {0, 0, 0, 1};
            for (TargetId t : vis_) {
                device_.setTarget(t);
                device_.clear(black, false);
            }
            visCleared_ = true;
        }
        static VisBlock vb;
        std::fill(&vb.flares[0][0], &vb.flares[0][0] + kMaxFlares * 4, 0.0f);
        std::fill(&vb.steps[0][0], &vb.steps[0][0] + kMaxFlares * 4, 0.0f);
        for (const Flare& f : flareList_) {
            const Vec4 c = mul(vp, f.pos);
            auto* e = vb.flares[f.slot];
            if (c.w > 0.01f) {
                const float nx = c.x / c.w, ny = c.y / c.w, nz = c.z / c.w;
                e[0] = nx * 0.5f + 0.5f, e[1] = ny * 0.5f + 0.5f;
                e[2] = zeroToOne ? nz : nz * 0.5f + 0.5f;
                vb.steps[f.slot][1] = std::fabs(nx) <= 1 && std::fabs(ny) <= 1 ? 1.0f : 0.0f;
            }
            e[3] = f.test ? 1.0f : 0.0f;
            vb.steps[f.slot][0] = 1.0f / float(f.steps);
        }
        vb.texel[0] = 1.0f / float(std::max(1, viewW)), vb.texel[1] = 1.0f / float(std::max(1, viewH));
        const int next = 1 - visCurrent_;
        device_.setTarget(vis_[next]);
        device_.setViewport(0, 0, kMaxFlares, 1);
        DrawCall c;
        c.program = visProgram_;
        c.state.alphaWrite = true;
        c.layout = &triangleLayout();
        c.vertices = triangle_;
        c.count = 3;
        c.textures[0] = device_.targetTexture(vis_[visCurrent_]);
        c.samplers[0] = {Filter::Nearest, Wrap::Clamp, 0};
        c.textures[1] = worldDepth;
        c.samplers[1] = {Filter::Nearest, Wrap::Clamp, 0};
        c.uniforms[0] = UniformData{&vb, sizeof vb, 0};
        device_.draw(c);
        visCurrent_ = next;
        device_.setTarget(worldTarget);
        device_.setViewport(0, 0, viewW, viewH);
    }
    // The flares, one draw per texture.
    std::vector<float> v;
    std::vector<std::pair<TextureId, std::pair<int, int>>> runs;
    std::stable_sort(flareList_.begin(), flareList_.end(), [](const Flare& a, const Flare& b) { return a.tex.id < b.tex.id; });
    const float aspect = float(std::max(1, viewW)) / float(std::max(1, viewH));
    for (const Flare& f : flareList_) {
        if (runs.empty() || runs.back().first.id != f.tex.id) runs.push_back({f.tex, {int(v.size() / 12), 0}});
        const float hw = f.size, hh = f.size * aspect;
        const float corners[6][4] = {{-1, -1, 0, 1}, {1, -1, 1, 1}, {1, 1, 1, 0}, {-1, -1, 0, 1}, {1, 1, 1, 0}, {-1, 1, 0, 0}};
        for (const auto& k : corners)
            v.insert(v.end(), {f.pos.x, f.pos.y, f.pos.z, k[0] * hw, k[1] * hh, k[2], k[3], f.rgba[0], f.rgba[1], f.rgba[2], f.rgba[3], float(f.slot)});
        runs.back().second.second += 6;
        ++flares_;
    }
    const size_t bytes = v.size() * sizeof(float);
    if (bytes > flareVbSize_) {
        if (flareVb_) device_.destroyBuffer(flareVb_);
        flareVbSize_ = std::max<size_t>(bytes * 2, 16 * 1024);
        flareVb_ = device_.createBuffer(BufferKind::Vertex, flareVbSize_, nullptr, true);
    }
    device_.updateBuffer(flareVb_, 0, v.data(), bytes);
    FlareBlock fb;
    std::copy(vp.m, vp.m + 16, fb.viewProj);
    fb.params[0] = hdr ? 0.25f : 1.0f;
    fb.params[1] = test ? 1.0f : 0.0f;
    fb.params[2] = fb.params[3] = 0;
    for (const auto& [tex, range] : runs) {
        if (!tex) continue;
        DrawCall c;
        c.program = flareProgram_;
        c.state.blend = true;
        c.state.src = BlendFactor::One, c.state.dst = BlendFactor::One;
        c.layout = &flareLayout();
        c.vertices = flareVb_;
        c.first = range.first, c.count = range.second;
        c.textures[0] = tex;
        c.samplers[0] = {Filter::Linear, Wrap::Clamp, 0};
        c.textures[1] = test ? device_.targetTexture(vis_[visCurrent_]) : tex;
        c.samplers[1] = {Filter::Nearest, Wrap::Clamp, 0};
        c.uniforms[0] = UniformData{&fb, sizeof fb, 0};
        device_.draw(c);
    }
}

}  // namespace gfx
