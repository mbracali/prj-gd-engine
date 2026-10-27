// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "module_host.h"

#include <cstdlib>
#include <vector>

#include "action_log.h"
#include "gd_host.h"
#include "platform.h"

namespace {

// The game's SDL_Log lines: on the console as usual, and in
// logs/<game id>.log in the launcher's save folder (one file per game, the last run).
struct LogTee {
    SDL_LogOutputFunction fallback = nullptr;
    void* fallbackData = nullptr;
    SDL_IOStream* file = nullptr;
};

void SDLCALL teeLog(void* user, int category, SDL_LogPriority priority, const char* message) {
    auto* tee = static_cast<LogTee*>(user);
    if (tee->fallback) tee->fallback(tee->fallbackData, category, priority, message);
    if (tee->file && message) {
        SDL_WriteIO(tee->file, message, SDL_strlen(message));
        SDL_WriteIO(tee->file, "\n", 1);
    }
}

struct HostLog {
    ActionLog* log;
    std::string id;
};

void hostLog(void* user, const char* line) {
    if (user && line) static_cast<HostLog*>(user)->log->add("game " + static_cast<HostLog*>(user)->id + ": " + line);
}

// Development aid: GDUL_GAME_QUIT_AFTER=<ms> quits the game that long after
// it starts (SDL_EVENT_QUIT, as closing the window would), for scripted runs.
Uint32 SDLCALL quitTimer(void*, SDL_TimerID, Uint32) {
    SDL_Event e{};
    e.type = SDL_EVENT_QUIT;
    e.quit.timestamp = SDL_GetTicksNS();
    SDL_PushEvent(&e);
    return 0;
}

// The controllers already connected, announced again: the game opens its
// controllers when SDL says they were added, and SDL says so only once per
// process.
void announceDevices(SDL_Window* window) {
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&n)) {
        for (int i = 0; i < n; ++i) {
            SDL_Event e{};
            e.type = SDL_EVENT_JOYSTICK_ADDED;
            e.jdevice.timestamp = SDL_GetTicksNS();
            e.jdevice.which = ids[i];
            SDL_PushEvent(&e);
            if (SDL_IsGamepad(ids[i])) {
                SDL_Event g{};
                g.type = SDL_EVENT_GAMEPAD_ADDED;
                g.gdevice.timestamp = SDL_GetTicksNS();
                g.gdevice.which = ids[i];
                SDL_PushEvent(&g);
            }
        }
        SDL_free(ids);
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    SDL_Event r{};
    r.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    r.window.timestamp = SDL_GetTicksNS();
    r.window.windowID = SDL_GetWindowID(window);
    r.window.data1 = w, r.window.data2 = h;
    SDL_PushEvent(&r);
}

}  // namespace

GameRun runGameModule(SDL_Window* window, SDL_GLContext context, const Game& game, const std::string& launcherVersion,
                      ActionLog& log) {
    GameRun run;
    std::string error;
    void* lib = platform::openLibrary(game.modulePath, error);
    if (!lib) {
        run.error = "cannot load " + platform::fileName(game.modulePath) + ": " + error;
        return run;
    }
    auto entry = reinterpret_cast<GdGameRunFn>(platform::librarySymbol(lib, GD_GAME_RUN_SYMBOL));
    if (!entry) {
        run.error = platform::fileName(game.modulePath) + " has no " GD_GAME_RUN_SYMBOL;
        run.unloaded = platform::closeLibrary(lib, game.modulePath);
        return run;
    }

    // The game's command line: its program name, then the disc image.
    std::string program = "gd" + game.id.substr(game.id.size() >= 2 ? game.id.size() - 2 : 0);
    std::vector<std::string> args{program};
    if (const char* extra = SDL_getenv("GDUL_GAME_ARGS"))  // development aid: more options, space separated
        for (std::string a; ; ++extra) {
            if (*extra == ' ' || *extra == 0) {
                if (!a.empty()) args.push_back(a), a.clear();
                if (*extra == 0) break;
            } else {
                a += *extra;
            }
        }
    args.push_back(game.iso);
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    const std::string saveDir = platform::saveDir();
    const std::string gameDir = platform::parentDir(game.modulePath);

    GdHost host{};
    host.abi_version = GD_HOST_ABI_VERSION;
    host.size = sizeof(GdHost);
    host.window = window;
    host.gl_context = context;
    host.argc = int(args.size());
    host.argv = argv.data();
    host.launcher_version = launcherVersion.c_str();
    host.game_id = game.id.c_str();
    host.game_dir = gameDir.c_str();
    host.save_dir = saveDir.c_str();
    HostLog hostLogUser{&log, game.id};
    host.log = &hostLog;
    host.log_user = &hostLogUser;

    // Hand over: no launcher input state left, the controllers announced, the
    // game's log captured.
    SDL_StopTextInput(window);
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    announceDevices(window);
    LogTee tee;
    SDL_GetLogOutputFunction(&tee.fallback, &tee.fallbackData);
    SDL_CreateDirectory(platform::join(saveDir, "logs").c_str());
    tee.file = SDL_IOFromFile(platform::join(saveDir, "logs/" + game.id + ".log").c_str(), "wb");
    SDL_SetLogOutputFunction(&teeLog, &tee);
    SDL_TimerID timer = 0;
    if (const char* q = SDL_getenv("GDUL_GAME_QUIT_AFTER")) timer = SDL_AddTimer(Uint32(std::atoi(q)), &quitTimer, nullptr);

    const Uint64 t0 = SDL_GetTicksNS();
    run.started = true;
    run.exitCode = entry(&host);
    run.seconds = double(SDL_GetTicksNS() - t0) / 1e9;

    // Take everything back.
    if (timer) SDL_RemoveTimer(timer);
    SDL_SetLogOutputFunction(tee.fallback, tee.fallbackData);
    if (tee.file) SDL_CloseIO(tee.file);
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_ShowCursor();
    SDL_StopTextInput(window);
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    run.unloaded = platform::closeLibrary(lib, game.modulePath);
    return run;
}
