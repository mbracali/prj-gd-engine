// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/xaudio_reverb.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// The Xbox 360 XAudio reverb (effect id 3) as statically linked into GH2,
// ported operation for operation. Every single-precision rounding of the
// original is kept: a separate PowerPC fmuls + fadds stays two roundings, an
// fmadds is one rounding (fmadd() below), and the compiler's quirks are
// reproduced where they change the rounding (some unrolled loops use a
// different but algebraically equal formula for some samples). It was
// checked sample for sample against the original machine code run in a
// PowerPC interpreter (identical output for impulses and noise, mono and
// stereo, for "plate", "small_room" and other presets).
//
// Object layout: the effect object (0x4BAB0 bytes, factory 0x826AD5D8 /
// 0x826AD4D0) holds the network ("core") at +0xD0; offsets below are from
// the core [C].
//
// Per 256-frame block (process 0x826AB9C0, called by 0x826AD0E0 <- the
// effect's Process 0x826AD2F0) [C]:
//   for each input half (L = in[0], R = in[1]; mono: both = in[0]):
//     1. XfadeDelay   (0x826AA2D0, +0x68 / +0x10088): the reflections delay,
//        16384 samples, cross-fading over 5000 samples to a new length.
//     2. RoomFilter   (0x826AA970, +0x200A8 / +0x200C8): two cascaded
//        one-pole low-passes (room / roomHF / HF reference).
//     3. Tap3         (0x826AAAA0, +0x200F0 / +0x247B8): 512-sample line,
//        out1 = 0.2 x[d0] + 0.4 x[d0-1], out2 = c3 x[1].
//     4. Allpass      (0x826AAB70, +0x2091C / +0x24FE4): early diffusion.
//     5. Tap3         (0x826AAE08, +0x20B34 / +0x251FC): 2048-sample line,
//        out1 = 0.2 x[d0] + 0.6 x[d0-1], out2 = c3 x[600].
//     6. Allpass      (0x826AAED8, +0x22B60 / +0x27228), in place.
//     7. Allpass      (0x826AB1B8, +0x22F78 / +0x27640).
//     8. Tap2         (0x826AB320, +0x23790 / +0x27E58): 1024-sample line,
//        two scaled taps feeding the late field.
//   early mix (inline in 0x826AB9C0): the two halves cross-mixed, times the
//     reflections gain (+0x60).
//   late field (inline loop in 0x826AB9C0): 12 all-passes, 4 two-tap delay
//     lines (the reverb delay) and 10 short two-sample-memory filters
//     (the decay EQ), per sample.
//   4 all-passes (0x826AB048, +0x49130 .. +0x4A978) on the front pair,
//   2 XfadeDelays (0x826AB3C8, +0x4B190 / +0x4B5B0): the rear delay.
//   output (VMX128 loop at 0x826ACF54): FL/FR = (1-wet) dry + wet front,
//     SL/SR = 2 wet rear; C and LFE untouched.
// Most stages write their output one sample late: out[0] is the previous
// block's last sample (a "carry"), out[1..256] this block's; the next stage
// reads out[0..255]. Kept as is.
//
// Parameters: 0x826B6A18 (I3DL2, effect parameter 1) -> 0x826B6740 (I3DL2
// -> the native XAudio parameters, the same mapping as XAudio2's
// ReverbConvertI3DL2ToNative) -> 0x826B4D48 (native -> the network: prime
// delay lengths 0x826BDC20, gains, EQ tables at 0x820D9C54..0x820DA858).

// Keep every multiply and add rounded on its own, as on the Xenon FPU.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace {

// PowerPC fmadds: a * c + b rounded once to single [C]. The product of two
// floats is exact in double; the sum is rounded to odd so that the final
// conversion to float is correctly rounded (no double rounding).
inline float fmadd(float a, float c, float b) {
    const double p = double(a) * double(c), q = double(b);
    double s = p + q;
    const double bv = s - p;
    const double err = (p - (s - bv)) + (q - bv);
    if (err != 0.0 && std::isfinite(s)) {
        uint64_t u;
        std::memcpy(&u, &s, 8);
        if ((u & 1) == 0) {
            u += ((err > 0) == (s > 0)) ? 1 : uint64_t(-1);
            std::memcpy(&s, &u, 8);
        }
    }
    return float(s);
}

// fnmsubs: b - a * c, one rounding.
inline float fnmsub(float a, float c, float b) { return fmadd(-a, c, b); }

// fsqrts of a double operand: the square root correctly rounded to single.
inline float sqrtSingle(double x) {
    float s = float(std::sqrt(x));
    const float hi = std::nextafter(s, 1e30f), lo = std::nextafter(s, 0.0f);
    const double mh = (double(s) + double(hi)) * 0.5, ml = (double(s) + double(lo)) * 0.5;
    if (mh * mh < x) return hi;
    if (ml * ml > x) return lo;
    return s;
}

// fctidz / fctiwz then the low 32 bits.
inline uint32_t truncU32(float f) { return uint32_t(int64_t(f)); }

// Xenon's VMX unit runs in non-IEEE mode: denormal inputs and results are 0.
inline float ftz(float x) { return std::fabs(x) < 1.17549435e-38f ? 0.0f : x; }

// 0x826BDC20: a length rounded to the table [1, 2, 3, 5, ..., 9973] (1 and
// the primes below 10000, 0x820DAD00) by this exact binary search, which
// stops when the midpoint repeats (not always on the nearest prime) [C].
uint32_t primeLength(uint32_t n) {
    static const std::vector<uint32_t> table = [] {
        std::vector<uint32_t> t{1};
        for (uint32_t p = 2; p < 10000; ++p) {
            bool prime = true;
            for (uint32_t q = 2; q * q <= p && prime; ++q) prime = p % q != 0;
            if (prime) t.push_back(p);
        }
        return t;
    }();
    if (n <= 1) return 1;
    if (n >= 9973) return 9973;
    uint32_t lo = 0, hi = 0x4CD, mid = 1, prevMid;
    do {
        prevMid = mid;
        mid = (lo + hi) >> 1;
        const uint32_t v = table[mid];
        if (v == n) break;
        if (v > n) hi = mid - 1;
        else lo = mid + 1;
    } while (mid != prevMid);
    return table[mid];
}

constexpr float kFadeStep = 0.000199999995f;  // 0x820D9810
constexpr float kFadeBlockStep = 0.0512f;     // 0x820D980C (256 x the step)

// 0x826AA2D0 (reflections, 16384 samples, VMX copy path) and 0x826AB3C8
// (rear, 256 samples, no VMX path) [C]: a delay line that cross-fades from
// the old length (prev, weight x) to the new one (cur, weight 1 - x), x
// falling by 0.0002 a sample. A new target is taken when a fade ends. x keeps
// falling below 0 after that; the blocks that do not take the plain-copy path
// then compute x * a + (1 - x) * a with that x, as the original does.
struct XfadeDelay {
    float xf = 0;                          // +0x04
    uint32_t cur = 0, prev = 0, target = 0;  // +0x08 +0x0C +0x10
    float last = 0;                        // +0x14 (carry)
    uint32_t w = 0;                        // +0x18
    uint32_t mask = 0;
    bool vmxCopy = false;
    std::vector<float> buf;                // +0x20

