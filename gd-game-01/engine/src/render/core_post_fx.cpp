// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The post-processing chain on gd-engine-core (core_post_fx.h). Each shader
// is post_fx.cpp's (or renderer.cpp's True HDR pass) in the gd shader
// dialect: the declarations become GD_* macros and the loose uniforms one
// std140 block, "Post", whose members keep their names, so every function
// body is the same text as local's. Keep the two in step: a change to a
// shader in post_fx.cpp is made here too (tools/check_core_shaders.py
// compares the bodies). Each function of the chain mirrors PostFx's of the
// same name; the comments say where the explicit API needs something GL did
// implicitly.

#include "render/core_post_fx.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gr = gd::core::render;

namespace {

// A full-screen triangle; vUv covers the target 0..1. Frames are stored GL
// style on the core path (CoreRenderer::setMvp negates clip y), so y is
// negated here as there: on OpenGL3 the backend's own flip cancels it and
// the triangle rasterises exactly as local's.
const char* kVertex = R"(
GD_OUT(0) vec2 vUv;
void main() {
    vec2 p = vec2(float((GD_VERTEX_ID & 1) << 2) - 1.0, float((GD_VERTEX_ID & 2) << 1) - 1.0);
    vUv = p * 0.5 + 0.5;
    gl_Position = gd_clip(vec4(p.x, -p.y, 0.0, 1.0));
}
)";

// Dual Kawase down: 5 taps around the destination texel (Marius Bjørge, SIGGRAPH 2015).
const char* kDownFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_UNIFORMS(0, Post) {
    vec2 uTexel;  // source texel
};
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kUpFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_SAMPLER2D(1, uSkip);
GD_UNIFORMS(0, Post) {
    vec2 uTexel;  // source (smaller) texel
};
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kCompositeFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);    // the scene
GD_SAMPLER2D(1, uBlur);   // the blur pyramid's top (1/2 res)
GD_SAMPLER2D(2, uBlurLo); // a wide level (1/8 res)
GD_SAMPLER2D(3, uAo);     // ambient occlusion (r) and the eye distance (g)
GD_UNIFORMS(0, Post) {
    vec2 uTexel;
    int uSsao, uGrade;
    float uBloom;      // 0 off
    float uAoK;        // the occlusion's exponent: light 0.5 .. stronger 1.5
    int uCinematic;
    float uPeak;       // HDR output: the display's headroom in paper whites; 1 = SDR (GD_HDR only)
};
GD_FRAG_OUT(0) vec4 fragColor;
#if GD_HDR  // Enhanced > HDR (PostFx::program builds this variant only for Emulated / True HDR)
GD_SAMPLER2D(4, uExposure); // the auto exposure (1x1, r)

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
const char* kAoFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uDepth);
GD_UNIFORMS(0, Post) {
    mat4 uInvViewProj, uViewProj;
    vec2 uTexel;
    float uRadius;
};
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kStarBlurFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_SAMPLER2D(1, uDepth);
GD_SAMPLER2D(2, uAccum);
GD_UNIFORMS(0, Post) {
    mat4 uInvViewProj, uPrevViewProj;
    float uScale, uRadial, uFeedback;
    int uValid;
};
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kFinishFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_UNIFORMS(0, Post) {
    vec2 uTexel;
    float uSharp, uCa, uGrain, uTime;
    float uPeak;  // True HDR: the encoded white, above 1 (GD_HDR only)
};
#if GD_HDR
#define PEAK uPeak
#else
#define PEAK 1.0
#endif
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kTaaFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_SAMPLER2D(1, uDepth);
GD_SAMPLER2D(2, uHistory);
GD_UNIFORMS(0, Post) {
    mat4 uInvViewProj, uPrevViewProj;
    vec2 uTexel, uJitterUv;
    int uReset;
};
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kFxaaFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_UNIFORMS(0, Post) {
    vec2 uTexel;
};
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kDownsampleFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_UNIFORMS(0, Post) {
    vec2 uTexel;  // source texel
    vec2 uRatio;  // source / output size
};
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kEasuFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_UNIFORMS(0, Post) {
    vec2 uInSize, uOutSize;
};
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kRcasFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_UNIFORMS(0, Post) {
    vec2 uTexel;
    float uSharp;  // exp2(-stops)
    float uPeak;   // True HDR: the encoded white, above 1 (GD_HDR only)
};
#if GD_HDR
#define PEAK uPeak
#else
#define PEAK 1.0
#endif
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kLumFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_UNIFORMS(0, Post) {
    vec2 uTexel;  // the target's texel
};
GD_FRAG_OUT(0) vec4 fragColor;
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
const char* kAdaptFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uLum);
GD_SAMPLER2D(1, uPrev);
GD_UNIFORMS(0, Post) {
    float uLevel;  // the 1x1 mip
    float uBlend;  // 1: no history
};
GD_FRAG_OUT(0) vec4 fragColor;
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

