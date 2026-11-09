// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// gdcore_audio_selftest [--device] [id ...]: opens every backend compiled in
// (or the ones named) on its null / dummy device (--device: the real default
// device instead, for a quick listen), pulls buffers for a while, pauses,
// resumes and closes it. Exit code 0 when every backend that is available
// passed; a backend that is not available (OpenAL Soft's library missing) is
// reported and skipped unless named on the command line.
#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "gd/core/audio/output.h"

namespace audio = gd::core::audio;
using namespace std::chrono_literals;

namespace {

bool waitFor(const std::atomic<long long>& frames, long long target, std::chrono::milliseconds limit) {
    auto end = std::chrono::steady_clock::now() + limit;
    while (frames.load() < target) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

bool testBackend(const std::string& id, bool realDevice) {
    std::string error;
    std::unique_ptr<audio::Output> out = audio::create(id, error);
    if (!out) {
        std::printf("FAIL %s: create: %s\n", id.c_str(), error.c_str());
        return false;
    }
    std::atomic<long long> frames{0};
    std::atomic<int> calls{0}, badCount{0};
    std::atomic<int> channels{2};
    double phase = 0;
    audio::Spec want;
    want.sampleRate = 44100;  // gd01's song rate: some backends keep it, some resample
    want.channels = 2;
    want.framesHint = 512;
    want.nullDevice = !realDevice;
    const bool ok = out->open(want, [&](float* buf, int n) {
        if (n <= 0 || n > 1 << 16) badCount.fetch_add(1);
        const int ch = channels.load();
        for (int i = 0; i < n; ++i) {
            const float s = 0.1f * float(std::sin(phase));
            phase += 2 * 3.14159265358979 * 440.0 / 44100.0;
            for (int c = 0; c < ch; ++c) buf[i * ch + c] = s;
        }
        frames.fetch_add(n);
        calls.fetch_add(1);
    }, error);
    if (!ok) {
        std::printf("FAIL %s: open: %s\n", id.c_str(), error.c_str());
        return false;
    }
    const audio::Spec got = out->spec();
    channels = got.channels;
    std::printf("     %s: %s\n     %d Hz, %d ch, period %d frames, latency %.1f ms\n", id.c_str(), out->name().c_str(),
                got.sampleRate, got.channels, got.framesHint, out->latencyMs());
    std::this_thread::sleep_for(50ms);
    if (calls.load() != 0) {
        std::printf("FAIL %s: pulled before start()\n", id.c_str());
        return false;
    }
    if (!out->start(error)) {
        std::printf("FAIL %s: start: %s\n", id.c_str(), error.c_str());
        return false;
    }
    // 0.25 s of audio: the null devices consume in real time.
    const long long quarter = got.sampleRate / 4;
    const auto t0 = std::chrono::steady_clock::now();
    if (!waitFor(frames, quarter, 3000ms)) {
        std::printf("FAIL %s: only %lld frames pulled in 3 s\n", id.c_str(), frames.load());
        return false;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    out->pause();
    const long long atPause = frames.load();
    std::this_thread::sleep_for(150ms);
    const long long afterPause = frames.load();
    if (afterPause != atPause) {
        std::printf("FAIL %s: pulled %lld frames while paused\n", id.c_str(), afterPause - atPause);
        return false;
    }
    if (!out->start(error) || !waitFor(frames, afterPause + quarter / 2, 3000ms)) {
        std::printf("FAIL %s: did not resume (%s)\n", id.c_str(), error.c_str());
        return false;
    }
    out->close();
    const long long atClose = frames.load();
    std::this_thread::sleep_for(100ms);
    if (frames.load() != atClose) {
        std::printf("FAIL %s: pulled after close()\n", id.c_str());
        return false;
    }
    out->close();  // twice is fine
    if (badCount.load()) {
        std::printf("FAIL %s: %d pulls with a bad frame count\n", id.c_str(), badCount.load());
        return false;
    }
    std::printf("ok   %s: %lld frames in %d pulls; the first %lld in %.2f s; pause, resume, close\n", id.c_str(), atClose,
                calls.load(), quarter, secs);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool realDevice = false;
    std::vector<std::string> named;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--device") realDevice = true;
        else named.push_back(a);
    }
    // FAudio's null device: SDL's dummy driver (FAudio starts SDL audio).
    if (!realDevice) SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    int failures = 0;
    std::vector<std::string> ids = named;
    std::printf("gdcore_audio self-test (%s)\n", realDevice ? "default devices" : "null devices");
    for (const audio::Backend& b : audio::backends()) {
        std::string why;
        const bool avail = b.probe(why);
        std::printf("     backend %s (%s): %s%s\n", b.id.c_str(), b.displayName.c_str(), avail ? "available" : "not available: ",
                    avail ? "" : why.c_str());
        if (named.empty() && avail) ids.push_back(b.id);
    }
    std::string error;
    if (audio::create("no-such-backend", error) || error.empty()) {
        std::printf("FAIL unknown ids must fail with an error\n");
        ++failures;
    }
    for (const std::string& id : ids)
        if (!testBackend(id, realDevice)) ++failures;
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    SDL_Quit();
    return failures ? 1 : 0;
}
