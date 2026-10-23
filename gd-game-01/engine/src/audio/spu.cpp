// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/spu.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kVoices = 48;
constexpr double kSpuRate = 48000;

// The IOP's pan table (SYNTH_S.IRX .data 0x37190): T[i] = round(255 sin(pi/2 i/254)).
const std::array<uint8_t, 255>& panTable() {
    static const std::array<uint8_t, 255> t = [] {
        std::array<uint8_t, 255> a{};
        for (int i = 0; i < 255; ++i) a[size_t(i)] = uint8_t(std::lround(255 * std::sin(1.5707963267948966 * i / 254.0)));
        return a;
    }();
    return t;
}

}  // namespace

// The SPU2 envelope (psx-spx ADSR rules, ticked at 48 kHz).
struct Spu::Voice {
    int id = 0;
    bool allocated = false;
    bool paused = false;  // frozen by pause_all_sfx (VOL = PITCH = 0)
    uint32_t freeStamp = 0xFFFFFFFF;  // never used
    KeyOn k;
    double pos = 0;  // sample position
    int level = 0, counter = 0;
    enum class Phase { Attack, Decay, Sustain, Release, Off } phase = Phase::Off;
    double tickAcc = 0;
    float gainL = 0, gainR = 0;

    void setVolumePan(float volume, float pan) {
        int p = std::clamp(int(std::lround(pan * 127)), -127, 127) + 127;
        float v = std::clamp(volume, 0.0f, 1.0f);
        gainL = v * panTable()[size_t(254 - p)] / 255.0f;
        gainR = v * panTable()[size_t(p)] / 255.0f;
    }
    // One envelope step: `shift`, `step` and mode give cycles and increment.
    void envStep(int shift, int step, bool exp, bool dec) {
        int cycles = 1 << std::max(0, shift - 11);
        int st = step * (1 << std::max(0, 11 - shift));
        if (exp && !dec && level > 0x6000) cycles *= 4;
        if (exp && dec) st = (st * level) >> 15;
        if (++counter >= cycles) {
            counter = 0;
            level = std::clamp(level + st, 0, 0x7FFF);
        }
    }
    void tick() {
        const uint16_t a1 = k.adsr1, a2 = k.adsr2;
        switch (phase) {
        case Phase::Attack:
            envStep((a1 >> 10) & 0x1F, 7 - ((a1 >> 8) & 3), a1 >> 15, false);
            if (level >= 0x7FFF) phase = Phase::Decay, counter = 0;
            break;
        case Phase::Decay: {
            int sustain = std::min(((a1 & 0xF) + 1) * 0x800, 0x7FFF);
            if (level <= sustain) {
                phase = Phase::Sustain, counter = 0;
                break;
            }
            envStep((a1 >> 4) & 0xF, -8, true, true);
            break;
        }
        case Phase::Sustain: {
            bool dec = (a2 >> 14) & 1;
            int s = (a2 >> 6) & 3;
            envStep((a2 >> 8) & 0x1F, dec ? -8 + s : 7 - s, a2 >> 15, dec);
            break;
        }
        case Phase::Release:
            envStep(a2 & 0x1F, -8, (a2 >> 5) & 1, true);
            if (level <= 0) phase = Phase::Off;
            break;
        case Phase::Off: break;
        }
    }
};

// A Freeverb-style room per channel, standing in for the SPU2 reverb presets
// (docs/audio_sfx.md §5: the preset tables are not in the binary; tuned by ear).
struct Spu::Reverb {
    struct Comb {
        std::vector<float> buf;
        size_t i = 0;
        float store = 0;
        float run(float x, float fb, float damp) {
            float y = buf[i];
            store = y * (1 - damp) + store * damp;
            buf[i] = x + store * fb;
            if (++i == buf.size()) i = 0;
            return y;
        }
    };
    struct Allpass {
        std::vector<float> buf;
        size_t i = 0;
        float run(float x) {
            float b = buf[i];
            float y = -x + b;
            buf[i] = x + b * 0.5f;
            if (++i == buf.size()) i = 0;
            return y;
        }
    };
    struct Delay {
        std::vector<float> buf;
        size_t i = 0;
    };
    bool echo = false;
    float feedback = 0.84f, damp = 0.2f, delaySec = 0.34f;
    int rate = 0;
    std::array<std::vector<Comb>, 2> combs;
    std::array<std::vector<Allpass>, 2> allpasses;
    std::array<Delay, 2> pre;

