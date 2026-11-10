// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio/pitch_detector.h"

namespace gd::core::audio {
class Input;
}

// The singer's voice for the vocal judge (game/vocal_player.h): a
// microphone through gd-engine-core's audio input (GDCORE-011), or a WAV
// file (GD05_VOCALS_FILE=<file.wav>, played against the song clock) so the
// vocals can be tested headless. Each poll gives the pitch of the latest
// window (PitchDetector) and its loudness.
class VocalInput {
public:
    VocalInput();
    ~VocalInput();
    VocalInput(const VocalInput&) = delete;
    VocalInput& operator=(const VocalInput&) = delete;

    // Opens GD05_VOCALS_FILE when set, else the default recording device
    // of the first capture backend that opens (`preferred` first). False
    // with `error` when there is no voice source (the judge then hears
    // silence).
    bool open(const std::string& preferred, std::string& error);
    void close();
    bool isOpen() const { return open_; }
    const std::string& name() const { return name_; }

    // The voice up to song time `songMs`: MIDI pitch with fractions or -1,
    // `loudness` its RMS (0..1).
    float poll(float songMs, float& loudness);
    // The capture's latency in ms (the mic's; 0 for a file).
    double latencyMs() const;
    // Samples heard so far (the tests).
    long long samples() const { return samples_; }

private:
    std::unique_ptr<gd::core::audio::Input> mic_;
    std::unique_ptr<PitchDetector> detector_;
    std::mutex lock_;
    std::vector<float> pending_;  // pushed by the capture thread
    std::vector<float> file_;     // a WAV file's mono samples
    int rate_ = 48000;
    size_t filePos_ = 0;
    bool open_ = false;
    long long samples_ = 0;
    float lastPitch_ = -1, lastLoud_ = 0;
    std::string name_;
};
