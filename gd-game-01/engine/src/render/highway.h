// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <vector>

#include "game/chart.h"
#include "game/player.h"
#include "render/renderer.h"

// Draws the note highway and HUD for one player.
class Highway {
public:
    void draw(Renderer& r, const Song& song, const Player& player, float nowMs, float trackSpeed,
              const std::string& title, const std::vector<float>& hitFlashes);
    // Score, multiplier, streak, rock meter and Star Power meter.
    void drawHud(Renderer& r, const Player& player, const std::string& title);

private:
    struct Point { float x, y, scale; };
    Point project(float lane, float dtMs) const;
    void quad(Point a, Point b, float halfWidth, SDL_Color c);
    void disc(float x, float y, float rx, float ry, SDL_Color c);

    std::vector<Vertex> batch_;
    float aheadMs_ = 1600;
};
