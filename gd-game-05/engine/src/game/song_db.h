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
    int hopoThresholdTicks = -1;  // (hopo_threshold n), or -1: the parser's default
    std::vector<int> crowdChannels;  // (crowd_channels 11 12): the crowd stems [C data]
    // (drum_solo (seqs (kick.cue ...))) / (drum_freestyle (seqs ...)): the
    // drum kit's sounds per lane (kick, red, yellow, blue, green) in fills
    // and freestyle [C data].
    std::vector<std::string> drumSoloSeqs, drumFreestyleSeqs;
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
    // Rock Band's song entry [C data: songs.dtb]:
    bool master = false;           // (master TRUE): the master recording
    int scrollSpeedMs = 2300;      // (song_scroll_speed 2300)
    std::string bank;              // (bank sfx/tambourine_bank.milo): the vocal percussion sounds
    std::map<std::string, int> rank;  // (rank (drum 300) (guitar 285) ... (band 300)): the difficulty tiers
    std::string genre, decade, vocalGender;
    int context = 0;               // (context n)
    bool tutorial = false;         // a tutorial / drum trainer / test track (no rank), not in the song lists
    bool unlockable = false;       // (unlockable TRUE): a bonus song, unlocked by the tours only (SongMgr 0x82434020) [C data]
};

// The songs of config/gen/songs.dtb (retail build: _SHIP and HX_XBOX
// defined, so the test entries inside #ifndef _SHIP are dropped) whose chart
// and audio exist in the archive, in file order.
std::vector<SongInfo> loadSongs(const GameData& data);
