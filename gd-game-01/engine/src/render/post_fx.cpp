// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/post_fx.h"
#include "render/vk_upscaler.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "render/gl.h"

namespace {

// A full-screen triangle; vUv covers the target 0..1.
const char* kVertex = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);
    vUv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

// Dual Kawase down: 5 taps around the destination texel (Marius Bjørge, SIGGRAPH 2015).
const char* kDown = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uTexel;  // source texel
out vec4 fragColor;
void main() {
    vec3 c = texture(uTex, vUv).rgb * 4.0;
    c += texture(uTex, vUv - uTexel).rgb;
    c += texture(uTex, vUv + uTexel).rgb;
    c += texture(uTex, vUv + vec2(uTexel.x, -uTexel.y)).rgb;
    c += texture(uTex, vUv - vec2(uTexel.x, -uTexel.y)).rgb;
    fragColor = vec4(c / 8.0, 1.0);
}
)";

// Dual Kawase up: 8 taps from the smaller level, plus the same-size level of the down chain.
const char* kUp = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform sampler2D uSkip;
uniform vec2 uTexel;  // source (smaller) texel
out vec4 fragColor;
void main() {
    vec2 h = uTexel * 0.5;
    vec3 c = texture(uTex, vUv + vec2(-h.x * 2.0, 0.0)).rgb;
    c += texture(uTex, vUv + vec2(-h.x, h.y)).rgb * 2.0;
    c += texture(uTex, vUv + vec2(0.0, h.y * 2.0)).rgb;
    c += texture(uTex, vUv + vec2(h.x, h.y)).rgb * 2.0;
    c += texture(uTex, vUv + vec2(h.x * 2.0, 0.0)).rgb;
    c += texture(uTex, vUv + vec2(h.x, -h.y)).rgb * 2.0;
    c += texture(uTex, vUv + vec2(0.0, -h.y * 2.0)).rgb;
    c += texture(uTex, vUv + vec2(-h.x, -h.y)).rgb * 2.0;
    fragColor = vec4(mix(c / 12.0, texture(uSkip, vUv).rgb, 0.3), 1.0);
}
)";

// Ambient occlusion, bloom and colour in one pass at render resolution.
const char* kComposite = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;    // the scene
uniform sampler2D uBlur;   // the blur pyramid's top (1/2 res)
uniform sampler2D uBlurLo; // a wide level (1/8 res)
uniform sampler2D uAo;     // ambient occlusion (r) and the eye distance (g)
uniform int uSsao, uGrade;
uniform float uBloom;      // 0 off
uniform float uAoK;        // the occlusion's exponent: light 0.5 .. stronger 1.5
uniform vec2 uTexel;
uniform int uCinematic;
out vec4 fragColor;
#if GD_HDR  // Enhanced > HDR (PostFx::program builds this variant only for Emulated / True HDR)
uniform sampler2D uExposure; // the auto exposure (1x1, r)
uniform float uPeak;       // HDR output: the display's headroom in paper whites; 1 = SDR

// The scene's sRGB-encoded values and linear light (extended above 1.0).
vec3 toLinear(vec3 c) { c = max(c, 0.0); return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }
vec3 toSrgb(vec3 c) { c = max(c, 0.0); return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }
// ACES filmic (Krzysztof Narkowicz's fit of the RRT + ODT) to SDR.
vec3 aces(vec3 x) { return clamp(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
// HDR output: the same curve up to 0.5, then an exponential shoulder of the
// same slope that reaches the display's peak (uPeak) instead of 1.0.
vec3 acesHdr(vec3 x) {
    const float a = 0.61631, s = 0.65105;  // aces(0.5) and its slope there
    float r = uPeak - a;
    vec3 hi = a + r * (1.0 - exp(-max(x - 0.5, 0.0) * s / r));
    return mix(aces(x), hi, step(0.5, x));
}
#endif

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 saturation(vec3 c, float s) { return mix(vec3(luma(c)), c, s); }

// Colour grading presets, on the display-referred image.
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
        float m = clamp((pow(l, 0.75) - 0.3) * 1.3 + 0.3, 0.0, 1.0);  // contrast around the venue's dark mid-tones
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
    vec3 c = texture(uTex, vUv).rgb;
    if (uSsao != 0) {
        // A 3x3 blur of the occlusion that keeps to surfaces at the same distance.
        vec2 a0 = texture(uAo, vUv).rg;
        float sum = 0.0, wsum = 0.0;
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) {
                vec2 a = texture(uAo, vUv + vec2(i, j) * uTexel * 1.5).rg;
                float w = 1.0 / (1.0 + abs(a.y - a0.y) * 0.25);
                sum += a.x * w, wsum += w;
            }
        c *= pow(sum / wsum, uAoK);
    }
    if (uBloom > 0.0) {
        // Bright parts of the blurred frame (thresholded after the blur).
        // Thresholded on luminance with a soft knee, keeping the light's colour: a per-channel
        // threshold made broad, saturated surfaces glow (Toxic's teal stage floor blew out to
        // white in close-ups), where only the lights themselves should.
        vec3 b = texture(uBlur, vUv).rgb;
        vec3 wide = texture(uBlurLo, vUv).rgb;
        float lb = luma(b), lw = luma(wide);
        float kb = smoothstep(0.62, 0.9, lb) * max(lb - 0.62, 0.0) / max(lb, 1e-3);
        float kw = smoothstep(0.55, 0.85, lw) * max(lw - 0.55, 0.0) / max(lw, 1e-3);
        vec3 glow = b * kb * 2.2 + wide * kw * 1.6;
        c += glow * uBloom;
    }
#if GD_HDR
    // Exposure and tone map in linear light, back to the display's encoding.
    vec3 lin = toLinear(min(c, vec3(64.0))) * texture(uExposure, vec2(0.5)).r;
    c = toSrgb(uPeak > 1.0 ? acesHdr(lin) : aces(lin));
    // HDR output: the colour looks below apply to the SDR range; highlights above it ride along.
    vec3 over = vec3(0.0);
#endif
    if (uCinematic != 0) {
#if GD_HDR
        if (uPeak > 1.0) over = max(c - 1.0, 0.0), c -= over;
#endif
        float l = luma(c);
        c = mix(vec3(l), c, 1.12);                        // a little more saturation
        c = clamp(c, 0.0, 1.0);
        c = mix(c, c * c * (3.0 - 2.0 * c), 0.4);         // filmic S-curve (toe and shoulder)
        c += vec3(0.025, 0.006, -0.02) * l - vec3(0.012, -0.004, -0.022) * (1.0 - l) * 0.6;  // warm highs, teal lows
        float v = length((vUv - 0.5) * vec2(1.25, 1.0));
#if GD_HDR
        float vig = mix(1.0, 0.7, smoothstep(0.35, 0.9, v));
        c *= vig;
        if (uPeak > 1.0) c += over * vig;
#else
        c *= mix(1.0, 0.7, smoothstep(0.35, 0.9, v));     // vignette
#endif
    }
#if GD_HDR
    if (uGrade != 0) {
        if (uPeak > 1.0) over = max(c - 1.0, 0.0);
        c = grade(clamp(c, 0.0, 1.0), uGrade);
        if (uPeak > 1.0) c += uGrade == 4 || uGrade == 6 ? vec3(luma(over)) : over;
    }
    fragColor = vec4(clamp(c, 0.0, uPeak > 1.0 ? toSrgb(vec3(uPeak)).r : 1.0), 1.0);
#else
    if (uGrade != 0) c = grade(clamp(c, 0.0, 1.0), uGrade);
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
#endif
}
)";

