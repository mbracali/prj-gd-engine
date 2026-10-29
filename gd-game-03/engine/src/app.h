// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

struct Options {
    std::string dataPath;  // disc image or extracted folder
    std::string song;      // --song <id>: play it, then quit
    int difficulty = 3;    // 0 Easy .. 3 Expert
    bool autoplay = false;
    bool noFail = false;
    std::string walk;      // --walktest <scenario> (tests/walktest.h)
};

// The window and GH2's own front end (ui/ui_manager.h) around the song
// screen; `--song` plays one song directly, GD03_DEBUG_MENU=1 shows the
// debug song list instead of the menus. Returns the exit code.
int runApp(const Options& options);