    void init(uint32_t delay, uint32_t size, bool copyPath) {
        xf = 0, cur = prev = target = delay, last = 0, w = 0;
        mask = size - 1, vmxCopy = copyPath;
        buf.assign(size, 0.0f);
    }
    // 0x826B567C / 0x826B5B10 [C]
    void setDelay(uint32_t d) {
        target = d;
        if (prev == cur) cur = d, xf = 1.0f;
    }
    float at(uint32_t i) const { return buf[i & mask]; }

    void run(const float* in, float* out) {
        out[0] = last;
        const bool fading = cur - prev > 4u || prev - cur > 4u;
        const uint32_t n = truncU32(xf * 10000.0f) >> 1;
        if (n < 256) {
            float x = xf, omx = 1.0f - x;
            uint32_t k = 0;
            // Form A: prev * x + (cur * (1 - x)); form B: cur * (1 - x) + (prev * x).
            auto formA = [&](uint32_t i) {
                const float c = at(w + i - cur) * omx;
                const float y = fmadd(at(w + i - prev), x, c);
                buf[(w + i) & mask] = in[i];
                out[i + 1] = y;
                x = x - kFadeStep, omx = omx + kFadeStep;
            };
            auto formB = [&](uint32_t i) {
                const float c = at(w + i - prev) * x;
                const float y = fmadd(at(w + i - cur), omx, c);
                buf[(w + i) & mask] = in[i];
                out[i + 1] = y;
                x = x - kFadeStep, omx = omx + kFadeStep;
            };
            // The rest of the current fade: 4-way unrolled (first sample form
            // A, the other three form B), then one at a time.
            if (n >= 4)
                for (uint32_t g = 0; g < n / 4; ++g) {
                    formA(k++);
                    formB(k++);
                    formB(k++);
                    formB(k++);
                }
            while (k < n) formA(k++);
            // Fade over: old = new, new = target; a new fade if they differ.
            x = target != cur ? 1.0f : 0.0f;
            omx = 1.0f - x;
            prev = cur, cur = target;
            while (k < 256) formA(k++);
            xf = x;
            w = (w + 256) & mask;
        } else if (vmxCopy && w + 256 < mask + 1 && w - prev + 256 < mask + 1 && w - cur + 256 < mask + 1 &&
                   int32_t(w - prev) >= 0 && int32_t(w - cur) >= 0) {
            if (!fading) {
                // VMX lvlx/lvrx copy: the delayed samples unchanged.
                for (uint32_t i = 0; i < 256; ++i) {
                    out[i + 1] = buf[w - prev + i];
                    buf[w + i] = in[i];
                }
                xf = xf - kFadeBlockStep;
            } else {
                float x = xf, omx = 1.0f - x;
                for (uint32_t i = 0; i < 256; ++i) {
                    const float c = buf[w - cur + i] * omx;
                    const float y = fmadd(buf[w - prev + i], x, c);
                    buf[w + i] = in[i];
                    out[i + 1] = y;
                    x = x - kFadeStep, omx = omx + kFadeStep;
                }
                xf = x;
            }
            w += 256;
        } else {
            float x = xf, omx = 1.0f - x;
            for (uint32_t i = 0; i < 256; ++i) {
                const float a = at(w + i - cur) * omx;
                const float b = at(w + i - prev) * x;
                buf[(w + i) & mask] = in[i];
                x = x - kFadeStep, omx = omx + kFadeStep;
                out[i + 1] = a + b;
            }
            xf = x;
            w = (w + 256) & mask;
        }
        last = out[256];
    }
};

// 0x826AA970 [C]: two one-pole low-passes in series, gain g each,
//   s1 = a s1 + (1-a) g x,  s2 = a s2 + (1-a) g s1(previous);
// coefficients from 0x826B48E8.
struct RoomFilter {
    float rate = 48000, freq = 24000, main = 0, hf = 0;  // +0x00..+0x0C
    float s1 = 0, s2 = 0;                                // +0x10 +0x14
    float g = 1, a = 0;                                  // +0x18 +0x1C

    // 0x826B48E8 [C]
    void compute() {
        const float total = hf + main;
        const float mainShare = total < 0.0f ? main / total : 1.0f;
        const float omega = float(double(freq) * 6.2831853080000002 / double(rate));
        const float hfDb = (1.0f - mainShare) * total;
        float hfGain = float(std::pow(10.0, double(hfDb * 0.1f)));
        hfGain = float(std::pow(double(hfGain), 0.5));
        g = float(std::pow(10.0, double(mainShare * total * 0.05f)));
        g = float(std::pow(double(g), 0.5));
        if (hfGain < 0.995000005f) {
            const float c = float(std::cos(double(omega)));
            const float h2 = hfGain * hfGain, c2 = c * c, ch = c * hfGain;
            const double t = (1.0 - double(c)) * double(hfGain);
            const double r = std::sqrt(t * 2.0 - (1.0 - double(c2)) * double(h2));
            a = float((1.0 - double(ch)) - r) / float(1.0 - double(hfGain));
        } else {
            a = 0;
        }
    }

    // 256 samples; out[0] = previous s2, out[k+1] = s2 after sample k. The
    // 4-way unrolled loop rounds sample 0 of each group differently.
    void run(const float* in, float* out) {
        out[0] = s2;
        const float om = 1.0f - a, gom = g * om;
        for (int k = 0; k < 256; k += 4) {
            const float p0 = in[k] * gom;
            const float p1 = in[k + 1] * g, p2 = in[k + 2] * g, p3 = in[k + 3] * g;
            float old = s1;
            s1 = fmadd(a, s1, p0);
            s2 = fmadd(gom, old, a * s2);
            out[k + 1] = s2;
            const float pk[3] = {p1, p2, p3};
            for (int j = 0; j < 3; ++j) {
                old = s1;
                s1 = fmadd(pk[j], om, a * s1);
                s2 = fmadd(gom, old, a * s2);
                out[k + 2 + j] = s2;
            }
        }
    }
};

// 0x826AAAA0 (512 samples) / 0x826AAE08 (2048) [C]:
//   out1 = x[d1] c2 + (x[d0] c1),  out2 = x[d2] c3.
struct Tap3 {
    uint32_t d0 = 0;    // +0x00
    float c1 = 0;       // +0x04
    float carry1 = 0;   // +0x08
    uint32_t d1 = 0;    // +0x0C
    float c2 = 0;       // +0x10
    uint32_t d2 = 0;    // +0x18
    float c3 = 0;       // +0x1C
    float carry2 = 0;   // +0x20
    uint32_t w = 0;     // +0x24
    uint32_t mask = 0;
    std::vector<float> buf;  // +0x2C

