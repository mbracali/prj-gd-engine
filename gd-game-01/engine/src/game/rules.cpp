// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/rules.h"

#include <stdexcept>

#include "data/dta.h"
#include "data/game_data.h"

namespace {

const char* const kDifficultySymbols[kDifficultyCount] = {
    "kDifficultyEasy", "kDifficultyMedium", "kDifficultyHard", "kDifficultyExpert"};

const DataNode& require(const DataNode& parent, std::string_view key) {
    const DataNode* n = parent.find(key);
    if (!n) throw std::runtime_error("game script is missing '" + std::string(key) + "'");
    return *n;
}

}  // namespace

const char* difficultyName(Difficulty d) {
    static const char* const names[] = {"Easy", "Medium", "Hard", "Expert"};
    return names[static_cast<int>(d)];
}

int Rules::multiplierForStreak(int streak) const {
    // FUN_0010b450: the last entry whose threshold the streak has reached.
    int mult = 1;
    for (const auto& [count, value] : streaks) {
        if (streak < count) break;
        mult = value;
    }
    return mult;
}

Rules loadRules(const GameData& data) {
    Rules r;

    DataNode scoring = parseDtb(data.read(dtbPath("config/scoring.dta")));
    const DataNode& points = require(scoring, "points");
    r.headPoints = require(points, "head").intAt(1);
    r.tailPoints = require(points, "tail").intAt(1);
    const DataNode& errorClass = require(scoring, "error_class");
    r.goodMs = require(errorClass, "good").intAt(1);
    r.perfectMs = require(errorClass, "perfect").intAt(1);

    // The game prepends an implicit (0 1) entry (FUN_0010adf8).
    const DataNode& streaks = require(scoring, "streaks");
    r.streaks = {{0, 1}};
    for (size_t i = 1; i < streaks.children.size(); ++i)
        r.streaks.emplace_back(streaks.children[i].intAt(0), streaks.children[i].intAt(1));

    const DataNode& sp = require(scoring, "star_power");
    r.spDeployRate = require(sp, "deploy_rate").floatAt(1);
    r.spPhrase = require(sp, "star_phrase").floatAt(1);
    r.spWhammyRate = require(sp, "whammy_rate").floatAt(1);
    r.spReadyLevel = require(sp, "ready_level").floatAt(1);
    r.spMultiplier = require(sp, "multiplier").intAt(1);
    r.spCrowdBoost = require(sp, "crowd_boost").floatAt(1);

    const DataNode& crowd = require(scoring, "crowd");
    for (int d = 0; d < kDifficultyCount; ++d) {
        const DataNode& c = require(crowd, kDifficultySymbols[d]);
        CrowdRules& cr = r.crowd[d];
        cr.noteWeight = require(c, "note_weight").floatAt(1);
        const DataNode& range = require(c, "range");
        cr.rangeLow = range.floatAt(1);
        cr.rangeHigh = range.floatAt(2);
        cr.freeRide = require(c, "free_ride").floatAt(1);
        cr.warningLevel = require(c, "warning_level").floatAt(1);
        cr.loseLevel = require(c, "lose_level").floatAt(1);
    }

    DataNode beatmatch = parseDtb(data.read(dtbPath("config/beatmatch.dta")));
    r.slopMs = require(require(require(beatmatch, "beatmatcher"), "watcher"), "slop").floatAt(1);

    DataNode track = parseDtb(data.read(dtbPath("config/track_graphics.dta")));
    const DataNode& speed = require(track, "track_speed");
    for (int d = 0; d < kDifficultyCount; ++d) r.trackSpeed[d] = require(speed, kDifficultySymbols[d]).floatAt(1);

    return r;
}