    void setup(int r) {
        rate = r;
        float s = r / 44100.0f;
        if (echo) {
            for (Delay& d : pre) d.buf.assign(size_t(std::max(1.0f, delaySec * r)), 0), d.i = 0;
            return;
        }
        static const int kComb[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int kAll[] = {556, 441, 341, 225};
        for (int c = 0; c < 2; ++c) {
            combs[size_t(c)].clear();
            allpasses[size_t(c)].clear();
            for (int n : kComb) combs[size_t(c)].push_back({std::vector<float>(size_t((n + c * 23) * s), 0)});
            for (int n : kAll) allpasses[size_t(c)].push_back({std::vector<float>(size_t((n + c * 23) * s), 0)});
            pre[size_t(c)].buf.assign(size_t(0.035f * r), 0);  // ~35 ms pre-delay
            pre[size_t(c)].i = 0;
        }
    }
    // in: interleaved send; adds wet * reverb into out.
    void process(const float* in, float* out, int frames, int r, float wet) {
        if (r != rate) setup(r);
        for (int f = 0; f < frames; ++f)
            for (int c = 0; c < 2; ++c) {
                float x = in[f * 2 + c];
                Delay& d = pre[size_t(c)];
                float y;
                if (echo) {  // feedback delay
                    y = d.buf[d.i];
                    d.buf[d.i] = x + y * feedback;
                    if (++d.i == d.buf.size()) d.i = 0;
                } else {
                    float delayed = d.buf[d.i];
                    d.buf[d.i] = x;
                    if (++d.i == d.buf.size()) d.i = 0;
                    y = 0;
                    for (Comb& cb : combs[size_t(c)]) y += cb.run(delayed * 0.015f, feedback, damp);
                    for (Allpass& ap : allpasses[size_t(c)]) y = ap.run(y);
                }
                out[f * 2 + c] += y * wet;
            }
    }
};

Spu::Spu() : voices_(kVoices), echo_(std::make_unique<Reverb>()), studio_(std::make_unique<Reverb>()) {
    echo_->echo = true;
    echo_->feedback = 38.0f / 127.0f;
}

Spu::~Spu() = default;

void Spu::setEffects(float echoWet, float echoDelay, float echoFeedback, float studioWet) {
    std::lock_guard<std::mutex> lock(mutex_);
    echoWet_ = echoWet, studioWet_ = studioWet;
    echo_->feedback = echoFeedback;
    echo_->delaySec = 0.68f * echoDelay;  // libsd delay 0..127 over the echo work area [I]
    echo_->rate = 0;
}

void Spu::setMaster(float v) {
    std::lock_guard<std::mutex> lock(mutex_);
    master_ = std::clamp(v, 0.0f, 1.0f);
}

// FUN_000042ec: the free voice in the core's range with the largest free
// stamp (never used first, then least recently freed); no stealing.
int Spu::keyOn(const KeyOn& k) {
    if (!k.sample || k.sample->pcm.empty()) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    int lo = k.core < 0 ? 0 : k.core * 24, hi = k.core < 0 ? kVoices : lo + 24;
    Voice* best = nullptr;
    for (int i = lo; i < hi; ++i) {
        Voice& v = voices_[size_t(i)];
        if (!v.allocated && (!best || v.freeStamp > best->freeStamp)) best = &v;
    }
    if (!best) return 0;
    Voice& v = *best;
    v.allocated = true;
    v.paused = false;
    v.id = nextId_++;
    v.k = k;
    v.pos = 0;
    v.level = 0, v.counter = 0, v.tickAcc = 0;
    v.phase = Voice::Phase::Attack;
    v.setVolumePan(k.volume, k.pan);
    return v.id;
}

void Spu::keyOff(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_)
        if (v.allocated && v.id == id && v.phase != Voice::Phase::Off) v.phase = Voice::Phase::Release, v.counter = 0;
}

void Spu::keyOffAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_)
        if (v.allocated && v.phase != Voice::Phase::Off) v.phase = Voice::Phase::Release, v.counter = 0;
}

void Spu::setVolumePan(int id, float volume, float pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_)
        if (v.allocated && v.id == id) v.k.volume = volume, v.k.pan = pan, v.setVolumePan(volume, pan);
}

void Spu::setSpeed(int id, float speed) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_)
        if (v.allocated && v.id == id) v.k.speed = speed;
}

bool Spu::playing(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Voice& v : voices_)
        if (v.allocated && v.id == id) return true;
    return false;
}

// pause_all_sfx / GamePanel SetPaused (IOP FUN_000041dc): freezes the voices
// playing now; sounds started while paused (the pause menu's) play normally.
void Spu::setPaused(bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_)
        if (v.allocated) v.paused = paused;
}

void Spu::mix(float* out, int frames, int rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    send0_.assign(size_t(frames) * 2, 0.0f);
    send1_.assign(size_t(frames) * 2, 0.0f);
    const double ticksPerFrame = kSpuRate / rate;
    for (Voice& v : voices_) {
        if (!v.allocated) continue;
        const AdpcmSample& s = *v.k.sample;
        // FUN_000051c0: pitch = ((rate << 12) / 48000) * speed1000 / 1000, 4.12 per 48 kHz tick.
        int pitch = int((int64_t(v.k.rate) << 12) / 48000 * int(v.k.speed * 1000) / 1000);
        pitch = std::clamp(pitch, 0, 0x3FFF);
        const double step = pitch / 4096.0 * ticksPerFrame;
        float* send = v.k.core == 0 ? send0_.data() : v.k.core == 1 ? send1_.data() : nullptr;
        for (int f = 0; f < frames; ++f) {
            if (v.paused) break;
            v.tickAcc += ticksPerFrame;
            while (v.tickAcc >= 1) {
                v.tick();
                v.tickAcc -= 1;
            }
            if (v.phase == Voice::Phase::Off) break;
            if (v.pos >= double(s.pcm.size())) {  // the END block
                if (s.loops && s.loopStart >= 0) v.pos = s.loopStart + (v.pos - double(s.pcm.size()));
                else {
                    v.phase = Voice::Phase::Off;
                    break;
                }
            }
            size_t i0 = size_t(v.pos);
            size_t i1 = i0 + 1 < s.pcm.size() ? i0 + 1 : i0;
            float fr = float(v.pos - double(i0));
            float x = (s.pcm[i0] * (1 - fr) + s.pcm[i1] * fr) * (1.0f / 32768.0f) * (v.level / 32767.0f);
            float l = x * v.gainL, r = x * v.gainR;
            out[f * 2] += l * master_;
            out[f * 2 + 1] += r * master_;
            if (send) send[f * 2] += l, send[f * 2 + 1] += r;
            v.pos += step;
        }
        if (v.phase == Voice::Phase::Off) {  // ENDX and ENVX 0: the voice is freed
            v.allocated = false;
            v.freeStamp = freeCounter_--;
        }
    }
    echo_->process(send0_.data(), out, frames, rate, echoWet_ * master_);
    studio_->process(send1_.data(), out, frames, rate, studioWet_ * master_);
}
