// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <string>

class Globals;
struct SongInfo;
struct Chart;

// Totals over every chart the self-test plays (printed, so score changes
// show up in diffs; lesson L4).
struct PlayTotals {
    bool rulesImplemented = false;
    size_t charts = 0, notes = 0, chords = 0, sustains = 0, hopos = 0, flagged = 0, phrases = 0;
};

// Plays every part and difficulty of a chart with the autoplayer; false and
// `why` when a note was not hit.
bool playChart(const Globals& g, const SongInfo& song, const Chart& chart, PlayTotals& totals, std::string& why);
void printPlayTotals(const PlayTotals& t);
