// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/selftest_play.h"

#include <cstdio>
#include <cstdlib>

#include "game/autoplay.h"
#include "game/chart.h"
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
    return ok;
}

void printPlayTotals(const PlayTotals& t) {
    std::printf("charts: %zu, notes: %zu, chords: %zu, sustains: %zu, HOPO notes: %zu, flagged (0x20): %zu, star phrases: %zu\n",
                t.charts, t.notes, t.chords, t.sustains, t.hopoNotes, t.flagged, t.phrases);
    std::printf("autoplay: score (strummed) %lld, HOPO landings %zu, phrases captured %zu, charts failing %zu\n",
                static_cast<long long>(t.score), t.hopos, t.phrasesCaptured, t.failedCharts);
}