const char* kCopyFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_FRAG_OUT(0) vec4 fragColor;
void main() { fragColor = vec4(texture(uTex, vUv).rgb, 1.0); }
)";

// renderer.cpp's True HDR output pass: the frame (sRGB-encoded, paper white
// at 1.0, True HDR highlights above it) to the surface: linear scRGB (1.0 =
// 80 nits) scaled to the display's paper white, or BT.2020 PQ.
const char* kHdrOutFs = R"(
GD_IN(0) vec2 vUv;
GD_SAMPLER2D(0, uTex);
GD_UNIFORMS(0, Post) {
    float uWhite;  // paper white in scRGB units
    int uPq;
};
GD_FRAG_OUT(0) vec4 fragColor;
void main() {
    vec3 c = max(texture(uTex, vUv).rgb, 0.0);
    vec3 lin = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)) * uWhite;
    if (uPq != 0) {
        const mat3 toBt2020 = mat3(0.6274, 0.0691, 0.0164, 0.3293, 0.9195, 0.0880, 0.0433, 0.0114, 0.8956);
        vec3 y = clamp(toBt2020 * lin * (80.0 / 10000.0), 0.0, 1.0);
        vec3 p = pow(y, vec3(0.1593017578125));
        lin = pow((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), vec3(78.84375));
    }
    fragColor = vec4(lin, 1.0);
}
)";

// The "Post" blocks as std140 lays them out (scalars 4-byte aligned, vec2 8,
// mat4 16), each padded to a multiple of 16 bytes (a bound uniform range must
// cover the block's whole size).
struct Texel {
    float texel[2], pad[2];
};
struct CompositeU {
    float texel[2];
    int32_t ssao, grade;
    float bloom, aoK;
    int32_t cinematic;
    float peak;
};
struct AoU {
    float inv[16], viewProj[16];
    float texel[2], radius, pad;
};
struct StarBlurU {
    float inv[16], prev[16];
    float scale, radial, feedback;
    int32_t valid;
};
struct FinishU {
    float texel[2], sharp, ca, grain, time, peak, pad;
};
struct TaaU {
    float inv[16], prev[16];
    float texel[2], jitter[2];
    int32_t reset, pad[3];
};
struct Vec4U {
    float v[4];
};
struct RcasU {
    float texel[2], sharp, peak;
};
struct AdaptU {
    float level, blend, pad[2];
};
struct HdrOutU {
    float white;
    int32_t pq, pad[2];
};
static_assert(sizeof(CompositeU) == 32 && sizeof(AoU) == 144 && sizeof(StarBlurU) == 144 && sizeof(FinishU) == 32 && sizeof(TaaU) == 160 &&
                  sizeof(RcasU) == 16 && sizeof(AdaptU) == 16 && sizeof(HdrOutU) == 16 && sizeof(Texel) == 16,
              "std140 blocks");

// Column-major 4x4 inverse (cofactors): post_fx.cpp's.
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

