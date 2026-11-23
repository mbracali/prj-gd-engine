// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Reading a Guitar Hero 1 (gd-game-01, PS2) disc's songs from another game, so
// a host (Guitar Hero 2 first) can play GH1 songs as "additive" / shared songs
// (GitHub #58, the additive-songs mod). Everything is read in memory from the
// disc image the player owns; nothing is extracted or written. The GH1 reader
// code (ISO 9660 + ARK archive, songs.dta, Harmonix MIDI, VGS / PS-ADPCM audio)
// lives under gd-engine-core/gh1/src, promoted from gd-game-01's own readers and
// kept in namespace gd::core::gh1 so a host's identically named types do not
// clash. This header is the only thing a host includes.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gd::core::gh1 {

// One GH1 song, enough to list it and set up its audio in a host.
struct SongMeta {
    std::string id;       // "aceofspades"
    std::string name;
    std::string artist;
    std::string venue;    // GH1 quickplay venue folder, e.g. "arena"
    std::string character, guitar;
    int animTempo = 1;    // 1 medium, 2 fast
    std::vector<float> pans;  // per audio channel, -1 (left) .. 1 (right)
    std::vector<float> vols;  // per audio channel, linear gain
    std::vector<int> cores;   // 1 = the player's guitar channel (muted on a miss)
    float previewStartMs = 0, previewEndMs = 0;
};

// A gem, with the same fields every game's own Gem has (so a host copies them
// field for field). HOPO is already marked by GH1's rule (gap <= 169 ticks).
struct NeutralGem {
    float ms = 0;
    int tick = 0;
    float durationMs = 0;
    int durationTicks = 0;
    uint8_t frets = 0;     // bit 0 green .. bit 4 orange
    bool hopo = false;
    bool noSustain = false;
    uint8_t players = 0;   // face-off ownership bits
    int phrase = -1;       // Star Power phrase index, or -1
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
    bool measure = false;  // a down-beat (bar line)
};
// A GH1 song's chart, one entry per difficulty (0 Easy .. 3 Expert). The host
// also gets rawMidi() to build its own tempo map (tick <-> ms).
struct NeutralChart {
    std::vector<NeutralDifficulty> difficulties;  // size 4
    std::vector<NeutralBeat> beats;
    float endMs = 0;
    bool ok = false;
};

// A GH1 song's audio: its VGS (PS-ADPCM) channels decoded to float. Pan / vol /
// which channel is the guitar come from SongMeta (not the file).
class AudioStream {
public:
    virtual ~AudioStream() = default;
    virtual int channels() const = 0;
    virtual int sampleRate() const = 0;
    virtual uint64_t lengthFrames() const = 0;
    // Fills out[ch][0..frames) with float samples (-1..1); past the end: silence.
    virtual void decode(float* const* out, int frames) = 0;
    virtual void seek(uint64_t frame) = 0;
};

// A GH1 disc opened for reading its songs. open() returns nullptr when the
// image is not a readable GH1 disc.
class Disc {
public:
    static std::unique_ptr<Disc> open(const std::string& discPath);
    ~Disc();
    Disc(const Disc&) = delete;
    Disc& operator=(const Disc&) = delete;

    const std::vector<SongMeta>& songs() const;
    // The song's Harmonix MIDI bytes, so the host can build a tempo map with
    // its own parser. Empty on error.
    std::vector<uint8_t> rawMidi(int index) const;
    // The song's chart for every difficulty. `ok` is false on error.
    NeutralChart loadChart(int index) const;
    // A stream over the song's VGS audio. nullptr on error.
    std::unique_ptr<AudioStream> openAudio(int index) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit Disc(std::unique_ptr<Impl> impl);
};

}  // namespace gd::core::gh1
