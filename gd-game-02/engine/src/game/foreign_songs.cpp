// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/foreign_songs.h"

#include <algorithm>

#include "audio/audio_source.h"
#include "data/midi.h"
#include "gd/core/gh1/reader.h"

namespace {

// Wraps a gd-engine-core GH1 audio stream as a GH2 mixer AudioSource. The VGS
// stream pads silence past the end, so we clamp to the real length and report
// the frames actually produced, which the mixer uses to know the song ended.
struct ForeignAudioSource : AudioSource {
    std::unique_ptr<gd::core::gh1::AudioStream> stream;
    uint64_t pos = 0, len = 0;
    explicit ForeignAudioSource(std::unique_ptr<gd::core::gh1::AudioStream> s) : stream(std::move(s)), len(stream->lengthFrames()) {
        stream->seek(0);
    }
    int channels() const override { return stream->channels(); }
    int sampleRate() const override { return stream->sampleRate(); }
    int decode(float* const* channelBuffers, int frames) override {
        const uint64_t remain = pos < len ? len - pos : 0;
        const int n = int(std::min<uint64_t>(uint64_t(frames), remain));
        if (n <= 0) return 0;
        stream->decode(channelBuffers, n);
        pos += uint64_t(n);
        return n;
    }
    bool seek(uint64_t frame) override {
        stream->seek(frame);
        pos = frame;
        return true;
    }
};

}  // namespace

std::vector<SongInfo> openForeignSongs(const std::vector<std::pair<std::string, std::string>>& extra, ForeignLibrary& lib) {
    std::vector<SongInfo> out;
    for (const auto& [gameId, path] : extra) {
        if (gameId != "gd-game-01") continue;  // Guitar Hero 1 (wave 2); other sources follow
        auto disc = gd::core::gh1::Disc::open(path);
        if (!disc) continue;
        const int discIdx = int(lib.discs.size());
        const auto& songs = disc->songs();
        for (int j = 0; j < int(songs.size()); ++j) {
            const gd::core::gh1::SongMeta& m = songs[size_t(j)];
            SongInfo info;
            info.id = "gh1_" + m.id;  // a namespace so it never collides with a GH2 id
            info.name = m.name;
            info.artist = m.artist;
            info.animTempo = m.animTempo;
            info.previewStartMs = m.previewStartMs;
            info.previewEndMs = m.previewEndMs;
            info.foreignDisc = discIdx;
            info.foreignSong = j;
            info.foreignLabel = "Guitar Hero";
            // Audio channel layout (the chart and audio come from the reader).
            info.audio.pans = m.pans;
            info.audio.vols = m.vols;   // dB, as GH2's own songs
            info.audio.cores = m.cores;  // 1 = guitar channel (muted on a miss)
            out.push_back(std::move(info));
        }
        lib.discs.push_back(std::move(disc));
    }
    return out;
}

Song loadForeignChart(const ForeignLibrary& lib, const SongInfo& info) {
    Song song;
    if (info.foreignDisc < 0 || info.foreignDisc >= int(lib.discs.size())) return song;
    const gd::core::gh1::Disc& disc = *lib.discs[size_t(info.foreignDisc)];
    const gd::core::gh1::NeutralChart nc = disc.loadChart(info.foreignSong);
    // A tempo map from the GH1 MIDI, with GH2's own parser (gems already carry
    // absolute ms; this is for beat lines and measures). Best effort.
    try {
        const std::vector<uint8_t> midi = disc.rawMidi(info.foreignSong);
        if (!midi.empty()) song.tempo.build(parseMidi(midi));
    } catch (...) {
    }
    const size_t gp = size_t(Part::Guitar);
    for (size_t d = 0; d < nc.difficulties.size() && d < size_t(kDifficultyCount); ++d) {
        Chart& c = song.charts[gp][d];
        c.gems.reserve(nc.difficulties[d].gems.size());
        for (const gd::core::gh1::NeutralGem& g : nc.difficulties[d].gems)
            c.gems.push_back({g.ms, g.tick, g.durationMs, g.durationTicks, g.frets, g.hopo, g.noSustain, g.players, g.phrase});
        for (const gd::core::gh1::NeutralPhrase& p : nc.difficulties[d].phrases)
            c.phrases.push_back({p.startTick, p.endTick, p.firstGem, p.lastGem});
    }
    song.hasPart[gp] = true;
    for (const gd::core::gh1::NeutralBeat& b : nc.beats) song.beats.push_back({b.ms, b.measure});
    song.endMs = nc.endMs;
    return song;
}

std::unique_ptr<AudioSource> loadForeignAudio(const ForeignLibrary& lib, const SongInfo& info) {
    if (info.foreignDisc < 0 || info.foreignDisc >= int(lib.discs.size())) return nullptr;
    auto stream = lib.discs[size_t(info.foreignDisc)]->openAudio(info.foreignSong);
    if (!stream) return nullptr;
    return std::make_unique<ForeignAudioSource>(std::move(stream));
}
