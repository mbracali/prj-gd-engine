// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The port's post chain (render/enhanced_fx.h): gd01's shaders
// (gd-game-01/engine/src/render/post_fx.cpp) in the GLSL subset of gd02's
// device (330 core and 300 es), their uniforms in one std140 block.
#include "render/enhanced_fx.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gfx {
namespace {

// The passes' shared constants (std140: mat4 and vec4 members only).
struct EnhBlock {
    float invViewProj[16];
    float viewProj[16];
    float prevViewProj[16];
    float texel[4];   // xy the source's texel, zw a second texel (the streaks' source)
    float size[4];    // xy the source's size, zw the output's
    float fx[4];      // x bloom, y streaks, z cinematic, w grade
    float fog[4];     // x mode, y floor z, z height, w density
    float fog2[4];    // x max, y time, z ssao, w ao radius
    float pass[4];    // per pass (star blur, TAA, finish, RCAS, downsample)
    float lightPos[8][4];  // [0].w: the number of lights
    float lightDir[8][4];
    float lightCol[8][4];
    float lightCone[8][4];
};

const char* kVertex = R"(
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragment = R"(
in vec2 vUv;
out vec4 fragColor;
layout(std140) uniform GdEnh {
    mat4 uInvViewProj;
    mat4 uViewProj;
    mat4 uPrevViewProj;
    vec4 uTexel;
    vec4 uSize;
    vec4 uFx;
    vec4 uFog;
    vec4 uFog2;
    vec4 uPass;
    vec4 uLightPos[8];
    vec4 uLightDir[8];
    vec4 uLightCol[8];
    vec4 uLightCone[8];
};
vec3 world(vec2 uv, float d) {
    vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

#if PASS_DOWN
// Dual Kawase down: 5 taps around the destination texel (Marius Bjorge, SIGGRAPH 2015).
uniform sampler2D sSrc;
void main() {
    vec2 t = uTexel.xy;
    vec3 c = texture(sSrc, vUv).rgb * 4.0;
    c += texture(sSrc, vUv - t).rgb;
    c += texture(sSrc, vUv + t).rgb;
    c += texture(sSrc, vUv + vec2(t.x, -t.y)).rgb;
    c += texture(sSrc, vUv - vec2(t.x, -t.y)).rgb;
    fragColor = vec4(c / 8.0, 1.0);
}
#elif PASS_UP
// Dual Kawase up: 8 taps from the smaller level, plus the same-size level of the down chain.
uniform sampler2D sSrc;
uniform sampler2D sSkip;
void main() {
    vec2 h = uTexel.xy * 0.5;
    vec3 c = texture(sSrc, vUv + vec2(-h.x * 2.0, 0.0)).rgb;
    c += texture(sSrc, vUv + vec2(-h.x, h.y)).rgb * 2.0;
    c += texture(sSrc, vUv + vec2(0.0, h.y * 2.0)).rgb;
    c += texture(sSrc, vUv + vec2(h.x, h.y)).rgb * 2.0;
    c += texture(sSrc, vUv + vec2(h.x * 2.0, 0.0)).rgb;
    c += texture(sSrc, vUv + vec2(h.x, -h.y)).rgb * 2.0;
    c += texture(sSrc, vUv + vec2(0.0, -h.y * 2.0)).rgb;
    c += texture(sSrc, vUv + vec2(-h.x, -h.y)).rgb * 2.0;
    fragColor = vec4(mix(c / 12.0, texture(sSkip, vUv).rgb, 0.3), 1.0);
}
#elif PASS_AO
// Ambient occlusion: a world-space hemisphere of 12 samples around each
// pixel's surface (normal from the nearer depth neighbours), tested against
// the depth buffer, with a range check. Out: occlusion (r), eye distance (g).
uniform sampler2D sDepth;
vec3 at(vec2 uv) { return world(uv, texture(sDepth, uv).r); }
void main() {
    float d = texture(sDepth, vUv).r;
    vec4 e = uInvViewProj * vec4(0.0, 0.0, -1.0, 1.0);
    vec3 eye = e.xyz / e.w;
    if (d >= 0.99999) {
        fragColor = vec4(1.0, 1.0, 0.0, 1.0);
        return;
    }
    vec3 p = world(vUv, d);
    vec3 l = at(vUv - vec2(uTexel.x, 0.0)), r = at(vUv + vec2(uTexel.x, 0.0));
    vec3 dn = at(vUv - vec2(0.0, uTexel.y)), up = at(vUv + vec2(0.0, uTexel.y));
    vec3 dx = length(r - p) < length(p - l) ? r - p : p - l;
    vec3 dy = length(up - p) < length(p - dn) ? up - p : p - dn;
    vec3 n = normalize(cross(dx, dy));
    if (dot(n, eye - p) < 0.0) n = -n;
    vec3 t = normalize(cross(n, abs(n.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(n, t);
    float rot = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;
    float radius = uFog2.w;
    float occ = 0.0;
    for (int i = 0; i < 12; ++i) {
        float fi = (float(i) + 0.5) / 12.0;
        float a = float(i) * 2.39996 + rot;
        float z = mix(0.15, 1.0, fi), rr = sqrt(1.0 - z * z);
        vec3 k = (t * cos(a) * rr + b * sin(a) * rr + n * z) * radius * mix(0.2, 1.0, fi * fi);
        vec4 cs = uViewProj * vec4(p + k, 1.0);
        if (cs.w <= 0.0) continue;
        vec3 ndc = cs.xyz / cs.w;
        vec2 suv = ndc.xy * 0.5 + 0.5;
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
        float sd = texture(sDepth, suv).r;
        if (sd < ndc.z * 0.5 + 0.5 - 1e-5) {
            float dist = length(world(suv, sd) - p);
            occ += 1.0 - smoothstep(radius * 0.8, radius * 2.0, dist);
        }
    }
    // The eye distance in thousands (an 8-bit target keeps it for the blur's weights).
    fragColor = vec4(1.0 - 0.85 * occ / 12.0, clamp(length(eye - p) / 4000.0, 0.0, 1.0), 0.0, 1.0);
}
#elif PASS_FOGVOL
// Super Fog (half resolution): the view ray marched through a ground fog
// layer and a thin haze, stirred by animated 3D noise, lit by the stage's
// light cones (forward scattering) and the frame's own light. Out: the
// in-scattered light and the transmittance.
uniform sampler2D sDepth;
uniform sampler2D sBlurLo;
// Not "noise3": GLSL reserves noise1..noise4 (lesson L5).
float hash3(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float fogNoise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), u.x), mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), u.x), u.y),
               mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), u.x), mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), u.x), u.y),
               u.z);
}
void main() {
    float d = min(texture(sDepth, vUv).r, 0.99999);
    vec3 o = world(vUv, 0.0), p = world(vUv, d);
    vec3 ray = p - o;
    float dist = min(length(ray), 2500.0);
    vec3 dir = normalize(ray);
    float stepLen = dist / 24.0;
    float t = stepLen * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float time = uFog2.y, floorZ = uFog.y;
    vec3 ambient = vec3(0.07, 0.07, 0.085) + texture(sBlurLo, vUv).rgb * 1.5;
    vec3 inScatter = vec3(0.0);
    float trans = 1.0;
    const float g = 0.55;
    int lights = int(uLightPos[0].w + 0.5);
    for (int i = 0; i < 24; ++i) {
        vec3 q = o + dir * t;
        float h = max(q.z - floorZ, 0.0);
        vec3 nq = q * 0.011 + vec3(time * 0.05, time * 0.02, -time * 0.03);
        float n = h < 80.0 ? fogNoise3(nq) * 0.6 + fogNoise3(nq * 2.6 + 7.3) * 0.4 : fogNoise3(nq);
        float dens = 0.009 * exp(-h / 26.0) * mix(0.25, 1.9, n) + 0.00055 * mix(0.6, 1.4, n);
        vec3 li = ambient;
        float near = smoothstep(40.0, 220.0, t);
        for (int k = 0; k < 8; ++k) {
            if (k >= lights) break;
            vec3 v = q - uLightPos[k].xyz;
            float dl = length(v);
            vec3 lv = v / max(dl, 1e-3);
            float cone = smoothstep(uLightCone[k].y, uLightCone[k].x, dot(lv, uLightDir[k].xyz));
            if (cone <= 0.0) continue;
            float mu = dot(lv, -dir);
            float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * mu, 1.5) * 0.25;
            li += uLightCol[k].rgb * cone * phase * 40.0 * near / (1.0 + dl * dl * 1.5e-5);
        }
        float sigma = dens * stepLen;
        inScatter += trans * li * sigma;
        trans *= exp(-sigma);
        t += stepLen;
    }
    inScatter = inScatter / (1.0 + max(max(inScatter.r, inScatter.g), inScatter.b) * 0.7);
    fragColor = vec4(inScatter, max(trans, 0.3));
}
#elif PASS_COMPOSITE
// Ambient occlusion, fog, bloom, light streaks and colour in one pass.
uniform sampler2D sTex;
uniform sampler2D sDepth;
uniform sampler2D sBlur;       // the pyramid's top (1/2)
uniform sampler2D sBlurMid;    // 1/4
uniform sampler2D sStreakSrc;  // the first 1/2 step down
uniform sampler2D sBlurLo;     // 1/8: the fog's light
uniform sampler2D sAo;
uniform sampler2D sFogVol;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
vec3 saturation(vec3 c, float s) { return mix(vec3(luma(c)), c, s); }
vec3 grade(vec3 c, int g) {
    float l = luma(c);
    if (g == 1) {         // warm: golden highs, amber shadows
        c *= vec3(1.08, 1.0, 0.86);
        c += vec3(0.03, 0.012, -0.01) * (1.0 - l);
    } else if (g == 2) {  // cold: steel blue, teal shadows
        c *= vec3(0.88, 0.99, 1.12);
        c += vec3(-0.01, 0.015, 0.03) * (1.0 - l);
    } else if (g == 3) {  // vintage: faded film, lifted blacks, sepia
        c = saturation(c, 0.7);
        c = c * vec3(1.0, 0.93, 0.78) * 0.88 + vec3(0.07, 0.055, 0.04);
    } else if (g == 4) {  // noir: high-contrast black and white
        float m = clamp((pow(l, 0.75) - 0.3) * 1.3 + 0.3, 0.0, 1.0);
        c = vec3(mix(m, m * m * (3.0 - 2.0 * m), 0.5)) * vec3(0.97, 0.99, 1.04);
    } else if (g == 5) {  // neon: punchy saturation, purple shadows, cyan highs
        c = saturation(c, 1.4);
        c += mix(vec3(0.05, -0.01, 0.07), vec3(-0.02, 0.03, 0.04), smoothstep(0.2, 0.8, l)) * 0.8;
        c = mix(c, c * c * (3.0 - 2.0 * c), 0.25);
    } else if (g == 6) {  // sepia: old photographs (the "Good old days" mode)
        c = vec3(l) * vec3(1.07, 0.9, 0.68) + vec3(0.04, 0.025, 0.0);
    }
    return c;
}
void main() {
    vec3 c = texture(sTex, vUv).rgb;
    int fog = int(uFog.x + 0.5);
    if (uFog2.z > 0.0) {
        // A 3x3 blur of the occlusion that keeps to surfaces at the same distance.
        vec2 a0 = texture(sAo, vUv).rg;
        float sum = 0.0, wsum = 0.0;
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) {
                vec2 a = texture(sAo, vUv + vec2(i, j) * uTexel.xy * 1.5).rg;
                float w = 1.0 / (1.0 + abs(a.y - a0.y) * 1000.0);
                sum += a.x * w, wsum += w;
            }
        c *= pow(sum / wsum, uFog2.z);  // the occlusion's exponent: light 0.5 .. stronger 1.5 (gd01's)
    }
    if (fog == 3) {
        vec4 v = texture(sFogVol, vUv);
        c = c * v.a + v.rgb;
    } else if (fog != 0) {
        // Exponential height fog on the stage floor along the view ray.
        float d = min(texture(sDepth, vUv).r, 0.99999);
        vec3 o = world(vUv, 0.0), p = world(vUv, d);
        vec3 ray = p - o;
        float dist = min(length(ray), 3000.0);
        p = o + normalize(ray) * dist;
        float h0 = max(o.z - uFog.y, 0.0) / uFog.z, h1 = max(p.z - uFog.y, 0.0) / uFog.z;
        float k = abs(h1 - h0) < 1e-3 ? exp(-h0) : (exp(-h0) - exp(-h1)) / (h1 - h0);
        float time = uFog2.y;
        vec2 q = p.xy * 0.012 + time * vec2(0.035, 0.012);
        vec2 warp = vec2(vnoise(q * 0.7 + 3.1), vnoise(q * 0.7 - 1.7)) - 0.5;
        q += warp * 1.4;
        float n = vnoise(q) * 0.55 + vnoise(q * 2.3 - time * vec2(0.02, 0.05)) * 0.3 + vnoise(q * 5.1 + time * 0.04) * 0.15;
        float amount = uFog.w * dist * k * mix(0.35, 1.7, n);
        float f = (1.0 - exp(-amount)) * uFog2.x;
        vec3 light = texture(sBlurLo, vUv).rgb;
        vec3 glow = max(texture(sBlurMid, vUv).rgb - 0.35, 0.0);
        c = mix(c, vec3(0.09, 0.09, 0.11) + light * 1.3 + glow * 1.2, f);
    }
    if (uFx.x > 0.0) {
        // The bright parts of the blurred frame, on luminance with a soft knee.
        vec3 b = texture(sBlur, vUv).rgb;
        vec3 wide = texture(sBlurLo, vUv).rgb;
        float lb = luma(b), lw = luma(wide);
        float kb = smoothstep(0.62, 0.9, lb) * max(lb - 0.62, 0.0) / max(lb, 1e-3);
        float kw = smoothstep(0.55, 0.85, lw) * max(lw - 0.55, 0.0) / max(lw, 1e-3);
        c += (b * kb * 2.2 + wide * kw * 1.6) * uFx.x;
    }
    if (uFx.y > 0.0) {
        // Anamorphic streaks: the brightest lights smeared sideways, blue-tinted.
        vec3 s = vec3(0.0);
        for (int i = -24; i <= 24; ++i) {
            vec3 t = texture(sStreakSrc, vUv + vec2(float(i) * uTexel.z, 0.0)).rgb;
            float m = max(t.r, max(t.g, t.b));
            s += t * (max(m - 0.8, 0.0) / max(m, 1e-3)) * exp(-abs(float(i)) * 0.09);
        }
        c += (s * 0.5 + vec3(luma(s)) * vec3(0.55, 0.8, 1.3) * 0.7) * uFx.y;
    }
    if (uFx.z > 0.5) {
        float l = luma(c);
        c = mix(vec3(l), c, 1.12);
        c = clamp(c, 0.0, 1.0);
        c = mix(c, c * c * (3.0 - 2.0 * c), 0.4);
        c += vec3(0.025, 0.006, -0.02) * l - vec3(0.012, -0.004, -0.022) * (1.0 - l) * 0.6;
        float v = length((vUv - 0.5) * vec2(1.25, 1.0));
        c *= mix(1.0, 0.7, smoothstep(0.35, 0.9, v));
    }
    int g = int(uFx.w + 0.5);
    if (g != 0) c = grade(clamp(c, 0.0, 1.0), g);
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
#elif PASS_STARBLUR
// Blur on Star Power: the frame smeared along the camera's motion (the depth
// buffer reprojected into the last frame) and a slight zoom, blended with
// the last frames.
uniform sampler2D sTex;
uniform sampler2D sDepth;
uniform sampler2D sAccum;
void main() {
    float d = min(texture(sDepth, vUv).r, 0.99999);
    vec4 p = uInvViewProj * vec4(vUv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 prev = uPrevViewProj * vec4(p.xyz / p.w, 1.0);
    bool valid = uPass.w > 0.5;
    vec2 vel = valid && prev.w > 0.0 ? vUv - (prev.xy / prev.w * 0.5 + 0.5) : vec2(0.0);
    vel *= uPass.x;
    float len = length(vel);
    if (len > 0.08) vel *= 0.08 / len;
    vec2 v = vel + (vUv - 0.5) * uPass.y;
    vec3 c = vec3(0.0);
    for (int i = 0; i < 12; ++i) c += texture(sTex, vUv - v * (float(i) / 11.0 - 0.5)).rgb;
    c /= 12.0;
    if (valid) c = mix(c, texture(sAccum, vUv).rgb, uPass.z);
    fragColor = vec4(c, 1.0);
}
#elif PASS_TAA
// TAA: the history reprojected through the depth buffer, clamped to the
// current 3x3 neighbourhood in YCoCg (variance clipping).
uniform sampler2D sTex;
uniform sampler2D sDepth;
uniform sampler2D sHistory;
vec3 toYCoCg(vec3 c) { return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25))); }
vec3 toRgb(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
void main() {
    vec3 cur = texture(sTex, vUv + uPass.xy).rgb;  // un-jittered
    if (uPass.z > 0.5) {
        fragColor = vec4(cur, 1.0);
        return;
    }
    vec3 mn = vec3(1e9), mx = vec3(-1e9), m1 = vec3(0.0), m2 = vec3(0.0);
    float nearest = 1.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            vec2 uv = vUv + vec2(i, j) * uTexel.xy;
            vec3 s = toYCoCg(texture(sTex, uv).rgb);
            mn = min(mn, s), mx = max(mx, s);
            m1 += s, m2 += s * s;
            nearest = min(nearest, texture(sDepth, uv).r);
        }
    vec3 mean = m1 / 9.0, sd = sqrt(max(m2 / 9.0 - mean * mean, 0.0));
    mn = max(mn, mean - sd * 1.25), mx = min(mx, mean + sd * 1.25);
    vec4 p = uInvViewProj * vec4(vUv * 2.0 - 1.0, nearest * 2.0 - 1.0, 1.0);
    vec4 prev = uPrevViewProj * vec4(p.xyz / p.w, 1.0);
    vec2 prevUv = prev.xy / prev.w * 0.5 + 0.5;
    if (any(lessThan(prevUv, vec2(0.0))) || any(greaterThan(prevUv, vec2(1.0)))) {
        fragColor = vec4(cur, 1.0);
        return;
    }
    vec3 hist = clamp(toYCoCg(texture(sHistory, prevUv).rgb), mn, mx);
    float motion = length((prevUv - vUv) / uTexel.xy);
    float alpha = mix(0.1, 0.35, clamp(motion / 8.0, 0.0, 1.0));
    fragColor = vec4(toRgb(mix(hist, toYCoCg(cur), alpha)), 1.0);
}
#elif PASS_FXAA
// FXAA 3.11 (Timothy Lottes), the PC quality preset 12 search.
uniform sampler2D sTex;
float lumaAt(vec2 uv) { return dot(texture(sTex, uv).rgb, vec3(0.299, 0.587, 0.114)); }
void main() {
    vec2 tx = uTexel.xy;
    vec3 rgbM = texture(sTex, vUv).rgb;
    float lM = dot(rgbM, vec3(0.299, 0.587, 0.114));
    float lN = lumaAt(vUv + vec2(0, -tx.y)), lS = lumaAt(vUv + vec2(0, tx.y));
    float lE = lumaAt(vUv + vec2(tx.x, 0)), lW = lumaAt(vUv + vec2(-tx.x, 0));
    float rangeMax = max(max(lN, lS), max(lE, max(lW, lM))), rangeMin = min(min(lN, lS), min(lE, min(lW, lM)));
    float range = rangeMax - rangeMin;
    if (range < max(0.0312, rangeMax * 0.125)) {
        fragColor = vec4(rgbM, 1.0);
        return;
    }
    float lNW = lumaAt(vUv - tx), lSE = lumaAt(vUv + tx);
    float lNE = lumaAt(vUv + vec2(tx.x, -tx.y)), lSW = lumaAt(vUv + vec2(-tx.x, tx.y));
    float lNS = lN + lS, lWE = lW + lE;
    float edgeH = abs(-2.0 * lW + lNW + lSW) + abs(-2.0 * lM + lNS) * 2.0 + abs(-2.0 * lE + lNE + lSE);
    float edgeV = abs(-2.0 * lS + lSW + lSE) + abs(-2.0 * lM + lWE) * 2.0 + abs(-2.0 * lN + lNW + lNE);
    bool horz = edgeH >= edgeV;
    float subA = (lNS + lWE) * 2.0 + (lNW + lSW + lNE + lSE);
    float l1 = horz ? lN : lW, l2 = horz ? lS : lE;
    float g1 = abs(l1 - lM), g2 = abs(l2 - lM);
    bool pair1 = g1 >= g2;
    float gScaled = max(g1, g2) * 0.25;
    float stepLen = horz ? tx.y : tx.x;
    if (pair1) stepLen = -stepLen;
    float lLocal = pair1 ? (l1 + lM) * 0.5 : (l2 + lM) * 0.5;
    vec2 uv = vUv;
    if (horz) uv.y += stepLen * 0.5; else uv.x += stepLen * 0.5;
    vec2 off = horz ? vec2(tx.x, 0) : vec2(0, tx.y);
    vec2 uvN = uv - off, uvP = uv + off;
    float eN = lumaAt(uvN) - lLocal, eP = lumaAt(uvP) - lLocal;
    bool doneN = abs(eN) >= gScaled, doneP = abs(eP) >= gScaled;
    const float kSteps[10] = float[10](1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0, 8.0, 8.0);
    for (int i = 0; i < 10; ++i) {
        if (doneN && doneP) break;
        if (!doneN) uvN -= off * kSteps[i], eN = lumaAt(uvN) - lLocal, doneN = abs(eN) >= gScaled;
        if (!doneP) uvP += off * kSteps[i], eP = lumaAt(uvP) - lLocal, doneP = abs(eP) >= gScaled;
    }
    float dN = horz ? vUv.x - uvN.x : vUv.y - uvN.y, dP = horz ? uvP.x - vUv.x : uvP.y - vUv.y;
    bool nearN = dN < dP;
    float dist = min(dN, dP), span = dN + dP;
    bool lMSmaller = lM - lLocal < 0.0;
    bool goodSpan = ((nearN ? eN : eP) < 0.0) != lMSmaller;
    float pixelOff = goodSpan ? -dist / span + 0.5 : 0.0;
    float sub = clamp(abs(subA / 12.0 - lM) / range, 0.0, 1.0);
    sub = (-2.0 * sub + 3.0) * sub * sub;
    pixelOff = max(pixelOff, sub * sub * 0.75);
    vec2 fuv = vUv;
    if (horz) fuv.y += pixelOff * stepLen; else fuv.x += pixelOff * stepLen;
    fragColor = vec4(texture(sTex, fuv).rgb, 1.0);
}
#elif PASS_DOWNSAMPLE
// SSAA: each output pixel averages its 2x2 source pixels with four bilinear taps.
uniform sampler2D sTex;
void main() {
    vec2 o = uTexel.xy * uPass.xy * 0.25;
    vec3 c = texture(sTex, vUv + vec2(-o.x, -o.y)).rgb + texture(sTex, vUv + vec2(o.x, -o.y)).rgb +
             texture(sTex, vUv + vec2(-o.x, o.y)).rgb + texture(sTex, vUv + vec2(o.x, o.y)).rgb;
    fragColor = vec4(c * 0.25, 1.0);
}
#elif PASS_EASU
// FSR 1 EASU (Edge Adaptive Spatial Upsampling), after AMD FidelityFX
// ffx_fsr1.h (MIT): 12 taps, the local edge direction and length, an
// approximated Lanczos-2 kernel stretched along the edge, deringing.
uniform sampler2D sTex;
vec3 tap(vec2 p) { return texture(sTex, (p + 0.5) / uSize.xy).rgb; }
float lum(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }
void easuSet(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
    float dc = lD - lC, cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = lenX > 0.0 ? 1.0 / lenX : 0.0;
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    len += lenX * lenX * w;
    float ec = lE - lC, ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = lenY > 0.0 ? 1.0 / lenY : 0.0;
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    len += lenY * lenY * w;
}
void easuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;
    float d2 = min(dot(v, v), clp);
    float wB = 2.0 / 5.0 * d2 - 1.0, wA = lob * d2 - 1.0;
    wB *= wB, wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w, aW += w;
}
void main() {
    vec2 pp = vUv * uSize.xy - 0.5;
    vec2 fp = floor(pp);
    pp -= fp;
    vec3 b = tap(fp + vec2(0, -1)), c = tap(fp + vec2(1, -1));
    vec3 e = tap(fp + vec2(-1, 0)), f = tap(fp), g = tap(fp + vec2(1, 0)), h = tap(fp + vec2(2, 0));
    vec3 i = tap(fp + vec2(-1, 1)), j = tap(fp + vec2(0, 1)), k = tap(fp + vec2(1, 1)), l = tap(fp + vec2(2, 1));
    vec3 n = tap(fp + vec2(0, 2)), o = tap(fp + vec2(1, 2));
    float bL = lum(b), cL = lum(c), eL = lum(e), fL = lum(f), gL = lum(g), hL = lum(h);
    float iL = lum(i), jL = lum(j), kL = lum(k), lL = lum(l), nL = lum(n), oL = lum(o);
    vec2 dir = vec2(0.0);
    float len = 0.0;
    easuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    easuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    easuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    easuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
    float dirR = dot(dir, dir);
    bool zro = dirR < 1.0 / 32768.0;
    dirR = zro ? 1.0 : inversesqrt(dirR);
    dir.x = zro ? 1.0 : dir.x;
    dir *= dirR;
    len = len * 0.5;
    len *= len;
    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;
    vec3 mn = min(min(f, g), min(j, k)), mx = max(max(f, g), max(j, k));
    vec3 aC = vec3(0.0);
    float aW = 0.0;
    easuTap(aC, aW, vec2(0, -1) - pp, dir, len2, lob, clp, b);
    easuTap(aC, aW, vec2(1, -1) - pp, dir, len2, lob, clp, c);
    easuTap(aC, aW, vec2(-1, 1) - pp, dir, len2, lob, clp, i);
    easuTap(aC, aW, vec2(0, 1) - pp, dir, len2, lob, clp, j);
    easuTap(aC, aW, vec2(0, 0) - pp, dir, len2, lob, clp, f);
    easuTap(aC, aW, vec2(-1, 0) - pp, dir, len2, lob, clp, e);
    easuTap(aC, aW, vec2(1, 1) - pp, dir, len2, lob, clp, k);
    easuTap(aC, aW, vec2(2, 1) - pp, dir, len2, lob, clp, l);
    easuTap(aC, aW, vec2(2, 0) - pp, dir, len2, lob, clp, h);
    easuTap(aC, aW, vec2(1, 0) - pp, dir, len2, lob, clp, g);
    easuTap(aC, aW, vec2(1, 2) - pp, dir, len2, lob, clp, o);
    easuTap(aC, aW, vec2(0, 2) - pp, dir, len2, lob, clp, n);
    fragColor = vec4(min(mx, max(mn, aC / aW)), 1.0);
}
#elif PASS_RCAS
// FSR 1 RCAS: a 5-tap cross whose negative lobe never leaves the local range.
uniform sampler2D sTex;
void main() {
    vec2 tx = uTexel.xy;
    vec3 b = texture(sTex, vUv + vec2(0, -tx.y)).rgb, d = texture(sTex, vUv + vec2(-tx.x, 0)).rgb;
    vec3 e = texture(sTex, vUv).rgb;
    vec3 f = texture(sTex, vUv + vec2(tx.x, 0)).rgb, h = texture(sTex, vUv + vec2(0, tx.y)).rgb;
    vec3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));
    vec3 hitMin = mn4 / max(4.0 * mx4, 1e-4);
    vec3 hitMax = (1.0 - mx4) / min(4.0 * mn4 - 4.0, -1e-4);
    vec3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * uPass.x;
    fragColor = vec4((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0), 1.0);
}
#elif PASS_FINISH
// At the picture's size: contrast-adaptive sharpening (after AMD CAS),
// chromatic aberration and animated film grain (strongest in mid-tones).
uniform sampler2D sTex;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec2 tx = uTexel.xy;
    vec3 e = texture(sTex, vUv).rgb;
    vec3 c = e;
    float sharp = uPass.x, ca = uPass.y, grain = uPass.z, time = uPass.w;
    if (sharp > 0.0) {
        vec3 b = texture(sTex, vUv + vec2(0, -tx.y)).rgb, dd = texture(sTex, vUv + vec2(-tx.x, 0)).rgb;
        vec3 f = texture(sTex, vUv + vec2(tx.x, 0)).rgb, h = texture(sTex, vUv + vec2(0, tx.y)).rgb;
        vec3 mn = min(e, min(min(b, dd), min(f, h))), mx = max(e, max(max(b, dd), max(f, h)));
        vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-4), 0.0, 1.0));
        vec3 w = amp * (-1.0 / mix(8.0, 5.0, sharp));
        c = clamp((e + (b + dd + f + h) * w) / (1.0 + 4.0 * w), 0.0, 1.0);
    }
    if (ca > 0.0) {
        vec2 off = (vUv - 0.5) * ca * length(vUv - 0.5);
        c.r += texture(sTex, vUv + off).r - e.r;
        c.b += texture(sTex, vUv - off).b - e.b;
    }
    if (grain > 0.0) {
        float l = luma(c);
        vec2 px = floor(vUv * uSize.zw);
        float n = hash(px + fract(time * 7.31) * 413.0) + hash(px * 1.37 - fract(time * 3.17) * 211.0) - 1.0;
        c += n * grain * (0.35 + 2.6 * l * (1.0 - l));
    }
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
#else
uniform sampler2D sTex;
void main() { fragColor = vec4(texture(sTex, vUv).rgb, 1.0); }
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

