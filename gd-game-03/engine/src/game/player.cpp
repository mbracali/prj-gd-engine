// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/player.h"

#include "game/band_performer.h"

#include <algorithm>
#include <cmath>

// Every rule below follows docs/FINDINGS.md. Addresses are GH2's
// default.xex; [C] = confirmed there, [I] = inferred (the reason is given).

namespace {

int highestFret(uint32_t mask) {
    for (int f = 4; f >= 0; --f)
        if (mask & (1u << f)) return f;
    return -1;
}

// 0x822de310 / 0x822de428: round half away from zero [C].
int roundHalfAway(double v) { return static_cast<int>(std::lround(v)); }

}  // namespace

Player::Player(const Song& song, Part part, Difficulty difficulty, const Rules& rules, ControllerMode mode, bool noFail,
               int index, const ScoreConfig* config)
    : song_(song),
      chart_(song.chart(part, difficulty)),
      rules_(rules),
      crowd_((config ? *config : rules.solo).crowd[size_t(difficulty)]),
      sp_((config ? *config : rules.solo).starPower),
      difficulty_(difficulty),
      mode_(mode),
      noFail_(noFail),
      index_(index),
      hit_(chart_.gems.size(), 0),
      phraseBroken_(chart_.phrases.size(), 0) {
    // Crowd set-up 0x822de640: rating 0.5, raw = the middle of the range [C].
    rating_ = 0.5f;
    raw_ = crowd_.rangeLow + (crowd_.rangeHigh - crowd_.rangeLow) * 0.5f;
    // Base score 0x826a0ab0 [C]: Σ frets × (head + (sustain ? duration_ticks × tail / 480 : 0)),
    // the tail part an integer division.
    for (size_t i = 0; i < chart_.gems.size(); ++i) {
        const Gem& g = chart_.gems[i];
        if (!owned(int(i))) continue;
        int tail = g.noSustain ? 0 : g.durationTicks * rules_.tailPoints / rules_.tailTicksPerBeat;
        baseScore_ += g.fretCount() * (rules_.headPoints + tail);
    }
}

void Player::setPracticeRange(int firstGem, int startTick, int endTick) {
    firstPlayable_ = std::max(0, firstGem);
    practiceStartTick_ = startTick;
    practiceEndTick_ = endTick;
}

std::vector<PlayerEvent> Player::takeEvents() {
    std::vector<PlayerEvent> out;
    out.swap(events_);
    return out;
}

// For drawing: a phrase still worth playing. Broken by a miss or by playing
// it during Star Power (0x822dc368 / 0x822dc090) [C]; a phrase whose first
// gem went by unhit can no longer be armed [C: arming needs the first gem].
bool Player::phraseIntact(int phrase) const {
    if (phrase < 0 || phraseBroken_[size_t(phrase)] || spActive_) return false;
    int first = chart_.phrases[size_t(phrase)].firstGem;
    return !(first >= 0 && first <= sweep_ && !hit_[size_t(first)]);
}

int Player::stars() const {
    // 0x822d6410 [C]: 5 at >= five × base, 4 at >= four × base, else 3.
    const auto& [four, five] = rules_.starThresholds[size_t(difficulty_)];
    if (float(score()) >= five * float(baseScore_)) return 5;
    if (float(score()) >= four * float(baseScore_)) return 4;
    return 3;
}

bool Player::whammying() const { return whammyOn_; }

// ======================================================= controller layer

// Strum buttons -> player sink 0x8233aa80 -> Swing(highest held fret or -1,
// strum = 1) (guitar controller 0x8234c538) [C].
void Player::strum(float t) {
    if (failed_ || mode_ != ControllerMode::Guitar) return;  // the joypad strums with its frets
    // 0x8233aa80: a strum <= 52 ms after the last accepted one is dropped
    // (`last + 52 >= now`, 0x8205807c) [C].
    if (lastStrumMs_ + rules_.strumRepeatMs >= t) return;
    lastStrumMs_ = t;
    swing(t, highestFret(held_), true);
}

// Guitar controller 0x8234c538: FretDown(slot); then, if no held fret is
// above it (held < 1 << slot), NonStrumSwing(slot, hammer = 1) [C].
// There is no "strummed since all frets were released" gate in GH2: the
// controller's +0x48 byte is never read [C].
void Player::fretDown(int fret, float t) {
    if (failed_ || fret < 0 || fret > 4) return;
    if (mode_ == ControllerMode::Joypad) {
        // The joypad watcher's press (PS2 0x0024e050) has its own sustain
        // rule and no pending strum; the held mask is kept for drawing.
        held_ |= 1u << fret;
        joypadPress(fret, t);
        return;
    }
    const bool noneAbove = held_ < (1u << fret);
    watcherFretDown(fret, t);
    if (noneAbove) nonStrumSwing(t, fret, true);
}