    void init(uint32_t size, uint32_t a, float ca, uint32_t b, float cb, uint32_t c, float cc) {
        mask = size - 1, buf.assign(size, 0.0f);
        d0 = a, c1 = ca, d1 = b, c2 = cb, d2 = c, c3 = cc, carry1 = carry2 = 0, w = 0;
    }
    void run(const float* in, float* out1, float* out2) {
        out1[0] = carry1, out2[0] = carry2;
        for (uint32_t i = 0; i < 256; ++i) {
            const float p = buf[(w + i - d0) & mask] * c1;
            const float t1 = buf[(w + i - d1) & mask], t2 = buf[(w + i - d2) & mask];
            buf[(w + i) & mask] = in[i];
            out2[i + 1] = t2 * c3;
            out1[i + 1] = fmadd(t1, c2, p);
        }
        carry1 = out1[256], carry2 = out2[256];
        w = (w + 256) & mask;
    }
};

// Schroeder all-pass: v = x + cin d, y = d + cout v (cout = -cin) [C].
// 0x826AAB70 (128 samples), 0x826AAED8 (256), 0x826AB048 (512): in place;
// 0x826AB1B8 (512): to another buffer, and it also stores the last output
// into in[256].
struct Allpass {
    uint32_t d = 0;      // +0x00
    float cout = 0;      // +0x04
    float cin = 0;       // +0x08
    float carry = 0;     // +0x0C
    uint32_t w = 0;      // +0x10
    uint32_t mask = 0;
    std::vector<float> buf;  // +0x18

    void init(uint32_t size, uint32_t delay, float c) {
        mask = size - 1, buf.assign(size, 0.0f);
        d = delay, cin = c, cout = -c, carry = 0, w = 0;
    }
    void setCoef(float c) { cin = c, cout = -c; }
    void run(float* in, float* out) {
        float prev = carry;
        for (uint32_t i = 0; i < 256; ++i) {
            const float x = in[i];
            const float t = buf[(w + i - d) & mask];
            const float v = fmadd(cin, t, x);
            buf[(w + i) & mask] = v;
            out[i] = prev;
            prev = fmadd(cout, v, t);
        }
        out[256] = prev;
        in[256] = prev;
        carry = prev;
        w = (w + 256) & mask;
    }
    void runInPlace(float* io) { run(io, io); }
};

// 0x826AB320 (1024 samples) [C]: out1 = x[da] ca, out2 = x[db] cb.
struct Tap2 {
    uint32_t da = 0;   // +0x00
    float ca = 0;      // +0x04
    float carryA = 0;  // +0x08
    uint32_t db = 0;   // +0x0C
    float cb = 0;      // +0x10
    float carryB = 0;  // +0x14
    uint32_t w = 0;    // +0x18
    std::vector<float> buf = std::vector<float>(1024, 0.0f);  // +0x20

    void run(const float* in, float* out1, float* out2) {
        out1[0] = carryA, out2[0] = carryB;
        for (uint32_t i = 0; i < 256; ++i) {
            const float a = buf[(w + i - da) & 1023] * ca;
            const float b = buf[(w + i - db) & 1023] * cb;
            buf[(w + i) & 1023] = in[i];
            out1[i + 1] = a, out2[i + 1] = b;
        }
        carryA = out1[256], carryB = out2[256];
        w = (w + 256) & 1023;
    }
};

// ---- the late field's per-sample parts (all inline in 0x826AB9C0) [C].

// All-pass with its output kept as state: v = x + cin d -> line,
// out = d + cout v.
struct LateAllpass {
    uint32_t d = 0;  // +0x00
    float cout = 0, cin = 0, out = 0;  // +0x04 +0x08 +0x0C
    uint32_t idx = 0;                  // +0x10
    uint32_t mask = 0;
    std::vector<float> buf;  // +0x18
    void init(uint32_t size, uint32_t delay, float c) {
        mask = size - 1, buf.assign(size, 0.0f), d = delay, cin = c, cout = -c, out = 0, idx = 0;
    }
    float tap() const { return buf[(idx - d) & mask]; }
    void push(float v) { buf[idx] = v, idx = (idx + 1) & mask; }
};

// The 3-coefficient variant (+0x35008, +0x400A8): line = x + b d,
// out = a x + (d c).
struct LateAllpass3 {
    uint32_t d = 0;  // +0x00
    float a = 0, b = 0, c = 0, out = 0;  // +0x04 .. +0x10
    uint32_t idx = 0;                    // +0x14
    std::vector<float> buf = std::vector<float>(2048, 0.0f);
    float tap() const { return buf[(idx - d) & 2047]; }
    void push(float v) { buf[idx] = v, idx = (idx + 1) & 2047; }
};

// Two-tap delay line, 4096 samples: outA = x[da] ga, outB = x[db] gb.
struct TwoTap {
    uint32_t da = 0;           // +0x00
    float ga = 1, outA = 0;    // +0x04 +0x08
    uint32_t db = 0;           // +0x0C
    float gb = 0, outB = 0;    // +0x10 +0x14
    uint32_t idx = 0;          // +0x18
    std::vector<float> buf = std::vector<float>(4096, 0.0f);  // +0x20
    void step(float in) {
        const float a = buf[(idx - da) & 4095], b = buf[(idx - db) & 4095];
        outA = a * ga, outB = b * gb;
        buf[idx] = in, idx = (idx + 1) & 4095;
    }
};

// The short filters: a two-sample memory z1 (newest), z2. Their "select"
// word is 1 at creation and never written, so the tap is always z2 [C].
struct Mem2 {
    float z1 = 0, z2 = 0;
    void shift(float v) { z2 = z1, z1 = v; }
};

}  // namespace

struct XAudioReverb::Core {
    float rate = 48000;  // +0x58
    float wet = 1;       // +0x5C (effect parameter 2 x 0.01; GH2 never sets it)
    float reflGain = 1;  // +0x60
    int mode = 0;        // +0x64: 0 stereo, 1 mono (0x826B4D28 <- 0x826AD2F0)

    XfadeDelay reflL, reflR;   // +0x68, +0x10088
    RoomFilter roomL, roomR;   // +0x200A8, +0x200C8
    float mixL1 = -0.0125f, mixL2 = -0.0675f;  // +0x200E8 +0x200EC
    Tap3 tapL, tap2L;          // +0x200F0, +0x20B34
    Allpass ap1L, ap2L, ap3L;  // +0x2091C, +0x22B60, +0x22F78
    Tap2 lateTapL;             // +0x23790
    float mixR1 = -0.0125f, mixR2 = -0.0675f;  // +0x247B0 +0x247B4
    Tap3 tapR, tap2R;          // +0x247B8, +0x251FC
    Allpass ap1R, ap2R, ap3R;  // +0x24FE4, +0x27228, +0x27640
    Tap2 lateTapR;             // +0x27E58