void put(float* dst, const Mat4& m) { std::memcpy(dst, m.m, sizeof m.m); }

// A camera cut: the matrix jumps (reprojection only follows smooth moves).
bool cut(const Mat4& a, const Mat4& b) {
    float diff = 0, norm = 0;
    for (int k = 0; k < 16; ++k) diff += std::fabs(a.m[k] - b.m[k]), norm += std::fabs(a.m[k]);
    return diff > 0.15f * norm;
}

// The Halton (2, 3) sequence, 8 frames: TAA's sub-pixel offsets.
float halton(unsigned i, unsigned base) {
    float f = 1, r = 0;
    while (i > 0) {
        f /= float(base);
        r += f * float(i % base);
        i /= base;
    }
    return r;
}

}  // namespace

Mat4 inverse(const Mat4& a) {
    const float* m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    Mat4 r;
    if (std::fabs(det) < 1e-20f) return r;
    for (int i = 0; i < 16; ++i) r.m[i] = inv[i] / det;
    return r;
}

EnhancedFx::EnhancedFx(RenderDevice& device) : device_(device) {
    const float tri[6] = {-1, -1, 3, -1, -1, 3};
    triangle_ = device_.createBuffer(BufferKind::Vertex, sizeof tri, tri, false);
    if (!triangle_) failed_ = true;
}

