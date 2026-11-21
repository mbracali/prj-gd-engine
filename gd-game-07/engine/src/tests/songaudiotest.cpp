// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/songaudiotest.h"

#include <cstdio>
#include <memory>

#include "audio/song_stream.h"
#include "data/disc.h"

int runSongAudioTest(const std::string& discPath, const std::string& song) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::fprintf(stderr, "songaudiotest: %s\n", error.c_str());
        return 2;
    }
    auto stream = SongStream::open(*disc, song, error);
    if (!stream) {
        std::fprintf(stderr, "songaudiotest: %s\n", error.c_str());
        return 1;
    }
    const int rate = stream->sampleRate(), step = rate * 5;
    const int seconds = int(stream->length() / uint32_t(rate));
    std::printf("songaudiotest: %s, %d Hz, %d s\n", song.c_str(), rate, seconds);
    bool any = false;
    for (int t = 0; t < seconds; t += 5) {
        const std::vector<float> rms = stream->pairRms(step);
        std::printf("%4d s", t);
        for (float r : rms) {
            std::printf(" %6.3f", r);
            any = any || r > 0.001f;
        }
        std::printf("\n");
    }
    if (!stream->error().empty()) std::printf("songaudiotest: %s\n", stream->error().c_str());
    std::printf("songaudiotest: %s\n", any ? "decoded" : "SILENT");
    return any ? 0 : 1;
}
