// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/selftest_play.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "game/autoplay.h"
#include "game/chart.h"
#include "game/match.h"
#include "game/note_track.h"
#include "game/player.h"
#include "game/tuning.h"
#include "game/song_db.h"
#include "script/globals.h"
#include "ui/song_screen.h"

namespace {

// Plays one part / difficulty at 60 Hz from 2 s before the first note to 3 s
// after the last; true when every note was hit with no overstrum.
bool playOnce(const NoteTrack& track, const Tuning& tu, int diff, bool tap, PlayTotals& t, std::string& why) {
    Player p(track, tu, diff, false);
    Autoplay ap(track, tap);
    if (track.notes.empty()) return true;
    const double frame = 1000.0 / 60.0;
    const double start = track.notes.front().time - 2000.0, end = track.notes.back().time + track.notes.back().length + 3000.0;
    p.setSongLengthMs(end);
    for (double now = start; now < end; now += frame) p.frame(now, frame, ap.input(now, frame, p));
    const PlayerStats& s = p.stats();
    if (s.notesHit != int(track.notes.size()) || s.misses || s.overstrums || s.failed) {
        why = std::string(tap ? "tapping HOPOs" : "strumming") + ": hit " + std::to_string(s.notesHit) + " of " +
              std::to_string(track.notes.size()) + ", " + std::to_string(s.misses) + " missed, " + std::to_string(s.overstrums) +
              " overstrums";
        if (std::getenv("GD04_SELFTEST_TRACE")) {
            int first = -1;
            for (const PlayerEvent& e : p.events())
                if (e.kind == PlayerEvent::Kind::Miss || e.kind == PlayerEvent::Kind::Overstrum) { first = e.note; break; }
            for (const PlayerEvent& e : p.events())
                if (e.note >= first - 3 && e.note <= first + 3)
                    std::printf("  trace: event %d note %d at %.1f ms\n", int(e.kind), e.note, e.atMs);
        }
        for (const PlayerEvent& e : p.events())
            if (e.kind == PlayerEvent::Kind::Miss || e.kind == PlayerEvent::Kind::Overstrum) {
                why += ", first at note " + std::to_string(e.note) + " (" + std::to_string(e.note >= 0 && size_t(e.note) < track.notes.size() ? track.notes[size_t(e.note)].time : -1) + " ms)";
                break;
            }
        return false;
    }
    if (tap) t.hopos += size_t(s.hopoHits);
    else t.score += s.score;
    t.phrasesCaptured += size_t(s.phrasesCaptured);
    return true;
}

// #69: Enhanced > Audio / Video Calibration with a large heard delay
// (wireless headphones, a soundbar). The judge's input offset is the
// song's - (lag_calibration + the audio delay), as the app sets it; the
// notes are played when they are heard (note time + the audio delay):
// every note hit, and the highway drawn at the song time - the audio delay
// + the video delay (the note on the line when heard). No delay: no change.
// Played on the chart's time under +180 ms, notes are missed.
int playLate(const NoteTrack& track, const Tuning& tu, double audioMs, double playedLateMs) {
    Player p(track, tu, 3, true, float(0.0 - audioMs));
    Autoplay ap(track, false);
    const double frame = 1000.0 / 60.0;
    const double start = track.notes.front().time - 2000.0, end = track.notes.back().time + track.notes.back().length + 3000.0 + audioMs;
    p.setSongLengthMs(end);
    for (double now = start; now < end; now += frame) p.frame(now, frame, ap.input(now - playedLateMs, frame, p));
    const PlayerStats& s = p.stats();
    return s.misses || s.overstrums ? -1 : s.notesHit;
}

bool calibration(const NoteTrack& track, const Tuning& tu, PlayTotals& t, std::string& why) {
    if (track.notes.empty()) return true;
    const int total = int(track.notes.size());
    struct Case { double audio, video; } cases[] = {{0, 0}, {180, 0}, {180, 60}, {-60, 0}, {300, 100}};
    for (const Case& c : cases) {
        ++t.calibrationRuns;
        SongSetup setup;
        setup.audioOffsetMs = c.audio, setup.videoOffsetMs = c.video;
        const double gem = track.notes[track.notes.size() / 2].time;
        const double drawn = (gem + c.audio) + setup.drawOffsetMs();  // the highway's time when the note is heard
        const int hit = playLate(track, tu, c.audio, c.audio);
        if (hit != total || std::abs(drawn - (gem + c.video)) > 0.01) {
            ++t.calibrationFails;
            why = "calibration audio " + std::to_string(int(c.audio)) + " ms, video " + std::to_string(int(c.video)) + " ms: hit " +
                  std::to_string(hit) + " of " + std::to_string(total) + ", drawn " + std::to_string(drawn - gem) + " ms off";
            return false;
        }
    }
    ++t.calibrationRuns;
    if (playLate(track, tu, 180, 0) == total || SongSetup{}.drawOffsetMs() != 0.0) {
        ++t.calibrationFails;
        why = "calibration: +180 ms played on the chart's time still hit every note";
        return false;
    }
    return true;
}

// GD04-108: autoplay holds the first sustain with at least 400 ms of hold
// and no note within check_time_early of its middle; one extra strum there
// is an overstrum that ends the sustain (0x829E4C80 -> 0x8211C9D0's
// released path [C 0x8211FA14, 0x8211C9D0]): one overstrum, the note's
// SustainLost, no sustain held after it and no score until the next note.
// True when the chart has no such sustain or the cut behaves.
bool overstrumCut(const NoteTrack& track, const Tuning& tu, PlayTotals& t, std::string& why) {
    const double E = tu.checkTimeEarly * 1000.0;
    int target = -1;
    double strumAt = 0;
    for (size_t i = 0; i + 1 < track.notes.size(); ++i) {
        const TrackNote& n = track.notes[i];
        if (!n.sustain || n.holdMs < 400) continue;
        const double mid = n.time + n.holdMs / 2.0;
        if (double(track.notes[i + 1].time) - mid <= E + 100) continue;
        target = int(i), strumAt = mid;
        break;
    }
    if (target < 0) return true;
    Player p(track, tu, 3, true);
    Autoplay ap(track, false);
    const double frame = 1000.0 / 60.0;
    const double nextAt = track.notes[size_t(target) + 1].time;
    p.setSongLengthMs(nextAt + 1000);
    bool injected = false, held = false;
    long long scoreAfter = -1;
    for (double now = track.notes.front().time - 2000.0; now < nextAt - E - 2 * frame; now += frame) {
        FrameInput in = ap.input(now, frame, p);
        if (!injected && now >= strumAt) {
            held = p.sustaining() == target;
            in.strum = true, injected = true;
        }
        p.frame(now, frame, in);
        if (injected && scoreAfter < 0) scoreAfter = p.stats().score;
    }
    bool lost = false;
    for (const PlayerEvent& e : p.events()) lost = lost || (e.kind == PlayerEvent::Kind::SustainLost && e.note == target);
    ++t.overstrumCuts;
    if (held && p.stats().overstrums == 1 && lost && p.sustaining() < 0 && p.stats().score == scoreAfter) return true;
    ++t.overstrumCutFails;
    why = "overstrum during the sustain of note " + std::to_string(target) + ": held " + std::to_string(held) + ", overstrums " +
          std::to_string(p.stats().overstrums) + ", lost " + std::to_string(lost) + ", still held " + std::to_string(p.sustaining() >= 0) +
          ", score " + std::to_string(scoreAfter) + " -> " + std::to_string(p.stats().score);
    return false;
}

// GD04-112: strums before the first note's window (the highway's intro)
// are nothing: no overstrum, no health lost; the first note is still hit.
bool strumBeforeFirst(const NoteTrack& track, const Tuning& tu, std::string& why) {
    if (track.notes.empty()) return true;
    const double E = tu.checkTimeEarly * 1000.0, first = track.notes.front().time;
    Player p(track, tu, 3, true);
    Autoplay ap(track, false);
    const double frame = 1000.0 / 60.0;
    p.setSongLengthMs(first + 2000);
    int strums = 0;
    for (double now = first - 3000.0; now < first + 500.0; now += frame) {
        FrameInput in = ap.input(now, frame, p);
        if (now < first - E - 2 * frame && int((now - first) / frame) % 10 == 0) in.strum = true, ++strums;
        p.frame(now, frame, in);
    }
    if (strums > 0 && p.stats().overstrums == 0 && p.stats().notesHit >= 1) return true;
    why = "strums before the first note: " + std::to_string(strums) + " strums, " + std::to_string(p.stats().overstrums) + " overstrums, " +
          std::to_string(p.stats().notesHit) + " hit";
    return false;
}

// Enhanced > Accessibility > Easy Chords (GDALL-039): the first chord with
// room around it, strummed with `frets` held. Off (the default, as the
// original) one fret of the chord misses; on, any part of it hits and scores
// as the whole chord, a fret outside it misses, and the chord's sustain lasts
// on one of its frets. True when the chart has no such chord or every case holds.
bool easyChords(const NoteTrack& track, const Tuning& tu, PlayTotals& t, std::string& why) {
    int target = -1;
    for (size_t i = 1; i + 1 < track.notes.size(); ++i) {
        const TrackNote& n = track.notes[i];
        if (!n.chord() || n.hopo || n.frets == 0x1f || n.time - track.notes[i - 1].time < 400 || track.notes[i + 1].time - n.time < 400 ||
            (n.sustain && track.notes[i + 1].time - (n.time + n.holdMs) < 200))
            continue;
        target = int(i);
        break;
    }
    if (target < 0) return true;
    const TrackNote& n = track.notes[size_t(target)];
    const double frame = 1000.0 / 60.0, at = n.time;
    uint8_t low = 0;
    for (int b = 0; b < 5 && !low; ++b)
        if (n.frets & (1u << b)) low = uint8_t(1u << b);
    uint8_t outside = 0;
    for (int b = 0; b < 5 && !outside; ++b)
        if (!(n.frets & (1u << b))) outside = uint8_t(1u << b);
    struct Run { bool hit; long long score; };
    // `release`: the frets let go after 50 ms (the sustain's test).
    auto run = [&](bool easy, uint8_t frets, uint8_t release = 0) {
        const bool keep = Player::easyChords;
        Player::easyChords = easy;
        Player p(track, tu, 3, true);
        p.setSongLengthMs(at + 4000);
        p.startAt(at - 300);  // the notes before it are not played
        bool strummed = false;
        const double end = n.sustain ? at + n.holdMs + 50 : at + 300;
        for (double now = at - 600; now < end; now += frame) {
            FrameInput in;
            if (now >= at - 50) in.frets = frets;
            if (release && now >= at + 50) in.frets = uint8_t(frets & ~release);
            if (!strummed && now >= at) in.strum = true, strummed = true;
            p.frame(now, frame, in);
        }
        Player::easyChords = keep;
        bool hit = false;
        for (const PlayerEvent& e : p.events()) hit = hit || (e.kind == PlayerEvent::Kind::Hit && e.note == target);
        return Run{hit, (long long)p.stats().score};
    };
    std::string bad;
    const Run full = run(false, n.frets), offOne = run(false, low), onFull = run(true, n.frets), onOne = run(true, low);
    if (!full.hit) bad += "off: the whole chord missed; ";
    if (offOne.hit) bad += "off: one fret of the chord hit; ";
    if (!onFull.hit || onFull.score != full.score) bad += "on: the whole chord changed; ";
    if (!onOne.hit || onOne.score != full.score) bad += "on: one fret did not hit as the whole chord; ";
    if (run(true, uint8_t(low | outside)).hit) bad += "on: a fret outside the chord hit; ";
    if (run(true, outside).hit) bad += "on: only a fret outside the chord hit; ";
    if (n.sustain && n.holdMs >= 300) {
        const Run cutOn = run(true, n.frets, uint8_t(n.frets & ~low)), cutOff = run(false, n.frets, uint8_t(n.frets & ~low));
        if (cutOn.score != full.score || cutOff.score >= full.score) bad += "on: the sustain did not last on one of its frets; ";
    }
    {
        // Frets that strum (a joypad, Require Strum off): the chord's first fret
        // hits it, the next fret of it pressed two frames later is no overstrum.
        const bool keep = Player::easyChords;
        Player::easyChords = true;
        Player p(track, tu, 3, true);
        p.setSongLengthMs(at + 4000);
        p.startAt(at - 300);
        uint8_t second = uint8_t(n.frets & ~low);
        second = uint8_t(second & (~second + 1));
        uint8_t held = 0;
        int step = 0;
        for (double now = at - 600; now < at + 300; now += frame) {
            FrameInput in;
            bool press = false;
            if (now >= at - 10 && step == 0) held |= low, press = true, step = 1;
            else if (step >= 1 && step < 3) ++step;
            else if (step == 3) held |= second, press = true, step = 4;
            in.frets = held, in.strum = press, in.fretStrum = press;
            p.frame(now, frame, in);
        }
        Player::easyChords = keep;
        bool hit = false;
        for (const PlayerEvent& e : p.events()) hit = hit || (e.kind == PlayerEvent::Kind::Hit && e.note == target);
        if (!hit || p.stats().overstrums != 0) bad += "on, frets that strum: the chord's next fret was an overstrum; ";
    }
    ++t.easyChordRuns;
    if (bad.empty()) return true;
    ++t.easyChordFails;
    why = "easy chords (note " + std::to_string(target) + "): " + bad;
    return false;
}

}  // namespace

