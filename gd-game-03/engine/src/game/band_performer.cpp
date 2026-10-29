// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/band_performer.h"

#include <algorithm>
#include <utility>

BandPerformer::BandPerformer(const Song& song, const Rules& rules, Difficulty d0, Difficulty d1, std::string songId)
    : song_(song), rules_(rules), songId_(std::move(songId)) {
    // 0x822DE800 [C]: the band's crowd from the harder difficulty
    // (0x822DE640: rating 0.5, raw the middle of the range); the easier
    // player (player 1 on a tie) maps its targets from its own difficulty's
    // range onto the band's, its note weight scaled by min / max.
    const int i0 = int(d0), i1 = int(d1);
    const int hard = std::max(i0, i1), easy = std::min(i0, i1);
    easier_ = i1 <= i0 ? 1 : 0;
    crowd_ = rules.coop.crowd[size_t(hard)];
    rating_ = 0.5f;
    raw_ = (crowd_.rangeHigh - crowd_.rangeLow) * 0.5f + crowd_.rangeLow;
    const CrowdRules& e = rules.coop.crowd[size_t(easy)];
    mix_.w = e.noteWeight / crowd_.noteWeight;
    const float lo = e.rangeLow, hi = e.rangeHigh;
    mix_.a = (hi * crowd_.rangeLow - lo * crowd_.rangeHigh) / (hi - lo);
    mix_.b = (crowd_.rangeHigh - crowd_.rangeLow) / (hi - lo) + mix_.a;
}

// The band's Star Power multiplier and crowd boost are player 0's
// (0x822DD1F8 / 0x822DD1B8 -> 0x822DB690 / 0x822DB6C0) [C].
int BandPerformer::spMultiplier() const { return lead_ && lead_->starPowerActive() ? rules_.coop.starPower.multiplier : 1; }
float BandPerformer::crowdBoost() const { return lead_ && lead_->starPowerActive() ? rules_.coop.starPower.crowdBoost : 1.0f; }

// Performer hit 0x822DDF70 (stats 0x822DCAA8) [C]: streak + 1, build_streak
// when the multiplier went up.
void BandPerformer::hit() {
    const int before = baseMultiplier();
    ++streak_;
    longest_ = std::max(longest_, streak_);
    if (baseMultiplier() > before) events_.push_back({PlayerEvent::Kind::BuildStreak, -1, float(baseMultiplier())});
}

// 0x822DD028 -> 0x822DC518 [C]: the streak ends, the longest is kept.
void BandPerformer::endStreak() {
    if (streak_ > 0) events_.push_back({PlayerEvent::Kind::EndStreak, -1, float(streak_)});
    streak_ = 0;
}

// Performer AddScore 0x822DCFB0 [C]: score (a float) += total multiplier
// (vtable +0x10: the Star Power multiplier × the streak multiplier) × points.
void BandPerformer::addScore(float points) { score_ += float(multiplier()) * points; }

// Performer UpdateCrowd 0x822DD3E0 -> 0x822DE570 [C], the band's: boosted
// by player 0's Star Power when the target is above raw; its fail
// (0x822DD108: Performer +0x50 is always TRUE for the band, 0x8234E1E0)
// ends a co-op song; competitive modes never get here.
void BandPerformer::crowdUpdate(float target, float weight, float t) {
    if (failed_) return;
    if (target > raw_) weight *= crowdBoost();
    const float w = crowd_.noteWeight * weight;
    const float raw = std::clamp((1 - w) * raw_ + w * target, 0.0f, 1.0f);
    float rating = 0;
    if (raw > crowd_.rangeLow) rating = raw < crowd_.rangeHigh ? (raw - crowd_.rangeLow) / (crowd_.rangeHigh - crowd_.rangeLow) : 1.0f;
    // Progress (0x822DD238): the band's +0x68 (the poll's time) over the song's length.
    const float progress = song_.endMs > 0 ? std::min(1.0f, t / song_.endMs) : 1.0f;
    if (rating_ <= crowd_.loseLevel && rating <= rating_) return;
    if (rating <= crowd_.warningLevel && progress <= crowd_.freeRide) return;
    raw_ = raw;
    rating_ = rating;
    if (rating_ < crowd_.loseLevel) {
        failed_ = true;
        events_.push_back({PlayerEvent::Kind::Failed, -1, 0});
    }
}

// Performer poll 0x822DD298 [C]: eight_x once the total multiplier reaches 8
// (the band's +0x4c is TRUE, 0x8234E1E0).
void BandPerformer::poll(float t) {
    (void)t;
    if (!eightX_ && multiplier() == 8) {
        eightX_ = true;
        events_.push_back({PlayerEvent::Kind::EightX, -1, 0});
    }
}

void BandPerformer::forceFail() {
    if (failed_) return;
    failed_ = true;
    events_.push_back({PlayerEvent::Kind::Failed, -1, 0});
}

PerformerState BandPerformer::state() const {
    PerformerState s;
    s.score = score();
    s.streak = streak_;
    s.multiplier = multiplier();
    s.baseMultiplier = baseMultiplier();
    s.starMeter = lead_ ? lead_->starMeter() : 0.0f;
    s.spActive = lead_ && lead_->starPowerActive();
    s.rock = rating_;
    s.warning = crowd_.warningLevel;
    return s;
}

std::vector<PlayerEvent> BandPerformer::takeEvents() {
    std::vector<PlayerEvent> out;
    out.swap(events_);
    return out;
}

void normaliseScores(Player& p0, Player& p1) {
    if (p1.baseScore() <= 0 || p0.baseScore() <= 0) return;
    const float r = float(p0.baseScore()) / float(p1.baseScore());
    p0.setNormalisation((1.0f / r + 1.0f) * 0.5f);
    p1.setNormalisation((r + 1.0f) * 0.5f);
}

void linkCoop(Player& p0, Player& p1, BandPerformer& band, StarMeter& meter) {
    band.setLead(&p0);
    const BandPerformer::Mix m = band.mix();
    for (int p = 0; p < 2; ++p) {
        Player& pl = p == 0 ? p0 : p1;
        if (p == band.easierPlayer()) pl.setBand(&band, m.a, m.b, m.w);
        else pl.setBand(&band, 0, 1, 1);
    }
    meter = {};
    p0.shareStarMeter(&meter, &p1);
    p1.shareStarMeter(&meter, &p0);
}
