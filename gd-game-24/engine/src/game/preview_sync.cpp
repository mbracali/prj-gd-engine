// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/preview_sync.h"

#include <algorithm>
#include <cmath>

namespace gd24 {

PreviewLag measurePreviewLag(const PcmClip& clip, const std::vector<Beatmatch::Track>& tracks, int previewStartMs) {
    PreviewLag out;
    if (!clip.rate || !clip.channels || clip.frames() < size_t(clip.rate)) return out;
        // Onsets: the positive change of the log energy per 10 ms frame, of
        // the signal and of its first difference (the highs).
        const int hop = clip.rate / 100, frames = int(clip.frames()) / hop;
        std::vector<double> flux(size_t(frames), 0), prev(2, 0);
        for (int f = 0; f < frames; ++f) {
            double band[2] = {0, 0};
            for (int i = 0; i < hop; ++i) {
                const size_t k = size_t(f * hop + i) * size_t(clip.channels);
                const double x = clip.samples[k] + (clip.channels > 1 ? clip.samples[k + 1] : 0);
                const size_t k1 = k >= size_t(clip.channels) ? k - size_t(clip.channels) : k;
                const double d1 = x - (clip.samples[k1] + (clip.channels > 1 ? clip.samples[k1 + 1] : 0));
                band[0] += x * x, band[1] += d1 * d1;
            }
            band[0] = std::log1p(band[0]), band[1] = std::log1p(band[1] * 4);
            for (int b = 0; b < 2; ++b) flux[size_t(f)] += std::max(0.0, band[b] - prev[size_t(b)]), prev[size_t(b)] = band[b];
        }
        // The chart: Expert drums (track 0) and guitar / bass gems, 10 ms bins.
        std::vector<double> chart(size_t(frames), 0);
        for (size_t t = 0; t < tracks.size() && t < 3; ++t)
            for (const Gem& g : tracks[t].difficulty.back().gems) {
                const int f = int(std::lround((g.ms - previewStartMs) / 10.0));
                if (f >= 0 && f < frames) chart[size_t(f)] += t == 0 ? 1.0 : 0.5;
            }
        double best = -1, sumScore = 0, nearBest = -1;
        int bestLag = 0, nearLag = 0, n = 0;
        for (int lag = -100; lag <= 100; ++lag) {  // +-1 s
            double sc = 0;
            for (int f = 0; f < frames; ++f) {
                const int a = f + lag;
                if (a >= 0 && a < frames) sc += chart[size_t(f)] * (flux[size_t(a)] + 0.5 * ((a > 0 ? flux[size_t(a - 1)] : 0) + (a + 1 < frames ? flux[size_t(a + 1)] : 0)));
            }
            sumScore += sc, ++n;
            if (sc > best) best = sc, bestLag = lag;
            if (std::abs(lag) <= 20 && sc > nearBest) nearBest = sc, nearLag = lag;
        }
        out.lagMs = nearLag * 10, out.globalLagMs = bestLag * 10;
        out.strength = n && sumScore > 0 ? nearBest / (sumScore / n) : 0;
        out.nearOfGlobal = best > 0 ? nearBest / best : 0;
    return out;
}

}  // namespace gd24
