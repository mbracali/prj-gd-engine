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

// Ambient occlusion, fog, bloom, light streaks and colour in one pass at render resolution.
const char* kComposite = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;    // the scene
uniform sampler2D uDepth;
uniform sampler2D uBlur;   // the blur pyramid's top (1/2 res)
uniform sampler2D uBlurMid; // 1/4 res
uniform sampler2D uStreakSrc; // the first 1/2 res step down: the light streaks' source
uniform sampler2D uBlurLo; // a wide level (1/8 res): the fog's light
uniform sampler2D uAo;     // ambient occlusion (r) and the eye distance (g)
uniform sampler2D uFogVol; // Super Fog: in-scattered light (rgb), transmittance (a)
uniform int uFog, uSsao, uGrade;
uniform mat4 uInvViewProj;
uniform float uFloorZ, uFogHeight, uFogDensity, uFogMax, uTime;
uniform float uBloom;      // 0 off
uniform float uStreaks;    // 0 off
uniform vec2 uTexel, uTexelMid;  // uTexelMid: uStreakSrc's texel
uniform int uCinematic;
out vec4 fragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
vec3 world(vec2 uv, float d) {
    vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
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
        c *= sum / wsum;
    }
    if (uFog == 3) {
        // Super Fog: the half-resolution volume (light shafts, glow and the ground layer).
        vec4 v = texture(uFogVol, vUv);
        c = c * v.a + v.rgb;
    } else if (uFog != 0) {
        // Exponential height fog on the stage floor, integrated along the
        // view ray from the near plane (heights clamped at the floor, so the
        // pit in front of the stage holds the floor's density).
        float d = min(texture(uDepth, vUv).r, 0.99999);
        vec3 o = world(vUv, 0.0), p = world(vUv, d);
        vec3 ray = p - o;
        float dist = min(length(ray), 3000.0);
        p = o + normalize(ray) * dist;
        float h0 = max(o.z - uFloorZ, 0.0) / uFogHeight, h1 = max(p.z - uFloorZ, 0.0) / uFogHeight;
        float k = abs(h1 - h0) < 1e-3 ? exp(-h0) : (exp(-h0) - exp(-h1)) / (h1 - h0);
        // Rolling density: three octaves of domain-warped value noise on the floor plane.
        vec2 q = p.xy * 0.012 + uTime * vec2(0.035, 0.012);
        vec2 warp = vec2(noise(q * 0.7 + 3.1), noise(q * 0.7 - 1.7)) - 0.5;
        q += warp * 1.4;
        float n = noise(q) * 0.55 + noise(q * 2.3 - uTime * vec2(0.02, 0.05)) * 0.3 + noise(q * 5.1 + uTime * 0.04) * 0.15;
        float amount = uFogDensity * dist * k * mix(0.35, 1.7, n);
        float f = (1.0 - exp(-amount)) * uFogMax;
        // Lit by the stage: the wide blur of the frame there, the bright lights glowing through it.
        vec3 light = texture(uBlurLo, vUv).rgb;
        vec3 glow = max(texture(uBlurMid, vUv).rgb - 0.35, 0.0);
        vec3 fogColor = vec3(0.09, 0.09, 0.11) + light * 1.3 + glow * 1.2;
        c = mix(c, fogColor, f);
    }
    if (uBloom > 0.0) {
        // Bright parts of the blurred frame (threshold after the blur: shares the fog's pyramid).
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
    if (uStreaks > 0.0) {
        // Anamorphic streaks: the brightest lights smeared sideways, blue-tinted.
        vec3 s = vec3(0.0);
        for (int i = -24; i <= 24; ++i) {
            vec3 t = texture(uStreakSrc, vUv + vec2(float(i) * uTexelMid.x, 0.0)).rgb;
            float m = max(t.r, max(t.g, t.b));
            s += t * (max(m - 0.8, 0.0) / max(m, 1e-3)) * exp(-abs(float(i)) * 0.09);  // only the lights themselves, not lit skin
        }
        // Half the light's own colour, half a cool blue-white.
        c += (s * 0.5 + vec3(luma(s)) * vec3(0.55, 0.8, 1.3) * 0.7) * uStreaks;
    }
    if (uCinematic != 0) {
        float l = luma(c);
        c = mix(vec3(l), c, 1.12);                        // a little more saturation
        c = clamp(c, 0.0, 1.0);
        c = mix(c, c * c * (3.0 - 2.0 * c), 0.4);         // filmic S-curve (toe and shoulder)
        c += vec3(0.025, 0.006, -0.02) * l - vec3(0.012, -0.004, -0.022) * (1.0 - l) * 0.6;  // warm highs, teal lows
        float v = length((vUv - 0.5) * vec2(1.25, 1.0));
        c *= mix(1.0, 0.7, smoothstep(0.35, 0.9, v));     // vignette
    }
    if (uGrade != 0) c = grade(clamp(c, 0.0, 1.0), uGrade);
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
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

// Super Fog (half resolution): the view ray marched to the scene through a
// ground fog layer and a thin room haze, both stirred by animated 3D noise,
// lit by the stage's light cones (Henyey-Greenstein forward scattering, so
// the beams glow looking into the lights) and by the frame's own light.
// Out: the in-scattered light and the transmittance.
const char* kFogVol = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uDepth;
uniform sampler2D uBlurLo;
uniform mat4 uInvViewProj;
uniform float uFloorZ, uTime;
uniform int uLights;
uniform vec3 uLightPos[8], uLightDir[8], uLightCol[8];
uniform vec2 uLightCone[8];  // cos of the inner and outer half-angles
out vec4 fragColor;
vec3 world(vec2 uv, float d) {
    vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
// Dave Hoskins' hash without sine (cheap: the march calls it 8 times per octave per step).
float hash3(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
// Not "noise3": GLSL reserves noise1..noise4 as built-ins (vec3 noise3(genType)),
// and Apple's compiler rejects a redeclaration with another return type.
float fogNoise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), u.x), mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), u.x), u.y),
               mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), u.x), mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), u.x), u.y),
               u.z);
}
void main() {
    float d = min(texture(uDepth, vUv).r, 0.99999);
    vec3 o = world(vUv, 0.0), p = world(vUv, d);
    vec3 ray = p - o;
    float dist = min(length(ray), 2500.0);
    vec3 dir = normalize(ray);
    const int kSteps = 24;  // with the per-pixel jitter, enough for soft shafts
    float stepLen = dist / float(kSteps);
    float t = stepLen * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec3 ambient = vec3(0.07, 0.07, 0.085) + texture(uBlurLo, vUv).rgb * 1.5;
    vec3 inScatter = vec3(0.0);
    float trans = 1.0;
    const float g = 0.55;  // forward scattering
    for (int i = 0; i < kSteps; ++i, t += stepLen) {
        vec3 q = o + dir * t;
        float h = max(q.z - uFloorZ, 0.0);
        vec3 nq = q * 0.011 + vec3(uTime * 0.05, uTime * 0.02, -uTime * 0.03);
        // The second octave only in the ground layer, where the billows show.
        float n = h < 80.0 ? fogNoise3(nq) * 0.6 + fogNoise3(nq * 2.6 + 7.3) * 0.4 : fogNoise3(nq);
        float ground = 0.009 * exp(-h / 26.0) * mix(0.25, 1.9, n);
        float haze = 0.00055 * mix(0.6, 1.4, n);
        float dens = ground + haze;
        vec3 li = ambient;
        float near = smoothstep(40.0, 220.0, t);  // no beam glare right at the lens (close-ups inside a cone)
        for (int k = 0; k < 8; ++k) {
            if (k >= uLights) break;
            vec3 v = q - uLightPos[k];
            float dl = length(v);
            vec3 lv = v / max(dl, 1e-3);
            float cone = smoothstep(uLightCone[k].y, uLightCone[k].x, dot(lv, uLightDir[k]));
            if (cone <= 0.0) continue;
            float mu = dot(lv, -dir);
            float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * mu, 1.5) * 0.25;
            li += uLightCol[k] * cone * phase * 40.0 * near / (1.0 + dl * dl * 1.5e-5);
        }
        float sigma = dens * stepLen;
        inScatter += trans * li * sigma;
        trans *= exp(-sigma);
    }
    // Keep the band readable: a soft limit on the glow, and never fully fogged.
    inScatter = inScatter / (1.0 + max(max(inScatter.r, inScatter.g), inScatter.b) * 0.7);
    fragColor = vec4(inScatter, max(trans, 0.3));
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
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec3 e = texture(uTex, vUv).rgb;
    vec3 c = e;
    if (uSharp > 0.0) {
        vec3 b = texture(uTex, vUv + vec2(0, -uTexel.y)).rgb, dd = texture(uTex, vUv + vec2(-uTexel.x, 0)).rgb;
        vec3 f = texture(uTex, vUv + vec2(uTexel.x, 0)).rgb, h = texture(uTex, vUv + vec2(0, uTexel.y)).rgb;
        vec3 mn = min(e, min(min(b, dd), min(f, h))), mx = max(e, max(max(b, dd), max(f, h)));
        vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-4), 0.0, 1.0));
        vec3 w = amp * (-1.0 / mix(8.0, 5.0, uSharp));
        c = clamp((e + (b + dd + f + h) * w) / (1.0 + 4.0 * w), 0.0, 1.0);
    }
    if (uCa > 0.0) {
        vec2 off = (vUv - 0.5) * uCa * length(vUv - 0.5);
        c.r += texture(uTex, vUv + off).r - e.r;
        c.b += texture(uTex, vUv - off).b - e.b;
    }
    if (uGrain > 0.0) {
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        float n = hash(floor(gl_FragCoord.xy) + fract(uTime * 7.31) * 413.0) + hash(floor(gl_FragCoord.xy) * 1.37 - fract(uTime * 3.17) * 211.0) - 1.0;
        c += n * uGrain * (0.35 + 2.6 * l * (1.0 - l));
    }
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
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
out vec4 fragColor;
void main() {
    vec3 b = texture(uTex, vUv + vec2(0, -uTexel.y)).rgb, d = texture(uTex, vUv + vec2(-uTexel.x, 0)).rgb;
    vec3 e = texture(uTex, vUv).rgb;
    vec3 f = texture(uTex, vUv + vec2(uTexel.x, 0)).rgb, h = texture(uTex, vUv + vec2(0, uTexel.y)).rgb;
    vec3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));
    vec3 hitMin = mn4 / max(4.0 * mx4, 1e-4);
    vec3 hitMax = (1.0 - mx4) / min(4.0 * mn4 - 4.0, -1e-4);
    vec3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * uSharp;
    fragColor = vec4((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0), 1.0);
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

