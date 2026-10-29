// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio/sfx_dsp.h"
#include "audio/spu_reverb.h"

// A decoded mono sample (a SynthSample, audio/synth.h).
struct SfxPcm {
    int rate = 44100;
    std::vector<float> samples;
    bool looping = false;
    uint32_t loopStart = 0, loopEnd = 0;  // frames; loopEnd > loopStart when looping
};

// The sound effect voices: GH2's SampleInst360 / Voice (one XAudio source
// voice each, 0x82689C68) and the FxSend360 submix voices, mixed the way
// the 360's XAudio does (docs/audio_and_charts.md "Playing them"):
//   - 48000 Hz in 256-frame passes (sfxdsp::kRate / kBlock);
//   - a voice plays its sample at int(rate x 0.9975) Hz (0x82689768 scales
//     the SynthSample's rate by the float at 0x820D5C88) [C] times its speed
//     (2^(semitones / 12), at most 2 for XMA, at least 0.01: 0x8268A100)
//     [C]; the sample-rate conversion is linear here (XAudio's own SRC is
//     in the console) [I];
//   - XMA loops start and end on 128-frame multiples (0x82689C68) [C];
//   - the envelope (sfxdsp::Envelope) shapes the voice, stop() releases it;
//   - the voice's 5.1 gains are the pan's (sfxdsp::panGains) times its
//     volume, clamped to 0 .. 1 (0x8268A078) [C], times the master fader
//     (`synth set_master_vol`, applied with the volume before the clamp:
//     SampleInst 0x822FF8D8) [C];
//   - the PS2 disc has no FxSends: a voice whose SfxMap picks an SPU2 core
//     (fx_core 0 / 1) also feeds that core's effect unit (audio/
//     spu_reverb.h) with its own volume and pan, VMIXEL / VMIXER, as the
//     IOP's key-on does (SYNTH_S.IRX 0x0D04) [C]; kFXCoreNone stays dry.
//     The stereo the core gets is the voice's 5.1 gains folded like the
//     master (sfxdsp::downmix51) [I: the 360 pan model this player keeps];
//     each core's wet output (EVOL) returns to the master's FL / FR, core
//     0's also into core 1's effect when (fx (chain TRUE)) [I: MMIX bits];
//   - the 5.1 master folds to stereo (sfxdsp::downmix51) [I].
// The stream is 48000 Hz stereo float; SDL converts it to the device. A
// failed voice start (over kMaxVoices) plays nothing: GH2 sets no voice
// limit of its own and XAudio's (a failed CreateSourceVoice) leaves the
// voice silent, it steals nothing [C for GH2's side; the count is I].
class SfxPlayer {
public:
    SfxPlayer();
    ~SfxPlayer();
    SfxPlayer(const SfxPlayer&) = delete;
    SfxPlayer& operator=(const SfxPlayer&) = delete;

    bool open();
    void close();
    bool isOpen() const { return stream_ != nullptr; }

    // The two SPU2 cores' effects (config/synth.dta's fx; SynthPS2 init
    // 0x002276D8 -> 0x00225D50): each core's unit set up and cleared.
    void setFx(const spu2::FxConfig& fx);
    const spu2::FxConfig& fx() const { return fx_; }

    struct Params {
        float gain = 1;           // linear, before the master fader and the 0..1 clamp
        float pan = 0;            // -4 .. 4 (wrapped), sfxdsp::panGains
        float speed = 1;          // playback-rate ratio
        float attack = 0.001f;    // ADSR attack_rate / release_rate (s)
        float release = 0.005f;
        int fxCore = -1;          // SfxMap fx_core: -1 dry, 0 / 1 that SPU2 core's effect
    };
    using VoiceId = uint64_t;
    VoiceId play(std::shared_ptr<const SfxPcm> pcm, const Params& p);
    // Releases the voice (its envelope's release).
    void stop(VoiceId id);
    // Releases every voice.
    void stopAll();
    // Pauses (or resumes) the voices playing now: they hold their place
    // (SampleInst360 pause 0x8268A018) and the cores' reverb tails go on.
    // Voices started later play: GH2's pause_all_sfx / Game::SetPaused
    // pause each Sfx's current instances (0x822E7C68 -> 0x822ECB40), not
    // the Synth, so the menus after a song keep their sounds [C].
    void setPaused(bool paused);
    // `synth set_master_vol` (dB, -96 = silent): the Synth's master Fader,
    // in every Sfx's fader group (Sfx ctor 0x822EE190) [C].
    void setMasterDb(float db);
    float masterDb() const { return masterDb_.load(); }
    bool playing(VoiceId id) const;
    int voices() const;
    int liveVoices() const;  // the voices not paused
    uint64_t started() const { return started_.load(); }

    // Mixes `frames` stereo frames into `out` (overwrites) without a device
    // (the tests); `out51` (6 planar channels) also gets the 5.1 master
    // before the fold when not null.
    void render(float* out, int frames, std::vector<float>* out51 = nullptr);

    static constexpr size_t kMaxVoices = 256;

private:
    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void mixBlock();  // one 256-frame pass into block_ / master_

    struct Voice {
        VoiceId id = 0;
        std::shared_ptr<const SfxPcm> pcm;
        double pos = 0, step = 1;
        bool loop = false;
        double loopStart = 0, loopEnd = 0;
        float gain = 1;
        sfxdsp::Gains51 pan{};
        int fxCore = -1;
        float fxL = 0, fxR = 0;  // the pan's stereo fold (the core's input gains before the volume)
        sfxdsp::Envelope env;
        bool ended = false;
        bool paused = false;
    };
    SDL_AudioStream* stream_ = nullptr;
    mutable std::mutex lock_;
    std::vector<Voice> voices_;
    spu2::FxConfig fx_{};
    std::array<spu2::SpuReverb, 2> cores_{};
    float coreIn_[2][2 * sfxdsp::kBlock] = {}, coreOut_[2 * sfxdsp::kBlock] = {};
    VoiceId next_ = 1;
    std::atomic<float> masterDb_{0};
    std::atomic<uint64_t> started_{0};
    float master_[6][sfxdsp::kBlock] = {};
    float block_[2 * sfxdsp::kBlock] = {};
    float mono_[sfxdsp::kBlock] = {};
    int blockPos_ = sfxdsp::kBlock;  // frames of block_ already handed out
};
