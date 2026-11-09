// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/vocal_input.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

#include "gd/core/audio/input.h"

namespace audio_in = gd::core::audio;

VocalInput::VocalInput() = default;

VocalInput::~VocalInput() { close(); }

bool VocalInput::open(const std::string& preferred, std::string& error) {
    close();
    if (const char* file = SDL_getenv("GD05_VOCALS_FILE"); file && *file) {
        // A WAV file, converted to mono float at its own rate.
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        SDL_IOStream* io = SDL_IOFromFile(file, "rb");
        if (!io || !SDL_LoadWAV_IO(io, true, &spec, &buf, &len)) {
            error = std::string("GD05_VOCALS_FILE: ") + SDL_GetError();
            return false;
        }
        SDL_AudioSpec mono{SDL_AUDIO_F32, 1, spec.freq};
        Uint8* out = nullptr;
        int outLen = 0;
        const bool ok = SDL_ConvertAudioSamples(&spec, buf, int(len), &mono, &out, &outLen);
        SDL_free(buf);
        if (!ok) {
            error = std::string("GD05_VOCALS_FILE: ") + SDL_GetError();
            return false;
        }
        file_.assign(reinterpret_cast<float*>(out), reinterpret_cast<float*>(out) + outLen / int(sizeof(float)));
        SDL_free(out);
        rate_ = spec.freq;
        detector_ = std::make_unique<PitchDetector>(rate_);
        name_ = std::string("file ") + file;
        open_ = true;
        return true;
    }
    std::vector<std::string> ids = audio_in::availableInputs();
    if (!preferred.empty()) {
        auto it = std::find(ids.begin(), ids.end(), preferred);
        if (it != ids.end()) std::rotate(ids.begin(), it, it + 1);
    }
    std::string why;
    for (const std::string& id : ids) {
        std::unique_ptr<audio_in::Input> in = audio_in::createInput(id, why);
        if (!in) continue;
        audio_in::InputSpec want;
        want.sampleRate = 48000;
        want.channels = 1;
        // SDL's dummy audio driver (the tests): a silent device in real time.
        if (const char* d = SDL_getenv("SDL_AUDIO_DRIVER"); d && std::strcmp(d, "dummy") == 0) want.nullDevice = true;
        const bool ok = in->open(want, [this](const float* f, int frames) {
            std::lock_guard<std::mutex> g(lock_);
            pending_.insert(pending_.end(), f, f + frames);
            if (pending_.size() > 48000) pending_.erase(pending_.begin(), pending_.end() - 48000);
        }, why);
        if (!ok || !in->start(why)) continue;
        rate_ = in->spec().sampleRate;
        detector_ = std::make_unique<PitchDetector>(rate_);
        name_ = in->name();
        mic_ = std::move(in);
        open_ = true;
        return true;
    }
    error = ids.empty() ? "no capture backend" : why;
    return false;
}

void VocalInput::close() {
    if (mic_) mic_->close();
    mic_.reset();
    open_ = false;
    file_.clear();
    filePos_ = 0;
    pending_.clear();
}

double VocalInput::latencyMs() const { return mic_ ? mic_->latencyMs() : 0.0; }

float VocalInput::poll(float songMs, float& loudness) {
    loudness = 0;
    if (!open_ || !detector_) return -1;
    std::vector<float> got;
    if (mic_) {
        std::lock_guard<std::mutex> g(lock_);
        got.swap(pending_);
    } else {
        // The file plays from the song's start (song time 0 = its first sample).
        const size_t want = songMs <= 0 ? 0 : std::min(file_.size(), size_t(double(songMs) * rate_ / 1000.0));
        if (want > filePos_) got.assign(file_.begin() + long(filePos_), file_.begin() + long(want)), filePos_ = want;
    }
    if (got.empty()) {
        loudness = lastLoud_;
        return lastPitch_;
    }
    detector_->push(got.data(), int(got.size()));
    samples_ += (long long)got.size();
    lastPitch_ = detector_->pitch(lastLoud_);
    loudness = lastLoud_;
    return lastPitch_;
}
