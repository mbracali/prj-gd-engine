// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/chart.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <stdexcept>

namespace {

// GH2 0x82344de0 [C]: fret note-ons whose ticks differ by at most 10
// (|Δ| < 11) form one chord gem.
constexpr int kChordTicks = 10;
// GH2 0x82345480 [C]: a gem <= 160 ticks long (< 0xa1) has no sustain. The
// parser's ignore_durations flag (every gem without sustain) is not set by
// any GH2 guitar parser, so it is not modelled [I].
constexpr int kNoSustainTicks = 160;

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

// GH2 HOPO marking, 0x8234dcc8 (called by the add-gem routine 0x8234e190) [C]:
//   hopo(new) = list not empty
//            && (prev.fret_mask & new.fret_mask) == 0   (prev = the last gem added; a chord is one gem)
//            && popcount(new.fret_mask) == 1           (chords are never HOPOs)
//            && new.tick - prev.tick < hopo_threshold   (strict: blt at 0x8234dd38)
// The threshold is the song's, else beatmatcher's (0x823341d0) [C]; it does
// not depend on the difficulty [C]. GH2 has no forced HOPO notes [C].
// Face-off: the PS2 keeps one gem list per difficulty and track, not per
// player. Each new track adds a list to every difficulty (0x002447a8); the
// parser hands each gem to the sink with its difficulty index (0x00245f28,
// from the pitch through 0x00246668), and the sink picks the difficulty's
// lists (0x002448d0) and then the gem's track (gem +0, 0x0024bba8). Every gem
// goes into the list, whatever its player bits, and the HOPO test
// (0x0024d3a0) never reads them [C]. Both face-off players play the same
// track, so "prev" is the previous gem of the whole chart, including the
// other player's gems, which is what this does.
void markHopos(Chart& chart, int hopoTicks) {
    for (size_t i = 0; i < chart.gems.size(); ++i) {
        Gem& g = chart.gems[i];
        if (i == 0) {
            g.hopo = false;
            continue;
        }
        const Gem& prev = chart.gems[i - 1];
        g.hopo = (prev.frets & g.frets) == 0 && g.fretCount() == 1 && g.tick - prev.tick < hopoTicks;
    }
}

// One difficulty of a PART track: base = 60 + 12 * difficulty; base+0..4
// frets, base+7 Star Power, base+9 / base+10 face-off sections [C data: the
// same layout as GH1]. The face-off section handling is GH1's (FUN_002619d0)
// [I for GH2, FINDINGS open question 3].
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
        g.noSustain = g.durationTicks <= kNoSustainTicks;  // 0x82345480 [C]
        g.players = open[rel].players;
        for (int f = 0; f < 5; ++f) {
            if (open[f].tick >= 0 && std::abs(open[f].tick - start) <= kChordTicks) {
                g.frets |= static_cast<uint8_t>(1 << f);
                open[f] = {};
            }
        }
        chart.gems.push_back(g);
    }

    // Gems are emitted at note-off; keep that order unless it breaks time order.
    if (!std::is_sorted(chart.gems.begin(), chart.gems.end(), [](const Gem& a, const Gem& b) { return a.tick < b.tick; }))
        std::stable_sort(chart.gems.begin(), chart.gems.end(), [](const Gem& a, const Gem& b) { return a.tick < b.tick; });
    // The game adds gems in time order; mark HOPOs in that order.
    markHopos(chart, hopoTicks);

    // Phrase membership, 0x822bc1e8 [C]: phrase.start <= gem.tick < phrase.end.

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
        for (const MidiEvent& e : track->events)
            if (e.isNoteOn() && e.data1 == 110) song.bigNotes[size_t(p)].push_back(int(e.tick));
    }
    song.bigNotes[size_t(Part::GuitarCoop)] = song.bigNotes[size_t(Part::Guitar)];
    if (!song.hasPart[0]) throw std::runtime_error("chart has no PART GUITAR track");

    uint32_t lastTick = 0;
    for (const auto& t : midi.tracks)
        if (!t.events.empty()) lastTick = std::max(lastTick, t.events.back().tick);
    // The band and venue notes the world plays (VenueNote).
    const std::pair<const char*, VenueNote::Track> venueTracks[] = {
        {"BAND DRUMS", VenueNote::Track::Drums}, {"BAND BASS", VenueNote::Track::Bass}, {"TRIGGERS", VenueNote::Track::Triggers}};
    for (const auto& [name, kind] : venueTracks)
        if (const MidiTrack* t = findTrack(midi, name))
            for (const MidiEvent& e : t->events) {
                if (!e.isNoteOn()) continue;
                const int p = e.data1;
                const bool used = kind == VenueNote::Track::Triggers ? (p >= 48 && p <= 50) || p == 52
                                  : kind == VenueNote::Track::Drums  ? p == 36 || p == 37
                                                                     : p == 36;
                if (used) song.venueNotes.push_back({static_cast<float>(song.tempo.tickToMs(e.tick)), static_cast<int>(e.tick), kind, p});
            }
    std::stable_sort(song.venueNotes.begin(), song.venueNotes.end(), [](const VenueNote& a, const VenueNote& b) { return a.tick < b.tick; });
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
