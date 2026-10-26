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

// GH2's gem record (16 bytes, built by 0x82345480) [C]:
// {f32 start_ms; s32 start_tick; u16 duration_ms; u16 duration_ticks;
//  u8 fret_mask; u8 flags; u8 player_mask; u8 pad}, flags bit 7 = HOPO-able,
// bit 6 = no sustain.
struct Gem {
    float ms = 0;          // start_ms
    int tick = 0;          // start_tick
    float durationMs = 0;  // u16 duration_ms (whole ms)
    int durationTicks = 0;
    uint8_t frets = 0;     // fret_mask: bit 0 = green .. bit 4 = orange
    bool hopo = false;     // flags 0x80
    bool noSustain = false;  // flags 0x40
    // player_mask: 0 = every player, else bit p = player p (0x82347688) [C].
    // Set from the face-off section notes (assumed as in GH1, FINDINGS open
    // question 3) [I].
    uint8_t players = 0;
    bool ownedBy(int p) const { return players == 0 || ((players >> p) & 1); }
    int phrase = -1;       // Star Power phrase index, or -1 (0x822bc1e8)

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

// A note of the band and venue tracks the world plays (config/
// midi_parsers.dta [C data]): BAND DRUMS 36 kick_drum / 37 crash_symbal
// (drummer_kick_drum), BAND BASS 36 bass_hit (speaker_pulse), TRIGGERS
// 48 / 49 / 50 do_lighting_next / prev / first_keyframe (lighting_parser,
// start_offset -4: four beats early) and 52 venue_effect (effect_parser).
struct VenueNote {
    enum class Track : uint8_t { Drums, Bass, Triggers };
    float ms;
    int tick;
    Track track;
    int pitch;
};

struct Song {
    TempoMap tempo;
    // charts[part][difficulty]; `hasPart` says whether the MIDI has the track.
    std::array<std::array<Chart, kDifficultyCount>, kPartCount> charts;
    std::array<bool, kPartCount> hasPart{};
    std::vector<BeatLine> beats;
    std::vector<SongEvent> events;  // in time order
    std::vector<VenueNote> venueNotes;  // note-ons, in time order
    float endMs = 0;  // the EVENTS track's [end], or the last event

    const Chart& chart(Part p, Difficulty d) const { return charts[size_t(p)][size_t(d)]; }
};

// Builds the playable charts of every PART track with GH2's parser rules
// (docs/FINDINGS.md "Chart -> gem list") and the song's HOPO threshold in
// ticks (songs.dtb's hopo_threshold, else beatmatcher.dtb's; 0x823341d0).
// Throws on malformed input.
Song buildSong(const MidiFile& midi, int hopoThresholdTicks);
