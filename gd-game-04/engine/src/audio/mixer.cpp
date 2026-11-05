// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/mixer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "audio/audio_device.h"
#include "audio/sound_buss.h"

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
    bussGain_.assign(tracks_.size(), 1.0f);
    for (size_t c = 0; c < tracks_.size(); ++c) bussGain_[c] = soundBusses().gainAt(tracks_[c].buss);
    ring_.assign(size_t(stream->channels()), std::vector<float>(size_t(kRing), 0.0f));
    written_ = 0, read_ = 0, sourceEnded_ = false, underruns_ = 0;
    silenced_ = false;
    buss_ = {}, pitched_ = false, ratio_ = 1;
    speed_ = 1, slowPitch_ = 1, slowShift_ = SlowShift{};
    source_ = std::move(stream);
    leadInFrames_ = static_cast<int64_t>(leadInMs * rate_ / 1000.0);
    leadIn_ = leadInFrames_;
    songFrames_ = 0;
    guitarGain_ = 1, guitarTarget_ = 1;
    guitar2Gain_ = 1, guitar2Target_ = 1;
    guitarFx_.setRate(rate_);
    anchorFrames_ = 0;
    anchorNs_ = SDL_GetTicksNS();
    paused_ = false;
    frameNs_ = 0;
    SDL_UnlockAudioStream(stream_);
    startWorker();
    SDL_ResumeAudioStreamDevice(stream_);
}

void Mixer::setSlomo(float speed, float guitarPitch) {
    if (!stream_) return;
    speed = std::clamp(speed, 0.05f, 4.0f);
    // Re-anchored where the clock is, so it changes rate from here.
    const uint64_t now = SDL_GetTicksNS();
    if (!paused_) anchorFrames_ = int64_t(songTimeMs(now) * rate_ / 1000.0) + leadIn_, anchorNs_ = now;
    speed_ = speed;
    slowPitch_ = guitarPitch > 0 ? guitarPitch : 1.0f;
    frameNs_ = 0;
    const float st = pitched_.load() ? buss_[0].st(now) : 0.0f;
    ratio_ = speed * std::exp2(st / 12.0f);
    SDL_SetAudioStreamFrequencyRatio(stream_, std::clamp(ratio_, 0.01f, 100.0f));
}

void Mixer::setBuss(bool guitar, float db, float semitones, float seconds) {
    if (!stream_) return;
    SDL_LockAudioStream(stream_);
    Buss& b = buss_[guitar ? 1 : 0];
    const uint64_t now = SDL_GetTicksNS();
    b.fromDb = b.db(now), b.fromSt = b.st(now);
    b.toDb = db, b.toSt = semitones, b.seconds = std::max(0.0f, seconds), b.startNs = now;
    if (!guitar && (semitones != 0 || b.fromSt != 0)) pitched_ = true;
    SDL_UnlockAudioStream(stream_);
}

// The band buss's pitch as the stream's resampling ratio (frameTimeMs, each frame).
void Mixer::updatePitch() {
    if (!stream_ || !pitched_.load()) return;
    SDL_LockAudioStream(stream_);
    const float st = buss_[0].st(SDL_GetTicksNS());
    SDL_UnlockAudioStream(stream_);
    const float ratio = speed_.load() * std::exp2(st / 12.0f);
    if (std::fabs(ratio - ratio_) > 1e-4f) ratio_ = ratio, SDL_SetAudioStreamFrequencyRatio(stream_, std::clamp(ratio, 0.01f, 100.0f));
}

void Mixer::stop() {
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
        anchorFrames_ = static_cast<int64_t>((pausedTimeMs_ / 1000.0) * rate_) + leadIn_;
        paused_ = false;
    }
    frameNs_ = 0;
}

double Mixer::songTimeMs(uint64_t ticksNs) const {
    if (paused_) return pausedTimeMs_;
    if (ticksNs == 0) ticksNs = SDL_GetTicksNS();
    double elapsed = (double(ticksNs) - double(anchorNs_.load())) / 1e9;
    double frames = double(anchorFrames_.load()) + std::max(0.0, elapsed) * rate_ * double(speed_.load());
    return (frames - double(leadIn_.load())) * 1000.0 / rate_;
}