// Guitar controller 0x8234c6d0: FretUp(slot); then, if frets remain and the
// highest remaining one is below the released one, NonStrumSwing(highest,
// hammer = 0) [C].
void Player::fretUp(int fret, float t) {
    if (failed_ || fret < 0 || fret > 4) return;
    watcherFretUp(fret, t);
    if (mode_ == ControllerMode::Joypad) return;
    if (held_ != 0 && highestFret(held_) < fret) nonStrumSwing(t, highestFret(held_), false);
}

// ================================================================ watcher

// 0x8234ddd0 [C]: binary search, then the closer of the two neighbours,
// ties to the earlier gem.
int Player::nearestGem(float t) const {
    const auto& g = chart_.gems;
    if (g.empty()) return -1;
    auto it = std::lower_bound(g.begin(), g.end(), t, [](const Gem& x, float v) { return x.ms < v; });
    if (it == g.begin()) return 0;
    if (it == g.end()) return static_cast<int>(g.size()) - 1;
    int i = static_cast<int>(it - g.begin());
    return std::abs(t - g[size_t(i) - 1].ms) <= std::abs(g[size_t(i)].ms - t) ? i - 1 : i;
}

// 0x8234f5d0 [C]: |(t + offset) - gem.start_ms| <= slop (100, every difficulty).
bool Player::inWindow(int gem, float t) const {
    return std::abs(t + offset_ - chart_.gems[size_t(gem)].ms) <= rules_.slopMs;
}

// 0x8234efc8 [C]: HOPO-able, k >= 1, gem k-1 hit and the last hit gem.
bool Player::hopoEligible(int k) const {
    return k >= 1 && chart_.gems[size_t(k)].hopo && hit_[size_t(k) - 1] && lastHit_ == k - 1;
}

// GuitarWatcher::Swing 0x8234eb60 [C, from disassembly].
void Player::swing(float t, int slot, bool isStrum) {
    if (t == chordFixMs_ && !isStrum) return;
    const int n = static_cast<int>(chart_.gems.size());
    if (n == 0) {
        watcherMiss(t, slot, -1);
        return;
    }
    const int i = nearestGem(t + offset_);
    int j = i;
    if (!owned(i) || hit_[size_t(i)]) {
        if (i + 1 < n) j = i + 1;
        if (i == hopoHit_) {  // a strum right after a hammer-on / pull-off: forgiven, no miss
            hopoHit_ = -1;
            emit(PlayerEvent::Kind::SwingAtHopo, i);
            return;
        }
    }
    if (j < firstPlayable_) return;
    if (inWindow(j, t) && !hit_[size_t(j)] && owned(j)) {
        const Gem& g = chart_.gems[size_t(j)];
        // Single: the highest held fret is the gem's (lower frets may be
        // held: anchoring). Chord: the held mask is exactly the chord.
        const bool match = g.fretCount() == 1 ? (slot >= 0 && g.frets == (1u << slot)) : g.frets == held_;
        if (match) {
            watcherHit(t, slot, j, false);
            return;
        }
        if (pending_.gem >= 0) {
            const Pending p = pending_;
            clearPending();
            watcherMiss(t, p.slot, p.gem);
        }
        if (isStrum || g.fretCount() > 1) {
            // 60 ms for the frets to come right (watcher +0xa8 = 60.0, 0x82002af8) [C].
            // The after-HOPO flag is just "the HOPO-hit gem is j - 1" (PS2
            // 0x0024d560) [C]: with no HOPO hit (-1) a wrong strum on gem 0
            // also counts, so it expires silently and only the pass penalises.
            pending_ = {j, slot, t + rules_.pendingStrumMs, j - 1 == hopoHit_};
            return;
        }
    }
    watcherMiss(t, slot, j);  // overstrum
}

// NonStrumSwing 0x8234f3e0 -> 0x8234f210 [C, from disassembly]. The joypad
// watcher has none (+0x1c is a no-op) [C].
void Player::nonStrumSwing(float t, int slot, bool hammer) {
    if (mode_ != ControllerMode::Guitar || slot < 0) return;
    if (t == lastHopoTryMs_) return;  // +0x94: the time of the last hit (set by OnHit, 0x8234eb28) [C]
    const int n = static_cast<int>(chart_.gems.size());
    if (n == 0) return;
    int k = nearestGem(t + offset_);
    if ((!owned(k) || hit_[size_t(k)]) && k + 1 < n) ++k;
    if (k < firstPlayable_ || !hopoEligible(k)) return;
    // Window -slop .. slop + 60 = -100 .. +160 ms (0x8234f294..0x8234f2b0) [C].
    const float dt = t + offset_ - chart_.gems[size_t(k)].ms;
    if (dt < -rules_.slopMs || dt > rules_.slopMs + rules_.pendingStrumMs) return;
    if (hit_[size_t(k)] || !owned(k) || k == lockedGem_) return;
    const bool noneAbove = (held_ >> (slot + 1)) == 0;  // lower frets may be held (anchoring) [C]
    if (chart_.gems[size_t(k)].frets == (1u << slot) && noneAbove) {
        watcherHit(t, slot, k, true);
    } else if (hammer && noneAbove) {
        lockedGem_ = k;  // a wrong hammer-on locks the gem for HOPO tries; a wrong pull-off does not [C]
    }
}

