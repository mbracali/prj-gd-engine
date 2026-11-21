// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// UiSystem's texture-slot movies (ui_movies.cpp); private to ui_system*.cpp.

#include <memory>
#include <string>
#include <vector>

#include "render/device.h"
#include "ui/ui_system.h"
#include "video/movie_player.h"

namespace ui {

struct UiSystem::MovieSlot {
    gfx::RenderDevice* dev = nullptr;
    std::unique_ptr<MoviePlayer> player;
    std::string name;
    Element el;             // the full-screen sprite it draws as (z = texturepri)
    gfx::TextureId tex;
    int w = 0, h = 0;
    double ms = 0;          // its clock (game time, paused with PauseMovie)
    bool started = false, rendered = false, paused = false, loop = true, hold = true;
    std::vector<uint8_t> rgba;
    ~MovieSlot() {
        if (dev && tex) dev->destroyTexture(tex);
    }
};

}  // namespace ui
