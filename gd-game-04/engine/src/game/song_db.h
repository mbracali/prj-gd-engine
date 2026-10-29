// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Globals;

// GH3's song list: scripts\guitar\songlist.qb's gh3_songlist (the order)
// and gh3_songlist_props (one struct per song) [C data].
struct SongInfo {
    uint32_t checksum = 0;   // the song's key (props "checksum")
    std::string name;        // file stem: songs\<name>.mid.qb, MUSIC/<name>.fsb.xen
    std::string title, artist, year, countoff;
    bool originalArtist = false;  // "by" vs "as made famous by"
    bool rhythmTrack = false;     // the second part is rhythm guitar (else bass) [I: props "rhythm_track"]
    int gemOffset = 0, inputOffset = 0;  // per-song offsets (ms) [I: units]
    float bandVolume = 0, guitarVolume = 0;  // "band_playback_volume", "guitar_playback_volume" (dB?) [I]
    uint32_t singer = 0;     // $male / $female / $none ...
    bool keyboard = false;
};

class SongDb {
public:
    // Reads the list from the loaded globals; returns false if missing.
    bool load(const Globals& g);
    const std::vector<SongInfo>& songs() const { return songs_; }
    const SongInfo* find(const std::string& name) const;

private:
    std::vector<SongInfo> songs_;
};
