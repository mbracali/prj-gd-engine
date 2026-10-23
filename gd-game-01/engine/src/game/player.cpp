// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/player.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kStrumDebounceMs = 52;   // FUN_0025a0e0
// The PS2 reads the pad once a frame, so letting go of one fret and pressing
// the next within a frame is one change there: no "nothing held" moment ends
// the HOPO chain (FUN_0025b9b0 clears +0x1c only when a frame shows no fret).
constexpr float kPadFrameMs = 1000.0f / 59.94f;
constexpr float kOverstrumGapMs = 50;    // FUN_002641e0
constexpr float kMissAttributionMs = 500;  // FUN_001166f0
constexpr float kLastGemNextMs = 4000;   // FUN_0025b5a8

int highestFret(uint32_t mask) {
    for (int f = 4; f >= 0; --f)
        if (mask & (1u << f)) return f;
    return -1;
}

int lowestFret(uint32_t mask) {
    for (int f = 0; f < 8; ++f)
        if (mask & (1u << f)) return f;
    return -1;
}

}  // namespace

Player::Player(const Song& song, Difficulty difficulty, const Rules& rules, ControllerMode mode, bool noFail)
    : song_(song),
      chart_(song.charts[static_cast<int>(difficulty)]),
      rules_(rules),
      crowd_(rules.crowd[static_cast<int>(difficulty)]),
      mode_(mode),
      noFail_(noFail),
      hit_(chart_.gems.size(), 0),
      phraseBroken_(chart_.phrases.size(), 0) {
    // FUN_0011d3f8(0.5): the crowd starts in the middle of its range.
    rating_ = 0.5f;
    raw_ = crowd_.rangeLow + (crowd_.rangeHigh - crowd_.rangeLow) * 0.5f;
}

void Player::setFaceOff(int index, float handicap) {
    owned_.assign(chart_.gems.size(), 0);
    ownedCount_ = 0;
    for (size_t i = 0; i < chart_.gems.size(); ++i)
        if (chart_.gems[i].ownedBy(index)) owned_[i] = 1, ++ownedCount_;
    handicap_ = index == 0 ? 1.0f : handicap;
    noFail_ = true;  // FUN_0011d3b8: no lose test in multiplayer
}

std::vector<PlayerEvent> Player::takeEvents() {
    std::vector<PlayerEvent> out;
    out.swap(events_);
    return out;
}

bool Player::phraseIntact(int phrase) const {
    return phrase >= 0 && !phraseBroken_[phrase] && !spDeployed_;
}

// ---------------------------------------------------------------- input layer

void Player::strum(float t) {
    if (std::getenv("GD01_HOPOLOG")) std::fprintf(stderr, "strum at %.1f (held %02x)\n", t, pressed_);
    if (mode_ != ControllerMode::Guitar || failed_) return;
    if (lastStrumMs_ + kStrumDebounceMs < t) {
        lastStrumMs_ = t;
        watcherStrum(t, highestFret(pressed_), true);
    }
    strummed_ = true;
}

void Player::fretDown(int fret, float t) {
    if (failed_) return;
    if (std::getenv("GD01_HOPOLOG"))
        std::fprintf(stderr, "fret %d down at %.1f (held %02x, strummed %d, mode %s)\n", fret, t, pressed_, int(strummed_),
                     mode_ == ControllerMode::Guitar ? "guitar" : "joypad");
    uint32_t bit = 1u << fret;
    watcherFretDown(fret, t);
    if (mode_ == ControllerMode::Guitar) {
        if (pressed_ == 0 && strummed_ && t - allUpMs_ > kPadFrameMs && !forgivingHopos_) strummed_ = false;
        if ((strummed_ || forgivingHopos_) && (pressed_ >> fret) == 0) hopoAttempt(fret, t);
    } else {
        lastStrumMs_ = t;
        watcherStrum(t, fret, false);
    }
    pressed_ |= bit;
}

