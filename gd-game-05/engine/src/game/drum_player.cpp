// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/drum_player.h"

#include <algorithm>
#include <cmath>

DrumPlayer::DrumPlayer(const Song& song, Difficulty difficulty, const Rules& rules, bool noFail, bool noKick)
    : song_(song),
      chart_(song.chart(Part::Drums, difficulty)),
      rules_(rules),
      noFail_(noFail),
      noKick_(noKick),
      hitLanes_(chart_.gems.size(), 0),
      missedLanes_(chart_.gems.size(), 0),
      phraseBroken_(chart_.phrases.size(), 0) {
    for (const Gem& g : chart_.gems)
        for (int l = 0; l < 5; ++l)
            if ((g.frets >> l & 1) && counts(l)) ++notes_;
}

bool DrumPlayer::gemHit(int gem) const {
    uint8_t want = chart_.gems[size_t(gem)].frets;
    if (noKick_) want &= uint8_t(~1u);
    return (hitLanes_[size_t(gem)] & want) == want;
}

int DrumPlayer::multiplier() const {
    const int base = rules_.multiplierForStreak(streak_);
    return active_ ? base * rules_.solo.starPower.multiplier : base;
}

void DrumPlayer::strike(int lane, float t) {
    if (lane < 0 || lane > 4 || !counts(lane)) return;
    // The nearest open note of that lane inside the window [I: nearest, as GH2's watcher].
    int best = -1;
    float bestErr = rules_.slopMs + 1;
    for (size_t i = sweep_; i < chart_.gems.size(); ++i) {
        const Gem& g = chart_.gems[i];
        if (g.ms - t > rules_.slopMs) break;
        if (!(g.frets >> lane & 1) || (hitLanes_[i] >> lane & 1) || (missedLanes_[i] >> lane & 1)) continue;
        const float err = std::fabs(g.ms - t);
        if (err <= rules_.slopMs && err < bestErr) best = int(i), bestErr = err;
    }
    if (best >= 0) {
        hit(best, lane, t);
        return;
    }
    // A strike with no note: during a fill when Overdrive is ready, the
    // fill's green (crash) lane deploys it (star_power crash_fill_ready_hits) [I].
    events_.push_back({Event::Kind::Overhit, -1, lane});
    if (lane == 4 && !active_ && meter_ >= rules_.solo.starPower.readyLevel)
        for (const Span& f : chart_.fills)
            if (t >= f.startMs && t <= f.endMs + rules_.slopMs) {
                active_ = true;
                const double beatMs = song_.tempo.tickToMs(song_.tempo.msToTick(t) + 480) - t;
                activeUntil_ = t + float(meter_ * float(rules_.rb.deployBeats) * beatMs);
                events_.push_back({Event::Kind::OverdriveStart, -1, lane});
                break;
            }
}

void DrumPlayer::hit(int gem, int lane, float t) {
    (void)t;
    hitLanes_[size_t(gem)] |= uint8_t(1 << lane);
    ++hits_;
    ++streak_;
    score_ += rules_.headPoints * multiplier();
    events_.push_back({Event::Kind::Hit, gem, lane});
    const Gem& g = chart_.gems[size_t(gem)];
    if (g.phrase >= 0 && !phraseBroken_[size_t(g.phrase)] && gemHit(gem) && chart_.phrases[size_t(g.phrase)].lastGem == gem) {
        meter_ = std::min(1.0f, meter_ + rules_.solo.starPower.phrase);
        events_.push_back({Event::Kind::PhraseCaptured, gem, -1});
        if (meter_ >= rules_.solo.starPower.readyLevel) events_.push_back({Event::Kind::OverdriveReady, gem, -1});
    }
}

void DrumPlayer::miss(int gem, int lane) {
    missedLanes_[size_t(gem)] |= uint8_t(1 << lane);
    streak_ = 0;
    events_.push_back({Event::Kind::Miss, gem, lane});
    const Gem& g = chart_.gems[size_t(gem)];
    if (g.phrase >= 0 && !phraseBroken_[size_t(g.phrase)]) {
        phraseBroken_[size_t(g.phrase)] = 1;
        events_.push_back({Event::Kind::PhraseMissed, gem, -1});
    }
    (void)noFail_;  // the band meter (crowd) decides failing: game/band_performer [I]
}

void DrumPlayer::update(float t) {
    while (sweep_ < chart_.gems.size() && chart_.gems[sweep_].ms + rules_.slopMs < t) {
        const Gem& g = chart_.gems[sweep_];
        for (int l = 0; l < 5; ++l)
            if ((g.frets >> l & 1) && counts(l) && !(hitLanes_[sweep_] >> l & 1) && !(missedLanes_[sweep_] >> l & 1)) miss(int(sweep_), l);
        ++sweep_;
    }
    if (active_ && t >= activeUntil_) {
        active_ = false;
        meter_ = 0;
        events_.push_back({Event::Kind::OverdriveEnd, -1, -1});
    }
}

std::vector<DrumPlayer::Event> DrumPlayer::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}
