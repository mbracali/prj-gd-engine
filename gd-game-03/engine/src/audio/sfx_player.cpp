// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/sfx_player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/audio_device.h"

using sfxdsp::kBlock;

SfxPlayer::SfxPlayer() = default;

SfxPlayer::~SfxPlayer() { close(); }

bool SfxPlayer::open() {
    if (stream_) return true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, sfxdsp::kRate};
    stream_ = audiodev::openStream(spec, &SfxPlayer::callback, this);
    if (!stream_) {
        SDL_Log("sound effects: no audio device: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void SfxPlayer::close() {
    if (stream_) audiodev::closeStream(stream_);
    stream_ = nullptr;
    std::lock_guard<std::mutex> g(lock_);
    voices_.clear();
}

void SfxPlayer::setFx(const spu2::FxConfig& fx) {
    std::lock_guard<std::mutex> g(lock_);
    fx_ = fx;
    for (size_t c = 0; c < cores_.size(); ++c) cores_[c].configure(fx.core[c]);
}

SfxPlayer::VoiceId SfxPlayer::play(std::shared_ptr<const SfxPcm> pcm, const Params& p) {
    if (!pcm || pcm->samples.empty()) return 0;
    Voice v;
    v.pcm = std::move(pcm);
    // The voice's rate (0x82689768) and speed (0x8268A100; every sample on
    // the disc is XMA, so at most 2) [C].
    const float speed = std::min(std::max(p.speed, 0.01f), 2.0f);
    const int rate = int(float(v.pcm->rate) * 0.99750006f);
    v.step = double(rate) * double(speed) / double(sfxdsp::kRate);
    // XMA loops on 128-frame multiples (0x82689C68) [C].
    if (v.pcm->looping) {
        const uint32_t s = v.pcm->loopStart / 128 * 128, e = v.pcm->loopEnd / 128 * 128;
        if (e > s && e <= v.pcm->samples.size()) v.loop = true, v.loopStart = s, v.loopEnd = e;
    }
    v.gain = p.gain;
    v.pan = sfxdsp::panGains(sfxdsp::wrapPan(p.pan));
    if (p.fxCore == 0 || p.fxCore == 1) {
        v.fxCore = p.fxCore;
        v.fxL = v.pan[sfxdsp::kFL] + sfxdsp::kDownmixCentre * v.pan[sfxdsp::kC] + sfxdsp::kDownmixRear * v.pan[sfxdsp::kRL];
        v.fxR = v.pan[sfxdsp::kFR] + sfxdsp::kDownmixCentre * v.pan[sfxdsp::kC] + sfxdsp::kDownmixRear * v.pan[sfxdsp::kRR];
    }
    v.env = sfxdsp::Envelope(p.attack, p.release);
    v.stream = p.stream;
    ++started_;
    std::lock_guard<std::mutex> g(lock_);
    if (voices_.size() >= kMaxVoices) return 0;
    v.id = next_++;
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void SfxPlayer::stop(VoiceId id) {
    std::lock_guard<std::mutex> g(lock_);
    for (Voice& v : voices_)
        if (v.id == id) v.env.release(), v.ended = v.ended || v.paused;  // a paused voice can't fade: it goes
}

void SfxPlayer::stopAll() {
    std::lock_guard<std::mutex> g(lock_);
    for (Voice& v : voices_) v.env.release(), v.ended = v.ended || v.paused;
}

void SfxPlayer::setPaused(bool paused) {
    std::lock_guard<std::mutex> g(lock_);
    for (Voice& v : voices_)
        if (!v.stream) v.paused = paused;
}

void SfxPlayer::setVoicePaused(VoiceId id, bool paused) {
    std::lock_guard<std::mutex> g(lock_);
    for (Voice& v : voices_)
        if (v.id == id) v.paused = paused;
}

void SfxPlayer::setMasterDb(float db) { masterDb_.store(db); }

bool SfxPlayer::playing(VoiceId id) const {
    std::lock_guard<std::mutex> g(lock_);
    for (const Voice& v : voices_)
        if (v.id == id) return true;
    return false;
}

int SfxPlayer::liveVoices() const {
    std::lock_guard<std::mutex> g(lock_);
    return int(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return !v.paused && !v.ended; }));
}

int SfxPlayer::voices() const {
    std::lock_guard<std::mutex> g(lock_);
    return int(voices_.size());
}