void Player::fretUp(int fret, float t) {
    if (failed_) return;
    watcherFretUp(fret, t);
    pressed_ &= ~(1u << fret);
    if (mode_ != ControllerMode::Guitar) return;
    if (pressed_ == 0) allUpMs_ = t;  // strummed_ ends unless a fret comes back within a pad frame
    else if ((strummed_ || forgivingHopos_) && highestFret(pressed_) < fret) hopoAttempt(highestFret(pressed_), t);
}

// -------------------------------------------------------------------- watcher

int Player::nearestGem(float t) const {
    // FUN_0025b468: lower bound by time, then the closer neighbour (ties: earlier).
    const auto& g = chart_.gems;
    if (g.empty()) return -1;
    auto it = std::lower_bound(g.begin(), g.end(), t, [](const Gem& x, float v) { return x.ms < v; });
    if (it == g.begin()) return 0;
    if (it == g.end()) return static_cast<int>(g.size()) - 1;
    int i = static_cast<int>(it - g.begin());
    return std::abs(t - g[i - 1].ms) <= std::abs(g[i].ms - t) ? i - 1 : i;
}

bool Player::inSlop(float gemMs, float t) const { return std::abs(t - gemMs) <= rules_.slopMs; }

void Player::clearPending() {
    pending_ = -1;
    pendingFret_ = -1;
    pendingDeadline_ = 0;
}

// FUN_00263248
void Player::watcherStrum(float t, int fret, bool guitarStrum) {
    if (chordStrumMs_ == t && !guitarStrum) return;
    int i = nearestGem(t);
    if (i < 0 || i < firstGem_) return;
    const Gem& g = chart_.gems[i];
    if (!inSlop(g.ms, t) || !owned(i)) {  // the opponent's gem: an overstrum
        overstrum(t, i, fret);
        return;
    }
    if (!hit_[i]) {
        bool match = g.fretCount() == 1 ? lowestFret(g.frets) == fret : g.frets == held_;
        if (match) {
            hitGem(t, i, false);
            return;
        }
        if (pending_ != -1) {
            overstrum(t, pending_, pendingFret_);  // 0x2633b8: the pending gem and its fret
            clearPending();
        }
        if (!guitarStrum && g.fretCount() < 2) {
            overstrum(t, i, fret);
            return;
        }
        pending_ = i;
        pendingFret_ = fret;
        pendingDeadline_ = t + rules_.strumGraceMs;
    } else if (i == hopoHit_) {
        hopoHit_ = -1;  // strumming a note already hit as a HOPO is forgiven
        emit(PlayerEvent::Kind::SwingAtHopo, i);
    } else {
        overstrum(t, i, fret);
    }
}

// FUN_002634f0 / FUN_00263fa0
void Player::watcherFretDown(int fret, float t) {
    held_ |= 1u << fret;
    if (sustainGem_ != -1) {
        const Gem& g = chart_.gems[sustainGem_];
        if (fret > highestFret(g.frets) || g.fretCount() >= 2) endSustainEarly(t);
    }
    pendingCheck(t, fret);
}

// FUN_00263578
void Player::watcherFretUp(int fret, float t) {
    held_ &= ~(1u << fret);
    if (sustainGem_ != -1) {
        uint32_t need = chart_.gems[sustainGem_].frets;
        if ((held_ & need) != need) endSustainEarly(t);
    }
    pendingCheck(t, -1);
}

// FUN_00263860: frets that come to match a strummed gem before the grace ends.
void Player::pendingCheck(float t, int fret) {
    if (pending_ == -1 || pendingDeadline_ <= t) return;
    int i = pending_;
    const Gem& g = chart_.gems[i];
    bool match = (g.fretCount() == 1 && fret != -1) ? lowestFret(g.frets) == fret : g.frets == held_;
    if (match) {
        hitGem(t, i, false);
        if (g.fretCount() > 1) chordStrumMs_ = t;
    } else if (fret != -1) {
        overstrum(t, i, pendingFret_);
        clearPending();
    }
}