// Screen-space ambient occlusion: a world-space hemisphere of 12 samples
// around each pixel's surface (normal from the nearer depth neighbours),
// tested against the depth buffer, with a range check so far-off surfaces do
// not shadow. Out: occlusion (r) and the eye distance (g) for the blur.
const char* kAo = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uDepth;
uniform mat4 uInvViewProj, uViewProj;
uniform vec2 uTexel;
uniform float uRadius;
out vec4 fragColor;
vec3 world(vec2 uv, float d) {
    vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
vec3 at(vec2 uv) { return world(uv, texture(uDepth, uv).r); }
void main() {
    float d = texture(uDepth, vUv).r;
    vec4 e = uInvViewProj * vec4(0.0, 0.0, 1.0, 0.0);
    vec3 eye = e.xyz / e.w;
    if (d >= 0.99999) {
        fragColor = vec4(1.0, 1e4, 0.0, 1.0);
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
    float occ = 0.0;
    const int kSamples = 12;
    for (int i = 0; i < kSamples; ++i) {
        float fi = (float(i) + 0.5) / float(kSamples);
        float a = float(i) * 2.39996 + rot;
        float z = mix(0.15, 1.0, fi), rr = sqrt(1.0 - z * z);
        vec3 k = (t * cos(a) * rr + b * sin(a) * rr + n * z) * uRadius * mix(0.2, 1.0, fi * fi);
        vec4 cs = uViewProj * vec4(p + k, 1.0);
        if (cs.w <= 0.0) continue;
        vec3 ndc = cs.xyz / cs.w;
        vec2 suv = ndc.xy * 0.5 + 0.5;
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
        float sd = texture(uDepth, suv).r;
        if (sd < ndc.z * 0.5 + 0.5 - 1e-5) {
            float dist = length(world(suv, sd) - p);
            occ += 1.0 - smoothstep(uRadius * 0.8, uRadius * 2.0, dist);
        }
    }
    fragColor = vec4(1.0 - 0.85 * occ / float(kSamples), length(eye - p), 0.0, 1.0);
}
)";

// Blur on Star Power: the scene smeared along its camera motion (the depth
// buffer reprojected into the last frame) and a slight zoom from the centre,
// then blended with the last frames (trails on the band and the lights).
const char* kStarBlur = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform sampler2D uDepth;
uniform sampler2D uAccum;
uniform mat4 uInvViewProj, uPrevViewProj;
uniform float uScale, uRadial, uFeedback;
uniform int uValid;
out vec4 fragColor;
void main() {
    float d = min(texture(uDepth, vUv).r, 0.99999);
    vec4 p = uInvViewProj * vec4(vUv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 prev = uPrevViewProj * vec4(p.xyz / p.w, 1.0);
    vec2 vel = uValid != 0 && prev.w > 0.0 ? vUv - (prev.xy / prev.w * 0.5 + 0.5) : vec2(0.0);
    vel *= uScale;
    float len = length(vel);
    if (len > 0.08) vel *= 0.08 / len;
    vec2 v = vel + (vUv - 0.5) * uRadial;
    vec3 c = vec3(0.0);
    const int kTaps = 12;
    for (int i = 0; i < kTaps; ++i) c += texture(uTex, vUv - v * (float(i) / float(kTaps - 1) - 0.5)).rgb;
    c /= float(kTaps);
    if (uValid != 0) c = mix(c, texture(uAccum, vUv).rgb, uFeedback);
    fragColor = vec4(c, 1.0);
}
)";

// At the output size: contrast-adaptive sharpening (after AMD CAS: a cross
// whose negative lobe shrinks where the local range is already high),
// chromatic aberration (red and blue pulled apart towards the edges) and
// animated film grain, strongest in the mid-tones.
const char* kFinish = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uTexel;
uniform float uSharp, uCa, uGrain, uTime;
#if GD_HDR
uniform float uPeak;  // True HDR: the encoded white, above 1
#define PEAK uPeak
#else
#define PEAK 1.0
#endif
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec3 e = texture(uTex, vUv).rgb;
    vec3 c = e;
    if (uSharp > 0.0) {
        vec3 b = texture(uTex, vUv + vec2(0, -uTexel.y)).rgb, dd = texture(uTex, vUv + vec2(-uTexel.x, 0)).rgb;
        vec3 f = texture(uTex, vUv + vec2(uTexel.x, 0)).rgb, h = texture(uTex, vUv + vec2(0, uTexel.y)).rgb;
        vec3 mn = min(e, min(min(b, dd), min(f, h))), mx = max(e, max(max(b, dd), max(f, h)));
        vec3 amp = sqrt(clamp(min(mn, PEAK - mx) / max(mx, 1e-4), 0.0, 1.0));
        vec3 w = amp * (-1.0 / mix(8.0, 5.0, uSharp));
        c = clamp((e + (b + dd + f + h) * w) / (1.0 + 4.0 * w), 0.0, PEAK);
    }
    if (uCa > 0.0) {
        vec2 off = (vUv - 0.5) * uCa * length(vUv - 0.5);
        c.r += texture(uTex, vUv + off).r - e.r;
        c.b += texture(uTex, vUv - off).b - e.b;
    }
    if (uGrain > 0.0) {
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
#if GD_HDR
        l = min(l, 1.0);
#endif
        float n = hash(floor(gl_FragCoord.xy) + fract(uTime * 7.31) * 413.0) + hash(floor(gl_FragCoord.xy) * 1.37 - fract(uTime * 3.17) * 211.0) - 1.0;
        c += n * uGrain * (0.35 + 2.6 * l * (1.0 - l));
    }
    fragColor = vec4(clamp(c, 0.0, PEAK), 1.0);
}
)";

