// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <bit>
#include <cstdint>
#include <vector>

#include "game/chart.h"

struct Tuning;

// A player's notes as GH3's judge sees them: the "input array" the game
// builds per note (0x821044C8 -> 0x82101770) [C]. Rendering-free.
struct TrackNote {
    int32_t time = 0;     // ms
    int32_t length = 0;   // ms, as charted
    uint8_t frets = 0;    // bit 0 green .. bit 4 orange
    bool hopo = false;    // natural HOPO XOR the 0x20 flip
    bool sustain = false; // length > 0.5 x first beat [C 0x82102E28]
    int32_t holdMs = 0;   // the length to hold: length - whammy_shorten x first beat [C 0x82102E28]
    int maxSustainPoints = 0;  // round(length in beats x points_per_note_per_beat) [C 0x8210B774]
    int phrase = -1;      // index of the Star Power phrase it belongs to, or -1
    bool phraseLast = false;   // the phrase's last note
    int fretCount() const { return std::popcount(unsigned(frets)); }
    bool chord() const { return (frets & (frets - 1)) != 0; }
};

struct NoteTrack {
    std::vector<TrackNote> notes;
    std::vector<ChartPhrase> phrases;
    std::vector<int32_t> fretbars;
    std::vector<TimeSig> timesig;
    int32_t firstBeatMs = 500;  // fretbar[1] - fretbar[0] [C 0x829E3918]
    // Beat length (ms) of the fretbar interval holding `t`.
    float beatAt(float t) const;
    // Length of the measure holding `t`: the time signature's numerator x
    // the beat there (fretbars are beats) [I: the drain script's measure].
    float measureAt(float t) const;
    // Beats (fretbar intervals) between t0 and t1.
    float beatsBetween(float t0, float t1) const;
};

// The HOPO tolerance (ms) at song time `t`: the fretbar interval holding t
// divided by hammer_on_measure_scale, times floor(den / 4) of the time
// signature in force [C 0x82107590]. Which interval applies to a note is
// the one holding the note's time [I: the game updates it on a fretbar
// iterator that runs ahead with the gem scroller].
float hopoTolerance(const Chart& c, float measureScale, float t);

NoteTrack buildTrack(const Chart& chart, Part part, int difficulty, const Tuning& tuning, float hammerOnMeasureScale);

// Face-off: only the notes inside the player's sections (FaceOffP1 / P2:
// time, length) enter its input array (gem_filler_enabled_time_on / off,
// 0x82104558-0x821045CC) [C]; HOPOs keep the flags of the full chart [I].
NoteTrack faceOffTrack(const NoteTrack& full, const std::vector<ChartPhrase>& sections);
