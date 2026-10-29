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

void hostLogLine(void* user, const char* line) {
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

std::vector<std::string>& extraGameArgs() {
    static std::vector<std::string> a;
    return a;
}

// What a module gets: the GdHost and everything it points to.
struct HostSetup {
    std::vector<std::string> args;
    std::vector<const char*> argv;
    std::string saveDir, gameDir, launcherVersion;
    HostLog logUser;
    GdHost host{};
    LogTee tee;

    int discFd = -1;        // Android: the opened document (see below)
    std::string discError;  // why the disc could not be opened, or ""

    HostSetup(const HostSetup&) = delete;
    HostSetup& operator=(const HostSetup&) = delete;
    ~HostSetup() { platform::closeDocument(discFd); }
    HostSetup(SDL_Window* window, SDL_GLContext context, const Game& game, const std::string& version, ActionLog& log)
        : launcherVersion(version), logUser{&log, game.id} {
        // The game's command line: its program name, then the disc image.
        args.push_back("gd" + game.id.substr(game.id.size() >= 2 ? game.id.size() - 2 : 0));
        if (const char* extra = SDL_getenv("GDUL_GAME_ARGS"))  // development aid: more options, space separated
            for (std::string a; ; ++extra) {
                if (*extra == ' ' || *extra == 0) {
                    if (!a.empty()) args.push_back(a), a.clear();
                    if (*extra == 0) break;
                } else {
                    a += *extra;
                }
            }
        for (const std::string& a : extraGameArgs()) args.push_back(a);
        // Android: a disc chosen in the file dialog is a content:// document;
        // the game gets /proc/self/fd/<n> (it opens files with fopen; the
        // module runs in this process, so the descriptor is its own too).
        std::string disc = game.iso;
        if (platform::isDocument(disc)) {
            discFd = platform::openDocument(disc, discError);
            if (discFd >= 0) {
                disc = "/proc/self/fd/" + std::to_string(discFd);
                SDL_Log("gdul: %s's disc %s opened as %s", game.id.c_str(), game.iso.c_str(), disc.c_str());
            } else {
                SDL_Log("gdul: %s's disc %s cannot be opened: %s", game.id.c_str(), game.iso.c_str(), discError.c_str());
                log.add("disc of " + game.id + " cannot be opened (" + game.iso + "): " + discError);
            }
        }
        args.push_back(disc);
        for (const std::string& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        saveDir = platform::saveDir();
        gameDir = platform::parentDir(game.modulePath);
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
        host.log = &hostLogLine;
        host.log_user = &logUser;
        host.flags = platform::isSteamDeck() ? GD_HOST_HANDHELD : 0u;
    }

    // The game's SDL_Log lines into logs/<file> in the save folder too.
    void captureLog(const std::string& file) {
        SDL_GetLogOutputFunction(&tee.fallback, &tee.fallbackData);
        SDL_CreateDirectory(platform::join(saveDir, "logs").c_str());
        tee.file = SDL_IOFromFile(platform::join(saveDir, "logs/" + file).c_str(), "wb");
        SDL_SetLogOutputFunction(&teeLog, &tee);
    }
    void releaseLog() {
        SDL_SetLogOutputFunction(tee.fallback, tee.fallbackData);
        if (tee.file) SDL_CloseIO(tee.file);
        tee.file = nullptr;
    }
};

// The launcher's window state again after a module had it.
void takeBack(SDL_Window* window, SDL_GLContext context) {
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_ShowCursor();
    SDL_StopTextInput(window);
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
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
    HostSetup setup(window, context, game, launcherVersion, log);
    if (!setup.discError.empty()) {
        run.error = "its disc image (" + platform::displayName(game.iso) + ") cannot be read: " + setup.discError +
                    ". Choose it again with Load ISO.";
        run.unloaded = platform::closeLibrary(lib, game.modulePath);
        return run;
    }

    // Hand over: no launcher input state left, the controllers announced, the
    // game's log captured.
    SDL_StopTextInput(window);
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    announceDevices(window);
    setup.captureLog(game.id + ".log");
    SDL_TimerID timer = 0;
    if (const char* q = SDL_getenv("GDUL_GAME_QUIT_AFTER")) timer = SDL_AddTimer(Uint32(std::atoi(q)), &quitTimer, nullptr);

    const Uint64 t0 = SDL_GetTicksNS();
    run.started = true;
    run.exitCode = entry(&setup.host);
    run.seconds = double(SDL_GetTicksNS() - t0) / 1e9;

    // Take everything back.
    if (timer) SDL_RemoveTimer(timer);
    setup.releaseLog();
    takeBack(window, context);
    run.unloaded = platform::closeLibrary(lib, game.modulePath);
    return run;
}

// ---- Menu sounds ----

int fetchUiSounds(SDL_Window* window, SDL_GLContext context, const Game& game, const std::string& launcherVersion, ActionLog& log,
                  GdUiSoundFn sound, void* user, std::string& error) {
    void* lib = platform::openLibrary(game.modulePath, error);
    if (!lib) {
        error = "cannot load " + platform::fileName(game.modulePath) + ": " + error;
        return 0;
    }
    int given = 0;
    if (auto entry = reinterpret_cast<GdGameUiSoundsFn>(platform::librarySymbol(lib, GD_GAME_UI_SOUNDS_SYMBOL))) {
        HostSetup setup(window, context, game, launcherVersion, log);
        if (!setup.discError.empty()) {
            error = "the disc cannot be read: " + setup.discError;
            platform::closeLibrary(lib, game.modulePath);
            return 0;
        }
        setup.captureLog(game.id + "-sounds.log");
        given = entry(&setup.host, sound, user);
        setup.releaseLog();
        SDL_GL_MakeCurrent(window, context);
        if (given == 0) error = "the module found none (see logs/" + game.id + "-sounds.log in the save folder)";
    } else {
        error = "no menu sounds in this module";
    }
    platform::closeLibrary(lib, game.modulePath);
    return given;
}

// ---- The model explorer ----

struct ExploreSession::State {
    SDL_Window* window;
    SDL_GLContext context;
    std::string modulePath;
    void* lib = nullptr;
    std::unique_ptr<HostSetup> setup;
};

ExploreSession::ExploreSession() = default;
ExploreSession::~ExploreSession() { close(); }

bool ExploreSession::open(SDL_Window* window, SDL_GLContext context, const Game& game, const std::string& launcherVersion,
                          ActionLog& log, std::string& error) {
    close();
    void* lib = platform::openLibrary(game.modulePath, error);
    if (!lib) {
        error = "cannot load " + platform::fileName(game.modulePath) + ": " + error;
        return false;
    }
    auto entry = reinterpret_cast<GdGameExploreFn>(platform::librarySymbol(lib, GD_GAME_EXPLORE_SYMBOL));
    if (!entry) {
        error = "this " + game.id + " module has no model explorer";
        platform::closeLibrary(lib, game.modulePath);
        return false;
    }
    state_ = std::make_unique<State>();
    state_->window = window, state_->context = context, state_->modulePath = game.modulePath, state_->lib = lib;
    state_->setup = std::make_unique<HostSetup>(window, context, game, launcherVersion, log);
    if (!state_->setup->discError.empty()) {
        error = "its disc image cannot be read: " + state_->setup->discError + ". Choose it again with Load ISO.";
        state_.reset();
        platform::closeLibrary(lib, game.modulePath);
        return false;
    }
    state_->setup->captureLog(game.id + "-explore.log");
    const GdExplorer* api = entry(&state_->setup->host);
    SDL_GL_MakeCurrent(window, context);
    if (!api || api->size < sizeof(GdExplorer) || !api->item_count || !api->item || !api->select || !api->draw || !api->close) {
        error = "the " + game.id + " model explorer did not open (see logs/" + game.id + "-explore.log in the save folder)";
        close();
        return false;
    }
    api_ = api;
    return true;
}

bool ExploreSession::close() {
    if (!state_) return true;
    if (api_) api_->close(api_->self);
    api_ = nullptr;
    state_->setup->releaseLog();
    takeBack(state_->window, state_->context);
    const bool unloaded = platform::closeLibrary(state_->lib, state_->modulePath);
    state_.reset();
    return unloaded;
}

void setExtraGameArgs(const std::vector<std::string>& args) { extraGameArgs() = args; }
