// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// MUSIC/<SONG>/BEATMATCH.STR (docs/FORMATS.md "BEATMATCH.STR"): the STR
// container with four heap images, entries 125..128, one per difficulty
// (the word at +0x2F4: 0..3) [C data]. Each image is RAM loaded at
// 0x096E9A80 [C: all 27 kPad_* symbol strings of an image are pointed to
// from that base; checked per song by kPadSymbolsAt()].
//
// The gems: 20-byte records {f32 ms, u32 tick (480 per beat), u16, u16, u8
// id, u8, u8 lane mask (1 2 4 8; chords OR them), u8 flags, u32 flags}
// [C data]. Every image holds every track's gems for all four
// difficulties (the images differ only in about 2,000 words of other
// per-difficulty objects [C: ABC, Easy vs Expert]).
//
// The tracks [C data, all 41 songs]: an object per instrument holds a
// pointer to its name "PART DRUMS" / "PART BASS" / "PART GUITAR" / "PART
// VOCALS" (and the short name drum / bass / guitar / vocals, then its index
// 0..3), followed by four gem vectors {u32 pointer, u32 count, u32
// capacity}, each after a small number that is the same for the song (0xAA
// in ABC, 0x5A in WOULD), one per difficulty, in address order [I: Easy..Expert, from the
// counts, which grow; most Easy charts use the two middle lanes only]. readTracks() takes every gem vector
// (records with ascending ticks) and gives it to the nearest track name
// pointer before it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gd24 {

struct Gem {
    float ms = 0;
    uint32_t tick = 0;
    uint16_t a = 0, b = 0;  // [I] durations
    uint8_t id = 0;         // [I] MIDI-pitch-like
    uint8_t lanes = 0;      // bit mask: 1 2 4 8
    uint8_t flags = 0;
    uint32_t flags2 = 0;
};

struct GemList {
    uint32_t address = 0;  // in PSP RAM
    std::vector<Gem> gems;
    int chords() const;
    bool twoLane() const;   // only lanes 2 and 3 (masks 2, 4)
    bool offGrid() const;   // ticks off the 1/32-beat grid (vocal-like) [I]
};

// A time signature change [C data, all 55 BEATMATCH files]: 16-byte records
// {u32 bar, u32 numerator, u32 denominator, u32 tick}, the first one at bar
// 0 / tick 0; a vector {pointer, count, capacity = count} points to them
// (seven copies per image, all equal). The ticks agree with the bars:
// tick[i+1] - tick[i] = (bar[i+1] - bar[i]) * num[i] * 1920 / den[i].
struct TimeSig {
    int bar = 0, num = 4, den = 4;
    uint32_t tick = 0;
};

struct BeatmatchImage {
    uint32_t entry = 0, offset = 0, size = 0;
    uint32_t difficulty = 0;  // the word at +0x2F4
};

struct Beatmatch {
    static constexpr uint32_t kBase = 0x096E9A80;
    std::vector<BeatmatchImage> images;  // entries 125..128
    std::vector<uint8_t> file;

    bool parse(std::vector<uint8_t> bytes, std::string* error);
    const uint8_t* image(const BeatmatchImage& im) const { return file.data() + im.offset; }
    // How many kPad_* symbol strings of the image are pointed to from kBase
    // (and how many there are).
    void kPadSymbolsAt(const BeatmatchImage& im, int& pointed, int& total) const;
    // The gem lists of an image found by their records alone (at least
    // `minGems` each), owned or not: a cross-check of readTracks.
    std::vector<GemList> readGemLists(const BeatmatchImage& im, size_t minGems = 8) const;

    struct Track {
        std::string name;              // "PART DRUMS"
        std::vector<GemList> difficulty;  // [0] Easy .. [3] Expert
    };
    // The instrument tracks and their four difficulties' gems.
    std::vector<Track> readTracks(const BeatmatchImage& im, std::string* error) const;
    // The song's time signatures (the longest list that passes the bar /
    // tick check above); {0, 4, 4, 0} when none is found.
    std::vector<TimeSig> readTimeSigs(const BeatmatchImage& im) const;
};

}  // namespace gd24
