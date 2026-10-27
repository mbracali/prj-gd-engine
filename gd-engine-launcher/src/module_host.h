// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Runs a game module (sdk/gd_host.h) in the launcher's window: loads the
// library, hands over the window, the controllers and SDL, calls
// gd_game_run, takes everything back and unloads the library.
#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

#include "catalog.h"
#include "gd_host.h"

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

// The game's menu sounds (gd_game_ui_sounds, optional): loads the module,
// lets it give each sound to `sound`, unloads it. Returns how many it gave;
// 0 with `error` set when it has none.
int fetchUiSounds(SDL_Window* window, SDL_GLContext context, const Game& game, const std::string& launcherVersion, ActionLog& log,
                  GdUiSoundFn sound, void* user, std::string& error);

// A module's model explorer (gd_game_explore), open while the launcher shows
// the Explore models screen: the library stays loaded until close().
class ExploreSession {
public:
    ExploreSession();
    ~ExploreSession();
    ExploreSession(const ExploreSession&) = delete;
    ExploreSession& operator=(const ExploreSession&) = delete;

    // Loads the module and opens its explorer; false with `error` set when the
    // module cannot, or has no explorer.
    bool open(SDL_Window* window, SDL_GLContext context, const Game& game, const std::string& launcherVersion, ActionLog& log,
              std::string& error);
    // The module's explorer (null when closed).
    const GdExplorer* api() const { return api_; }
    // Closes the explorer, makes the launcher's context current again and
    // unloads the library; true when it left the process.
    bool close();

private:
    struct State;
    std::unique_ptr<State> state_;
    const GdExplorer* api_ = nullptr;
};