// FretDown: the held mask (+0x84), the sustain release rules, then the
// pending-strum fret change with the pressed slot [C].
void Player::watcherFretDown(int slot, float t) {
    const uint32_t bit = 1u << slot;
    held_ |= bit;
    if (sustainGem_ >= 0) {
        // 0x823500c8 / 0x8234f070 [C]: a fret pressed above a single note
        // ends the sustain (a lower one, an anchor, does not); on a chord any
        // press does (the chord's frets are all held already).
        const Gem& g = chart_.gems[size_t(sustainGem_)];
        const bool release = g.fretCount() == 1 ? slot > highestFret(g.frets) : true;
        if (release) endSustain(t, true);
    }
    fretChange(t, slot);
}

// FretUp: any fret of the sustained gem let go ends it (0x8234f110) [C];
// then the pending-strum fret change with -1 [C].
void Player::watcherFretUp(int slot, float t) {
    const uint32_t bit = 1u << slot;
    held_ &= ~bit;
    if (sustainGem_ >= 0 && (chart_.gems[size_t(sustainGem_)].frets & bit)) endSustain(t, true);
    fretChange(t, -1);
}

// Pending wrong strum, fret change 0x8234edf8 [C], while now < deadline:
// a single gem and a pressed fret hits when the pressed fret is the gem's;
// otherwise (a chord, or any release) when held == gem mask. A pressed fret
// that does not match is a miss now; a release that does not match waits.
void Player::fretChange(float t, int pressed) {
    if (pending_.gem < 0 || !(t < pending_.deadline)) return;
    const int gem = pending_.gem;
    const Gem& g = chart_.gems[size_t(gem)];
    const bool match = (g.fretCount() == 1 && pressed >= 0) ? g.frets == (1u << pressed) : held_ == g.frets;
    if (match) {
        clearPending();
        watcherHit(t, pressed >= 0 ? pressed : highestFret(held_), gem, false);
        // A HOPO-able gem hit this way also tells the sinks "swingAtHopo"
        // (0x8234fcb8 at 0x8234eee4) [C].
        if (g.hopo) emit(PlayerEvent::Kind::SwingAtHopo, gem);
        if (g.fretCount() > 1) chordFixMs_ = t;  // +0x90
    } else if (pressed >= 0) {
        const int slot = pending_.slot;
        clearPending();
        watcherMiss(t, slot, gem);
    }
}

// Joypad watcher press, PS2 0x0024e050 / 0x0024e2d0 [C]: every fret press
// is a swing for that fret.
//  1. any press ends the held sustain (a release, whatever the fret);
//  2. the target gem: the nearest, or the next one when that is hit or not
//     this player's; none before the first playable gem;
//  3. a chord waiting for its other frets (+0x88): when it is the target the
//     press continues it with no deadline test (a fret outside the chord is
//     a miss of the chord, the full chord a hit); otherwise the waiting chord
//     is dropped with a miss (its slot, the target gem) and the press goes on;
//  4. outside the hit window: a miss. A single gem: hit when the fret is its
//     fret, else a miss. A chord: a fret of the chord opens a 50 ms window
//     (+0x84 = 50.0) for the rest, any other fret misses.
// The window running out is a miss in the poll (0x0024e450). No 52 ms strum
// filter (it would block chords) [I].
void Player::joypadPress(int slot, float t) {
    const uint32_t bit = 1u << slot;
    endSustain(t, true);
    const int n = static_cast<int>(chart_.gems.size());
    if (n == 0) {
        watcherMiss(t, slot, -1);
        return;
    }
    const int i = nearestGem(t + offset_);
    int j = i;
    if ((!owned(i) || hit_[size_t(i)]) && i + 1 < n) j = i + 1;
    if (j < firstPlayable_) return;
    if (padChordGem_ >= 0) {
        if (padChordGem_ == j) {
            const int gem = padChordGem_;
            const Gem& g = chart_.gems[size_t(gem)];
            if (!(g.frets & bit)) {
                clearPadChord();
                watcherMiss(t, slot, gem);
            } else if ((padChordGot_ | bit) == g.frets) {
                clearPadChord();
                watcherHit(t, slot, gem, false);
            } else {
                padChordGot_ |= bit;
            }
            return;
        }
        const int padSlot = padChordSlot_;
        clearPadChord();
        watcherMiss(t, padSlot, j);
    }
    if (inWindow(j, t) && !hit_[size_t(j)] && owned(j)) {
        const Gem& g = chart_.gems[size_t(j)];
        if (g.fretCount() == 1) {
            if (g.frets == bit) watcherHit(t, slot, j, false);
            else watcherMiss(t, slot, j);
            return;
        }
        if (g.frets & bit) {
            padChordGem_ = j, padChordGot_ = bit, padChordSlot_ = slot, padChordDeadline_ = t + rules_.joypadChordMs;
            return;
        }
    }
    watcherMiss(t, slot, j);
}

