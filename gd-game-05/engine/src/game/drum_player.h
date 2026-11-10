// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <vector>

#include "game/chart.h"
#include "game/rules.h"

// Rock Band's drums, the judge (rendering-free, L17). A chart gem holds the
// lanes struck together (kick, red, yellow, blue, green); each lane is a
// note of its own: hit when its pad is struck inside the watcher's slop
// window (beatmatcher.dtb (watcher (slop 100)) [C data]), missed when the
// window passes. Scoring: (points (drum (head 25))) per note, the streak
// multiplier (singleplayer table), Overdrive from phrases (star_phrase)
// and deployed by a fill's last (green / crash) lane when ready
// (crash_fill_ready_hits) [C data]; how RB's drum watcher pairs strikes and
// notes, and what an extra strike costs, are [I] (docs/FINDINGS.md "Drums").
class DrumPlayer {
public:
    struct Event {
        enum class Kind { Hit, Miss, Overhit, PhraseCaptured, PhraseMissed, OverdriveReady, OverdriveStart, OverdriveEnd, FillStart };
        Kind kind;
        int gem;
        int lane;
    };

    DrumPlayer(const Song& song, Difficulty difficulty, const Rules& rules, bool noFail, bool noKick = false);

    // A pad (0 kick, 1 red, 2 yellow, 3 blue, 4 green) struck at song time t.
    void strike(int lane, float t);
    void update(float t);

    const Chart& chart() const { return chart_; }
    int notes() const { return notes_; }  // lane notes in the chart (the kick's left out with noKick)
    int notesHit() const { return hits_; }
    bool gemHit(int gem) const;
    int score() const { return score_; }
    int streak() const { return streak_; }
    int multiplier() const;
    float overdrive() const { return meter_; }
    bool overdriveActive() const { return active_; }
    bool failed() const { return failed_; }
    std::vector<Event> takeEvents();

private:
    bool counts(int lane) const { return !(noKick_ && lane == 0); }
    void hit(int gem, int lane, float t);
    void miss(int gem, int lane);

    const Song& song_;
    const Chart& chart_;
    const Rules& rules_;
    bool noFail_, noKick_;
    std::vector<uint8_t> hitLanes_, missedLanes_;
    std::vector<uint8_t> phraseBroken_;
    size_t sweep_ = 0;
    int notes_ = 0, hits_ = 0, score_ = 0, streak_ = 0;
    float meter_ = 0;
    bool active_ = false, failed_ = false;
    float activeUntil_ = 0;
    std::vector<Event> events_;
};
