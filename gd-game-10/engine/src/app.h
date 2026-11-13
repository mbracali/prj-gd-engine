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
    std::string out;       // --out <file> (--audittest)
    std::string walk;      // --walktest <scenario> (tests/walktest.h)
    // A restart from Enhanced > Graphics / Audio Backend (Restart Now):
    // "port_enhanced" goes through GH3's boot screens unseen (no movies) to
    // Enhanced Options.
    std::string startScreen;
};

// The window and main loop (app.cpp); `--song` plays one song directly.
// Returns the exit code.
int runApp(const Options& options);
// Enhanced > Performance > Graphics Backend / Audio Backend (GDCORE-010,
// engine-backend-guidelines.md): the ids this start of the game uses,
// "local" (the game's own code, the default) or a gd-engine-core backend:
// graphics "sdl3" / "vulkan" / "opengl3" (not "opengl3" on macOS), audio
// "miniaudio" / "openal" / "faudio". Latched once at start-up (App::run
// reads enhanced.json before SDL starts); a change in the menu applies on the
// next start. A backend that cannot start falls back to local (rule 2).
std::string startupGraphicsBackend();
std::string startupAudioBackend();
