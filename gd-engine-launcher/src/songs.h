// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "Hear the songs" screen's state (songs.cpp). In a header so the TU that
// defines Launcher's destructor (explorer.cpp) sees the complete type behind
// the unique_ptr, as it does for Explore.
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "launcher.h"
#include "module_host.h"

struct Launcher::Songs {
    std::string gameId, title;
    SongSession session;
    struct Entry {
        std::string title, artist, detail;
    };
    std::vector<Entry> items;
    int cursor = 0;
    float scroll = 0, scrollTarget = 0;
    int playing = -1;                   // the row being previewed (-1: none)
    SDL_AudioStream* stream = nullptr;  // the preview's audio (its own device)
    std::vector<Hit> hits;              // list rows (row >= 0), the footer (row -3, col fret)
    double lastFrame = 0;
};