EnhancedFx::~EnhancedFx() {
    for (Target* t : {&a_, &b_, &ao_, &fogVol_, &star_[0], &star_[1], &history_[0], &history_[1], &out_, &out2_})
        if (t->id) device_.destroyTarget(t->id);
    for (int l = 0; l < kLevels; ++l) {
        if (down_[l].id) device_.destroyTarget(down_[l].id);
        if (up_[l].id) device_.destroyTarget(up_[l].id);
    }
    for (ProgramId p : programs_)
        if (p) device_.destroyProgram(p);
    if (triangle_) device_.destroyBuffer(triangle_);
}

ProgramDesc EnhancedFx::describe(Pass p) {
    static const char* kNames[] = {"enh_down", "enh_up", "enh_ao", "enh_fogvol", "enh_composite", "enh_starblur", "enh_taa",
                                   "enh_fxaa", "enh_downsample", "enh_easu", "enh_rcas", "enh_finish", "enh_copy"};
    static const char* kDefines[] = {"PASS_DOWN", "PASS_UP", "PASS_AO", "PASS_FOGVOL", "PASS_COMPOSITE", "PASS_STARBLUR", "PASS_TAA",
                                     "PASS_FXAA", "PASS_DOWNSAMPLE", "PASS_EASU", "PASS_RCAS", "PASS_FINISH"};
    ProgramDesc d;
    d.name = kNames[int(p)];
    d.vertex = kVertex;
    d.fragment = kFragment;
    d.uniformBlocks = {"GdEnh"};
    // Every switch is defined (OpenGL ES rejects undefined names in #if).
    for (int i = 0; i < int(Pass::Copy); ++i) d.defines += std::string("#define ") + kDefines[i] + (i == int(p) ? " 1\n" : " 0\n");
    switch (p) {
    case Pass::Down: d.samplers = {"sSrc"}; break;
    case Pass::Up: d.samplers = {"sSrc", "sSkip"}; break;
    case Pass::Ao: d.samplers = {"sDepth"}; break;
    case Pass::FogVol: d.samplers = {"sDepth", "sBlurLo"}; break;
    case Pass::Composite: d.samplers = {"sTex", "sDepth", "sBlur", "sBlurMid", "sStreakSrc", "sBlurLo", "sAo", "sFogVol"}; break;
    case Pass::StarBlur: d.samplers = {"sTex", "sDepth", "sAccum"}; break;
    case Pass::Taa: d.samplers = {"sTex", "sDepth", "sHistory"}; break;
    default: d.samplers = {"sTex"}; break;
    }
    return d;
}

