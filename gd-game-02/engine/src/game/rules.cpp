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

// One (default_config ...) / (coop_config ...) block: crowd (0x822de640)
// and star_power (0x822d5fc0) [C].
ScoreConfig readScoreConfig(const DataNode& config) {
    ScoreConfig c;
    const DataNode& sp = require(config, "star_power");
    StarPowerRules& s = c.starPower;
    s.rechargeRate = require(sp, "recharge_rate").floatAt(1);
    s.deployRate = require(sp, "deploy_rate").floatAt(1);
    s.phrase = require(sp, "star_phrase").floatAt(1);
    s.whammyRate = require(sp, "whammy_rate").floatAt(1);
    s.readyLevel = require(sp, "ready_level").floatAt(1);
    s.multiplier = require(sp, "multiplier").intAt(1);
    s.crowdBoost = require(sp, "crowd_boost").floatAt(1);
    s.minLength = require(sp, "min_length").floatAt(1);
    s.whammySpeed = require(sp, "whammy_speed").floatAt(1);
    s.whammyTimeout = require(sp, "whammy_timeout").floatAt(1);

    const DataNode& crowd = require(config, "crowd");
    for (int d = 0; d < kDifficultyCount; ++d) {
        const DataNode& n = require(crowd, kDifficultySymbols[d]);
        CrowdRules& cr = c.crowd[size_t(d)];
        cr.noteWeight = require(n, "note_weight").floatAt(1);
        const DataNode& range = require(n, "range");
        cr.rangeLow = range.floatAt(1);
        cr.rangeHigh = range.floatAt(2);
        cr.freeRide = require(n, "free_ride").floatAt(1);
        cr.warningLevel = require(n, "warning_level").floatAt(1);
        cr.loseLevel = require(n, "lose_level").floatAt(1);
    }
    return c;
}

}  // namespace

const char* difficultyName(Difficulty d) {
    static const char* const names[] = {"Easy", "Medium", "Hard", "Expert"};
    return names[static_cast<int>(d)];
}

// GH2 0x822d66b0 [C]: table (0 -> 1) plus scoring.dtb streaks; the last entry
// whose count <= streak.
int Rules::multiplierForStreak(int streak) const {
    int mult = 1;
    for (const auto& [count, value] : streaks) {
        if (streak < count) break;
        mult = value;
    }
    return mult;
}

Rules loadRules(const GameData& data) {
    Rules r;

    // Score singleton 0x822d69c8: +0 head, +4 tail, +0xc good, +0x10 perfect,
    // +0x14 streaks, +0x24 star power config [C].
    DataNode scoring = parseDtb(data.read("config/gen/scoring.dtb"));
    const DataNode& points = require(scoring, "points");
    r.headPoints = require(points, "head").intAt(1);
    r.tailPoints = require(points, "tail").intAt(1);
    const DataNode& errorClass = require(scoring, "error_class");
    r.goodMs = require(errorClass, "good").intAt(1);
    r.perfectMs = require(errorClass, "perfect").intAt(1);

    const DataNode& streaks = require(scoring, "streaks");
    r.streaks = {{0, 1}};  // 0x822d66b0's implicit first entry [C]
    for (size_t i = 1; i < streaks.children.size(); ++i)
        r.streaks.emplace_back(streaks.children[i].intAt(0), streaks.children[i].intAt(1));

    r.solo = readScoreConfig(require(scoring, "default_config"));
    r.coop = readScoreConfig(require(scoring, "coop_config"));

    // (star_ratings (thresholds (kDifficultyEasy 2.0 2.8) ...)), 0x822d6410 [C].
    // thresholds_section is never read by the executable [C].
    const DataNode& thresholds = require(require(scoring, "star_ratings"), "thresholds");
    for (int d = 0; d < kDifficultyCount; ++d) {
        const DataNode& t = require(thresholds, kDifficultySymbols[d]);
        r.starThresholds[size_t(d)] = {t.floatAt(1), t.floatAt(2)};
    }

    // (watcher (slop 100) ...) read by the watcher constructor 0x82350ae0, and
    // (parser (hopo_threshold 170)) read by 0x823341d0 [C].
    DataNode beatmatcher = parseDtb(data.read("config/gen/beatmatcher.dtb"));
    r.slopMs = require(require(beatmatcher, "watcher"), "slop").floatAt(1);
    r.hopoThresholdTicks = require(require(beatmatcher, "parser"), "hopo_threshold").intAt(1);

    DataNode track = parseDtb(data.read("config/gen/track_graphics.dtb"));
    const DataNode& speed = require(track, "track_speed");
    for (int d = 0; d < kDifficultyCount; ++d) r.trackSpeed[size_t(d)] = require(speed, kDifficultySymbols[d]).floatAt(1);

    return r;
}
