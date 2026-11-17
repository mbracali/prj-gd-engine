// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// A song being played (GD24-002's first boot): the four instrument tracks of
// one difficulty, the song clock from the preview's position, the player's
// lane presses judged against the active track's gems, track switching.
//
// Judging [I, Harmonix's joypad watcher as the data configure it: (watcher
// (type joypad) (slop 100))]: a press hits the earliest unjudged gem of the
// active track within +-slop ms whose lanes include the pressed lane; a
// chord is hit when all its lanes are pressed inside the window; a gem not
// hit by its time + slop is missed; a press with no gem in the window
// counts as a wrong press. Overdrive, phrase capture and scoring are later
// work (GD24-004).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "data/beatmatch.h"

namespace gd24 {

class SongPlay {
public:
    static constexpr int kTracks = 4;
    enum class State : uint8_t { Pending, Hit, Missed, Passed };  // Passed: gone by while another track played

    struct Track {
        std::string name;  // "PART DRUMS"
        std::vector<Gem> gems;
        std::vector<State> state;
        std::vector<uint8_t> pressed;  // the lanes pressed so far (chords)
        std::vector<double> hitMs;     // when it was hit (the crash effect)
        int hits = 0, misses = 0;
    };

    // `tracks`: the song's tracks (Beatmatch::readTracks), `difficulty` 0..3.
    void start(const std::vector<Beatmatch::Track>& tracks, int difficulty, int initialTrack, int slopMs);
    // Every frame with the song time (ms); returns the number of new misses.
    int update(double songMs);
    // A lane pressed at `songMs`: true when it hit a gem.
    bool press(int lane, double songMs);
    void switchTrack(int dir, double songMs);
    // The clock went back (the preview looped): every gem after `songMs` is pending again.
    void rewind(double songMs);

    int active() const { return active_; }
    int difficulty() const { return difficulty_; }
    int slop() const { return slop_; }
    const std::array<Track, kTracks>& tracks() const { return tracks_; }
    int hits() const;
    int misses() const;
    int wrongPresses() const { return wrong_; }
    int streak() const { return streak_; }
    int bestStreak() const { return best_; }
    // Beats: the (tick, ms) points of every track's gems, interpolated
    // (the tempo map object is not decoded yet).
    double tickToMs(double tick) const;
    double msToTick(double ms) const;

private:
    std::array<Track, kTracks> tracks_;
    std::vector<std::pair<double, double>> tempo_;  // (tick, ms), ascending
    int active_ = 0, difficulty_ = 3, slop_ = 100, wrong_ = 0, streak_ = 0, best_ = 0;
};

}  // namespace gd24
