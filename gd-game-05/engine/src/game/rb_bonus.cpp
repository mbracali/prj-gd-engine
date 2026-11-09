// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/rb_bonus.h"

#include <algorithm>
#include <cmath>

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = c == '_' ? ' ' : char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

RbBonus::RbBonus(Song& song, Part part, Difficulty difficulty, const Rules& rules)
    : chart_(song.charts[size_t(part)][size_t(difficulty)]), rules_(rules), part_(part) {
    soloHits_.assign(chart_.solos.size(), 0);
    soloGems_.assign(chart_.solos.size(), 0);
    for (const Gem& g : chart_.gems)
        if (g.solo >= 0 && size_t(g.solo) < soloGems_.size()) ++soloGems_[size_t(g.solo)];
    if (song.codaTick >= 0) {
        codaMs_ = float(song.tempo.tickToMs(uint32_t(song.codaTick)));
        // The ending's last gem: the first gem after the coda's lanes end
        // (the final chord) [I].
        float lanesEnd = codaMs_;
        for (const Span& f : chart_.fills)
            if (f.startMs >= codaMs_ - 1) lanesEnd = std::max(lanesEnd, f.endMs);
        for (size_t i = 0; i < chart_.gems.size(); ++i)
            if (chart_.gems[i].ms >= lanesEnd - 1) {
                codaLastGem_ = int(i);
                break;
            }
        if (lanesEnd <= codaMs_) codaMs_ = -1;  // a [coda] without ending lanes: no Big Rock Ending
    }
}

bool RbBonus::inSolo(float t) const {
    for (const Span& s : chart_.solos)
        if (t >= s.startMs && t <= s.endMs) return true;
    return false;
}

float RbBonus::soloShare(float t) const {
    for (size_t i = 0; i < chart_.solos.size(); ++i) {
        const Span& s = chart_.solos[i];
        if (t >= s.startMs && t <= s.endMs) return soloGems_[i] ? float(soloHits_[i]) / float(soloGems_[i]) : 0.0f;
    }
    return -1;
}

bool RbBonus::fillActive(float t) const {
    return activeFill_ >= 0 && t >= chart_.fills[size_t(activeFill_)].startMs && t <= chart_.fills[size_t(activeFill_)].endMs + 200;
}

void RbBonus::codaStrike(int lane, float t) {
    if (!inCoda(t) || codaDone_ || lane < 0 || lane > 4) return;
    if (codaLastGem_ >= 0 && t >= chart_.gems[size_t(codaLastGem_)].ms - 200) return;  // the final chord is judged
    if (t - laneLast_[lane] < float(rules_.rb.codaMaxMashMs) * 0.1f) return;  // one strike counts per lane per mash step [I]
    laneLast_[lane] = t;
    codaPot_ += rules_.rb.codaPointRate;
}

void RbBonus::update(Player& player, const std::vector<PlayerEvent>& events, float t) {
    for (const PlayerEvent& e : events) {
        if (e.kind != PlayerEvent::Kind::Hit || e.gem < 0 || size_t(e.gem) >= chart_.gems.size()) continue;
        const Gem& g = chart_.gems[size_t(e.gem)];
        if (g.solo >= 0 && size_t(g.solo) < soloHits_.size()) ++soloHits_[size_t(g.solo)];
        if (activeFill_ >= 0 && e.gem == fillLastGem_) {
            // The fill's crash: Overdrive deployed.
            player.starPowerButton(t);
            player.starPowerEngage(false, t);
            ++fillsDeployed_;
            notices_.push_back({"OVERDRIVE!", t});
        }
        if (codaMs_ >= 0 && !codaDone_ && e.gem == codaLastGem_) {
            codaDone_ = codaBanked_ = true;
            player.addBonus(codaPot_);
            bonus_ += codaPot_;
            notices_.push_back({"BIG ROCK ENDING! +" + std::to_string(codaPot_), t});
        }
    }
    // A missed final chord loses the pot.
    if (codaMs_ >= 0 && !codaDone_ && codaLastGem_ >= 0) {
        const Gem& last = chart_.gems[size_t(codaLastGem_)];
        if (t > last.ms + 250 && !player.gemHit(codaLastGem_)) {
            codaDone_ = true;
            notices_.push_back({"ENDING MISSED", t});
        }
    }
    // Solos end: the award.
    while (nextSolo_ < chart_.solos.size() && chart_.solos[nextSolo_].endMs + 250 < t) {
        const size_t i = nextSolo_++;
        if (!soloGems_[i]) continue;
        const int pct = int(std::floor(100.0f * float(soloHits_[i]) / float(soloGems_[i])));
        const RbScoring::Award* award = nullptr;
        for (const RbScoring::Award& a : rules_.rb.guitarSolo)
            if (pct >= a.percent) award = &a;
        const int points = award ? award->bonus * soloHits_[i] : 0;
        player.addBonus(points);
        bonus_ += points;
        ++solosDone_;
        notices_.push_back({std::to_string(pct) + "% " + (award ? upper(award->name) : std::string("SOLO")) + (points ? " +" + std::to_string(points) : ""), t});
    }
    // Drum fills: shown only when Overdrive is ready as one starts (and not in the coda).
    if (part_ == Part::Drums) {
        while (nextFill_ < chart_.fills.size() && chart_.fills[nextFill_].startMs <= t) {
            const size_t i = nextFill_++;
            const Span& f = chart_.fills[i];
            if (codaMs_ >= 0 && f.startMs >= codaMs_ - 1) continue;
            if (!player.starPowerReady()) continue;
            // Freestyle: the fill's gems are nobody's but its last (the crash).
            activeFill_ = int(i), fillLastGem_ = -1;
            for (size_t k = 0; k < chart_.gems.size(); ++k)
                if (chart_.gems[k].ms >= f.startMs - 1 && chart_.gems[k].ms <= f.endMs + 1) fillLastGem_ = int(k);
            for (size_t k = 0; k < chart_.gems.size(); ++k) {
                Gem& g = chart_.gems[k];
                if (g.ms >= f.startMs - 1 && g.ms <= f.endMs + 1 && int(k) != fillLastGem_ && !player.gemHit(int(k))) g.players = 0x80;
            }
            ++fillsShown_;
        }
        if (activeFill_ >= 0 && t > chart_.fills[size_t(activeFill_)].endMs + 300) activeFill_ = -1, fillLastGem_ = -1;
    }
}

int RbBonus::stars(const Rules& rules, int score, int baseScore) {
    if (baseScore <= 0) return 0;
    const float r = float(score) / float(baseScore);
    int n = 0;
    for (float th : rules.rb.starThresholds)
        if (r >= th) ++n;
    return std::min(n, 6);
}
