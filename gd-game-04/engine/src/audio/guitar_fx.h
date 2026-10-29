// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <mutex>
#include <vector>

// The guitar buss's effects, as GH3's scripts set them with
// setsoundbusseffects (GH_Star_Power_Verb_On / _Off, GH3_Set_Guitar_Verb_
// And_Echo_to_Dry: `GuitarEcho1` an echo, `GuitarReverb1` an sfxreverb)
// and SetSoundBussParams (Star_Power_BussSet: the Right_Notes busses' vol
// in dB) [C scripts, data: Echo_Guitar_Buss_Dry, Reverb_Guitar_Buss_Dry].
// The parameters are the scripts' (delay ms, DecayRatio, Drymix / Wetmix;
// I3DL2 levels in millibels, DecayTime s); the DSP is gd04's: a feedback
// delay for the echo and a small Schroeder reverb (four combs, two
// all-passes) for the I3DL2 reverb [I: FMOD's echo / sfxreverb units are
// not reimplemented]. Set from the main thread, run in the audio callback.
class GuitarFx {
public:
    struct Params {
        float echoDelayMs = 400, echoDecay = 0.15f, echoDry = 1, echoWet = 0;
        float reverbLevelMb = -10000, reflectionsLevelMb = -10000, decayTime = 2.2f, reflectionsDelay = 0, reverbDelay = 0.04f;
        float bussDb = 0;  // Right_Notes_Player1's vol
    };
    void setRate(int rate);
    // New targets, reached linearly over `timeS` (the scripts' time = ...).
    void set(const Params& p, float timeS);
    Params params() const;
    // One stereo frame through the buss (call per frame from the callback).
    void process(float& l, float& r);
    void reset();

private:
    struct Comb {
        std::vector<float> buf;
        size_t pos = 0;
        float fb = 0, lp = 0;
    };
    struct AllPass {
        std::vector<float> buf;
        size_t pos = 0;
    };
    void tune();
    mutable std::mutex m_;
    Params target_, cur_, from_;
    float rampLeft_ = 0, ramp_ = 0;
    bool dirty_ = true;
    int rate_ = 48000;
    std::array<std::vector<float>, 2> echo_;
    size_t echoPos_ = 0;
    std::array<std::array<Comb, 4>, 2> comb_;
    std::array<std::array<AllPass, 2>, 2> ap_;
    std::array<std::vector<float>, 2> pre_;
    size_t prePos_ = 0;
    float tunedDecay_ = -1;
};
