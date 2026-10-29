// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class Disc;

// One song's chart: songs\<name>.mid.qb in SONGS/<name>_song.pak.xen
// (docs/formats.md "Song charts"). Times in ms. Rendering-free (L17).
enum class Part : uint8_t { Guitar, Rhythm, GuitarCoop, RhythmCoop, Count };
constexpr int kDifficulties = 4;  // Easy, Medium, Hard, Expert
const char* partName(Part p);
const char* difficultyName(int d);

struct ChartNote {
    int32_t time = 0, length = 0;
    uint8_t frets = 0;    // bit 0 green .. bit 4 orange [C data: bits 0-4 and 0x20 only]
    bool flag20 = false;  // mask bit 0x20 [I: GH3's forced (HOPO-flip) flag; confirm in the XEX]
};

struct ChartPhrase {
    int32_t time = 0, length = 0;
    int32_t notes = 0;  // Star Power / battle-star phrases: notes in the phrase
};

struct TimeSig {
    int32_t time = 0, num = 4, den = 4;
};

struct ChartEvent {  // the *_notes arrays: (time, note, length[, value])
    int32_t time = 0, note = 0, length = 0;
    int32_t value = -1;  // the optional 4th number (-1: none) [I: velocity]
};

struct ChartMarker {
    int32_t time = 0;
    uint32_t text = 0;  // key of the marker's text
};

struct PartChart {
    std::array<std::vector<ChartNote>, kDifficulties> notes;
    std::array<std::vector<ChartPhrase>, kDifficulties> star, starBattle;
    bool empty() const {
        for (const auto& n : notes)
            if (!n.empty()) return false;
        return true;
    }
};

struct Chart {
    std::string song;
    std::array<PartChart, size_t(Part::Count)> parts;
    std::vector<ChartPhrase> faceOffP1, faceOffP2, bossP1, bossP2;
    std::vector<TimeSig> timesig;
    std::vector<int32_t> fretbars;
    std::vector<ChartMarker> markers;
    // The event arrays by their suffix (scripts, anim, triggers, cameras,
    // lightshow, crowd, drums, performance).
    std::vector<ChartEvent> cameras, lightshow, crowd, drums, anim, scriptsNotes, triggers, performanceNotes;
    // <song>_scripts: scripts run at a time ({time, scr}), e.g. the boss
    // songs' boss_battle_begin_deathlick [C data].
    struct TimedScript {
        int32_t time = 0;
        uint32_t script = 0;
    };
    std::vector<TimedScript> scripts;

    const std::vector<ChartNote>& notes(Part p, int diff) const { return parts[size_t(p)].notes[size_t(diff)]; }
};

// Loads a song's chart from the disc; throws std::runtime_error.
Chart loadChart(const Disc& disc, const std::string& songName);