// OnHit -> 0x8234fe20 [C]: marks the gem hit, sets the last hit gem (and
// the HOPO-hit gem +0x88), notifies the player, and tracks the gem as the
// held note until start_ms + duration_ms. The watcher's previous held note is
// dropped without a release (PS2 0x002490f8). The PS2 (0x00248910) tracks
// every gem whose duration_ms is not 0, which the parser never clears for
// short notes (0x00236428): a note of 160 ticks or less is held too, only
// its points have no tail [C].
void Player::watcherHit(float t, int slot, int gem, bool hopo) {
    (void)slot;
    sustainGem_ = -1;
    hit_[size_t(gem)] = 1;
    lastHit_ = gem;
    // vtable +0x6c (0x8234eb28) at the end of 0x8234fe20 [C]: a HOPO hit
    // sets the HOPO-hit gem (+0x88); every hit sets +0x94 (the time
    // NonStrumSwing compares) and clears the pending strum.
    if (hopo) hopoHit_ = gem;
    lastHopoTryMs_ = t;
    clearPending();
    clearPadChord();
    playerHit(t, gem);
    if (hopo) emit(PlayerEvent::Kind::Hopo, gem);
    const Gem& g = chart_.gems[size_t(gem)];
    if (g.durationMs != 0) {
        sustainGem_ = gem;
        sustainEndMs_ = g.ms + g.durationMs;
    }
}

// Overstrum / miss: 0x823503d0 -> 0x8234f860 [C, from disassembly]. The
// current sustain ends first (0x8234f600, before the gap test); then at most
// one report per 50 ms (`last + 50 < now`, 0x82002228). The gem: the given
// one, the next (vtable +0x74 = 0x8234f768) when it is the gem the sweep
// passed last (+0x4c), and the last gem of the list when that leaves none
// (-1). Then the player is told.
void Player::watcherMiss(float t, int slot, int gem) {
    (void)slot;
    endSustain(t, false);
    if (!(lastOverstrumMs_ + rules_.overstrumGapMs < t)) return;
    lastOverstrumMs_ = t;
    const int n = static_cast<int>(chart_.gems.size());
    if (gem >= 0 && gem == sweep_) gem = gem + 1 < n ? gem + 1 : -1;
    if (gem < 0) gem = n - 1;
    playerMiss(t, gem);
}

// The watcher's held note ends. At its end time or with a miss it is just
// dropped (PS2 0x00248c80 / 0x002495d8 -> 0x002490f8): the player's record
// then completes in its poll or is finished by the penalty [C]. A release
// (`early`: a fret let go or pressed, PS2 0x00249038) tells the player
// (sustain_released, 0x00116f28 -> 0x00111810). A release of a real sustain
// with time left also silences the guitar track (GH1's UnmuteBeatMatchAudio)
// [I: the PS2 release only reaches the player and the highway, so short
// notes, now released too, keep the guitar on].
void Player::endSustain(float t, bool early) {
    if (sustainGem_ < 0) return;
    const int gem = sustainGem_;
    const Gem& g = chart_.gems[size_t(gem)];
    sustainGem_ = -1;
    sustainEndMs_ = 0;
    if (!early) return;
    if (!g.noSustain && g.ms + g.durationMs - t > 0) guitarAudible_ = false;
    sustainReleased(t, gem);
}

// The player's "sustain released" (PS2 0x00111810) [C]: when the open record
// is the released gem's, its points are brought up to now and it is
// finished; a record of another gem (a short note was released while an
// earlier sustain still accrued) is thrown away: no more points, no crowd
// update.
void Player::sustainReleased(float t, int gem) {
    if (tail_.gem < 0) return;
    if (tail_.gem != gem) {
        tail_ = {};
        return;
    }
    accrueSustain(t);
    finishSustain(t);
}

// Miss sweep 0x82350718 [C, from disassembly]:
//   late = (k < first_playable) ? 0 : slop + (hopo_eligible(k) ? 60 : 0) - offset   (vtable +0x78 = 0x8234f1c8)
//   deadline = min(gem[k].ms + late, (gem[k].ms + gem[k+1].ms)/2 - offset)  (no midpoint for the last gem)
//   another player's gem: deadline = gem[k].ms
// Stops at the pending gem (vtable +0x70 = 0x8234e948). Every unhit gem is
// swept; the cursor never jumps to the last hit gem.
void Player::sweep(float t) {
    const auto& g = chart_.gems;
    const int n = static_cast<int>(g.size());
    for (int k = sweep_ + 1; k < n; ++k) {
        if (k == pending_.gem) return;
        const float late = k < firstPlayable_ ? 0.0f
                                              : rules_.slopMs + (hopoEligible(k) ? rules_.pendingStrumMs : 0.0f) - offset_;
        float deadline = g[size_t(k)].ms + late;
        if (k + 1 < n) deadline = std::min(deadline, (g[size_t(k)].ms + g[size_t(k) + 1].ms) * 0.5f - offset_);
        if (!owned(k)) deadline = g[size_t(k)].ms;
        if (deadline >= t) return;
        sweep_ = k;
        if (hit_[size_t(k)] || !owned(k)) continue;  // another player's gem passes without a penalty [C]
        if (k < firstPlayable_) continue;            // "skipped" (before the practice start) [C]
        playerPass(t, k);
    }
}

