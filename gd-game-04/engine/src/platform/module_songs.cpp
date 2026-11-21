// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd_game_songs: the launcher's "Hear the songs" screen (gd_host.h). gd04
// lists the disc's songs (scripts\guitar\songlist.qb via SongDb) and decodes a
// short preview of one on demand into interleaved stereo float PCM, with the
// game's own FSB/XMA loaders (SongStream): the three stems (guitar, rhythm,
// song) summed, as the song sounds when played. No window or GL work; called
// on the launcher's thread.
#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "audio/song_stream.h"
#include "data/disc.h"
#include "game/song_db.h"
#include "gd_host.h"
#include "script/globals.h"

namespace {

struct SongsImpl {
    std::unique_ptr<Disc> disc;
    std::unique_ptr<Globals> globals;
    SongDb db;
    std::vector<const SongInfo*> list;  // songs that have audio on the disc
    std::vector<std::string> details;   // stable storage for GdSong::detail
};

int songCount(void* self) {
    auto* impl = static_cast<SongsImpl*>(self);
    return int(impl->list.size());
}

int song(void* self, int index, GdSong* out) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (!out || index < 0 || index >= int(impl->list.size())) return 1;
    const SongInfo& s = *impl->list[size_t(index)];
    out->id = s.name.c_str();
    out->title = s.title.empty() ? s.name.c_str() : s.title.c_str();
    out->artist = s.artist.c_str();
    out->detail = impl->details[size_t(index)].c_str();
    return 0;
}

int decodePreview(void* self, int index, GdSongPcm* out) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (!out || !out->samples || out->capacity < 2 || index < 0 || index >= int(impl->list.size())) return 1;
    const SongInfo& s = *impl->list[size_t(index)];
    out->frames = 0;
    try {
        std::string error;
        auto stream = SongStream::open(*impl->disc, s.name, error);
        if (!stream) return 2;
        out->rate = stream->sampleRate();
        if (out->start_seconds > 0.0f) stream->seek(double(out->start_seconds) * 1000.0);

        const int capFrames = out->capacity / 2;
        constexpr int kBlock = 2048;
        const int chans = stream->channels();  // 6: guitar L R, rhythm L R, song L R
        const size_t nch = size_t(chans);
        std::vector<std::vector<float>> buf(nch, std::vector<float>(size_t(kBlock)));
        std::vector<float*> ptrs(nch);
        for (int c = 0; c < chans; ++c) ptrs[size_t(c)] = buf[size_t(c)].data();

        int written = 0;
        while (written < capFrames) {
            const int want = std::min(kBlock, capFrames - written);
            const int n = stream->decode(ptrs.data(), want);
            if (n <= 0) break;
            const int take = std::min(n, capFrames - written);
            for (int f = 0; f < take; ++f) {
                float l = 0.0f, r = 0.0f;
                for (int st = 0; st * 2 + 1 < chans; ++st) {
                    l += buf[size_t(st * 2)][size_t(f)];
                    r += buf[size_t(st * 2 + 1)][size_t(f)];
                }
                out->samples[size_t((written + f) * 2)] = std::clamp(l, -1.0f, 1.0f);
                out->samples[size_t((written + f) * 2 + 1)] = std::clamp(r, -1.0f, 1.0f);
            }
            written += take;
        }
        out->frames = written;
        return written > 0 ? 0 : 3;
    } catch (const std::exception&) {
        return 4;
    }
}

void closeSongs(void* self) {
    delete static_cast<SongsImpl*>(self);
}

GdSongs g_songs;

}  // namespace

extern "C" GD_MODULE_EXPORT const GdSongs* gd_game_songs(const GdHost* host) {
    if (!host || host->argc < 1) return nullptr;
    auto impl = std::make_unique<SongsImpl>();
    try {
        std::string error;
        impl->disc = Disc::open(host->argv[host->argc - 1], error);
        if (!impl->disc) return nullptr;
        impl->disc->loadNames();
        impl->globals = std::make_unique<Globals>();
        impl->globals->load(*impl->disc);
        if (!impl->db.load(*impl->globals)) return nullptr;
        for (const SongInfo& s : impl->db.songs()) {
            if (s.name.empty()) continue;
            // Skip the disc's debug / tutorial / placeholder entries (not real
            // songs the player hears in the set list).
            if (s.title.rfind("DEBUG", 0) == 0 || s.title.rfind("Placeholder", 0) == 0 ||
                s.title.rfind("Tutorial", 0) == 0 || s.name.rfind("Tutorial_", 0) == 0 ||
                s.name.rfind("synctest", 0) == 0 || s.name == "TimRappTest")
                continue;
            if (!impl->disc->exists("DATA/MUSIC/" + s.name + ".fsb.xen")) continue;
            impl->list.push_back(&s);
            // GH3 stores "year" with a leading ", " (it renders "Artist, 1976").
            std::string detail = s.year;
            size_t nb = detail.find_first_not_of(", ");
            if (nb != std::string::npos) detail = detail.substr(nb);
            if (!s.originalArtist && !detail.empty()) detail += " (as made famous)";
            impl->details.push_back(detail);
        }
    } catch (const std::exception&) {
        return nullptr;
    }
    if (impl->list.empty()) return nullptr;

    g_songs.size = sizeof(GdSongs);
    g_songs.self = impl.release();
    g_songs.song_count = songCount;
    g_songs.song = song;
    g_songs.decode_preview = decodePreview;
    g_songs.close = closeSongs;
    if (host->log) host->log(host->log_user, "gd04: song previews ready");
    return &g_songs;
}
