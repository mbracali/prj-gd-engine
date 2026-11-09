// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <vector>

#include "game/chart.h"
#include "game/player.h"
#include "game/rules.h"

// Rock Band's bonuses on top of a gem player's judge (GD05-019, wave 02),
// rendering-free (L17). It reads the player's events each frame:
//
// - Solos (pitch 103 spans [C RB 0x8249F990]): at the end of a solo, the
//   share of its gems hit picks scoring.dtb's (solo (guitar (awards
//   (percent bonus name) ...))) [C data]; the bonus is that award's points
//   per gem hit [I: the multiplication is not read in the XEX].
// - Drum fills (pitches 120..124 before the [coda] [C data]): a fill
//   shows when the player's Overdrive is ready as it starts; its gems are
//   then freestyle (not judged, not drawn) and hitting its last gem (the
//   crash) deploys Overdrive [I: RB's fill rules, the crash_fill_ready_hits
//   count is not used].
// - The Big Rock Ending (the [coda] on, the 120..124 lanes): every hit
//   inside it adds (coda (point_rate 100)) to a pot, at most one per lane
//   per (max_mash_ms 1500) [I: the pot's maths]; the pot is banked when the
//   song's last gem after the coda is hit, else lost.
// - Stars: the score against the base score and star_thresholds.dtb's
//   (thresholds ...) [C data: the thresholds; I: one star per threshold
//   passed, the sixth the gold stars].
class RbBonus {
public:
    struct Notice {
        std::string text;  // what the HUD shows ("PERFECT SOLO!", "100%", "BIG ROCK ENDING!")
        float ms = 0;
    };
    RbBonus(Song& song, Part part, Difficulty difficulty, const Rules& rules);
    // One frame: the player's events (as App took them) at song time t.
    void update(Player& player, const std::vector<PlayerEvent>& events, float t);
    // A strike in the coda (any fret or pad, lane 0..4).
    void codaStrike(int lane, float t);
    int bonusTotal() const { return bonus_; }
    int solosDone() const { return solosDone_; }
    int fillsShown() const { return fillsShown_; }
    int fillsDeployed() const { return fillsDeployed_; }
    int codaPot() const { return codaPot_; }
    bool codaBanked() const { return codaBanked_; }
    bool inSolo(float t) const;
    // The current solo's share hit so far (-1 outside a solo).
    float soloShare(float t) const;
    bool inCoda(float t) const { return codaMs_ >= 0 && t >= codaMs_; }
    bool fillActive(float t) const;
    const std::vector<Notice>& notices() const { return notices_; }
    // Stars for `score` against the base score (0..6, 6 = gold).
    static int stars(const Rules& rules, int score, int baseScore);

private:
    Chart& chart_;
    const Rules& rules_;
    Part part_;
    std::vector<int> soloHits_, soloGems_;
    size_t nextSolo_ = 0;
    int bonus_ = 0, solosDone_ = 0, fillsShown_ = 0, fillsDeployed_ = 0;
    // Fills.
    size_t nextFill_ = 0;
    int activeFill_ = -1, fillLastGem_ = -1;
    // The coda.
    float codaMs_ = -1;
    int codaPot_ = 0, codaLastGem_ = -1;
    bool codaBanked_ = false, codaDone_ = false;
    float laneLast_[5] = {-1e9f, -1e9f, -1e9f, -1e9f, -1e9f};
    std::vector<Notice> notices_;
};