// TAA: the history reprojected through the depth buffer (camera motion),
// clamped to the current 3x3 neighbourhood in YCoCg (moving characters).
const char* kTaa = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform sampler2D uDepth;
uniform sampler2D uHistory;
uniform mat4 uInvViewProj, uPrevViewProj;
uniform vec2 uTexel, uJitterUv;
uniform int uReset;
out vec4 fragColor;
vec3 toYCoCg(vec3 c) { return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25))); }
vec3 toRgb(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
void main() {
    vec3 cur = texture(uTex, vUv + uJitterUv).rgb;  // un-jittered
    if (uReset != 0) {
        fragColor = vec4(cur, 1.0);
        return;
    }
    vec3 mn = vec3(1e9), mx = vec3(-1e9), m1 = vec3(0.0), m2 = vec3(0.0);
    float nearest = 1.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            vec2 uv = vUv + vec2(i, j) * uTexel;
            vec3 s = toYCoCg(texture(uTex, uv).rgb);
            mn = min(mn, s), mx = max(mx, s);
            m1 += s, m2 += s * s;
            nearest = min(nearest, texture(uDepth, uv).r);  // the closest depth: edges follow the foreground
        }
    // Variance clipping tightens the box (fewer ghosts without motion vectors).
    vec3 mean = m1 / 9.0, sd = sqrt(max(m2 / 9.0 - mean * mean, 0.0));
    mn = max(mn, mean - sd * 1.25), mx = min(mx, mean + sd * 1.25);
    vec4 p = uInvViewProj * vec4(vUv * 2.0 - 1.0, nearest * 2.0 - 1.0, 1.0);
    vec4 prev = uPrevViewProj * vec4(p.xyz / p.w, 1.0);
    vec2 prevUv = prev.xy / prev.w * 0.5 + 0.5;
    if (any(lessThan(prevUv, vec2(0.0))) || any(greaterThan(prevUv, vec2(1.0)))) {
        fragColor = vec4(cur, 1.0);
        return;
    }
    vec3 hist = clamp(toYCoCg(texture(uHistory, prevUv).rgb), mn, mx);
    // Faster convergence where the history moved (less smearing on camera moves).
    float motion = length((prevUv - vUv) / uTexel);
    float alpha = mix(0.1, 0.35, clamp(motion / 8.0, 0.0, 1.0));
    fragColor = vec4(toRgb(mix(hist, toYCoCg(cur), alpha)), 1.0);
}
)";

// FXAA 3.11 (Timothy Lottes), the PC quality preset 12 search, luma from green-weighted RGB.
const char* kFxaa = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uTexel;
out vec4 fragColor;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
float lumaAt(vec2 uv) { return luma(texture(uTex, uv).rgb); }
void main() {
    vec3 rgbM = texture(uTex, vUv).rgb;
    float lM = luma(rgbM);
    float lN = lumaAt(vUv + vec2(0, -uTexel.y)), lS = lumaAt(vUv + vec2(0, uTexel.y));
    float lE = lumaAt(vUv + vec2(uTexel.x, 0)), lW = lumaAt(vUv + vec2(-uTexel.x, 0));
    float rangeMax = max(max(lN, lS), max(lE, max(lW, lM))), rangeMin = min(min(lN, lS), min(lE, min(lW, lM)));
    float range = rangeMax - rangeMin;
    if (range < max(0.0312, rangeMax * 0.125)) {  // edge threshold min / quality
        fragColor = vec4(rgbM, 1.0);
        return;
    }
    float lNW = lumaAt(vUv - uTexel), lSE = lumaAt(vUv + uTexel);
    float lNE = lumaAt(vUv + vec2(uTexel.x, -uTexel.y)), lSW = lumaAt(vUv + vec2(-uTexel.x, uTexel.y));
    float lNS = lN + lS, lWE = lW + lE;
    float edgeH = abs(-2.0 * lW + lNW + lSW) + abs(-2.0 * lM + lNS) * 2.0 + abs(-2.0 * lE + lNE + lSE);
    float edgeV = abs(-2.0 * lS + lSW + lSE) + abs(-2.0 * lM + lWE) * 2.0 + abs(-2.0 * lN + lNW + lNE);
    bool horz = edgeH >= edgeV;
    float subA = (lNS + lWE) * 2.0 + (lNW + lSW + lNE + lSE);
    float l1 = horz ? lN : lW, l2 = horz ? lS : lE;
    float g1 = abs(l1 - lM), g2 = abs(l2 - lM);
    bool pair1 = g1 >= g2;
    float gScaled = max(g1, g2) * 0.25;
    float step = horz ? uTexel.y : uTexel.x;
    if (pair1) step = -step;
    float lLocal = pair1 ? (l1 + lM) * 0.5 : (l2 + lM) * 0.5;
    vec2 uv = vUv;
    if (horz) uv.y += step * 0.5; else uv.x += step * 0.5;
    vec2 off = horz ? vec2(uTexel.x, 0) : vec2(0, uTexel.y);
    vec2 uvN = uv - off, uvP = uv + off;
    float eN = lumaAt(uvN) - lLocal, eP = lumaAt(uvP) - lLocal;
    bool doneN = abs(eN) >= gScaled, doneP = abs(eP) >= gScaled;
    const float kSteps[10] = float[10](1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0, 8.0, 8.0);
    for (int i = 0; i < 10 && !(doneN && doneP); ++i) {
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
    pixelOff = max(pixelOff, sub * sub * 0.75);  // subpixel quality
    vec2 fuv = vUv;
    if (horz) fuv.y += pixelOff * step; else fuv.x += pixelOff * step;
    fragColor = vec4(texture(uTex, fuv).rgb, 1.0);
}
)";

