// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Runs a game module (sdk/gd_host.h) in the launcher's window: loads the
// library, hands over the window, the controllers and SDL, calls
// gd_game_run, takes everything back and unloads the library.
#pragma once

#include <SDL3/SDL.h>

#include <string>

#include "catalog.h"

class ActionLog;

struct GameRun {
    bool started = false;   // the module loaded and its entry point ran
    int exitCode = 0;       // gd_game_run's result
    std::string error;      // why it did not start
    double seconds = 0;     // how long the game ran
    bool unloaded = false;  // the library left the process (its globals start fresh next time)
};

GameRun runGameModule(SDL_Window* window, SDL_GLContext context, const Game& game, const std::string& launcherVersion,
                      ActionLog& log);
