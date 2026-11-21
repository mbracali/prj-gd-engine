// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd24's sound: one gd-engine-core audio output (gd::core::audio; the first
// backend that runs here, GD24_AUDIO_BACKEND=<id> to pick one) playing one
// clip at a time, the song previews for now. The clip is resampled (linear)
// when the device does not run at its rate.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "audio/at3_preview.h"

namespace gd::core::audio {
class Output;
}

class AudioOut {
public:
    AudioOut();
    ~AudioOut();
    // Opens the device (stereo, 44.1 kHz asked); false + `error` without sound.
    bool open(std::string& error);
    void close();
    bool isOpen() const { return out_ != nullptr; }
    std::string name() const;

    // Plays `clip` from its start (replacing what played); loops when `loop`.
    void play(std::shared_ptr<const PcmClip> clip, bool loop);
    void stop();
    bool playing() const;
    double positionSec() const;
    void setVolume(float v) { volume_.store(v); }

private:
    void pull(float* out, int frames);

    std::unique_ptr<gd::core::audio::Output> out_;
    int rate_ = 44100, channels_ = 2;
    mutable std::mutex mu_;
    std::shared_ptr<const PcmClip> clip_;
    double pos_ = 0;  // in clip frames
    bool loop_ = false;
    std::atomic<float> volume_{1.0f};
};
