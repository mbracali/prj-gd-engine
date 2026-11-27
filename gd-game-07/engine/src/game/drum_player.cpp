// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/drum_player.h"

#include <algorithm>
#include <cmath>

// The kit-piece notes the band animator reads (render/band_anim.cpp
// drumKitIndex): 73 kick, 75 tom2, 76 tom1, 77 snare, 78 HH closed, 79 HH
// open, 80/81/82 cymbals, 84 HH open, 85 cymbal 4. Mapped onto the five GH
// drum lanes [I]: hats and the extra HH-open share yellow, the toms share
// blue, the cymbals share green.
int DrumPlayer::laneForNote(int note) {
    switch (note) {
    case 73: return Kick;    // GH_Drum_Kick
    case 77: return Red;     // GH_Drum_Snare
    case 78: return Yellow;  // GH_Drum_Cymbal_HH_Closed
    case 79: return Yellow;  // GH_Drum_Cymbal_HH_Open
    case 84: return Yellow;  // GH_Drum_Cymbal_HH_Open
    case 75: return Blue;    // GH_Drum_Tom_2
    case 76: return Blue;    // GH_Drum_Tom_1
    case 80: return Green;   // GH_Drum_Cymbal_3
    case 81: return Green;   // GH_Drum_Cymbal_1
    case 82: return Green;   // GH_Drum_Cymbal_2
    case 85: return Green;   // GH_Drum_Cymbal_4
    default: return -1;      // an arm-animation note (37..57) or out of range
    }
}

DrumPlayer::DrumPlayer(const std::vector<ChartEvent>& drums, float slopMs) : slopMs_(slopMs) {
    // Build a gem per distinct time from the playable kit-piece events; a kit
    // note's recorded velocity (ChartEvent::value, -1 none) becomes the lane's
    // velocity (default 100, as the chart carries nearly everywhere -- gh4
    // GetDrumHitVelocity semantics, 0x82192350).
    for (const ChartEvent& e : drums) {
        const int lane = laneForNote(e.note);
        if (lane < 0) continue;
        const uint8_t vel = uint8_t(std::clamp(e.value < 0 ? 100 : e.value, 0, 127));
        if (!gems_.empty() && gems_.back().ms == e.time) {
            Gem& g = gems_.back();
            g.lanes = uint8_t(g.lanes | (1u << lane));
            g.velocity[size_t(lane)] = std::max(g.velocity[size_t(lane)], vel);
            continue;
        }
        Gem g;
        g.ms = e.time;
        g.lanes = uint8_t(1u << lane);
        g.velocity[size_t(lane)] = vel;
        gems_.push_back(g);
    }
    std::sort(gems_.begin(), gems_.end(), [](const Gem& a, const Gem& b) { return a.ms < b.ms; });
    hitLanes_.assign(gems_.size(), 0);
    missedLanes_.assign(gems_.size(), 0);
    for (const Gem& g : gems_)
        for (int l = 0; l < LaneCount; ++l)
            if (g.lanes >> l & 1) ++notes_;
}

bool DrumPlayer::gemHit(int gem) const {
    if (gem < 0 || size_t(gem) >= gems_.size()) return false;
    const uint8_t want = gems_[size_t(gem)].lanes;
    return (hitLanes_[size_t(gem)] & want) == want;
}

void DrumPlayer::strike(int lane, float t, int strikeVelocity) {
    if (lane < 0 || lane >= LaneCount) return;
    // The nearest open note of that lane inside the window, as gd05's watcher.
    int best = -1;
    float bestErr = slopMs_ + 1;
    for (size_t i = sweep_; i < gems_.size(); ++i) {
        const Gem& g = gems_[i];
        if (float(g.ms) - t > slopMs_) break;
        if (!(g.lanes >> lane & 1) || (hitLanes_[i] >> lane & 1) || (missedLanes_[i] >> lane & 1)) continue;
        const float err = std::fabs(float(g.ms) - t);
        if (err <= slopMs_ && err < bestErr) best = int(i), bestErr = err;
    }
    if (best >= 0) {
        hit(best, lane, t, strikeVelocity);
        return;
    }
    events_.push_back({Event::Kind::Overhit, -1, lane, std::max(0, strikeVelocity)});
    streak_ = 0;  // an extra strike breaks the streak [I]
}

void DrumPlayer::hit(int gem, int lane, float t, int strikeVelocity) {
    (void)t;
    hitLanes_[size_t(gem)] |= uint8_t(1u << lane);
    ++hits_;
    ++streak_;
    score_ += kHeadPoints * multiplier();
    // The velocity for feel/anim: the live strike's when the caller has one,
    // else the chart's recorded value for this lane.
    const int vel = strikeVelocity >= 0 ? strikeVelocity : gems_[size_t(gem)].velocity[size_t(lane)];
    events_.push_back({Event::Kind::Hit, gem, lane, vel});
}

void DrumPlayer::miss(int gem, int lane) {
    missedLanes_[size_t(gem)] |= uint8_t(1u << lane);
    streak_ = 0;
    events_.push_back({Event::Kind::Miss, gem, lane, 0});
}

void DrumPlayer::update(float t) {
    while (sweep_ < gems_.size() && float(gems_[sweep_].ms) + slopMs_ < t) {
        const Gem& g = gems_[sweep_];
        for (int l = 0; l < LaneCount; ++l)
            if ((g.lanes >> l & 1) && !(hitLanes_[sweep_] >> l & 1) && !(missedLanes_[sweep_] >> l & 1)) miss(int(sweep_), l);
        ++sweep_;
    }
}

std::vector<DrumPlayer::Event> DrumPlayer::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}
