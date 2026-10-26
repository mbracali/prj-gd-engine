// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <climits>
#include <cstdint>
#include <vector>

#include "game/chart.h"
#include "game/rules.h"

enum class ControllerMode {
    Guitar,  // strum bar + frets: the guitar watcher (0x8234e9e8, vtable 0x8205b33c)
    Joypad,  // every fret press is a strum: the joypad watcher (0x8234e1f8, vtable 0x8205b204)
};

struct PlayerEvent {
    // Hit / Miss (overstrum, wrong strum) / Pass (a gem scrolled past unplayed,
    // whether or not it was penalised) and the game's sink events:
    // Hopo (hit by a hammer-on / pull-off), SwingAtHopo (a strum forgiven
    // after one), HeldNoteReleased (value = fraction of the tail kept),
    // BuildStreak / EndStreak, EightX, StarGemHit, PhraseCaptured,
    // PhraseMissed, StarPowerReady, StarPowerStart, StarPowerEnd,
    // WhammyStart / WhammyEnd, Failed.
    enum class Kind { Hit, Miss, Pass, Hopo, SwingAtHopo, HeldNoteReleased, BuildStreak, EndStreak, EightX,
                      StarGemHit, PhraseCaptured, PhraseMissed, StarPowerReady, StarPowerStart, StarPowerEnd,
                      WhammyStart, WhammyEnd, Failed };
    Kind kind;
    int gem;  // gem index, or -1
    float value = 0;
};

// One player of GH2 (Xbox 360): the beatmatch watcher (input judgement), the
// Player (score, streak), its Star Power object and its crowd (rock meter),
// following docs/FINDINGS.md. Addresses are default.xex's (Ghidra, image
// base 0x82000000); [C] confirmed there, [I] inferred.
// All times are song milliseconds.
//
// Single player is the target. TODO(multiplayer): face-off ownership is
// kept (player_mask, `index`), and competitive modes pass noFail; the co-op
// player (vtable 0x82049004: band streak, band score, band meter, shared
// Star Power that both players must engage) and score normalisation
// (0x822bb320) are not implemented yet.
class Player {
public:
    // `index`: this face-off player's number for gem ownership (player_mask,
    // 0x82347688) [C]; -1 (single player and the other modes) owns every gem.
    // The parser fills player_mask from the face-off section notes, which
    // only face-off uses [I: single player cannot skip any gem].
    Player(const Song& song, Part part, Difficulty difficulty, const Rules& rules, ControllerMode mode, bool noFail,
           int index = -1);

    // Controller layer (guitar controller 0x8234c538 down / 0x8234c6d0 up).
    void fretDown(int fret, float t);
    void fretUp(int fret, float t);
    void strum(float t);
    void starPowerButton(float t);
    // Whammy bar, 0 rest .. 1 full press [I: the range, FINDINGS open
    // question 6], sampled every frame.
    void setWhammyValue(float value, float t);
    // A digital whammy (a key): counts as a moving bar while held. Engine
    // convenience for keyboards; GH2 only has the analog bar [I].
    void setWhammy(bool held) { whammyKey_ = held; }
    // Watcher +0x2c: added to the input time (the player's lag setting) [C].
    void setCalibrationMs(float ms) { offset_ = ms; }
    // Practice: gems before `firstGem` are skipped (watcher +0x1c) and the
    // player counts only gems with ticks in [startTick, endTick) (+0xf0/+0xf4) [C].
    void setPracticeRange(int firstGem, int startTick, int endTick);
    void update(float t);

    const Chart& chart() const { return chart_; }
    bool owned(int gem) const { return index_ < 0 || chart_.gems[size_t(gem)].ownedBy(index_); }
    bool gemHit(int i) const { return hit_[size_t(i)] != 0; }
    int sustainGem() const { return sustainGem_; }
    uint32_t heldFrets() const { return held_; }
    int score() const { return score_; }
    int streak() const { return streak_; }
    int longestStreak() const { return longestStreak_; }
    int multiplier() const { return baseMultiplier_ * spMultiplier(); }
    int notesHit() const { return notesHit_; }
    int passes() const { return passes_; }
    int penalties() const { return penalties_; }
    int perfectHits() const { return perfect_; }
    int goodHits() const { return good_; }
    float rockMeter() const { return rating_; }
    float warningLevel() const { return crowd_.warningLevel; }
    float starMeter() const { return spMeter_; }
    bool starPowerReady() const { return spReady_ && !spActive_; }
    bool starPowerActive() const { return spActive_; }
    bool whammying() const;
    // The whammy bar's position (0 rest .. 1), 1 while a whammy key is held:
    // the track bends sustained tails with it.
    float whammyBar() const { return whammyKey_ ? 1.0f : whammyValue_; }
    bool phraseIntact(int phrase) const;
    bool failed() const { return failed_; }
    // The base score (0x826a0ab0) and the stars it gives (0x822d6410) [C].
    int baseScore() const { return baseScore_; }
    int stars() const;
    // Whether this player's guitar track is heard: GH1's UnmuteBeatMatchAudio
    // behaviour (on with a hit, off with a miss / pass / sustain let go
    // early) [I for GH2].
    bool guitarAudible() const { return guitarAudible_; }
    std::vector<PlayerEvent> takeEvents();

private:
    // ---- watcher
    int nearestGem(float t) const;
    bool inWindow(int gem, float t) const;
    bool hopoEligible(int k) const;
    void swing(float t, int slot, bool isStrum);
    void nonStrumSwing(float t, int slot, bool hammer);
    void watcherFretDown(int slot, float t);
    void watcherFretUp(int slot, float t);
    void fretChange(float t, int pressed);
    void joypadPress(int slot, float t);
    void watcherHit(float t, int slot, int gem, bool hopo);
    void watcherMiss(float t, int slot, int gem);
    void endSustain(float t, bool early);
    void sweep(float t);
    void clearPending() { pending_ = {}; }