// SSAA: each output pixel averages its 2x2 (or more) source pixels with four
// bilinear taps (a 4x4 tent over the footprint).
const char* kDownsample = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uTexel;  // source texel
uniform vec2 uRatio;  // source / output size
out vec4 fragColor;
void main() {
    vec2 o = uTexel * uRatio * 0.25;
    vec3 c = texture(uTex, vUv + vec2(-o.x, -o.y)).rgb + texture(uTex, vUv + vec2(o.x, -o.y)).rgb +
             texture(uTex, vUv + vec2(-o.x, o.y)).rgb + texture(uTex, vUv + vec2(o.x, o.y)).rgb;
    fragColor = vec4(c * 0.25, 1.0);
}
)";

// FSR 1 EASU (Edge Adaptive Spatial Upsampling), after AMD FidelityFX
// ffx_fsr1.h (MIT): 12 taps, the local edge direction and length from four
// bilinear luma gradients, an approximated Lanczos-2 kernel stretched along
// the edge, then clamped to the 2x2 nearest texels (deringing).
const char* kEasu = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uInSize, uOutSize;
out vec4 fragColor;
vec3 tap(vec2 p) { return texture(uTex, (p + 0.5) / uInSize).rgb; }
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
    vec2 pp = vUv * uInSize - 0.5;
    vec2 fp = floor(pp);
    pp -= fp;
    //    b c
    //  e f g h
    //  i j k l
    //    n o
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
)";

// FSR 1 RCAS (Robust Contrast-Adaptive Sharpening): a 5-tap cross whose
// negative lobe is limited so the result never leaves the local range.
const char* kRcas = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uTexel;
uniform float uSharp;  // exp2(-stops)
#if GD_HDR
uniform float uPeak;   // True HDR: the encoded white, above 1
#define PEAK uPeak
#else
#define PEAK 1.0
#endif
out vec4 fragColor;
void main() {
    vec3 b = texture(uTex, vUv + vec2(0, -uTexel.y)).rgb, d = texture(uTex, vUv + vec2(-uTexel.x, 0)).rgb;
    vec3 e = texture(uTex, vUv).rgb;
    vec3 f = texture(uTex, vUv + vec2(uTexel.x, 0)).rgb, h = texture(uTex, vUv + vec2(0, uTexel.y)).rgb;
    vec3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));
    vec3 hitMin = mn4 / max(4.0 * mx4, 1e-4);
#if GD_HDR
    vec3 hitMax = (PEAK - mx4) / min(4.0 * mn4 - 4.0 * PEAK, -1e-4);
#else
    vec3 hitMax = (1.0 - mx4) / min(4.0 * mn4 - 4.0, -1e-4);
#endif
    vec3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * uSharp;
    fragColor = vec4((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0), 1.0);
}
)";

// HDR auto exposure, 1: the scene's log luminance (4 taps per texel of a
// 128 x 128 target, whose mipmaps then average it).
const char* kLum = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uTexel;  // the target's texel
out vec4 fragColor;
float lum(vec2 uv) {
    vec3 c = max(min(texture(uTex, uv).rgb, vec3(64.0)), 0.0);
    c = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}
void main() {
    vec2 q = uTexel * 0.25;
    float l = lum(vUv + vec2(-q.x, -q.y)) + lum(vUv + vec2(q.x, -q.y)) + lum(vUv + vec2(-q.x, q.y)) + lum(vUv + q);
    fragColor = vec4(log(max(l * 0.25, 1e-4)), 0.0, 0.0, 1.0);
}
)";

// HDR auto exposure, 2: the exposure that brings the mean luminance part of
// the way to the key, within limits (dark venues stay dark, a bright flash
// does not black out the stage), eased in over time from the last one.
const char* kAdapt = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uLum;
uniform sampler2D uPrev;
uniform float uLevel;  // the 1x1 mip
uniform float uBlend;  // 1: no history
out vec4 fragColor;
void main() {
    float avg = exp(textureLod(uLum, vec2(0.5), uLevel).r);
    // The key is the venues' typical log-average (about 0.012: dark stages with bright lights), so an
    // average shot keeps the exposure of the original; darker shots open up to +0.5 stop, brighter
    // ones close down to -0.5, each by half the difference.
    float target = exp2(clamp(0.5 * log2(0.012 / max(avg, 1e-4)), -0.5, 0.5));
    float prev = texture(uPrev, vec2(0.5)).r;
    fragColor = vec4(mix(prev, target, uBlend), 0.0, 0.0, 1.0);
}
)";

const char* kCopy = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
out vec4 fragColor;
void main() { fragColor = vec4(texture(uTex, vUv).rgb, 1.0); }
)";

unsigned compileShader(GLenum type, const char* src, std::string& error) {
    unsigned s = gl::glCreateShader(type);
    gl::glShaderSource(s, 1, &src, nullptr);
    gl::glCompileShader(s);
    int ok = 0;
    gl::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::glGetShaderInfoLog(s, sizeof log, nullptr, log);
        error = log;
        gl::glDeleteShader(s);
        return 0;
    }
    return s;
}

// Column-major 4x4 inverse (cofactors).
std::array<float, 16> inverse(const std::array<float, 16>& m) {
    std::array<float, 16> inv{};
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
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f) return {};
    for (float& v : inv) v /= det;
    return inv;
}

// The radical inverse in `base` (Halton sequence).
float halton(unsigned i, unsigned base) {
    float f = 1, r = 0;
    for (; i > 0; i /= base) f /= float(base), r += f * float(i % base);
    return r;
}

}  // namespace

PostFx::PostFx() = default;

float PostFx::upscalerMs() const { return vk_ && upHistory_ ? vk_->gpuMs() : 0.0f; }

PostFx::~PostFx() {
    // The GL context may already be gone at exit; the driver frees its objects with it.
}

