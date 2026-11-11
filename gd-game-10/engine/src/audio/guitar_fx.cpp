// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/guitar_fx.h"

#include <algorithm>
#include <cmath>

namespace {
// Freeverb's comb and all-pass lengths at 44100 Hz, the right channel 23
// samples longer (stereo spread) [I: gd10's reverb].
constexpr int kComb[GuitarFx::kCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllPass[2] = {556, 441};
constexpr int kSpread = 23;
// The early reflections: four taps (ms after ReflectionsDelay) of gain 0.5
// (unit energy), the right channel's second and fourth inverted. A single
// tap at the default ReflectionsDelay 0 was the dry signal again: it added
// in phase (+1.9 dB at ReflectionsLevel -1200 instead of +0.3) [I: gd10's;
// FMOD's reflection pattern is not reimplemented].
constexpr float kReflMs[4] = {7.1f, 11.3f, 17.9f, 23.7f};
float mbToGain(float mb) { return mb <= -9999 ? 0.0f : std::pow(10.0f, mb / 2000.0f); }
float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
}  // namespace

void GuitarFx::setRate(int rate) {
    std::lock_guard<std::mutex> g(m_);
    if (rate <= 0 || rate == rate_) return;
    rate_ = rate;
    tunedDecay_ = -1;
    echo_[0].clear(), echo_[1].clear();
}

void GuitarFx::set(const Params& p, float timeS) {
    std::lock_guard<std::mutex> g(m_);
    from_ = cur_;
    target_ = p;
    ramp_ = rampLeft_ = std::max(0.0f, timeS) * float(rate_);
    if (ramp_ <= 0) cur_ = target_;
    dirty_ = true;
}

GuitarFx::Params GuitarFx::params() const {
    std::lock_guard<std::mutex> g(m_);
    return target_;
}

void GuitarFx::reset() {
    std::lock_guard<std::mutex> g(m_);
    target_ = cur_ = from_ = Params{};
    rampLeft_ = ramp_ = 0;
    for (auto& e : echo_) std::fill(e.begin(), e.end(), 0.0f);
    for (auto& ch : comb_)
        for (Comb& c : ch) std::fill(c.buf.begin(), c.buf.end(), 0.0f), c.lp = 0;
    for (auto& ch : ap_)
        for (AllPass& a : ch) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    for (auto& p : pre_) std::fill(p.begin(), p.end(), 0.0f);
}

void GuitarFx::tune() {
    const float k = float(rate_) / 44100.0f;
    if (echo_[0].empty()) {
        for (auto& e : echo_) e.assign(size_t(rate_ * 2) + 1, 0.0f);
        for (auto& p : pre_) p.assign(size_t(rate_ / 2) + 1, 0.0f);
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < kCombs; ++i) comb_[size_t(c)][size_t(i)].buf.assign(size_t(float(kComb[i] + c * kSpread) * k) + 1, 0.0f);
            for (int i = 0; i < 2; ++i) ap_[size_t(c)][size_t(i)].buf.assign(size_t(float(kAllPass[i] + c * kSpread) * k) + 1, 0.0f);
        }
        echoPos_ = prePos_ = 0;
    }
    // The combs' feedback for an RT60 of DecayTime: g = 10^(-3 delay / T).
    for (auto& ch : comb_)
        for (Comb& c : ch) c.fb = std::pow(10.0f, -3.0f * float(c.buf.size()) / float(rate_) / std::max(0.1f, cur_.decayTime));
    tunedDecay_ = cur_.decayTime;
    // The network's energy for a unit impulse (left channel; the right's
    // lengths differ by kSpread only), over 1.5 x DecayTime (< -90 dB left).
    // Unscaled (0.25) it was ~+8 dB over the level asked: the SP reverb at
    // ReverbLevel -550 came out near +3 dB and the guitar buss ~6 dB louder
    // than its dry level, clipping on loud guitar (owner 11-05: distortion).
    std::array<Comb, kCombs> cs = comb_[0];
    std::array<AllPass, 2> as = ap_[0];
    for (Comb& c : cs) std::fill(c.buf.begin(), c.buf.end(), 0.0f), c.pos = 0, c.lp = 0;
    for (AllPass& a : as) std::fill(a.buf.begin(), a.buf.end(), 0.0f), a.pos = 0;
    double energy = 0;
    const int n = int(float(rate_) * 1.5f * std::max(0.1f, cur_.decayTime));
    for (int i = 0; i < n; ++i) {
        const float acc = runLate(cs, as, i == 0 ? 1.0f : 0.0f);
        energy += double(acc) * acc;
    }
    lateNorm_ = energy > 1e-9 ? float(1.0 / std::sqrt(energy)) : 0.0f;
}

