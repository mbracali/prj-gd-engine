// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/sfx_player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/audio_device.h"
#include "audio/xaudio_reverb.h"

using sfxdsp::kBlock;

// An FxSendReverb's three submix voices (FxSend360 0x826861D0 / 0x826863D0:
// front 2 channels, centre 1, rear 2, 48000 Hz, each with the reverb effect
// and its I3DL2 environment, 0x826848A0) [C]. The reverb turns each into a
// 5.1 voice whose FL FR SL SR it writes (wet only: GH2 never sets the wet
// parameter); the submix voices' output maps (no next_send in the data)
// take input channels 0 (/ 1): front -> master FL FR, centre -> C, rear ->
// RL RR [C: 0x826861D0]. So each voice's reverb FL (FR) is what returns and
// its SL SR are not routed [I: that XAudio applies the map to the effect's
// output channels].
class SfxPlayer::Bus {
public:
    explicit Bus(const I3DL2Params& p) : front(2, sfxdsp::kRate), centre(1, sfxdsp::kRate), rear(2, sfxdsp::kRate) {
        front.setI3DL2(p), centre.setI3DL2(p), rear.setI3DL2(p);
    }
    XAudioReverb front, centre, rear;
    float inFront[2 * kBlock] = {}, inCentre[kBlock] = {}, inRear[2 * kBlock] = {};
};

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

std::shared_ptr<SfxPlayer::Bus> SfxPlayer::reverbBus(const I3DL2Params& params) {
    auto b = std::make_shared<Bus>(params);
    std::lock_guard<std::mutex> g(lock_);
    buses_.erase(std::remove_if(buses_.begin(), buses_.end(), [](const std::weak_ptr<Bus>& w) { return w.expired(); }), buses_.end());
    buses_.push_back(b);
    return b;
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
    if (p.send) {
        v.send = p.send;
        v.sendGains = sfxdsp::sendGains(v.pan, std::min(std::max(p.sendVolume, 0.0001f), 5.0f), true);
    }
    v.env = sfxdsp::Envelope(p.attack, p.release);
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
        if (v.id == id) v.env.release();
}

void SfxPlayer::stopAll() {
    std::lock_guard<std::mutex> g(lock_);
    for (Voice& v : voices_) v.env.release();
}

void SfxPlayer::setPaused(bool paused) {
    std::lock_guard<std::mutex> g(lock_);
    paused_ = paused;
}

void SfxPlayer::setMasterDb(float db) { masterDb_.store(db); }

bool SfxPlayer::playing(VoiceId id) const {
    std::lock_guard<std::mutex> g(lock_);
    for (const Voice& v : voices_)
        if (v.id == id) return true;
    return false;
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
    std::vector<std::shared_ptr<Bus>> buses;
    for (const auto& w : buses_)
        if (auto b = w.lock()) {
            std::memset(b->inFront, 0, sizeof(b->inFront)), std::memset(b->inCentre, 0, sizeof(b->inCentre));
            std::memset(b->inRear, 0, sizeof(b->inRear));
            buses.push_back(std::move(b));
        }
    const float fader = sfxdsp::dbToLin(masterDb_.load());
    for (Voice& v : voices_) {
        if (paused_) continue;
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
        if (v.send) {
            Bus& b = *v.send;
            const auto& sg = v.sendGains;
            for (int i = 0; i < kBlock; ++i) {
                const float x = mono_[i];
                b.inFront[2 * i] += x * sg[0], b.inFront[2 * i + 1] += x * sg[1];
                b.inCentre[i] += x * sg[2];
                b.inRear[2 * i] += x * sg[3], b.inRear[2 * i + 1] += x * sg[4];
            }
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.ended; }), voices_.end());
    // The sends' submix voices, then back into the master.
    for (auto& b : buses) {
        b->front.process(b->inFront, kBlock);
        b->centre.process(b->inCentre, kBlock);
        b->rear.process(b->inRear, kBlock);
        for (int i = 0; i < kBlock; ++i) {
            master_[sfxdsp::kFL][i] += b->inFront[2 * i], master_[sfxdsp::kFR][i] += b->inFront[2 * i + 1];
            master_[sfxdsp::kC][i] += b->inCentre[i];
            master_[sfxdsp::kRL][i] += b->inRear[2 * i], master_[sfxdsp::kRR][i] += b->inRear[2 * i + 1];
        }
    }
    const float* ch[6] = {master_[0], master_[1], master_[2], master_[3], master_[4], master_[5]};
    sfxdsp::downmix51(ch, block_, kBlock);
}