// Base watcher poll 0x82350d68 [C]: end a finished sustain, (autoplay
// cheat), miss sweep, passed-gem events, whammy, then the guitar extra
// 0x8234f028 (pending expiry, HOPO forgiveness expiry). Then the player's
// own poll: sustain points and Star Power.
void Player::update(float t) {
    if (failed_) return;
    if (sustainGem_ >= 0 && sustainEndMs_ < t) endSustain(t, false);  // `end < now` (PS2 0x00248c80) [C]
    sweep(t);
    whammyPoll(t);
    // 0x8234e968 [C]: a pending strum past its deadline is a miss, unless it
    // was set right after a HOPO hit; then it is dropped silently. Past means
    // `deadline < now` (PS2 0x0024de90) [C].
    if (pending_.gem >= 0 && pending_.deadline < t) {
        const Pending p = pending_;
        clearPending();
        if (!p.afterHopo) watcherMiss(t, p.slot, p.gem);
    }
    // 0x8234ef50 [C]: the HOPO forgiveness ends when that gem leaves the hit window.
    if (hopoHit_ >= 0 && !inWindow(hopoHit_, t)) hopoHit_ = -1;
    // The joypad chord window ran out (`deadline < now`, PS2 0x0024e450) [C]:
    // a miss with the slot that opened it.
    if (padChordGem_ >= 0 && padChordDeadline_ < t) {
        const int gem = padChordGem_, slot = padChordSlot_;
        clearPadChord();
        watcherMiss(t, slot, gem);
    }
    if (failed_) return;
    accrueSustain(t);
    spPoll(t);
}

// ================================================================= player

bool Player::inPractice(int gem) const {
    const int tick = chart_.gems[size_t(gem)].tick;
    return tick >= practiceStartTick_ && tick < practiceEndTick_;
}

// Score add 0x822c6a58 -> 0x822dcfb0 [C]: score (a float) += points × the
// normalisation factor (0x822bb320: 1 with one track) × base multiplier ×
// Star Power multiplier (0x822c6c20). A co-op player (0x822DEB30) also adds
// the normalised points to the band, which applies its own multiplier.
void Player::addScore(int points) {
    const float p = float(points) * norm_;
    score_ += float(baseMultiplier_ * spMultiplier()) * p;
    if (band_) band_->addScore(p);
}

// End streak, vtable +0x38: 0x822dd028 (the stats' streak reset) and, for a
// co-op player, the band's too (0x822DEBF8) [C].
void Player::endStreak() {
    setStreak(0);
    if (band_) band_->endStreak();
}

// Streak and multiplier (0x822ddf70, 0x822d66b0) [C]: build_streak when the
// multiplier goes up. The 360's eight_x (0x822dd298) is not in the PS2: its
// performer hit (0x0010fa98) only sends build_streak and the executable has
// no eight_x string [C].
void Player::setStreak(int streak) {
    const int oldBase = baseMultiplier_;
    streak_ = streak;
    longestStreak_ = std::max(longestStreak_, streak_);
    baseMultiplier_ = rules_.multiplierForStreak(streak_);
    if (baseMultiplier_ > oldBase) emit(PlayerEvent::Kind::BuildStreak, -1, float(baseMultiplier_));
}

// Hit 0x822c9338 [C], in this order:
//  1. gems outside the practice range [+0xf0, +0xf4) are ignored;
//  2. streak += 1, hits += 1 (0x822ddf70 / 0x822dcaa8), build_streak; then
//     the penalised-gem mark drops to this gem when it is above it (PS2);
//  3. the open sustain record accrues up to the new gem's start_ms and is
//     finished only if that completes it (PS2 0x001111d0 -> 0x00112280, the
//     time passed is the gem's start) [C];
//  4. head points popcount × 50 with the current (new) multiplier
//     (0x822c6b18 -> 0x822c6a58): the 10th note already scores at 2×;
//  5. a gem with sustain opens a sustain record, replacing an unfinished one
//     with no crowd update and no more points (PS2 0x0012a0e0) [C]; otherwise
//     rock meter target 1.0 and the open record keeps accruing in the poll;
//  6. Star Power gem hit.
void Player::playerHit(float t, int gem) {
    if (!inPractice(gem)) return;
    const Gem& g = chart_.gems[size_t(gem)];
    ++notesHit_;
    setStreak(streak_ + 1);
    if (band_) band_->hit();  // 0x822DEBA8 [C]
    // A hit below the highest penalised gem lowers that mark to the hit gem
    // (PS2 0x00111254..0x00111264, after the streak update, before the
    // sustain accrual) [C], so the gems after it can be penalised again.
    if (gem < penalisedGem_) penalisedGem_ = gem;
    // Timing class 0x822d6260: computed on every hit by 0x822e2748, which
    // drops the result [C] (counted here for the stats only).
    const float err = std::abs(t + offset_ - g.ms);
    if (err <= float(rules_.perfectMs)) ++perfect_;
    else if (err <= float(rules_.goodMs)) ++good_;
    accrueSustain(g.ms);
    addScore(g.fretCount() * rules_.headPoints);
    if (g.noSustain) {
        crowdUpdate(1.0f, t);
    } else {
        // 0x822de310 [C]: max_tail = round_half_away(duration_ticks × 25 / 480) × frets.
        const double beats = double(g.durationTicks) / double(rules_.tailTicksPerBeat);
        tail_ = {gem, roundHalfAway(beats * rules_.tailPoints) * g.fretCount(), 0};
    }
    spGemHit(gem);
    guitarAudible_ = true;
    emit(PlayerEvent::Kind::Hit, gem);
}