    // ---- player
    void playerHit(float t, int gem);
    void playerMiss(float t, int gem);
    void playerPass(float t, int gem);
    void penalise(float t, int gem);
    void addScore(int points);
    void setStreak(int streak);
    void accrueSustain(float t);
    void finishSustain(float t);
    void crowdUpdate(float target, float t);
    bool inPractice(int gem) const;

    // ---- star power
    int spMultiplier() const { return spActive_ ? sp_.multiplier : 1; }
    void spAdd(float amount);
    void spGemHit(int gem);
    void spPhraseMiss(int gem);
    void spPoll(float t);
    void whammyPoll(float t);

    void emit(PlayerEvent::Kind kind, int gem, float value = 0) { events_.push_back({kind, gem, value}); }

    const Song& song_;
    const Chart& chart_;
    const Rules& rules_;
    CrowdRules crowd_;
    StarPowerRules sp_;
    Difficulty difficulty_;
    ControllerMode mode_;
    bool noFail_;
    int index_;
    std::vector<uint8_t> hit_;
    std::vector<PlayerEvent> events_;
    int baseScore_ = 0;

    // Controller / player sink.
    float lastStrumMs_ = -1e9f;  // sink 0x8233aa80: last accepted strum

    // Watcher fields (offsets of the guitar watcher, FINDINGS "Watcher fields").
    float offset_ = 0;            // +0x2c calibration
    int lastHit_ = -1;            // +0x18
    int firstPlayable_ = 0;       // +0x1c
    int sustainGem_ = -1;         // +0x24
    float sustainEndMs_ = 0;      // +0x28
    int sweep_ = -1;              // +0x4c
    float lastOverstrumMs_ = -1e9f;  // +0x54
    uint32_t held_ = 0;           // +0x84
    int hopoHit_ = -1;            // +0x88
    int lockedGem_ = -1;          // +0x8c
    float chordFixMs_ = -1e9f;    // +0x90
    float lastHopoTryMs_ = -1e9f; // +0x94
    struct Pending {              // +0x98 .. +0xa4
        int gem = -1;
        int slot = -1;
        float deadline = 0;
        bool afterHopo = false;
    } pending_;
    // Joypad watcher: a chord being completed within joypadChordMs (0x8234e350).
    int padChordGem_ = -1;
    uint32_t padChordGot_ = 0;
    float padChordDeadline_ = 0;

    // Player.
    int score_ = 0;
    int streak_ = 0, longestStreak_ = 0;
    int baseMultiplier_ = 1;
    int notesHit_ = 0, passes_ = 0, penalties_ = 0, perfect_ = 0, good_ = 0;
    int penalisedGem_ = -1;  // +0xac: the highest gem already penalised
    int practiceStartTick_ = INT_MIN, practiceEndTick_ = INT_MAX;  // +0xf0 / +0xf4
    struct Tail {  // the open sustain record (0x822de310)
        int gem = -1;
        int max = 0;
        int earned = 0;
    } tail_;
    bool guitarAudible_ = true;

    // Crowd (player +0x40).
    float raw_ = 0;      // +8
    float rating_ = 0.5f;  // +0xc
    bool failed_ = false;

    // Star Power (0x822db498).
    float spMeter_ = 0;
    bool spReady_ = false, spActive_ = false;
    int spState_ = 0;  // 0 none, 1 broken, 2 armed, 3 captured
    std::vector<uint8_t> phraseBroken_;  // drawing: the phrase can no longer be captured
    double spLastBeat_ = 0, spLastMeasure_ = 0;
    bool spPolled_ = false;
    bool whammyKey_ = false, whammyOn_ = false;
    float whammyValue_ = 0, whammyLastMs_ = -1e9f, whammyMovedMs_ = -1e9f;
    bool whammyValueSeen_ = false;
};
