// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/selftest_play.h"

#include <algorithm>
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
    std::printf("autoplay: score (strummed) %lld, HOPO landings %zu, phrases captured %zu, charts failing %zu\n",
                static_cast<long long>(t.score), t.hopos, t.phrasesCaptured, t.failedCharts);
}
