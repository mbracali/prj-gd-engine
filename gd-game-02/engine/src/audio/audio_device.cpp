// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/audio_device.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "audio/sfx_dsp.h"

#if GDCORE_HAS_AUDIO
#include "gd/core/audio/output.h"
#endif

namespace audiodev {

namespace {

std::mutex g_lock;
std::mutex g_openLock;  // openStream / closeStream, one at a time (the device opens and closes in them)
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

// Enhanced > Audio backend (setBackend): the backend asked for, the one that
// runs ("" while nothing is open, "local" for SDL's device), and whether the
// asked one failed (then local for the rest of the run, as rule 2 asks).
std::string g_asked = "local", g_running;
bool g_failed = false;

#if GDCORE_HAS_AUDIO
// A gd-engine-core output in place of SDL's device: the streams are not bound
// to a device; the output's thread pulls each (which runs its get callback,
// as SDL's device thread would), sums them as SDL mixes bound streams, and
// passes the sum through the master limiter (SDL's postmix point).
std::unique_ptr<gd::core::audio::Output> g_out;
gd::core::audio::Spec g_outSpec;
std::vector<SDL_AudioStream*> g_coreStreams;  // under g_lock
std::vector<float> g_scratch;                 // the output's thread only

void pullCore(float* out, int frames) {
    const int ch = g_outSpec.channels, n = frames * ch, bytes = n * int(sizeof(float));
    std::fill(out, out + n, 0.0f);
    if (int(g_scratch.size()) < n) g_scratch.resize(size_t(n));
    {
        std::lock_guard<std::mutex> g(g_lock);
        for (SDL_AudioStream* s : g_coreStreams) {
            const int got = SDL_GetAudioStreamData(s, g_scratch.data(), bytes);
            const int m = std::max(0, got) / int(sizeof(float));
            for (int i = 0; i < m; ++i) out[i] += g_scratch[size_t(i)];
        }
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, ch, g_outSpec.sampleRate};
    postmix(nullptr, &spec, out, bytes);
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
    std::string open;
    {
        std::lock_guard<std::mutex> g(g_lock);
        if (g_asked != "local" && !g_failed && !g_out) open = g_asked;
    }
    if (!open.empty()) {
        std::unique_ptr<gd::core::audio::Output> out = openCore(open);
        std::lock_guard<std::mutex> g(g_lock);
        if (out) g_out = std::move(out);
        else g_failed = true;
    }
#endif
    std::lock_guard<std::mutex> g(g_lock);
#if GDCORE_HAS_AUDIO
    if (g_out) {
        const SDL_AudioSpec dst{SDL_AUDIO_F32, g_outSpec.channels, g_outSpec.sampleRate};
        SDL_AudioStream* s = SDL_CreateAudioStream(&spec, &dst);
        if (s && !SDL_SetAudioStreamGetCallback(s, callback, user)) SDL_DestroyAudioStream(s), s = nullptr;
        if (!s) return nullptr;
        g_coreStreams.push_back(s);
        g_running = g_asked;
        ++g_streams;
        return s;
    }
#else
    if (g_asked != "local" && !g_failed) {
        SDL_Log("audio: backend %s failed (this build has no gd-engine-core audio); using local", g_asked.c_str());
        g_failed = true;
    }
#endif
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
    g_running = "local";
    return s;
}

void closeStream(SDL_AudioStream* stream) {
    if (!stream) return;
    std::lock_guard<std::mutex> o(g_openLock);
#if GDCORE_HAS_AUDIO
    {
        std::unique_ptr<gd::core::audio::Output> last;
        bool core = false;
        {
            std::lock_guard<std::mutex> g(g_lock);
            auto it = std::find(g_coreStreams.begin(), g_coreStreams.end(), stream);
            if (it != g_coreStreams.end()) {
                core = true;
                g_coreStreams.erase(it);  // no pull reaches it after this
                SDL_DestroyAudioStream(stream);
                if (--g_streams == 0) last = std::move(g_out), g_running.clear();
            }
        }
        if (last) last->close();  // outside g_lock: the output's thread may be waiting on it
        if (core) return;
    }
#endif
    SDL_DestroyAudioStream(stream);
    std::lock_guard<std::mutex> g(g_lock);
    if (--g_streams == 0 && g_device) {
        SDL_CloseAudioDevice(g_device);
        g_device = 0;
        g_running.clear();
    }
}

void toggleLimiter() { g_limiterOn.store(!g_limiterOn.load()); }

bool limiterEnabled() { return g_limiterOn.load(); }

}  // namespace audiodev
