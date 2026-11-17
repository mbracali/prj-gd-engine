// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// A song being played (GD24-002 / GD24-003): the four instrument tracks of
// one difficulty, the player's lanes judged against the active track's gems,
// track switching, and the gameplay rules of the original (GD24-003,
// docs/FORMATS.md "Gameplay rules"). [C] = from the executable / the disc's
// data or text, [I] = inferred or chosen (agents/DECISIONS.md D7..D9).
//
// Judging [I, Harmonix's joypad watcher as the data configure it: (watcher
// (type joypad) (slop 100))]: a press hits the earliest unjudged gem of the
// active track within +-slop ms whose lanes include the pressed lane; a
// chord is hit when all its lanes are pressed inside the window; a gem not
// hit by its time + slop is missed.
//
// Phrases and capture:
//   [C text]  "To play a phrase, hit all the notes inside the Phrase
//             Indicator"; "When you play a phrase correctly, you get the
//             instrument to play on its own for a while"; the indicator
//             follows the player's track.
//   [I]       a phrase = a track's gems inside a window of 2 bars ((phrases
//             (finder (bar 2))) in the DTA), windows aligned to bar 0 of the
//             song's time signatures [C data]; the indicator takes the first
//             phrase that has not started when the player gets there.
//   [C code]  capture (0x08a86368): multiplier = int(multiplier) + 1 (at most
//             5), score += capture points x multiplier; the track joins a
//             capture queue with the current bar and is released once more
//             than 7 bars have gone by, or 10 while the multiplier is 2 or
//             more (0x08a80ad8) [the exact queue rules: I, simplified to that
//             plus an early release when no track is left to play].
//   [C code]  a miss inside the indicator (0x08a8de08) lowers the track's
//             health and, once the short timer set by a capture has run out,
//             resets the multiplier to 1; [C text] "Missing a note outside of
//             the Phrase Indicator won't break your streak", "Any note you
//             play outside of the Phrase Indicator will give you points".
// Score [C code 0x08a91240]: each gem hit adds gem points x multiplier; the
//   multiplier shown is int(multiplier) 1..5, doubled in Overdrive with 5 ->
//   11 (the pa_2x..11x badges) [C]. The point values come from a tuning
//   block the code reads at run time (not found): [I] the DTA's (scoring
//   (gem_value 25)) and (max_bar_value 500) for a capture.
// Health [C code]: each track has a health 0..1; hits raise it, misses inside
//   its phrase lower it by a per-difficulty amount (times a factor when it is
//   not the active track); the crowd meter = the mean of the four healths
//   minus a per-difficulty amount per failed track (0x08af608c); Easy keeps
//   every health at least 2 x the miss amount for the first 50 % of the song,
//   Medium for the first 30 % [C]. [I] values: the DTA's (health (init 0.6)
//   (failure 0.04..0.07) (capture 0.06)); a track whose health reaches 0
//   fails (can't be played) [C text]; the song fails when the crowd meter
//   reaches 0 [I].
// Overdrive [C text]: "glowing white notes" (gems flagged 0x02000000, the run
//   ending at 0x04000000 [I]) hit perfectly fill the Energy meter; at least
//   half full, Overdrive can be deployed; it doubles the multiplier "with no
//   failed instruments" and rescues failed instruments. [C code] in
//   Overdrive every hit heals all four tracks. [I] a run gives 1/4 meter, a
//   full meter lasts 32 beats.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "data/beatmatch.h"

namespace gd24 {

// The numbers of the rules (see the top of this file for [C] / [I]).
struct Rules {
    int gemPoints = 25;        // [I] DTA (scoring (gem_value 25))
    int capturePoints = 500;   // [I] DTA (scoring (max_bar_value 500))
    int phraseBars = 2;        // [I] DTA (phrases (finder (bar 2)))
    int releaseBars = 8;       // [C] released when more than 7 bars have gone by
    int releaseBarsHigh = 11;  // [C] ... 11 while the multiplier is >= 2
    int maxMultiplier = 5;     // [C]
    double captureGraceMs = 1500;  // [I] the capture timer before a miss resets the multiplier
    float healthInit = 0.6f;   // [I] DTA (health (init 0.6))
    float healthMiss[4] = {0.04f, 0.05f, 0.06f, 0.07f};  // [I] DTA (failure ...) per difficulty
    float inactiveFactor = 1.0f;   // [I] x the miss amount once per phrase nobody plays (the code applies a factor off the active track)
    float healthHit = 0.02f;       // [I]
    float healthCapture = 0.06f;   // [I] DTA (health (capture 0.06))
    float healthDanger = 0.3f, healthCritical = 0.15f;  // [I] red / blinking
    float crowdFailedPenalty = 0.15f;  // [I] per failed track
    float odRunEnergy = 0.25f;     // [I]
    float odMinEnergy = 0.5f;      // [C text] "at least halfway full"
    double odBeatsFull = 32;       // [I] a full meter's length in beats
};

class SongPlay {
public:
    static constexpr int kTracks = 4;
    // Passed: gone by while another track played; Auto: played by a captured track.
    enum class State : uint8_t { Pending, Hit, Missed, Passed, Auto };

