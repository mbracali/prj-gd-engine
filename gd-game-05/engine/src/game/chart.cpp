// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/chart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <utility>

namespace {

// Fret note-ons whose ticks differ by at most 10 form one chord gem (GH2
// 0x82344de0 [C]; RB's parser groups the same way [I]).
constexpr int kChordTicks = 10;
// A gem < 161 ticks long has no sustain [C RB 0x8249FB68: param_2 - start < 0xa1].
constexpr int kNoSustainTicks = 160;
// The parser's special pitches [C RB 0x8249F990]: 116 Overdrive phrase, 103
// solo, 120 .. 120 + num_slots - 1 the fill / Big Rock Ending lanes.
constexpr int kOverdrivePitch = 116, kSoloPitch = 103, kFillPitch = 120;
constexpr int kSlots = 5;          // num_slots [C RB 0x824A1770]
constexpr int kPlayerSlot = 9;     // player_slot [C RB 0x824A1770]
constexpr int kVocalLow = 36, kVocalHigh = 84;  // [C RB 0x824A1770: 0x24, 0x54]

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Text events are bracketed ("[end]"), some with stray spaces [C data]: trim both.
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

// HOPO marking, GH2's rule (0x8234dcc8) [C for GH2; I for RB until its
// add-gem routine is read]: a single-fret gem whose fret is not in the
// previous gem's, strictly less than the threshold after it.
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

// Spans of one pitch (note on .. note off).
std::vector<Span> spansOf(const MidiTrack& track, int pitch, const TempoMap& tempo) {
    std::vector<Span> out;
    int open = -1;
    for (const MidiEvent& e : track.events) {
        if (e.data1 != pitch || !(e.isNoteOn() || e.isNoteOff())) continue;
        if (e.isNoteOn()) {
            open = int(e.tick);
        } else if (open >= 0) {
            out.push_back({open, int(e.tick), float(tempo.tickToMs(open)), float(tempo.tickToMs(e.tick)), 0});
            open = -1;
        }
    }
    return out;
}

// The fill lanes 120..124: one span from the first lane's note on to the
// last lane's note off of a group starting together [C RB 0x8249F990: the
// lanes' bits are or-ed in +0x8c while their notes are on].
std::vector<Span> fillSpans(const MidiTrack& track, const TempoMap& tempo) {
    std::vector<Span> out;
    uint8_t on = 0;
    Span cur;
    for (const MidiEvent& e : track.events) {
        if (!(e.isNoteOn() || e.isNoteOff()) || e.data1 < kFillPitch || e.data1 >= kFillPitch + kSlots) continue;
        const uint8_t bit = uint8_t(1 << (e.data1 - kFillPitch));
        if (e.isNoteOn()) {
            if (!on) cur = {int(e.tick), int(e.tick), float(tempo.tickToMs(e.tick)), 0, 0};
            on |= bit;
            cur.lanes |= bit;
        } else if (on & bit) {
            on &= uint8_t(~bit);
            if (!on) {
                cur.endTick = int(e.tick);
                cur.endMs = float(tempo.tickToMs(e.tick));
                out.push_back(cur);
            }
        }
    }
    return out;
}

// One difficulty of a gem track: base = 60 + 12 * difficulty; base + 0..4
// the lanes, base + 9 / + 10 the player sections [C data].
Chart parseDifficulty(const MidiTrack& track, int difficulty, const TempoMap& tempo, int hopoTicks, bool drums) {
    Chart chart;
    struct Open { int tick = -1; uint8_t players = 0; };
    std::array<Open, kSlots> open;
    uint8_t playerBits = 0;
    const int base = 60 + 12 * difficulty;

    for (const MidiEvent& e : track.events) {
        if (!e.isNoteOn() && !e.isNoteOff()) continue;
        const int rel = e.data1 - base;
        const int tick = static_cast<int>(e.tick);
        if (rel == kPlayerSlot || rel == kPlayerSlot + 1) {
            const uint8_t bit = static_cast<uint8_t>(1 << (rel - kPlayerSlot));
            playerBits = e.isNoteOn() ? (playerBits | bit) : (playerBits & ~bit);
            for (Open& o : open)
                if (o.tick == tick) o.players = playerBits;
            continue;
        }
        if (rel < 0 || rel >= kSlots) continue;
        if (e.isNoteOn()) {
            open[size_t(rel)] = {tick, playerBits};
            continue;
        }
        const int start = open[size_t(rel)].tick;
        if (start < 0) continue;
        Gem g;
        g.tick = start;
        g.durationTicks = tick - start;
        g.ms = static_cast<float>(tempo.tickToMs(start));
        g.durationMs = std::trunc(static_cast<float>(tempo.tickToMs(tick)) - g.ms);
        g.noSustain = drums || g.durationTicks <= kNoSustainTicks;
        g.players = open[size_t(rel)].players;
        for (int f = 0; f < kSlots; ++f) {
            if (open[size_t(f)].tick >= 0 && std::abs(open[size_t(f)].tick - start) <= kChordTicks) {
                g.frets |= static_cast<uint8_t>(1 << f);
                open[size_t(f)] = {};
            }
        }
        chart.gems.push_back(g);
    }
    // Gems are emitted at note-off; keep that order unless it breaks time order.
    auto byTick = [](const Gem& a, const Gem& b) { return a.tick < b.tick; };
    if (!std::is_sorted(chart.gems.begin(), chart.gems.end(), byTick)) std::stable_sort(chart.gems.begin(), chart.gems.end(), byTick);
    if (!drums) markHopos(chart, hopoTicks);  // drums have no HOPOs

    for (const Span& s : spansOf(track, kOverdrivePitch, tempo)) chart.phrases.push_back({s.startTick, s.endTick, -1, -1});
    chart.solos = spansOf(track, kSoloPitch, tempo);
    chart.fills = fillSpans(track, tempo);

    // Membership: start <= gem.tick < end (GH2 0x822bc1e8 [C]; the same test for RB's spans [I]).
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
    for (size_t s = 0; s < chart.solos.size(); ++s)
        for (Gem& g : chart.gems)
            if (g.tick >= chart.solos[s].startTick && g.tick < chart.solos[s].endTick) g.solo = int(s);
    for (size_t s = 0; s < chart.fills.size(); ++s)
        for (Gem& g : chart.gems)
            if (g.tick >= chart.fills[s].startTick && g.tick <= chart.fills[s].endTick) g.fill = int(s);
    return chart;
}

// The syllable as shown [C data: the lyric suffixes on the disc are '#'
// (unpitched), '^' (unpitched, RB 0x... [I]), '-' (joined to the next), '='
// (a shown hyphen), '+' (a slide, no new syllable)].
void parseLyric(VocalNote& n) {
    std::string t = n.lyric;
    if (t == "+") {
        n.slide = true;
        n.text.clear();
        return;
    }
    while (!t.empty() && (t.back() == '#' || t.back() == '^' || t.back() == '$')) {
        if (t.back() != '$') n.unpitched = true;
        t.pop_back();
    }
    if (!t.empty() && t.back() == '-') {
        n.joinNext = true;
        t.pop_back();
    }
    for (char& c : t)
        if (c == '=') c = '-';
    n.text = t;
}

VocalTrack parseVocals(const MidiTrack& track, const TempoMap& tempo) {
    VocalTrack v;
    std::map<uint32_t, std::string> lyrics;
    for (const MidiEvent& e : track.events)
        if (e.status == 0xFF && e.data1 == 0x05) lyrics[e.tick] = e.text;
    std::array<int, 128> open;
    open.fill(-1);
    for (const MidiEvent& e : track.events) {
        if (!(e.isNoteOn() || e.isNoteOff())) continue;
        const int p = e.data1;
        if (p == 96 || p == 97) {
            if (e.isNoteOn()) v.percussion.push_back({float(tempo.tickToMs(e.tick)), int(e.tick), p == 96});
            continue;
        }
        if (p < kVocalLow || p > kVocalHigh) continue;
        if (e.isNoteOn()) {
            open[size_t(p)] = int(e.tick);
            continue;
        }
        if (open[size_t(p)] < 0) continue;
        VocalNote n;
        n.tick = open[size_t(p)];
        n.durationTicks = int(e.tick) - n.tick;
        n.ms = float(tempo.tickToMs(n.tick));
        n.durationMs = float(tempo.tickToMs(e.tick)) - n.ms;
        n.pitch = p;
        if (auto it = lyrics.find(uint32_t(n.tick)); it != lyrics.end()) n.lyric = it->second;
        parseLyric(n);
        open[size_t(p)] = -1;
        v.notes.push_back(std::move(n));
    }
    std::stable_sort(v.notes.begin(), v.notes.end(), [](const VocalNote& a, const VocalNote& b) { return a.tick < b.tick; });
    for (int marker : {105, 106})
        for (const Span& s : spansOf(track, marker, tempo))
            v.phrases.push_back({s.startTick, s.endTick, s.startMs, s.endMs, false, -1, -1, marker == 106});
    std::stable_sort(v.phrases.begin(), v.phrases.end(), [](const VocalPhrase& a, const VocalPhrase& b) { return a.startTick < b.startTick; });
    for (const Span& s : spansOf(track, kOverdrivePitch, tempo)) v.overdrive.push_back({s.startTick, s.endTick, -1, -1});
    for (size_t i = 0; i < v.phrases.size(); ++i) {
        VocalPhrase& ph = v.phrases[i];
        for (const StarPhrase& od : v.overdrive)
            if (od.startTick < ph.endTick && od.endTick > ph.startTick) ph.overdrive = true;
        for (size_t k = 0; k < v.notes.size(); ++k) {
            VocalNote& n = v.notes[k];
            if (n.tick < ph.startTick || n.tick >= ph.endTick || n.phrase >= 0) continue;
            n.phrase = int(i);
            if (ph.firstNote < 0) ph.firstNote = int(k);
            ph.lastNote = int(k);
        }
    }
    std::stable_sort(v.percussion.begin(), v.percussion.end(), [](const Percussion& a, const Percussion& b) { return a.tick < b.tick; });
    return v;
}

}  // namespace