unsigned PostFx::program(const char* fragment, const char* name) {
    std::string error;
    unsigned vs = compileShader(GL_VERTEX_SHADER, kVertex, error);
    unsigned fs = vs ? compileShader(GL_FRAGMENT_SHADER, fragment, error) : 0;
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
    pTaa_ = program(kTaa, "TAA");
    pFxaa_ = program(kFxaa, "FXAA");
    pDownsample_ = program(kDownsample, "SSAA");
    pEasu_ = program(kEasu, "FSR EASU");
    pRcas_ = program(kRcas, "FSR RCAS");
    pCopy_ = program(kCopy, "copy");
    pAo_ = program(kAo, "ambient occlusion");
    pFogVol_ = program(kFogVol, "super fog");
    pStarBlur_ = program(kStarBlur, "star power blur");
    pFinish_ = program(kFinish, "finish");
    failed_ = !(pDown_ && pUp_ && pComposite_ && pTaa_ && pFxaa_ && pDownsample_ && pEasu_ && pRcas_ && pCopy_ && pAo_ && pFogVol_ &&
                pStarBlur_ && pFinish_);
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

    // 1. The blur pyramid (dual Kawase) for bloom, the fog's light and the light streaks.
    const bool pyramid = s.bloom || s.streaks || (s.fog && f.hasCamera);
    if (pyramid) {
        int w = sw, h = sh;
        unsigned src = color;
        gl::glUseProgram(pDown_);
        gl::glUniform1i(uniform(pDown_, "uTex"), 0);
        for (int l = 0; l < kLevels; ++l) {
            gl::glUniform2f(uniform(pDown_, "uTexel"), 0.5f / float(w), 0.5f / float(h));
            w = std::max(1, w / 2), h = std::max(1, h / 2);
            ensure(down_[l], w, h, false);
            bindTarget(down_[l]);
            bindTex(0, src);
            drawQuad();
            src = down_[l].tex;
        }
        gl::glUseProgram(pUp_);
        gl::glUniform1i(uniform(pUp_, "uTex"), 0);
        gl::glUniform1i(uniform(pUp_, "uSkip"), 1);
        for (int l = kLevels - 2; l >= 0; --l) {
            ensure(up_[l], down_[l].w, down_[l].h, false);
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
    // Super Fog's volume at half resolution (16-bit: the transmittance in alpha).
    const bool superFog = s.fog >= 3 && f.hasCamera;
    if (superFog) {
        ensure(fogVol_, std::max(1, sw / 2), std::max(1, sh / 2), true);
        bindTarget(fogVol_);
        gl::glUseProgram(pFogVol_);
        gl::glUniform1i(uniform(pFogVol_, "uDepth"), 0);
        gl::glUniform1i(uniform(pFogVol_, "uBlurLo"), 1);
        bindTex(0, depth);
        bindTex(1, up_[2].tex);
        gl::glUniformMatrix4fv(uniform(pFogVol_, "uInvViewProj"), 1, GL_FALSE, inv.data());
        gl::glUniform1f(uniform(pFogVol_, "uFloorZ"), f.floorZ);
        gl::glUniform1f(uniform(pFogVol_, "uTime"), f.timeSec);
        float pos[24] = {}, dir[24] = {}, col[24] = {}, cone[16] = {};
        int n = 0;
        for (const PostFrame::Light& l : f.lights) {
            if (n == 8) break;
            float d[3] = {l.target[0] - l.pos[0], l.target[1] - l.pos[1], l.target[2] - l.pos[2]};
            float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 1) continue;
            for (int k = 0; k < 3; ++k) pos[n * 3 + k] = l.pos[size_t(k)], dir[n * 3 + k] = d[k] / len, col[n * 3 + k] = l.color[size_t(k)];
            // A pool ~60 units across where it lands, as the floor spots.
            float half = std::atan(60.0f / len);
            cone[n * 2] = std::cos(half * 0.55f), cone[n * 2 + 1] = std::cos(half);
            ++n;
        }
        gl::glUniform1i(uniform(pFogVol_, "uLights"), n);
        gl::glUniform3fv(uniform(pFogVol_, "uLightPos"), 8, pos);
        gl::glUniform3fv(uniform(pFogVol_, "uLightDir"), 8, dir);
        gl::glUniform3fv(uniform(pFogVol_, "uLightCol"), 8, col);
        gl::glUniform2fv(uniform(pFogVol_, "uLightCone"), 8, cone);
        drawQuad();
    }

    // 2. Ambient occlusion, fog, bloom, light streaks, colour.
    if (pyramid || ssao || s.cinematic || s.grade) {
        ensure(a_, sw, sh, false);
        bindTarget(a_);
        gl::glUseProgram(pComposite_);
        gl::glUniform1i(uniform(pComposite_, "uTex"), 0);
        gl::glUniform1i(uniform(pComposite_, "uDepth"), 1);
        gl::glUniform1i(uniform(pComposite_, "uBlur"), 2);
        gl::glUniform1i(uniform(pComposite_, "uBlurLo"), 3);
        gl::glUniform1i(uniform(pComposite_, "uBlurMid"), 4);
        gl::glUniform1i(uniform(pComposite_, "uAo"), 5);
        gl::glUniform1i(uniform(pComposite_, "uFogVol"), 6);
        gl::glUniform1i(uniform(pComposite_, "uStreakSrc"), 7);
        bindTex(0, color);
        bindTex(1, depth);
        bindTex(2, pyramid ? up_[0].tex : color);
        bindTex(3, pyramid ? up_[2].tex : color);
        bindTex(4, pyramid ? up_[1].tex : color);
        bindTex(5, ssao ? ao_.tex : color);
        bindTex(6, superFog ? fogVol_.tex : color);
        bindTex(7, pyramid ? down_[0].tex : color);
        const int fog = f.hasCamera ? s.fog : 0;
        gl::glUniform1i(uniform(pComposite_, "uFog"), fog);
        gl::glUniform1i(uniform(pComposite_, "uSsao"), ssao ? 1 : 0);
        gl::glUniformMatrix4fv(uniform(pComposite_, "uInvViewProj"), 1, GL_FALSE, inv.data());
        // Low: knee-high wisps; full: a thicker layer to the waist (rockers are ~80 units tall).
        gl::glUniform1f(uniform(pComposite_, "uFloorZ"), f.floorZ);
        gl::glUniform1f(uniform(pComposite_, "uFogHeight"), s.fog >= 2 ? 24.0f : 13.0f);
        gl::glUniform1f(uniform(pComposite_, "uFogDensity"), s.fog >= 2 ? 0.0065f : 0.004f);
        gl::glUniform1f(uniform(pComposite_, "uFogMax"), s.fog >= 2 ? 0.88f : 0.68f);
        gl::glUniform1f(uniform(pComposite_, "uTime"), f.timeSec);
        gl::glUniform1f(uniform(pComposite_, "uBloom"), !pyramid ? 0.0f : s.bloom >= 3 ? 0.9f : s.bloom == 2 ? 0.68f : s.bloom == 1 ? 0.45f : 0.0f);
        gl::glUniform1f(uniform(pComposite_, "uStreaks"), s.streaks && pyramid ? 1.1f : 0.0f);
        gl::glUniform2f(uniform(pComposite_, "uTexel"), 1.0f / float(sw), 1.0f / float(sh));
        gl::glUniform2f(uniform(pComposite_, "uTexelMid"), pyramid ? 1.0f / float(down_[0].w) : 0.0f, pyramid ? 1.0f / float(down_[0].h) : 0.0f);
        gl::glUniform1i(uniform(pComposite_, "uCinematic"), s.cinematic ? 1 : 0);
        gl::glUniform1i(uniform(pComposite_, "uGrade"), s.grade);
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
        // Light, medium, high: the motion smear's gain, the zoom and the trail.
        static constexpr float kScale[4] = {0, 2.0f, 3.5f, 5.0f}, kRadial[4] = {0, 0.008f, 0.016f, 0.026f},
                               kFeedback[4] = {0, 0.35f, 0.55f, 0.7f};
        const int lv = std::clamp(s.starBlur, 1, 3);
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
        ensure(dst, sw, sh, false);
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
        ensure(out_, ow, oh, false);
        bindTarget(out_);
        gl::glUseProgram(pDownsample_);
        gl::glUniform1i(uniform(pDownsample_, "uTex"), 0);
        gl::glUniform2f(uniform(pDownsample_, "uTexel"), 1.0f / float(sw), 1.0f / float(sh));
        gl::glUniform2f(uniform(pDownsample_, "uRatio"), float(sw) / float(ow), float(sh) / float(oh));
        bindTex(0, cur);
        drawQuad();
        cur = out_.tex;
    } else if (sw < ow || sh < oh) {  // FSR 1: EASU then RCAS
        ensure(out_, ow, oh, false);
        bindTarget(out_);
        gl::glUseProgram(pEasu_);
        gl::glUniform1i(uniform(pEasu_, "uTex"), 0);
        gl::glUniform2f(uniform(pEasu_, "uInSize"), float(sw), float(sh));
        gl::glUniform2f(uniform(pEasu_, "uOutSize"), float(ow), float(oh));
        bindTex(0, cur);
        drawQuad();
        ensure(out2_, ow, oh, false);
        bindTarget(out2_);
        gl::glUseProgram(pRcas_);
        gl::glUniform1i(uniform(pRcas_, "uTex"), 0);
        gl::glUniform2f(uniform(pRcas_, "uTexel"), 1.0f / float(ow), 1.0f / float(oh));
        gl::glUniform1f(uniform(pRcas_, "uSharp"), std::exp2(-0.2f));  // FSR's default 0.2 stops
        bindTex(0, out_.tex);
        drawQuad();
        cur = out2_.tex;
    }
    gl::glActiveTexture(GL_TEXTURE0);
    ++frame_;
    return finish(cur, ow, oh, s, f);
}

unsigned PostFx::finish(unsigned cur, int ow, int oh, const PostSettings& s, const PostFrame& f) {
    if (!s.sharpen && !s.chromatic && !s.grain) return cur;
    ensure(fin_, ow, oh, false);
    bindTarget(fin_);
    gl::glUseProgram(pFinish_);
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glBindTexture(GL_TEXTURE_2D, cur);
    gl::glUniform1i(gl::glGetUniformLocation(pFinish_, "uTex"), 0);
    gl::glUniform2f(gl::glGetUniformLocation(pFinish_, "uTexel"), 1.0f / float(ow), 1.0f / float(oh));
    gl::glUniform1f(gl::glGetUniformLocation(pFinish_, "uSharp"), s.sharpen >= 2 ? 1.0f : s.sharpen == 1 ? 0.45f : 0.0f);
    gl::glUniform1f(gl::glGetUniformLocation(pFinish_, "uCa"), s.chromatic ? 0.012f : 0.0f);
    gl::glUniform1f(gl::glGetUniformLocation(pFinish_, "uGrain"), s.grain >= 2 ? 0.09f : s.grain == 1 ? 0.045f : 0.0f);
    gl::glUniform1f(gl::glGetUniformLocation(pFinish_, "uTime"), f.timeSec);
    drawQuad();
    return fin_.tex;
}