// Miss / overstrum 0x822c9478 -> 0x822c8d40 [C].
void Player::playerMiss(float t, int gem) {
    if (gem >= 0 && !inPractice(gem)) return;
    penalise(t, gem);
    emit(PlayerEvent::Kind::Miss, gem);
}

// Pass 0x822c9498 [C]: counts the pass and penalises only when the gem was
// not already penalised by an overstrum (+0xac). No re-attribution of early
// misses (GH1's 500 ms rule is not in GH2's player). Then, for a phrase's
// first gem (which the penalty skips), the Star Power phrase miss (PS2
// 0x00111358 -> 0x0011e6a8) [C]: an unhit first gem breaks the phrase state.
void Player::playerPass(float t, int gem) {
    if (!inPractice(gem)) return;
    ++passes_;
    emit(PlayerEvent::Kind::Pass, gem);
    if (gem > penalisedGem_) penalise(t, gem);
    const int phrase = chart_.gems[size_t(gem)].phrase;
    if (phrase >= 0 && chart_.phrases[size_t(phrase)].firstGem == gem) spPhraseMiss(gem);
}

// 0x822c8d40 [C]: end_streak, streak reset (the longest kept, 0x822dc518),
// rock meter target 0.0, Star Power phrase miss (not for a phrase's first
// gem), the sustain ended. The record is finished with the points of the
// last poll, not brought up to now (PS2 0x001114c0 -> 0x00112348) [C].
void Player::penalise(float t, int gem) {
    emit(PlayerEvent::Kind::EndStreak, gem, float(streak_));
    endStreak();
    crowdUpdate(0.0f, t);
    const int phrase = gem >= 0 ? chart_.gems[size_t(gem)].phrase : -1;
    if (gem < 0 || phrase < 0 || chart_.phrases[size_t(phrase)].firstGem != gem) spPhraseMiss(gem);
    endSustain(t, false);
    finishSustain(t);
    if (gem > penalisedGem_) penalisedGem_ = gem;  // +0xac
    ++penalties_;
    guitarAudible_ = false;
}

// Sustain points (0x822de310, 0x822de428, 0x822d62b0) [C]: every poll
// earned = round_half_away(clamp((t - start_ms) / duration_ms, 0, 1) × max_tail),
// the difference added with the current multiplier; at max the note is
// finished at once (PS2 0x00112280 / 0x0012a168 / 0x0012a150). The PS2 adds
// the signed difference: an accrual at an earlier time (a hit's, step 3)
// can take points back [C].
void Player::accrueSustain(float t) {
    if (tail_.gem < 0) return;
    const Gem& g = chart_.gems[size_t(tail_.gem)];
    const float frac = g.durationMs > 0 ? std::clamp((t - g.ms) / g.durationMs, 0.0f, 1.0f) : 1.0f;
    const int earned = roundHalfAway(double(frac) * tail_.max);
    if (earned != tail_.earned) addScore(earned - tail_.earned), tail_.earned = earned;
    if (tail_.earned == tail_.max) finishSustain(t);
}

// Sustain end 0x822c86f8 [C]: rock meter target (earned + 50) / (max + 50).
// TODO: blew_big_note (earned / max < 0.1 on a MIDI 110 big note) only plays
// a crowd sound (ui/gen/game.dta) and has no gameplay effect [C].
// It uses the points earned so far and accrues nothing itself (PS2
// 0x00112348); callers that bring the record up to now do so first.
void Player::finishSustain(float t) {
    if (tail_.gem < 0) return;
    const Tail done = tail_;
    tail_ = {};
    const float head = float(rules_.headPoints);
    crowdUpdate((float(done.earned) + head) / (float(done.max) + head), t);
    emit(PlayerEvent::Kind::HeldNoteReleased, done.gem, done.max > 0 ? float(done.earned) / float(done.max) : 1.0f);
}

// Crowd update 0x822de570 (through 0x822dd3e0) [C]:
//   k = (target > raw && star power active) ? crowd_boost : 1
//   raw' = clamp01((1 - note_weight·k)·raw + note_weight·k·target)
//   rating' = raw' <= lo ? 0 : raw' >= hi ? 1 : (raw' - lo)/(hi - lo)
//   once at or below lose_level it can only rise; during the free ride
//   (progress <= free_ride) it cannot drop to warning_level.
//   Fail when rating < lose_level, unless the mode cannot fail.
void Player::setCrowdRating(float rating) {
    rating_ = std::clamp(rating, 0.0f, 1.0f);
    raw_ = crowd_.rangeLow + (crowd_.rangeHigh - crowd_.rangeLow) * rating_;
}