// FUN_002639b8
void Player::hopoAttempt(int fret, float t) {
    // Development aid: GD01_HOPOLOG=1 logs every try and why it failed.
    static const bool log = std::getenv("GD01_HOPOLOG") != nullptr;
    int i = nearestGem(t);
    auto why = [&](const char* r) {
        if (log) std::fprintf(stderr, "hopo try fret %d at %.1f: gem %d (%.1f, %+.1f ms) %s\n", fret, t, i,
                              i >= 0 ? chart_.gems[size_t(i)].ms : 0.0f, i >= 0 ? t - chart_.gems[size_t(i)].ms : 0.0f, r);
    };
    if (i <= 0 || i < firstGem_) return why("no gem");
    const Gem& g = chart_.gems[i];
    if (!g.hopo) return why("not a HOPO gem");
    if (!owned(i) || !owned(i - 1)) return why("not own");
    if (!hit_[i - 1] || lastHit_ != i - 1) return why("previous gem not hit last");
    // As decoded: the window is offset by the watcher's 60 ms field.
    // Forgiving: as early as a strum may be (-100 .. +160 ms).
    const float early = forgivingHopos_ ? -rules_.slopMs : rules_.strumGraceMs - rules_.slopMs;
    if (t - g.ms < early || t - g.ms > rules_.strumGraceMs + rules_.slopMs) return why("outside the window");
    if (hit_[i]) return why("already hit");
    if (i == hopoFailed_ && !forgivingHopos_) return why("blocked by an earlier wrong try");
    if (fret >= 0 && g.frets == (1u << fret)) hitGem(t, i, true), why("HIT");
    else hopoFailed_ = i, why("wrong fret: blocks this gem");
}

// FUN_00263728
void Player::hitGem(float t, int i, bool viaHopo) {
    if (viaHopo) hopoHit_ = i;
    endSustain(t);
    hit_[i] = 1;
    onHit(t, i);
    // PlayerMatcher_v17 hopo / _v16 swingAtHopo: hit without a strum, or a HOPO-able gem strummed.
    if (viaHopo) emit(PlayerEvent::Kind::Hopo, i);
    else if (chart_.gems[size_t(i)].hopo) emit(PlayerEvent::Kind::SwingAtHopo, i);
    heldSinceMs_ = t;
    lastHit_ = i;
    sweep_ = i;
    const Gem& g = chart_.gems[i];
    if (g.durationMs == 0) {
        sustainGem_ = -1;
    } else {
        sustainGem_ = i;
        sustainEndMs_ = g.ms + g.durationMs;
    }
    clearPending();
}

// FUN_002641e0
void Player::overstrum(float t, int gem, int fret) {
    endSustain(t);
    if (lastOverstrumMs_ + kOverstrumGapMs < t) {
        lastOverstrumMs_ = t;
        ++missCount_;
        lastMissFret_ = fret;
        if (gem != -1 && gem == sweep_) ++gem;
        if (gem >= static_cast<int>(chart_.gems.size())) gem = -1;
        onMiss(t, gem);
    }
}

// FUN_002640d8
void Player::endSustain(float t) {
    bendArmed_ = false, bendSpeed_ = 1;  // FUN_002640d8: the bend resets with every judgement
    if (sustainGem_ != -1) finalizeHeld(t);
    sustainGem_ = -1;
    sustainEndMs_ = 0;
}

// FUN_00264018: the sustain let go (a fret released, or a higher one pressed)
// sends sink slot 0x9c (UnmuteBeatMatchAudio_v18) with the time left: the
// hit-state gem, with a sustain (gem flag bit 9 clear) and time left, goes
// silent until the next hit.
void Player::endSustainEarly(float t) {
    if (sustainGem_ != -1) {
        const Gem& g = chart_.gems[size_t(sustainGem_)];
        if (sustainGem_ == hitStateGem_ && !g.noSustain && g.ms + g.durationMs - t > 0) stopSlip(), hitState_ = false;
    }
    endSustain(t);
}

