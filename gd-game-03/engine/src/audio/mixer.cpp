// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/mixer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "audio/audio_device.h"

Mixer::~Mixer() { close(); }

void Mixer::close() {
    stopWorker();
    if (stream_) audiodev::closeStream(stream_);
    stream_ = nullptr;
    source_.reset();
}

bool Mixer::open(std::string& error) {
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate_};
    stream_ = audiodev::openStream(spec, &Mixer::callback, this);
    if (!stream_) {
        error = SDL_GetError();
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void Mixer::startWorker() {
    workerRun_ = true;
    worker_ = std::thread([this] { workerLoop(); });
}

void Mixer::stopWorker() {
    workerRun_ = false;
    if (worker_.joinable()) worker_.join();
}

// Decodes ahead of the callback in blocks, keeping the ring nearly full.
void Mixer::workerLoop() {
    constexpr int kBlock = 2048;
    const size_t channels = ring_.size();
    std::vector<std::vector<float>> block(channels, std::vector<float>(kBlock));
    std::vector<float*> ptrs(channels);
    for (size_t c = 0; c < channels; ++c) ptrs[c] = block[c].data();
    while (workerRun_.load()) {
        int64_t w = written_.load(std::memory_order_relaxed);
        if (sourceEnded_.load() || w - read_.load(std::memory_order_acquire) > kRing - kBlock) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }
        int n = source_->decode(ptrs.data(), kBlock);
        for (size_t c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i) ring_[c][size_t((w + i) & (kRing - 1))] = block[c][size_t(i)];
        written_.store(w + n, std::memory_order_release);
        if (n < kBlock) sourceEnded_ = true;
    }
}

void Mixer::play(std::unique_ptr<AudioSource> stream, std::vector<Track> tracks, double leadInMs) {
    stop();
    SDL_LockAudioStream(stream_);
    SDL_ClearAudioStream(stream_);
    rate_ = stream->sampleRate();
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate_};
    SDL_SetAudioStreamFormat(stream_, &spec, nullptr);
    SDL_SetAudioStreamFrequencyRatio(stream_, 1.0f);
    tracks.resize(size_t(stream->channels()));
    tracks_ = std::move(tracks);
    ring_.assign(size_t(stream->channels()), std::vector<float>(size_t(kRing), 0.0f));
    written_ = 0, read_ = 0, sourceEnded_ = false, underruns_ = 0;
    source_ = std::move(stream);
    leadInFrames_ = static_cast<int64_t>(leadInMs * rate_ / 1000.0);
    leadIn_ = leadInFrames_;
    songFrames_ = 0;
    guitarGain_ = 1, guitarTarget_ = 1;
    guitar2Gain_ = 1, guitar2Target_ = 1;
    anchorFrames_ = 0;
    anchorNs_ = SDL_GetTicksNS();
    paused_ = false;
    frameNs_ = 0;
    SDL_UnlockAudioStream(stream_);
    startWorker();
    SDL_ResumeAudioStreamDevice(stream_);
}

void Mixer::setVolumeDb(float db, float fadeMs) {
    const float target = db <= -96.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
    const float frames = std::max(1.0f, fadeMs / 1000.0f * float(rate_));
    volumeStep_.store(std::fabs(target - volumeTarget_.load()) / frames + 1e-9f);
    volumeTarget_.store(target);
}

void Mixer::setSpeed(float speed) {
    if (stream_) SDL_SetAudioStreamFrequencyRatio(stream_, std::clamp(speed, 0.01f, 100.0f));
}

void Mixer::stop() {
    clockScale_ = 1, clockOffsetMs_ = 0;
    if (!stream_) return;
    stopWorker();
    SDL_LockAudioStream(stream_);
    source_.reset();
    ring_.clear();
    SDL_ClearAudioStream(stream_);
    SDL_UnlockAudioStream(stream_);
}

void Mixer::setPaused(bool paused) {
    if (paused == paused_) return;
    if (paused) {
        pausedTimeMs_ = songTimeMs();
        paused_ = true;
    } else {
        // Re-anchor so the clock resumes where it stopped.
        anchorNs_ = SDL_GetTicksNS();
        anchorFrames_ = static_cast<int64_t>(((pausedTimeMs_ - clockOffsetMs_) / 1000.0) * rate_) + leadIn_;
        paused_ = false;
    }
    frameNs_ = 0;
}

