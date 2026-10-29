// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/player.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int kPointsPerNote = 50;         // points_per_note [C 0x829CD708 table]
constexpr double kPointsPerBeat = 25;      // points_per_note_per_beat [C 0x829BC0BC table]
constexpr float kStarPowerNeeded = 50.0f;  // [C 0x82112D7C]
constexpr float kHealthMax = 2.0f;         // health_scale [C]

// The highest (most orange) fret of a mask: GH3 judges a single note by it,
// so lower frets may be held (anchoring) [C 0x82120260-0x82120880].
uint8_t highest(uint8_t m) {
    for (int b = 4; b >= 0; --b)
        if (m & (1u << b)) return uint8_t(1u << b);
    return 0;
}

bool matches(uint8_t shape, const TrackNote& n) {
    if (n.chord()) return shape == n.frets;  // a chord needs the exact shape
    return highest(shape) == n.frets;
}

}  // namespace

int PlayerStats::multiplier() const {
    const int m = currentRun < 10 ? 1 : currentRun < 20 ? 2 : currentRun < 30 ? 3 : 4;  // [C 0x8210BB40]
    return starPowerActive ? m * 2 : m;
}

Player::Player(const NoteTrack& track, const Tuning& tuning, int difficulty, bool noFail, float inputOffsetMs)
    : t_(track), tu_(tuning), diff_(std::clamp(difficulty, 0, 3)), noFail_(noFail), offsetMs_(inputOffsetMs) {
    missedPhrase_.assign(t_.phrases.size(), false);
    histTime_.fill(-1e18);
}

void Player::changeHealth(bool good, double now) {
    const size_t d = size_t(diff_);
    float delta = good ? (st_.starPowerActive ? tu_.healthStar[d] : tu_.healthGood[d]) : tu_.healthBad[d];
    // x1.35 in the red, x0.85 in the green [C 0x821062A8, 0x8210F6A0].
    if (st_.health <= tu_.healthPoorMedium) delta *= tu_.healthRedMulti;
    else if (st_.health >= tu_.healthMediumGood) delta *= tu_.healthGreenMulti;
    if (!good) {
        // Invincibility: the first 20% (Easy) / 10% (Medium) of the song, no
        // loss once at or below health_invincible_minimum x 0.02 [C 0x8210FD18].
        const float pct = diff_ == 0 ? tu_.healthInvincibleEasy : diff_ == 1 ? tu_.healthInvincibleMedium : 0;
        if (pct > 0 && now < songLengthMs_ * pct / 100.0 && st_.health <= tu_.healthInvincibleMinimum * 0.02f) return;
    }
    st_.health = std::min(kHealthMax, st_.health + delta);
    if (st_.health <= 0 && !noFail_ && !st_.failed) {
        st_.failed = true;
        events_.push_back({PlayerEvent::Kind::Failed, -1, now});
    }
    if (noFail_) st_.health = std::max(st_.health, 0.0f);
}

void Player::addStarPower(float amount) { st_.starPower = std::clamp(st_.starPower + amount, 0.0f, 100.0f); }

void Player::miss(size_t i, double now) {
    const TrackNote& n = t_.notes[i];
    ++st_.totalNotes;
    ++st_.misses;
    st_.currentRun = 0;
    lastHitMask_ = 0;
    if (n.phrase >= 0) missedPhrase_[size_t(n.phrase)] = true;  // star_miss_note [C 0x8211F56C]
    changeHealth(false, now);
    events_.push_back({PlayerEvent::Kind::Miss, int(i), now});
}

void Player::overstrum(double now) {
    ++st_.overstrums;
    st_.currentRun = 0;
    lastHitMask_ = 0;
    // The phrase in progress is lost if the previous note was in it [C 0x8211F938].
    if (cur_ > 0 && cur_ < t_.notes.size()) {
        const int p = t_.notes[cur_ - 1].phrase;
        if (p >= 0 && t_.notes[cur_].phrase == p) missedPhrase_[size_t(p)] = true;
    }
    changeHealth(false, now);
    events_.push_back({PlayerEvent::Kind::Overstrum, int(cur_), now});
}

