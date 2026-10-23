#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "script/script.h"

class Campaign;

// What the career objects need from the game state (docs/career.md §2).
struct CareerContext {
    Campaign* campaign = nullptr;
    std::function<int()> difficulty;                  // GameConfig +0x90
    std::function<std::string()> song;                // GameConfig +0x98
    std::function<int(int score)> starsOf;            // num_stars for the current song (3..5)
    std::function<std::string(const std::string&)> localize;
    // Player 0's config: character, guitar, skin.
    std::function<void(std::string&, std::string&, std::string&)> getPlayer;
    std::function<void(const std::string&, const std::string&, const std::string&)> setPlayer;
    std::function<void()> changed;                    // unlocks / scores changed (song lists rebuild)
};

// Creates `campaign` (Campaign_v1), `highscores` (HighScoreDB), `options`,
// `character_provider`, `store_guitar_provider`, `store_skin_provider` and
// `video_provider` on `ctx`.
std::vector<std::unique_ptr<ScriptObject>> makeCareerObjects(const CareerContext& ctx);