CorePostFx::~CorePostFx() {
    // The device frees what it made when it goes; free the targets now in case it outlives this.
    auto drop = [&](Target& t) {
        if (t.tex) dev_.destroyTexture(t.tex);
        t = {};
    };
    for (Target* t : {&lum_, &expo_[0], &expo_[1], &a_, &b_, &history_[0], &history_[1], &out_, &out2_, &ao_, &starAccum_[0], &starAccum_[1], &fin_})
        drop(*t);
    for (int l = 0; l < kLevels; ++l) drop(down_[l]), drop(up_[l]);
    for (auto& [key, p] : pipelines_) dev_.destroyPipeline(p);
    for (gr::Shader s : shaders_)
        if (s) dev_.destroyShader(s);
}

bool CorePostFx::init() {
    if (ready_ || failed_) return ready_;
    struct Src {
        const char* name;
        const char* fs;
        bool hdr;
        const char* samplers[5];
    };
    // In PostFx::init's order (the failure log names them the same way).
    const Src src[kProgramCount] = {
        {"blur down", kDownFs, false, {"uTex"}},
        {"blur up", kUpFs, false, {"uTex", "uSkip"}},
        {"composite", kCompositeFs, false, {"uTex", "uBlur", "uBlurLo", "uAo"}},
        {"composite (HDR)", kCompositeFs, true, {"uTex", "uBlur", "uBlurLo", "uAo", "uExposure"}},
        {"finish (HDR)", kFinishFs, true, {"uTex"}},
        {"FSR RCAS (HDR)", kRcasFs, true, {"uTex"}},
        {"TAA", kTaaFs, false, {"uTex", "uDepth", "uHistory"}},
        {"FXAA", kFxaaFs, false, {"uTex"}},
        {"SSAA", kDownsampleFs, false, {"uTex"}},
        {"FSR EASU", kEasuFs, false, {"uTex"}},
        {"FSR RCAS", kRcasFs, false, {"uTex"}},
        {"copy", kCopyFs, false, {"uTex"}},
        {"ambient occlusion", kAoFs, false, {"uDepth"}},
        {"star power blur", kStarBlurFs, false, {"uTex", "uDepth", "uAccum"}},
        {"finish", kFinishFs, false, {"uTex"}},
        {"hdr luminance", kLumFs, false, {"uTex"}},
        {"hdr exposure", kAdaptFs, false, {"uLum", "uPrev"}},
        {"hdr output", kHdrOutFs, false, {"uTex"}},
    };
    for (int p = 0; p < kProgramCount; ++p) {
        gr::ShaderDesc d;
        d.name = src[p].name;
        d.glslVertex = kVertex;
        d.glslFragment = src[p].fs;
        // GD_HDR selects a shader's HDR variant (Enhanced > HDR), as PostFx::program's #define line.
        d.defines = src[p].hdr ? "#define GD_HDR 1\n" : "#define GD_HDR 0\n";
        d.uniformBlocks[0] = "Post";
        for (int s = 0; s < 5; ++s) d.samplers[s] = src[p].samplers[s];
        shaders_[p] = dev_.createShader(d);
        if (!shaders_[p]) {
            SDL_Log("post-processing %s: %s", src[p].name, dev_.lastError().c_str());
            failedPrograms_.push_back(src[p].name);
        }
    }
    // The True HDR output pass is not part of the chain: without it the frame shows unconverted.
    for (int p = 0; p < kHdrOut; ++p) failed_ = failed_ || !shaders_[p];
    gr::SamplerDesc s;
    s.mipFilter = gr::MipFilter::None;
    s.wrapU = s.wrapV = s.wrapW = gr::Wrap::ClampToEdge;
    linear_ = dev_.createSampler(s);
    s.minFilter = s.magFilter = gr::Filter::Nearest;
    nearest_ = dev_.createSampler(s);
    s.magFilter = gr::Filter::Linear, s.mipFilter = gr::MipFilter::Nearest;  // GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR
    lumSampler_ = dev_.createSampler(s);
    gr::SamplerDesc r;
    r.mipFilter = gr::MipFilter::None;
    r.wrapU = r.wrapV = r.wrapW = gr::Wrap::Repeat;
    repeat_ = dev_.createSampler(r);
    ready_ = !failed_;
    return ready_;
}