void Player::hit(size_t i, double now, bool tap) {
    // Earlier notes the scan passed over are missed [C 0x82120700].
    while (cur_ < i) miss(cur_++, now);
    const TrackNote& n = t_.notes[i];
    ++st_.currentRun;
    st_.bestRun = std::max(st_.bestRun, st_.currentRun);
    ++st_.notesHit;
    ++st_.totalNotes;
    st_.score += int64_t(kPointsPerNote) * st_.multiplier() * n.fretCount();  // [C 0x8210BB40]
    lastHitMask_ = n.frets;
    pendingMs_ = -1;
    if (tap) {
        ++st_.hopoHits;
        tappedMask_ = n.frets;
        tapTimerMs_ = 0;  // set below
        events_.push_back({PlayerEvent::Kind::Hopo, int(i), now});
    }
    if (tu_.clearHistoryOnHit) {
        histMask_.fill(0);
        histTime_.fill(-1e18);
    }
    events_.push_back({PlayerEvent::Kind::Hit, int(i), now});
    // A sustained note's health gain waits for the end of its sustain [C 0x82110E8C].
    if (n.sustain) {
        sus_ = Sustain{int(i), double(n.time), double(n.time) + n.holdMs, 0, 0, now};
    } else {
        changeHealth(true, now);
    }
    // Star Power phrase: its last note hit with no star note missed [C 0x8211E4E8].
    if (n.phrase >= 0 && n.phraseLast && !missedPhrase_[size_t(n.phrase)]) {
        ++st_.phrasesCaptured;
        if (!st_.starPowerActive) addStarPower(tu_.starPowerSequenceBonus);
        events_.push_back({PlayerEvent::Kind::StarPhrase, int(i), now});
    }
    cur_ = i + 1;
}

void Player::updateSustain(double now, double dt, const FrameInput& in) {
    (void)dt;
    if (sus_.note < 0) return;
    const TrackNote& n = t_.notes[size_t(sus_.note)];
    const bool held = n.chord() ? in.frets == n.frets : highest(in.frets) == n.frets;  // [C 0x8211C9D0]
    const int mult = st_.multiplier();
    if (now >= sus_.end) {
        // Held to the end: the rest of the note's points [C 0x82106B98].
        if (n.maxSustainPoints > sus_.awarded) st_.score += int64_t(n.maxSustainPoints - sus_.awarded) * mult;
        changeHealth(true, now);
        events_.push_back({PlayerEvent::Kind::SustainDone, sus_.note, now});
        sus_ = Sustain{};
        return;
    }
    if (held) {
        if (now > sus_.lastMs) sus_.units += (now - sus_.lastMs) / std::max(1.0f, t_.beatAt(float(now))) * kPointsPerBeat;
        sus_.lastMs = now;
        const int due = int(std::floor(sus_.units)) - sus_.awarded;
        if (due >= 2) {  // paid in chunks of at least 2 [C 0x82106B98]
            const int pay = std::min(due, std::max(0, n.maxSustainPoints - sus_.awarded));
            st_.score += int64_t(pay) * mult;
            sus_.awarded += pay;
        }
        // Star whammy: a star sustain held with the whammy moving adds
        // star_power_whammy_add per beat, not while Star Power is on [C script star_power_whammy].
        if (n.phrase >= 0 && !missedPhrase_[size_t(n.phrase)] && !st_.starPowerActive && in.whammy > 0.03f)
            addStarPower(float(tu_.starPowerWhammyAdd * dt / std::max(1.0f, t_.beatAt(float(now)))));
        return;
    }
    // Released early: complete if little was left [C 0x8211C9D0].
    const double hold = std::max(1.0, sus_.end - sus_.start), left = sus_.end - now;
    const bool shortHold = hold < tu_.whammyMinHoldMuteMs;
    if (left <= hold * tu_.whammyMinHoldPct / 100.0 || (shortHold && left <= hold * tu_.whammyMinHoldPctShort / 100.0))
        changeHealth(true, now);
    sus_ = Sustain{};
}

void Player::updateStarPower(double now, double dt, const FrameInput& in) {
    // Select (or tilt for star_power_trigger_frames frames) deploys at >= 50 [C 0x82112D7C, 0x82112F28].
    triggerFrames_ = in.starPower ? triggerFrames_ + 1 : 0;
    if (!st_.starPowerActive && in.starPower && st_.starPower >= kStarPowerNeeded) {
        st_.starPowerActive = true;
        ++st_.starPowerUses;
        events_.push_back({PlayerEvent::Kind::StarPowerOn, -1, now});
    }
    if (st_.starPowerActive) {
        // Drains star_power_drain_rate per measure [C script star_power_activate_and_drain].
        const double measure = std::max(1.0f, t_.measureAt(float(now)));
        st_.starPower = float(std::max(0.0, double(st_.starPower) - tu_.starPowerDrainRate * dt / measure));
        if (st_.starPower <= 0) {
            st_.starPowerActive = false;
            events_.push_back({PlayerEvent::Kind::StarPowerOff, -1, now});
        }
    }
}

