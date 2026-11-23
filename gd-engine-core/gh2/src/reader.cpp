// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The GH2 song reader façade (gd/core/gh2/reader.h), mirror of gd/core/gh1.
#include "gd/core/gh2/reader.h"

#include <algorithm>
#include <string>

#include "audio/vorbis_stream.h"
#include "data/game_data.h"
#include "data/midi.h"
#include "game/chart.h"
#include "game/song_db.h"

namespace gd::core::gh2 {

namespace {
bool isRealSong(const SongInfo& s) {
    static const char* kSkip[] = {"tutorial", "credits", "practice", "test", "placeholder", "thefury"};
    for (const char* k : kSkip)
        if (s.id.find(k) != std::string::npos) return false;
    return true;
}

struct VorbisAudioStream : AudioStream {
    VorbisStream v;
    explicit VorbisAudioStream(Bytes mogg) : v(std::move(mogg)) {}
    int channels() const override { return v.channels(); }
    int sampleRate() const override { return v.sampleRate(); }
    uint64_t lengthFrames() const override { return v.lengthFrames(); }
    void decode(float* const* out, int frames) override {
        const int n = v.decode(out, frames);  // Ogg gives fewer than asked at the end
        for (int c = 0; c < v.channels(); ++c)
            for (int i = std::max(0, n); i < frames; ++i) out[c][i] = 0.f;
    }
    void seek(uint64_t frame) override { v.seek(frame); }
};
}  // namespace

struct Disc::Impl {
    std::unique_ptr<GameData> data;
    std::vector<SongInfo> infos;
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
            m.animTempo = s.animTempo;
            m.pans = s.audio.pans;
            m.vols = s.audio.vols;
            m.cores = s.audio.cores;
            m.tracks = s.audio.tracks;
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
        return impl_->data->read(impl_->infos[size_t(index)].audio.midiPath);
    } catch (...) {
        return {};
    }
}

NeutralChart Disc::loadChart(int index) const {
    NeutralChart out;
    if (index < 0 || index >= int(impl_->infos.size())) return out;
    try {
        const SongAudio& a = impl_->infos[size_t(index)].audio;
        const int hopo = a.hopoThresholdTicks >= 0 ? a.hopoThresholdTicks : 170;
        const Song song = buildSong(parseMidi(impl_->data->read(a.midiPath)), hopo);
        const size_t gp = size_t(Part::Guitar);
        out.difficulties.resize(song.charts[gp].size());
        for (size_t d = 0; d < song.charts[gp].size(); ++d) {
            const Chart& c = song.charts[gp][d];
            NeutralDifficulty& nd = out.difficulties[d];
            nd.gems.reserve(c.gems.size());
            for (const Gem& g : c.gems)
                nd.gems.push_back({g.ms, g.tick, g.durationMs, g.durationTicks, g.frets, g.hopo, g.noSustain, g.players, g.phrase});
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
        return std::make_unique<VorbisAudioStream>(impl_->data->read(impl_->infos[size_t(index)].audio.moggPath));
    } catch (...) {
        return nullptr;
    }
}

}  // namespace gd::core::gh2
