// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Additive songs (GDALL-042): playing another game's songs inside GH2. The
// launcher shares the discs the player exposed (Additive Music tool); here GH2
// opens each one through gd-engine-core's reader (gd/core/gh1/reader.h for
// Guitar Hero 1) and turns its songs into GH2 SongInfo entries, charts and
// audio sources. Nothing is extracted; everything is read in memory from the
// player's own disc.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/chart.h"
#include "game/song_db.h"

class AudioSource;

namespace gd::core::gh1 {
class Disc;
}

// The foreign discs GH2 opened for this run; SongInfo::foreignDisc indexes it.
struct ForeignLibrary {
    std::vector<std::shared_ptr<gd::core::gh1::Disc>> discs;
};

// Opens each shared disc the reader understands and returns a GH2 SongInfo per
// song (foreignDisc / foreignSong set, audio pans/vols/cores filled). The
// opened discs are kept in `lib` for loadForeignChart / loadForeignAudio.
std::vector<SongInfo> openForeignSongs(const std::vector<std::pair<std::string, std::string>>& extra, ForeignLibrary& lib);

// Builds a GH2 Song (its guitar part) from a foreign song's chart.
Song loadForeignChart(const ForeignLibrary& lib, const SongInfo& info);

// A GH2 AudioSource over a foreign song's audio.
std::unique_ptr<AudioSource> loadForeignAudio(const ForeignLibrary& lib, const SongInfo& info);
