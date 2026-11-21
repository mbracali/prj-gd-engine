// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/fast_pitch.h"

#include <algorithm>

float FastPitch::read(int channel, float lag) const {
    // write_ is one past the newest frame: lag 0 reads the newest.
    const float at = float(write_) - 1.0f - lag + float(kRing);
    const uint32_t i = uint32_t(at);
    const float t = at - float(i);
    const auto& ring = ring_[size_t(channel)];
    return ring[i & (kRing - 1)] * (1 - t) + ring[(i + 1) & (kRing - 1)] * t;
}

void FastPitch::startFade() {
    fadeLag_ = lag_, fadePitch_ = lastPitch_;
    fadeLen_ = std::min(kMaxFade, std::min(kMaxTail, int(lag_)));
    fadeAt_ = 0;
    lag_ = 0;
}

void FastPitch::process(float& l, float& r, float pitch) {
    ring_[0][write_ & (kRing - 1)] = l;
    ring_[1][write_ & (kRing - 1)] = r;
    ++write_;
    if (pitch < 1) {
        lag_ += 1 - pitch;
        if (lag_ > 0) l = read(0, lag_), r = read(1, lag_);
    } else if (lag_ > 0) {
        startFade();
    }
    lastPitch_ = pitch;
    if (fadeAt_ < fadeLen_) {
        fadeLag_ += 1 - fadePitch_;
        const float t = float(fadeAt_++) / float(fadeLen_);
        l = read(0, fadeLag_) * (1 - t) + l * t;
        r = read(1, fadeLag_) * (1 - t) + r * t;
    }
    if (lag_ > kMaxLag) startFade();
}
