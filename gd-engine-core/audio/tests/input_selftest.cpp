// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gdcore_audio_input_selftest: every capture backend on its null / dummy
// device: nothing pushed before start, frames pushed in real time after it,
// nothing while paused, nothing after close, close twice, an unknown id
// fails. `--device` uses the real default recording devices instead; ids on
// the command line limit the run.
#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "gd/core/audio/input.h"

using namespace gd::core::audio;

int main(int argc, char** argv) {
    bool real = false;
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--device") == 0) real = true;
        else only.push_back(argv[i]);
    }
    if (!real) SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        failures += ok ? 0 : 1;
    };
    std::string err;
    check(!createInput("no_such_backend", err) && !err.empty(), "an unknown id fails with an error");
    for (const InputBackend& b : inputBackends()) {
        if (!only.empty() && std::find(only.begin(), only.end(), b.id) == only.end()) continue;
        std::printf("%s:\n", b.id.c_str());
        auto in = createInput(b.id, err);
        check(in != nullptr, "created");
        if (!in) continue;
        std::atomic<long> frames{0};
        InputSpec spec;
        spec.sampleRate = 48000, spec.channels = 1, spec.nullDevice = !real;
        if (!in->open(spec, [&](const float*, int n) { frames += n; }, err)) {
            check(false, "open: " + err);
            continue;
        }
        std::printf("  %s, %.1f ms\n", in->name().c_str(), in->latencyMs());
        SDL_Delay(100);
        check(frames == 0, "nothing pushed before start");
        check(in->start(err), "start");
        SDL_Delay(500);
        const long got = frames;
        check(got > spec.sampleRate / 10, "frames pushed in real time (" + std::to_string(got) + " in 0.5 s)");
        in->pause();
        const long paused = frames;
        SDL_Delay(200);
        check(frames - paused < spec.sampleRate / 20, "nothing (more than a period) while paused");
        in->close();
        const long closed = frames;
        SDL_Delay(100);
        check(frames == closed, "nothing after close");
        in->close();
        check(true, "close twice");
        std::printf("  devices: %zu\n", b.devices ? b.devices().size() : size_t(0));
    }
    std::printf("input selftest: %s\n", failures ? "FAIL" : "OK");
    SDL_Quit();
    return failures ? 1 : 0;
}
