// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// What a song's BEATMATCH image says about the song and the game (the DTA
// tree in it, data/dta.h): the song's entry under (songs (<folder in lower
// case> ...)) and a few game settings.
//   (preview <start ms> <end ms>)   where the disc's _PREVIEW.AT3 sits in the
//                                   song [I: the preview lengths agree with
//                                   the AT3s', --charttest prints both]
//   (song_scroll_speed 2300)        Rock Band's highway time in ms [C data]
//   (initial_track 2)               the track the song starts on [C data]
//   (game (track (y_per_second (0 7.5) (1 8.5) (2 9.5) (3 11.5))))
//                                   the highway's speed per difficulty in
//                                   model units per second [C data; the
//                                   executable has no such string, so whether
//                                   its code reads it is not known: I]
//   (beatmatcher (watcher (slop 100) (intent_window 250)))
//                                   the hit window in ms [C data, same note]
#pragma once

#include <string>

namespace gd24 {

struct Beatmatch;
struct BeatmatchImage;

struct SongConfig {
    bool found = false;     // the song's entry
    int previewStartMs = 0, previewEndMs = 0;
    int scrollSpeedMs = 0;  // song_scroll_speed
    int initialTrack = -1;  // 0 drums 1 bass 2 guitar 3 vocals [I: the (tracks (drum 0) (bass 1) (guitar 2) (vocals 3)) order]
    float yPerSecond[4] = {0, 0, 0, 0};  // per difficulty, 0: not in the data
    int slopMs = 0;
};

// `song` is the folder name ("ABC"); the DTA name is its lower case.
SongConfig readSongConfig(const Beatmatch& bm, const BeatmatchImage& im, const std::string& song);

}  // namespace gd24
