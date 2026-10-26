// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>

// The small DSP pieces GH2's Xbox 360 Synth applies to its sound effect
// voices, each as the executable computes it (docs/audio_and_charts.md
// "Playing them"). The 360's XAudio mixes in blocks of 256 frames at
// 48000 Hz; the envelope and the limiter step per block / per frame of
// that clock.
namespace sfxdsp {

constexpr int kRate = 48000;   // XAudio's mixing rate (0x82684430, 0x82682A68: seconds x 48000) [C]
constexpr int kBlock = 256;    // frames per XAudio processing pass (0x826844C8, 0x82682958) [C]

// dB <-> linear as GH2 converts them: 0x826910A0 (dB > -96 -> 10^(dB/20),
// else 0) and 0x826910F8 (x > 0 -> 20 log10 x, else -96) [C].
float dbToLin(float db);
float linToDb(float lin);

// A voice's pan, -4 .. 4, wrapped as SetPan does: fmod(pan + 4, 8) - 4
// (0x8268A0A8 with 0x82192610) [C].
float wrapPan(float pan);

// The 5.1 speaker gains of a pan (0x826898C8) [C]: equal power (cos / sin
// of a quarter turn) between neighbouring speakers at -5 RR, -3 RL, -1 FL,
// 0 C, 1 FR, 3 RR, 5 RL. XAudio's channel order: FL FR C LFE RL RR.
enum Speaker { kFL = 0, kFR = 1, kC = 2, kLFE = 3, kRL = 4, kRR = 5 };
using Gains51 = std::array<float, 6>;
Gains51 panGains(float pan);

// The send of a voice (0x826898C8) [C]: the send volume times the pan
// gains (not the voice's volume); for an FxSendReverb the front is also
// folded into the centre (+ half of each front) and the rears (+ their
// side's front). Returned in the order the FxSend's three submix voices
// take them: front L, front R, centre, rear L, rear R.
std::array<float, 5> sendGains(const Gains51& pan, float sendVolume, bool reverb);

// The stereo fold-down of the 5.1 master when the console's output is
// stereo. This happens in the 360's system software, not in GH2: gd02 uses
// the ITU-R BS.775 Lo/Ro fold (L = FL + 0.7071 C + 0.7071 RL, R = FR +
// 0.7071 C + 0.7071 RR, LFE dropped) [I].
constexpr float kDownmixCentre = 0.70710678f, kDownmixRear = 0.70710678f;
void downmix51(const float* const ch[6], float* stereo, int frames);

// GH2's voice envelope: its own XAudio effect id 4 (vtable 0x820D4F6C;
// SetParam 0x82684430, GetParam 0x826843C0, Process 0x826844C8) [C]. Only
// the ADSR's attack and release are used (SampleInst360::SetADSR
// 0x8268A590): the gain rises from silence to 1 over `attack` s and falls
// to silence over `release` s once released (SampleInst360::Stop 0x826896F0
// sets parameter 2). Each 256-frame pass moves the gain 60 dB x 256 /
// (time x 48000) dB, stepping linearly in amplitude across the pass, from a
// floor of -60 dB; the attack ends at 0 dB, the release below -60 dB.
class Envelope {
public:
    enum State : int32_t { kAttack = 0, kSustain = 1, kRelease = 2, kDone = 3 };
    Envelope() = default;
    Envelope(float attackSec, float releaseSec);
    void release() { if (state_ != kDone) state_ = kRelease; }
    // One 256-frame pass over a mono buffer (in place).
    void process(float* x);
    State state() const { return state_; }
    float gain() const { return gain_; }

private:
    int32_t attack_ = 48, release_ = 240;  // frames: SampleInst default ADSR 0.001 / 0.005 s (0x822F8898)
    float gain_ = 0;
    State state_ = kAttack;
};

// The master limiter: GH2's XAudio effect id 9 on the mastering voice
// (vtable 0x820D4DEC; SetParam 0x82682A68, Process 0x82682958), set from
// default.dta (synth (limiter (attack_ms 1) (release_ms 50) (threshold
// -2))) in Synth360::Init 0x82682558 [C]. Per frame: the peak over the
// channels; above threshold x g the divisor g grows by 1 / (attack x rate),
// else it falls by 1 / (release x rate), never below 1; every channel is
// divided by g.
class Limiter {
public:
    void configure(float attackMs, float releaseMs, float thresholdDb, int rate);
    void process(float* interleaved, int frames, int channels);
    float divisor() const { return g_; }
    void reset() { g_ = 1; }

private:
    float attackStep_ = 1.0f / 48, releaseStep_ = 1.0f / 2400, threshold_ = 1, g_ = 1;
};

}  // namespace sfxdsp
