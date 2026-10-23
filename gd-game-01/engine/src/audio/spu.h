// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "audio/adpcm.h"

// A software SPU2 for the sound effects (docs/audio_sfx.md §3, §5): 48 voices
// (core 0 = 0..23, core 1 = 24..47), the SPU2 ADSR envelope, 4.12 integer
// pitch, the constant-power pan table, and the dry / echo / large-studio
// buses. Thread safe: the game calls the voice functions, the audio thread mix().
class Spu {
public:
    struct KeyOn {
        std::shared_ptr<const AdpcmSample> sample;
        int rate = 22050;
        float speed = 1;  // transpose ratio(s)
        float volume = 1;  // 0..1
        float pan = 0;     // -1..1
        uint16_t adsr1 = 0x8F1F, adsr2 = 0x03C7;
        int core = -1;     // -1 dry (any voice), 0 echo send, 1 large-studio send
    };

    Spu();
    ~Spu();
    // Allocates a free voice (no stealing) and keys it on; returns its id, or 0.
    int keyOn(const KeyOn& k);
    void keyOff(int id);  // the ADSR release
    void keyOffAll();
    void setVolumePan(int id, float volume, float pan);
    void setSpeed(int id, float speed);
    bool playing(int id);
    // pause_all_sfx: volumes and pitches to 0, then restored.
    void setPaused(bool paused);
    // The master volume (MVOL; FadePanel fades it).
    void setMaster(float v);
    float master() const { return master_; }
    // Echo (core 0, wet 0.3) and large studio (core 1, wet 0.4) from config/synth.dta.
    void setEffects(float echoWet, float echoDelay, float echoFeedback, float studioWet);

    // Adds the voices into interleaved stereo `out` at `rate` Hz.
    void mix(float* out, int frames, int rate);

private:
    struct Voice;
    struct Reverb;
    std::mutex mutex_;
    std::vector<Voice> voices_;
    std::unique_ptr<Reverb> echo_, studio_;
    std::vector<float> send0_, send1_;
    uint32_t freeCounter_ = 0xFFFFFFFE;
    int nextId_ = 1;
    float master_ = 1, echoWet_ = 0.3f, studioWet_ = 0.4f;
};
