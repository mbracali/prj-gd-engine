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
    int playing = -1;                   // the row being played (-1: none)
    SDL_AudioStream* stream = nullptr;  // the playback audio (its own device)
    std::vector<Hit> hits;              // list rows (row >= 0), footer (row -3, col fret), part checkboxes (row -4, col part)
    double lastFrame = 0;

    // Full-song playback with per-part (stem) control (module's play_song/
    // read_song/set_part). When the module lacks it, falls back to a preview.
    bool streaming = false;  // the module is streaming the whole song (not a one-shot preview)
    bool paused = false;
    bool atEnd = false;      // read_song reached the end; stop once the device drains
    int playRate = 0;
    std::vector<float> chunk;  // reusable decode buffer (interleaved stereo)
    struct Part {
        std::string name;
        bool on = true;
    };
    std::vector<Part> parts;  // the stems of the playing song (empty = none/preview)
};
