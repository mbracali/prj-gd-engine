// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Reading a Guitar Hero 2 (gd-game-02, Xbox 360) disc's songs from another game
// (additive-songs mod, GitHub #58), the mirror of gd/core/gh1/reader.h. The GH2
// readers (XDVDFS + ARK archive, songs.dtb, Harmonix MIDI, .mogg = Ogg Vorbis
// via stb_vorbis) live under gd-engine-core/gh2/src, promoted from gd-game-02's
// own source and kept in namespace gd::core::gh2 so a host's identically named
// types do not clash. Everything is read in memory; nothing is extracted.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gd::core::gh2 {

// One GH2 song, enough to list it and set up its audio in a host.
struct SongMeta {
    std::string id, name, artist, venue;
    int animTempo = 1;
    std::vector<float> pans;  // per audio channel, -1..1
    std::vector<float> vols;  // per audio channel, dB
    std::vector<int> cores;   // 1 = the player's guitar channel (muted on a miss)
    std::map<std::string, std::vector<int>> tracks;  // instrument -> channels (guitar/bass/rhythm)
    float previewStartMs = 0, previewEndMs = 0;
};

// A gem, fields identical to each game's own Gem (the host copies them).
struct NeutralGem {
    float ms = 0;
    int tick = 0;
    float durationMs = 0;
    int durationTicks = 0;
    uint8_t frets = 0;
    bool hopo = false;
    bool noSustain = false;
    uint8_t players = 0;
    int phrase = -1;
};
struct NeutralPhrase {
    int startTick = 0, endTick = 0, firstGem = -1, lastGem = -1;
};
struct NeutralDifficulty {
    std::vector<NeutralGem> gems;
    std::vector<NeutralPhrase> phrases;
};
struct NeutralBeat {
    float ms = 0;
    bool measure = false;
};
struct NeutralChart {
    std::vector<NeutralDifficulty> difficulties;  // size 4 (Easy..Expert), the guitar part
    std::vector<NeutralBeat> beats;
    float endMs = 0;
    bool ok = false;
};

// A GH2 song's audio: its .mogg (Ogg Vorbis) channels decoded to float. decode()
// always fills `frames` (silence past the end), as gd/core/gh1's AudioStream.
class AudioStream {
public:
    virtual ~AudioStream() = default;
    virtual int channels() const = 0;
    virtual int sampleRate() const = 0;
    virtual uint64_t lengthFrames() const = 0;
    virtual void decode(float* const* out, int frames) = 0;
    virtual void seek(uint64_t frame) = 0;
};

// A GH2 disc opened for reading its songs. open() returns nullptr when the
// image is not a readable GH2 disc.
class Disc {
public:
    static std::unique_ptr<Disc> open(const std::string& discPath);
    ~Disc();
    Disc(const Disc&) = delete;
    Disc& operator=(const Disc&) = delete;

    const std::vector<SongMeta>& songs() const;
    std::vector<uint8_t> rawMidi(int index) const;  // the song's MIDI bytes (host tempo map)
    NeutralChart loadChart(int index) const;         // the guitar part, every difficulty
    std::unique_ptr<AudioStream> openAudio(int index) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit Disc(std::unique_ptr<Impl> impl);
};

}  // namespace gd::core::gh2
