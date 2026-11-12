// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>

// GH3's FastPitchShift, the "Neversoft Fast Pitch Shifter" on each player's
// guitar buss [C 0x826E5C38; script data PitchShiftEffect1 / 2]. The whammy
// and the boss battle's pitch down set it (set_whammy_pitchshift,
// BossBattle_PitchDown).
//
// Below pitch 1 it is a linear resampler reading an 8192-frame history: the
// read point falls behind the input by (1 - pitch) frames a frame. Past 4096
// frames of lag it jumps back to the input, and at pitch 1 or above the
// output is the input. Either jump crossfades from where the old read point
// was going, over at most 100 frames.
//
// The original runs on FMOD's blocks and takes a new pitch per block; this
// runs per frame with no block of latency, so the guitar stays in time with
// the band at rest [I for the block size].
class FastPitch {
public:
    static constexpr int kRing = 1 << 13;  // 8192 frames, one per channel
    static constexpr float kMaxLag = 4096;
    static constexpr int kMaxFade = 100, kMaxTail = 256;

    // One stereo frame, in place. Audio thread only.
    void process(float& l, float& r, float pitch);
    // Frames the read point is behind the input.
    float lag() const { return lag_; }

private:
    float read(int channel, float lag) const;
    void startFade();

    std::array<std::array<float, kRing>, 2> ring_{};
    uint32_t write_ = 0;
    float lag_ = 0;
    float lastPitch_ = 1;
    // The read point being faded out.
    float fadeLag_ = 0, fadePitch_ = 1;
    int fadeLen_ = 0, fadeAt_ = 0;
};
