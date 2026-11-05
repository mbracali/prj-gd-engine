// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/audio_device.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

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

void SDLCALL postmixAndDump(void* user, const SDL_AudioSpec* spec, float* buffer, int bytes) {
    postmix(user, spec, buffer, bytes);
    dumpPostmix(0, "song", spec, buffer, bytes);
}

struct Dump {
    std::FILE* f = nullptr;
    std::atomic<long long> frames{0};
    bool tried = false;
};
Dump g_dumps[2];

void put32(std::FILE* f, uint32_t v) {
    const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    std::fwrite(b, 1, 4, f);
}

}  // namespace

SDL_AudioStream* openStream(const SDL_AudioSpec& spec, SDL_AudioStreamCallback callback, void* user) {
    std::lock_guard<std::mutex> g(g_lock);
    if (!g_device) {
        g_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!g_device) return nullptr;
        SDL_SetAudioPostmixCallback(g_device, dumpEnabled() ? &postmixAndDump : &postmix, nullptr);
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

bool dumpEnabled() {
    static const bool on = SDL_getenv("GD04_AUDIODUMP") != nullptr;
    return on;
}

// A WAVE_FORMAT_IEEE_FLOAT file; its sizes rewritten after each block, so
// it stays readable however the game ends.
void dumpPostmix(int slot, const char* tag, const SDL_AudioSpec* spec, const float* buffer, int bytes) {
    if (slot < 0 || slot > 1 || !spec || spec->channels <= 0 || bytes <= 0) return;
    Dump& d = g_dumps[slot];
    if (!d.tried) {
        d.tried = true;
        const char* prefix = SDL_getenv("GD04_AUDIODUMP");
        if (!prefix) return;
        const std::string path = std::string(prefix) + "." + tag + ".wav";
        d.f = std::fopen(path.c_str(), "wb");
        if (!d.f) return;
        const uint32_t ch = uint32_t(spec->channels), rate = uint32_t(spec->freq);
        std::fwrite("RIFF", 1, 4, d.f), put32(d.f, 36), std::fwrite("WAVEfmt ", 1, 8, d.f), put32(d.f, 16);
        const uint8_t fmt[4] = {3, 0, uint8_t(ch), 0};  // IEEE float
        std::fwrite(fmt, 1, 4, d.f), put32(d.f, rate), put32(d.f, rate * ch * 4);
        const uint8_t align[4] = {uint8_t(ch * 4), 0, 32, 0};
        std::fwrite(align, 1, 4, d.f), std::fwrite("data", 1, 4, d.f), put32(d.f, 0);
        SDL_Log("audiodump: %s at %.0f ms (wall), %d Hz, %d ch: %s", tag, double(SDL_GetTicksNS()) / 1e6, spec->freq, spec->channels, path.c_str());
    }
    if (!d.f) return;
    std::fwrite(buffer, 1, size_t(bytes), d.f);
    d.frames += bytes / int(sizeof(float)) / spec->channels;
    const long end = std::ftell(d.f);
    std::fseek(d.f, 4, SEEK_SET), put32(d.f, uint32_t(end - 8));
    std::fseek(d.f, 40, SEEK_SET), put32(d.f, uint32_t(end - 44));
    std::fseek(d.f, 0, SEEK_END);
    std::fflush(d.f);
}

long long dumpFrames(int slot) { return slot >= 0 && slot <= 1 ? g_dumps[slot].frames.load() : 0; }

}  // namespace audiodev
