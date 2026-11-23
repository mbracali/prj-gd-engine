// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd_game_songs: the launcher's "Hear the songs" screen (gd_host.h). gd01
// lists the disc's songs (config/songs.dta via loadSongs) and decodes a short
// preview of one on demand into interleaved stereo float PCM, with the game's
// own PS2 .vgs loader (VgsStream: PS-ADPCM) downmixed to stereo exactly as the
// mixer plays it (per-channel pan and linear volume, App::startPreview). No
// window or GL work; called on the launcher's thread.
#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "data/game_data.h"
#include "data/vgs.h"
#include "game/song_db.h"
#include "gd_host.h"

namespace {

// One stem the player can mute/solo. GH1 has no per-instrument track map, but
// `cores` marks the player's guitar channels, so a song splits into Guitar and
// the Backing mix (the band + vocals).
struct Part {
    std::string name;
    std::vector<int> channels;
};

std::vector<Part> partsOf(const SongInfo& s) {
    const int nch = int(std::max(s.pans.size(), s.vols.size()));
    Part guitar, backing;
    guitar.name = "Guitar";
    backing.name = "Backing";
    for (int c = 0; c < nch; ++c) {
        const bool isGuitar = size_t(c) < s.cores.size() && s.cores[size_t(c)] == 1;
        (isGuitar ? guitar : backing).channels.push_back(c);
    }
    std::vector<Part> parts;
    if (!guitar.channels.empty()) parts.push_back(std::move(guitar));
    if (!backing.channels.empty()) parts.push_back(std::move(backing));
    return parts;
}

// A streaming full song: its VGS decoder, per-channel gains, the parts and which
// are on. read() mixes the on parts' channels to stereo, stopping at the length.
struct Playback {
    int index = -1;
    std::unique_ptr<VgsStream> stream;
    int nch = 0, rate = 0;
    uint64_t pos = 0, len = 0;
    std::vector<float> gl, gr;
    std::vector<int> chPart;
    std::vector<Part> parts;
    std::vector<unsigned char> on;  // per-part enabled
};

struct SongsImpl {
    std::unique_ptr<GameData> data;
    std::vector<SongInfo> songs;  // songs with chart + audio on the disc
    Playback pb;                   // the one streaming song (Songs section)
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
    out->frames = 0;
    try {
        auto vgs = std::make_unique<VgsStream>(impl->data->read(s.audioPath));
        const int chans = vgs->channels();
        if (chans < 1) return 2;
        out->rate = vgs->sampleRate();

        // Where to start: the launcher's offset, else the song's preview start.
        double startMs = out->start_seconds > 0.0f ? double(out->start_seconds) * 1000.0 : double(s.previewStartMs);
        uint64_t startFrame = startMs > 0.0 ? uint64_t(startMs * out->rate / 1000.0) : 0;
        const uint64_t len = vgs->lengthFrames();
        if (len && startFrame >= len) startFrame = 0;
        if (startFrame) vgs->seek(startFrame);

        // Per-channel stereo gains, as the mixer plays the song (App's preview
        // tracks): pan -1..1 (linear pan law), vols are linear gains.
        std::vector<float> gl(size_t(chans), 1.0f), gr(size_t(chans), 1.0f);
        for (int c = 0; c < chans; ++c) {
            const float pan = size_t(c) < s.pans.size() ? s.pans[size_t(c)] : 0.0f;
            const float vol = size_t(c) < s.vols.size() ? s.vols[size_t(c)] : 1.0f;
            gl[size_t(c)] = vol * std::clamp(1.0f - pan, 0.0f, 1.0f);
            gr[size_t(c)] = vol * std::clamp(1.0f + pan, 0.0f, 1.0f);
        }

        int capFrames = out->capacity / 2;
        // VgsStream::decode pads past the end with silence; stop at the song's
        // length so the preview is the music, not a tail of silence.
        if (len) capFrames = int(std::min<uint64_t>(uint64_t(capFrames), len - startFrame));
        if (capFrames <= 0) return 3;

        constexpr int kBlock = 2048;
        const size_t nch = size_t(chans);
        std::vector<std::vector<float>> buf(nch, std::vector<float>(size_t(kBlock)));
        std::vector<float*> ptrs(nch);
        for (int c = 0; c < chans; ++c) ptrs[size_t(c)] = buf[size_t(c)].data();

        int written = 0;
        while (written < capFrames) {
            const int take = std::min(kBlock, capFrames - written);
            vgs->decode(ptrs.data(), take);
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

// ---- Full-song streaming with per-part (stem) control ----

int partCount(void* self, int index) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (index < 0 || index >= int(impl->songs.size())) return 0;
    return int(partsOf(impl->songs[size_t(index)]).size());
}

int part(void* self, int index, int part, GdSongPart* out) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (!out || index < 0 || index >= int(impl->songs.size())) return 1;
    const std::vector<Part> parts = partsOf(impl->songs[size_t(index)]);
    if (part < 0 || part >= int(parts.size())) return 1;
    static thread_local std::string name;
    name = parts[size_t(part)].name;
    out->name = name.c_str();
    out->default_on = 1;
    return 0;
}

int playSong(void* self, int index) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (index < 0 || index >= int(impl->songs.size())) return 1;
    const SongInfo& s = impl->songs[size_t(index)];
    try {
        Playback pb;
        pb.index = index;
        pb.stream = std::make_unique<VgsStream>(impl->data->read(s.audioPath));
        pb.nch = pb.stream->channels();
        if (pb.nch < 1) return 2;
        pb.rate = pb.stream->sampleRate();
        pb.len = pb.stream->lengthFrames();
        pb.pos = 0;
        pb.gl.assign(size_t(pb.nch), 1.0f);
        pb.gr.assign(size_t(pb.nch), 1.0f);
        for (int c = 0; c < pb.nch; ++c) {
            const float pan = size_t(c) < s.pans.size() ? s.pans[size_t(c)] : 0.0f;
            const float vol = size_t(c) < s.vols.size() ? s.vols[size_t(c)] : 1.0f;
            pb.gl[size_t(c)] = vol * std::clamp(1.0f - pan, 0.0f, 1.0f);
            pb.gr[size_t(c)] = vol * std::clamp(1.0f + pan, 0.0f, 1.0f);
        }
        pb.parts = partsOf(s);
        pb.on.assign(pb.parts.size(), 1);
        pb.chPart.assign(size_t(pb.nch), -1);
        for (size_t p = 0; p < pb.parts.size(); ++p)
            for (int c : pb.parts[p].channels)
                if (c >= 0 && c < pb.nch) pb.chPart[size_t(c)] = int(p);
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
    const size_t nz = size_t(nch);
    // VgsStream pads silence past the end, so clamp to the remaining length.
    int cap = capacityFrames;
    if (pb.len) {
        const uint64_t remain = pb.len > pb.pos ? pb.len - pb.pos : 0;
        cap = int(std::min<uint64_t>(uint64_t(capacityFrames), remain));
    }
    if (cap <= 0) return 0;
    constexpr int kBlock = 2048;
    std::vector<std::vector<float>> buf(nz, std::vector<float>(size_t(kBlock)));
    std::vector<float*> ptrs(nz);
    for (int c = 0; c < nch; ++c) ptrs[size_t(c)] = buf[size_t(c)].data();
    int written = 0;
    while (written < cap) {
        const int take = std::min(kBlock, cap - written);
        pb.stream->decode(ptrs.data(), take);
        for (int f = 0; f < take; ++f) {
            float l = 0.0f, r = 0.0f;
            for (int c = 0; c < nch; ++c) {
                const int p = pb.chPart[size_t(c)];
                if (p >= 0 && !pb.on[size_t(p)]) continue;
                const float in = buf[size_t(c)][size_t(f)];
                l += in * pb.gl[size_t(c)];
                r += in * pb.gr[size_t(c)];
            }
            samples[size_t((written + f) * 2)] = std::clamp(l, -1.0f, 1.0f);
            samples[size_t((written + f) * 2 + 1)] = std::clamp(r, -1.0f, 1.0f);
        }
        written += take;
    }
    pb.pos += uint64_t(written);
    return written;
}

void setPart(void* self, int part, int enabled) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (part >= 0 && part < int(impl->pb.on.size())) impl->pb.on[size_t(part)] = enabled ? 1 : 0;
}

void stopSong(void* self) {
    static_cast<SongsImpl*>(self)->pb = Playback{};
}

int songFrames(void* self) {
    return int(static_cast<SongsImpl*>(self)->pb.len);
}

void closeSongs(void* self) {
    delete static_cast<SongsImpl*>(self);
}

// Skip the disc's debug / tutorial / placeholder entries, not real set-list songs.
bool isRealSong(const SongInfo& s) {
    auto has = [](const std::string& h, const char* n) {
        std::string lo = h;
        std::transform(lo.begin(), lo.end(), lo.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return lo.find(n) != std::string::npos;
    };
    for (const char* bad : {"tutorial", "credits", "synctest", "practice_", "debug", "placeholder"})
        if (has(s.id, bad)) return false;
    return !s.id.empty() && !s.audioPath.empty();
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
    g_songs.part_count = partCount;
    g_songs.part = part;
    g_songs.play_song = playSong;
    g_songs.read_song = readSong;
    g_songs.set_part = setPart;
    g_songs.stop_song = stopSong;
    g_songs.song_frames = songFrames;
    if (host->log) host->log(host->log_user, "gd01: song previews ready");
    return &g_songs;
}
