// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "game/chart.h"

// Guitar Hero: Metallica (gh4) drums: a rendering-free judge, adapted from
// gd05's Rock Band DrumPlayer to gh4's chart. gh4 drums are a first-class
// controller in the original (descriptor type 10 GH / 11 RB drum kit;
// GetDrumHitVelocity 0x82192350 reads six per-pad velocity bytes), but this
// port grew from gd01's five-fret guitar engine and has no drum gameplay yet.
//
// The playable gem stream is built from the <song>_drums_notes array (parsed
// into Chart::drums). In gh4 that array is the drummer's performance /
// animation track: each row is (time, note, length, velocity) with the note
// naming the struck kit piece (73 kick, 75/76 toms, 77 snare, 78/79/84 hi-hat,
// 80/81/82/85 cymbals) and the velocity the recorded hit strength. The band
// animator (render/band_anim.cpp drumKitIndex) already drives the on-stage
// DrumKit from these same notes; here they become playable gems. The arm
// notes (37..57) in the same array are skipped -- they animate the drummer's
// arms, not a pad hit, and would double-count.
//
// Lanes (the five-pad GH drum layout; the kit-piece -> lane mapping is [I]):
//   0 kick, 1 red (snare), 2 yellow (hi-hat), 3 blue (tom), 4 green (cymbal).
//
// NOTE (honest scope): gh4's real gameplay drum chart is per-difficulty; this
// stream is the single performance track (no per-difficulty reduction), and
// this judge is NOT yet wired into the live match loop -- it is a tested
// component plus the input-layer guard that stops a drum kit scrambling the
// guitar part. See docs/FINDINGS.md "Drums (gh4)".
class DrumPlayer {
public:
    enum Lane { Kick = 0, Red = 1, Yellow = 2, Blue = 3, Green = 4, LaneCount = 5 };

    struct Gem {
        int32_t ms = 0;
        uint8_t lanes = 0;                      // bit per Lane struck together
        std::array<uint8_t, LaneCount> velocity{};  // recorded hit strength per lane (0..127)
    };

    struct Event {
        enum class Kind { Hit, Miss, Overhit };
        Kind kind;
        int gem;       // gem index, or -1 (an overhit has no gem)
        int lane;      // the lane
        int velocity;  // the recorded velocity of the hit lane (overhit: the strike's, 0)
    };

    // Base points a drum head is worth, and the streak multiplier steps
    // (every 10 notes up to 4x), as the five-fret rules [I: gh4's own drum
    // values are unconfirmed]. Shared with the self-test.
    static constexpr int kHeadPoints = 50;
    static int multiplierForStreak(int streak) { return std::min(4, 1 + streak / 10); }

    // The kit-piece note (73..85) this lane plays, or -1 if the note is not a
    // playable kit hit (an arm-animation note, or anything out of range).
    static int laneForNote(int note);

    // Builds the gem stream from Chart::drums (the kit-piece events); slopMs is
    // the hit window either side of a gem (gd05's watcher slop is 100 ms).
    explicit DrumPlayer(const std::vector<ChartEvent>& drums, float slopMs = 100.0f);

    // A pad (0 kick .. 4 green) struck at song time t (ms). The strike's own
    // velocity is unknown here (the chart holds the recorded one); callers that
    // have a live per-pad velocity pass it so it reaches the Hit event.
    void strike(int lane, float t, int strikeVelocity = -1);
    void update(float t);  // sweep gems whose window has fully passed into misses

    const std::vector<Gem>& gems() const { return gems_; }
    int notes() const { return notes_; }        // total lane-notes in the stream
    int notesHit() const { return hits_; }
    int score() const { return score_; }
    int streak() const { return streak_; }
    int multiplier() const { return multiplierForStreak(streak_); }
    bool gemHit(int gem) const;
    std::vector<Event> takeEvents();

private:
    void hit(int gem, int lane, float t, int strikeVelocity);
    void miss(int gem, int lane);

    std::vector<Gem> gems_;
    std::vector<uint8_t> hitLanes_, missedLanes_;
    float slopMs_;
    size_t sweep_ = 0;
    int notes_ = 0, hits_ = 0, score_ = 0, streak_ = 0;
    std::vector<Event> events_;
};
