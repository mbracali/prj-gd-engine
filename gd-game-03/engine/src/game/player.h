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
    // BuildStreak / EndStreak, StarGemHit, PhraseCaptured,
    // PhraseMissed, StarPowerReady, StarPowerStart, StarPowerEnd,
    // WhammyStart / WhammyEnd, Failed, StarPowerEngage (value 1 / 0: the
    // sp_engage message).
    enum class Kind { Hit, Miss, Pass, Hopo, SwingAtHopo, HeldNoteReleased, BuildStreak, EndStreak,
                      StarGemHit, PhraseCaptured, PhraseMissed, StarPowerReady, StarPowerStart, StarPowerEnd,
                      WhammyStart, WhammyEnd, Failed, StarPowerEngage };
    Kind kind;
    int gem;  // gem index, or -1
    float value = 0;
};

// What the HUD shows of a performer (a player or the co-op band).
struct PerformerState {
    int score = 0, streak = 0, multiplier = 1, baseMultiplier = 1;
    float starMeter = 0, rock = 0.5f, warning = 0.15f;
    bool spActive = false;
};

// The Star Power meter {value, shown} (0x822DB478): each player's own, or
// one shared by both co-op players (shared_star_meter, 0x822C9E80 /
// 0x822C69E8) [C].
struct StarMeter {
    float value = 0;
};

class BandPerformer;

// One player of GH2 (Xbox 360): the beatmatch watcher (input judgement), the
// Player (score, streak), its Star Power object and its crowd (rock meter),
// following docs/FINDINGS.md and docs/multiplayer.md. Addresses are
// default.xex's (Ghidra, image base 0x82000000); [C] confirmed there, [I]
// inferred. All times are song milliseconds.
//
// Two players (docs/multiplayer.md): face-off players own their gems
// (player_mask, `index`); the scores are normalised (0x822BB320); co-op
// players (vtable 0x82049004) feed the band (game/band_performer.h) and
// share one Star Power meter that both must engage.
class Player {
public:
    // `index`: this face-off player's number for gem ownership (player_mask,
    // 0x82347688) [C]; -1 (single player and the other modes) owns every gem.
    // The parser fills player_mask from the face-off section notes, which
    // only face-off uses [I: single player cannot skip any gem].
    // `config`: the mode's score_config (default_config, or coop_config in
    // co-op) [C]; nullptr is default_config.
    Player(const Song& song, Part part, Difficulty difficulty, const Rules& rules, ControllerMode mode, bool noFail,
           int index = -1, const ScoreConfig* config = nullptr);

    // ---- two players (docs/multiplayer.md)
    // The player's number (player +0x9c), for the normalisation and the
    // shared deploy; `index` above (ownership) is set only in face-off.
    void setNumber(int number) { number_ = number; }
    // Every point × this (use_normalization, 0x822BB320 / 0x822C6A58) [C].
    void setNormalisation(float factor) { norm_ = factor; }
    float normalisation() const { return norm_; }
    // Co-op: one meter for both players, the other player (both must
    // engage, 0x822C80F8) and the band with this player's crowd mix
    // (0x822DEC68, +0x118 / +0x11c / +0x120) [C].
    void shareStarMeter(StarMeter* meter, Player* partner) { meter_ = meter, partner_ = partner; }
    void setBand(BandPerformer* band, float a, float b, float w) { band_ = band, bandA_ = a, bandB_ = b, bandW_ = w; }

