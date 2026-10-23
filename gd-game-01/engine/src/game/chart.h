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
    float durationMs = 0;  // stored as a whole number of ms, as in the game
    int durationTicks = 0;
    uint8_t frets = 0;     // bit 0 = green .. bit 4 = orange
    bool hopo = false;     // can be hammered on / pulled off
    bool noSustain = false;
    uint8_t players = 0;   // face-off ownership bits (multiplayer only)
    // FUN_0025b688: mask 0 (no section note held) or 3 is both players'.
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

struct BeatLine {
    float ms;
    bool measure;
};

// A text event of the EVENTS track ("verse", "chorus", "solo", "gtr_on", ...).
struct SongEvent {
    float ms;
    int tick;
    std::string text;
};

// GuitarAnimInfo's MIDI inputs (docs/guitarist_hands.md 1.2): ANIM-track
// fret positions (notes 40..59 -> spot 0..19, beats) and the HandMap_* /
// StrumMap_* text events of any track.
struct FretPosEvent {
    float start, end;
    int value;
};
struct HandMapEvent {
    float beat;
    std::string symbol;
};

struct Song {
    TempoMap tempo;
    std::array<Chart, kDifficultyCount> charts;
    std::vector<BeatLine> beats;
    std::vector<SongEvent> events;  // in time order
    // TRIGGERS (config/midi_triggers.dta): note 60 kick_drum 90 ms early, 61 bass_hit 50 ms early.
    std::vector<SongEvent> triggers;  // in time order, already moved early
    // GuitarAnimInfo singer_events: T1 GEMS note 108 (open) windows, ms (docs/faces_shadows.md A.4).
    std::vector<std::pair<float, float>> singerEvents;
    float endMs = 0;  // the EVENTS track's "end", or the last gem
    std::vector<FretPosEvent> fretPositions;      // in note-off order
    std::vector<HandMapEvent> handMaps, strumMaps;
};

// Builds the playable charts with the game's rules (DifficultyParser,
// GameGemList; see FINDINGS.md "Song charts"). Throws on malformed input.
Song buildSong(const MidiFile& midi);
