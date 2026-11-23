// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

class GameData;

// One `song` / `song_coop` block of songs.dtb (docs/songs.md) [C data]:
// the .mogg, which of its channels each instrument uses, and per channel
// pan, volume and "core" flag.
struct SongAudio {
    std::string moggPath;  // ARK path: (name songs/x/x) + ".mogg"
    std::string midiPath;  // (midi_file songs/x/x.mid)
    // (tracks ((guitar (2 3)) (bass 4))): instrument -> channels. Channels in
    // no list are the backing band (always 0-1 in GH2's songs).
    std::map<std::string, std::vector<int>> tracks;
    std::vector<float> pans;  // per channel, -1 left .. 1 right
    std::vector<float> vols;  // per channel, dB
    // 1 exactly on the guitar channels [C data]. That these are the
    // channels muted when the player misses is [I] (docs/audio_and_charts.md).
    std::vector<int> cores;
    int hopoThresholdTicks = -1;  // (hopo_threshold n), or -1: beatmatcher.dtb's
};

struct SongInfo {
    std::string id;  // e.g. "yyz"
    std::string name;
    std::string artist;
    SongAudio audio;                     // (song ...)
    std::optional<SongAudio> coopAudio;  // (song_coop ...), 17 songs
    // Practice (songs.dta): (practice_speeds (100 85 65 50)) [C data]; empty
    // when the song has none. song_practice_1..3 name audio the disc lacks.
    std::vector<int> practiceSpeeds;
    float previewStartMs = 0, previewEndMs = 0;
    int animTempo = 1;  // kTempoMedium 1, kTempoFast 2
    std::string venue;  // (quickplay (venue x))
    // Additive songs (GDALL-042): a song read from another game's disc, not
    // GH2's own. foreignDisc >= 0 indexes App's foreign library; foreignSong is
    // the song's index on that disc; foreignLabel is its section header
    // ("Guitar Hero"). audio.pans/vols/cores are the source song's; moggPath /
    // midiPath are empty (the chart and audio come from the foreign reader).
    int foreignDisc = -1;
    int foreignSong = -1;
    std::string foreignLabel;
    bool foreign() const { return foreignDisc >= 0; }
};

// The songs of config/gen/songs.dtb (retail build: _SHIP and HX_XBOX
// defined, so the test entries inside #ifndef _SHIP are dropped) whose chart
// and audio exist in the archive, in file order.
std::vector<SongInfo> loadSongs(const GameData& data);