unsigned PostFx::program(const char* fragment, const char* name, bool hdr) {
    std::string error;
    // GD_HDR selects a shader's HDR variant (Enhanced > HDR); without it the source is the SDR chain's own.
    std::string src = fragment;
    const size_t eol = src.find('\n');
    src.insert(eol + 1, hdr ? "#define GD_HDR 1\n" : "#define GD_HDR 0\n");
    unsigned vs = compileShader(GL_VERTEX_SHADER, kVertex, error);
    unsigned fs = vs ? compileShader(GL_FRAGMENT_SHADER, src.c_str(), error) : 0;
    if (!fs) {
        SDL_Log("post-processing %s: %s", name, error.c_str());
        if (vs) gl::glDeleteShader(vs);
        failedPrograms_.push_back(name);
        return 0;
    }
    unsigned p = gl::glCreateProgram();
    gl::glAttachShader(p, vs);
    gl::glAttachShader(p, fs);
    gl::glLinkProgram(p);
    gl::glDeleteShader(vs);
    gl::glDeleteShader(fs);
    int ok = 0;
    gl::glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::glGetProgramInfoLog(p, sizeof log, nullptr, log);
        SDL_Log("post-processing %s: %s", name, log);
        gl::glDeleteProgram(p);
        failedPrograms_.push_back(name);
        return 0;
    }
    return p;
}

bool PostFx::init() {
    if (ready_ || failed_) return ready_;
    gl::glGenVertexArrays(1, &vao_);
    pDown_ = program(kDown, "blur down");
    pUp_ = program(kUp, "blur up");
    pComposite_ = program(kComposite, "composite");
    pCompositeHdr_ = program(kComposite, "composite (HDR)", true);
    pFinishHdr_ = program(kFinish, "finish (HDR)", true);
    pRcasHdr_ = program(kRcas, "FSR RCAS (HDR)", true);
    pTaa_ = program(kTaa, "TAA");
    pFxaa_ = program(kFxaa, "FXAA");
    pDownsample_ = program(kDownsample, "SSAA");
    pEasu_ = program(kEasu, "FSR EASU");
    pRcas_ = program(kRcas, "FSR RCAS");
    pCopy_ = program(kCopy, "copy");
    pAo_ = program(kAo, "ambient occlusion");
    pStarBlur_ = program(kStarBlur, "star power blur");
    pFinish_ = program(kFinish, "finish");
    pLum_ = program(kLum, "hdr luminance");
    pAdapt_ = program(kAdapt, "hdr exposure");
    failed_ = !(pDown_ && pUp_ && pComposite_ && pTaa_ && pFxaa_ && pDownsample_ && pEasu_ && pRcas_ && pCopy_ && pAo_ && pStarBlur_ &&
                pFinish_ && pLum_ && pAdapt_ && pCompositeHdr_ && pFinishHdr_ && pRcasHdr_);
    ready_ = !failed_;
    return ready_;
}

void PostFx::ensure(Target& t, int w, int h, bool hdr) {
    w = std::max(w, 1), h = std::max(h, 1);
    if (t.fbo && t.w == w && t.h == h && t.hdr == hdr) return;
    if (!t.fbo) gl::glGenFramebuffers(1, &t.fbo), gl::glGenTextures(1, &t.tex);
    gl::glBindTexture(GL_TEXTURE_2D, t.tex);
    gl::glTexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    t.w = w, t.h = h, t.hdr = hdr;
}

void PostFx::bindTarget(const Target& t) {
    gl::glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl::glViewport(0, 0, t.w, t.h);
}

void PostFx::drawQuad() {
    gl::glBindVertexArray(vao_);
    gl::glDrawArrays(GL_TRIANGLES, 0, 3);
}

std::array<float, 2> PostFx::jitter(const PostSettings& s, int sw, int sh) {
    if (sw <= 0 || sh <= 0) return lastJitter_ = {0, 0};
    unsigned phases = 0;
    if (s.temporalUpscaler() && upFailed_ != int(s.upscaler)) {
        // FSR's rule: 8 x (display / render)^2 phases, so every output pixel is covered.
        float r = 1.0f / std::max(s.fsrScale, 0.1f);
        phases = unsigned(std::lround(8.0f * r * r));
    } else if (s.aa == PostSettings::AA::Taa) {
        phases = 8;
    }
    if (!phases) return lastJitter_ = {0, 0};
    unsigned i = frame_ % phases + 1;  // Halton (2, 3)
    return lastJitter_ = {(halton(i, 2) - 0.5f) * 2.0f / float(sw), (halton(i, 3) - 0.5f) * 2.0f / float(sh)};
}

void PostFx::blit(unsigned texture) {
    if (!init()) return;
    gl::glUseProgram(pCopy_);
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glBindTexture(GL_TEXTURE_2D, texture);
    gl::glUniform1i(gl::glGetUniformLocation(pCopy_, "uTex"), 0);
    drawQuad();
}

