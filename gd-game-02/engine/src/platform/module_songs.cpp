// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd_game_songs: the launcher's "Hear the songs" screen (gd_host.h). gd02
// lists the disc's songs (config/gen/songs.dtb via loadSongs) and decodes a
// short preview of one on demand into interleaved stereo float PCM, with the
// game's own .mogg / Ogg Vorbis loader (VorbisStream) downmixed to stereo
// exactly as the mixer plays it (per-channel pan and dB volume, mixerTracks in
// app.cpp). No window or GL work; called on the launcher's thread.
#include <algorithm>
#include <cctype>
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

// One stem the player can mute/solo: an instrument's channels in the .mogg.
struct Part {
    std::string name;
    std::vector<int> channels;
};

// A capitalised, friendly name for a track instrument.
std::string prettyName(const std::string& inst) {
    static const std::vector<std::pair<const char*, const char*>> kNames = {
        {"guitar", "Guitar"},  {"bass", "Bass"},    {"drum", "Drums"},  {"vocal", "Vocals"},
        {"singer", "Vocals"},  {"keys", "Keys"},    {"key", "Keys"},    {"rhythm", "Rhythm"},
        {"crowd", "Crowd"},    {"song", "Backing"}, {"backing", "Backing"}};
    std::string lo = inst;
    std::transform(lo.begin(), lo.end(), lo.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const auto& [key, name] : kNames)
        if (lo.find(key) != std::string::npos) return name;
    if (!inst.empty()) { std::string s = inst; s[0] = char(std::toupper((unsigned char)s[0])); return s; }
    return "Part";
}

// The song's parts: each instrument track's channels, then a "Song" part for the
// channels no instrument claims (the backing mix). A nice fixed instrument order.
std::vector<Part> partsOf(const SongInfo& s) {
    const SongAudio& a = s.audio;
    const int nch = int(std::max(a.pans.size(), a.vols.size()));
    std::vector<bool> used(size_t(std::max(0, nch)), false);
    std::vector<Part> parts;
    static const char* const kOrder[] = {"guitar", "rhythm", "bass", "drum", "vocal", "singer", "keys", "key"};
    auto addTrack = [&](const std::string& inst, const std::vector<int>& chs) {
        Part p;
        p.name = prettyName(inst);
        for (int c : chs)
            if (c >= 0 && c < nch && !used[size_t(c)]) { p.channels.push_back(c); used[size_t(c)] = true; }
        if (!p.channels.empty()) {
            for (Part& ex : parts)  // merge instruments that map to the same friendly name
                if (ex.name == p.name) { ex.channels.insert(ex.channels.end(), p.channels.begin(), p.channels.end()); return; }
            parts.push_back(std::move(p));
        }
    };
    for (const char* want : kOrder)
        for (const auto& [inst, chs] : a.tracks) {
            std::string lo = inst;
            std::transform(lo.begin(), lo.end(), lo.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (lo.find(want) != std::string::npos) addTrack(inst, chs);
        }
    for (const auto& [inst, chs] : a.tracks) addTrack(inst, chs);  // any instrument not in the order
    Part song;
    song.name = "Backing";
    for (int c = 0; c < nch; ++c)
        if (!used[size_t(c)]) song.channels.push_back(c);
    if (!song.channels.empty()) {
        for (Part& ex : parts)
            if (ex.name == "Backing") { ex.channels.insert(ex.channels.end(), song.channels.begin(), song.channels.end()); song.channels.clear(); break; }
        if (!song.channels.empty()) parts.push_back(std::move(song));
    }
    return parts;
}

// A streaming full song: its decoder, per-channel stereo gains, the parts and
// which are on. read() mixes the on parts' channels to stereo.
struct Playback {
    int index = -1;
    std::unique_ptr<VorbisStream> stream;
    int nch = 0, rate = 0;
    std::vector<float> gl, gr;  // per-channel pan/vol gains
    std::vector<int> chPart;    // per-channel -> part index
    std::vector<Part> parts;
    std::vector<unsigned char> on;  // per-part enabled (vector<bool> has no data())
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
    static thread_local std::string name;  // keep the c_str() alive past the call
    name = parts[size_t(part)].name;
    out->name = name.c_str();
    out->default_on = 1;  // the full mix is on at the start
    return 0;
}

int playSong(void* self, int index) {
    auto* impl = static_cast<SongsImpl*>(self);
    if (index < 0 || index >= int(impl->songs.size())) return 1;
    const SongInfo& s = impl->songs[size_t(index)];
    const SongAudio& a = s.audio;
    try {
        Playback pb;
        pb.index = index;
        pb.stream = std::make_unique<VorbisStream>(impl->data->read(a.moggPath));
        pb.nch = pb.stream->channels();
        if (pb.nch < 1) return 2;
        pb.rate = pb.stream->sampleRate();
        pb.gl.assign(size_t(pb.nch), 0.0f);
        pb.gr.assign(size_t(pb.nch), 0.0f);
        for (int c = 0; c < pb.nch; ++c) {
            const float pan = size_t(c) < a.pans.size() ? a.pans[size_t(c)] : 0.0f;
            const float vol = size_t(c) < a.vols.size() ? std::pow(10.0f, a.vols[size_t(c)] / 20.0f) : 1.0f;
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
    constexpr int kBlock = 2048;
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
            for (int c = 0; c < nch; ++c) {
                const int p = pb.chPart[size_t(c)];
                if (p >= 0 && !pb.on[size_t(p)]) continue;  // a muted part
                const float in = buf[size_t(c)][size_t(f)];
                l += in * pb.gl[size_t(c)];
                r += in * pb.gr[size_t(c)];
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
    if (part >= 0 && part < int(impl->pb.on.size())) impl->pb.on[size_t(part)] = enabled ? 1 : 0;
}

void stopSong(void* self) {
    static_cast<SongsImpl*>(self)->pb = Playback{};
}

int songFrames(void* self) {
    const Playback& pb = static_cast<SongsImpl*>(self)->pb;
    return pb.stream ? int(pb.stream->lengthFrames()) : 0;
}

void closeSongs(void* self) {
    delete static_cast<SongsImpl*>(self);
}

// Skip the disc's debug / tutorial / credits entries, not real set-list songs.
bool isRealSong(const SongInfo& s) {
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
    g_songs.part_count = partCount;
    g_songs.part = part;
    g_songs.play_song = playSong;
    g_songs.read_song = readSong;
    g_songs.set_part = setPart;
    g_songs.stop_song = stopSong;
    g_songs.song_frames = songFrames;
    if (host->log) host->log(host->log_user, "gd02: song previews ready");
    return &g_songs;
}