    // Late field.
    LateAllpass lap1, lap2;     // +0x28E78, +0x29690
    struct { float c = -0.99845f, out = 0; Mem2 m; } f1;            // +0x29EA8
    struct { float ca = 0.23906f, cb = 0.98453f, cc = -0.0901f, out = 0; Mem2 m; } f2;  // +0x29EC0
    struct { float out = 0, g = 0.2006f, c = 0.76094f; Mem2 m; } f3;  // +0x29EE0
    TwoTap dl1;                 // +0x29EFC
    struct { float c = 0.6f, out = 0; Mem2 m; } f4;                  // +0x2DF1C
    LateAllpass lap3, lap4;     // +0x2DF34, +0x2E74C
    struct { float c = -0.99845f, out = 0; Mem2 m; } f5;            // +0x2EF64
    struct { float ca = 0.23906f, cb = 0.98453f, cc = -0.0901f, out = 0; Mem2 m; } f6;  // +0x2EF7C
    struct { float out = 0, g = 0.2006f, c = 0.76094f; Mem2 m; } f7;  // +0x2EF9C
    TwoTap dl2;                 // +0x2EFB8
    struct { float c = 0.6f, out = 0; Mem2 m; } f8;                  // +0x32FD8
    LateAllpass lap5;           // +0x32FF0
    LateAllpass3 lap6;          // +0x35008
    LateAllpass lap7, lap8;     // +0x37024, +0x3903C
    TwoTap dl3;                 // +0x3A054
    struct { float out = 0, ca = 0.3f, cb = 0.3f; Mem2 m; } f9;       // +0x3E074
    LateAllpass lap9;           // +0x3E090
    LateAllpass3 lap10;         // +0x400A8
    LateAllpass lap11, lap12;   // +0x420C4, +0x440DC
    TwoTap dl4;                 // +0x450F4
    struct { float out = 0, ca = -0.3f, cb = -0.3f; Mem2 m; } f10;    // +0x49114

    Allpass outU1, outU2, outP1, outP2;  // +0x49130 +0x49948 +0x4A160 +0x4A978
    XfadeDelay rearL, rearR;              // +0x4B190 +0x4B5B0

    // Block scratch (the process function's stack buffers), 256 + carry +
    // slack.
    float bIn[264], bRoom[264], bA_L[264], bE_L[264], bX[264], bT2L[264], b190L[264];
    float bA_R[264], b190R[264], bB_R[264], bT2R[264], bC_R[264];
    float eL[264], eR[264], U[264], Q[264], P[264], S[264], rL[264], rR[264];

    // 0x826B5B70 + 0x826B6B70 [C]
    Core() {
        const float k618 = 0.617999971f;
        reflL.init(40, 16384, true), reflR.init(40, 16384, true);
        tapL.init(512, 509, 0.200000003f, 508, 0.400000006f, 1, -0.00100000005f);
        tapR.init(512, 509, 0.200000003f, 508, 0.400000006f, 1, -0.00100000005f);
        ap1L.init(128, 83, 0.375f), ap1R.init(128, 97, 0.375f);
        tap2L.init(2048, 2039, 0.200000003f, 2038, 0.600000024f, 600, 0.0500000007f);
        tap2R.init(2048, 1297, 0.200000003f, 1296, 0.600000024f, 600, 0.0500000007f);
        ap2L.init(256, 211, 0.5f), ap2R.init(256, 223, 0.5f);
        ap3L.init(512, 311, 0.5f), ap3R.init(512, 293, 0.5f);
        for (Tap2* t : {&lateTapL, &lateTapR}) t->da = 1020, t->ca = -0.699999988f, t->db = 1, t->cb = 0.0437499993f;
        lap1.init(512, 409, k618), lap2.init(512, 257, 0.5f);
        lap3.init(512, 383, k618), lap4.init(512, 233, 0.5f);
        lap5.init(2048, 1511, k618);
        lap6.d = 1061, lap6.a = k618, lap6.b = k618, lap6.c = -k618;
        lap7.init(2048, 853, k618), lap8.init(1024, 541, 0.5f);
        lap9.init(2048, 1657, k618);
        lap10.d = 1103, lap10.a = k618, lap10.b = k618, lap10.c = -k618;
        lap11.init(2048, 887, k618), lap12.init(1024, 491, 0.5f);
        const uint32_t dlLen[4] = {3432, 3820, 1510, 1438};
        TwoTap* dls[4] = {&dl1, &dl2, &dl3, &dl4};
        for (int i = 0; i < 4; ++i) dls[i]->da = dls[i]->db = dlLen[i], dls[i]->gb = 0.999899983f;
        outU1.init(512, 131, 0.308999985f), outU2.init(512, 113, 0.308999985f);
        outP1.init(512, 107, 0.308999985f), outP2.init(512, 127, 0.308999985f);
        rearL.init(20, 256, false), rearR.init(20, 256, false);
    }

    // Wrapper state (XAudioReverb::run).
    std::vector<float> pendIn, readyOut, scratch6;
    size_t readyPos = 0;
    bool buffered = false;
    I3DL2Params params;
    bool haveParams = false;

    void applyI3DL2(const I3DL2Params& p);
    void block(const float* inL, const float* inR, float* fl, float* fr, float* sl, float* sr);
};

// ---- parameters