std::vector<ProgramDesc> EnhancedFx::allPrograms() {
    std::vector<ProgramDesc> all;
    for (int p = 0; p < int(Pass::Count); ++p) all.push_back(describe(Pass(p)));
    return all;
}

// A program, compiled on first use. One that fails turns the chain off
// (logged once): the world is shown as GH2's pass left it (lesson L5).
ProgramId EnhancedFx::program(Pass p) {
    const int i = int(p);
    if (!tried_[i]) {
        tried_[i] = true;
        std::string error;
        programs_[i] = device_.createProgram(describe(p), error);
        if (!programs_[i]) {
            SDL_Log("enhanced fx: %s does not compile: %s (the port's effects are off)", describe(p).name.c_str(), error.c_str());
            failed_ = true;
        }
    }
    return programs_[i];
}

TextureId EnhancedFx::ensure(Target& t, int w, int h, bool hdr) {
    w = std::max(1, w), h = std::max(1, h);
    if (t.id && t.w == w && t.h == h && t.hdr == hdr) return device_.targetTexture(t.id);
    if (t.id) device_.destroyTarget(t.id);
    std::string error;
    t.id = device_.createTarget(TargetDesc{w, h, false, hdr ? TargetFormat::RGBA16F : TargetFormat::RGBA8}, error);
    if (!t.id && hdr) t.id = device_.createTarget(TargetDesc{w, h, false, TargetFormat::RGBA8}, error);  // no float targets (some GLES)
    if (!t.id) {
        SDL_Log("enhanced fx: cannot make a %dx%d buffer (%s); the port's effects are off", w, h, error.c_str());
        failed_ = true;
        t = {};
        return {};
    }
    t.w = w, t.h = h, t.hdr = hdr;
    return device_.targetTexture(t.id);
}

