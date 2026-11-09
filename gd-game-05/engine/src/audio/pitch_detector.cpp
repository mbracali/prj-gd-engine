// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/pitch_detector.h"

#include <algorithm>
#include <cmath>

namespace {
// The vocal range of PART VOCALS (MIDI 36..84 [C RB 0x824A1770]) with room:
// 60 Hz .. 1200 Hz.
constexpr float kMinHz = 60.0f, kMaxHz = 1200.0f;
constexpr float kThreshold = 0.15f;    // the dip that counts as a period [I]
constexpr float kQuietRms = 0.005f;    // below: no voice [I]
}  // namespace

PitchDetector::PitchDetector(int sampleRate, float windowMs)
    : rate_(sampleRate), window_(std::max(64, int(float(sampleRate) * windowMs / 1000.0f))), ring_(size_t(window_), 0.0f) {}

void PitchDetector::push(const float* samples, int count) {
    for (int i = 0; i < count; ++i) {
        ring_[pos_] = samples[i];
        pos_ = (pos_ + 1) % ring_.size();
    }
    filled_ = std::min(ring_.size(), filled_ + size_t(std::max(count, 0)));
}

float PitchDetector::pitch(float& loudness) {
    loudness = 0;
    if (filled_ < ring_.size()) return -1;
    frame_.resize(ring_.size());
    for (size_t i = 0; i < ring_.size(); ++i) frame_[i] = ring_[(pos_ + i) % ring_.size()];
    double sum = 0;
    for (float x : frame_) sum += double(x) * x;
    loudness = float(std::sqrt(sum / double(frame_.size())));
    if (loudness < kQuietRms) return -1;
    const int minLag = std::max(2, int(float(rate_) / kMaxHz)), maxLag = std::min(window_ / 2, int(float(rate_) / kMinHz));
    const int half = window_ / 2;
    diff_.assign(size_t(maxLag + 2), 0.0f);
    for (int lag = 1; lag <= maxLag + 1; ++lag) {
        double d = 0;
        for (int i = 0; i < half; ++i) {
            const double e = double(frame_[size_t(i)]) - frame_[size_t(i + lag)];
            d += e * e;
        }
        diff_[size_t(lag)] = float(d);
    }
    // Cumulative-mean normalised difference, then the first dip under the threshold.
    double running = 0;
    diff_[0] = 1;
    for (int lag = 1; lag <= maxLag + 1; ++lag) {
        running += diff_[size_t(lag)];
        diff_[size_t(lag)] = running > 0 ? float(diff_[size_t(lag)] * lag / running) : 1.0f;
    }
    int best = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (diff_[size_t(lag)] < kThreshold) {
            while (lag + 1 <= maxLag && diff_[size_t(lag + 1)] < diff_[size_t(lag)]) ++lag;
            best = lag;
            break;
        }
    if (best < 0) return -1;
    // Parabolic interpolation around the dip.
    const float a = diff_[size_t(best - 1)], b = diff_[size_t(best)], c = diff_[size_t(best + 1)];
    const float den = a - 2 * b + c;
    const float lag = float(best) + (std::fabs(den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f);
    const float hz = float(rate_) / lag;
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}
