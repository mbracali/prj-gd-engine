// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/audio_device.h"

#include <atomic>
#include <mutex>

#include "audio/sfx_dsp.h"

namespace audiodev {

namespace {

std::mutex g_lock;
SDL_AudioDeviceID g_device = 0;
int g_streams = 0;
std::atomic<bool> g_limiterOn{true};
sfxdsp::Limiter g_limiter;  // the device thread only
int g_limiterRate = 0;

void SDLCALL postmix(void*, const SDL_AudioSpec* spec, float* buffer, int bytes) {
    if (!g_limiterOn.load() || !spec || spec->channels <= 0) return;
    if (spec->freq != g_limiterRate) {
        g_limiterRate = spec->freq;
        g_limiter.configure(1, 50, -2, spec->freq);  // default.dta synth limiter [C data]
    }
    g_limiter.process(buffer, bytes / int(sizeof(float)) / spec->channels, spec->channels);
}

}  // namespace

SDL_AudioStream* openStream(const SDL_AudioSpec& spec, SDL_AudioStreamCallback callback, void* user) {
    std::lock_guard<std::mutex> g(g_lock);
    if (!g_device) {
        g_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!g_device) return nullptr;
        SDL_SetAudioPostmixCallback(g_device, &postmix, nullptr);
    }
    SDL_AudioStream* s = SDL_CreateAudioStream(&spec, nullptr);
    if (s && (!SDL_SetAudioStreamGetCallback(s, callback, user) || !SDL_BindAudioStream(g_device, s))) {
        SDL_DestroyAudioStream(s);
        s = nullptr;
    }
    if (!s) {
        if (g_streams == 0) SDL_CloseAudioDevice(g_device), g_device = 0;
        return nullptr;
    }
    ++g_streams;
    return s;
}

void closeStream(SDL_AudioStream* stream) {
    if (!stream) return;
    SDL_DestroyAudioStream(stream);
    std::lock_guard<std::mutex> g(g_lock);
    if (--g_streams == 0 && g_device) {
        SDL_CloseAudioDevice(g_device);
        g_device = 0;
    }
}

void toggleLimiter() { g_limiterOn.store(!g_limiterOn.load()); }

bool limiterEnabled() { return g_limiterOn.load(); }

}  // namespace audiodev