unsigned PostFx::process(unsigned color, unsigned depth, int sw, int sh, int ow, int oh, const PostSettings& s, const PostFrame& f) {
    if (!init()) return 0;
    gl::glDisable(GL_BLEND);
    gl::glDisable(GL_DEPTH_TEST);
    gl::glDisable(GL_CULL_FACE);
    gl::glDisable(GL_SCISSOR_TEST);
    gl::glDepthMask(GL_FALSE);
    gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::glActiveTexture(GL_TEXTURE0);
    auto uniform = [](unsigned p, const char* n) { return gl::glGetUniformLocation(p, n); };
    auto bindTex = [](int unit, unsigned tex) {
        gl::glActiveTexture(GLenum(GL_TEXTURE0 + unit));
        gl::glBindTexture(GL_TEXTURE_2D, tex);
    };
    const std::array<float, 16> inv = f.hasCamera ? inverse(f.viewProj) : std::array<float, 16>{};
    unsigned cur = color;
    // HDR: the pyramid keeps lights above 1.0; True HDR (peak above 1) keeps the
    // whole chain in RGBA16F after the composite. A temporal AI upscaler takes
    // an 8-bit image, so with one the highlights are tone mapped to SDR.
    const bool hdr = s.hdr != 0;
    const float peak = hdr && s.hdrPeak > 1.0f && !s.temporalUpscaler() ? std::min(s.hdrPeak, 64.0f) : 1.0f;
    const bool wide = peak > 1.0f;
    const float peakEnc = wide ? (peak <= 0.0031308f ? peak * 12.92f : 1.055f * std::pow(peak, 1 / 2.4f) - 0.055f) : 1.0f;

    // 1. The blur pyramid (dual Kawase) for bloom.
    const bool pyramid = s.bloom != 0;
    if (pyramid) {
        int w = sw, h = sh;
        unsigned src = color;
        gl::glUseProgram(pDown_);
        gl::glUniform1i(uniform(pDown_, "uTex"), 0);
        for (int l = 0; l < kLevels; ++l) {
            gl::glUniform2f(uniform(pDown_, "uTexel"), 0.5f / float(w), 0.5f / float(h));
            w = std::max(1, w / 2), h = std::max(1, h / 2);
            ensure(down_[l], w, h, hdr);
            bindTarget(down_[l]);
            bindTex(0, src);
            drawQuad();
            src = down_[l].tex;
        }
        gl::glUseProgram(pUp_);
        gl::glUniform1i(uniform(pUp_, "uTex"), 0);
        gl::glUniform1i(uniform(pUp_, "uSkip"), 1);
        for (int l = kLevels - 2; l >= 0; --l) {
            ensure(up_[l], down_[l].w, down_[l].h, hdr);
            bindTarget(up_[l]);
            gl::glUniform2f(uniform(pUp_, "uTexel"), 1.0f / float(down_[l + 1].w), 1.0f / float(down_[l + 1].h));
            bindTex(0, src);
            bindTex(1, down_[l].tex);
            drawQuad();
            src = up_[l].tex;
        }
    }
    // Ambient occlusion at render resolution (16-bit: the eye distance rides along for the blur).
    const bool ssao = s.ssao && f.hasCamera;
    if (ssao) {
        ensure(ao_, sw, sh, true);
        bindTarget(ao_);
        gl::glUseProgram(pAo_);
        gl::glUniform1i(uniform(pAo_, "uDepth"), 0);
        bindTex(0, depth);
        gl::glUniformMatrix4fv(uniform(pAo_, "uInvViewProj"), 1, GL_FALSE, inv.data());
        gl::glUniformMatrix4fv(uniform(pAo_, "uViewProj"), 1, GL_FALSE, f.viewProj.data());
        gl::glUniform2f(uniform(pAo_, "uTexel"), 1.0f / float(sw), 1.0f / float(sh));
        gl::glUniform1f(uniform(pAo_, "uRadius"), 14.0f);  // world units; a rocker is ~80 tall
        drawQuad();
    }
    if (hdr) autoExposure(color);
    // 2. Ambient occlusion, bloom, (HDR) exposure and tone map, colour.
    if (pyramid || ssao || s.cinematic || s.grade || hdr) {
        ensure(a_, sw, sh, wide);
        bindTarget(a_);
        const unsigned comp = hdr ? pCompositeHdr_ : pComposite_;
        gl::glUseProgram(comp);
        gl::glUniform1i(uniform(comp, "uTex"), 0);
        gl::glUniform1i(uniform(comp, "uBlur"), 1);
        gl::glUniform1i(uniform(comp, "uBlurLo"), 2);
        gl::glUniform1i(uniform(comp, "uAo"), 3);
        bindTex(0, color);
        bindTex(1, pyramid ? up_[0].tex : color);
        bindTex(2, pyramid ? up_[2].tex : color);
        bindTex(3, ssao ? ao_.tex : color);
        gl::glUniform1i(uniform(comp, "uSsao"), ssao ? 1 : 0);
        static constexpr float kAoK[5] = {0, 0.5f, 0.8f, 1.1f, 1.5f};  // none, light, medium, strong, stronger
        gl::glUniform1f(uniform(comp, "uAoK"), kAoK[std::clamp(s.ssao, 0, 4)]);
        gl::glUniform1f(uniform(comp, "uBloom"), !pyramid ? 0.0f : s.bloom >= 3 ? 0.9f : s.bloom == 2 ? 0.68f : s.bloom == 1 ? 0.45f : 0.0f);
        gl::glUniform2f(uniform(comp, "uTexel"), 1.0f / float(sw), 1.0f / float(sh));
        gl::glUniform1i(uniform(comp, "uCinematic"), s.cinematic ? 1 : 0);
        gl::glUniform1i(uniform(comp, "uGrade"), s.grade);
        gl::glUniform1i(uniform(comp, "uExposure"), 4);
        bindTex(4, hdr ? expo_[expoIndex_].tex : color);
        gl::glUniform1f(uniform(comp, "uPeak"), peak);
        drawQuad();
        cur = a_.tex;
    }

    // 2b. Blur on Star Power (venue and band only: the highway is drawn after the chain).
    if (s.starBlur && f.starBlur > 0.001f && f.hasCamera) {
        Target& next = starAccum_[starIndex_ ^ 1];
        Target& prev = starAccum_[starIndex_];
        const bool resized = prev.w != sw || prev.h != sh;
        ensure(next, sw, sh, true);
        ensure(prev, sw, sh, true);
        float diff = 0, norm = 0;
        for (int k = 0; k < 16; ++k) diff += std::fabs(f.viewProj[size_t(k)] - starPrevViewProj_[size_t(k)]), norm += std::fabs(f.viewProj[size_t(k)]);
        const bool valid = starValid_ && !resized && diff <= 0.15f * norm;  // not across a camera cut
        // Light, medium, strong, stronger: the motion smear's gain, the zoom and the trail.
        static constexpr float kScale[5] = {0, 2.0f, 3.5f, 5.0f, 6.5f}, kRadial[5] = {0, 0.008f, 0.016f, 0.026f, 0.036f},
                               kFeedback[5] = {0, 0.35f, 0.55f, 0.7f, 0.8f};
        const int lv = std::clamp(s.starBlur, 1, 4);
        bindTarget(next);
        gl::glUseProgram(pStarBlur_);
        gl::glUniform1i(uniform(pStarBlur_, "uTex"), 0);
        gl::glUniform1i(uniform(pStarBlur_, "uDepth"), 1);
        gl::glUniform1i(uniform(pStarBlur_, "uAccum"), 2);
        bindTex(0, cur);
        bindTex(1, depth);
        bindTex(2, prev.tex);
        gl::glUniformMatrix4fv(uniform(pStarBlur_, "uInvViewProj"), 1, GL_FALSE, inv.data());
        gl::glUniformMatrix4fv(uniform(pStarBlur_, "uPrevViewProj"), 1, GL_FALSE, starPrevViewProj_.data());
        gl::glUniform1f(uniform(pStarBlur_, "uScale"), kScale[lv] * f.starBlur);
        gl::glUniform1f(uniform(pStarBlur_, "uRadial"), kRadial[lv] * f.starBlur);
        gl::glUniform1f(uniform(pStarBlur_, "uFeedback"), kFeedback[lv] * f.starBlur);
        gl::glUniform1i(uniform(pStarBlur_, "uValid"), valid ? 1 : 0);
        drawQuad();
        cur = next.tex;
        starIndex_ ^= 1;
        starValid_ = true;
        starPrevViewProj_ = f.viewProj;
    } else {
        starValid_ = false;
    }

    // 3-4 with a temporal AI upscaler (Vulkan): it anti-aliases and upscales.
    if (s.temporalUpscaler()) {
        if (!vk_) vk_ = std::make_unique<VkUpscaler>();
        VkUpscaleFrame uf;
        uf.color = cur, uf.depth = depth;
        uf.sw = sw, uf.sh = sh, uf.ow = ow, uf.oh = oh;
        uf.jitterNdc = lastJitter_;
        uf.viewProj = f.viewProj;
        uf.hasCamera = f.hasCamera;
        float diff = 0, norm = 0;
        for (int k = 0; k < 16; ++k) diff += std::fabs(f.viewProj[size_t(k)] - upPrevViewProj_[size_t(k)]), norm += std::fabs(f.viewProj[size_t(k)]);
        uf.reset = !upHistory_ || !f.hasCamera || diff > 0.15f * norm;  // a camera cut
        uf.prevViewProj = uf.reset ? f.viewProj : upPrevViewProj_;
        uf.dtMs = upLastTime_ >= 0 ? (f.timeSec - upLastTime_) * 1000.0f : 16.7f;
        uf.quality = s.quality;
        unsigned out = vk_->upscale(s.upscaler == PostSettings::Upscaler::Dlss4 ? AiUpscaler::Dlss4 : AiUpscaler::Fsr31, uf);
        upscalerStatus_ = vk_->status();
        upPrevViewProj_ = f.viewProj;
        upLastTime_ = f.timeSec;
        upHistory_ = out != 0;
        upFailed_ = out ? -1 : int(s.upscaler);
        if (out) {
            if (upscalerStatus_ != loggedStatus_) SDL_Log("ai upscaling: %s", (loggedStatus_ = upscalerStatus_).c_str());
            historyValid_ = false;
            ++frame_;
            peakEnc_ = 1.0f;
            return finish(out, ow, oh, s, f);
        }
        upscalerStatus_ += " - using FSR 1";
    } else {
        upHistory_ = false;
        upFailed_ = -1;
        upscalerStatus_ = s.upscaler == PostSettings::Upscaler::Fsr1 ? "running FSR 1" : "";
    }

    if (upscalerStatus_ != loggedStatus_) SDL_Log("ai upscaling: %s", (loggedStatus_ = upscalerStatus_).c_str());

    // 3. Anti-aliasing at render resolution.
    if (s.aa == PostSettings::AA::Taa && f.hasCamera && !s.temporalUpscaler()) {
        Target& next = history_[historyIndex_ ^ 1];
        Target& prev = history_[historyIndex_];
        const bool resized = prev.w != sw || prev.h != sh;
        ensure(next, sw, sh, true);
        ensure(prev, sw, sh, true);
        // A camera cut: the matrix jumps (TAA reprojection only follows smooth moves).
        float diff = 0, norm = 0;
        for (int k = 0; k < 16; ++k) diff += std::fabs(f.viewProj[size_t(k)] - prevViewProj_[size_t(k)]), norm += std::fabs(f.viewProj[size_t(k)]);
        const bool reset = !historyValid_ || resized || diff > 0.15f * norm;
        bindTarget(next);
        gl::glUseProgram(pTaa_);
        gl::glUniform1i(uniform(pTaa_, "uTex"), 0);
        gl::glUniform1i(uniform(pTaa_, "uDepth"), 1);
        gl::glUniform1i(uniform(pTaa_, "uHistory"), 2);
        bindTex(0, cur);
        bindTex(1, depth);
        bindTex(2, prev.tex);
        gl::glUniformMatrix4fv(uniform(pTaa_, "uInvViewProj"), 1, GL_FALSE, inv.data());
        gl::glUniformMatrix4fv(uniform(pTaa_, "uPrevViewProj"), 1, GL_FALSE, prevViewProj_.data());
        gl::glUniform2f(uniform(pTaa_, "uTexel"), 1.0f / float(sw), 1.0f / float(sh));
        std::array<float, 2> j = jitter(s, sw, sh);
        gl::glUniform2f(uniform(pTaa_, "uJitterUv"), j[0] * 0.5f, j[1] * 0.5f);
        gl::glUniform1i(uniform(pTaa_, "uReset"), reset ? 1 : 0);
        drawQuad();
        cur = next.tex;
        historyIndex_ ^= 1;
        historyValid_ = true;
        prevViewProj_ = f.viewProj;
    } else {
        historyValid_ = false;
    }
    if (s.aa == PostSettings::AA::Fxaa && !s.temporalUpscaler()) {
        Target& dst = cur == a_.tex ? b_ : a_;
        ensure(dst, sw, sh, wide);
        bindTarget(dst);
        gl::glUseProgram(pFxaa_);
        gl::glUniform1i(uniform(pFxaa_, "uTex"), 0);
        gl::glUniform2f(uniform(pFxaa_, "uTexel"), 1.0f / float(sw), 1.0f / float(sh));
        bindTex(0, cur);
        drawQuad();
        cur = dst.tex;
    }

    // 4. To the output size.
    if (sw > ow || sh > oh) {  // SSAA (with FSR quality at 2x: still a downsample)
        ensure(out_, ow, oh, wide);
        bindTarget(out_);
        gl::glUseProgram(pDownsample_);
        gl::glUniform1i(uniform(pDownsample_, "uTex"), 0);
        gl::glUniform2f(uniform(pDownsample_, "uTexel"), 1.0f / float(sw), 1.0f / float(sh));
        gl::glUniform2f(uniform(pDownsample_, "uRatio"), float(sw) / float(ow), float(sh) / float(oh));
        bindTex(0, cur);
        drawQuad();
        cur = out_.tex;
    } else if (sw < ow || sh < oh) {  // FSR 1: EASU then RCAS
        ensure(out_, ow, oh, wide);
        bindTarget(out_);
        gl::glUseProgram(pEasu_);
        gl::glUniform1i(uniform(pEasu_, "uTex"), 0);
        gl::glUniform2f(uniform(pEasu_, "uInSize"), float(sw), float(sh));
        gl::glUniform2f(uniform(pEasu_, "uOutSize"), float(ow), float(oh));
        bindTex(0, cur);
        drawQuad();
        ensure(out2_, ow, oh, wide);
        bindTarget(out2_);
        const unsigned rcas = wide ? pRcasHdr_ : pRcas_;
        gl::glUseProgram(rcas);
        gl::glUniform1i(uniform(rcas, "uTex"), 0);
        gl::glUniform2f(uniform(rcas, "uTexel"), 1.0f / float(ow), 1.0f / float(oh));
        gl::glUniform1f(uniform(rcas, "uSharp"), std::exp2(-0.2f));  // FSR's default 0.2 stops
        gl::glUniform1f(uniform(rcas, "uPeak"), peakEnc);
        bindTex(0, out_.tex);
        drawQuad();
        cur = out2_.tex;
    }
    gl::glActiveTexture(GL_TEXTURE0);
    ++frame_;
    peakEnc_ = peakEnc;
    return finish(cur, ow, oh, s, f);
}

