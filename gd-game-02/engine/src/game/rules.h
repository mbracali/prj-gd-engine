// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

class GameData;

enum class Difficulty { Easy, Medium, Hard, Expert };
constexpr int kDifficultyCount = 4;
const char* difficultyName(Difficulty d);

// scoring.dtb <config> (crowd <difficulty> ...): the crowd object's fields,
// set up by 0x822de640 (+0x10/+0x14 range, +0x18 note_weight, +0x1c
// warning_level, +0x20 lose_level, +0x24 free_ride) [C].
struct CrowdRules {
    float noteWeight = 0.008f;
    float rangeLow = 0.65f, rangeHigh = 0.9f;
    float freeRide = 0;
    float warningLevel = 0.15f;
    float loseLevel = 0.01f;
};

// scoring.dtb <config> (star_power ...), read by 0x822d5fc0 into
// [0] recharge_rate .. [9] whammy_timeout [C]. Times are kept in the data's
// units here (seconds); the game stores min_length and whammy_timeout × 1000
// and whammy_speed × 0.01 [C].
struct StarPowerRules {
    float rechargeRate = 0;       // [0] no reader of a refill over time found [I: unused]
    float deployRate = 0.125f;    // [1] meter lost per measure while deployed
    float phrase = 0.25f;         // [2] star_phrase: meter per captured phrase
    float whammyRate = 0.034f;    // [3] meter per beat of whammy
    float readyLevel = 0.5f;      // [4]
    int multiplier = 2;           // [5]
    float crowdBoost = 6;         // [6]
    float minLength = 2.0f;       // [7] seconds; copied to player +0xd4, no reader found [I: unused]
    float whammySpeed = 0.05f;    // [8] × 0.01 = bar range per ms
    float whammyTimeout = 0.5f;   // [9] seconds
};

// One score config block of scoring.dtb: default_config (single player and
// the competitive modes) or coop_config (modes.dta score_config) [C].
struct ScoreConfig {
    std::array<CrowdRules, kDifficultyCount> crowd{};
    StarPowerRules starPower;
};

// Gameplay tunables. Values that GH2 reads from its own config are loaded
// from config/gen/scoring.dtb, config/gen/beatmatcher.dtb (+ songs.dtb's
// per-song hopo_threshold, song_db) and config/gen/track_graphics.dtb; the
// defaults below are the shipped values. Constants that GH2 hard-codes in
// default.xex are marked with their address (docs/FINDINGS.md).
struct Rules {
    // ---- beatmatcher.dtb (watcher ...) / (parser ...)
    float slopMs = 100;          // watcher slop, written to watcher +0x14 by 0x82350ae0, every difficulty [C]
    int hopoThresholdTicks = 170;  // parser hopo_threshold, read by 0x823341d0 [C]

    // ---- constants of default.xex (not in the data) [C]
    float pendingStrumMs = 60;   // watcher +0xa8 = 60.0 (0x82002af8): wrong-strum grace and HOPO late extra
    float strumRepeatMs = 52;    // player sink 0x8233aa80: drop a strum <= 52 ms after the last (0x8205807c)
    float overstrumGapMs = 50;   // 0x8234f860: one overstrum per 50 ms (0x82002228)
    float joypadChordMs = 50;    // joypad watcher +0x84 = 50.0 (0x82002228)
    int tailTicksPerBeat = 480;  // 0x822de310 / 0x826a0ab0: tail points per 480 ticks

    // ---- scoring.dtb
    int headPoints = 50;   // (points (head 50)) per fret [C]
    int tailPoints = 25;   // (points (tail 25)) per beat of sustain, per fret [C]
    int perfectMs = 30;    // (error_class (perfect 30)) [C]
    int goodMs = 70;       // (error_class (good 70)) [C]
    // (streaks (10 2) (20 3) (30 4)) behind an implicit (0 1) (0x822d66b0) [C].
    std::vector<std::pair<int, int>> streaks{{0, 1}, {10, 2}, {20, 3}, {30, 4}};
    ScoreConfig solo;  // default_config [C]
    ScoreConfig coop;  // coop_config [C]; TODO(co-op): used by the co-op player and the band meter
    // (star_ratings (thresholds (<difficulty> four five))): 4 stars at >= four × base,
    // 5 at >= five × base, else 3 (0x822d6410) [C].
    std::array<std::pair<float, float>, kDifficultyCount> starThresholds{
        {{2.0f, 2.8f}, {2.0f, 2.8f}, {2.0f, 2.8f}, {2.0f, 2.8f}}};

    // ---- track_graphics.dtb (track_speed)
    std::array<float, kDifficultyCount> trackSpeed{1.0f, 1.0f, 1.4f, 1.4f};

    // The streak multiplier: the last entry whose count <= streak (0x822d66b0) [C].
    int multiplierForStreak(int streak) const;
};

// Loads the rules from the game data; throws std::runtime_error on failure.
Rules loadRules(const GameData& data);