namespace {

// XAudio's native reverb parameters (0x54 bytes; the I3DL2 conversion fills
// every field) [C].
struct Native {
    uint32_t reflectionsDelay, reverbDelay, rearDelay, rearDelay2, positionLeft, positionRight;
    uint32_t earlyDiffusion, lateDiffusion, lowEQGain, lowEQCutoff, highEQGain, highEQCutoff, rearDelayMs;
    float roomFilterFreq, roomFilterMain, roomFilterHF, reflectionsGain, reverbGain, decayTime, density, roomSize;
};

// 0x826B6740 [C]
Native toNative(const I3DL2Params& p) {
    Native n{};
    n.rearDelayMs = 5;  // 0x820D9BB4
    n.rearDelay = n.rearDelay2 = 6;  // 0x820D9BAC
    n.positionLeft = n.positionRight = 27;  // 0x820D9BB0
    n.roomSize = 100.0f;  // 0x820D9BCC
    n.lowEQCutoff = 4, n.highEQCutoff = 6;
    n.roomFilterMain = float(p.room) * 0.00999999978f;
    n.roomFilterHF = float(p.roomHF) * 0.00999999978f;
    auto gainIndex = [](double x) {
        const int32_t i = int32_t(x);
        return uint32_t(i < -8 ? 0 : i < 0 ? i + 8 : 8);
    };
    if (p.decayHFRatio < 1.0f) {
        n.lowEQGain = 8;
        n.highEQGain = gainIndex(double(float(std::log10(double(p.decayHFRatio)))) * 4.0);
        n.decayTime = p.decayTime;
    } else {
        n.highEQGain = 8;
        n.lowEQGain = gainIndex(double(float(std::log10(double(p.decayHFRatio)))) * -4.0);
        n.decayTime = p.decayTime * p.decayHFRatio;
    }
    n.reflectionsGain = float(p.reflections) * 0.00999999978f;
    float rd = p.reflectionsDelay * 1000.0f;
    if (!(rd < 300.0f)) rd = 300.0f - 1.0f;
    if (!(rd > 1.0f)) rd = 1.0f;
    n.reflectionsDelay = truncU32(rd);
    n.reverbGain = float(p.reverb) * 0.00999999978f;
    float vd = p.reverbDelay * 1000.0f;
    if (!(vd < 85.0f)) vd = 85.0f - 1.0f;
    n.reverbDelay = truncU32(vd);
    n.earlyDiffusion = n.lateDiffusion = truncU32(p.diffusion * 0.150000006f);
    n.density = p.density;
    n.roomFilterFreq = p.hfReference;
    return n;
}

// The tables 0x826B4D48 reads, at the indices the I3DL2 path can produce
// (positions 27, rear delay 6, cutoffs 4 / 6; gains 0..8) [C].
const float kDiffusion[16] = {0.0f, 0.015625f, 0.03125f, 0.046875f, 0.0703125f, 0.09375f, 0.125f, 0.15625f,
                              0.1953125f, 0.234375f, 0.2734375f, 0.3125f, 0.375f, 0.4375f, 0.5f, 0.625f};  // 0x820DA818
const float kLowEqA[9] = {-0.969909668f, -0.964935303f, -0.959960938f, -0.957427979f, -0.954956055f,
                          -0.952941895f, -0.951934814f, -0.950927734f, -0.949951172f};  // 0x820D9FC8[10 g + 4]
const float kLowEqB = 0.949951172f;  // 0x820DA1D0[10 g + 4], the same for every gain
const float kHighEqA[9] = {0.0641479492f, 0.0124206496f, 0.167755127f, 0.04150391f, -0.0682983398f,
                           -0.134155273f, -0.189483643f, -0.217712402f, -0.23260498f};  // 0x820DA3D8[15 g + 6]
const float kHighEqB[9] = {0.107574463f, 0.121185303f, 0.0877990723f, 0.113128662f, 0.151062012f,
                           0.189025879f, 0.239654541f, 0.277618408f, 0.302886963f};  // 0x820DA5F8[15 g + 6]
const float kHighCutA = 0.603881836f;  // 0x820D9C54[6]
const float kHighCutB = 0.396087646f;  // 0x820D9D0C[6]
const float kPosC90 = 0.649999976f;    // 0x820D9C90[27]
const float kPosD48 = -0.850000024f;   // 0x820D9D48[27]
const float kPosF48 = 0.84375f;        // 0x820D9F48[27]
const float kRearDC8 = -0.239999995f;  // 0x820D9DC8[6]
const float kRearE48 = -0.300000012f;  // 0x820D9E48[6]
const float kRearEC8 = -0.119999997f;  // 0x820D9EC8[6]

uint32_t msToSamples(uint32_t ms, float rate) {
    return ms == 0 ? 1 : truncU32(float(ms) * rate * 0.00100000005f);
}

}  // namespace

// 0x826B4D48 [C]
void XAudioReverb::Core::applyI3DL2(const I3DL2Params& ip) {
    const Native n = toNative(ip);
    const uint32_t revDelay = std::min<uint32_t>(primeLength(n.reverbDelay == 0 ? 1 : truncU32(rate * float(n.reverbDelay) * 0.00100000005f)), 4095);
    dl1.db = dl2.db = dl3.db = dl4.db = revDelay;

    // Room size scale (always 100 from I3DL2) and the density split.
    float rs = n.roomSize;
    rs = rs < 50.0f ? fmadd(rs, 0.400000006f, 30.0f) * 0.00999999978f : rs * 0.00999999978f;
    float s1 = rs * rate * 2.08333331e-05f;
    if (s1 < 0.300000012f) s1 = 0.300000012f;
    float s2 = s1 * 2.0f;
    if (s2 < 0.300000012f) s2 = 0.300000012f;
    else if (s2 > 1.0f) s2 = 1.0f;
    float dA, dB, dC, dD;  // 0..25, 25..50, 50..75, 75..100
    const float dens = n.density;
    if (!(dens < 75.0f)) dA = dB = dC = 1.0f, dD = (dens - 75.0f) * 0.0399999991f;
    else if (!(dens < 50.0f)) dA = dB = 1.0f, dD = 0, dC = (dens - 50.0f) * 0.0399999991f;
    else if (!(dens < 25.0f)) dD = dC = 0, dA = 1.0f, dB = (dens - 25.0f) * 0.0399999991f;
    else dD = dC = dB = 0, dA = dens * 0.0399999991f;

    auto len = [](float f) { return primeLength(truncU32(f)); };
    const uint32_t l509 = truncU32(s2 * 509.0f);
    tapL.d0 = primeLength(l509), tapL.d1 = tapL.d0 - 1;
    tap2L.d0 = len(s2 * 2039.0f), tap2L.d1 = tap2L.d0 - 1;
    const uint32_t lTap = truncU32(float(n.reverbDelay != 0 ? 9 : 1020) * s2);
    lateTapL.da = primeLength(lTap);
    tapR.d0 = primeLength(l509), tapR.d1 = tapR.d0 - 1;
    tap2R.d0 = len(s2 * 1297.0f), tap2R.d1 = tap2R.d0 - 1;
    lateTapR.da = primeLength(lTap);
    const float dD2 = dD * s2;
    lap1.d = len(dD2 * 409.0f);
    lap2.d = len(s2 * 257.0f);
    dl1.da = len(dD * s1 * 3432.0f);
    lap3.d = len(dD2 * 383.0f);
    lap4.d = len(s2 * 233.0f);
    dl2.da = len(dC * s1 * 3820.0f);
    const float dB2 = dB * s2, dC2 = dC * s2;
    lap5.d = len(dB2 * 1511.0f);
    lap6.d = len(dC2 * 1061.0f) - 1;
    lap7.d = len(s2 * 853.0f);
    lap8.d = len(dD2 * 541.0f);
    dl3.da = len(dA * s1 * 1510.0f);
    lap9.d = len(dB2 * 1657.0f);
    lap10.d = len(dC2 * 1103.0f);
    lap11.d = len(dD2 * 887.0f);
    lap12.d = len(dD2 * 491.0f);
    dl4.da = len(s1 * 1438.0f);

    // Decay: the mean loop length -> the per-loop gain for -60 dB in
    // DecayTime.
    const uint32_t sum = lap4.d + lap9.d + lap3.d + lap12.d + lap11.d + dl3.da + dl1.da + lap10.d + lap7.d +
                         lap6.d + lap2.d + lap8.d + lap1.d + lap5.d + dl4.da + dl2.da;
    const float meanSec = float(sum >> 1) / rate;
    const double loopGain = std::pow(10.0, -3.0 / (double(n.decayTime) / double(meanSec)));
    const float lg = sqrtSingle(loopGain * 0.5);
    const float lg2 = lg * 0.5f;
    const float fb = fnmsub(lg, 0.629999995f, 0.899999976f);

    const float eg = kDiffusion[n.earlyDiffusion & 15], ld = kDiffusion[n.lateDiffusion & 15];
    const float posF48x = kPosF48 * -0.125f;
    const float c90f = kPosC90 * fb, d48f = kPosD48 * fb;
    const float d48fx = d48f * -0.125f;

    reflL.setDelay(msToSamples(n.reflectionsDelay, rate));
    reflR.setDelay(msToSamples(n.reflectionsDelay, rate));

    tapL.c1 = 0.200000003f, tapL.c2 = 0.400000006f, tapL.c3 = kRearDC8;
    ap1L.setCoef(eg);
    tap2L.c1 = 0.200000003f, tap2L.c2 = 0.600000024f, tap2L.c3 = posF48x;
    ap2L.setCoef(eg), ap3L.setCoef(eg);
    tapR.c1 = 0.200000003f;
    lateTapL.ca = c90f, lateTapL.cb = d48f;
    mixL1 = kRearE48, mixL2 = kRearEC8;
    tapR.c2 = 0.400000006f, tapR.c3 = kRearDC8;
    ap1R.setCoef(eg);
    tap2R.c1 = 0.200000003f, tap2R.c2 = 0.600000024f, tap2R.c3 = posF48x;
    ap2R.setCoef(eg), ap3R.setCoef(eg);
    lateTapR.ca = c90f, lateTapR.cb = d48f;
    mixR1 = kRearE48, mixR2 = kRearEC8;
    for (LateAllpass* a : {&lap1, &lap2, &lap3, &lap4, &lap8, &lap12}) a->cin = ld, a->cout = -ld;
    const uint32_t gl = std::min<uint32_t>(n.lowEQGain, 8), gh = std::min<uint32_t>(n.highEQGain, 8);
    f1.c = f5.c = kLowEqA[gl];
    f2.ca = f6.ca = kHighCutB;
    f2.cb = f6.cb = kLowEqB;
    f2.cc = f6.cc = kHighEqA[gh];
    f3.g = f7.g = kHighEqB[gh];
    f3.c = f7.c = kHighCutA;
    f4.c = f8.c = lg;
    f9.ca = f9.cb = lg2;
    f10.ca = f10.cb = -lg2;

    const float rv = float(std::pow(10.0, double(n.reverbGain) * 0.050000000000000003));
    dl1.gb = rv * kPosF48;
    dl2.gb = rv * kPosF48;
    dl3.gb = rv * d48fx;
    dl4.gb = rv * d48fx;
    reflGain = float(std::pow(10.0, double(n.reflectionsGain) * 0.050000000000000003));
    const float og = ld * 0.600000024f;
    outU1.setCoef(og), outU2.setCoef(og), outP1.setCoef(og), outP2.setCoef(og);

    for (RoomFilter* f : {&roomL, &roomR}) {
        f->freq = n.roomFilterFreq, f->main = n.roomFilterMain, f->hf = n.roomFilterHF;
        f->compute();
    }
    rearL.setDelay(msToSamples(n.rearDelayMs, rate));
    rearR.setDelay(msToSamples(n.rearDelayMs, rate));
}