unsigned PostFx::finish(unsigned cur, int ow, int oh, const PostSettings& s, const PostFrame& f) {
    if (!s.sharpen && !s.chromatic && !s.grain) return cur;
    ensure(fin_, ow, oh, peakEnc_ > 1.0f);
    bindTarget(fin_);
    const unsigned fin = peakEnc_ > 1.0f ? pFinishHdr_ : pFinish_;
    gl::glUseProgram(fin);
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glBindTexture(GL_TEXTURE_2D, cur);
    gl::glUniform1i(gl::glGetUniformLocation(fin, "uTex"), 0);
    gl::glUniform2f(gl::glGetUniformLocation(fin, "uTexel"), 1.0f / float(ow), 1.0f / float(oh));
    gl::glUniform1f(gl::glGetUniformLocation(fin, "uSharp"), s.sharpen >= 2 ? 1.0f : s.sharpen == 1 ? 0.45f : 0.0f);
    static constexpr float kCa[4] = {0, 0.006f, 0.012f, 0.02f};                // none, light, medium, strong
    static constexpr float kGrain[5] = {0, 0.03f, 0.045f, 0.07f, 0.095f};     // none, light, medium, strong, stronger
    gl::glUniform1f(gl::glGetUniformLocation(fin, "uCa"), kCa[std::clamp(s.chromatic, 0, 3)]);
    gl::glUniform1f(gl::glGetUniformLocation(fin, "uGrain"), kGrain[std::clamp(s.grain, 0, 4)]);
    gl::glUniform1f(gl::glGetUniformLocation(fin, "uTime"), f.timeSec);
    gl::glUniform1f(gl::glGetUniformLocation(fin, "uPeak"), peakEnc_);
    drawQuad();
    return fin_.tex;
}

