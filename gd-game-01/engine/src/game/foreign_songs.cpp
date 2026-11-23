// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/foreign_songs.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "data/midi.h"
#include "game/rules.h"
#include "gd/core/gh2/reader.h"

namespace {

constexpr int kOutRate = 44100;  // GH1's VGS rate
constexpr int kFilters[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

void putU32(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v)), b.push_back(uint8_t(v >> 8)), b.push_back(uint8_t(v >> 16)), b.push_back(uint8_t(v >> 24));
}

// Encodes one 28-sample block for channel `ch`, writing 16 bytes to `out`.
// Fast single pass: filter 0 (no prediction, so no cross-block state), a
// per-block shift so the loudest sample uses the nibble's full range. The
// decoder reconstructs s = int16(nibble<<12) >> shift. (Lossy, like GH1's own
// VGS, but quick enough to transcode a whole song at a loading screen.)
void encodeBlock(const int16_t* s, int n, int ch, int& /*s1*/, int& /*s2*/, uint8_t out[16]) {
    int maxAbs = 1;
    for (int i = 0; i < n; ++i) maxAbs = std::max(maxAbs, std::abs(int(s[i])));
    int shift = 0;
    for (int sh = 12; sh >= 0; --sh)  // the largest shift (finest step) that does not clip the nibble
        if ((maxAbs >> (12 - sh)) <= 7) {
            shift = sh;
            break;
        }
    const int d = 12 - shift;
    for (int i = 0; i < 16; ++i) out[i] = 0;
    out[0] = uint8_t(shift);  // filter 0
    out[1] = uint8_t(ch & 0x7F);
    for (int i = 0; i < 28; ++i) {
        const int sample = i < n ? s[i] : 0;
        const int q = std::clamp(d > 0 ? (sample + (1 << (d - 1))) >> d : sample, -8, 7);
        out[2 + i / 2] |= uint8_t((q & 0x0F) << ((i & 1) * 4));
    }
}

// Linear resample one channel from srcRate to kOutRate.
std::vector<int16_t> resample(const std::vector<float>& in, int srcRate) {
    if (in.empty()) return {};
    if (srcRate == kOutRate) {
        std::vector<int16_t> out(in.size());
        for (size_t i = 0; i < in.size(); ++i) out[i] = int16_t(std::clamp(in[i], -1.f, 1.f) * 32767.f);
        return out;
    }
    const double ratio = double(srcRate) / kOutRate;
    const size_t outN = size_t(double(in.size()) / ratio);
    std::vector<int16_t> out(outN);
    for (size_t t = 0; t < outN; ++t) {
        const double sp = double(t) * ratio;
        const size_t i0 = size_t(sp);
        const double frac = sp - double(i0);
        const float a = in[i0], b = i0 + 1 < in.size() ? in[i0 + 1] : a;
        out[t] = int16_t(std::clamp(a + float(frac) * (b - a), -1.f, 1.f) * 32767.f);
    }
    return out;
}

}  // namespace

std::vector<SongInfo> openForeignSongs(const std::vector<std::pair<std::string, std::string>>& extra, ForeignLibrary& lib) {
    std::vector<SongInfo> out;
    for (const auto& [gameId, path] : extra) {
        if (gameId != "gd-game-02") continue;  // Guitar Hero 2 (wave 2); other sources follow
        auto disc = gd::core::gh2::Disc::open(path);
        if (!disc) continue;
        const int discIdx = int(lib.discs.size());
        const auto& songs = disc->songs();
        for (int j = 0; j < int(songs.size()); ++j) {
            const gd::core::gh2::SongMeta& m = songs[size_t(j)];
            SongInfo info;
            info.id = "gh2_" + m.id;
            info.name = m.name;
            info.artist = m.artist;
            info.animTempo = m.animTempo;
            info.previewStartMs = m.previewStartMs;
            info.previewEndMs = m.previewEndMs;
            info.foreignDisc = discIdx;
            info.foreignSong = j;
            info.foreignLabel = "Guitar Hero II";
            info.pans = m.pans;
            info.cores = m.cores;
            info.vols.resize(m.vols.size());  // GH2 vols are dB; GH1 uses linear gain
            for (size_t c = 0; c < m.vols.size(); ++c) info.vols[c] = std::pow(10.f, m.vols[c] / 20.f);
            out.push_back(std::move(info));
        }
        lib.discs.push_back(std::move(disc));
    }
    return out;
}