// ---- processing

// 0x826AB9C0 [C]
void XAudioReverb::Core::block(const float* inL, const float* inR, float* fl, float* fr, float* sl, float* sr) {
    if (mode == 1) inR = inL;

    // Left half.
    reflL.run(inL, bIn);
    roomL.run(bIn, bRoom);
    tapL.run(bRoom, bA_L, bE_L);
    ap1L.runInPlace(bA_L);
    tap2L.run(bA_L, bX, bT2L);
    ap2L.runInPlace(bX);
    ap3L.run(bX, b190L);
    lateTapL.run(b190L, U, Q);
    // Right half.
    reflR.run(inR, bIn);
    roomR.run(bIn, bRoom);
    tapR.run(bRoom, bA_R, b190R);
    ap1R.runInPlace(bA_R);
    tap2R.run(bA_R, bB_R, bT2R);
    ap2R.runInPlace(bB_R);
    ap3R.run(bB_R, bC_R);
    lateTapR.run(bC_R, P, S);

    // Early reflections, cross-mixed. The 8-way unrolled loop sums the
    // second left sample of each group in another order.
    for (int k = 0; k < 256; ++k) {
        if ((k & 7) == 1) eL[k] = fmadd(mixL2, bX[k], fmadd(bA_R[k], mixR1, bE_L[k])) * reflGain;
        else eL[k] = (fmadd(bA_R[k], mixR1, bX[k] * mixL2) + bE_L[k]) * reflGain;
        eR[k] = (fmadd(bA_L[k], mixL1, bB_R[k] * mixR2) + b190R[k]) * reflGain;
    }

    // Late field. "o" values are the parts' outputs from the previous sample.
    for (int i = 0; i < 256; ++i) {
        const float u = U[i], q = Q[i], s = S[i], p = P[i], t2l = bT2L[i], t2r = bT2R[i];
        const float o2df24 = f4.out, o32fe0 = f8.out, o3e078 = f9.out, o49118 = f10.out;
        const float newP = ((eR[i] + dl3.outB) + dl2.outB) * 2.0f;
        const float newU = ((eL[i] + dl4.outB) + dl1.outB) * 2.0f;
        const float qOut = dl1.outB, sOut = dl2.outB;
        const float o28e84 = lap1.out, o2969c = lap2.out, o29eb0 = f1.out, o29ed0 = f2.out, o29ee4 = f3.out;
        const float o29f04 = dl1.outA, o2df40 = lap3.out, o2e758 = lap4.out, o2ef6c = f5.out, o2ef8c = f6.out;
        const float o2efa0 = f7.out, o2efc0 = dl2.outA, o32ffc = lap5.out, o35018 = lap6.out, o37030 = lap7.out;
        const float o39048 = lap8.out, o3a05c = dl3.outA, o3e09c = lap9.out, o400b8 = lap10.out;
        const float o420d0 = lap11.out, o440e8 = lap12.out, o450fc = dl4.outA;

        {  // +0x28E78
            const float d = lap1.tap();
            const float v = (fmadd(lap1.cin, d, u) + o49118) + o3e078;
            lap1.push(v), lap1.out = fmadd(lap1.cout, v, d);
        }
        {  // +0x29690
            const float d = lap2.tap();
            const float v = fmadd(lap2.cin, d, o28e84);
            lap2.push(v), lap2.out = fmadd(v, lap2.cout, d);
        }
        {  // +0x29EA8
            const float x = t2l + o2969c;
            f1.out = fmadd(f1.m.z2, f1.c, x);
            f1.m.shift(x);
        }
        {  // +0x29EC0
            const float z2 = f2.m.z2;
            const float t = f2.cc * z2;
            const float v = fmadd(f2.cb, z2, o29eb0);
            f2.m.shift(v);
            f2.out = fmadd(v, f2.ca, t);
        }
        {  // +0x29EE0
            const float v = fmadd(f3.m.z2, f3.c, o29ed0);
            f3.m.shift(v);
            f3.out = f3.g * v;
        }
        dl1.step(o29ee4 * 8.0f);  // +0x29EFC
        f4.out = f4.m.z2 * f4.c, f4.m.shift(o29f04);  // +0x2DF1C
        {  // +0x2DF34
            const float d = lap3.tap();
            const float v = fmadd(lap3.cin, d, o49118 - o3e078) + p;
            lap3.push(v), lap3.out = fmadd(v, lap3.cout, d);
        }
        {  // +0x2E74C
            const float d = lap4.tap();
            const float v = fmadd(lap4.cin, d, o2df40);
            lap4.push(v), lap4.out = fmadd(v, lap4.cout, d);
        }
        {  // +0x2EF64
            const float x = t2r + o2e758;
            f5.out = fmadd(f5.m.z2, f5.c, x);
            f5.m.shift(x);
        }
        {  // +0x2EF7C
            const float z2 = f6.m.z2;
            const float v = fmadd(f6.cb, z2, o2ef6c);
            f6.m.shift(v);
            f6.out = fmadd(z2, f6.cc, v * f6.ca);
        }
        {  // +0x2EF9C
            const float v = fmadd(f7.m.z2, f7.c, o2ef8c);
            f7.m.shift(v);
            f7.out = v * f7.g;
        }
        dl2.step(o2efa0 * 8.0f);  // +0x2EFB8
        f8.out = f8.m.z2 * f8.c, f8.m.shift(o2efc0);  // +0x32FD8
        {  // +0x32FF0
            const float d = lap5.tap();
            const float v = (fmadd(d, lap5.cin, s) + o32fe0) + o2df24;
            lap5.push(v), lap5.out = fmadd(v, lap5.cout, d);
        }
        {  // +0x35008
            const float x = q + o32ffc;
            const float d = lap6.tap();
            lap6.out = fmadd(lap6.a, x, d * lap6.c);
            lap6.push(fmadd(d, lap6.b, x));
        }
        {  // +0x37024
            const float d = lap7.tap();
            const float v = fmadd(d, lap7.cin, o35018);
            lap7.push(v), lap7.out = fmadd(v, lap7.cout, d);
        }
        {  // +0x3903C
            const float d = lap8.tap();
            const float v = fmadd(d, lap8.cin, q) + o37030;
            lap8.push(v), lap8.out = fmadd(v, lap8.cout, d);
        }
        dl3.step(t2l + o39048);  // +0x3A054
        {  // +0x3E074
            const float t = f9.ca * o3a05c;
            f9.out = fmadd(f9.m.z2, f9.cb, t);
            f9.m.shift(o3a05c);
        }
        {  // +0x3E090
            const float d = lap9.tap();
            const float v = fmadd(d, lap9.cin, o32fe0 - o2df24) + q;
            lap9.push(v), lap9.out = fmadd(v, lap9.cout, d);
        }
        {  // +0x400A8
            const float x = s + o3e09c;
            const float d = lap10.tap();
            const float t = d * lap10.c;
            lap10.push(fmadd(d, lap10.b, x));
            lap10.out = fmadd(x, lap10.a, t);
        }
        {  // +0x420C4
            const float d = lap11.tap();
            const float v = fmadd(lap11.cin, d, o400b8);
            lap11.push(v), lap11.out = fmadd(v, lap11.cout, d);
        }
        P[i] = newP, U[i] = newU, Q[i] = qOut, S[i] = sOut;
        {  // +0x440DC
            const float d = lap12.tap();
            const float v = fmadd(d, lap12.cin, s) + o420d0;
            lap12.push(v), lap12.out = fmadd(v, lap12.cout, d);
        }
        dl4.step(t2r + o440e8);  // +0x450F4
        {  // +0x49114
            const float t = f10.ca * o450fc;
            f10.out = fmadd(f10.m.z2, f10.cb, t);
            f10.m.shift(o450fc);
        }
    }

    outU1.runInPlace(U), outU2.runInPlace(U);
    outP1.runInPlace(P), outP2.runInPlace(P);
    rearL.run(Q, rL);
    rearR.run(S, rR);

    // VMX128 output (0x826ACF54); wet = +0x5C.
    const float w = wet, dry = ftz(1.0f - w), w2 = ftz(w + w);
    for (int k = 0; k < 256; ++k) {
        fl[k] = ftz(fmadd(dry, ftz(inL[k]), ftz(w * ftz(U[k]))));
        fr[k] = ftz(fmadd(dry, ftz(inR[k]), ftz(w * ftz(P[k]))));
        sl[k] = ftz(w2 * ftz(rL[k]));
        sr[k] = ftz(w2 * ftz(rR[k]));
    }
}

