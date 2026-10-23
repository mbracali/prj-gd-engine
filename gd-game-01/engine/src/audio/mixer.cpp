// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/mixer.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

Mixer::~Mixer() { close(); }

void Mixer::close() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    vgs_.reset();
}

bool Mixer::open(std::string& error) {
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate_};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Mixer::callback, this);
    if (!stream_) {
        error = SDL_GetError();
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void Mixer::play(std::unique_ptr<VgsStream> stream, std::vector<Track> tracks, double leadInMs) {
    // Lock order: the stream, then mutex_ (the audio thread holds the stream
    // lock when it calls callback(), which takes mutex_).
    SDL_LockAudioStream(stream_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SDL_ClearAudioStream(stream_);
        rate_ = stream->sampleRate();
        for (Stream& s : streams_)
            if (s.vgs) s.ratio = s.vgs->sampleRate() / double(rate_);
        SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate_};
        SDL_SetAudioStreamFormat(stream_, &spec, nullptr);
        tracks.resize(stream->channels());
        tracks_ = std::move(tracks);
        scratch_.assign(stream->channels(), {});
        vgs_ = std::move(stream);
        leadInFrames_ = static_cast<int64_t>(leadInMs * rate_ / 1000.0);
        leadIn_ = leadInFrames_;
        framesPushed_ = 0;
        songFrames_ = 0;
        for (GuitarCopy& g : copies_) {
            g.gain = 1, g.target = 1, g.volume = 1, g.slipSpeed = 1;
            g.lag = 0, g.lagMs = 0, g.resyncSeen = g.resync.load();
        }
        slipWrite_ = 0;
        slipHist_.assign(tracks_.size(), std::vector<float>(kSlipRing, 0.0f));
        anchorFrames_ = 0;
        anchorNs_ = SDL_GetTicksNS();
        paused_ = false;
    }
    SDL_UnlockAudioStream(stream_);
    SDL_ResumeAudioStreamDevice(stream_);
}

void Mixer::stop() {
    SDL_LockAudioStream(stream_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        vgs_.reset();
        SDL_ClearAudioStream(stream_);
    }
    SDL_UnlockAudioStream(stream_);
}

void Mixer::setPaused(bool paused) {
    if (paused == paused_) return;
    if (paused) {
        pausedTimeMs_ = songTimeMs();
        paused_ = true;  // the song stops; the SPU keeps playing (the pause / end menus' sounds)
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
    double frames = anchorFrames_.load() + std::max(0.0, elapsed) * rate_;
    return (frames - leadIn_.load()) * 1000.0 / rate_;
}

double Mixer::frameTimeMs() {
    uint64_t now = SDL_GetTicksNS();
    double raw = songTimeMs(now);
    if (paused_ || !frameNs_) {
        frameMs_ = raw, frameNs_ = now;
        return raw;
    }
    double predicted = frameMs_ + double(now - frameNs_) / 1e6;
    double err = raw - predicted;
    // Big jumps (seek, resume, a stalled device) snap; small ones ease in at
    // 5 % a frame, never moving the clock backwards.
    frameMs_ = std::fabs(err) > 40 ? raw : std::max(frameMs_, predicted + err * 0.05);
    frameNs_ = now;
    return frameMs_;
}

void SDLCALL Mixer::callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<Mixer*>(user);
    int frames = additional / int(2 * sizeof(float));
    if (frames <= 0) return;
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->fill(frames);
    for (float& x : self->out_) x = std::clamp(x, -1.0f, 1.0f);
    // Development aid: GD01_AUDIO_DUMP=<file> appends the mixed output (f32 stereo).
    static FILE* dump = SDL_getenv("GD01_AUDIO_DUMP") ? std::fopen(SDL_getenv("GD01_AUDIO_DUMP"), "wb") : nullptr;
    if (dump) std::fwrite(self->out_.data(), sizeof(float), self->out_.size(), dump);
    SDL_PutAudioStreamData(stream, self->out_.data(), frames * int(2 * sizeof(float)));
    self->framesPushed_ += frames;
    // The song clock counts only the frames the song played (it stops while paused).
    if (!self->paused_) {
        self->songFrames_ += frames;
        int64_t queued = SDL_GetAudioStreamQueued(stream) / int(2 * sizeof(float));
        self->anchorFrames_ = self->songFrames_ - queued;
        self->anchorNs_ = SDL_GetTicksNS();
    }
}