    struct Phrase {
        size_t first = 0, end = 0;  // gems [first, end)
        int bar = 0;                // the window's first bar
        double startMs = 0, endMs = 0;  // first and last gem
    };
    struct OdRun {
        size_t first = 0, end = 0;
        bool broken = false;
    };

    struct Track {
        std::string name;  // "PART DRUMS"
        std::vector<Gem> gems;
        std::vector<State> state;
        std::vector<uint8_t> pressed;  // the lanes pressed so far (chords)
        std::vector<double> hitMs;     // when it was hit (the crash effect)
        std::vector<int> phraseOf;     // gem -> phrase index
        std::vector<int> odRunOf;      // gem -> Overdrive run index or -1
        std::vector<Phrase> phrases;
        std::vector<OdRun> odRuns;
        int hits = 0, misses = 0;
        int indicator = -1;     // the Phrase Indicator's phrase (active track only)
        bool captured = false;  // playing on its own
        int captureBar = -1;
        double capturedAtMs = 0;
        float health = 0.6f;
        bool failed = false;
        int attempted = 0, capturedCount = 0;
        size_t passed = 0;  // gems before this index are judged / gone
    };

    struct Stats {
        int score = 0, gemScore = 0, captureScore = 0;
        int phraseStreak = 0, bestPhraseStreak = 0;
        int noteStreak = 0, bestNoteStreak = 0;
        int maxMultiplier = 1;
        int odActivations = 0, odRunsCompleted = 0, rescues = 0;
        int failedTracks = 0;
        bool songFailed = false;
        double failMs = 0;
    };

    // `tracks`: the song's tracks (Beatmatch::readTracks), `difficulty` 0..3.
    void start(const std::vector<Beatmatch::Track>& tracks, const std::vector<TimeSig>& sigs, int difficulty, int initialTrack,
               int slopMs, const Rules& rules = Rules{});
    // Play starts at `songMs` (the preview's place): earlier gems are gone by.
    void startAt(double songMs);
    // Every frame with the song time (ms); returns the number of new misses.
    int update(double songMs);
    // A lane pressed at `songMs`: true when it hit a gem.
    bool press(int lane, double songMs);
    void switchTrack(int dir, double songMs);
    // The Overdrive button: true when it was deployed.
    bool overdrive(double songMs);
    // The clock went back: every gem after `songMs` is pending again (dev aid).
    void rewind(double songMs) { startAt(songMs); }

    int active() const { return active_; }
    int difficulty() const { return difficulty_; }
    int slop() const { return slop_; }
    const Rules& rules() const { return rules_; }
    const std::array<Track, kTracks>& tracks() const { return tracks_; }
    const Stats& stats() const { return stats_; }
    int hits() const;
    int misses() const;
    int wrongPresses() const { return wrong_; }
    int streak() const { return stats_.phraseStreak; }
    int bestStreak() const { return stats_.bestPhraseStreak; }
    int score() const { return stats_.score; }
    // The multiplier shown: 1..5, doubled in Overdrive (5 -> 11) [C].
    int multiplier() const;
    float multiplierProgress() const { return multiplier_; }
    float energy() const { return energy_; }
    bool inOverdrive() const { return od_; }
    float crowd() const;
    bool failed() const { return stats_.songFailed; }
    // The song's bar at `ms` and the bar's start (ms).
    int barAt(double ms) const;
    double barMs(int bar) const;
    double chartEndMs() const { return chartEndMs_; }
    // The first phrase on track `t` that starts at or after `ms` (-1: none).
    int nextPhrase(int t, double ms) const;
    // Beats: the (tick, ms) points of every track's gems, interpolated.
    double tickToMs(double tick) const;
    double msToTick(double ms) const;
    // An event log for tests (score changes): {ms, what, value}.
    struct Event {
        double ms;
        char what;  // 'h' hit, 'c' capture, 'm' phrase miss, 'r' release, 'f' track failed, 'o' Overdrive, 'e' energy, 'F' song failed
        int track, value, multiplier;
    };
    const std::vector<Event>& events() const { return events_; }

private:
    void buildPhrases(Track& t);
    void setIndicator(int t, double ms);
    void capture(int t, double ms);
    void failPhrase(int t, double ms);
    void damage(int t, float amount, double ms);
    void heal(int t, float amount);
    void onHit(int t, size_t i, double ms);
    void release(int t, double ms);
    bool playable(int t) const { return !tracks_[size_t(t)].captured && !tracks_[size_t(t)].failed; }

    Rules rules_;
    std::array<Track, kTracks> tracks_;
    std::vector<std::pair<double, double>> tempo_;  // (tick, ms), ascending
    std::vector<uint32_t> barTick_;                  // each bar's first tick
    int active_ = 0, difficulty_ = 3, slop_ = 100, wrong_ = 0;
    float multiplier_ = 1, energy_ = 0;
    bool od_ = false;
    double odTick_ = 0, graceUntil_ = -1e300, lastMs_ = 0, chartEndMs_ = 0;
    Stats stats_;
    std::vector<Event> events_;
};

// Development aid and tests (GD24_AUTOPLAY, --rulestest): presses the active
// track's gems on time; once the active track is captured or failed, moves
// one track a call towards the playable track whose next phrase comes first;
// deploys Overdrive at multiplier 5 or when a track has failed.
void autoplayStep(SongPlay& p, double songMs, bool useOverdrive = true);

}  // namespace gd24
