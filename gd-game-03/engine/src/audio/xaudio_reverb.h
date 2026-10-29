// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

// The Xbox 360 XAudio built-in reverb (effect id 3, registered at 0x82771750:
// create 0x826AB9A8, factory 0x826AD5D8), reimplemented operation for
// operation from GH2's statically linked copy (docs: xaudio_reverb.cpp's
// header comment has the whole network and every address).
//
// I/O of the original effect (0x826AD2F0) [C]: input = the submix voice's 1
// or 2 channels (mono: the one channel feeds both halves of the network),
// output = 6 channels at 48000 Hz (FL FR C LFE SL SR), of which only FL, FR,
// SL, SR are written (C and LFE are never touched; this port leaves them 0).
// Front = (1 - wet) * dry + wet * reverb, rear = 2 * wet * reverb; wet is
// effect parameter 2, which GH2 never sets, so wet = 1.0 and the output is
// the reverb only (no dry) [C].

// I3DL2 listener parameters, field order and units of XAudio's I3DL2 block
// (0x30 bytes, effect parameter 1) [C].
struct I3DL2Params {
    int32_t room = -10000, roomHF = 0;  // mB
    float roomRolloffFactor = 0, decayTime = 1, decayHFRatio = 0.5f;
    int32_t reflections = -10000;  // mB
    float reflectionsDelay = 0.02f;  // s
    int32_t reverb = -10000;  // mB
    float reverbDelay = 0.04f, diffusion = 100, density = 100, hfReference = 5000;
};

// GH2's 30-entry environment table (0x826848A0) [C]. Returns false and gives
// "default" (the silent preset) for a name not in the table; GH2 itself then
// indexes one entry past the table and sends stack garbage [C], which no
// shipped data does.
bool i3dl2Preset(const std::string& environment, I3DL2Params& out);

class XAudioReverb {
public:
    static constexpr int kBlock = 256;  // XAudio's mixing quantum [C]

    // channels: 1 or 2 (the submix voice's input channel count).
    // sampleRate: XAudio forces the effect to 48000 (0x826AD2F0) [C]; other
    // rates only scale the millisecond delays as the original's formulas do.
    explicit XAudioReverb(int channels, int sampleRate = 48000);
    ~XAudioReverb();
    XAudioReverb(XAudioReverb&&) noexcept;
    XAudioReverb& operator=(XAudioReverb&&) noexcept;

    // Effect parameter 1 (0x826B6A18 -> 0x826B6740 -> 0x826B4D48). Takes
    // effect from the next processed 256-frame block, as in XAudio.
    void setI3DL2(const I3DL2Params& p);

    // The exact effect output: in = frames x channels interleaved, out =
    // frames x 6 interleaved (FL FR C LFE SL SR; C and LFE are 0).
    void process6(const float* in, float* out6, int frames);

    // In-place convenience for the front pair: channels == 2 -> FL FR,
    // channels == 1 -> FL. The rear pair (SL SR) is dropped; use process6
    // for the full output.
    void process(float* interleaved, int frames);

    // Back to the freshly created state (silent buffers, the creation-time
    // delays), then re-applies the last setI3DL2, if any.
    void reset();

    // Frames of delay this wrapper adds on top of the original: 0 while every
    // call so far had a multiple of 256 frames, 256 from the first call that
    // did not (the network runs on whole 256-frame blocks).
    int latency() const;

    struct Core;

private:
    void run(const float* in, float* out6, int frames);

    int channels_;
    int rate_;
    std::unique_ptr<Core> core_;
};