void Mixer::playStream(int slot, std::unique_ptr<VgsStream> stream, std::vector<Track> tracks, double startMs,
                       double endMs, bool loop, double fadeMs, float volume) {
    if (!stream_) return;
    SDL_LockAudioStream(stream_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Stream& s = streams_[size_t(slot)];
        double rate = stream->sampleRate();
        tracks.resize(size_t(stream->channels()));
        s.tracks = std::move(tracks);
        s.start = int64_t(startMs * rate / 1000.0);
        s.end = endMs > 0 ? int64_t(endMs * rate / 1000.0) : int64_t(stream->lengthFrames());
        s.loop = loop;
        stream->seek(uint64_t(s.start));
        s.pos = s.start;
        s.ratio = rate / double(rate_);
        s.frac = 1;
        s.src.assign(size_t(stream->channels()), std::vector<float>(1, 0.0f));
        s.vgs = std::move(stream);
        s.volume = volume;
        s.paused = false;
        s.gain = fadeMs > 0 ? 0.0f : 1.0f;
        s.target = 1;
        s.step = fadeMs > 0 ? float(1000.0 / (fadeMs * rate_)) : 1.0f;
        s.playing = true;
    }
    SDL_UnlockAudioStream(stream_);
    SDL_ResumeAudioStreamDevice(stream_);
}

void Mixer::stopStream(int slot, double fadeMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    Stream& s = streams_[size_t(slot)];
    if (!s.vgs) return;
    s.target = 0;
    s.step = fadeMs > 0 ? float(1000.0 / (fadeMs * rate_)) : 1.0f;
}

void Mixer::setStreamVolume(int slot, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    streams_[size_t(slot)].volume = volume;
}

void Mixer::setStreamPaused(int slot, bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    streams_[size_t(slot)].paused = paused;
}

// A stream voice mixed into out_, decoded at its own rate and linearly
// resampled to the device rate (crowd loops and VO are 22050 Hz, the device
// runs at the song's 44100).
void Mixer::fillStream(Stream& st, int frames) {
    if (!st.vgs || st.paused) return;
    // Source frames needed: positions frac + i * ratio, each between src[k] and
    // src[k + 1], and the frame at frac + frames * ratio carried to the next call.
    const double end = st.frac + frames * st.ratio;
    int need = std::max(int(std::floor(st.frac + (frames - 1) * st.ratio)) + 1, int(std::floor(end)));
    std::vector<float*> ptrs;
    for (auto& sc : st.src) {
        sc.resize(size_t(need) + 1);
        ptrs.push_back(sc.data() + 1);
    }
    int got = 0;
    while (got < need) {
        int64_t left = st.end - st.pos;
        if (left <= 0) {
            if (!st.loop) break;
            st.vgs->seek(uint64_t(st.start));
            st.pos = st.start;
            continue;
        }
        int n = int(std::min<int64_t>(left, need - got));
        std::vector<float*> at;
        for (auto* p : ptrs) at.push_back(p + got);
        st.vgs->decode(at.data(), n);
        st.pos += n;
        got += n;
    }
    for (auto& sc : st.src) std::fill(sc.begin() + 1 + got, sc.end(), 0.0f);
    // FadePanel fade_synth (SPU MVOL) fades the crowd voices with the sfx.
    const float fade = &st == &streams_[size_t(kCrowd)] ? spu_.master() : 1.0f;
    // Output frames that still have source behind them.
    int done = frames;
    if (got < need) done = std::clamp(int((double(got) - st.frac) / st.ratio) + 1, 0, frames);
    for (int i = 0; i < done; ++i) {
        if (st.gain < st.target) st.gain = std::min(st.target, st.gain + st.step);
        else if (st.gain > st.target) st.gain = std::max(st.target, st.gain - st.step);
        double p = st.frac + i * st.ratio;
        size_t k = size_t(p);
        float t = float(p - double(k));
        float l = 0, r = 0;
        for (size_t c = 0; c < st.tracks.size(); ++c) {
            const std::vector<float>& x = st.src[c];
            float sample = x[k] + (x[std::min(k + 1, x.size() - 1)] - x[k]) * t;
            float v = sample * st.tracks[c].volume * st.gain * st.volume * fade;
            l += v * std::clamp(1.0f - st.tracks[c].pan, 0.0f, 1.0f);
            r += v * std::clamp(1.0f + st.tracks[c].pan, 0.0f, 1.0f);
        }
        out_[size_t(i) * 2] += l;
        out_[size_t(i) * 2 + 1] += r;
    }
    // The next call starts from src[floor(end)].
    size_t used = std::min(size_t(end), st.src.empty() ? 0 : st.src[0].size() - 1);
    for (auto& sc : st.src) sc[0] = sc[used];
    st.frac = end - double(size_t(end));
    if (done < frames || (st.target == 0 && st.gain == 0)) {
        st.vgs.reset();
        st.playing = false;
    }
}