// FUN_00263b20
void Player::sweep(float t) {
    const auto& g = chart_.gems;
    int n = static_cast<int>(g.size());
    while (true) {
        int j = sweep_ + 1;
        if (sweep_ == n - 1 || j == pending_) return;
        float t0 = g[j].ms;
        float t1 = j + 1 < n ? g[j + 1].ms : t0 + kLastGemNextMs;
        bool live = j >= firstGem_;
        float deadline = std::min(t0 + (live ? rules_.slopMs : 0.0f), (t0 + t1) * 0.5f);
        if (!owned(j)) deadline = t0;  // an opponent's gem passes at its own time
        if (t <= deadline) return;
        sweep_ = j;
        if (!owned(j)) {
            // An opponent's gem passes without a miss (sink 0x3c,
            // UnmuteBeatMatchAudio_v6): this player's guitar copy stops, the
            // hit state stays (the next own gem's time brings it back).
            stopSlip();
        } else if (live) {
            endSustain(t);
            onMiss(t, j, true);
        } else {
            // A gem before the start point (sink 0x44, _v7 -> _v3): as if hit.
            slipTo();
            hitState_ = true, hitStateGem_ = j;
        }
    }
}

// FUN_002630b0
void Player::update(float t) {
    if (failed_) return;
    if (pending_ != -1 && t > pendingDeadline_) {
        overstrum(t, pending_, pendingFret_);  // 0x2630cc: the pending gem and its fret
        clearPending();
    }
    accrueHeld(t);
    if (sustainGem_ != -1 && t > sustainEndMs_) endSustain(t);
    if (hopoHit_ != -1 && !inSlop(chart_.gems[hopoHit_].ms, t)) hopoHit_ = -1;
    sweep(t);
    // FUN_00263db0: each own gem whose time has come (sink 0x14,
    // UnmuteBeatMatchAudio_v1) SlipTo 0 while in hit state.
    for (int n = int(chart_.gems.size()); reached_ + 1 < n && chart_.gems[size_t(reached_ + 1)].ms < t;) {
        ++reached_;
        if (owned(reached_) && hitState_) slipTo();
    }
    spPoll(t);
    updateBend(t);
}

// FUN_00263e60: the bend arms once the bar has been at rest since the hit
// (the keyboard's wiggle never rests: <= 0.01 [I]); its depth ramps over
// ms_to_full_pitch_bend (1000) to pitch_bend_range (1 semitone), down only.
void Player::updateBend(float t) {
    if (whammyValue_ <= 0.01f) bendArmed_ = true;
    if (sustainGem_ < 0) {
        bendSpeed_ = 1;
        return;
    }
    if (!bendArmed_) return;
    float ramp = std::clamp((t - heldSinceMs_) / 1000.0f, 0.0f, 1.0f);
    bendSpeed_ = std::pow(2.0f, -whammyValue_ * ramp / 12.0f);
}

// --------------------------------------------------------------------- player

// FUN_00116580
void Player::onHit(float t, int i) {
    const Gem& g = chart_.gems[i];
    ++streak_;
    longestStreak_ = std::max(longestStreak_, streak_);
    ++notesHit_;
    finalizeHeld(t);
    score_ += rules_.headPoints * g.fretCount() * multiplier_;  // previous multiplier
    if (g.noSustain) {
        crowdUpdate(1.0f, t);
    } else {
        double tail = rules_.tailPoints * (g.durationTicks / double(song_.tempo.division()));
        held_note_ = {i, static_cast<int>(std::lround(tail)) * g.fretCount(), 0};
    }
    spGemHit(i);
    recomputeMultiplier();
    // UnmuteBeatMatchAudio_v3 (sink hit): SlipTo 0, hit state on this gem.
    slipTo();
    hitState_ = true, hitStateGem_ = i;
    emit(PlayerEvent::Kind::Hit, i);
}

// FUN_001166f0
void Player::onMiss(float t, int gem, bool pass) {
    streak_ = 0;
    crowdUpdate(0.0f, t);
    if (gem != -1 && chart_.gems[gem].ms - t > kMissAttributionMs) --gem;
    spGemMiss(gem);
    finalizeHeld(t);
    recomputeMultiplier();
    // UnmuteBeatMatchAudio_v4 (miss, overstrum) / _v5 (pass): StopSlip, out of hit state.
    stopSlip();
    hitState_ = false;
    emit(pass ? PlayerEvent::Kind::Pass : PlayerEvent::Kind::Miss, gem);
}