void CorePostFx::ensure(Target& t, int w, int h, bool hdr, bool mips) {
    w = std::max(w, 1), h = std::max(h, 1);
    if (t.tex && t.w == w && t.h == h && t.hdr == hdr) return;
    // Local re-specifies the texture (its contents become undefined); here it is made again.
    if (t.tex) dev_.destroyTexture(t.tex);
    gr::TextureDesc d;
    d.format = hdr ? gr::Format::RGBA16F : gr::Format::RGBA8;
    d.width = w, d.height = h;
    d.mips = mips ? 0 : 1;
    d.usage = gr::kSampledTexture | gr::kColorTarget;
    d.label = "post-processing";
    t.tex = dev_.createTexture(d);
    t.w = w, t.h = h, t.hdr = hdr;
}

gr::Pipeline CorePostFx::pipeline(Program program, gr::Format format, uint8_t writeMask, gr::Format depth) {
    const uint32_t key = uint32_t(program) | uint32_t(format) << 8 | uint32_t(writeMask) << 16 | uint32_t(depth) << 20;
    if (auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;
    gr::PipelineDesc p;
    p.shader = shaders_[program];
    p.primitive = gr::Primitive::Triangles;  // no vertex buffers: the triangle comes from the vertex id
    p.blend[0].writeMask = writeMask;        // no blending, no depth, no culling (PostFx::process's state)
    p.colorFormats[0] = format;
    p.colorCount = 1;
    p.depthFormat = depth;  // the caller's pass may have one (the blit into the frame): never tested nor written
    p.label = "post-processing";
    gr::Pipeline pl = dev_.createPipeline(p);
    pipelines_[key] = pl;
    return pl;
}

void CorePostFx::begin(const Target& t, Program program) {
    gr::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = t.tex;
    p.colors[0].load = gr::LoadOp::DontCare;  // the triangle covers the target
    p.label = "post-processing";
    dev_.beginPass(p);
    dev_.setViewport({0, 0, t.w, t.h});
    dev_.setScissor({0, 0, t.w, t.h});
    dev_.bindPipeline(pipeline(program, t.hdr ? gr::Format::RGBA16F : gr::Format::RGBA8, gr::kWriteAll));
}

void CorePostFx::bind(int slot, Texture tex, gr::Sampler sampler) {
    gr::TextureBinding b{tex, sampler};
    dev_.bindTextures(slot, &b, 1);
}

void CorePostFx::finishPass() {
    dev_.draw(3);
    dev_.endPass();
}

std::array<float, 2> CorePostFx::jitter(const PostSettings& s, int sw, int sh) {
    if (sw <= 0 || sh <= 0) return lastJitter_ = {0, 0};
    // A temporal AI upscaler does not run on a core backend: FSR 1 stands in,
    // unjittered, as local's when its upscaler failed (PostFx::jitter's upFailed_).
    const unsigned phases = s.aa == PostSettings::AA::Taa && !s.temporalUpscaler() ? 8 : 0;
    if (!phases) return lastJitter_ = {0, 0};
    unsigned i = frame_ % phases + 1;  // Halton (2, 3)
    return lastJitter_ = {(halton(i, 2) - 0.5f) * 2.0f / float(sw), (halton(i, 3) - 0.5f) * 2.0f / float(sh)};
}

void CorePostFx::blit(Texture texture, gr::Format format, uint8_t writeMask, gr::Format depth) {
    if (!init()) return;
    dev_.bindPipeline(pipeline(kCopy, format, writeMask, depth));
    bind(0, texture, linear_);
    dev_.draw(3);
}

gr::Texture CorePostFx::process(Texture color, Texture depth, int sw, int sh, int ow, int oh, const PostSettings& s, const PostFrame& f) {
    if (!init()) return {};
    if (dev_.inPass()) dev_.endPass();
    const std::array<float, 16> inv = f.hasCamera ? inverse(f.viewProj) : std::array<float, 16>{};
    Texture cur = color;
    // HDR: the pyramid keeps lights above 1.0; True HDR (peak above 1) keeps the
    // whole chain in RGBA16F after the composite. (A temporal AI upscaler,
    // which tone maps to SDR on local, does not run here: FSR 1 does.)
    const bool hdr = s.hdr != 0;
    const float peak = hdr && s.hdrPeak > 1.0f && !s.temporalUpscaler() ? std::min(s.hdrPeak, 64.0f) : 1.0f;
    const bool wide = peak > 1.0f;
    const float peakEnc = wide ? (peak <= 0.0031308f ? peak * 12.92f : 1.055f * std::pow(peak, 1 / 2.4f) - 0.055f) : 1.0f;

    // 1. The blur pyramid (dual Kawase) for bloom.
    const bool pyramid = s.bloom != 0;
    if (pyramid) {
        int w = sw, h = sh;
        Texture src = color;
        for (int l = 0; l < kLevels; ++l) {
            const Texel u{{0.5f / float(w), 0.5f / float(h)}, {0, 0}};
            w = std::max(1, w / 2), h = std::max(1, h / 2);
            ensure(down_[l], w, h, hdr);
            begin(down_[l], kDown);
            bind(0, src, linear_);
            dev_.pushUniforms(0, &u, sizeof u);
            finishPass();
            src = down_[l].tex;
        }
        for (int l = kLevels - 2; l >= 0; --l) {
            ensure(up_[l], down_[l].w, down_[l].h, hdr);
            begin(up_[l], kUp);
            const Texel u{{1.0f / float(down_[l + 1].w), 1.0f / float(down_[l + 1].h)}, {0, 0}};
            bind(0, src, linear_);
            bind(1, down_[l].tex, linear_);
            dev_.pushUniforms(0, &u, sizeof u);
            finishPass();
            src = up_[l].tex;
        }
    }
    // Ambient occlusion at render resolution (16-bit: the eye distance rides along for the blur).
    const bool ssao = s.ssao && f.hasCamera;
    if (ssao) {
        ensure(ao_, sw, sh, true);
        begin(ao_, kAo);
        bind(0, depth, nearest_);
        AoU u{};
        std::copy(inv.begin(), inv.end(), u.inv);
        std::copy(f.viewProj.begin(), f.viewProj.end(), u.viewProj);
        u.texel[0] = 1.0f / float(sw), u.texel[1] = 1.0f / float(sh);
        u.radius = 14.0f;  // world units; a rocker is ~80 tall
        dev_.pushUniforms(0, &u, sizeof u);
        finishPass();
    }
    if (hdr) autoExposure(color);
    // 2. Ambient occlusion, bloom, (HDR) exposure and tone map, colour.
    if (pyramid || ssao || s.cinematic || s.grade || hdr) {
        ensure(a_, sw, sh, wide);
        begin(a_, hdr ? kCompositeHdr : kComposite);
        // Every slot local binds, with the scene in those it does not use (the same textures, the same samplers).
        bind(0, color, linear_);
        bind(1, pyramid ? up_[0].tex : color, linear_);
        bind(2, pyramid ? up_[2].tex : color, linear_);
        bind(3, ssao ? ao_.tex : color, linear_);
        bind(4, hdr ? expo_[expoIndex_].tex : color, linear_);
        static constexpr float kAoK[5] = {0, 0.5f, 0.8f, 1.1f, 1.5f};  // none, light, medium, strong, stronger
        CompositeU u{};
        u.texel[0] = 1.0f / float(sw), u.texel[1] = 1.0f / float(sh);
        u.ssao = ssao ? 1 : 0;
        u.grade = s.grade;
        u.bloom = !pyramid ? 0.0f : s.bloom >= 3 ? 0.9f : s.bloom == 2 ? 0.68f : s.bloom == 1 ? 0.45f : 0.0f;
        u.aoK = kAoK[std::clamp(s.ssao, 0, 4)];
        u.cinematic = s.cinematic ? 1 : 0;
        u.peak = peak;
        dev_.pushUniforms(0, &u, sizeof u);
        finishPass();
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
        begin(next, kStarBlur);
        bind(0, cur, linear_);
        bind(1, depth, nearest_);
        bind(2, prev.tex, linear_);
        StarBlurU u{};
        std::copy(inv.begin(), inv.end(), u.inv);
        std::copy(starPrevViewProj_.begin(), starPrevViewProj_.end(), u.prev);
        u.scale = kScale[lv] * f.starBlur;
        u.radial = kRadial[lv] * f.starBlur;
        u.feedback = kFeedback[lv] * f.starBlur;
        u.valid = valid ? 1 : 0;
        dev_.pushUniforms(0, &u, sizeof u);
        finishPass();
        cur = next.tex;
        starIndex_ ^= 1;
        starValid_ = true;
        starPrevViewProj_ = f.viewProj;
    } else {
        starValid_ = false;
    }

    // 3-4 with a temporal AI upscaler: not on a core backend (GL <-> Vulkan
    // interop); FSR 1 in its place, as local when it cannot run.
    if (s.temporalUpscaler())
        upscalerStatus_ = std::string(s.upscaler == PostSettings::Upscaler::Dlss4 ? "DLSS 4" : "FSR 3.1") + " is not on the " +
                          gr::backendId(dev_.backend()) + " graphics backend yet - using FSR 1";
    else
        upscalerStatus_ = s.upscaler == PostSettings::Upscaler::Fsr1 ? "running FSR 1" : "";
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
        begin(next, kTaa);
        bind(0, cur, linear_);
        bind(1, depth, nearest_);
        bind(2, prev.tex, linear_);
        TaaU u{};
        std::copy(inv.begin(), inv.end(), u.inv);
        std::copy(prevViewProj_.begin(), prevViewProj_.end(), u.prev);
        u.texel[0] = 1.0f / float(sw), u.texel[1] = 1.0f / float(sh);
        std::array<float, 2> j = jitter(s, sw, sh);
        u.jitter[0] = j[0] * 0.5f, u.jitter[1] = j[1] * 0.5f;
        u.reset = reset ? 1 : 0;
        dev_.pushUniforms(0, &u, sizeof u);
        finishPass();
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
        begin(dst, kFxaa);
        bind(0, cur, linear_);
        const Texel u{{1.0f / float(sw), 1.0f / float(sh)}, {0, 0}};
        dev_.pushUniforms(0, &u, sizeof u);
        finishPass();
        cur = dst.tex;
    }

    // 4. To the output size.
    if (sw > ow || sh > oh) {  // SSAA (with FSR quality at 2x: still a downsample)
        ensure(out_, ow, oh, wide);
        begin(out_, kDownsample);
        bind(0, cur, linear_);
        const Vec4U u{{1.0f / float(sw), 1.0f / float(sh), float(sw) / float(ow), float(sh) / float(oh)}};
        dev_.pushUniforms(0, &u, sizeof u);
        finishPass();
        cur = out_.tex;
    } else if (sw < ow || sh < oh) {  // FSR 1: EASU then RCAS
        ensure(out_, ow, oh, wide);
        begin(out_, kEasu);
        bind(0, cur, linear_);
        const Vec4U e{{float(sw), float(sh), float(ow), float(oh)}};
        dev_.pushUniforms(0, &e, sizeof e);
        finishPass();
        ensure(out2_, ow, oh, wide);
        begin(out2_, wide ? kRcasHdr : kRcas);
        bind(0, out_.tex, linear_);
        const RcasU r{{1.0f / float(ow), 1.0f / float(oh)}, std::exp2(-0.2f), peakEnc};  // FSR's default 0.2 stops
        dev_.pushUniforms(0, &r, sizeof r);
        finishPass();
        cur = out2_.tex;
    }
    ++frame_;
    peakEnc_ = peakEnc;
    return finish(cur, ow, oh, s, f);
}

