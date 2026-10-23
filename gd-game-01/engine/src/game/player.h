// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <vector>

#include "game/chart.h"
#include "game/rules.h"

enum class ControllerMode {
    Guitar,  // strum bar + frets (GuitarController, FUN_0025b868)
    Joypad,  // each fret button press also strums (FUN_0025bea8)
};

struct PlayerEvent {
    // Miss = overstrum / wrong strum, Pass = a gem scrolled past unplayed;
    // Hopo / SwingAtHopo / HeldNoteReleased (value = fraction kept) /
    // WhammyStart / WhammyEnd / StarGemHit feed the tutorial callbacks.
    enum class Kind { Hit, Miss, PhraseCaptured, PhraseMissed, StarPowerStart, StarPowerEnd, Failed,
                      Pass, Hopo, SwingAtHopo, HeldNoteReleased, WhammyStart, WhammyEnd, StarGemHit };
    Kind kind;
    int gem;  // gem index, or -1
    float value = 0;
};

// One player's judgment, scoring, Star Power and rock meter, replicating the
// game's watcher (0x263248..0x2647f0) and Player / StarPower / crowd code.
// All times are song milliseconds.
class Player {
public:
    Player(const Song& song, Difficulty difficulty, const Rules& rules, ControllerMode mode, bool noFail);

    void fretDown(int fret, float t);
    void fretUp(int fret, float t);
    void strum(float t);
    void starPowerButton(float t);
    // Whammy: `held` is a digital whammy (key / button); `value` the bar,
    // 0 rest .. 1 full press, set every frame (FUN_00116930).
    void setWhammy(bool held) { whammyHeld_ = held; }
    // Enhanced "Hammer-ons & Pull-offs: Forgiving": no strum needed since the
    // frets were let go, an early window as wide as a strum's, and a wrong
    // fret does not lock the gem out.
    void setForgivingHopos(bool on) { forgivingHopos_ = on; }
    void setWhammyValue(float value, float t);
    float whammyValue() const { return whammyValue_; }
    // The whammy bend (FUN_00263e60; docs/whammy_audio_plan.md): the guitar's
    // playback speed, 1 down to 2^(-1/12), while a hit sustain is held.
    float bendSpeed() const { return bendSpeed_; }
    // player0::player script setters (Player_v1 0x1174f8; docs/tutorials_plan.md §7.2).
    void setCrowdRatingActive(bool on) { crowdActive_ = on; }  // inactive: the rock meter is frozen, no fail
    void setCrowdRating(float rating);
    void setStarPower(float level);
    void setDeployRate(float r) { deployRate_ = r; }
    void setPhraseBoost(float b) { phraseBoost_ = b; }
    void setWhammySpEnabled(bool on) { whammySp_ = on; }
    void setMercuryEnabled(bool on) { mercury_ = on; }
    // Tutorials: whammy counts on any held sustain.
    void setTutorialMode(bool on) { tutorialMode_ = on; }
    // Face-off (docs/multiplayer_plan.md §4): player `index` of 2 owns only the
    // gems of its sections; P2's score is scaled by B0 / B1 (`handicap`);
    // nobody fails.
    void setFaceOff(int index, float handicap);
    bool owned(int gem) const { return owned_.empty() || owned_[size_t(gem)]; }
    int ownedCount() const { return owned_.empty() ? int(chart_.gems.size()) : ownedCount_; }
    // The score the HUD and results show (handicapped for P2).
    int displayScore() const { return int(float(score_) * handicap_); }
    bool multiplayer() const { return !owned_.empty(); }
    float percentComplete(float t) const;
    bool whammyHeld() const { return whammyHeld_; }
    // "Whammy active" (player+0xc0): held, or the bar moved faster than
    // whammy_speed within the last whammy_timeout.
    bool whammying() const { return whammyHeld_ || whammyMoving_; }
    void update(float t);