// Sustain points accrue while the note is held (FUN_00116ef0 / FUN_0011d4f0):
// earned = round(clamp((t - start) / duration_ms, 0, 1) * max_tail), the
// difference scored at the current multiplier; once all of it is earned the
// held note is finished at once (FUN_00116f50), even while still held.
void Player::accrueHeld(float t) {
    if (held_note_.gem < 0 || held_note_.gem != sustainGem_) return;
    const Gem& g = chart_.gems[held_note_.gem];
    float frac = g.durationMs > 0 ? std::clamp((t - g.ms) / g.durationMs, 0.0f, 1.0f) : 1.0f;
    int earned = static_cast<int>(frac * float(held_note_.maxTail) + 0.5f);
    score_ += (earned - held_note_.earned) * multiplier_;
    held_note_.earned = earned;
    if (earned == held_note_.maxTail) finalizeHeld(t);
}

// FUN_00116f50 with FUN_0011d5c8: the crowd sees the fraction of the note kept.
void Player::finalizeHeld(float t) {
    if (held_note_.gem < 0) return;
    if (held_note_.earned < held_note_.maxTail) {
        int gem = held_note_.gem;
        accrueHeld(t);
        if (held_note_.gem != gem) return;  // accrueHeld finished it
    }
    float fraction = float(rules_.headPoints + held_note_.earned) / float(rules_.headPoints + held_note_.maxTail);
    int gem = held_note_.gem;
    held_note_ = {};
    crowdUpdate(fraction, t);
    events_.push_back({PlayerEvent::Kind::HeldNoteReleased, gem, fraction});  // FUN_00116f50
}

// FUN_001171b8 + FUN_0011d268
void Player::crowdUpdate(float target, float t) {
    if (failed_ || !crowdActive_) return;
    float k = (spDeployed_ && target > raw_) ? rules_.spCrowdBoost : 1.0f;
    float progress = song_.endMs > 0 ? std::min(1.0f, t / song_.endMs) : 1.0f;
    float w = crowd_.noteWeight * k;
    float raw = std::clamp((1 - w) * raw_ + w * target, 0.0f, 1.0f);
    float rating = 0;
    if (raw > crowd_.rangeLow)
        rating = raw >= crowd_.rangeHigh ? 1.0f : (raw - crowd_.rangeLow) / (crowd_.rangeHigh - crowd_.rangeLow);
    if (rating_ <= crowd_.loseLevel && rating <= rating_) return;
    if (rating <= crowd_.warningLevel && progress <= crowd_.freeRide) return;
    rating_ = rating;
    raw_ = raw;
    if (rating_ < crowd_.loseLevel && !noFail_) {  // FUN_0011d3b8
        failed_ = true;
        crowdActive_ = false;
        emit(PlayerEvent::Kind::Failed, -1);
    }
}

void Player::recomputeMultiplier() {
    multiplier_ = rules_.multiplierForStreak(streak_) * (spDeployed_ ? rules_.spMultiplier : 1);
}

// ----------------------------------------------------------------- star power

void Player::spAdd(float amount) { spMeter_ = std::clamp(spMeter_ + amount, 0.0f, 1.0f); }

// FUN_0011c390
void Player::spGemHit(int i) {
    if (i <= spLast_) return;
    if (spDeployed_) {
        spGemMiss(i);
        return;
    }
    spLast_ = i;
    int p = chart_.gems[i].phrase;
    if (p < 0) {
        spState_ = 0;
        return;
    }
    emit(PlayerEvent::Kind::StarGemHit, i);
    const StarPhrase& ph = chart_.phrases[p];
    if (i == ph.firstGem) spState_ = 2;
    if (spState_ == 2 && i == ph.lastGem) {
        spAdd(phraseBoost_ >= 0 ? phraseBoost_ : rules_.spPhrase);
        spState_ = 3;
        emit(PlayerEvent::Kind::PhraseCaptured, i);
    }
}