void EnhancedFx::pass(Pass p, Target* dst, const TextureId* tex, int slots, const void* block, size_t size, const char* name) {
    const ProgramId prog = program(p);
    if (!prog || failed_) return;
    if (dst) device_.setTarget(dst->id);
    const SamplerState linear{Filter::Linear, Wrap::Clamp, 0}, point{Filter::Nearest, Wrap::Clamp, 0};
    const ProgramDesc d = describe(p);
    DrawCall c;
    c.program = prog;
    c.state.alphaWrite = dst != nullptr;
    c.layout = &triangleLayout();
    c.vertices = triangle_;
    c.count = 3;
    for (int i = 0; i < slots; ++i) {
        c.textures[i] = tex[i];
        c.samplers[i] = d.samplers[size_t(i)] == "sDepth" ? point : linear;  // depth: nearest only
    }
    c.uniforms[0] = UniformData{block, size, 0};
    device_.draw(c);
    lastPasses_.push_back(name);
}

std::array<float, 2> EnhancedFx::jitter(const PostSettings& s, int sw, int sh) {
    if (s.aa != PostSettings::AA::Taa || sw <= 0 || sh <= 0) return {0, 0};
    const unsigned i = (frame_ % 8) + 1;
    return {(halton(i, 2) - 0.5f) * 2.0f / float(sw), (halton(i, 3) - 0.5f) * 2.0f / float(sh)};
}

