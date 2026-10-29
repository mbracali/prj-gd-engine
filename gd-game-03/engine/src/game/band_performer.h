// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <vector>

#include "game/chart.h"
#include "game/player.h"
#include "game/rules.h"

// GH2's co-op band (Performer vtable 0x82048EE4, built by 0x822DE170 in the
// players' set-up 0x822C9E80 when the mode has shared_star_meter): the
// band's streak and multiplier, its score and its crowd (the rock meter that
// ends a co-op song). docs/multiplayer.md. Addresses are default.xex's; [C]
// confirmed there, [I] inferred.
class BandPerformer {
public:
    // The crowd is coop_config's at the harder of the two difficulties
    // (0x822DE800 -> 0x822DE640) [C].
    BandPerformer(const Song& song, const Rules& rules, Difficulty d0, Difficulty d1, std::string songId);

    // How the easier player's crowd targets reach the band (0x822DE800):
    // target' = a + (b - a) × target, note weight × w; the other player (and
    // equal difficulties) a = 0, b = 1, w = 1 [C]. `easier` is the easier
    // player (1 on a tie).
    struct Mix {
        float a = 0, b = 1, w = 1;
    };
    int easierPlayer() const { return easier_; }
    Mix mix() const { return mix_; }

    // Performer's methods, called by the co-op players (0x822DEBA8 hit,
    // 0x822DEBF8 end streak, 0x822DEB30 score, 0x822DEC68 crowd, 0x822DEAD0
    // poll). `lead` is player 0, whose Star Power gives the band's
    // multiplier and crowd boost (0x822DD1F8 / 0x822DD1B8) [C].
    void setLead(const Player* lead) { lead_ = lead; }
    void hit();
    void endStreak();
    void addScore(float points);
    void crowdUpdate(float target, float weight, float t);
    void poll(float t);

    int score() const { return int(score_); }  // +0x6c, a float read with fctiwz (0x822DCB80) [C]
    int streak() const { return streak_; }
    int longestStreak() const { return longest_; }
    int baseMultiplier() const { return rules_.multiplierForStreak(streak_); }
    int multiplier() const { return baseMultiplier() * spMultiplier(); }
    float rockMeter() const { return rating_; }
    float warningLevel() const { return crowd_.warningLevel; }
    bool failed() const { return failed_; }
    // Development aid (GD03_FAIL_AT in co-op): fails now.
    void forceFail();
    // The band's stars (0x822DCDF0 -> 0x822CE5D8) [C].
    int stars() const { return rules_.coopStars(songId_, score()); }
    PerformerState state() const;
    // build_streak, eight_x, Failed.
    std::vector<PlayerEvent> takeEvents();

private:
    int spMultiplier() const;
    float crowdBoost() const;

    const Song& song_;
    const Rules& rules_;
    std::string songId_;
    CrowdRules crowd_;
    int easier_ = 1;
    Mix mix_;
    const Player* lead_ = nullptr;
    float score_ = 0;
    int streak_ = 0, longest_ = 0;
    float raw_ = 0, rating_ = 0.5f;
    bool failed_ = false;
    bool eightX_ = false;
    std::vector<PlayerEvent> events_;
};

// Two players (docs/multiplayer.md). The normalisation (0x822BC818 ->
// 0x826A0A50, 0x822BB320) [C]: r = base score 0 / base score 1 (each
// player's own gems), player 0's points × (1/r + 1)/2, player 1's ×
// (r + 1)/2.
void normaliseScores(Player& p0, Player& p1);
// Co-op (0x822C9E80) [C]: both players feed the band (the easier one with
// the band's mix), share `meter` and must both engage Star Power.
void linkCoop(Player& p0, Player& p1, BandPerformer& band, StarMeter& meter);
