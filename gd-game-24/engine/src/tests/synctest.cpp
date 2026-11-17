// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/synctest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio/at3_preview.h"
#include "data/beatmatch.h"
#include "data/song_config.h"
#include "data/song_list.h"
#include "data/umd.h"
#include "game/preview_sync.h"

namespace gd24 {

int syncTest(const Umd& umd, const std::string& song) {
    int checked = 0, failures = 0, aligned = 0, exact = 0;
    std::vector<double> lags;
    for (const SongEntry& s : readSongList(umd)) {
        if (s.tutorial || (!song.empty() && song != "all" && s.folder != song)) continue;
        std::vector<uint8_t> bytes;
        Beatmatch bm;
        std::string error;
        if (!umd.read(s.beatmatchPath(), bytes) || !bm.parse(std::move(bytes), &error)) {
            std::printf("synctest: FAIL %s: %s\n", s.folder.c_str(), error.c_str()), ++failures;
            continue;
        }
        const BeatmatchImage& im = bm.images.back();
        const SongConfig cfg = readSongConfig(bm, im, s.folder);
        const std::vector<Beatmatch::Track> tracks = bm.readTracks(im, &error);
        PcmClip clip;
        if (!cfg.found || tracks.empty() || !umd.read(s.previewPath(), bytes) || !decodeAt3(bytes, clip, error)) {
            std::printf("synctest: FAIL %s: %s\n", s.folder.c_str(), cfg.found ? error.c_str() : "no (preview ...) entry"), ++failures;
            continue;
        }
        ++checked;
        const PreviewLag pl = measurePreviewLag(clip, tracks, cfg.previewStartMs);
        const int nearLag = pl.lagMs / 10, bestLag = pl.globalLagMs / 10;
        const double atMs = clip.seconds() * 1000.0, dtaMs = cfg.previewEndMs - cfg.previewStartMs;
        const bool ok = std::abs(nearLag) <= 3 && pl.strength > 1.3;
        aligned += ok, exact += std::abs(bestLag) <= 3;
        lags.push_back(nearLag * 10.0);
        std::printf("synctest: %s %-18s preview %6d..%6d ms (%5.0f ms), AT3 %5.0f ms (%+6.0f), lag %+4d ms (%.0f%% of the +-1 s peak at %+4d ms, peak / mean %.2f)\n",
                    ok ? "ok  " : "warn", s.folder.c_str(), cfg.previewStartMs, cfg.previewEndMs, dtaMs, atMs, atMs - dtaMs, nearLag * 10,
                    100.0 * pl.nearOfGlobal, bestLag * 10, pl.strength);
    }
    std::sort(lags.begin(), lags.end());
    std::printf("synctest: %d songs, %d with the near peak within +-30 ms (%d with the +-1 s peak there), median near lag %+.0f ms, %d failures\n",
                checked, aligned, exact, lags.empty() ? 0.0 : lags[lags.size() / 2], failures);
    return failures ? 1 : 0;
}

}  // namespace gd24