bool EnhancedFx::process(TextureId color, TextureId depth, int sw, int sh, int x, int y, int ow, int oh, const PostSettings& s,
                         const PostFrame& f) {
    lastPasses_.clear();
    if (failed_ || !color) return false;
    EnhBlock b{};
    const Mat4 inv = inverse(f.viewProj);
    put(b.invViewProj, inv);
    put(b.viewProj, f.viewProj);
    b.texel[0] = 1.0f / float(sw), b.texel[1] = 1.0f / float(sh);
    b.size[0] = float(sw), b.size[1] = float(sh), b.size[2] = float(ow), b.size[3] = float(oh);
    TextureId cur = color;

    // 1. The blur pyramid (dual Kawase) for bloom, the fog's light and the light streaks.
    const bool pyramid = s.bloom || s.streaks || (s.fog && f.hasCamera);
    if (pyramid) {
        int w = sw, h = sh;
        TextureId src = color;
        for (int l = 0; l < kLevels; ++l) {
            EnhBlock k = b;
            k.texel[0] = 0.5f / float(w), k.texel[1] = 0.5f / float(h);
            w = std::max(1, w / 2), h = std::max(1, h / 2);
            if (!ensure(down_[l], w, h, false)) return false;
            pass(Pass::Down, &down_[l], &src, 1, &k, sizeof k, "down");
            src = device_.targetTexture(down_[l].id);
        }
        for (int l = kLevels - 2; l >= 0; --l) {
            if (!ensure(up_[l], down_[l].w, down_[l].h, false)) return false;
            EnhBlock k = b;
            k.texel[0] = 1.0f / float(down_[l + 1].w), k.texel[1] = 1.0f / float(down_[l + 1].h);
            const TextureId t[2] = {src, device_.targetTexture(down_[l].id)};
            pass(Pass::Up, &up_[l], t, 2, &k, sizeof k, "up");
            src = device_.targetTexture(up_[l].id);
        }
    }
    // Ambient occlusion at render resolution.
    const bool ssao = s.ssao && f.hasCamera && depth;
    if (ssao) {
        if (!ensure(ao_, sw, sh, false)) return false;
        EnhBlock k = b;
        k.fog2[3] = 14.0f;  // world units; a rocker is ~80 tall (gd01's)
        pass(Pass::Ao, &ao_, &depth, 1, &k, sizeof k, "ao");
    }
    // Super Fog's volume at half resolution.
    const bool superFog = s.fog >= 3 && f.hasCamera && depth && pyramid;
    if (superFog) {
        if (!ensure(fogVol_, sw / 2, sh / 2, true)) return false;
        EnhBlock k = b;
        k.fog[1] = f.floorZ, k.fog2[1] = f.timeSec;
        int n = 0;
        for (const PostFrame::Light& l : f.lights) {
            if (n == 8) break;
            const Vec3 d = l.target - l.pos;
            const float len = std::sqrt(dot(d, d));
            if (len < 1) continue;
            k.lightPos[n][0] = l.pos.x, k.lightPos[n][1] = l.pos.y, k.lightPos[n][2] = l.pos.z;
            k.lightDir[n][0] = d.x / len, k.lightDir[n][1] = d.y / len, k.lightDir[n][2] = d.z / len;
            k.lightCol[n][0] = l.color.x, k.lightCol[n][1] = l.color.y, k.lightCol[n][2] = l.color.z;
            const float half = std::atan(60.0f / len);  // a pool ~60 units across where it lands
            k.lightCone[n][0] = std::cos(half * 0.55f), k.lightCone[n][1] = std::cos(half);
            ++n;
        }
        k.lightPos[0][3] = float(n);
        const TextureId t[2] = {depth, device_.targetTexture(up_[2].id)};
        pass(Pass::FogVol, &fogVol_, t, 2, &k, sizeof k, "fogvol");
    }

    // 2. Ambient occlusion, fog, bloom, light streaks, colour.
    static constexpr float kAoK[5] = {0, 0.5f, 0.8f, 1.1f, 1.5f};  // the occlusion's exponent: none, light, medium, strong, stronger
    if (pyramid || ssao || s.cinematic || s.grade) {
        if (!ensure(a_, sw, sh, false)) return false;
        EnhBlock k = b;
        const int fog = f.hasCamera && depth ? s.fog : 0;
        k.fx[0] = !pyramid ? 0.0f : s.bloom >= 3 ? 0.9f : s.bloom == 2 ? 0.68f : s.bloom == 1 ? 0.45f : 0.0f;
        static constexpr float kStreaks[4] = {0, 0.4f, 0.7f, 1.1f};  // none, very light, light, medium
        k.fx[1] = pyramid ? kStreaks[std::clamp(s.streaks, 0, 3)] : 0.0f;
        k.fx[2] = s.cinematic ? 1.0f : 0.0f;
        k.fx[3] = float(s.grade);
        // Low: knee-high wisps; full: a thicker layer to the waist (a rocker is ~80 units tall).
        k.fog[0] = float(superFog ? 3 : std::min(fog, 2));
        k.fog[1] = f.floorZ, k.fog[2] = fog >= 2 ? 24.0f : 13.0f, k.fog[3] = fog >= 2 ? 0.0065f : 0.004f;
        k.fog2[0] = fog >= 2 ? 0.88f : 0.68f, k.fog2[1] = f.timeSec, k.fog2[2] = ssao ? kAoK[std::clamp(s.ssao, 0, 4)] : 0.0f;
        if (pyramid) k.texel[2] = 1.0f / float(down_[0].w), k.texel[3] = 1.0f / float(down_[0].h);
        const TextureId t[8] = {color,
                                depth ? depth : color,
                                pyramid ? device_.targetTexture(up_[0].id) : color,
                                pyramid ? device_.targetTexture(up_[1].id) : color,
                                pyramid ? device_.targetTexture(down_[0].id) : color,
                                pyramid ? device_.targetTexture(up_[2].id) : color,
                                ssao ? device_.targetTexture(ao_.id) : color,
                                superFog ? device_.targetTexture(fogVol_.id) : color};
        pass(Pass::Composite, &a_, t, 8, &k, sizeof k, "composite");
        cur = device_.targetTexture(a_.id);
    }

    // 2b. Blur on Star Power (the world only: the highway is drawn after the chain).
    if (s.starBlur && f.starBlur > 0.001f && f.hasCamera && depth) {
        Target& next = star_[starIndex_ ^ 1];
        Target& prev = star_[starIndex_];
        const bool resized = prev.w != sw || prev.h != sh;
        if (!ensure(next, sw, sh, true) || !ensure(prev, sw, sh, true)) return false;
        const bool valid = starValid_ && !resized && !cut(f.viewProj, starPrev_);
        // Light, medium, strong, stronger: the motion smear's gain, the zoom and the trail (gd01's).
        static constexpr float kScale[5] = {0, 2.0f, 3.5f, 5.0f, 6.5f}, kRadial[5] = {0, 0.008f, 0.016f, 0.026f, 0.036f},
                               kFeedback[5] = {0, 0.35f, 0.55f, 0.7f, 0.8f};
        const int lv = std::clamp(s.starBlur, 1, 4);
        EnhBlock k = b;
        put(k.prevViewProj, starPrev_);
        k.pass[0] = kScale[lv] * f.starBlur, k.pass[1] = kRadial[lv] * f.starBlur, k.pass[2] = kFeedback[lv] * f.starBlur;
        k.pass[3] = valid ? 1.0f : 0.0f;
        const TextureId t[3] = {cur, depth, device_.targetTexture(prev.id)};
        pass(Pass::StarBlur, &next, t, 3, &k, sizeof k, "starblur");
        cur = device_.targetTexture(next.id);
        starIndex_ ^= 1;
        starValid_ = true;
        starPrev_ = f.viewProj;
    } else {
        starValid_ = false;
    }

    // 3. Anti-aliasing at render resolution.
    if (s.aa == PostSettings::AA::Taa && f.hasCamera && depth) {
        Target& next = history_[historyIndex_ ^ 1];
        Target& prev = history_[historyIndex_];
        const bool resized = prev.w != sw || prev.h != sh;
        if (!ensure(next, sw, sh, true) || !ensure(prev, sw, sh, true)) return false;
        const bool reset = !historyValid_ || resized || cut(f.viewProj, taaPrev_);
        EnhBlock k = b;
        put(k.prevViewProj, taaPrev_);
        const std::array<float, 2> j = jitter(s, sw, sh);
        k.pass[0] = j[0] * 0.5f, k.pass[1] = j[1] * 0.5f, k.pass[2] = reset ? 1.0f : 0.0f;
        const TextureId t[3] = {cur, depth, device_.targetTexture(prev.id)};
        pass(Pass::Taa, &next, t, 3, &k, sizeof k, "taa");
        cur = device_.targetTexture(next.id);
        historyIndex_ ^= 1;
        historyValid_ = true;
        taaPrev_ = f.viewProj;
    } else {
        historyValid_ = false;
    }
    if (s.aa == PostSettings::AA::Fxaa) {
        Target& dst = a_.id && cur.id == device_.targetTexture(a_.id).id ? b_ : a_;
        if (!ensure(dst, sw, sh, false)) return false;
        pass(Pass::Fxaa, &dst, &cur, 1, &b, sizeof b, "fxaa");
        cur = device_.targetTexture(dst.id);
    }

    // 4. To the picture's size.
    if (sw > ow || sh > oh) {  // SSAA
        if (!ensure(out_, ow, oh, false)) return false;
        EnhBlock k = b;
        k.pass[0] = float(sw) / float(ow), k.pass[1] = float(sh) / float(oh);
        pass(Pass::Downsample, &out_, &cur, 1, &k, sizeof k, "downsample");
        cur = device_.targetTexture(out_.id);
    } else if (sw < ow || sh < oh) {  // FSR 1: EASU then RCAS
        if (!ensure(out_, ow, oh, false) || !ensure(out2_, ow, oh, false)) return false;
        pass(Pass::Easu, &out_, &cur, 1, &b, sizeof b, "easu");
        EnhBlock k = b;
        k.texel[0] = 1.0f / float(ow), k.texel[1] = 1.0f / float(oh);
        k.pass[0] = std::exp2(-0.2f);  // FSR's default 0.2 stops
        const TextureId up = device_.targetTexture(out_.id);
        pass(Pass::Rcas, &out2_, &up, 1, &k, sizeof k, "rcas");
        cur = device_.targetTexture(out2_.id);
    }
    if (failed_) return false;

    // 5. Sharpening, chromatic aberration, grain, into the picture.
    device_.setTarget({});
    device_.setViewport(x, y, ow, oh);
    EnhBlock k = b;
    k.texel[0] = 1.0f / float(ow), k.texel[1] = 1.0f / float(oh);
    k.pass[0] = s.sharpen >= 2 ? 1.0f : s.sharpen == 1 ? 0.45f : 0.0f;
    static constexpr float kCa[4] = {0, 0.006f, 0.012f, 0.02f};             // none, light, medium, strong
    static constexpr float kGrain[5] = {0, 0.03f, 0.045f, 0.07f, 0.095f};  // none, light, medium, strong, stronger
    k.pass[1] = kCa[std::clamp(s.chromatic, 0, 3)];
    k.pass[2] = kGrain[std::clamp(s.grain, 0, 4)];
    k.pass[3] = f.timeSec;
    pass(s.sharpen || s.chromatic || s.grain ? Pass::Finish : Pass::Copy, nullptr, &cur, 1, &k, sizeof k,
         s.sharpen || s.chromatic || s.grain ? "finish" : "copy");
    ++frame_;
    ++frames_;
    return !failed_;
}

}  // namespace gfx
