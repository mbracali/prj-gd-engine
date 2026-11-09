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

// Rock Band's charts (docs/audio_and_charts.md "Charts"). The gem record
// keeps gd-game-02's (GH2) fields; the RB parser's note handling is read in
// RB 0x8249F990 (note on) / 0x8249FB68 (gem at note off) [C].
struct Gem {
    float ms = 0;          // start_ms
    int tick = 0;          // start_tick
    float durationMs = 0;  // whole ms
    int durationTicks = 0;
    // Lanes: guitar / bass bit 0 = green .. bit 4 = orange; drums bit 0 =
    // kick, bit 1 red, 2 yellow, 3 blue, 4 green (base + 0..4 of each
    // difficulty's octave, num_slots 5) [C data + RB 0x824A1770 num_slots].
    uint8_t frets = 0;
    bool hopo = false;
    // A gem shorter than 161 ticks has no sustain, and every drum gem
    // (drum_style_instruments, the parser's +0x91 flag) [C RB 0x8249FB68].
    bool noSustain = false;
    // 0 = every player, else bit p = player p: the player sections
    // (player_slot 9: base + 9 / base + 10) of Tug of War and Score Duel [I].
    uint8_t players = 0;
    bool ownedBy(int p) const { return players == 0 || ((players >> p) & 1); }
    int phrase = -1;  // Overdrive phrase index (pitch 116), or -1
    int solo = -1;    // solo index (pitch 103), or -1
    int fill = -1;    // drum fill / Big Rock Ending lane span index (pitches 120..124), or -1

    int fretCount() const { return std::popcount(static_cast<unsigned>(frets)); }
};

// An Overdrive phrase (pitch 116 [C RB 0x8249F990]) and the gems inside it.
struct StarPhrase {
    int startTick = 0, endTick = 0;
    int firstGem = -1, lastGem = -1;
};

// A span of the track: solos (pitch 103 [C]), fills (120..124 [C]: the
// lanes' mask in `lanes`), player sections.
struct Span {
    int startTick = 0, endTick = 0;
    float startMs = 0, endMs = 0;
    uint8_t lanes = 0;
};

struct Chart {
    std::vector<Gem> gems;
    std::vector<StarPhrase> phrases;  // Overdrive
    std::vector<Span> solos;
    // Drum fills (PART DRUMS) and, from the [coda] on, the Big Rock Ending
    // (every instrument's 120..124 lanes).
    std::vector<Span> fills;
};

// The playable PART tracks of a Rock Band chart [C data: every .mid on the
// disc has these four].
enum class Part { Guitar, Bass, Drums, Vocals };
constexpr int kPartCount = 4;
const char* partTrackName(Part p);  // "PART GUITAR", ...
const char* partSymbol(Part p);     // songs.dtb's instrument names: guitar, bass, drum, vocals

// The vocal track (PART VOCALS) [C data]: notes on pitches 36..84 (the
// parser's vocal range: RB 0x824A1770 sets 0x24 .. 0x54), lyrics as MIDI
// lyric events (meta 5) at the note's tick, phrase markers 105 / 106, the
// Overdrive phrases 116 and the percussion 96 (shown) / 97 (hidden).
struct VocalNote {
    float ms = 0, durationMs = 0;
    int tick = 0, durationTicks = 0;
    int pitch = 0;            // MIDI note, 36..84
    std::string lyric;        // the syllable as charted (suffixes kept)
    std::string text;         // the syllable as shown: '#', '^' and '-' removed, '=' -> '-'
    bool unpitched = false;   // a '#' (or '^') syllable: talky, scored by rhythm only [I]
    bool slide = false;       // a '+' syllable: the pitch slide from the previous note
    bool joinNext = false;    // the syllable ends with '-': no space before the next
    int phrase = -1;
};

struct VocalPhrase {
    int startTick = 0, endTick = 0;
    float startMs = 0, endMs = 0;
    bool overdrive = false;  // inside a 116 span
    int firstNote = -1, lastNote = -1;
    bool second = false;     // a 106 marker (the second player's in the split modes) [I]
};

struct Percussion {
    float ms = 0;
    int tick = 0;
    bool shown = true;  // 96 shown, 97 only heard [I]
};

struct VocalTrack {
    std::vector<VocalNote> notes;
    std::vector<VocalPhrase> phrases;
    std::vector<StarPhrase> overdrive;  // 116 spans (gem indices unused)
    std::vector<Percussion> percussion;
    bool empty() const { return notes.empty(); }
};

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

// A drum mix event of PART DRUMS ("[mix <difficulty> drums<n>...]"): which
// beatmatcher.dtb submix the drum stems use from this point [C data].
struct DrumMix {
    int tick = 0;
    int difficulty = 0;
    std::string mix;
};

// A note of the VENUE track the world plays (camera cuts, lighting, the
// band's directed shots are text events; notes as listed in
// docs/venues.md) [C data].
struct VenueNote {
    enum class Track : uint8_t { Venue, Beat };
    float ms;
    int tick;
    Track track;
    int pitch;
};

struct Song {
    TempoMap tempo;
    // charts[part][difficulty] for guitar, bass and drums; `hasPart` says
    // whether the MIDI has the track (vocals: `vocals`).
    std::array<std::array<Chart, kDifficultyCount>, kPartCount> charts;
    std::array<bool, kPartCount> hasPart{};
    VocalTrack vocals;
    std::vector<DrumMix> drumMixes;
    // The bass ranges (gd02 GD02-036): every PART track's notes below pitch
    // 36 (the animation notes 12..15 of RB's tracks are not), [on, off] ms.
    std::vector<std::pair<float, float>> bassRanges;
    std::array<std::vector<int>, kPartCount> bigNotes;  // unused by RB (GH2's pitch 110)
    std::vector<BeatLine> beats;
    std::vector<SongEvent> events;  // in time order
    std::vector<SongEvent> venueEvents;  // VENUE track texts (cuts, lighting)
    std::vector<VenueNote> venueNotes;  // note-ons, in time order
    float endMs = 0;    // the EVENTS track's [end], or the last event
    int codaTick = -1;  // the EVENTS track's [coda]: the Big Rock Ending starts there

    const Chart& chart(Part p, Difficulty d) const { return charts[size_t(p)][size_t(d)]; }
};

// Builds the playable charts of every PART track and the vocal track, with
// the song's HOPO threshold in ticks (songs.dtb's hopo_threshold, else 170;
// RB 0x824A67A8). Throws on malformed input.
Song buildSong(const MidiFile& midi, int hopoThresholdTicks);
// Rock Band's drums: every pad of a chord is its own gem at the same time
// (DrumTrackWatcherImpl, RB 0x824A21E8) [C]; the phrases' gem indices follow.
void splitDrumChords(Chart& chart);
