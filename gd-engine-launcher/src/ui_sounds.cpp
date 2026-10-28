// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui_sounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

const char* const kRoles[] = {"move", "select", "play", "back", "error"};
constexpr float kGain = 0.6f;  // the menu sounds under the games' own levels

// ---- The built-in sounds: plucked strings (Karplus-Strong), made at start ----

constexpr int kRate = 48000;

struct Synth {
    std::vector<float> l, r;
    uint32_t seed = 0x6d2b79f5u;  // fixed: the same sounds every time
    explicit Synth(double seconds) : l(size_t(seconds * kRate)), r(l.size()) {}

    float noise() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 24) * 2.f - 1.f;
    }
    // One plucked string: `freq` Hz from `at` s, `decay` per period (lower is
    // shorter, a palm mute), `tone` 0..1 (lower is darker), `pan` -1..1.
    void pluck(double freq, double at, float amp, float decay, float tone, float pan) {
        const size_t n = std::max<size_t>(2, size_t(kRate / freq + 0.5));
        std::vector<float> ring(n);
        float lp = 0;
        for (float& v : ring) lp += tone * (noise() - lp), v = lp;  // a softer pick than white noise
        const float gl = amp * std::sqrt(0.5f * (1 - pan)), gr = amp * std::sqrt(0.5f * (1 + pan));
        size_t i = 0;
        for (size_t k = size_t(at * kRate); k < l.size(); ++k) {
            const size_t j = (i + 1) % n;
            const float v = ring[i];
            ring[i] = decay * 0.5f * (ring[i] + ring[j]);
            i = j;
            l[k] += v * gl, r[k] += v * gr;
        }
    }
    // Overdrive (a soft clip), as an amp would.
    void drive(float gain) {
        const float norm = 1.f / std::tanh(gain);
        for (size_t k = 0; k < l.size(); ++k) l[k] = std::tanh(l[k] * gain) * norm, r[k] = std::tanh(r[k] * gain) * norm;
    }
    // A one-pole low-pass at `hz`.
    void lowpass(double hz) {
        const float a = float(1 - std::exp(-2 * 3.14159265358979 * hz / kRate));
        float yl = 0, yr = 0;
        for (size_t k = 0; k < l.size(); ++k) yl += a * (l[k] - yl), yr += a * (r[k] - yr), l[k] = yl, r[k] = yr;
    }
    // A short fade-in (no click) and a fade-out over the last `out` seconds;
    // then the loudest sample at `peak`.
    std::vector<float> finish(double out, float peak) {
        const size_t fin = kRate / 1000 * 2, fout = std::min(l.size(), size_t(out * kRate));
        float top = 1e-6f;
        for (size_t k = 0; k < l.size(); ++k) {
            float g = k < fin ? float(k) / float(fin) : 1.f;
            if (k + fout >= l.size()) g *= float(l.size() - k) / float(fout);
            l[k] *= g, r[k] *= g;
            top = std::max(top, std::max(std::fabs(l[k]), std::fabs(r[k])));
        }
        std::vector<float> out2(l.size() * 2);
        for (size_t k = 0; k < l.size(); ++k) out2[2 * k] = l[k] * peak / top, out2[2 * k + 1] = r[k] * peak / top;
        return out2;
    }
};

// Note frequencies (A4 = 440 Hz).
constexpr double kE2 = 82.41, kF2 = 87.31, kB2 = 123.47, kE3 = 164.81, kE4 = 329.63, kGs4 = 415.30, kB4 = 493.88, kE5 = 659.26;

}  // namespace

