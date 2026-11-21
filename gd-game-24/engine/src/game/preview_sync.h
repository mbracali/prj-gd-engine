// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Where the preview AT3 sits in the chart. The DTA's (preview <start> <end>)
// places it [C data], but for 23 of 41 songs the AT3's length differs from
// end - start by up to 11 s and its onsets sit up to ~200 ms off the chart
// placed from <start> (gd24 --synctest). So the song screen measures it: the
// lag that best lines the preview's onsets (energy flux, 10 ms frames) up
// with the Expert drum / bass / guitar gems placed from <start>, searched
// within +-200 ms (music repeats every beat, so a wider search finds a beat
// away) [I: a measurement, not the original's data; the original never
// plays a preview under the chart].
#pragma once

#include <vector>

#include "audio/at3_preview.h"
#include "data/beatmatch.h"

namespace gd24 {

struct PreviewLag {
    int lagMs = 0;          // the preview's onsets come this much after the chart's
    double strength = 0;   // the near peak / the mean score over +-1 s
    int globalLagMs = 0;   // the best lag over +-1 s
    double nearOfGlobal = 0;  // the near peak / the +-1 s peak
};

PreviewLag measurePreviewLag(const PcmClip& clip, const std::vector<Beatmatch::Track>& tracks, int previewStartMs);

}  // namespace gd24