// A co-op player (0x822DEC68) [C]: its own crowd (weight 1), then the
// band's with the target mapped a + (b - a) × target and weight w.
void Player::crowdUpdate(float target, float t) {
    ownCrowdUpdate(target, t);
    if (band_) band_->crowdUpdate(bandA_ + (bandB_ - bandA_) * target, bandW_, t);
}

void Player::ownCrowdUpdate(float target, float t) {
    if (failed_) return;
    const float k = (target > raw_ && spActive_) ? sp_.crowdBoost : 1.0f;
    const float w = crowd_.noteWeight * k;
    const float raw = std::clamp((1 - w) * raw_ + w * target, 0.0f, 1.0f);
    float rating;
    if (raw <= crowd_.rangeLow) rating = 0;
    else if (raw >= crowd_.rangeHigh) rating = 1;
    else rating = (raw - crowd_.rangeLow) / (crowd_.rangeHigh - crowd_.rangeLow);
    // progress = song position / song length (0x822dd238) [C].
    const float progress = song_.endMs > 0 ? t / song_.endMs : 1.0f;
    if (rating_ <= crowd_.loseLevel && rating <= rating_) return;
    if (rating <= crowd_.warningLevel && progress <= crowd_.freeRide) return;
    raw_ = raw;
    rating_ = rating;
    // PS2 0x0010fc78 -> 0x0010fe30 [C]: only a performer that may end the
    // game fails (Player::mayFail: the solo player, the co-op band; never a
    // co-op or face-off player), so callers pass noFail for the others.
    if (rating_ < crowd_.loseLevel && !noFail_) {
        failed_ = true;
        emit(PlayerEvent::Kind::Failed, -1);
    }
}

PerformerState Player::state() const {
    PerformerState s;
    s.score = score();
    s.streak = streak_;
    s.multiplier = multiplier();
    s.baseMultiplier = baseMultiplier_;
    s.starMeter = meter_->value;
    s.spActive = spActive_;
    s.rock = rating_;
    s.warning = crowd_.warningLevel;
    return s;
}

void Player::forceFail() {
    if (failed_) return;
    failed_ = true;
    emit(PlayerEvent::Kind::Failed, -1);
}

// ============================================================= star power

// The meter, clamped to 0..1 (0x822db788) [C]; shared by both co-op players.
void Player::spAdd(float amount) { meter_->value = std::clamp(meter_->value + amount, 0.0f, 1.0f); }

// Gem hit 0x822dc090 [C], phrase state 0 none / 1 broken / 2 armed / 3 captured:
// not deployed: star_gem_hit, the first gem arms, the last gem of an armed
// phrase captures it (+ star_phrase); deployed: the phrase is broken (no
// capturing during Star Power); a gem not in a phrase: state 0.
void Player::spGemHit(int gem) {
    const int p = chart_.gems[size_t(gem)].phrase;
    if (p < 0) {
        spState_ = 0;
        return;
    }
    if (spActive_) {
        // PS2 0x0011e2b8 [C]: phrase_miss unless the state is already broken.
        if (spState_ != 1) emit(PlayerEvent::Kind::PhraseMissed, gem);
        spState_ = 1;
        phraseBroken_[size_t(p)] = 1;
        return;
    }
    emit(PlayerEvent::Kind::StarGemHit, gem);
    const StarPhrase& ph = chart_.phrases[size_t(p)];
    if (gem == ph.firstGem) spState_ = 2, armedPhrase_ = p;
    if (spState_ == 2 && gem == ph.lastGem) {
        spAdd(sp_.phrase);
        spState_ = 3;
        emit(PlayerEvent::Kind::PhraseCaptured, gem);
    }
}

// Phrase miss 0x822dc368, PS2 0x0011e6a8 -> 0x0011e7c8 phrase_miss [C]:
//  - no gem: phrase_miss only when a phrase is armed (state 2);
//  - a phrase gem: phrase_miss (state 1) when it is the first gem or the
//    state is not broken already; then, on the phrase's last gem, state 0;
//  - a gem outside every phrase: state 0.
// PhraseMissed is sent each time phrase_miss runs.
void Player::spPhraseMiss(int gem) {
    auto phraseMiss = [&](int p) {
        spState_ = 1;
        emit(PlayerEvent::Kind::PhraseMissed, gem);
        if (p >= 0) phraseBroken_[size_t(p)] = 1;
    };
    if (gem < 0) {
        if (spState_ == 2) phraseMiss(armedPhrase_);
        return;
    }
    const int p = chart_.gems[size_t(gem)].phrase;
    if (p >= 0) {
        const StarPhrase& ph = chart_.phrases[size_t(p)];
        if (gem == ph.firstGem || spState_ != 1) phraseMiss(p);
        phraseBroken_[size_t(p)] = 1;  // drawing: a missed gem means it cannot be captured any more
        if (gem != ph.lastGem) return;
    }
    spState_ = 0;
}

