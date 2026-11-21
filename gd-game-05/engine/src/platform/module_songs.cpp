// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd_game_songs: the launcher's "Hear the songs" screen (gd_host.h). gd05
// lists the disc's songs (config/gen/songs.dtb via loadSongs) and decodes a
// short preview of one on demand into interleaved stereo float PCM, with the
// game's own .mogg / Ogg Vorbis loader (VorbisStream) downmixed to stereo
// exactly as the mixer plays it (per-channel pan and dB volume, mixerTracks in
// app.cpp). No window or GL work; called on the launcher's thread.
#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "audio/vorbis_stream.h"
#include "data/game_data.h"
#include "game/song_db.h"
#include "gd_host.h"

namespace {

struct SongsImpl {
    std::unique_ptr<GameData> data;
    std::vector<SongInfo> songs;  // songs with chart + audio on the disc
};

int songCount(void* self) {
    auto* impl = static_cast<SongsImpl*>(self);
    return int(impl->songs.size());
}

int song(void* self, int index, GdSong* out) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (!out || index < 0 || index >= int(impl->songs.size())) return 1;
    const SongInfo& s = impl->songs[size_t(index)];
    out->id = s.id.c_str();
    out->title = s.name.empty() ? s.id.c_str() : s.name.c_str();
    out->artist = s.artist.c_str();
    out->detail = "";
    return 0;
}

int decodePreview(void* self, int index, GdSongPcm* out) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (!out || !out->samples || out->capacity < 2 || index < 0 || index >= int(impl->songs.size())) return 1;
    const SongInfo& s = impl->songs[size_t(index)];
    const SongAudio& a = s.audio;
    out->frames = 0;
    try {
        auto stream = std::make_unique<VorbisStream>(impl->data->read(a.moggPath));
        const int chans = stream->channels();
        if (chans < 1) return 2;
        out->rate = stream->sampleRate();

        // Where to start: the launcher's offset, else the song's preview start.
        double startMs = out->start_seconds > 0.0f ? double(out->start_seconds) * 1000.0 : double(s.previewStartMs);
        if (startMs > 0.0)
            stream->seek(uint64_t(startMs * out->rate / 1000.0));

        // Per-channel stereo gains, as the mixer plays the song (app.cpp
        // mixerTracks): pan -1..1 (linear pan law), vols dB -> gain.
        std::vector<float> gl(size_t(chans), 0.0f), gr(size_t(chans), 0.0f);
        for (int c = 0; c < chans; ++c) {
            const float pan = size_t(c) < a.pans.size() ? a.pans[size_t(c)] : 0.0f;
            const float vol = size_t(c) < a.vols.size() ? std::pow(10.0f, a.vols[size_t(c)] / 20.0f) : 1.0f;
            gl[size_t(c)] = vol * std::clamp(1.0f - pan, 0.0f, 1.0f);
            gr[size_t(c)] = vol * std::clamp(1.0f + pan, 0.0f, 1.0f);
        }

        const int capFrames = out->capacity / 2;
        constexpr int kBlock = 2048;
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
                for (int c = 0; c < chans; ++c) {
                    const float in = buf[size_t(c)][size_t(f)];
                    l += in * gl[size_t(c)];
                    r += in * gr[size_t(c)];
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

// Skip the disc's debug / tutorial / credits entries, not real set-list songs.
// Rock Band marks its trainers / test tracks with no rank (SongInfo::tutorial).
bool isRealSong(const SongInfo& s) {
    if (s.tutorial) return false;
    auto has = [](const std::string& h, const char* n) {
        std::string lo = h;
        std::transform(lo.begin(), lo.end(), lo.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return lo.find(n) != std::string::npos;
    };
    for (const char* bad : {"tutorial", "credits", "synctest", "practice_", "debug", "placeholder"})
        if (has(s.id, bad)) return false;
    return !s.id.empty();
}

GdSongs g_songs;

}  // namespace

extern "C" GD_MODULE_EXPORT const GdSongs* gd_game_songs(const GdHost* host) {
    if (!host || host->argc < 1) return nullptr;
    auto impl = std::make_unique<SongsImpl>();
    try {
        impl->data = std::make_unique<GameData>(host->argv[host->argc - 1]);
        for (SongInfo& s : loadSongs(*impl->data))
            if (isRealSong(s)) impl->songs.push_back(std::move(s));
    } catch (const std::exception&) {
        return nullptr;
    }
    if (impl->songs.empty()) return nullptr;

    g_songs.size = sizeof(GdSongs);
    g_songs.self = impl.release();
    g_songs.song_count = songCount;
    g_songs.song = song;
    g_songs.decode_preview = decodePreview;
    g_songs.close = closeSongs;
    if (host->log) host->log(host->log_user, "gd05: song previews ready");
    return &g_songs;
}
