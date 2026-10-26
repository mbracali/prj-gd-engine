// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "data/midi.h"
#include "game/rules.h"

struct Gem {
    float ms = 0;          // start time
    int tick = 0;
    float durationMs = 0;  // a whole number of ms, as in GH1 [I for GH2]
    int durationTicks = 0;
    uint8_t frets = 0;     // bit 0 = green .. bit 4 = orange
    bool hopo = false;     // can be hammered on / pulled off
    bool noSustain = false;
    uint8_t players = 0;   // face-off ownership bits (multiplayer only)
    // GH1 (FUN_0025b688): mask 0 (no section note held) or 3 is both players'.
    bool ownedBy(int p) const { return players == 0 || ((players >> p) & 1); }
    int phrase = -1;       // Star Power phrase index, or -1

    int fretCount() const { return std::popcount(static_cast<unsigned>(frets)); }
};

struct StarPhrase {
    int startTick = 0, endTick = 0;
    int firstGem = -1, lastGem = -1;
};

struct Chart {
    std::vector<Gem> gems;
    std::vector<StarPhrase> phrases;
};

// The playable PART tracks of a GH2 chart (docs/audio_and_charts.md) [C data].
enum class Part { Guitar, GuitarCoop, Bass, Rhythm };
constexpr int kPartCount = 4;
const char* partTrackName(Part p);  // "PART GUITAR", ...

struct BeatLine {
    float ms;
    bool measure;
};

// A text event of the EVENTS track, brackets and spaces trimmed
// ("[section verse_1]" -> "section verse_1", "[end]" -> "end").
struct SongEvent {
    float ms;
    int tick;
    std::string text;
};

struct Song {
    TempoMap tempo;
    // charts[part][difficulty]; `hasPart` says whether the MIDI has the track.
    std::array<std::array<Chart, kDifficultyCount>, kPartCount> charts;
    std::array<bool, kPartCount> hasPart{};
    std::vector<BeatLine> beats;
    std::vector<SongEvent> events;  // in time order
    float endMs = 0;  // the EVENTS track's [end], or the last event

    const Chart& chart(Part p, Difficulty d) const { return charts[size_t(p)][size_t(d)]; }
};

// Builds the playable charts of every PART track with GH1's parser rules
// (gd01 game/chart.cpp; [I] for GH2 until read in the 360 executable) and
// GH2's HOPO threshold in ticks (beatmatcher.dtb, or the song's own).
// Throws on malformed input.
Song buildSong(const MidiFile& midi, int hopoThresholdTicks);
