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

// Gameplay tunables, read from config/scoring.dta, config/beatmatch.dta and
// config/track_graphics.dta. Defaults are the shipped values.
struct Rules {
    // beatmatch watcher
    float slopMs = 100;
    float strumGraceMs = 60;  // watcher +0x74, a constant in the game

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

    std::array<CrowdRules, kDifficultyCount> crowd{};
    std::array<float, kDifficultyCount> trackSpeed{1.0f, 1.0f, 1.4f, 1.4f};

    int multiplierForStreak(int streak) const;
};

// Loads the rules from the game data; throws std::runtime_error on failure.
Rules loadRules(const GameData& data);
