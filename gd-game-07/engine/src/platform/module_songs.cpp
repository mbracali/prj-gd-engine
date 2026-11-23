// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd_game_songs: the launcher's "Hear the songs" screen (gd_host.h). gd07
// lists the disc's songs (scripts\songlist.qb via SongDb) and decodes a short
// preview of one on demand into interleaved stereo float PCM, with the game's
// own FSB/XMA loaders: gh4's own set-list preview clip
// (DATA/MUSIC/<song>_preview.fsb.xen via SongStream::openPreview, ~21 s),
// falling back to the full song when a preview is missing. The stems are
// summed, as the song sounds when played. No window or GL work; called on the
// launcher's thread.
#include <algorithm>
#include <array>
#include <cctype>
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

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// A disc debug / tutorial / placeholder entry, not a real set-list song.
bool isDebugEntry(const SongInfo& s) {
    const std::string t = upper(s.title);
    const std::string n = upper(s.name);
    const char* marks[] = {"DEBUG", "PLACEHOLDER", "PLACE-HOLDER", "TUTORIAL", "SYNCTEST", "TIMRAPP", "TENSECONDDEBUG"};
    for (const char* m : marks)
        if (t.find(m) != std::string::npos || n.find(m) != std::string::npos) return true;
    return false;
}

// One stem the launcher can mute/solo: SongStream exposes three (guitar, rhythm,
// song/backing) as six channels (two each), summed as the song is heard.
struct Playback {
    int index = -1;
    std::unique_ptr<SongStream> stream;
    int nch = 0, rate = 0, nStems = 0;
    std::array<unsigned char, 3> on = {{1, 1, 1}};  // guitar, rhythm, backing
};

const char* const kStemNames[3] = {"Guitar", "Rhythm", "Backing"};

struct SongsImpl {
    std::unique_ptr<Disc> disc;
    std::unique_ptr<Globals> globals;
    SongDb db;
    std::vector<const SongInfo*> list;  // songs that have audio on the disc
    std::vector<std::string> details;   // stable storage for GdSong::detail
    Playback pb;  // the streaming song (stem player)
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
        auto stream = SongStream::openPreview(*impl->disc, s.name, error);
        if (!stream) stream = SongStream::open(*impl->disc, s.name, error);
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

int partCount(void* self, int index) {
    auto* impl = static_cast<SongsImpl*>(self);
    return (index >= 0 && index < int(impl->list.size())) ? 3 : 0;  // guitar, rhythm, backing
}

int part(void* self, int index, int part, GdSongPart* out) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (!out || index < 0 || index >= int(impl->list.size()) || part < 0 || part >= 3) return 1;
    out->name = kStemNames[part];
    out->default_on = 1;
    return 0;
}

int playSong(void* self, int index) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (index < 0 || index >= int(impl->list.size())) return 1;
    try {
        std::string error;
        Playback pb;
        pb.index = index;
        pb.stream = SongStream::open(*impl->disc, impl->list[size_t(index)]->name, error);
        if (!pb.stream) return 2;
        pb.nch = pb.stream->channels();
        pb.rate = pb.stream->sampleRate();
        pb.nStems = pb.nch / 2;
        impl->pb = std::move(pb);
        return 0;
    } catch (const std::exception&) {
        return 3;
    }
}

int readSong(void* self, float* samples, int capacityFrames, int* rate) {
    auto* impl = static_cast<SongsImpl*>(self);
    Playback& pb = impl->pb;
    if (!samples || capacityFrames < 1 || !pb.stream) return 0;
    if (rate) *rate = pb.rate;
    const int nch = pb.nch;
    constexpr int kBlock = 2048;
    const size_t nz = size_t(nch);
    std::vector<std::vector<float>> buf(nz, std::vector<float>(size_t(kBlock)));
    std::vector<float*> ptrs(nz);
    for (int c = 0; c < nch; ++c) ptrs[size_t(c)] = buf[size_t(c)].data();
    int written = 0;
    while (written < capacityFrames) {
        const int want = std::min(kBlock, capacityFrames - written);
        const int n = pb.stream->decode(ptrs.data(), want);
        if (n <= 0) break;
        for (int f = 0; f < n; ++f) {
            float l = 0.0f, r = 0.0f;
            for (int st = 0; st < pb.nStems; ++st) {
                if (st < 3 && !pb.on[size_t(st)]) continue;  // a muted stem
                l += buf[size_t(st * 2)][size_t(f)];
                r += buf[size_t(st * 2 + 1)][size_t(f)];
            }
            samples[size_t((written + f) * 2)] = std::clamp(l, -1.0f, 1.0f);
            samples[size_t((written + f) * 2 + 1)] = std::clamp(r, -1.0f, 1.0f);
        }
        written += n;
    }
    return written;
}

void setPart(void* self, int part, int enabled) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (part >= 0 && part < 3) impl->pb.on[size_t(part)] = enabled ? 1 : 0;
}

void stopSong(void* self) {
    static_cast<SongsImpl*>(self)->pb = Playback{};
}

int songFrames(void* self) {
    const Playback& pb = static_cast<SongsImpl*>(self)->pb;
    return pb.stream ? int(pb.stream->length()) : 0;
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
            if (s.name.empty() || isDebugEntry(s)) continue;
            // Keep only songs with audio on the disc (a preview or the song bank).
            const bool hasPreview = impl->disc->exists("DATA/MUSIC/" + s.name + "_preview.fsb.xen");
            const bool hasSong = impl->disc->exists("DATA/MUSIC/" + s.name + ".fsb.xen") ||
                                 impl->disc->exists("DATA/MUSIC/" + s.name + "_1.fsb.xen");
            if (!hasPreview && !hasSong) continue;
            impl->list.push_back(&s);
            std::string detail = s.year;
            size_t nb = detail.find_first_not_of(", ");
            detail = nb == std::string::npos ? std::string() : detail.substr(nb);
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
    g_songs.part_count = partCount;
    g_songs.part = part;
    g_songs.play_song = playSong;
    g_songs.read_song = readSong;
    g_songs.set_part = setPart;
    g_songs.stop_song = stopSong;
    g_songs.song_frames = songFrames;
    if (host->log) host->log(host->log_user, "gd07: song previews ready");
    return &g_songs;
}
