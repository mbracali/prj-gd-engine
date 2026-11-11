// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <vector>

// The singer's pitch from microphone samples, for the vocal judge
// (game/vocal_player.h). Written for this project: the difference function
// with the cumulative-mean normalisation and the first dip under a
// threshold (the YIN idea from the literature), parabolic interpolation,
// over the last `windowMs` of mono samples. Rock Band's own detector is
// not read yet (its output feeds the judge the same way: a MIDI pitch and a
// loudness per packet) [I] (docs/vocals.md).
class PitchDetector {
public:
    explicit PitchDetector(int sampleRate, float windowMs = 40.0f);
    // Adds mono samples (-1..1).
    void push(const float* samples, int count);
    // The pitch of the latest window as a MIDI note number with fractions
    // (69 = A4 440 Hz), or -1 when unvoiced / too quiet; `loudness` gets the
    // window's RMS (0..1).
    float pitch(float& loudness);

private:
    int rate_, window_;
    std::vector<float> ring_;
    size_t pos_ = 0, filled_ = 0;
    std::vector<float> diff_, frame_;
};