void UiSounds::addBuiltIn() {
    clear();
    auto put = [this](UiSound s, const std::vector<float>& pcm) { clips_[size_t(s)].push_back({pcm, kRate}); };
    {  // move: one soft, short pluck high on the neck (two, picked at random)
        for (double f : {kB4, kGs4}) {
            Synth s(0.09);
            s.pluck(f, 0, 1, 0.985f, 0.35f, 0);
            s.lowpass(3500);
            put(UiSound::Move, s.finish(0.06, 0.25f));
        }
    }
    {  // select: E then B, rising, clean
        Synth s(0.32);
        s.pluck(kE4, 0, 1, 0.996f, 0.5f, -0.2f);
        s.pluck(kB4, 0.055, 0.9f, 0.996f, 0.5f, 0.2f);
        s.lowpass(4500);
        put(UiSound::Select, s.finish(0.18, 0.6f));
    }
    {  // play: an E5 power chord (E2 B2 E3), strummed down and overdriven, with a high E ringing
        Synth s(1.1);
        s.pluck(kE2, 0.000, 1.0f, 0.9985f, 0.7f, -0.3f);
        s.pluck(kB2, 0.014, 0.9f, 0.9985f, 0.7f, 0.0f);
        s.pluck(kE3, 0.028, 0.8f, 0.9985f, 0.7f, 0.3f);
        s.drive(5);
        s.pluck(kE5, 0.040, 0.12f, 0.998f, 0.5f, 0.1f);
        s.lowpass(3200);
        put(UiSound::Play, s.finish(0.7, 0.32f));
    }
    {  // back: B then E, falling, softer
        Synth s(0.26);
        s.pluck(kB4, 0, 0.9f, 0.994f, 0.45f, 0.2f);
        s.pluck(kE4, 0.05, 1, 0.994f, 0.45f, -0.2f);
        s.lowpass(3800);
        put(UiSound::Back, s.finish(0.15, 0.5f));
    }
    {  // error: a palm-muted low E against F (a semitone), a dull thud
        Synth s(0.24);
        s.pluck(kE2, 0, 1, 0.97f, 0.6f, -0.15f);
        s.pluck(kF2, 0.012, 0.9f, 0.97f, 0.6f, 0.15f);
        s.drive(3);
        s.lowpass(1400);
        put(UiSound::Error, s.finish(0.12, 0.43f));
    }
    builtIn_ = true;
}

void UiSounds::add(const std::string& role, const float* stereo, int frames, int rate) {
    if (!stereo || frames <= 0 || rate <= 0) return;
    for (size_t i = 0; i < clips_.size(); ++i)
        if (role == kRoles[i]) clips_[i].push_back({std::vector<float>(stereo, stereo + size_t(frames) * 2), rate});
}

void UiSounds::clear() {
    close();
    builtIn_ = false;
    for (auto& c : clips_) c.clear();
}

int UiSounds::count() const {
    int n = 0;
    for (const auto& c : clips_) n += int(c.size());
    return n;
}

bool UiSounds::open() {
    if (device_ || count() == 0) return device_ != 0;
    if (!audioInit_) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_Log("gdul: menu sounds: no audio (%s)", SDL_GetError());
            return false;
        }
        audioInit_ = true;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!device_) {
        SDL_Log("gdul: menu sounds: cannot open the audio device (%s)", SDL_GetError());
        close();
        return false;
    }
    // A few streams on the one device (SDL mixes them): quick moves overlap
    // as in the game's menus instead of queueing up.
    for (SDL_AudioStream*& s : streams_) {
        s = SDL_CreateAudioStream(&spec, nullptr);
        if (s) {
            SDL_SetAudioStreamGain(s, kGain);
            SDL_BindAudioStream(device_, s);
        }
    }
    return true;
}

void UiSounds::close() {
    for (SDL_AudioStream*& s : streams_) {
        if (s) SDL_DestroyAudioStream(s);
        s = nullptr;
    }
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = 0;
    if (audioInit_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audioInit_ = false;
}

void UiSounds::play(UiSound s) {
    const auto& set = clips_[size_t(s)];
    if (set.empty() || (!device_ && !open())) return;
    const Clip& c = set[std::uniform_int_distribution<size_t>(0, set.size() - 1)(rng_)];
    // The stream that is free (else the one used longest ago).
    SDL_AudioStream* stream = nullptr;
    for (size_t k = 0; k < streams_.size() && !stream; ++k) {
        SDL_AudioStream* t = streams_[(next_ + k) % streams_.size()];
        if (t && SDL_GetAudioStreamQueued(t) == 0) stream = t, next_ = (next_ + k + 1) % streams_.size();
    }
    if (!stream) {
        stream = streams_[next_];
        next_ = (next_ + 1) % streams_.size();
        if (stream) SDL_ClearAudioStream(stream);
    }
    if (!stream) return;
    const SDL_AudioSpec src{SDL_AUDIO_F32, 2, c.rate};
    SDL_SetAudioStreamFormat(stream, &src, nullptr);
    SDL_PutAudioStreamData(stream, c.pcm.data(), int(c.pcm.size() * sizeof(float)));
    if (logging_) SDL_Log("gdul: menu sound %s (%d ms)", kRoles[size_t(s)], int(c.pcm.size() / 2 * 1000 / size_t(c.rate)));
}