// Engage 0x822c80f8 [C]: +0xbc = the state; on TRUE, with the ready flag
// and Star Power not active, it deploys, but with a shared meter (co-op)
// only when the other player is engaged too, and then for both. Every call
// sends sp_engage (player, state) to the game (multi_coop's handler plays
// sp_deploystart.cue).
// PS2 0x00111940 [C]: the engaged flag (+0xc4) is stored first; with the
// mercury switch off (+0xb8, the tutorials' set_mercuryswitchenabled FALSE)
// nothing deploys. Otherwise an engage while ready and not active deploys
// (co-op: both, once the partner is engaged too). The sp_engage message then
// carries "mercury on, engaged, not active and ready" worked out after any
// deploy, so co-op's waiting first player gets 1 and a deploy gets 0.
void Player::starPowerEngage(bool engaged, float t) {
    (void)t;
    if (failed_) return;
    engaged_ = engaged;
    if (mercuryEnabled_ && engaged && !spActive_ && spReady_) {
        if (!partner_) spStart(true);
        else if (partner_->engaged_) spStart(true), partner_->spStart(true);
    }
    const bool waiting = mercuryEnabled_ && engaged && !spActive_ && spReady_;
    emit(PlayerEvent::Kind::StarPowerEngage, -1, waiting ? 1.0f : 0.0f);
}

// 0x822DBA90 [C]: start_using / stop_using.
void Player::spStart(bool on) {
    if (spActive_ == on) return;
    spActive_ = on;
    emit(on ? PlayerEvent::Kind::StarPowerStart : PlayerEvent::Kind::StarPowerEnd, -1);
}

// Star Power poll 0x822dbd60 / 0x822db3f8 [C]: while deployed the meter
// loses deploy_rate per measure and at 0 Star Power stops (stop_using /
// depleted, the ready flag cleared); otherwise, while the whammy flag is on,
// it gains whammy_rate per beat. Then star_power_ready once the meter
// reaches ready_level. Each co-op player's poll drains the shared meter, so
// with both deployed it empties twice as fast [C]. recharge_rate does have a
// reader on the PS2 (0x0011e1e0 adds it while not deployed), but it is 0.0 in
// both score configs of scoring.dta, so it is left out.
void Player::spPoll(float t) {
    const double tick = song_.tempo.msToTick(t);
    const double beat = tick / song_.tempo.division();
    const double measure = song_.tempo.tickToMeasure(tick);
    if (!spPolled_) {
        spPolled_ = true;
        spLastBeat_ = beat, spLastMeasure_ = measure;
    }
    if (spActive_) {
        spAdd(-static_cast<float>(sp_.deployRate * std::max(0.0, measure - spLastMeasure_)));
        if (meter_->value == 0) {
            spStart(false);
            spReady_ = false;
        }
    } else if (whammyOn_ && beat > spLastBeat_) {
        spAdd(static_cast<float>(sp_.whammyRate * (beat - spLastBeat_)));
    }
    spLastBeat_ = beat, spLastMeasure_ = measure;
    if (!spReady_ && meter_->value >= sp_.readyLevel) {
        spReady_ = true;
        emit(PlayerEvent::Kind::StarPowerReady, -1);
    }
}

// Whammy bar movement (0x822c8438) [C]: moving when |Δvalue / Δms| >
// whammy_speed × 0.01 (0.0005 per ms) [C; the 0..1 value range is I].
void Player::setWhammyValue(float value, float t) {
    if (whammyValueSeen_ && t > whammyLastMs_) {
        if (std::abs(value - whammyValue_) / (t - whammyLastMs_) > sp_.whammySpeed * 0.01f) whammyMovedMs_ = t;
    }
    whammyValueSeen_ = true;
    whammyValue_ = value;
    whammyLastMs_ = t;
}

// Whammy flag 0x822c8438 [C]: only while holding a sustain whose phrase is
// armed or captured (state 2 / 3), and while the bar moved within the last
// whammy_timeout (500 ms). Sends whammy_start / whammy_end. With the
// whammy gate (+0xbc) off the PS2 poll only notes the bar's value and time
// and leaves the flag as it is (0x00111d88) [C].
void Player::whammyPoll(float t) {
    if (!whammyStarPowerEnabled_) return;
    bool eligible = false;
    if (sustainGem_ >= 0 && tail_.gem == sustainGem_) {
        const int p = chart_.gems[size_t(sustainGem_)].phrase;
        eligible = p >= 0 && (spState_ == 2 || spState_ == 3);
    }
    const bool moving = whammyKey_ || t - whammyMovedMs_ <= sp_.whammyTimeout * 1000.0f;
    const bool on = eligible && moving;
    if (on != whammyOn_) emit(on ? PlayerEvent::Kind::WhammyStart : PlayerEvent::Kind::WhammyEnd, sustainGem_);
    whammyOn_ = on;
}