void Mixer::fill(int frames) {
    out_.assign(size_t(frames) * 2, 0.0f);
    for (Stream& st : streams_) fillStream(st, frames);
    spu_.mix(out_.data(), frames, rate_);
    if (!vgs_ || paused_) return;
    int silent = static_cast<int>(std::clamp<int64_t>(leadInFrames_ - songFrames_, 0, frames));
    int audible = frames - silent;
    if (audible <= 0) return;

    std::vector<float*> ptrs;
    const float speed = std::clamp(songSpeed_.load(), 0.0f, 1.0f);
    if (speed < 1) {
        // beatmatch set_music_speed: the song plays slower (and lower), resampled.
        double need = songFrac_ + audible * double(speed);
        int n = int(need) + 2;
        for (auto& s : scratch_) {
            s.resize(size_t(n));
            ptrs.push_back(s.data());
        }
        vgs_->decode(ptrs.data(), n - 2);
        for (auto& s : scratch_) {
            s[size_t(n - 2)] = s[size_t(n - 1)] = n >= 3 ? s[size_t(n - 3)] : 0.0f;
            std::vector<float> res(static_cast<size_t>(audible));
            for (int i = 0; i < audible; ++i) {
                double x = songFrac_ + i * double(speed);
                size_t i0 = size_t(x);
                float f = float(x - double(i0));
                res[size_t(i)] = s[i0] * (1 - f) + s[std::min(i0 + 1, s.size() - 1)] * f;
            }
            s = std::move(res);
        }
        songFrac_ = need - std::floor(need);
    } else {
        for (auto& s : scratch_) {
            s.resize(audible);
            ptrs.push_back(s.data());
        }
        vgs_->decode(ptrs.data(), audible);
    }

    // The guitar copies (one per player): each its own slip voice. A slip
    // voice reads its guitar samples `lag` frames behind, the lag growing by
    // 1 - speed a frame up to max_slip (synth.dta, 300 ms).
    const int copies = std::clamp(copyCount_.load(), 1, int(copies_.size()));
    const float panScale = copies > 1 ? copyPanScale_.load() : 1.0f, atten = copies > 1 ? copyAtten_.load() : 1.0f;
    float target[2], volume[2];
    double slip[2];
    for (int k = 0; k < copies; ++k) {
        GuitarCopy& g = copies_[size_t(k)];
        target[k] = g.target.load(), volume[k] = g.volume.load() * atten;
        slip[k] = 1.0 - std::clamp(double(g.slipSpeed.load()), 0.5, 1.0);
        if (int r = g.resync.load(); r != g.resyncSeen) g.resyncSeen = r, g.lag = 0;
    }
    const double maxLag = 0.3 * rate_;
    if (slipHist_.size() != tracks_.size()) slipHist_.assign(tracks_.size(), std::vector<float>(kSlipRing, 0.0f));
    for (int i = 0; i < audible; ++i) {
        float l = 0, r = 0;
        for (size_t c = 0; c < tracks_.size(); ++c) {
            const Track& t = tracks_[c];
            float in = scratch_[c][i];
            if (!t.guitar) {
                float v = in * t.volume;
                l += v * std::clamp(1.0f - t.pan, 0.0f, 1.0f);
                r += v * std::clamp(1.0f + t.pan, 0.0f, 1.0f);
                continue;
            }
            auto& h = slipHist_[c];
            h[slipWrite_ & (kSlipRing - 1)] = in;
            for (int k = 0; k < copies; ++k) {
                const GuitarCopy& g = copies_[size_t(k)];
                float x = in;
                if (g.lag > 1e-6) {
                    const double rp = double(slipWrite_) - g.lag;
                    const uint32_t i0 = uint32_t(std::floor(rp));
                    const float f = float(rp - std::floor(rp));
                    x = h[i0 & (kSlipRing - 1)] * (1 - f) + h[(i0 + 1) & (kSlipRing - 1)] * f;
                }
                // FUN_00267d08: pan * scale + offset (player 1 left, player 2 right).
                const float pan = copies > 1 ? t.pan * panScale + (k == 0 ? panScale - 1 : 1 - panScale) : t.pan;
                float v = x * t.volume * g.gain * volume[k];
                l += v * std::clamp(1.0f - pan, 0.0f, 1.0f);
                r += v * std::clamp(1.0f + pan, 0.0f, 1.0f);
            }
        }
        for (int k = 0; k < copies; ++k) {
            GuitarCopy& g = copies_[size_t(k)];
            g.gain += (target[k] - g.gain) * 0.003f;  // ~8 ms, no click [I]: the PS2 frees the slip voice
            g.lag = std::min(g.lag + slip[k], maxLag);
        }
        ++slipWrite_;
        out_[size_t(silent + i) * 2] = std::clamp(out_[size_t(silent + i) * 2] + l, -1.0f, 1.0f);
        out_[size_t(silent + i) * 2 + 1] = std::clamp(out_[size_t(silent + i) * 2 + 1] + r, -1.0f, 1.0f);
    }
    for (int k = 0; k < copies; ++k) copies_[size_t(k)].lagMs.store(copies_[size_t(k)].lag * 1000.0 / rate_);
}

void Mixer::setGuitarCopies(int count, float panScale, float atten) {
    copyPanScale_.store(panScale);
    copyAtten_.store(atten);
    copyCount_.store(std::clamp(count, 1, int(copies_.size())));
}
