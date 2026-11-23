// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <vector>

class GameData;

struct SongInfo {
    std::string id;         // e.g. "aceofspades"
    std::string name;
    std::string artist;
    std::string audioPath;  // ARK path of the .vgs
    std::string midiPath;   // ARK path of the .mid
    std::vector<float> pans;  // per audio channel, -1 (left) .. 1 (right)
    std::vector<float> vols;
    std::vector<int> cores;   // 1 = the player's guitar part (muted on misses)
    float previewStartMs = 0, previewEndMs = 0;
    std::string venue;      // quickplay venue folder, e.g. "arena"
    std::string character;  // quickplay guitarist archetype, e.g. "metal"
    std::string guitar;     // quickplay guitar, e.g. "gibson_lespaul"
    int animTempo = 1;      // character clip tempo: 1 medium, 2 fast (kTempo*)
    // (band SINGER_FEMALE_METAL BASS_METAL DRUMMER_METAL): the song's backing band
    // (band_chars.dta macros); empty = charsys.dta's default band.
    std::vector<std::string> band;
    // Additive songs (GDALL-042): a song read from another game's disc, not GH1's
    // own. foreignDisc >= 0 indexes App's foreign library; foreignSong is its
    // index on that disc; foreignLabel is its section header ("Guitar Hero II").
    int foreignDisc = -1;
    int foreignSong = -1;
    std::string foreignLabel;
    bool foreign() const { return foreignDisc >= 0; }
};

// Songs from config/songs.dta whose chart and audio exist in the archive.
std::vector<SongInfo> loadSongs(const GameData& data);
