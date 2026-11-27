// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/drumtest.h"

#include <cstdio>
#include <string>
#include <vector>

#include "game/chart.h"
#include "game/drum_player.h"
#include "input/input.h"

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// A synthetic gh4 drum track in the <song>_drums_notes shape (time, note,
// length, velocity). Kit notes 73..85 are the playable hits; arm notes 37..57
// are animation only and must be ignored by the judge. One tick carries a
// kick + crash chord, so chord handling is exercised too.
std::vector<ChartEvent> syntheticTrack() {
    return {
        {0, 37, 0, 100},     // arm note (left arm): ignored
        {0, 73, 0, 110},     // kick   -> lane 0, velocity 110
        {0, 81, 0, 95},      // cymbal -> lane 4 (chord with the kick), velocity 95
        {250, 77, 0, 120},   // snare  -> lane 1, velocity 120
        {250, 49, 0, 100},   // arm note (right arm): ignored
        {500, 78, 0, 90},    // hi-hat -> lane 2, velocity 90
        {750, 75, 0, 105},   // tom    -> lane 3, velocity 105
        {1000, 73, 0, 127},  // kick   -> lane 0, velocity 127
        {1250, 77, 0, 100},  // snare  -> lane 1
        {1500, 82, 0, 100},  // cymbal -> lane 4
    };
}

// Autoplay: strike every lane of every gem a hair after its time; sweep misses.
void playPerfect(DrumPlayer& dp) {
    const std::vector<DrumPlayer::Gem> gems = dp.gems();
    size_t next = 0;
    for (float t = -500; t < 3000; t += 4) {
        for (; next < gems.size() && float(gems[next].ms) <= t; ++next)
            for (int l = 0; l < DrumPlayer::LaneCount; ++l)
                if (gems[next].lanes >> l & 1) dp.strike(l, float(gems[next].ms) + 2);
        dp.update(t);
    }
}

void judgeTests() {
    std::printf("Drum judge (DrumPlayer on a synthetic gh4 drum track):\n");
    const std::vector<ChartEvent> track = syntheticTrack();

    // Lane mapping and the arm-note filter.
    check(DrumPlayer::laneForNote(73) == DrumPlayer::Kick, "note 73 -> kick lane");
    check(DrumPlayer::laneForNote(77) == DrumPlayer::Red, "note 77 -> red (snare) lane");
    check(DrumPlayer::laneForNote(78) == DrumPlayer::Yellow, "note 78 -> yellow (hi-hat) lane");
    check(DrumPlayer::laneForNote(75) == DrumPlayer::Blue, "note 75 -> blue (tom) lane");
    check(DrumPlayer::laneForNote(81) == DrumPlayer::Green, "note 81 -> green (cymbal) lane");
    check(DrumPlayer::laneForNote(37) < 0 && DrumPlayer::laneForNote(49) < 0, "arm notes 37/49 are not playable lanes");

    DrumPlayer built(track);
    // 10 track rows, 2 are arm notes; 8 kit hits, but the kick+cymbal share a
    // tick -> 7 gems, 8 lane-notes.
    check(built.gems().size() == 7, "7 gems built (kick+cymbal merged into one tick), got " + std::to_string(built.gems().size()));
    check(built.notes() == 8, "8 lane-notes counted, got " + std::to_string(built.notes()));
    // Velocity captured from the chart (GetDrumHitVelocity semantics).
    check(built.gems().front().velocity[DrumPlayer::Kick] == 110, "kick velocity 110 captured from the chart");
    check(built.gems().front().velocity[DrumPlayer::Green] == 95, "cymbal velocity 95 captured from the chart");

    // A perfect autoplay hits every lane-note with no miss, and the score and
    // combo advance.
    DrumPlayer dp(track);
    playPerfect(dp);
    int hitVelKick = -1, misses = 0, overhits = 0;
    for (const DrumPlayer::Event& e : dp.takeEvents()) {
        if (e.kind == DrumPlayer::Event::Kind::Miss) ++misses;
        else if (e.kind == DrumPlayer::Event::Kind::Overhit) ++overhits;
        else if (e.kind == DrumPlayer::Event::Kind::Hit && e.lane == DrumPlayer::Kick && hitVelKick < 0) hitVelKick = e.velocity;
    }
    check(dp.notesHit() == dp.notes() && dp.notesHit() == 8, "autoplay hit all 8 lane-notes, got " + std::to_string(dp.notesHit()));
    check(misses == 0 && overhits == 0, "no misses and no overhits on a perfect play");
    check(dp.score() > 0, "score advanced (" + std::to_string(dp.score()) + ")");
    check(dp.streak() == 8, "combo reached 8, got " + std::to_string(dp.streak()));
    check(hitVelKick == 110, "the first kick Hit carried velocity 110, got " + std::to_string(hitVelKick));
    // Score = sum of head points x the streak multiplier (1x for the first 10).
    check(dp.score() == 8 * DrumPlayer::kHeadPoints, "score == 8 heads at 1x, got " + std::to_string(dp.score()));

    // Wrong-lane strikes miss and reset the combo; no gem is credited.
    DrumPlayer bad(track);
    for (float t = -500; t < 3000; t += 4) {
        // Always strike green at every gem time -- wrong for the kick/snare/hat/tom gems.
        bad.strike(DrumPlayer::Kick, t);  // off the grid most of the time
        bad.update(t);
    }
    check(bad.notesHit() < bad.notes(), "stray/wrong strikes do not hit every note (" + std::to_string(bad.notesHit()) + "/" + std::to_string(bad.notes()) + ")");

    // A live per-pad velocity (what a real kit sends) reaches the Hit event.
    DrumPlayer live(track);
    const auto g0 = live.gems().front();
    (void)g0;
    live.strike(DrumPlayer::Kick, 2.0f, 64);
    int liveVel = -1;
    for (const DrumPlayer::Event& e : live.takeEvents())
        if (e.kind == DrumPlayer::Event::Kind::Hit && e.lane == DrumPlayer::Kick) liveVel = e.velocity;
    check(liveVel == 64, "a live strike velocity (64) overrides the chart's on the Hit event, got " + std::to_string(liveVel));
}

void guardTests() {
    std::printf("Drum-kit input guard (Input::drumKitSuppressesPlay):\n");
    using A = Action;
    check(Input::drumKitSuppressesPlay(A::Fret0) && Input::drumKitSuppressesPlay(A::Fret4), "frets are suppressed for a drum kit");
    check(Input::drumKitSuppressesPlay(A::Strum), "strum is suppressed for a drum kit");
    check(Input::drumKitSuppressesPlay(A::Whammy), "whammy is suppressed for a drum kit");
    check(!Input::drumKitSuppressesPlay(A::Start), "Start (pause) is NOT suppressed");
    check(!Input::drumKitSuppressesPlay(A::StarPower), "Star Power is NOT suppressed");
    check(!Input::drumKitSuppressesPlay(A::Up) && !Input::drumKitSuppressesPlay(A::Down) &&
              !Input::drumKitSuppressesPlay(A::Confirm) && !Input::drumKitSuppressesPlay(A::Back),
          "menu navigation is NOT suppressed");
}

}  // namespace

int runDrumTest() {
    failures = 0;
    std::printf("== gh4 drum judge + input guard self-test ==\n");
    judgeTests();
    guardTests();
    if (failures) std::printf("drumtest: %d check(s) FAILED\n", failures);
    else std::printf("drumtest: all checks passed\n");
    return failures ? 1 : 0;
}
