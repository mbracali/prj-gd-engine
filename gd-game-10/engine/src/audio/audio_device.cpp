// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/audio_device.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio/sfx_dsp.h"

#if GDCORE_HAS_AUDIO
#include "gd/core/audio/output.h"
#endif

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

std::mutex g_openLock;             // the opens and closes, one at a time (the core output opens and closes in them)
SDL_AudioDeviceID g_fxDevice = 0;  // local: the sound effects' own logical device (Sfx)
bool g_fxOpen = false;

// Enhanced > Audio backend (setBackend): the backend asked for, the one that
// runs ("" while nothing is open, "local" for SDL's devices), and whether the
// asked one failed (then local for the rest of the run, as rule 2 asks).
std::string g_asked = "local", g_running;
bool g_failed = false;

#if GDCORE_HAS_AUDIO
// A gd-engine-core output in place of SDL's two logical devices: the streams
// are not bound to a device; the output's thread pulls each (which runs its
// get callback, as SDL's device thread would) and sums them as SDL mixes
// bound streams: the song's device (Mixer) through the master limiter (its
// postmix), then the sound effects' device (Sfx) added unlimited, as the two
// SDL devices meet in the system's mixer.
std::unique_ptr<gd::core::audio::Output> g_out;
gd::core::audio::Spec g_outSpec;
std::vector<SDL_AudioStream*> g_songStreams, g_fxStreams;  // under g_lock
std::vector<float> g_scratch, g_fxMix;                     // the output's thread only

void pullCore(float* out, int frames) {
    const int ch = g_outSpec.channels, n = frames * ch, bytes = n * int(sizeof(float));
    std::fill(out, out + n, 0.0f);
    if (int(g_scratch.size()) < n) g_scratch.resize(size_t(n)), g_fxMix.resize(size_t(n));
    std::fill(g_fxMix.begin(), g_fxMix.begin() + n, 0.0f);
    std::lock_guard<std::mutex> g(g_lock);
    auto mix = [&](const std::vector<SDL_AudioStream*>& streams, float* dst) {
        for (SDL_AudioStream* s : streams) {
            const int got = SDL_GetAudioStreamData(s, g_scratch.data(), bytes);
            const int m = std::max(0, got) / int(sizeof(float));
            for (int i = 0; i < m; ++i) dst[i] += g_scratch[size_t(i)];
        }
    };
    const SDL_AudioSpec spec{SDL_AUDIO_F32, ch, g_outSpec.sampleRate};
    mix(g_songStreams, out);
    if (dumpEnabled()) postmixAndDump(nullptr, &spec, out, bytes);
    else postmix(nullptr, &spec, out, bytes);
    mix(g_fxStreams, g_fxMix.data());
    if (dumpEnabled()) dumpPostmix(1, "sfx", &spec, g_fxMix.data(), bytes);
    for (int i = 0; i < n; ++i) out[i] += g_fxMix[size_t(i)];
}

// Opens and starts the output `id` (outside g_lock: a backend's thread may
// pull, which takes it, while its start waits for that thread). nullptr:
// local for this run.
std::unique_ptr<gd::core::audio::Output> openCore(const std::string& id) {
    std::string why;
    std::unique_ptr<gd::core::audio::Output> out = gd::core::audio::create(id, why);
    if (out) {
        gd::core::audio::Spec want;
        want.sampleRate = 48000;  // the 360's mastering rate [C]; each stream converts to the output's
        want.channels = 2;
        // Headless (SDL's dummy driver: the self-tests, the walks): the
        // backend's null device, which consumes in real time like SDL's dummy.
        const char* drv = SDL_GetCurrentAudioDriver();
        want.nullDevice = drv && std::strcmp(drv, "dummy") == 0;
        if (out->open(want, &pullCore, why)) {
            g_outSpec = out->spec();  // before the first pull (start)
            if (out->start(why)) {
                SDL_Log("audio: backend %s: %s, %d Hz, %d channels (%.1f ms buffered)", id.c_str(), out->name().c_str(),
                        g_outSpec.sampleRate, g_outSpec.channels, out->latencyMs());
                return out;
            }
        }
        out.reset();
    }
    SDL_Log("audio: backend %s failed (%s); using local", id.c_str(), why.c_str());
    return nullptr;
}

// The core output for a user (under g_openLock, not g_lock): opened the first
// time when a core backend is asked; true while it runs.
bool ensureCore() {
    std::string open;
    {
        std::lock_guard<std::mutex> g(g_lock);
        if (g_out) return true;
        if (g_asked != "local" && !g_failed) open = g_asked;
    }
    if (open.empty()) return false;
    std::unique_ptr<gd::core::audio::Output> out = openCore(open);
    std::lock_guard<std::mutex> g(g_lock);
    if (!out) return g_failed = true, false;
    g_out = std::move(out);
    g_running = g_asked;
    return true;
}

// After a user left (under g_openLock): the output closes with the last one.
void releaseCore() {
    std::unique_ptr<gd::core::audio::Output> last;
    {
        std::lock_guard<std::mutex> g(g_lock);
        if (g_out && g_streams == 0 && !g_fxOpen) last = std::move(g_out), g_running.clear();
    }
    if (last) last->close();  // outside g_lock: the output's thread may be waiting on it
}

bool coreRunning() {
    std::lock_guard<std::mutex> g(g_lock);
    return g_out != nullptr;
}
#else
bool ensureCore() {
    std::lock_guard<std::mutex> g(g_lock);
    if (g_asked != "local" && !g_failed) {
        SDL_Log("audio: backend %s failed (this build has no gd-engine-core audio); using local", g_asked.c_str());
        g_failed = true;
    }
    return false;
}
#endif

}  // namespace