// ---- the public wrapper

XAudioReverb::XAudioReverb(int channels, int sampleRate)
    : channels_(channels == 1 ? 1 : 2), rate_(sampleRate), core_(std::make_unique<Core>()) {
    reset();
}

XAudioReverb::~XAudioReverb() = default;
XAudioReverb::XAudioReverb(XAudioReverb&&) noexcept = default;
XAudioReverb& XAudioReverb::operator=(XAudioReverb&&) noexcept = default;

void XAudioReverb::reset() {
    const bool had = core_->haveParams;
    const I3DL2Params last = core_->params;
    core_ = std::make_unique<Core>();
    core_->rate = float(rate_);
    core_->mode = channels_ == 1 ? 1 : 0;
    for (RoomFilter* f : {&core_->roomL, &core_->roomR}) f->rate = float(rate_), f->freq = float(rate_) * 0.5f;
    if (had) setI3DL2(last);
}

void XAudioReverb::setI3DL2(const I3DL2Params& p) {
    core_->params = p, core_->haveParams = true;
    core_->applyI3DL2(p);
}

int XAudioReverb::latency() const { return core_->buffered ? kBlock : 0; }

void XAudioReverb::run(const float* in, float* out6, int frames) {
    Core& c = *core_;
    const int ch = channels_;
    float inL[kBlock], inR[kBlock], fl[kBlock], fr[kBlock], sl[kBlock], sr[kBlock];
    auto doBlock = [&](const float* src, float* dst) {
        for (int k = 0; k < kBlock; ++k) {
            inL[k] = src[k * ch];
            inR[k] = ch == 2 ? src[k * ch + 1] : src[k * ch];
        }
        c.block(inL, inR, fl, fr, sl, sr);
        for (int k = 0; k < kBlock; ++k) {
            float* o = dst + k * 6;
            o[0] = fl[k], o[1] = fr[k], o[2] = 0, o[3] = 0, o[4] = sl[k], o[5] = sr[k];
        }
    };
    if (!c.buffered && frames % kBlock == 0) {
        for (int b = 0; b < frames; b += kBlock) doBlock(in + size_t(b) * ch, out6 + size_t(b) * 6);
        return;
    }
    // Unaligned use: from now on a fixed 256-frame delay through a FIFO.
    if (!c.buffered) {
        c.buffered = true;
        c.pendIn.clear();
        c.readyOut.assign(size_t(kBlock) * 6, 0.0f);
        c.readyPos = 0;
    }
    for (int f = 0; f < frames; ++f) {
        c.pendIn.insert(c.pendIn.end(), in + size_t(f) * ch, in + size_t(f + 1) * ch);
        std::memcpy(out6 + size_t(f) * 6, &c.readyOut[c.readyPos * 6], 6 * sizeof(float));
        if (++c.readyPos == size_t(kBlock)) {
            doBlock(c.pendIn.data(), c.readyOut.data());
            c.pendIn.clear();
            c.readyPos = 0;
        }
    }
}

void XAudioReverb::process6(const float* in, float* out6, int frames) {
    if (frames > 0) run(in, out6, frames);
}