float GuitarFx::runLate(std::array<Comb, kCombs>& combs, std::array<AllPass, 2>& aps, float x) {
    float acc = 0;
    for (Comb& cb : combs) {
        const float y = cb.buf[cb.pos];
        cb.lp = y * 0.8f + cb.lp * 0.2f;  // damping
        cb.buf[cb.pos] = x + cb.lp * cb.fb;
        cb.pos = (cb.pos + 1) % cb.buf.size();
        acc += y;
    }
    for (AllPass& a : aps) {
        const float b = a.buf[a.pos];
        a.buf[a.pos] = acc + b * 0.5f;
        a.pos = (a.pos + 1) % a.buf.size();
        acc = b - acc;
    }
    return acc;
}

void GuitarFx::process(float& l, float& r) {
    // Pick up new targets without blocking the callback.
    if (std::unique_lock<std::mutex> g(m_, std::try_to_lock); g.owns_lock()) {
        if (echo_[0].empty() || tunedDecay_ != cur_.decayTime) tune();
        if (rampLeft_ > 0) {
            rampLeft_ -= 1;
            const float f = ramp_ > 0 ? 1.0f - rampLeft_ / ramp_ : 1.0f;
            auto mix = [f](float a, float b) { return a + (b - a) * f; };
            cur_.echoDry = mix(from_.echoDry, target_.echoDry), cur_.echoWet = mix(from_.echoWet, target_.echoWet);
            cur_.echoDecay = mix(from_.echoDecay, target_.echoDecay);
            cur_.reverbLevelMb = mix(from_.reverbLevelMb, target_.reverbLevelMb);
            cur_.reflectionsLevelMb = mix(from_.reflectionsLevelMb, target_.reflectionsLevelMb);
            cur_.bussDb = mix(from_.bussDb, target_.bussDb);
            cur_.echoDelayMs = target_.echoDelayMs, cur_.decayTime = target_.decayTime;
            cur_.reflectionsDelay = target_.reflectionsDelay, cur_.reverbDelay = target_.reverbDelay;
        } else if (dirty_) {
            cur_ = target_;
            dirty_ = false;
        }
    }
    if (echo_[0].empty()) return;
    const float buss = cur_.bussDb == 0 ? 1.0f : dbToGain(cur_.bussDb);
    float in[2] = {l * buss, r * buss}, out[2];
    // The echo: dry x in + wet x the delayed signal, fed back by DecayRatio.
    const size_t n = echo_[0].size();
    const size_t delay = std::clamp<size_t>(size_t(cur_.echoDelayMs * float(rate_) / 1000.0f), 1, n - 1);
    const size_t at = (echoPos_ + n - delay) % n;
    for (int c = 0; c < 2; ++c) {
        const float d = echo_[size_t(c)][at];
        echo_[size_t(c)][echoPos_] = in[c] + d * cur_.echoDecay;
        out[c] = in[c] * cur_.echoDry + d * cur_.echoWet;
    }
    echoPos_ = (echoPos_ + 1) % n;
    // The reverb (dry 0: added on top): the reflections tap and the late
    // reverb from the pre-delay line.
    const float late = mbToGain(cur_.reverbLevelMb), refl = mbToGain(cur_.reflectionsLevelMb);
    const size_t pn = pre_[0].size();
    const float mono = (out[0] + out[1]) * 0.5f;
    pre_[0][prePos_] = mono;
    if (late > 0 || refl > 0) {
        const size_t rd = std::min(pn - 1, size_t(cur_.reverbDelay * float(rate_)));
        const size_t ed = std::min(pn - 1, size_t(cur_.reflectionsDelay * float(rate_)));
        const float x = pre_[0][(prePos_ + pn - rd) % pn];
        float e[2] = {0, 0};
        for (int k = 0; k < 4; ++k) {
            const size_t d = std::min(pn - 1, ed + size_t(kReflMs[k] * float(rate_) / 1000.0f));
            const float tap = 0.5f * pre_[0][(prePos_ + pn - d) % pn];
            e[0] += tap, e[1] += k % 2 ? -tap : tap;
        }
        for (int c = 0; c < 2; ++c) {
            const float acc = runLate(comb_[size_t(c)], ap_[size_t(c)], x);
            out[c] += acc * lateNorm_ * late + e[c] * refl;
        }
    }
    prePos_ = (prePos_ + 1) % pn;
    l = out[0], r = out[1];
}
