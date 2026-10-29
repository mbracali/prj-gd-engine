// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app_ids.h"
#include "audio/mixer.h"
#include "data/disc.h"
#include "data/qbkey.h"
#include "game/song_db.h"
#include "script/globals.h"
#include "ui/song_screen.h"
#include "devaids/menu_keys.h"
#include "devaids/screenshot.h"
#include "input/input.h"
#include "platform/app_icon.h"
#include "platform/disc_picker.h"
#include "platform/paths.h"
#include "render/canvas.h"
#include "render/texture_cache.h"

// The program's window and main loop. M0 (kickoff): the platform shell
// only: the window, the disc (picked, remembered, opened), input, audio
// device, screenshots and scripted keys. GH3's own front end comes from its
// scripts (M3, script/ and ui/); until then the window shows the disc's
// status. Nothing here is GH3 behaviour yet.
namespace {

constexpr uint64_t kMinFrameNs = 4'000'000;  // ~250 fps cap without vsync (offscreen tests)

bool envOn(const char* name) {
    const char* v = SDL_getenv(name);
    return v && *v && std::string(v) != "0";
}

class App {
public:
    explicit App(const Options& o) : options_(o) {}
    int run();

private:
    bool loadData(const std::string& path);
    void handleEvent(const SDL_Event& e);
    void frame();

    Options options_;
    SDL_Window* window_ = nullptr;
    Canvas canvas_;
    Mixer mixer_;
    Input input_;
    DiscPicker picker_;
    std::unique_ptr<Disc> disc_;
    std::unique_ptr<Globals> globals_;
    SongDb songs_;
    std::unique_ptr<SongScreen> song_;
    std::unique_ptr<TextureCache> textures_;
    std::string message_;
    bool running_ = true;
    int exitCode_ = 0;
    int escapes_ = 0;  // Esc three times on the first screen quits (guidelines)
    uint64_t startNs_ = 0;
    std::unique_ptr<MenuKeys> keys_;
};

bool App::loadData(const std::string& path) {
    std::string error;
    disc_ = Disc::open(path, error);
    if (!disc_) {
        message_ = error;
        SDL_Log("%s", error.c_str());
        return false;
    }
    disc_->loadNames();
    try {
        globals_ = std::make_unique<Globals>();
        globals_->load(*disc_);
        songs_.load(*globals_);
    } catch (const std::exception& e) {
        message_ = std::string("cannot read the scripts: ") + e.what();
        disc_.reset();
        return false;
    }
    rememberPath(path);
    message_ = "Guitar Hero III disc: " + std::to_string(songs_.songs().size()) + " songs";
    SDL_Log("disc: %s (%s)", path.c_str(), message_.c_str());
    return true;
}

void App::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) running_ = false;
    std::vector<InputEvent> events;
    input_.handle(e, events);
    for (const InputEvent& ev : events) {
        if (song_) {
            if (ev.action == Action::Back && ev.down) running_ = false;  // pause comes with the menus (M3)
            song_->handle(ev);
            continue;
        }
        if (!ev.down) continue;
        if (ev.action == Action::Back) {
            if (++escapes_ >= 3) running_ = false;
        } else {
            escapes_ = 0;
        }
        if (ev.action == Action::Confirm && !disc_ && !picker_.busy()) picker_.open(window_);
    }
}

void App::frame() {
    if (song_) {
        canvas_.begin(SDL_Color{0, 0, 0, 255});
        const bool more = song_->frame(canvas_, mixer_);
        captureMenuScreenshot(canvas_, double(SDL_GetTicksNS() - startNs_) / 1e6, song_->songMs());
        canvas_.end();
        if (!more) {
            const PlayerStats& s = song_->stats();
            std::printf("song: %s, score %lld, %d / %d notes hit, best streak %d\n", s.failed ? "failed" : "complete",
                        static_cast<long long>(s.score), s.notesHit, s.totalNotes, s.bestRun);
            exitCode_ = s.failed ? 1 : 0;
            running_ = false;
        }
        return;
    }
    canvas_.begin(SDL_Color{0, 0, 0, 255});
    const SDL_Color white{255, 255, 255, 255}, grey{160, 160, 160, 255};
    canvas_.textCentered(640, 300, 3, white, "Guitar Hero III: Legends of Rock");
    if (!disc_) {
        std::string status = picker_.status();
        canvas_.textCentered(640, 360, 2, grey, status.empty() ? "Press Enter to choose your disc image" : status);
    } else {
        canvas_.textCentered(640, 360, 2, grey, message_);
    }
    canvas_.textCentered(640, 680, 1, grey, std::string(app::kDisplayName) + " " + app::kVersion);
    const double wallMs = double(SDL_GetTicksNS() - startNs_) / 1e6;
    captureMenuScreenshot(canvas_, wallMs, -1);
    canvas_.end();
}

