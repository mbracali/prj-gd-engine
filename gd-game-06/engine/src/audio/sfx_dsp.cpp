// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/sfx_dsp.h"

#include <cmath>

namespace sfxdsp {

float dbToLin(float db) { return -96.0f < db ? float(std::pow(10.0, double(float(double(db) * 0.05000000074505806)))) : 0.0f; }

float linToDb(float lin) { return 0.0f < lin ? float(std::log10(double(lin))) * 20.0f : -96.0f; }

float wrapPan(float pan) {
    // 0x82192610: fmod, then + the period when negative.
    double m = float(std::fmod(double(float(double(pan) + 4.0)), 8.0));
    if (m < 0.0) m = float(m + 8.0);
    return float(m - 4.0);
}

Gains51 panGains(float p) {
    // 0x826898C8: the two speakers around the pan and the pan positions
    // they sit at ("from" gets the cosine).
    int from, to;
    float a, b;
    if (p < -3.0f) from = kRL, to = kRR, a = -3.0f, b = -5.0f;
    else if (p < -1.0f) from = kFL, to = kRL, a = -1.0f, b = -3.0f;
    else if (p < 0.0f) from = kFL, to = kC, a = -1.0f, b = 0.0f;
    else if (p < 1.0f) from = kC, to = kFR, a = 0.0f, b = 1.0f;
    else if (p < 3.0f) from = kFR, to = kRR, a = 1.0f, b = 3.0f;
    else from = kRR, to = kRL, a = 3.0f, b = 5.0f;
    const double t = double(((p - a) / (b - a)) * 1.5707964f);
    Gains51 g{};
    g[size_t(from)] = float(std::cos(t));
    g[size_t(to)] = float(std::sin(t));
    return g;
}

std::array<float, 5> sendGains(const Gains51& pan, float v, bool reverb) {
    float fl = v * pan[kFL], fr = v * pan[kFR], c = v * pan[kC], rl = v * pan[kRL], rr = v * pan[kRR];
    if (reverb) {
        rl = rl + fl;
        rr = rr + fr;
        c = fr * 0.5f + fl * 0.5f + c;
    }
    return {fl, fr, c, rl, rr};
}

void downmix51(const float* const ch[6], float* stereo, int frames) {
    for (int i = 0; i < frames; ++i) {
        const float c = ch[kC][i] * kDownmixCentre;
        stereo[2 * i] = ch[kFL][i] + c + ch[kRL][i] * kDownmixRear;
        stereo[2 * i + 1] = ch[kFR][i] + c + ch[kRR][i] * kDownmixRear;
    }
}

Envelope::Envelope(float attackSec, float releaseSec)
    : attack_(int32_t(double(attackSec) * 48000.0)), release_(int32_t(double(releaseSec) * 48000.0)) {}

void Envelope::process(float* x) {
    // 0x826844C8.
    if (state_ == kSustain) return;
    if (state_ == kDone) {
        for (int i = 0; i < kBlock; ++i) x[i] = 0;
        return;
    }
    double g = gain_;
    double gdb = linToDb(float(g));
    if (gdb < -60.0) gdb = -60.0;
    const int32_t n = state_ == kAttack ? attack_ : release_;
    double rate = 15360.0f / float(n);  // dB per pass (60 dB x 256 frames / n); n = 0: infinite
    if (state_ == kRelease) rate = -rate;
    int count = kBlock;
    if (rate > -gdb) {
        count = int(-double(float(gdb / rate)) * 256.0);
        rate = -gdb;
    } else if (state_ == kRelease && float(rate + gdb) < -60.0f) {
        count = int(double(float(gdb - -60.0) * 256.0f) / -rate);
    }
    const double delta = double(dbToLin(float(rate + gdb))) - g;
    int done = 0;
    if (count > 0) {
        for (int i = 0; i < count; ++i) {
            x[i] = float(g * double(x[i]));
            g = float(double(float(delta) / float(count)) + g);
        }
        done = count;
        if (count > kBlock - 1) {
            gain_ = float(g);
            return;
        }
    }
    if (state_ == kAttack) {
        state_ = kSustain;
        g = 1.0;
    } else {
        for (int i = done; i < kBlock; ++i) x[i] = 0;
        state_ = kDone;
        g = 0.0;
    }
    gain_ = float(g);
}

void Limiter::configure(float attackMs, float releaseMs, float thresholdDb, int rate) {
    // 0x82682A68 (parameters 0 / 1 in seconds, at least 0.0001; 2 in dB);
    // Synth360::Init passes attack_ms x 0.001, release_ms x 0.001 [C]. The
    // steps follow the output rate (48000 on the 360).
    const float a = std::fmax(attackMs * 0.001f, 0.0001f), r = std::fmax(releaseMs * 0.001f, 0.0001f);
    attackStep_ = 1.0f / (a * float(rate));
    releaseStep_ = 1.0f / (r * float(rate));
    threshold_ = dbToLin(thresholdDb);
}

void Limiter::process(float* x, int frames, int channels) {
    float g = g_;
    for (int i = 0; i < frames; ++i, x += channels) {
        float peak = 0;
        for (int c = 0; c < channels; ++c) peak = std::fabs(x[c]) <= peak ? peak : std::fabs(x[c]);
        if (peak <= threshold_ * g) g = g - releaseStep_;
        else g = attackStep_ + g;
        if (g < 1.0f) g = 1.0f;
        const float inv = 1.0f / g;
        for (int c = 0; c < channels; ++c) x[c] = x[c] * inv;
    }
    g_ = g;
}

}  // namespace sfxdsp
