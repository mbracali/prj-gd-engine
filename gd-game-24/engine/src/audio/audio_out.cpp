// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/audio_out.h"

#include <SDL3/SDL.h>

#include <algorithm>

#include "gd/core/audio/output.h"

namespace ga = gd::core::audio;

AudioOut::AudioOut() = default;
AudioOut::~AudioOut() { close(); }

bool AudioOut::open(std::string& error) {
    std::vector<std::string> ids;
    if (const char* want = SDL_getenv("GD24_AUDIO_BACKEND")) ids.emplace_back(want);
    for (const std::string& id : ga::available()) ids.push_back(id);
    if (ids.empty()) {
        error = "no audio backend runs here";
        return false;
    }
    for (const std::string& id : ids) {
        std::string why;
        auto out = ga::create(id, why);
        ga::Spec want;
        want.sampleRate = 44100, want.channels = 2;
        want.nullDevice = SDL_getenv("GD24_NULL_AUDIO") != nullptr;
        if (out && out->open(want, [this](float* o, int n) { pull(o, n); }, why) && out->start(why)) {
            rate_ = out->spec().sampleRate, channels_ = out->spec().channels;
            out_ = std::move(out);
            SDL_Log("audio: %s, %d Hz, %d channels", out_->name().c_str(), rate_, channels_);
            return true;
        }
        error += (error.empty() ? "" : "; ") + id + ": " + why;
    }
    return false;
}

void AudioOut::close() {
    if (out_) out_->close();
    out_.reset();
}

std::string AudioOut::name() const { return out_ ? out_->name() : std::string("none"); }

void AudioOut::play(std::shared_ptr<const PcmClip> clip, bool loop) {
    std::lock_guard<std::mutex> lock(mu_);
    clip_ = std::move(clip), pos_ = 0, loop_ = loop;
}

void AudioOut::stop() {
    std::lock_guard<std::mutex> lock(mu_);
    clip_.reset();
}

bool AudioOut::playing() const {
    std::lock_guard<std::mutex> lock(mu_);
    return clip_ != nullptr;
}

double AudioOut::positionSec() const {
    std::lock_guard<std::mutex> lock(mu_);
    return clip_ && clip_->rate ? pos_ / clip_->rate : 0;
}

void AudioOut::pull(float* out, int frames) {
    std::fill(out, out + size_t(frames) * size_t(channels_), 0.0f);
    std::lock_guard<std::mutex> lock(mu_);
    if (!clip_ || !clip_->frames()) return;
    const PcmClip& c = *clip_;
    const size_t n = c.frames();
    const double step = double(c.rate) / double(rate_);
    const float vol = volume_.load();
    for (int i = 0; i < frames; ++i) {
        if (pos_ >= double(n - 1)) {
            if (!loop_) {
                clip_.reset();
                return;
            }
            pos_ = 0;
        }
        const size_t k = size_t(pos_);
        const float t = float(pos_ - double(k));
        for (int ch = 0; ch < channels_; ++ch) {
            const int src = std::min(ch, c.channels - 1);
            const float a = c.samples[k * size_t(c.channels) + size_t(src)];
            const float b = c.samples[(k + 1) * size_t(c.channels) + size_t(src)];
            out[size_t(i) * size_t(channels_) + size_t(ch)] = (a + (b - a) * t) * vol;
        }
        pos_ += step;
    }
}
