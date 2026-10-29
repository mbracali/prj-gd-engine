// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game/note_track.h"
#include "game/tuning.h"

// One player's gameplay: GH3's per-frame judge (GuitarInputLogicPerFrame,
// 0x8211FA28) with its scoring, sustains, Star Power and rock meter.
// Rendering-free (L17); the rules and their addresses are in
// docs/FINDINGS.md "Gameplay". Fed once per game frame.
struct FrameInput {
    uint8_t frets = 0;     // held, bit 0 green .. bit 4 orange
    bool strum = false;    // the strum bar moved this frame (a pad: any fret change)
    bool starPower = false;  // Select / tilt held this frame (the trigger counts frames)
    float whammy = 0;      // 0 rest .. 1
};

struct PlayerEvent {
    enum class Kind { Hit, Miss, Overstrum, Hopo, StarPhrase, StarPowerOn, StarPowerOff, SustainDone, Failed };
    Kind kind;
    int note = -1;
    double atMs = 0;
};

struct PlayerStats {
    int64_t score = 0;
    int notesHit = 0, totalNotes = 0, currentRun = 0, bestRun = 0;
    int overstrums = 0, misses = 0, hopoHits = 0, phrasesCaptured = 0, starPowerUses = 0;
    float health = 1.0f;          // 0 .. 2 [C crowd_reset]
    float starPower = 0;          // 0 .. 100 [C 0x8210C5B8]
    bool starPowerActive = false;
    bool failed = false;
    int multiplier() const;
};

class Player {
public:
    // `inputOffsetMs`: the input offset (xenon_input_offset + the song's +
    // calibration) [C script gem_scroller]; 0 in the self-test.
    Player(const NoteTrack& track, const Tuning& tuning, int difficulty, bool noFail, float inputOffsetMs = 0);

    // One game frame at song time `nowMs` (dtMs since the last one).
    void frame(double nowMs, double dtMs, const FrameInput& in);
    const PlayerStats& stats() const { return st_; }
    const std::vector<PlayerEvent>& events() const { return events_; }
    void clearEvents() { events_.clear(); }
    size_t current() const { return cur_; }
    // The note index whose sustain is held, or -1.
    int sustaining() const { return sus_.note; }
    // Song length for the invincibility window (health_invincible_time).
    void setSongLengthMs(double ms) { songLengthMs_ = ms; }

private:
    void hit(size_t i, double now, bool tap);
    void miss(size_t i, double now);
    void overstrum(double now);
    void changeHealth(bool good, double now);
    void addStarPower(float amount);
    void updateSustain(double now, double dt, const FrameInput& in);
    void updateStarPower(double now, double dt, const FrameInput& in);

    const NoteTrack& t_;
    const Tuning& tu_;
    int diff_;
    bool noFail_;
    float offsetMs_;
    double songLengthMs_ = 0;
    size_t cur_ = 0;
    PlayerStats st_;
    std::vector<PlayerEvent> events_;

    // The 16-frame fret history [C 0x829E4BC0 / 0x829C1A08].
    std::array<uint8_t, 16> histMask_{};
    std::array<double, 16> histTime_{};
    size_t histPos_ = 0;
    uint8_t lastHitMask_ = 0;     // 0x829E3A20: cleared by a miss / overstrum
    double pendingMs_ = -1;       // 0x829E4B3C: a strum still forming (strum_before_forming)
    double tapTimerMs_ = -1;      // 0x829CD710
    uint8_t tappedMask_ = 0;      // 0x829BD540
    std::vector<bool> missedPhrase_;  // a star note of the phrase was missed
    int triggerFrames_ = 0;
    struct Sustain {
        int note = -1;
        double start = 0, end = 0;
        double units = 0;  // points_per_note_per_beat units accrued
        int awarded = 0;
        double lastMs = 0;
    } sus_;
};