void SDLCALL SfxPlayer::callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<SfxPlayer*>(user);
    int frames = additional / int(sizeof(float) * 2);
    // Whole 256-frame passes, like XAudio's.
    while (frames > 0) {
        {
            std::lock_guard<std::mutex> g(self->lock_);
            self->mixBlock();
        }
        SDL_PutAudioStreamData(stream, self->block_, int(sizeof(self->block_)));
        frames -= kBlock;
    }
}

void SfxPlayer::render(float* out, int frames, std::vector<float>* out51) {
    if (out51) out51->assign(size_t(frames) * 6, 0.0f);
    int done = 0;
    while (done < frames) {
        if (blockPos_ >= kBlock) {
            std::lock_guard<std::mutex> g(lock_);
            mixBlock();
            blockPos_ = 0;
        }
        const int n = std::min(kBlock - blockPos_, frames - done);
        std::memcpy(out + 2 * done, block_ + 2 * blockPos_, sizeof(float) * size_t(2 * n));
        if (out51)
            for (int c = 0; c < 6; ++c)
                std::memcpy(out51->data() + size_t(c) * size_t(frames) + size_t(done), master_[c] + blockPos_, sizeof(float) * size_t(n));
        blockPos_ += n, done += n;
    }
}

void SfxPlayer::mixBlock() {
    std::memset(master_, 0, sizeof(master_));
    std::memset(coreIn_, 0, sizeof(coreIn_));
    const float fader = sfxdsp::dbToLin(masterDb_.load());
    for (Voice& v : voices_) {
        if (v.paused) continue;
        // The source: linear interpolation at the voice's step [I: XAudio's SRC].
        const SfxPcm& p = *v.pcm;
        const std::vector<float>& s = p.samples;
        const double size = double(s.size());
        for (int i = 0; i < kBlock; ++i) {
            if (v.loop) {
                while (v.pos >= v.loopEnd) v.pos -= v.loopEnd - v.loopStart;
            } else if (v.pos >= size) {
                v.ended = true;
                std::fill(mono_ + i, mono_ + kBlock, 0.0f);
                break;
            }
            const size_t k = size_t(v.pos);
            const float f = float(v.pos - double(k));
            size_t k1 = k + 1;
            if (v.loop && double(k1) >= v.loopEnd) k1 = size_t(v.loopStart);
            const float a = s[k], b = k1 < s.size() ? s[k1] : 0.0f;
            mono_[i] = a + (b - a) * f;
            v.pos += v.step;
        }
        v.env.process(mono_);
        if (v.env.state() == sfxdsp::Envelope::kDone) v.ended = true;
        // The master: pan x volume (clamped) [C].
        const float vol = std::min(std::max(v.gain * fader, 0.0f), 1.0f);
        for (int c = 0; c < 6; ++c) {
            const float g = v.pan[size_t(c)] * vol;
            if (g == 0.0f) continue;
            for (int i = 0; i < kBlock; ++i) master_[c][i] += mono_[i] * g;
        }
        // The core's effect input (VMIXEL / VMIXER): the voice after its volume and pan.
        if (v.fxCore >= 0) {
            float* in = coreIn_[v.fxCore];
            const float gl = v.fxL * vol, gr = v.fxR * vol;
            for (int i = 0; i < kBlock; ++i) in[2 * i] += mono_[i] * gl, in[2 * i + 1] += mono_[i] * gr;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.ended; }), voices_.end());
    // The cores' effects (core 0 first: its output reaches core 1), back
    // into the master's front pair.
    for (size_t c = 0; c < cores_.size(); ++c) {
        std::memset(coreOut_, 0, sizeof(coreOut_));
        cores_[c].process(coreIn_[c], coreOut_, kBlock);
        if (c == 0 && fx_.chain)
            for (int i = 0; i < 2 * kBlock; ++i) coreIn_[1][i] += coreOut_[i];
        for (int i = 0; i < kBlock; ++i) master_[sfxdsp::kFL][i] += coreOut_[2 * i], master_[sfxdsp::kFR][i] += coreOut_[2 * i + 1];
    }
    const float* ch[6] = {master_[0], master_[1], master_[2], master_[3], master_[4], master_[5]};
    sfxdsp::downmix51(ch, block_, kBlock);
}