// FUN_0011c700
void Player::spGemMiss(int i) {
    if (i < 0 || i <= spLast_) return;
    spLast_ = i;
    int p = chart_.gems[i].phrase;
    if (p < 0) {
        spState_ = 0;
        return;
    }
    const StarPhrase& ph = chart_.phrases[p];
    if (!(i != ph.firstGem && spState_ == 1)) {
        spState_ = 1;
        phraseBroken_[p] = 1;
        emit(PlayerEvent::Kind::PhraseMissed, i);
    }
    if (i == ph.lastGem) spState_ = 0;
}

// FUN_00116930: whammy_speed (0.05) per second [I] and whammy_timeout (0.5 s) [I].
void Player::setWhammyValue(float value, float t) {
    if (whammyLastT_ >= 0 && t > whammyLastT_) {
        float speed = std::fabs(value - whammyValue_) / ((t - whammyLastT_) / 1000.0f);
        if (speed > 0.05f && std::fabs(value - whammyValue_) > 0.004f) whammyMovedT_ = t;
    }
    whammyMoving_ = t - whammyMovedT_ < 500.0f;
    whammyValue_ = value;
    whammyLastT_ = t;
}

// FUN_0011c9a0
void Player::starPowerButton(float t) {
    // FUN_0011c9a0: also the HUD's displayed meter must have reached ready_level.
    if (failed_ || spDeployed_ || spMeter_ < rules_.spReadyLevel || hudStarLevel_ < rules_.spReadyLevel) return;
    spDeployed_ = true;
    spLastMeasure_ = song_.tempo.tickToMeasure(song_.tempo.msToTick(t));
    recomputeMultiplier();
    emit(PlayerEvent::Kind::StarPowerStart, -1);
}

// set_crowd_rating (FUN_0011d3f8): the rating, and the raw value that gives it.
void Player::setCrowdRating(float rating) {
    rating_ = std::clamp(rating, 0.0f, 1.0f);
    raw_ = crowd_.rangeLow + rating_ * (crowd_.rangeHigh - crowd_.rangeLow);
}

// fill_star_power / empty_star_power / set_star_power (FUN_0011cd68).
void Player::setStarPower(float level) {
    spMeter_ = std::clamp(level, 0.0f, 1.0f);
    hudStarLevel_ = spMeter_;
}

float Player::percentComplete(float t) const { return song_.endMs > 0 ? std::clamp(t / song_.endMs, 0.0f, 1.0f) * 100 : 0; }

// FUN_0011c090
void Player::spPoll(float t) {
    double tick = song_.tempo.msToTick(t);
    double measure = song_.tempo.tickToMeasure(tick);
    // FUN_00116930: whammying a held Star Power note (phrase armed or captured
    // [I]); in a tutorial any held sustain. whammy_start every poll while the
    // bar is active, whammy_end every poll once idle (and held 500 ms).
    bool starNote = held_note_.gem >= 0 && chart_.gems[size_t(held_note_.gem)].phrase >= 0 && spState_ >= 2;
    bool eligible = whammySp_ && held_note_.gem >= 0 && (tutorialMode_ || starNote);
    if (eligible) {
        if (whammying()) emit(PlayerEvent::Kind::WhammyStart, held_note_.gem);
        else if (t - whammyMovedT_ >= 500 && t - heldSinceMs_ >= 500) emit(PlayerEvent::Kind::WhammyEnd, held_note_.gem);
    }
    if (!spDeployed_) {
        if (eligible && whammying() && tick > spLastTick_)
            spAdd(static_cast<float>(rules_.spWhammyRate * (tick - spLastTick_) / song_.tempo.division()));
    } else {
        spMeter_ -= static_cast<float>((deployRate_ >= 0 ? deployRate_ : rules_.spDeployRate) * (measure - spLastMeasure_));
        if (spMeter_ <= 0) {
            spMeter_ = 0;
            spDeployed_ = false;
            recomputeMultiplier();
            emit(PlayerEvent::Kind::StarPowerEnd, -1);
        }
    }
    spLastTick_ = tick;
    spLastMeasure_ = measure;
}