double Mixer::frameTimeMs() {
    updatePitch();
    uint64_t now = SDL_GetTicksNS();
    double raw = songTimeMs(now);
    if (paused_ || !frameNs_) {
        frameMs_ = raw, frameNs_ = now;
        return raw;
    }
    double predicted = frameMs_ + double(now - frameNs_) / 1e6 * double(speed_.load());
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
    // Development aid (gd01): GD04_AUDIO_DUMP=<file> writes the mixed output (f32 stereo, the song's rate).
    static std::FILE* dump = SDL_getenv("GD04_AUDIO_DUMP") ? std::fopen(SDL_getenv("GD04_AUDIO_DUMP"), "wb") : nullptr;
    if (dump && self->source_) std::fwrite(self->out_.data(), sizeof(float), self->out_.size(), dump);
    SDL_PutAudioStreamData(stream, self->out_.data(), frames * int(2 * sizeof(float)));
    // The song clock counts only the frames the song played (it stops while paused).
    // A pitched stream plays slower or faster than the clock's frames: the
    // clock keeps its last anchor and runs on with the wall clock.
    if (self->source_ && !self->paused_ && !self->pitched_.load()) {
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
    const float pitch0 = guitarPitch_[0].load(), pitch1 = guitarPitch_[1].load();
    // Practice's slowed song: player 1's guitar through SlowShift, read W / 2 frames ahead.
    const float slow = slowPitch_.load();
    const int64_t ahead = slow != 1.0f ? SlowShift::kW / 2 : 0;
    const int64_t end = written_.load(std::memory_order_acquire);
    float bussTarget[16];
    const size_t nt = std::min<size_t>(tracks_.size(), 16);
    for (size_t c = 0; c < nt; ++c) bussTarget[c] = soundBusses().gainAt(tracks_[c].buss);
    for (int i = 0; i < n; ++i) {
        size_t at = size_t((r + i) & (kRing - 1));
        // ~8 ms ramp, no click (gd01) [I].
        guitarGain_ += (target - guitarGain_) * 0.003f;
        guitar2Gain_ += (target2 - guitar2Gain_) * 0.003f;
        float l = 0, rr = 0;
        float g[2][2] = {};  // each player's guitar, ahead of the buss's effects
        for (size_t c = 0; c < tracks_.size(); ++c) {
            const Track& t = tracks_[c];
            float bg = 1;
            if (c < nt) bussGain_[c] += (bussTarget[c] - bussGain_[c]) * 0.003f, bg = bussGain_[c];
            float s = ring_[c][at];
            if (ahead && t.guitar) s = r + i + ahead < end ? ring_[c][size_t((r + i + ahead) & (kRing - 1))] : 0.0f;
            float v = s * t.volume * bg * (t.guitar ? guitarGain_ : t.guitar2 ? guitar2Gain_ : 1.0f);
            // gd01's linear pan law: -1 = left only, 0 = both at full, 1 = right only [I].
            // The players' guitar tracks go through the guitar buss's effects.
            float& L = t.guitar ? g[0][0] : t.guitar2 ? g[1][0] : l;
            float& R = t.guitar ? g[0][1] : t.guitar2 ? g[1][1] : rr;
            L += v * std::clamp(1.0f - t.pan, 0.0f, 1.0f);
            R += v * std::clamp(1.0f + t.pan, 0.0f, 1.0f);
        }
        // Player1PracticeEffects: PitchShiftSlow1, then the whammy's [C data].
        if (ahead) slowShift_.process(g[0][0], g[0][1], slow);
        // Player1Effects / Player2Effects: the pitch shift comes first [C data].
        pitch_[0].process(g[0][0], g[0][1], pitch0);
        pitch_[1].process(g[1][0], g[1][1], pitch1);
        float gl = g[0][0] + g[1][0], gr = g[0][1] + g[1][1];
        guitarFx_.process(gl, gr);
        l += gl, rr += gr;
        out_[size_t(silent + i) * 2] = std::clamp(l, -1.0f, 1.0f);
        out_[size_t(silent + i) * 2 + 1] = std::clamp(rr, -1.0f, 1.0f);
    }
    guitarLag_[0].store(pitch_[0].lag()), guitarLag_[1].store(pitch_[1].lag());
    read_.store(r + n, std::memory_order_release);
    if (silenced_.load()) std::fill(out_.begin(), out_.end(), 0.0f);
}

// Two taps W / 2 apart whose delay falls by (pitch - 1) a frame, wrapping
// from 0 to W; each faded by sin^2(pi * delay / W), so the two sum to 1 and
// a tap is silent as it wraps.
void Mixer::SlowShift::process(float& l, float& r, float pitch) {
    ring[0][write & (kRing - 1)] = l, ring[1][write & (kRing - 1)] = r;
    d -= pitch - 1.0f;
    while (d < 0) d += kW;
    while (d >= kW) d -= kW;
    float out[2] = {0, 0};
    for (float tap : {d, d + kW * 0.5f >= kW ? d - kW * 0.5f : d + kW * 0.5f}) {
        const float g = std::sin(3.14159265f * tap / kW);
        const float pos = float(write) - tap;
        const float fl = std::floor(pos);
        const uint32_t i0 = uint32_t(int64_t(fl)) & (kRing - 1), i1 = (i0 + 1) & (kRing - 1);
        const float f = pos - fl;
        for (int c = 0; c < 2; ++c) out[c] += g * g * (ring[size_t(c)][i0] * (1 - f) + ring[size_t(c)][i1] * f);
    }
    l = out[0], r = out[1];
    ++write;
}
