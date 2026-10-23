// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/chart.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

constexpr int kChordTicks = 10;      // fret note-ons this close form one chord
constexpr int kSustainTicks = 161;   // shorter gems have no sustain
constexpr int kHopoTicks = 169;      // HOPO if the gap to the previous gem is <= this

const MidiTrack* findTrack(const MidiFile& midi, const std::string& suffix) {
    for (const auto& t : midi.tracks) {
        if (t.name.size() >= suffix.size() && t.name.compare(t.name.size() - suffix.size(), suffix.size(), suffix) == 0)
            return &t;
    }
    return nullptr;
}

// GameGemList insert (FUN_0025b1f0 / FUN_0025b5f0): sets the HOPO flag.
void appendGem(Chart& chart, Gem g) {
    if (!chart.gems.empty()) {
        const Gem& prev = chart.gems.back();
        g.hopo = g.fretCount() == 1 && (g.frets & prev.frets) == 0 && g.tick - prev.tick <= kHopoTicks;
    }
    chart.gems.push_back(g);
}

// DifficultyParser for one difficulty of the GEMS track (FUN_002619d0).
Chart parseDifficulty(const MidiTrack& track, int difficulty, const TempoMap& tempo) {
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
        appendGem(chart, g);
    }

    // The game assumes time order; keep emission order but guard against oddities.
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

Song buildSong(const MidiFile& midi) {
    Song song;
    song.tempo.build(midi);
    const MidiTrack* gems = findTrack(midi, "GEMS");
    if (!gems) throw std::runtime_error("chart has no GEMS track");
    for (int d = 0; d < kDifficultyCount; ++d) song.charts[d] = parseDifficulty(*gems, d, song.tempo);

    uint32_t lastTick = 0;
    for (const auto& t : midi.tracks)
        if (!t.events.empty()) lastTick = std::max(lastTick, t.events.back().tick);
    if (const MidiTrack* events = findTrack(midi, "EVENTS")) {
        for (const MidiEvent& e : events->events) {
            if (e.status == 0xFF && e.data1 == 0x01)
                song.events.push_back({static_cast<float>(song.tempo.tickToMs(e.tick)), static_cast<int>(e.tick), e.text});
            if (e.status == 0xFF && e.data1 == 0x01 && e.text == "end") {
                song.endMs = static_cast<float>(song.tempo.tickToMs(e.tick));
                lastTick = e.tick;
            }
        }
    }
    if (song.endMs == 0) song.endMs = static_cast<float>(song.tempo.tickToMs(lastTick));

    // GuitarAnimInfo mode 2 (FUN_00115d68): T1 GEMS notes > 107 paired on note-off; 108 = singer "open".
    if (const MidiTrack* t1 = findTrack(midi, "T1 GEMS")) {
        int64_t on = -1;
        for (const MidiEvent& e : t1->events) {
            if (e.data1 != 108 || (e.status & 0xF0) < 0x80 || (e.status & 0xF0) > 0x90) continue;
            if (e.isNoteOn()) on = e.tick;
            else if (on >= 0) {
                song.singerEvents.push_back({float(song.tempo.tickToMs(on)), float(song.tempo.tickToMs(e.tick))});
                on = -1;
            }
        }
    }

    // SongDB (FUN_00106348): TRIGGERS note-ons become arena messages, sent `lead` ms early.
    if (const MidiTrack* tr = findTrack(midi, "TRIGGERS")) {
        int64_t lastTick = -1, lastNote = -1;
        for (const MidiEvent& e : tr->events) {
            if (!e.isNoteOn()) continue;
            if (int64_t(e.tick) == lastTick && e.data1 == lastNote) continue;  // duplicate
            lastTick = e.tick, lastNote = e.data1;
            const char* name = e.data1 == 60 ? "kick_drum" : e.data1 == 61 ? "bass_hit" : nullptr;
            if (!name) continue;
            double ms = song.tempo.tickToMs(e.tick) - (e.data1 == 60 ? 90.0 : 50.0);
            song.triggers.push_back({static_cast<float>(ms), static_cast<int>(song.tempo.msToTick(ms)), name});
        }
        std::sort(song.triggers.begin(), song.triggers.end(), [](const SongEvent& a, const SongEvent& b) { return a.ms < b.ms; });
    }

    // GuitarAnimInfo_v4/v5: ANIM notes 40..59 (hand_events fret_pos) are
    // emitted at note-off; HandMap_ / StrumMap_ texts may sit on any track.
    float division = static_cast<float>(midi.division);
    for (const MidiTrack& t : midi.tracks) {
        bool anim = t.name == "ANIM";
        std::array<int64_t, 128> on{};
        on.fill(-1);
        for (const MidiEvent& e : t.events) {
            if (e.status == 0xFF && e.data1 == 0x01) {
                if (e.text.rfind("HandMap_", 0) == 0) song.handMaps.push_back({float(e.tick) / division, e.text});
                if (e.text.rfind("StrumMap_", 0) == 0) song.strumMaps.push_back({float(e.tick) / division, e.text});
                continue;
            }
            if (!anim || e.data1 >= 128) continue;
            if (e.isNoteOn()) on[e.data1] = e.tick;
            else if (e.isNoteOff() && on[e.data1] >= 0) {
                if (e.data1 >= 40 && e.data1 < 60)
                    song.fretPositions.push_back({float(on[e.data1]) / division, float(e.tick) / division, e.data1 - 40});
                on[e.data1] = -1;
            }
        }
    }

    for (uint32_t tick = 0; tick <= lastTick; tick += midi.division) {
        double measure = song.tempo.tickToMeasure(tick);
        song.beats.push_back({static_cast<float>(song.tempo.tickToMs(tick)),
                              std::abs(measure - std::round(measure)) < 1e-6});
    }
    return song;
}