    // Controller layer (guitar controller 0x8234c538 down / 0x8234c6d0 up).
    void fretDown(int fret, float t);
    void fretUp(int fret, float t);
    void strum(float t);
    // Engaging Star Power (Select or tilt: force_mercury; down = TRUE, up =
    // FALSE), 0x822C80F8 [C]. starPowerButton(t) = engage(TRUE).
    void starPowerEngage(bool engaged, float t);
    void starPowerButton(float t) { starPowerEngage(true, t); }
    bool starPowerEngaged() const { return engaged_; }
    // Whammy bar, 0 rest .. 1 full press [I: the range, FINDINGS open
    // question 6], sampled every frame.
    void setWhammyValue(float value, float t);
    // A digital whammy (a key): counts as a moving bar while held. Engine
    // convenience for keyboards; GH2 only has the analog bar [I].
    void setWhammy(bool held) { whammyKey_ = held; }
    // Player +0xbc, the whammy gate of the whammy poll (PS2 0x00111d88): on
    // from the constructor (0x001107d8) and changed only by the script
    // method set_whammystarpowerenabled (0x001131a0 -> 0x00111cd8), which
    // only tutorial_22_star_combos uses (FALSE) [C]. While it is off the
    // poll leaves the whammy flag as it is.
    void setWhammyStarPowerEnabled(bool on) { whammyStarPowerEnabled_ = on; }
    // set_mercuryswitchenabled (+0xb8, 1 at construction; PS2 0x00111ce0).
    void setMercurySwitchEnabled(bool on) { mercuryEnabled_ = on; }
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
    // +0x6c, a float read with fctiwz (0x822DCB80) [C].
    int score() const { return int(score_); }
    int streak() const { return streak_; }
    int longestStreak() const { return longestStreak_; }
    int multiplier() const { return baseMultiplier_ * spMultiplier(); }
    // The streak multiplier without Star Power's doubling (1..4).
    int baseMultiplier() const { return baseMultiplier_; }
    int notesHit() const { return notesHit_; }
    int passes() const { return passes_; }
    int penalties() const { return penalties_; }
    // +0xac: the highest gem already penalised (-1 none).
    int penalisedGem() const { return penalisedGem_; }
    int perfectHits() const { return perfect_; }
    int goodHits() const { return good_; }
    float rockMeter() const { return rating_; }
    // The crowd's SetRating (0x822DE530): the rating clamped to 0..1, raw
    // at that point of the range (GamePanel sets 1 for an encore, 0x822CCA60).
    void setCrowdRating(float rating);
    float warningLevel() const { return crowd_.warningLevel; }
    float starMeter() const { return meter_->value; }
    PerformerState state() const;
    // Whether a player may fail the song, PS2 0x0010fc78: the crowd fails
    // only when the mode is not "multiplayer and competitive" (0x001260a8)
    // and the performer's vtable +0xa4 allows it. For a player (and a co-op
    // player) that is 0x00112890: competitive ? player number == 0 :
    // num_players == 1 [C, the same as GH2's 0x822C6B98]. So only the solo
    // player fails; co-op players and both face-off players never do. (The
    // co-op band is a plain Performer whose +0xa4, 0x0010f310, is TRUE: it
    // fails.) Callers pass !mayFail(...) as noFail.
    static bool mayFail(int numPlayers, bool competitive, int number) {
        if (numPlayers > 1 && competitive) return false;
        return competitive ? number == 0 : numPlayers == 1;
    }
    bool starPowerReady() const { return spReady_ && !spActive_; }
    bool starPowerActive() const { return spActive_; }
    bool whammying() const;
    // The whammy bar's position (0 rest .. 1), 1 while a whammy key is held:
    // the track bends sustained tails with it.
    float whammyBar() const { return whammyKey_ ? 1.0f : whammyValue_; }
    bool phraseIntact(int phrase) const;
    // The Star Power phrase state (+0x58 of the PS2's Star Power object):
    // 0 none, 1 broken, 2 armed, 3 captured.
    int spPhraseState() const { return spState_; }
    bool failed() const { return failed_; }
    // Development aid (GD03_FAIL_AT): fails the song now, as an empty rock
    // meter does (the Failed event; no-fail does not stop it).
    void forceFail();
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
    void sustainReleased(float t, int gem);
    void sweep(float t);
    void clearPending() { pending_ = {}; }

    // ---- player
    void playerHit(float t, int gem);
    void playerMiss(float t, int gem);
    void playerPass(float t, int gem);
    void penalise(float t, int gem);
    void addScore(int points);
    void endStreak();
    void setStreak(int streak);
    void accrueSustain(float t);
    void finishSustain(float t);
    void crowdUpdate(float target, float t);
    void ownCrowdUpdate(float target, float t);
    bool inPractice(int gem) const;

    // ---- star power
    int spMultiplier() const { return spActive_ ? sp_.multiplier : 1; }
    void spStart(bool on);
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
    int number_ = 0;
    float norm_ = 1;
    BandPerformer* band_ = nullptr;
    float bandA_ = 0, bandB_ = 1, bandW_ = 1;
    Player* partner_ = nullptr;
    bool mercuryEnabled_ = true;  // +0xb8
    bool engaged_ = false;  // +0xc4 on the PS2 (0x00111940)
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
    float lastHopoTryMs_ = -1e9f; // +0x94: the last hit (no hammer-on try at that instant)
    struct Pending {              // +0x98 .. +0xa4
        int gem = -1;
        int slot = -1;
        float deadline = 0;
        bool afterHopo = false;
    } pending_;
    // Joypad watcher: a chord being completed within joypadChordMs (0x8234e350).
    // PS2 joypad watcher +0x88 gem / +0x8c frets so far / +0x90 slot / +0x94 deadline.
    int padChordGem_ = -1;
    uint32_t padChordGot_ = 0;
    int padChordSlot_ = -1;
    float padChordDeadline_ = 0;
    void clearPadChord() { padChordGem_ = -1, padChordGot_ = 0, padChordSlot_ = -1, padChordDeadline_ = 0; }

    // Player.
    float score_ = 0;
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

    // Star Power (0x822db498); its meter (+0x6c) is ownMeter_ or the shared one.
    StarMeter ownMeter_;
    StarMeter* meter_ = &ownMeter_;
    bool spReady_ = false, spActive_ = false;
    int spState_ = 0;  // 0 none, 1 broken, 2 armed, 3 captured
    int armedPhrase_ = -1;  // drawing: the phrase that state 2 refers to
    std::vector<uint8_t> phraseBroken_;  // drawing: the phrase can no longer be captured
    double spLastBeat_ = 0, spLastMeasure_ = 0;
    bool spPolled_ = false;
    bool whammyKey_ = false, whammyOn_ = false;
    float whammyValue_ = 0, whammyLastMs_ = -1e9f, whammyMovedMs_ = -1e9f;
    bool whammyValueSeen_ = false;
    bool whammyStarPowerEnabled_ = true;  // +0xbc
};
