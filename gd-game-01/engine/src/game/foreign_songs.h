// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Additive songs (GDALL-042): playing another game's songs inside GH1. The
// launcher shares the discs the player exposed; here GH1 opens each one through
// gd-engine-core's reader (gd/core/gh2/reader.h for Guitar Hero 2) and turns its
// songs into GH1 SongInfo entries, charts and audio. GH1's mixer only plays VGS
// (PS-ADPCM), so the foreign audio is transcoded to VGS in memory; the chart is
// rebuilt as a GH1 chart. Nothing is extracted to disk.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "data/bytes.h"
#include "game/chart.h"
#include "game/song_db.h"

namespace gd::core::gh2 {
class Disc;
}

// The foreign discs GH1 opened for this run; SongInfo::foreignDisc indexes it.
struct ForeignLibrary {
    std::vector<std::shared_ptr<gd::core::gh2::Disc>> discs;
};

// Opens each shared disc the reader understands and returns a GH1 SongInfo per
// song (foreignDisc/foreignSong set, pans/vols/cores filled in GH1's linear
// convention). The opened discs stay in `lib`.
std::vector<SongInfo> openForeignSongs(const std::vector<std::pair<std::string, std::string>>& extra, ForeignLibrary& lib);

// Builds a GH1 Song from a foreign song's chart (guitar part, every difficulty).
Song loadForeignChart(const ForeignLibrary& lib, const SongInfo& info);

// Transcodes a foreign song's audio into VGS bytes GH1's VgsStream can play
// (decoded, resampled to 44.1 kHz, PS-ADPCM encoded). Empty on failure.
Bytes loadForeignVgs(const ForeignLibrary& lib, const SongInfo& info);

// As loadForeignVgs, but only the [startMs, endMs) window (the song's preview
// section), so a hover preview transcodes a few seconds, not the whole song.
// The returned VGS starts at the window, so loop it from 0.
Bytes loadForeignVgsWindow(const ForeignLibrary& lib, const SongInfo& info, float startMs, float endMs);