bool playChart(const Globals& g, const SongInfo& song, const Chart& chart, PlayTotals& t, std::string& why) {
    Tuning tu;
    if (!t.tuningLoaded) {
        if (!tu.load(g)) return why = "the gameplay tuning is incomplete", false;
    }
    tu.load(g);
    t.rulesImplemented = true;
    const float scale = song.hammerOnMeasureScale > 0 ? song.hammerOnMeasureScale : tu.defaultHammerOnMeasureScale;
    bool ok = true;
    for (size_t p = 0; p < size_t(Part::Count); ++p) {
        for (int d = 0; d < kDifficulties; ++d) {
            const auto& notes = chart.parts[p].notes[size_t(d)];
            if (notes.empty()) continue;
            ++t.charts;
            NoteTrack track = buildTrack(chart, Part(p), d, tu, scale);
            for (const TrackNote& n : track.notes) {
                ++t.notes;
                if (n.chord()) ++t.chords;
                if (n.hopo) ++t.hopoNotes;
                if (n.sustain) ++t.sustains;
            }
            for (const ChartNote& n : notes)
                if (n.flag20) ++t.flagged;
            t.phrases += chart.parts[p].star[size_t(d)].size();
            for (bool tap : {false, true}) {
                std::string w;
                if (!playOnce(track, tu, d, tap, t, w)) {
                    if (ok) why = std::string(partName(Part(p))) + " " + difficultyName(d) + ": " + w;
                    ok = false;
                    ++t.failedCharts;
                }
            }
        }
    }
    // GD04-108: an overstrum while a sustain is held ends it (guitar expert).
    if (!chart.notes(Part::Guitar, 3).empty()) {
        std::string w;
        if (!overstrumCut(buildTrack(chart, Part::Guitar, 3, tu, scale), tu, t, w)) {
            if (ok) why = "guitar expert: " + w;
            ok = false;
        }
    }
    if (!chart.notes(Part::Guitar, 3).empty()) {
        std::string w;
        if (!strumBeforeFirst(buildTrack(chart, Part::Guitar, 3, tu, scale), tu, w)) {
            if (ok) why = "guitar expert: " + w;
            ok = false;
        }
    }
    if (!chart.notes(Part::Guitar, 3).empty()) {
        std::string w;
        if (!calibration(buildTrack(chart, Part::Guitar, 3, tu, scale), tu, t, w)) {
            if (ok) why = "guitar expert: " + w;
            ok = false;
        }
    }
    if (!chart.notes(Part::Guitar, 3).empty()) {
        std::string w;
        if (!easyChords(buildTrack(chart, Part::Guitar, 3, tu, scale), tu, t, w)) {
            if (ok) why = "guitar expert: " + w;
            ok = false;
        }
    }
    // Face-off: each player's notes (guitar, every difficulty) [C 0x82104558].
    for (int d = 0; d < kDifficulties; ++d) {
        if (chart.notes(Part::Guitar, d).empty() || chart.faceOffP1.empty()) continue;
        NoteTrack full = buildTrack(chart, Part::Guitar, d, tu, scale);
        for (const auto* sections : {&chart.faceOffP1, &chart.faceOffP2}) {
            NoteTrack fo = faceOffTrack(full, *sections);
            t.faceOffNotes += fo.notes.size();
            std::string w;
            if (!playOnce(fo, tu, d, false, t, w)) {
                if (ok) why = std::string("face-off ") + difficultyName(d) + ": " + w;
                ok = false;
                ++t.failedCharts;
            }
        }
    }
    // Co-op: lead and bass / rhythm, both autoplayed, at every difficulty;
    // every note of both hit, the shared Star Power raised [C docs/modes.md].
    for (int d = 0; d < kDifficulties; ++d) {
        MatchSetup ms;
        ms.mode = GameMode::Coop, ms.players = 2;
        ms.part = {Part::Guitar, Part::Rhythm};
        ms.difficulty = {d, d};
        ms.autoplay = {true, true};
        ms.hopoScale = scale;
        ms.useCoopTracks = song.useCoopNotetracks;
        Match m;
        std::string err;
        if (chart.notes(Part::Guitar, d).empty() || (chart.notes(Part::Rhythm, d).empty() && chart.notes(Part::RhythmCoop, d).empty()) ||
            !m.start(chart, tu, &g, ms, 0, err))
            continue;
        ++t.coopCharts;
        const double frame = 1000.0 / 60.0, end = m.lastNoteEndMs() + 3000.0;
        double start = 1e18;
        for (int i = 0; i < 2; ++i)
            if (!m.track(i).notes.empty()) start = std::min(start, double(m.track(i).notes.front().time) - 2000.0);
        for (double now = start; now < end; now += frame) {
            m.frame(now, frame, {});
            m.clearEvents();
        }
        bool full = m.outcome() == Match::Outcome::None;
        for (int i = 0; i < 2; ++i) {
            const PlayerStats& st = m.player(i).stats();
            t.coopNotes += m.track(i).notes.size();
            full = full && st.misses == 0 && st.overstrums == 0;
        }
        // Both players' counters rise together [C 0x8210BB40]: each counted every note.
        const int both = int(m.track(0).notes.size() + m.track(1).notes.size());
        full = full && m.player(0).stats().notesHit == both && m.player(1).stats().notesHit == both;
        t.coopStarPower += size_t(m.counters().coopActivations);
        if (!full) {
            if (ok) why = std::string("co-op ") + difficultyName(d) + ": notes missed (" + std::to_string(m.player(0).stats().notesHit) + " / " +
                          std::to_string(both) + ")";
            ok = false;
            ++t.failedCharts;
        }
    }
    // Battle (expert, both autoplayed): cards, attacks, the KO or sudden death.
    if (!chart.notes(Part::Guitar, 3).empty()) {
        MatchSetup ms;
        ms.mode = GameMode::Battle, ms.players = 2;
        ms.autoplay = {true, true};
        ms.hopoScale = scale;
        Match m;
        std::string err;
        if (m.start(chart, tu, &g, ms, 0, err)) {
            ++t.battles;
            const double frame = 1000.0 / 60.0, end = m.lastNoteEndMs() + 3000.0;
            for (double now = -2000; now < end && m.outcome() == Match::Outcome::None; now += frame) {
                m.frame(now, frame, {});
                m.clearEvents();
            }
            t.battleAttacks += size_t(m.counters().fired[0] + m.counters().fired[1]);
            if (m.outcome() != Match::Outcome::None) ++t.battleKos;
        }
    }
    return ok;
}