// HDR auto exposure: the scene's mean log luminance (a 128 x 128 log map
// mip-reduced to 1x1 on the GPU, no read-back) and the exposure eased
// towards it (about a second), in a 1x1 target the composite reads.
void PostFx::autoExposure(unsigned color) {
    constexpr int kSize = 128, kTop = 7;  // 128 -> 1 in 7 mips
    const bool fresh = !lum_.fbo;
    ensure(lum_, kSize, kSize, true);
    if (fresh) {
        gl::glBindTexture(GL_TEXTURE_2D, lum_.tex);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        gl::glGenerateMipmap(GL_TEXTURE_2D);
    }
    bindTarget(lum_);
    gl::glUseProgram(pLum_);
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glBindTexture(GL_TEXTURE_2D, color);
    gl::glUniform1i(gl::glGetUniformLocation(pLum_, "uTex"), 0);
    gl::glUniform2f(gl::glGetUniformLocation(pLum_, "uTexel"), 1.0f / kSize, 1.0f / kSize);
    drawQuad();
    gl::glBindTexture(GL_TEXTURE_2D, lum_.tex);
    gl::glGenerateMipmap(GL_TEXTURE_2D);

    const uint64_t now = SDL_GetTicksNS();
    const float dt = expoValid_ ? std::clamp(float(now - expoNs_) / 1e9f, 0.0f, 0.25f) : 0.0f;
    expoNs_ = now;
    Target& prev = expo_[expoIndex_];
    Target& next = expo_[expoIndex_ ^ 1];
    ensure(prev, 1, 1, true);
    ensure(next, 1, 1, true);
    bindTarget(next);
    gl::glUseProgram(pAdapt_);
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glBindTexture(GL_TEXTURE_2D, lum_.tex);
    gl::glActiveTexture(GL_TEXTURE1);
    gl::glBindTexture(GL_TEXTURE_2D, prev.tex);
    gl::glUniform1i(gl::glGetUniformLocation(pAdapt_, "uLum"), 0);
    gl::glUniform1i(gl::glGetUniformLocation(pAdapt_, "uPrev"), 1);
    gl::glUniform1f(gl::glGetUniformLocation(pAdapt_, "uLevel"), float(kTop));
    gl::glUniform1f(gl::glGetUniformLocation(pAdapt_, "uBlend"), expoValid_ ? 1.0f - std::exp(-dt * 1.5f) : 1.0f);
    drawQuad();
    gl::glActiveTexture(GL_TEXTURE0);
    expoIndex_ ^= 1;
    expoValid_ = true;
}