double Mixer::songTimeMs(uint64_t ticksNs) const {
    if (paused_) return pausedTimeMs_;
    if (ticksNs == 0) ticksNs = SDL_GetTicksNS();
    double elapsed = (double(ticksNs) - double(anchorNs_.load())) / 1e9;
    double frames = double(anchorFrames_.load()) + std::max(0.0, elapsed) * rate_ * clockScale_;
    return clockOffsetMs_ + (frames - double(leadIn_.load())) * 1000.0 / rate_;
}

double Mixer::frameTimeMs() {
    uint64_t now = SDL_GetTicksNS();
    double raw = songTimeMs(now);
    if (paused_ || !frameNs_) {
        frameMs_ = raw, frameNs_ = now;
        return raw;
    }
    double predicted = frameMs_ + double(now - frameNs_) / 1e6 * clockScale_;
    double err = raw - predicted;
    // Big jumps (resume, a stalled device) snap; small ones ease in at 5 % a
    // frame, never moving the clock backwards (gd01).
    frameMs_ = std::fabs(err) > 40 ? raw : std::max(frameMs_, predicted + err * 0.05);
    frameNs_ = now;
    return frameMs_;
}

void SDLCALL Mixer::callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<Mixer*>(user);
    int frames = additional / int(2 * sizeof(float));
    if (frames <= 0) return;
    self->fill(frames);
    // Development aid (gd01): GD03_AUDIO_DUMP=<file> writes the mixed output (f32 stereo, the song's rate).
    static std::FILE* dump = SDL_getenv("GD03_AUDIO_DUMP") ? std::fopen(SDL_getenv("GD03_AUDIO_DUMP"), "wb") : nullptr;
    if (dump && self->source_) std::fwrite(self->out_.data(), sizeof(float), self->out_.size(), dump);
    SDL_PutAudioStreamData(stream, self->out_.data(), frames * int(2 * sizeof(float)));
    // The song clock counts only the frames the song played (it stops while paused).
    if (self->source_ && !self->paused_) {
        self->songFrames_ += frames;
        int64_t queued = SDL_GetAudioStreamQueued(stream) / int(2 * sizeof(float));
        self->anchorFrames_ = self->songFrames_ - queued;
        self->anchorNs_ = SDL_GetTicksNS();
    }
}

void Mixer::fill(int frames) {
    out_.assign(size_t(frames) * 2, 0.0f);
    if (!source_ || paused_) return;
    int silent = static_cast<int>(std::clamp<int64_t>(leadInFrames_ - songFrames_, 0, frames));
    int audible = frames - silent;
    if (audible <= 0) return;

    int64_t r = read_.load(std::memory_order_relaxed);
    int64_t available = written_.load(std::memory_order_acquire) - r;
    int n = int(std::min<int64_t>(audible, available));
    if (n < audible && !sourceEnded_.load()) underruns_ += audible - n;

    const float target = guitarTarget_.load(), target2 = guitar2Target_.load();
    const float vTarget = volumeTarget_.load(), vStep = volumeStep_.load();
    for (int i = 0; i < n; ++i) {
        size_t at = size_t((r + i) & (kRing - 1));
        volumeGain_ = volumeGain_ < vTarget ? std::min(vTarget, volumeGain_ + vStep) : std::max(vTarget, volumeGain_ - vStep);
        // ~8 ms ramp, no click (gd01) [I].
        guitarGain_ += (target - guitarGain_) * 0.003f;
        guitar2Gain_ += (target2 - guitar2Gain_) * 0.003f;
        float l = 0, rr = 0;
        for (size_t c = 0; c < tracks_.size(); ++c) {
            const Track& t = tracks_[c];
            float v = ring_[c][at] * t.volume * volumeGain_ * (t.guitar ? guitarGain_ : t.guitar2 ? guitar2Gain_ : 1.0f);
            // gd01's linear pan law: -1 = left only, 0 = both at full, 1 = right only [I].
            l += v * std::clamp(1.0f - t.pan, 0.0f, 1.0f);
            rr += v * std::clamp(1.0f + t.pan, 0.0f, 1.0f);
        }
        out_[size_t(silent + i) * 2] = std::clamp(l, -1.0f, 1.0f);
        out_[size_t(silent + i) * 2 + 1] = std::clamp(rr, -1.0f, 1.0f);
    }
    read_.store(r + n, std::memory_order_release);
}