gr::Texture CorePostFx::finish(Texture cur, int ow, int oh, const PostSettings& s, const PostFrame& f) {
    if (!s.sharpen && !s.chromatic && !s.grain) return cur;
    ensure(fin_, ow, oh, peakEnc_ > 1.0f);
    begin(fin_, peakEnc_ > 1.0f ? kFinishHdr : kFinish);
    bind(0, cur, linear_);
    static constexpr float kCa[4] = {0, 0.006f, 0.012f, 0.02f};             // none, light, medium, strong
    static constexpr float kGrain[5] = {0, 0.03f, 0.045f, 0.07f, 0.095f};  // none, light, medium, strong, stronger
    FinishU u{};
    u.texel[0] = 1.0f / float(ow), u.texel[1] = 1.0f / float(oh);
    u.sharp = s.sharpen >= 2 ? 1.0f : s.sharpen == 1 ? 0.45f : 0.0f;
    u.ca = kCa[std::clamp(s.chromatic, 0, 3)];
    u.grain = kGrain[std::clamp(s.grain, 0, 4)];
    u.time = f.timeSec;
    u.peak = peakEnc_;
    dev_.pushUniforms(0, &u, sizeof u);
    finishPass();
    return fin_.tex;
}

// HDR auto exposure: the scene's mean log luminance (a 128 x 128 log map
// mip-reduced to 1x1 on the GPU, no read-back) and the exposure eased
// towards it (about a second), in a 1x1 target the composite reads.
void CorePostFx::autoExposure(Texture color) {
    constexpr int kSize = 128, kTop = 7;  // 128 -> 1 in 7 mips
    ensure(lum_, kSize, kSize, true, true);
    begin(lum_, kLum);
    bind(0, color, linear_);
    const Texel lu{{1.0f / kSize, 1.0f / kSize}, {0, 0}};
    dev_.pushUniforms(0, &lu, sizeof lu);
    finishPass();
    dev_.generateMipmaps(lum_.tex);

    const uint64_t now = SDL_GetTicksNS();
    const float dt = expoValid_ ? std::clamp(float(now - expoNs_) / 1e9f, 0.0f, 0.25f) : 0.0f;
    expoNs_ = now;
    Target& prev = expo_[expoIndex_];
    Target& next = expo_[expoIndex_ ^ 1];
    ensure(prev, 1, 1, true);
    ensure(next, 1, 1, true);
    begin(next, kAdapt);
    bind(0, lum_.tex, lumSampler_);
    bind(1, prev.tex, linear_);
    const AdaptU u{float(kTop), expoValid_ ? 1.0f - std::exp(-dt * 1.5f) : 1.0f, {0, 0}};
    dev_.pushUniforms(0, &u, sizeof u);
    finishPass();
    expoIndex_ ^= 1;
    expoValid_ = true;
}

bool CorePostFx::hdrOutput(Texture src, Texture dst, int w, int h, float white, bool pq) {
    init();
    if (!shaders_[kHdrOut]) return false;
    if (dev_.inPass()) dev_.endPass();
    const gr::Format format = dev_.textureDesc(dst).format;
    gr::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = dst;
    p.colors[0].load = gr::LoadOp::DontCare;
    p.label = "hdr output";
    dev_.beginPass(p);
    dev_.setViewport({0, 0, w, h});
    dev_.setScissor({0, 0, w, h});
    dev_.bindPipeline(pipeline(kHdrOut, format, gr::kWriteAll));
    bind(0, src, repeat_);
    const HdrOutU u{white, pq ? 1 : 0, {0, 0}};
    dev_.pushUniforms(0, &u, sizeof u);
    finishPass();
    return true;
}
