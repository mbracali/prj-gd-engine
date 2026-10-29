// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/selftest_play.h"

#include <cstdio>

#include "game/chart.h"
#include "game/song_db.h"
#include "script/globals.h"

// Until the gameplay rules are reimplemented (M2, docs/FINDINGS.md), this
// only counts what the charts hold.
bool playChart(const Globals&, const SongInfo&, const Chart& chart, PlayTotals& t, std::string&) {
    for (size_t p = 0; p < size_t(Part::Count); ++p) {
        for (int d = 0; d < kDifficulties; ++d) {
            const auto& notes = chart.parts[p].notes[size_t(d)];
            if (notes.empty()) continue;
            ++t.charts;
            for (const ChartNote& n : notes) {
                ++t.notes;
                if (n.frets & (n.frets - 1)) ++t.chords;
                if (n.flag20) ++t.flagged;
            }
            t.phrases += chart.parts[p].star[size_t(d)].size();
        }
    }
    return true;
}

void printPlayTotals(const PlayTotals& t) {
    std::printf("charts: %zu, notes: %zu, chords: %zu, flagged (0x20): %zu, star phrases: %zu\n", t.charts, t.notes, t.chords,
                t.flagged, t.phrases);
}
