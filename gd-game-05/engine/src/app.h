// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

struct Options {
    std::string dataPath;  // disc image or extracted folder
    std::string song;      // --song <id>: play it, then quit
    int difficulty = 3;    // 0 Easy .. 3 Expert
    std::string part;      // --part guitar | bass | drum: the instrument played (the song's PART track)
    bool autoplay = false;
    bool noFail = false;
    std::string walk;      // --walktest <scenario> (tests/walktest.h)
    bool inputCapture = false;  // --inputcapture: log the controllers' raw events and what a song does with them
    // A restart from Enhanced > Graphics / Audio Backend (Restart Now): no
    // boot screens, this screen opens.
    std::string startScreen;
};

// The window and GH2's own front end (ui/ui_manager.h) around the song
// screen; `--song` plays one song directly, GD05_DEBUG_MENU=1 shows the
// debug song list instead of the menus. Returns the exit code.
int runApp(const Options& options);
// Enhanced > Performance > Graphics Backend / Audio Backend (GDCORE-008,
// engine-backend-guidelines.md): the ids this start of the game uses,
// "local" (the game's own code, the default) or a gd-engine-core backend:
// graphics "sdl3" / "vulkan" / "opengl3" (not "opengl3" on macOS), audio
// "miniaudio" / "openal" / "faudio". Latched once at start-up (App::run
// reads enhanced.json before SDL starts); a change in the menu applies on the
// next start. A backend that cannot start falls back to local (rule 2).
std::string startupGraphicsBackend();
std::string startupAudioBackend();