    const Chart& chart() const { return chart_; }
    bool gemHit(int i) const { return hit_[i] != 0; }
    int sustainGem() const { return sustainGem_; }
    uint32_t heldFrets() const { return pressed_; }
    int score() const { return score_; }
    int streak() const { return streak_; }
    int longestStreak() const { return longestStreak_; }
    int multiplier() const { return multiplier_; }
    // The HUD star meter's displayed level (FUN_00132dd0); without a HUD it follows the meter.
    void setHudStarLevel(float level) { hudStarLevel_ = level; }
    int notesHit() const { return notesHit_; }
    float rockMeter() const { return rating_; }
    float warningLevel() const { return crowd_.warningLevel; }
    float starMeter() const { return spMeter_; }
    bool starPowerReady() const { return !spDeployed_ && spMeter_ >= rules_.spReadyLevel; }
    bool starPowerActive() const { return spDeployed_; }
    bool phraseIntact(int phrase) const;
    bool failed() const { return failed_; }
    // UnmuteBeatMatchAudio (docs/guitar_audio.md): this player's guitar copy
    // is heard (its slip voice playing); slipTos() counts SlipTo(0) calls, each
    // a resync of the whammy lag.
    bool guitarAudible() const { return guitarAudible_; }
    int slipTos() const { return slipTos_; }
    std::vector<PlayerEvent> takeEvents();
    // The watcher's misses (FUN_002641e0 -> sink miss(slot, gem)): how many so
    // far and the last one's slot (the fret strummed, or -1); the track pops
    // that fret's smasher (FUN_0015e2f8).
    int missCount() const { return missCount_; }
    int lastMissFret() const { return lastMissFret_; }

private:
    float hudStarLevel_ = 1e9f;
    // watcher
    int nearestGem(float t) const;
    bool inSlop(float gemMs, float t) const;
    void watcherStrum(float t, int fret, bool guitarStrum);
    void watcherFretDown(int fret, float t);
    void watcherFretUp(int fret, float t);
    void pendingCheck(float t, int fret);
    void hopoAttempt(int fret, float t);
    void hitGem(float t, int i, bool viaHopo);
    void overstrum(float t, int gem, int fret);
    void endSustain(float t);
    void endSustainEarly(float t);
    void sweep(float t);
    void clearPending();

    // player
    void onHit(float t, int i);
    void onMiss(float t, int gem, bool pass = false);
    void accrueHeld(float t);
    void finalizeHeld(float t);
    void crowdUpdate(float target, float t);
    void recomputeMultiplier();
    void spGemHit(int i);
    void spGemMiss(int i);
    void spPoll(float t);
    void spAdd(float amount);
    void emit(PlayerEvent::Kind kind, int gem) { events_.push_back({kind, gem}); }

    const Song& song_;
    const Chart& chart_;
    const Rules& rules_;
    CrowdRules crowd_;
    ControllerMode mode_;
    bool noFail_;
    std::vector<uint8_t> hit_;
    std::vector<PlayerEvent> events_;

    // controller layer
    uint32_t pressed_ = 0;
    int missCount_ = 0, lastMissFret_ = -1;
    bool strummed_ = false;
    float allUpMs_ = -1e9f;        // when the last fret was let go (strummed_ ends a pad frame later)
    bool forgivingHopos_ = false;
    float lastStrumMs_ = -1e9f;
    bool whammyHeld_ = false, whammyMoving_ = false;
    float whammyValue_ = 0, whammyLastT_ = -1, whammyMovedT_ = -1e9f;

    // watcher state (offsets are the game's watcher fields)
    uint32_t held_ = 0;            // +0x28
    int lastHit_ = -1;             // +0x44
    int sweep_ = -1;               // +0x48
    int hopoHit_ = -1;             // +0x50
    int hopoFailed_ = -1;          // +0x54
    float chordStrumMs_ = -1e9f;   // +0x58
    float lastOverstrumMs_ = -10000;  // +0x5c
    int sustainGem_ = -1;          // +0x60
    float sustainEndMs_ = 0;       // +0x64
    int pending_ = -1;             // +0x68
    int pendingFret_ = -1;         // +0x6c
    float pendingDeadline_ = 0;    // +0x70
    int firstGem_ = 0;             // +0x78

    // scoring
    int score_ = 0;
    int streak_ = 0;
    int longestStreak_ = 0;
    int multiplier_ = 1;
    int notesHit_ = 0;
    struct Held { int gem = -1; int maxTail = 0; int earned = 0; } held_note_;

    // rock meter
    float raw_ = 0;
    bool crowdActive_ = true, whammySp_ = true, mercury_ = true, tutorialMode_ = false;
    std::vector<char> owned_;  // face-off: gems this player owns (empty: all)
    int ownedCount_ = 0;
    float handicap_ = 1;
    float deployRate_ = -1, phraseBoost_ = -1;  // -1: scoring.dta's
    float heldSinceMs_ = 0;
    bool bendArmed_ = false;
    float bendSpeed_ = 1;
    void updateBend(float t);
    float rating_ = 0.5f;
    bool failed_ = false;
    bool guitarAudible_ = true;
    // UnmuteBeatMatchAudio +0xf4 / +0xf8: in hit state since a hit (cleared by
    // a miss, a pass or a sustain let go early), and that hit's gem.
    bool hitState_ = false;
    int hitStateGem_ = -1;
    int slipTos_ = 0;
    int reached_ = -1;  // watcher +0x4c: the last gem whose time has come (FUN_00263db0)
    void slipTo() { ++slipTos_, guitarAudible_ = true; }
    void stopSlip() { guitarAudible_ = false; }

    // star power
    float spMeter_ = 0;
    bool spDeployed_ = false;
    int spState_ = 0;  // 0 none, 1 broken, 2 armed, 3 captured
    int spLast_ = -1;
    double spLastTick_ = 0;
    double spLastMeasure_ = 0;
    std::vector<uint8_t> phraseBroken_;
};