void XAudioReverb::process(float* x, int frames) {
    if (frames <= 0) return;
    std::vector<float>& tmp = core_->scratch6;
    tmp.resize(size_t(frames) * 6);
    run(x, tmp.data(), frames);
    for (int f = 0; f < frames; ++f) {
        x[f * channels_] = tmp[size_t(f) * 6];
        if (channels_ == 2) x[f * 2 + 1] = tmp[size_t(f) * 6 + 1];
    }
}

// ---- GH2's environment table (0x826848A0) [C]

bool i3dl2Preset(const std::string& environment, I3DL2Params& out) {
    struct Entry {
        const char* name;
        I3DL2Params p;
    };
    static const Entry kTable[30] = {
        {"default", {-10000, 0, 0.0f, 1.0f, 0.5f, -10000, 0.0199999996f, -10000, 0.0399999991f, 100.0f, 100.0f, 5000.0f}},
        {"generic", {-1000, -100, 0.0f, 1.49000001f, 0.829999983f, -2602, 0.00700000022f, 200, 0.0109999999f, 100.0f, 100.0f, 5000.0f}},
        {"padded_cell", {-1000, -6000, 0.0f, 0.170000002f, 0.100000001f, -1204, 0.00100000005f, 207, 0.00200000009f, 100.0f, 100.0f, 5000.0f}},
        {"room", {-1000, -454, 0.0f, 0.400000006f, 0.829999983f, -1646, 0.00200000009f, 53, 0.00300000003f, 100.0f, 100.0f, 5000.0f}},
        {"bath_room", {-1000, -1200, 0.0f, 1.49000001f, 0.540000021f, -370, 0.00700000022f, 1030, 0.0109999999f, 100.0f, 60.0f, 5000.0f}},
        {"living_room", {-1000, -6000, 0.0f, 0.5f, 0.100000001f, -1376, 0.00300000003f, -1104, 0.00400000019f, 100.0f, 100.0f, 5000.0f}},
        {"stone_room", {-1000, -300, 0.0f, 2.30999994f, 0.639999986f, -711, 0.0120000001f, 83, 0.0170000009f, 100.0f, 100.0f, 5000.0f}},
        {"auditorium", {-1000, -476, 0.0f, 4.32000017f, 0.589999974f, -789, 0.0199999996f, -289, 0.0299999993f, 100.0f, 100.0f, 5000.0f}},
        {"concert_hall", {-1000, -500, 0.0f, 3.92000008f, 0.699999988f, -1230, 0.0199999996f, -2, 0.0289999992f, 100.0f, 100.0f, 5000.0f}},
        {"cave", {-1000, 0, 0.0f, 2.91000009f, 1.29999995f, -602, 0.0149999997f, -302, 0.0219999999f, 100.0f, 100.0f, 5000.0f}},
        {"arena", {-1000, -698, 0.0f, 7.23999977f, 0.330000013f, -1166, 0.0199999996f, 16, 0.0299999993f, 100.0f, 100.0f, 5000.0f}},
        {"hangar", {-1000, -1000, 0.0f, 10.0500002f, 0.230000004f, -602, 0.0199999996f, 198, 0.0299999993f, 100.0f, 100.0f, 5000.0f}},
        {"carpeted_hallway", {-1000, -4000, 0.0f, 0.300000012f, 0.100000001f, -1831, 0.00200000009f, -1630, 0.0299999993f, 100.0f, 100.0f, 5000.0f}},
        {"hallway", {-1000, -300, 0.0f, 1.49000001f, 0.589999974f, -1219, 0.00700000022f, 441, 0.0109999999f, 100.0f, 100.0f, 5000.0f}},
        {"stone_corridor", {-1000, -237, 0.0f, 2.70000005f, 0.790000021f, -1214, 0.0130000003f, 395, 0.0199999996f, 100.0f, 100.0f, 5000.0f}},
        {"alley", {-1000, -270, 0.0f, 1.49000001f, 0.860000014f, -1204, 0.00700000022f, -4, 0.0109999999f, 100.0f, 100.0f, 5000.0f}},
        {"forest", {-1000, -3300, 0.0f, 1.49000001f, 0.540000021f, -2560, 0.162f, -613, 0.0879999995f, 79.0f, 100.0f, 5000.0f}},
        {"city", {-1000, -800, 0.0f, 1.49000001f, 0.670000017f, -2273, 0.00700000022f, -2217, 0.0109999999f, 50.0f, 100.0f, 5000.0f}},
        {"mountains", {-1000, -2500, 0.0f, 1.49000001f, 0.209999993f, -2780, 0.300000012f, -2014, 0.100000001f, 27.0f, 100.0f, 5000.0f}},
        {"quarry", {-1000, -1000, 0.0f, 1.49000001f, 0.829999983f, -10000, 0.0610000007f, 500, 0.0250000004f, 100.0f, 100.0f, 5000.0f}},
        {"plain", {-1000, -2000, 0.0f, 1.49000001f, 0.5f, -2466, 0.179000005f, -2514, 0.100000001f, 21.0f, 100.0f, 5000.0f}},
        {"parking_lot", {-1000, 0, 0.0f, 1.64999998f, 1.5f, -1363, 0.00800000038f, -1153, 0.0120000001f, 100.0f, 100.0f, 5000.0f}},
        {"sewer_pipe", {-1000, -1000, 0.0f, 2.80999994f, 0.140000001f, 429, 0.0140000004f, 648, 0.0209999997f, 80.0f, 60.0f, 5000.0f}},
        {"underwater", {-1000, -4000, 0.0f, 1.49000001f, 0.100000001f, -449, 0.00700000022f, 1700, 0.0109999999f, 100.0f, 100.0f, 5000.0f}},
        {"small_room", {-1000, -600, 0.0f, 1.10000002f, 0.829999983f, -400, 0.00499999989f, 500, 0.00999999978f, 100.0f, 100.0f, 5000.0f}},
        {"medium_room", {-1000, -600, 0.0f, 1.29999995f, 0.829999983f, -1000, 0.00999999978f, -200, 0.0199999996f, 100.0f, 100.0f, 5000.0f}},
        {"large_room", {-1000, -600, 0.0f, 1.5f, 0.829999983f, -1600, 0.0199999996f, -1000, 0.0399999991f, 100.0f, 100.0f, 5000.0f}},
        {"medium_hall", {-1000, -600, 0.0f, 1.79999995f, 0.699999988f, -1300, 0.0149999997f, -800, 0.0299999993f, 100.0f, 100.0f, 5000.0f}},
        {"large_hall", {-1000, -600, 0.0f, 1.79999995f, 0.699999988f, -2000, 0.0299999993f, -1400, 0.0599999987f, 100.0f, 100.0f, 5000.0f}},
        {"plate", {-1000, -200, 0.0f, 1.29999995f, 0.899999976f, 0, 0.00200000009f, 0, 0.00999999978f, 100.0f, 75.0f, 5000.0f}},
    };
    for (const Entry& e : kTable)
        if (environment == e.name) {
            out = e.p;
            return true;
        }
    out = kTable[0].p;
    return false;
}
