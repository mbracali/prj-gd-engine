// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Runs a game module (sdk/gd_host.h) in the launcher's window: loads the
// library, hands over the window, the controllers and SDL, calls
// gd_game_run, takes everything back and unloads the library.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "catalog.h"
#include "gd_host.h"

class ActionLog;

struct GameRun {
    bool started = false;   // the module loaded and its entry point ran
    int exitCode = 0;       // gd_game_run's result
    std::string error;      // why it did not start
    double seconds = 0;     // how long the game ran
    bool unloaded = false;  // the library left the process (its globals start fresh next time)
    int windowSwitches = 0; // request_window calls that made the window again (sdk/gd_host.h)
    bool windowLost = false;  // the launcher's OpenGL window could not be made again afterwards
};

// Arguments for the games' own command lines, after the program name and
// before the disc image (the launcher's `-- <game args>`, main.cpp).
void setExtraGameArgs(const std::vector<std::string>& args);

// Other games' discs the player exposed in the Additive Music tool, as
// (game id, disc path) pairs. The launcher sets this before a Play; the host
// module receives them in GdHost.extra_discs. Single-use: cleared when the
// next module host is built, so set it again before each Play.
void setExtraDiscs(const std::vector<std::pair<std::string, std::string>>& discs);

// Makes the launcher's window again for a graphics API (GD_WINDOW_API_*,
// sdk/gd_host.h request_window): releases the launcher's OpenGL context and
// objects for another API, makes them again for OpenGL. Gives the window and
// context to use from now on (context null when not OpenGL); false with
// `error` when it cannot (the window and context are then as they were).
using WindowSwitch = std::function<bool(uint32_t api, SDL_Window*& window, SDL_GLContext& context, std::string& error)>;

// `switchWindow` empty: the module only gets the OpenGL window (request_window
// answers NULL for any other API). After the game, a window it had made for
// another API is made an OpenGL one again (`window` / `context` then say
// which); GameRun::error says so if that failed.
GameRun runGameModule(SDL_Window*& window, SDL_GLContext& context, const Game& game, const std::string& launcherVersion,
                      ActionLog& log, const WindowSwitch& switchWindow = {});

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

// A module's song previews (gd_game_songs), open while the launcher shows the
// "Hear the songs" screen: the library stays loaded until close(). No window
// or GL work (the launcher plays the decoded PCM itself).
class SongSession {
public:
    SongSession();
    ~SongSession();
    SongSession(const SongSession&) = delete;
    SongSession& operator=(const SongSession&) = delete;

    // Loads the module and opens its song list; false with `error` set when
    // the module cannot, or has no song previews (gd_game_songs).
    bool open(SDL_Window* window, SDL_GLContext context, const Game& game, const std::string& launcherVersion, ActionLog& log,
              std::string& error);
    const GdSongs* api() const { return api_; }
    bool close();

private:
    struct State;
    std::unique_ptr<State> state_;
    const GdSongs* api_ = nullptr;
};
