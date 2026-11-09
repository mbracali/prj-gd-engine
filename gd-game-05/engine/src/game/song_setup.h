// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "game/chart.h"
#include "game/rules.h"
#include "game/song_db.h"

class GameData;

// The HOPO threshold in ticks for a song: its block's hopo_threshold, else
// beatmatcher.dtb's (170) [C data].
int hopoThreshold(const SongAudio& audio, const Rules& rules);

// Reads the song's chart and builds every part (throws on failure).
Song loadChart(const GameData& data, const SongInfo& info, const Rules& rules);
