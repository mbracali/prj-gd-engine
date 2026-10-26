// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/chart.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

// GH1's DifficultyParser constants (gd01 FINDINGS.md "Song charts"); GH2's
// charts use the same note layout [C data], the constants are [I] for GH2.
constexpr int kChordTicks = 10;      // fret note-ons this close form one chord [I]
constexpr int kSustainTicks = 161;   // shorter gems have no sustain [I]

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// GH2's text events are bracketed ("[end]"), and a few charts have stray
// spaces (docs/audio_and_charts.md) [C data]: trim both.
std::string eventText(const std::string& raw) {
    std::string t = trim(raw);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') t = trim(t.substr(1, t.size() - 2));
    return t;
}

const MidiTrack* findTrack(const MidiFile& midi, const std::string& name) {
    for (const auto& t : midi.tracks)
        if (trim(t.name) == name) return &t;
    return nullptr;
}

// GH1's GameGemList insert (FUN_0025b1f0 / FUN_0025b5f0) sets the HOPO flag:
// a single-fret gem that shares no fret with the previous gem and starts at
// most 169 ticks after it. GH2 makes the distance data (hopo_threshold, 170
// by default, so `gap < threshold` keeps GH1's boundary); the comparison and
// the single-fret / no-shared-fret conditions are [I] for GH2. GH2 has no
// forced-HOPO notes [C data].
void appendGem(Chart& chart, Gem g, int hopoTicks) {
    if (!chart.gems.empty()) {
        const Gem& prev = chart.gems.back();
        g.hopo = g.fretCount() == 1 && (g.frets & prev.frets) == 0 && g.tick - prev.tick < hopoTicks;
    }
    chart.gems.push_back(g);
}

// GH1's DifficultyParser (FUN_002619d0) for one difficulty of a PART track:
// base = 60 + 12 * difficulty; base+0..4 frets, base+7 Star Power,
// base+9 / base+10 face-off sections [C data: the same layout in GH2].
Chart parseDifficulty(const MidiTrack& track, int difficulty, const TempoMap& tempo, int hopoTicks) {
    Chart chart;
    struct Open { int tick = -1; uint8_t players = 0; };
    std::array<Open, 5> open;
    uint8_t playerBits = 0;
    int phraseStart = -1;
    const int base = 60 + 12 * difficulty;

    for (const MidiEvent& e : track.events) {
        if (!e.isNoteOn() && !e.isNoteOff()) continue;
        int rel = e.data1 - base;
        int tick = static_cast<int>(e.tick);
        if (rel == 7) {  // Star Power phrase
            if (e.isNoteOn()) phraseStart = tick;
            else if (phraseStart >= 0) {
                chart.phrases.push_back({phraseStart, tick});
                phraseStart = -1;
            }
            continue;
        }
        if (rel == 9 || rel == 10) {  // face-off player sections
            uint8_t bit = static_cast<uint8_t>(1 << (rel - 9));
            playerBits = e.isNoteOn() ? (playerBits | bit) : (playerBits & ~bit);
            for (Open& o : open) if (o.tick == tick) o.players = playerBits;
            continue;
        }
        if (rel < 0 || rel > 4) continue;
        if (e.isNoteOn()) {
            open[rel] = {tick, playerBits};
            continue;
        }
        int start = open[rel].tick;
        if (start < 0) continue;
        Gem g;
        g.tick = start;
        g.durationTicks = tick - start;
        g.ms = static_cast<float>(tempo.tickToMs(start));
        g.durationMs = std::trunc(static_cast<float>(tempo.tickToMs(tick)) - g.ms);
        g.noSustain = g.durationTicks < kSustainTicks;
        g.players = open[rel].players;
        for (int f = 0; f < 5; ++f) {
            if (open[f].tick >= 0 && std::abs(open[f].tick - start) <= kChordTicks) {
                g.frets |= static_cast<uint8_t>(1 << f);
                open[f] = {};
            }
        }
        appendGem(chart, g, hopoTicks);
    }

    // Gems are emitted at note-off; keep that order unless it breaks time order.
    if (!std::is_sorted(chart.gems.begin(), chart.gems.end(), [](const Gem& a, const Gem& b) { return a.tick < b.tick; }))
        std::stable_sort(chart.gems.begin(), chart.gems.end(), [](const Gem& a, const Gem& b) { return a.tick < b.tick; });

    for (size_t p = 0; p < chart.phrases.size(); ++p) {
        StarPhrase& ph = chart.phrases[p];
        for (size_t i = 0; i < chart.gems.size(); ++i) {
            Gem& g = chart.gems[i];
            if (g.tick < ph.startTick || g.tick >= ph.endTick) continue;
            g.phrase = static_cast<int>(p);
            if (ph.firstGem < 0) ph.firstGem = static_cast<int>(i);
            ph.lastGem = static_cast<int>(i);
        }
    }
    return chart;
}

}  // namespace

const char* partTrackName(Part p) {
    static const char* const names[kPartCount] = {"PART GUITAR", "PART GUITAR COOP", "PART BASS", "PART RHYTHM"};
    return names[static_cast<int>(p)];
}

Song buildSong(const MidiFile& midi, int hopoThresholdTicks) {
    Song song;
    song.tempo.build(midi);
    for (int p = 0; p < kPartCount; ++p) {
        const MidiTrack* track = findTrack(midi, partTrackName(static_cast<Part>(p)));
        song.hasPart[size_t(p)] = track != nullptr;
        if (!track) continue;
        for (int d = 0; d < kDifficultyCount; ++d)
            song.charts[size_t(p)][size_t(d)] = parseDifficulty(*track, d, song.tempo, hopoThresholdTicks);
    }
    if (!song.hasPart[0]) throw std::runtime_error("chart has no PART GUITAR track");

    uint32_t lastTick = 0;
    for (const auto& t : midi.tracks)
        if (!t.events.empty()) lastTick = std::max(lastTick, t.events.back().tick);
    if (const MidiTrack* events = findTrack(midi, "EVENTS")) {
        for (const MidiEvent& e : events->events) {
            if (e.status != 0xFF || e.data1 != 0x01) continue;
            std::string text = eventText(e.text);
            song.events.push_back({static_cast<float>(song.tempo.tickToMs(e.tick)), static_cast<int>(e.tick), text});
            if (text == "end") {
                song.endMs = static_cast<float>(song.tempo.tickToMs(e.tick));
                lastTick = e.tick;
            }
        }
    }
    if (song.endMs == 0) song.endMs = static_cast<float>(song.tempo.tickToMs(lastTick));

    for (uint32_t tick = 0; tick <= lastTick; tick += midi.division) {
        double measure = song.tempo.tickToMeasure(tick);
        song.beats.push_back({static_cast<float>(song.tempo.tickToMs(tick)),
                              std::abs(measure - std::round(measure)) < 1e-6});
    }
    return song;
}