int App::run() {
    SDL_SetAppMetadata(app::kDisplayName, app::kVersion, app::kAppId);
    Input::prepareSdl();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "%s: cannot start SDL: %s\n", app::kProgram, SDL_GetError());
        return 2;
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | gfx::prepareWindow();
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    window_ = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), flags);
    setAppWindowIcon(window_);
    installDesktopEntry();
    std::string error;
    if (!window_ || !canvas_.init(window_, error)) {
        std::fprintf(stderr, "%s: cannot open the window: %s\n", app::kProgram, window_ ? error.c_str() : SDL_GetError());
        if (window_) SDL_DestroyWindow(window_);
        SDL_Quit();
        return 2;
    }
    if (!mixer_.open(error)) SDL_Log("audio: %s (continuing without sound)", error.c_str());
    if (const char* k = SDL_getenv("GD04_KEYS")) keys_ = std::make_unique<MenuKeys>(k);

    const std::string path = options_.dataPath.empty() ? loadRememberedPath() : options_.dataPath;
    if (!path.empty()) loadData(path);
    if (disc_) {
        // The global pak's textures (the highway, HUD and menu sprites live there) [C data].
        textures_ = std::make_unique<TextureCache>(canvas_.renderer().device());
        const uint64_t t0 = SDL_GetTicksNS();
        const int n = textures_->loadPak(*disc_, "DATA/COMPRESSED/ZONES/global.pak.xen");
        SDL_Log("textures: %d from global.pak (%.0f ms)", n, double(SDL_GetTicksNS() - t0) / 1e6);
    }
    if ((!options_.song.empty() || !options_.walk.empty()) && !disc_) {
        std::fprintf(stderr, "%s: %s\n", app::kProgram, message_.empty() ? "no disc image given" : message_.c_str());
        running_ = false, exitCode_ = 2;
    } else if (!options_.walk.empty()) {
        std::fprintf(stderr, "%s: walks are not implemented yet (M3)\n", app::kProgram);
        running_ = false, exitCode_ = 2;
    } else if (!options_.song.empty()) {
        const SongInfo* s = songs_.find(options_.song);
        song_ = std::make_unique<SongScreen>();
        std::string err;
        if (!s) {
            std::fprintf(stderr, "%s: no song '%s'\n", app::kProgram, options_.song.c_str());
            running_ = false, exitCode_ = 2;
            song_.reset();
        } else if (!song_->start(*disc_, *globals_, *s, options_.difficulty, options_.autoplay, options_.noFail, mixer_, err)) {
            std::fprintf(stderr, "%s: %s\n", app::kProgram, err.c_str());
            running_ = false, exitCode_ = 2;
            song_.reset();
        } else {
            input_.setInGame(true);
        }
    }
    const bool stats = envOn("GD04_STATS");
    uint64_t statsStart = SDL_GetTicksNS();
    int statsFrames = 0;
    // GD04_QUIT_AFTER=<ms>: quits by itself (headless smoke runs).
    const char* quitAfter = SDL_getenv("GD04_QUIT_AFTER");
    startNs_ = SDL_GetTicksNS();
    while (running_) {
        const uint64_t frameStart = SDL_GetTicksNS();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (keys_) {
            for (const MenuKeys::Key& k : keys_->due(double(frameStart - startNs_) / 1e6)) {
                (void)k;  // the menus come with M3; the keys are parsed (and validated) already
            }
        }
        if (!disc_)
            if (std::string picked = picker_.takePicked(); !picked.empty()) loadData(picked);
        if (!running_) break;
        frame();
        if (quitAfter && double(SDL_GetTicksNS() - startNs_) / 1e6 >= std::atof(quitAfter)) running_ = false;
        const uint64_t spent = SDL_GetTicksNS() - frameStart;
        if (spent < kMinFrameNs) SDL_DelayNS(kMinFrameNs - spent);
        if (stats) {
            ++statsFrames;
            const uint64_t now = SDL_GetTicksNS();
            if (now - statsStart >= 2'000'000'000ull) {
                SDL_Log("stats: %.1f fps", statsFrames * 1e9 / double(now - statsStart));
                statsStart = now, statsFrames = 0;
            }
        }
    }
    song_.reset();
    textures_.reset();
    mixer_.stop();
    input_.close();
    mixer_.close();
    canvas_.shutdown();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return exitCode_;
}

}  // namespace

int runApp(const Options& options) {
    App app(options);
    return app.run();
}
