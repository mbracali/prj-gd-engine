// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/audiotest.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>

#include "audio/at3_preview.h"
#include "audio/audio_out.h"
#include "data/song_list.h"
#include "data/umd.h"

int audioTest(const gd24::Umd& umd, const std::string& which, bool play) {
    int failures = 0, tested = 0;
    double totalSec = 0;
    const uint64_t t0 = SDL_GetTicksNS();
    std::shared_ptr<PcmClip> first;
    for (const gd24::SongEntry& s : gd24::readSongList(umd)) {
        if (s.tutorial || !(which.empty() || which == "all" || gd24::upperPath(which) == s.folder)) continue;
        ++tested;
        std::vector<uint8_t> bytes;
        auto clip = std::make_shared<PcmClip>();
        std::string error;
        if (!umd.read(s.previewPath(), bytes) || !decodeAt3(bytes, *clip, error)) {
            std::printf("audiotest: FAIL %s: %s\n", s.folder.c_str(), error.empty() ? "cannot read" : error.c_str());
            ++failures;
            continue;
        }
        double sum = 0;
        float peak = 0;
        for (float v : clip->samples) sum += double(v) * v, peak = std::max(peak, std::fabs(v));
        const double rms = clip->samples.empty() ? 0 : std::sqrt(sum / double(clip->samples.size()));
        const double rmsDb = rms > 0 ? 20 * std::log10(rms) : -999;
        // What the decoder gave after the delay, against the fact chunk.
        const long long got = (long long)clip->decodedFrames - (long long)clip->delay;
        const long long shortBy = (long long)clip->factSamples - got;
        std::string why;
        if (clip->errors) why += " decode errors";
        if (clip->rate != 44100 || clip->channels != 2) why += " not stereo 44.1 kHz";
        if (!clip->factSamples || shortBy > 2048 || shortBy < -2048) why += " length off the fact chunk by more than a frame";
        if (rmsDb < -40 || peak < 0.05f) why += " (near) silent";
        if (peak > 1.5f) why += " clipping garbage";
        std::printf("audiotest: %s %-18s %3d frames, %d errors, %8zu samples (fact %u, delay %u, decoded %lld after the delay: %+lld) "
                    "%.2f s, peak %.3f, RMS %.1f dB\n",
                    why.empty() ? "ok  " : "FAIL", s.folder.c_str(), clip->packets, clip->errors, clip->frames(), clip->factSamples,
                    clip->delay, got, -shortBy, clip->seconds(), peak, rmsDb);
        if (!why.empty()) {
            std::printf("audiotest:   %s:%s\n", s.folder.c_str(), why.c_str());
            ++failures;
        }
        totalSec += clip->seconds();
        if (!first) first = clip;
    }
    const double ms = double(SDL_GetTicksNS() - t0) / 1e6;
    std::printf("audiotest: %d previews, %.1f s of audio decoded in %.0f ms; %d failures\n", tested, totalSec, ms, failures);
    if (!tested) return 1;
    if (play && first) {
        SDL_Init(SDL_INIT_AUDIO);
        AudioOut out;
        std::string error;
        if (!out.open(error)) {
            std::printf("audiotest: no audio output: %s\n", error.c_str());
            return 1;
        }
        out.play(first, false);
        SDL_Delay(3000);
        std::printf("audiotest: played 3 s through %s, position %.2f s\n", out.name().c_str(), out.positionSec());
        out.close();
        SDL_Quit();
    }
    return failures ? 1 : 0;
}