void printPlayTotals(const PlayTotals& t) {
    std::printf("charts: %zu, notes: %zu, chords: %zu, sustains: %zu, HOPO notes: %zu, flagged (0x20): %zu, star phrases: %zu\n",
                t.charts, t.notes, t.chords, t.sustains, t.hopoNotes, t.flagged, t.phrases);
    std::printf("face-off: %zu notes split between the players\n", t.faceOffNotes);
    std::printf("two players: co-op %zu charts (%zu notes, Star Power raised together %zu times), battles %zu (%zu attacks, %zu KOs)\n",
                t.coopCharts, t.coopNotes, t.coopStarPower, t.battles, t.battleAttacks, t.battleKos);
    std::printf("overstrum during a sustain: %zu sustains cut, %zu not ended\n", t.overstrumCuts, t.overstrumCutFails);
    std::printf("calibration (#69): %zu guitar expert runs played when heard, %zu failed\n", t.calibrationRuns, t.calibrationFails);
    std::printf("easy chords (GDALL-039): %zu guitar expert chords checked off and on, %zu failed\n", t.easyChordRuns, t.easyChordFails);
    std::printf("autoplay: score (strummed) %lld, HOPO landings %zu, phrases captured %zu, charts failing %zu\n",
                static_cast<long long>(t.score), t.hopos, t.phrasesCaptured, t.failedCharts);
}