void setBackend(const std::string& id) {
    std::lock_guard<std::mutex> g(g_lock);
    g_asked = id.empty() ? "local" : id;
    g_failed = false;
}

std::string backend() {
    std::lock_guard<std::mutex> g(g_lock);
    return g_running.empty() ? (g_failed ? "local" : "") : g_running;
}

std::string askedBackend() {
    std::lock_guard<std::mutex> g(g_lock);
    return g_asked;
}

SDL_AudioStream* openStream(const SDL_AudioSpec& spec, SDL_AudioStreamCallback callback, void* user) {
    std::lock_guard<std::mutex> o(g_openLock);
#if GDCORE_HAS_AUDIO
    if (ensureCore()) {
        std::lock_guard<std::mutex> g(g_lock);
        const SDL_AudioSpec dst{SDL_AUDIO_F32, g_outSpec.channels, g_outSpec.sampleRate};
        SDL_AudioStream* s = SDL_CreateAudioStream(&spec, &dst);
        if (s && !SDL_SetAudioStreamGetCallback(s, callback, user)) SDL_DestroyAudioStream(s), s = nullptr;
        if (!s) return nullptr;
        g_songStreams.push_back(s);
        ++g_streams;
        return s;
    }
#else
    ensureCore();
#endif
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
    g_running = "local";
    return s;
}

void closeStream(SDL_AudioStream* stream) {
    if (!stream) return;
    std::lock_guard<std::mutex> o(g_openLock);
#if GDCORE_HAS_AUDIO
    {
        bool core = false;
        {
            std::lock_guard<std::mutex> g(g_lock);
            auto it = std::find(g_songStreams.begin(), g_songStreams.end(), stream);
            if (it != g_songStreams.end()) {
                core = true;
                g_songStreams.erase(it);  // no pull reaches it after this
                SDL_DestroyAudioStream(stream);
                --g_streams;
            }
        }
        if (core) return releaseCore();
    }
#endif
    SDL_DestroyAudioStream(stream);
    std::lock_guard<std::mutex> g(g_lock);
    if (--g_streams == 0 && g_device) {
        SDL_CloseAudioDevice(g_device);
        g_device = 0;
        if (!g_fxDevice) g_running.clear();
    }
}

bool openFx(std::string& error) {
    std::lock_guard<std::mutex> o(g_openLock);
    if (ensureCore()) {
        std::lock_guard<std::mutex> g(g_lock);
        g_fxOpen = true;
        return true;
    }
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!dev) {
        error = SDL_GetError();
        return false;
    }
    // GD10_AUDIODUMP: this device's mix too (dumpPostmix).
    if (dumpEnabled())
        SDL_SetAudioPostmixCallback(
            dev, [](void*, const SDL_AudioSpec* spec, float* buffer, int bytes) { dumpPostmix(1, "sfx", spec, buffer, bytes); }, nullptr);
    std::lock_guard<std::mutex> g(g_lock);
    g_fxDevice = dev, g_fxOpen = true, g_running = "local";
    return true;
}

void closeFx() {
    std::lock_guard<std::mutex> o(g_openLock);
    SDL_AudioDeviceID dev = 0;
    {
        std::lock_guard<std::mutex> g(g_lock);
        if (!g_fxOpen) return;
        g_fxOpen = false;
        dev = g_fxDevice, g_fxDevice = 0;
#if GDCORE_HAS_AUDIO
        g_fxStreams.clear();
#endif
        if (dev && !g_device) g_running.clear();
    }
    if (dev) SDL_CloseAudioDevice(dev);
#if GDCORE_HAS_AUDIO
    releaseCore();
#endif
}

bool bindFx(SDL_AudioStream* stream) {
    if (!stream) return false;
#if GDCORE_HAS_AUDIO
    if (coreRunning()) {
        std::lock_guard<std::mutex> g(g_lock);
        if (!g_fxOpen) return false;
        const SDL_AudioSpec dst{SDL_AUDIO_F32, g_outSpec.channels, g_outSpec.sampleRate};
        if (!SDL_SetAudioStreamFormat(stream, nullptr, &dst)) return false;
        if (std::find(g_fxStreams.begin(), g_fxStreams.end(), stream) == g_fxStreams.end()) g_fxStreams.push_back(stream);
        return true;
    }
#endif
    SDL_AudioDeviceID dev;
    {
        std::lock_guard<std::mutex> g(g_lock);
        dev = g_fxDevice;
    }
    return dev && SDL_BindAudioStream(dev, stream);
}

void unbindFx(SDL_AudioStream* stream) {
    if (!stream) return;
#if GDCORE_HAS_AUDIO
    {
        std::lock_guard<std::mutex> g(g_lock);
        auto it = std::find(g_fxStreams.begin(), g_fxStreams.end(), stream);
        if (it != g_fxStreams.end()) return void(g_fxStreams.erase(it));  // no pull reaches it after this
    }
#endif
    SDL_UnbindAudioStream(stream);
}

void toggleLimiter() { g_limiterOn.store(!g_limiterOn.load()); }

bool limiterEnabled() { return g_limiterOn.load(); }

bool dumpEnabled() {
    static const bool on = SDL_getenv("GD10_AUDIODUMP") != nullptr;
    return on;
}

// A WAVE_FORMAT_IEEE_FLOAT file; its sizes rewritten after each block, so
// it stays readable however the game ends.
void dumpPostmix(int slot, const char* tag, const SDL_AudioSpec* spec, const float* buffer, int bytes) {
    if (slot < 0 || slot > 1 || !spec || spec->channels <= 0 || bytes <= 0) return;
    Dump& d = g_dumps[slot];
    if (!d.tried) {
        d.tried = true;
        const char* prefix = SDL_getenv("GD10_AUDIODUMP");
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
