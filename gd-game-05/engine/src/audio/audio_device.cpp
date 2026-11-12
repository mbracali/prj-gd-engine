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
#include "gd/core/audio/output.h"

namespace audiodev {

namespace {

namespace core = gd::core::audio;

std::mutex g_lock;
std::mutex g_openLock;  // openStream / closeStream, one at a time (the output opens and closes in them)
std::atomic<bool> g_limiterOn{true};
sfxdsp::Limiter g_limiter;  // the output's thread only
int g_limiterRate = 0;

// The master limiter on the summed mix (SDL's postmix point before the core).
void limit(float* buffer, int frames, int channels, int rate) {
    if (!g_limiterOn.load() || channels <= 0) return;
    if (rate != g_limiterRate) {
        g_limiterRate = rate;
        g_limiter.configure(1, 50, -2, rate);  // default.dta synth limiter [C data]
    }
    g_limiter.process(buffer, frames, channels);
}

// Enhanced > Audio backend (setBackend): the backend asked for, the one that
// plays ("" while nothing is open, "none" when none could start).
std::string g_asked = kDefaultBackend, g_running;
bool g_failed = false;  // every backend failed: no sound for the rest of the run

// The output: the streams are bound to no device; the output's thread pulls
// each (which runs its get callback, as SDL's device thread would), sums them
// as SDL mixes bound streams, and passes the sum through the master limiter.
std::unique_ptr<core::Output> g_out;
core::Spec g_outSpec;
std::vector<SDL_AudioStream*> g_streams;  // under g_lock
std::vector<float> g_scratch;             // the output's thread only

void pull(float* out, int frames) {
    const int ch = g_outSpec.channels, n = frames * ch, bytes = n * int(sizeof(float));
    std::fill(out, out + n, 0.0f);
    if (int(g_scratch.size()) < n) g_scratch.resize(size_t(n));
    {
        std::lock_guard<std::mutex> g(g_lock);
        for (SDL_AudioStream* s : g_streams) {
            const int got = SDL_GetAudioStreamData(s, g_scratch.data(), bytes);
            const int m = std::max(0, got) / int(sizeof(float));
            for (int i = 0; i < m; ++i) out[i] += g_scratch[size_t(i)];
        }
    }
    limit(out, frames, ch, g_outSpec.sampleRate);
}

// Opens and starts the output `id` (outside g_lock: a backend's thread may
// pull, which takes it, while its start waits for that thread). nullptr and
// `why` when it cannot.
std::unique_ptr<core::Output> openOutput(const std::string& id, std::string& why) {
    std::unique_ptr<core::Output> out = core::create(id, why);
    if (!out) return nullptr;
    core::Spec want;
    want.sampleRate = 48000;  // the 360's mastering rate [C]; each stream converts to the output's
    want.channels = 2;
    // Headless (SDL's dummy driver: the self-tests, the walks): the backend's
    // null device, which consumes in real time like SDL's dummy.
    const char* drv = SDL_GetCurrentAudioDriver();
    want.nullDevice = drv && std::strcmp(drv, "dummy") == 0;
    if (!out->open(want, &pull, why)) return nullptr;
    g_outSpec = out->spec();  // before the first pull (start)
    if (!out->start(why)) return nullptr;
    SDL_Log("audio: backend %s: %s, %d Hz, %d channels (%.1f ms buffered)", id.c_str(), out->name().c_str(), g_outSpec.sampleRate,
            g_outSpec.channels, out->latencyMs());
    return out;
}

// The asked backend, then every other one this build has in menu order
// after the default (gd-engine-core rule 2: never left without sound).
std::vector<std::string> backendsToTry(const std::string& asked) {
    std::vector<std::string> ids{asked};
    if (asked != kDefaultBackend) ids.push_back(kDefaultBackend);
    for (const core::Backend& b : core::backends())
        if (std::find(ids.begin(), ids.end(), b.id) == ids.end()) ids.push_back(b.id);
    return ids;
}

}  // namespace

void setBackend(const std::string& id) {
    std::lock_guard<std::mutex> g(g_lock);
    g_asked = id.empty() ? kDefaultBackend : id;
    g_failed = false;
}

std::string backend() {
    std::lock_guard<std::mutex> g(g_lock);
    return g_running.empty() && g_failed ? "none" : g_running;
}

std::string askedBackend() {
    std::lock_guard<std::mutex> g(g_lock);
    return g_asked;
}

SDL_AudioStream* openStream(const SDL_AudioSpec& spec, SDL_AudioStreamCallback callback, void* user) {
    std::lock_guard<std::mutex> o(g_openLock);
    std::string asked;
    {
        std::lock_guard<std::mutex> g(g_lock);
        if (!g_out && !g_failed) asked = g_asked;
    }
    if (!asked.empty()) {
        std::unique_ptr<core::Output> out;
        std::string running;
        for (const std::string& id : backendsToTry(asked)) {
            std::string why;
            if ((out = openOutput(id, why))) {
                running = id;
                break;
            }
            SDL_Log("audio: backend %s failed (%s)", id.c_str(), why.c_str());
        }
        if (!out) SDL_Log("audio: no backend could start; no sound this run");
        std::lock_guard<std::mutex> g(g_lock);
        if (out) g_out = std::move(out), g_running = running;
        else g_failed = true;
    }
    std::lock_guard<std::mutex> g(g_lock);
    if (!g_out) {
        SDL_SetError("no audio output");
        return nullptr;
    }
    const SDL_AudioSpec dst{SDL_AUDIO_F32, g_outSpec.channels, g_outSpec.sampleRate};
    SDL_AudioStream* s = SDL_CreateAudioStream(&spec, &dst);
    if (s && !SDL_SetAudioStreamGetCallback(s, callback, user)) SDL_DestroyAudioStream(s), s = nullptr;
    if (s) g_streams.push_back(s);
    return s;
}

void closeStream(SDL_AudioStream* stream) {
    if (!stream) return;
    std::lock_guard<std::mutex> o(g_openLock);
    std::unique_ptr<core::Output> last;
    {
        std::lock_guard<std::mutex> g(g_lock);
        auto it = std::find(g_streams.begin(), g_streams.end(), stream);
        if (it == g_streams.end()) return;
        g_streams.erase(it);  // no pull reaches it after this
        SDL_DestroyAudioStream(stream);
        if (g_streams.empty()) last = std::move(g_out), g_running.clear();
    }
    if (last) last->close();  // outside g_lock: the output's thread may be waiting on it
}

void toggleLimiter() { g_limiterOn.store(!g_limiterOn.load()); }

bool limiterEnabled() { return g_limiterOn.load(); }

}  // namespace audiodev