const char* partTrackName(Part p) {
    static const char* const names[kPartCount] = {"PART GUITAR", "PART BASS", "PART DRUMS", "PART VOCALS"};
    return names[static_cast<int>(p)];
}

const char* partSymbol(Part p) {
    static const char* const names[kPartCount] = {"guitar", "bass", "drum", "vocals"};
    return names[static_cast<int>(p)];
}

Song buildSong(const MidiFile& midi, int hopoThresholdTicks) {
    Song song;
    song.tempo.build(midi);
    for (Part p : {Part::Guitar, Part::Bass, Part::Drums}) {
        const MidiTrack* track = findTrack(midi, partTrackName(p));
        song.hasPart[size_t(p)] = track != nullptr;
        if (!track) continue;
        for (int d = 0; d < kDifficultyCount; ++d)
            song.charts[size_t(p)][size_t(d)] = parseDifficulty(*track, d, song.tempo, hopoThresholdTicks, p == Part::Drums);
        if (p == Part::Drums)
            for (const MidiEvent& e : track->events) {
                if (e.status != 0xFF || e.data1 != 0x01) continue;
                const std::string t = eventText(e.text);
                int diff = 0;
                char mix[64] = {};
                if (std::sscanf(t.c_str(), "mix %d %63s", &diff, mix) == 2 && diff >= 0 && diff < kDifficultyCount)
                    song.drumMixes.push_back({int(e.tick), diff, mix});
            }
    }
    if (const MidiTrack* track = findTrack(midi, partTrackName(Part::Vocals))) {
        song.vocals = parseVocals(*track, song.tempo);
        song.hasPart[size_t(Part::Vocals)] = !song.vocals.empty();
    }
    for (const MidiTrack& tr : midi.tracks) {  // tracks named "PART..." (GH2 0x82345B78)
        if (tr.name.rfind("PART", 0) != 0) continue;
        std::map<int, uint32_t> open;
        for (const MidiEvent& e : tr.events) {
            if (e.data1 >= 36 || e.data1 <= 15 || !(e.isNoteOn() || e.isNoteOff())) continue;
            if (e.isNoteOn()) open[e.data1] = e.tick;
            else if (auto it = open.find(e.data1); it != open.end()) {
                song.bassRanges.emplace_back(float(song.tempo.tickToMs(it->second)), float(song.tempo.tickToMs(e.tick)));
                open.erase(it);
            }
        }
    }
    if (!song.hasPart[size_t(Part::Guitar)] && !song.hasPart[size_t(Part::Drums)] && !song.hasPart[size_t(Part::Vocals)])
        throw std::runtime_error("chart has no PART GUITAR, PART DRUMS or PART VOCALS track");

    uint32_t lastTick = 0;
    for (const auto& t : midi.tracks)
        if (!t.events.empty()) lastTick = std::max(lastTick, t.events.back().tick);
    if (const MidiTrack* venue = findTrack(midi, "VENUE"))
        for (const MidiEvent& e : venue->events) {
            if (e.isNoteOn()) song.venueNotes.push_back({float(song.tempo.tickToMs(e.tick)), int(e.tick), VenueNote::Track::Venue, e.data1});
            else if (e.status == 0xFF && e.data1 == 0x01)
                song.venueEvents.push_back({float(song.tempo.tickToMs(e.tick)), int(e.tick), eventText(e.text)});
        }
    if (const MidiTrack* beat = findTrack(midi, "BEAT"))
        for (const MidiEvent& e : beat->events)
            if (e.isNoteOn()) song.venueNotes.push_back({float(song.tempo.tickToMs(e.tick)), int(e.tick), VenueNote::Track::Beat, e.data1});
    std::stable_sort(song.venueNotes.begin(), song.venueNotes.end(), [](const VenueNote& a, const VenueNote& b) { return a.tick < b.tick; });
    if (const MidiTrack* events = findTrack(midi, "EVENTS")) {
        for (const MidiEvent& e : events->events) {
            if (e.status != 0xFF || e.data1 != 0x01) continue;
            std::string text = eventText(e.text);
            song.events.push_back({static_cast<float>(song.tempo.tickToMs(e.tick)), static_cast<int>(e.tick), text});
            if (text == "end") {
                song.endMs = static_cast<float>(song.tempo.tickToMs(e.tick));
                lastTick = e.tick;
            } else if (text == "coda") {
                song.codaTick = int(e.tick);
            }
        }
    }
    if (song.endMs == 0) song.endMs = static_cast<float>(song.tempo.tickToMs(lastTick));

    // The beat lines: the BEAT track's notes (12 downbeat, 13 beat) [C data],
    // else every quarter note.
    bool haveBeatTrack = false;
    for (const VenueNote& n : song.venueNotes)
        if (n.track == VenueNote::Track::Beat && (n.pitch == 12 || n.pitch == 13)) {
            song.beats.push_back({n.ms, n.pitch == 12});
            haveBeatTrack = true;
        }
    if (!haveBeatTrack)
        for (uint32_t tick = 0; tick <= lastTick; tick += midi.division) {
            double measure = song.tempo.tickToMeasure(tick);
            song.beats.push_back({static_cast<float>(song.tempo.tickToMs(tick)), std::abs(measure - std::round(measure)) < 1e-6});
        }
    return song;
}