Song loadForeignChart(const ForeignLibrary& lib, const SongInfo& info) {
    Song song;
    if (info.foreignDisc < 0 || info.foreignDisc >= int(lib.discs.size())) return song;
    const gd::core::gh2::Disc& disc = *lib.discs[size_t(info.foreignDisc)];
    const gd::core::gh2::NeutralChart nc = disc.loadChart(info.foreignSong);
    try {
        const Bytes midi = disc.rawMidi(info.foreignSong);
        if (!midi.empty()) song.tempo.build(parseMidi(midi));
    } catch (...) {
    }
    for (size_t d = 0; d < nc.difficulties.size() && d < size_t(kDifficultyCount); ++d) {
        Chart& c = song.charts[d];
        c.gems.reserve(nc.difficulties[d].gems.size());
        for (const gd::core::gh2::NeutralGem& g : nc.difficulties[d].gems)
            c.gems.push_back({g.ms, g.tick, g.durationMs, g.durationTicks, g.frets, g.hopo, g.noSustain, g.players, g.phrase});
        for (const gd::core::gh2::NeutralPhrase& p : nc.difficulties[d].phrases)
            c.phrases.push_back({p.startTick, p.endTick, p.firstGem, p.lastGem});
    }
    for (const gd::core::gh2::NeutralBeat& b : nc.beats) song.beats.push_back({b.ms, b.measure});
    song.endMs = nc.endMs;
    return song;
}

Bytes loadForeignVgs(const ForeignLibrary& lib, const SongInfo& info) {
    if (info.foreignDisc < 0 || info.foreignDisc >= int(lib.discs.size())) return {};
    const uint64_t t0 = SDL_GetTicksNS();
    auto stream = lib.discs[size_t(info.foreignDisc)]->openAudio(info.foreignSong);
    if (!stream) return {};
    const int nch = stream->channels();
    const size_t nchz = size_t(nch);
    const int rate = stream->sampleRate();
    const uint64_t frames = stream->lengthFrames();
    if (nch <= 0 || frames == 0) return {};
    // Decode the whole song to float per channel.
    std::vector<std::vector<float>> pcm(nchz);
    for (auto& v : pcm) v.assign(size_t(frames), 0.f);
    {
        std::vector<float*> ptrs(nchz);
        const uint64_t kChunk = 1 << 16;
        for (uint64_t pos = 0; pos < frames; pos += kChunk) {
            const int want = int(std::min<uint64_t>(kChunk, frames - pos));
            for (int c = 0; c < nch; ++c) ptrs[size_t(c)] = pcm[size_t(c)].data() + pos;
            stream->decode(ptrs.data(), want);
        }
    }
    // Resample each channel to GH1's rate, then PS-ADPCM encode, interleaving
    // one block per channel per 28-frame step.
    std::vector<std::vector<int16_t>> res(nchz);
    size_t outFrames = 0;
    for (int c = 0; c < nch; ++c) {
        res[size_t(c)] = resample(pcm[size_t(c)], rate);
        outFrames = std::max(outFrames, res[size_t(c)].size());
    }
    pcm.clear();
    pcm.shrink_to_fit();
    const size_t blocks = (outFrames + 27) / 28;
    Bytes vgs;
    vgs.reserve(0x80 + blocks * size_t(nch) * 16);
    for (char ch : {'V', 'g', 'S', '!'}) vgs.push_back(uint8_t(ch));
    putU32(vgs, 2);
    for (int c = 0; c < nch; ++c) putU32(vgs, uint32_t(kOutRate)), putU32(vgs, uint32_t(blocks));
    vgs.resize(0x80, 0);
    std::vector<int> s1(size_t(nch), 0), s2(size_t(nch), 0);
    for (size_t blk = 0; blk < blocks; ++blk) {
        for (int c = 0; c < nch; ++c) {
            const auto& r = res[size_t(c)];
            int16_t buf[28] = {};
            const size_t base = blk * 28;
            int n = 0;
            for (; n < 28 && base + size_t(n) < r.size(); ++n) buf[n] = r[base + size_t(n)];
            uint8_t out[16];
            encodeBlock(buf, n, c, s1[size_t(c)], s2[size_t(c)], out);
            vgs.insert(vgs.end(), out, out + 16);
        }
    }
    SDL_Log("additive: transcoded %s (%d ch, %d Hz -> %d Hz, %zu KB VGS) in %.0f ms", info.id.c_str(), nch, rate, kOutRate,
            vgs.size() / 1024, double(SDL_GetTicksNS() - t0) / 1e6);
    return vgs;
}
