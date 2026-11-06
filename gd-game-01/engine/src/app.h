// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

struct Options {
    std::string dataPath;   // .iso or extracted game folder; empty = remembered / ask
    std::string song;       // song id to start directly
    int difficulty = 3;     // 0 Easy .. 3 Expert
    bool autoplay = false;
    bool noFail = false;
    double avOffsetMs = 0;  // added latency compensation
    bool venue = true;      // draw the song's venue behind the highway
    // Development scene viewer: --view <ark path> [--root <view>] [--cam <cam>]
    std::string viewScene, viewRoot, viewCam;
    std::string panel;  // development aid: show one ghui panel
    // Development aids for --song: replace the song's quickplay character / venue / guitar.
    std::string character, venueName, guitar;
    bool skipBoot = false;  // start at the main menu (no logos, intro, profile or memory card screens)
    bool splitKeyboard = false;  // --kb2p: player 2 on the right half of the keyboard
};

int runApp(const Options& options);
// Enhanced > Performance > Graphics backend / Audio backend (GDCORE-006): the
// ids this start of the game uses, "local" (the game's own code, the default)
// or a gd-engine-core backend: graphics "sdl3" / "vulkan" / "opengl3" (not
// "opengl3" on macOS), audio "miniaudio" / "openal" / "faudio". Latched once
// at start-up (App::run reads enhanced.json before SDL starts); a change in
// the menu applies on the next start. A backend that cannot start must fall
// back to local (engine-backend-guidelines.md rule 2).
std::string startupGraphicsBackend();
std::string startupAudioBackend();
// Plays every chart with perfect input through the real judgment code and
// reports scores; returns non-zero if any chart is not fully hit.
int runSelfTest(const Options& options);
// Compiles every GL shader on this machine's driver (no game data needed);
// returns 1 if any fails, 2 if no GL context could be made.
int runShaderTest();
// Decodes every scene file in the game data and reports objects that fail.
int runSceneTest(const Options& options);
