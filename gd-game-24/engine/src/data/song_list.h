// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The songs on the disc: the folders of PSP_GAME/USRDIR/MUSIC/ (41 songs and
// the tutorial tracks T01..T15) with their names from TEXT/STR_EN_MUSIC.TXT.
//
// STR_<L>_MUSIC.TXT holds 55 {title, artist, album} triples after the
// language name: the 41 songs in the alphabetical order of their folders,
// then "Lesson 1:" .. for the tutorials [C data: triple 0 "3's & 7's" =
// 3SAND7S, 1 "ABC" = ABC, ... 40 "Would?" = WOULD]. The game's own song
// table (JUKEBOX.STR, ids and short names) is not read yet: the order is
// [I] from the data, checked by the counts below.
#pragma once

#include <string>
#include <vector>

namespace gd24 {

class Umd;

struct SongEntry {
    std::string folder;   // "ABC"
    std::string dir;      // "PSP_GAME/USRDIR/MUSIC/ABC/"
    std::string title, artist, album;
    bool tutorial = false;
    bool hasPreview = false;  // <SONG>_PREVIEW.AT3
    std::string previewPath() const { return dir + folder + "_PREVIEW.AT3"; }
    std::string beatmatchPath() const { return dir + "BEATMATCH.STR"; }
};

// The songs (tutorials last), names in `language` ("EN", "FR", "GE", "IT",
// "SP"); folder names stand in when the table is missing or does not match.
std::vector<SongEntry> readSongList(const Umd& umd, const std::string& language = "EN");

}  // namespace gd24