void Player::frame(double now, double dt, const FrameInput& in) {
    const auto& notes = t_.notes;
    const size_t n = notes.size();
    if (pendingMs_ >= 0) pendingMs_ -= dt;
    if (tapTimerMs_ >= 0) tapTimerMs_ -= dt;
    const double base = now + offsetMs_;
    const double E = tu_.checkTimeEarly * 1000.0, HE = tu_.hammerTimeEarly * 1000.0;
    auto lateFor = [&](size_t k, double late) {
        // Capped at split_late_percent of the gap to the next note [C 0x8211FA28].
        if (k + 1 < n) return std::min(late, double(notes[k + 1].time - notes[k].time) * tu_.splitLatePercent);
        return late;
    };
    const double L = cur_ < n ? lateFor(cur_, tu_.checkTimeLate * 1000.0) : 0;
    const double HL = cur_ < n ? lateFor(cur_, tu_.hammerTimeLate * 1000.0) : 0;

    // 1. The current note passed its window: missed (one per frame) [C 0x8211FD40].
    if (cur_ < n && double(notes[cur_].time) < base - L) {
        if (pendingMs_ >= 0) {
            pendingMs_ = -1;
            overstrum(now);
        }
        miss(cur_, now);
        ++cur_;
    }
    // 2. The fret history.
    histPos_ = (histPos_ + 1) % histMask_.size();
    histMask_[histPos_] = in.frets;
    histTime_[histPos_] = now;
    auto shapesSince = [&](double since, std::vector<uint8_t>& out) {
        for (size_t k = 0; k < histMask_.size(); ++k)
            if (histTime_[k] >= since && std::find(out.begin(), out.end(), histMask_[k]) == out.end()) out.push_back(histMask_[k]);
    };
    // 3. A strum: the shapes formed within form_before_strumming [C 0x8211FF38].
    std::vector<uint8_t> shapes;
    bool strumActive = false;
    if (in.strum) {
        shapesSince(now - tu_.formBeforeStrumming * 1000.0 - 1e-6, shapes);
        if (tu_.disallowToleranceStrums && pendingMs_ >= 0 && cur_ < n) {
            // A second strum while one is still forming: the note is missed [C 0x8211FF80].
            pendingMs_ = -1;
            miss(cur_, now);
            ++cur_;
        } else if (tapTimerMs_ < 0) {
            pendingMs_ = tu_.strumBeforeForming * 1000.0;
        }
        strumActive = true;
    } else if (pendingMs_ >= 0) {
        // Strum, then form the chord within strum_before_forming [C 0x82120090].
        shapesSince(now - tu_.strumBeforeForming * 1000.0 - 1e-6, shapes);
        strumActive = true;
    }
    if (tapTimerMs_ >= 0)
        shapes.erase(std::remove_if(shapes.begin(), shapes.end(), [&](uint8_t s) { return (s & tappedMask_) != 0; }), shapes.end());
    // 4. The tap candidate: the highest held fret, differing from the last
    // note hit (which a miss clears) [C 0x82120160].
    uint8_t candidate = 0;
    if (!in.strum && lastHitMask_ != 0) {
        const uint8_t h = highest(in.frets);
        if (h && h != lastHitMask_) candidate = h;
    }
    // 5. The scan [C 0x82120260-0x82120880].
    bool hitSomething = false;
    for (size_t k = cur_; k < n && double(notes[k].time) <= base + E; ++k) {
        const TrackNote& note = notes[k];
        if (strumActive) {
            bool ok = false;
            for (uint8_t s : shapes) ok = ok || matches(s, note);
            if (ok) {
                hit(k, now, false);
                hitSomething = true;
                break;
            }
        }
        if (candidate && k == cur_ && note.hopo && !note.chord() && note.frets == candidate &&
            double(note.time) > base - HL && double(note.time) <= base + HE) {
            hit(k, now, true);
            // The post-tap window: the time until the note leaves its window,
            // capped at the next note [C 0x82120840].
            tapTimerMs_ = double(note.time) - (base - L);
            if (k + 1 < n) tapTimerMs_ = std::min(tapTimerMs_, double(notes[k + 1].time) - (base + E));
            hitSomething = true;
            break;
        }
    }
    // 6. A strum with no note in reach: an overstrum, unless it is the
    // strum after a tap on the same frets [C 0x821208B0].
    if (in.strum && !hitSomething && (cur_ >= n || double(notes[cur_].time) > base + E)) {
        if (!(tapTimerMs_ >= 0 && (in.frets & tappedMask_))) overstrum(now);
        pendingMs_ = -1;
    }
    updateSustain(now, dt, in);
    updateStarPower(now, dt, in);
}
