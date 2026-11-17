// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/song_list.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "data/text_table.h"
#include "data/umd.h"

namespace gd24 {

std::vector<SongEntry> readSongList(const Umd& umd, const std::string& language) {
    static const char* const kMusic = "PSP_GAME/USRDIR/MUSIC/";
    std::vector<SongEntry> songs, tutorials;
    for (const Umd::Entry* e : umd.list(kMusic)) {
        const std::string up = upperPath(e->path);
        const size_t slash = up.find('/', std::strlen(kMusic));
        if (slash == std::string::npos || up.compare(slash, std::string::npos, "/BEATMATCH.STR") != 0) continue;
        SongEntry s;
        s.folder = up.substr(std::strlen(kMusic), slash - std::strlen(kMusic));
        s.dir = up.substr(0, slash + 1);
        s.title = s.folder;
        s.tutorial = s.folder.size() == 3 && s.folder[0] == 'T' && std::isdigit(static_cast<unsigned char>(s.folder[1]));
        s.hasPreview = umd.exists(s.previewPath());
        (s.tutorial ? tutorials : songs).push_back(std::move(s));
    }
    auto byFolder = [](const SongEntry& a, const SongEntry& b) { return a.folder < b.folder; };
    std::sort(songs.begin(), songs.end(), byFolder);
    std::sort(tutorials.begin(), tutorials.end(), byFolder);

    std::vector<uint8_t> bytes;
    TextTable t;
    const std::string table = "PSP_GAME/USRDIR/TEXT/STR_" + language + "_MUSIC.TXT";
    if (umd.read(table, bytes) && t.parse(bytes, nullptr)) {
        const size_t triples = (t.strings.size() - 1) / 3;
        if (triples >= songs.size() + tutorials.size()) {
            auto name = [&](SongEntry& s, size_t i) {
                s.title = t.strings[1 + 3 * i], s.artist = t.strings[2 + 3 * i], s.album = t.strings[3 + 3 * i];
            };
            for (size_t i = 0; i < songs.size(); ++i) name(songs[i], i);
            for (size_t i = 0; i < tutorials.size(); ++i) name(tutorials[i], songs.size() + i);
        } else {
            SDL_Log("gd24: %s has %zu names for %zu songs: folder names shown", table.c_str(), triples,
                    songs.size() + tutorials.size());
        }
    }
    songs.insert(songs.end(), tutorials.begin(), tutorials.end());
    return songs;
}

}  // namespace gd24
