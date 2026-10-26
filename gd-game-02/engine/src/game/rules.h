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

struct CrowdRules {
    float noteWeight = 0.008f;
    float rangeLow = 0.65f, rangeHigh = 0.9f;
    float freeRide = 0;
    float warningLevel = 0.15f;
    float loseLevel = 0.01f;
};

// Gameplay tunables, read from GH2's config/gen/scoring.dtb,
// config/gen/beatmatcher.dtb and config/gen/track_graphics.dtb [C data].
// Defaults are GH2's shipped values. The fields are gd01's (GH1); the
// constants not in the data are GH1's, [I] for GH2 until the 360 executable
// is read.
struct Rules {
    // beatmatcher watcher
    float slopMs = 100;
    // GH1: watcher +0x74, a constant set by the constructor (60 ms). [I] for GH2.
    float strumGraceMs = 60;
    // beatmatcher.dtb (parser (hopo_threshold 170)), in ticks; songs.dtb may
    // override it per song [C data]. How the parser compares a gap with it
    // (< or <=) is [I]: GH1 hard-coded <= 169, the same boundary as < 170.
    int hopoThresholdTicks = 170;

    // points
    int headPoints = 50;
    int tailPoints = 25;  // per beat of sustain, per fret
    int perfectMs = 30;
    int goodMs = 70;
    std::vector<std::pair<int, int>> streaks{{0, 1}, {10, 2}, {20, 3}, {30, 4}};

    // star power
    float spDeployRate = 0.125f;  // meter per measure
    float spPhrase = 0.25f;
    float spWhammyRate = 0.034f;  // meter per beat
    float spReadyLevel = 0.5f;
    int spMultiplier = 2;
    float spCrowdBoost = 6;

    std::array<CrowdRules, kDifficultyCount> crowd{};  // scoring.dtb default_config (single player)
    std::array<float, kDifficultyCount> trackSpeed{1.0f, 1.0f, 1.4f, 1.4f};

    int multiplierForStreak(int streak) const;
};

// Loads the rules from the game data; throws std::runtime_error on failure.
Rules loadRules(const GameData& data);
