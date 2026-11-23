// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The GH1 song reader façade (gd/core/gh1/reader.h): opens a GH1 disc, lists its
// real songs, and hands a host each song's chart, raw MIDI and VGS audio. Built
// on the GH1 readers promoted into this area (data/*, game/*).
#include "gd/core/gh1/reader.h"

#include <string>

#include "data/game_data.h"
#include "data/midi.h"
#include "data/vgs.h"
#include "game/chart.h"
#include "game/song_db.h"

namespace gd::core::gh1 {

namespace {
// Drop non-songs the way gd01's song-preview path does (module_songs.cpp).
bool isRealSong(const SongInfo& s) {
    static const char* kSkip[] = {"tutorial", "credits", "synctest", "practice_", "debug", "placeholder"};
    for (const char* k : kSkip)
        if (s.id.find(k) != std::string::npos) return false;
    return true;
}

struct VgsAudioStream : AudioStream {
    VgsStream vgs;
    explicit VgsAudioStream(Bytes data) : vgs(std::move(data)) {}
    int channels() const override { return vgs.channels(); }
    int sampleRate() const override { return vgs.sampleRate(); }
    uint64_t lengthFrames() const override { return vgs.lengthFrames(); }
    void decode(float* const* out, int frames) override { vgs.decode(out, frames); }
    void seek(uint64_t frame) override { vgs.seek(frame); }
};
}  // namespace

struct Disc::Impl {
    std::unique_ptr<GameData> data;
    std::vector<SongInfo> infos;   // the kept GH1 songs (same order as meta)
    std::vector<SongMeta> meta;
};

Disc::Disc(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Disc::~Disc() = default;

std::unique_ptr<Disc> Disc::open(const std::string& discPath) {
    auto impl = std::make_unique<Impl>();
    try {
        impl->data = std::make_unique<GameData>(discPath);
        for (SongInfo& s : loadSongs(*impl->data)) {
            if (!isRealSong(s)) continue;
            SongMeta m;
            m.id = s.id;
            m.name = s.name;
            m.artist = s.artist;
            m.venue = s.venue;
            m.character = s.character;
            m.guitar = s.guitar;
            m.animTempo = s.animTempo;
            m.pans = s.pans;
            m.vols = s.vols;
            m.cores = s.cores;
            m.previewStartMs = s.previewStartMs;
            m.previewEndMs = s.previewEndMs;
            impl->meta.push_back(std::move(m));
            impl->infos.push_back(std::move(s));
        }
    } catch (...) {
        return nullptr;
    }
    if (impl->infos.empty()) return nullptr;
    return std::unique_ptr<Disc>(new Disc(std::move(impl)));
}

const std::vector<SongMeta>& Disc::songs() const { return impl_->meta; }

std::vector<uint8_t> Disc::rawMidi(int index) const {
    if (index < 0 || index >= int(impl_->infos.size())) return {};
    try {
        return impl_->data->read(impl_->infos[size_t(index)].midiPath);
    } catch (...) {
        return {};
    }
}

NeutralChart Disc::loadChart(int index) const {
    NeutralChart out;
    if (index < 0 || index >= int(impl_->infos.size())) return out;
    try {
        const Song song = buildSong(parseMidi(impl_->data->read(impl_->infos[size_t(index)].midiPath)));
        out.difficulties.resize(song.charts.size());
        for (size_t d = 0; d < song.charts.size(); ++d) {
            const Chart& c = song.charts[d];
            NeutralDifficulty& nd = out.difficulties[d];
            nd.gems.reserve(c.gems.size());
            for (const Gem& g : c.gems)
                nd.gems.push_back({g.ms, g.tick, g.durationMs, g.durationTicks, g.frets, g.hopo, g.noSustain, g.players, g.phrase});
            nd.phrases.reserve(c.phrases.size());
            for (const StarPhrase& p : c.phrases) nd.phrases.push_back({p.startTick, p.endTick, p.firstGem, p.lastGem});
        }
        out.beats.reserve(song.beats.size());
        for (const BeatLine& b : song.beats) out.beats.push_back({b.ms, b.measure});
        out.endMs = song.endMs;
        out.ok = true;
    } catch (...) {
        out = {};
    }
    return out;
}

std::unique_ptr<AudioStream> Disc::openAudio(int index) const {
    if (index < 0 || index >= int(impl_->infos.size())) return nullptr;
    try {
        return std::make_unique<VgsAudioStream>(impl_->data->read(impl_->infos[size_t(index)].audioPath));
    } catch (...) {
        return nullptr;
    }
}

}  // namespace gd::core::gh1
