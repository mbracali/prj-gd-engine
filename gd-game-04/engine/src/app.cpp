// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
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
#include "tests/walk.h"
#include "ui/front_end.h"
#include "ui/port_screens.h"
#include "ui/port_settings.h"
#include "game/save.h"
#include "ui/song_screen.h"
#include "video/movie_player.h"
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

class App : public WalkHost {
public:
    explicit App(const Options& o) : options_(o) {}
    int run();

    // WalkHost (the port's walks).
    void walkMouse(float x, float y, int button) override { menuMouse(x, y, button); }
    void walkKey(int keycode) override;
    PortScreens* walkPorts() override { return ports_.get(); }
    Input& walkInput() override { return input_; }
    std::string walkSaveDir() override { return saveDir_; }
    std::string walkEnhanced(const std::string& key) override { return enhanced_.get(key); }

private:
    bool loadData(const std::string& path);
    void handleEvent(const SDL_Event& e);
    void frame();
    void songEvent(const InputEvent& ev);
    // -- the port's own screens and the mouse (ui/port_screens.h)
    // An action for the menus: the port's open screen, its entry keys on
    // GH3's main menu (yellow: GAMEPADS) and options (blue: ENHANCED), else
    // GH3's front end.
    void menuAction(const InputEvent& ev);
    // The mouse in menu units: button 0 a move, 1 a left click, 3 a right click.
    void menuMouse(float x, float y, int button);
    bool menuMouseAllowed() const;
    void updatePointer();
    // An Enhanced setting changed ("" : all of them, at start): applied at once.
    void applyEnhanced(const std::string& key);
    // Drawn over every frame: the FPS counter (Enhanced > Show FPS Counter).
    void overlay();
    bool windowToMenu(float wx, float wy, float& mx, float& my) const;

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
    std::unique_ptr<FrontEnd> front_;
    std::unique_ptr<Walk> walk_;
    uint64_t lastFrameNs_ = 0;
    bool pauseSeen_ = false;  // GH3's PauseGame came after Start
    uint64_t pausedAtNs_ = 0;
    std::string message_;
    bool running_ = true;
    int exitCode_ = 0;
    int escapes_ = 0;  // Esc three times on the first screen quits (guidelines)
    uint64_t startNs_ = 0;
    std::unique_ptr<MenuKeys> keys_;
    EnhancedStore enhanced_;
    std::unique_ptr<PortScreens> ports_;
    std::string saveDir_;               // enhanced.json and controllers.json ("" : the pref dir)
    double mouseMovedMs_ = -1e9;        // the pointer shows for 3 s after the mouse moves
    bool pointerShown_ = true;
    uint64_t fpsStart_ = 0;
    int fpsFrames_ = 0;
    double fps_ = 0;
};

void App::walkKey(int keycode) {
    // A real key through the input layer (bindings, capture), as SDL would send it.
    for (bool down : {true, false}) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.timestamp = SDL_GetTicksNS();
        e.key.key = SDL_Keycode(keycode);
        e.key.down = down;
        e.key.windowID = SDL_GetWindowID(window_);
        handleEvent(e);
    }
}

bool App::windowToMenu(float wx, float wy, float& mx, float& my) const {
    int x, y, w, h;
    const_cast<Canvas&>(canvas_).renderer().pictureRect(x, y, w, h);
    const float density = SDL_GetWindowPixelDensity(window_);
    mx = (wx * density - float(x)) / float(w) * kScreenW;
    my = (wy * density - float(y)) / float(h) * kScreenH;
    return mx >= 0 && my >= 0 && mx <= kScreenW && my <= kScreenH;
}

bool App::menuMouseAllowed() const {
    // Enhanced > Enable Mouse Selection; the menus only (a song takes no mouse).
    return front_ && !song_ && !front_->songPlaying && enhanced_.get("mouse") != "off";
}

void App::menuMouse(float x, float y, int button) {
    if (!menuMouseAllowed()) return;
    if (ports_ && ports_->isOpen()) return ports_->mouse(x, y, button);
    const std::string state = front_->flowState();
    if (button == 3) return menuAction(InputEvent{Action::Back, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
    if (ports_) {
        const std::string item = ports_->itemAt(x, y, state);
        if (!item.empty()) {
            if (button == 1) ports_->open(item == "gamepads" ? PortScreens::Id::Gamepads : PortScreens::Id::Enhanced), front_->soundEvent("ui_sfx_select");
            return;
        }
    }
    // GH3's help bar: SELECT and BACK are clickable.
    const std::string help = front_->ui().helperAt(x, y);
    if (!help.empty()) {
        if (button == 1 && (help == "SELECT" || help == "BACK"))
            menuAction(InputEvent{help == "BACK" ? Action::Back : Action::Confirm, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
        return;
    }
    // GH3's menus: a hover focuses (not on a list, which takes clicks only),
    // a click focuses and selects.
    bool list = false;
    if (!front_->ui().menuAt(x, y, false, &list)) return;
    if (button == 1 || !list) front_->ui().menuAt(x, y, true);
    if (button == 1) front_->handle(InputEvent{Action::Confirm, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
}

void App::menuAction(const InputEvent& ev) {
    if (ports_ && ports_->isOpen()) return ports_->handle(ev);
    if (ports_ && ev.down) {
        const std::string state = front_->flowState();
        // The port's screens from GH3's (guidelines/universal_launcher.md;
        // agents/DECISIONS.md): yellow on the main menu, blue on the options.
        if (state == "main_menu_fs" && ev.action == Action::Fret2) {
            front_->soundEvent("ui_sfx_select");
            return ports_->open(PortScreens::Id::Gamepads);
        }
        if (state == "options_select_option_fs" && ev.action == Action::Fret3) {
            front_->soundEvent("ui_sfx_select");
            return ports_->open(PortScreens::Id::Enhanced);
        }
    }
    front_->handle(ev);
}

void App::updatePointer() {
    // The pointer shows when the mouse moves, hides after 3 s without
    // movement, on a key or a button, and in a song.
    const bool show = menuMouseAllowed() && double(SDL_GetTicksNS()) / 1e6 - mouseMovedMs_ < 3000;
    if (show != pointerShown_) {
        if (show) SDL_ShowCursor();
        else SDL_HideCursor();
        pointerShown_ = show;
    }
}

void App::applyEnhanced(const std::string& key) {
    auto is = [&](const char* k) { return key.empty() || key == k; };
    if (is("screen_mode") && window_) {
        const std::string m = enhanced_.get("screen_mode");
        if (m == "windowed") {
            SDL_SetWindowFullscreen(window_, false);
        } else {
            // Full Screen: the desktop's mode, exclusive; No Border: a window covering the screen.
            const SDL_DisplayMode* dm = m == "fullscreen" ? SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_)) : nullptr;
            SDL_SetWindowFullscreenMode(window_, dm);
            SDL_SetWindowFullscreen(window_, true);
        }
    }
    if (is("rockband")) input_.setRockBand(enhanced_.on("rockband"));
    if (is("xinput")) input_.setGamepadPlay(enhanced_.on("xinput"));
    if (is("touch")) input_.setTouch(false);  // touch controls are not built yet (greyed in Enhanced)
    if (is("mouse")) mouseMovedMs_ = -1e9;
}

void App::overlay() {
    const uint64_t now = SDL_GetTicksNS();
    ++fpsFrames_;
    if (!fpsStart_) fpsStart_ = now;
    if (now - fpsStart_ >= 500'000'000ull) fps_ = fpsFrames_ * 1e9 / double(now - fpsStart_), fpsStart_ = now, fpsFrames_ = 0;
    if (enhanced_.on("fps_counter") && fps_ > 0) {
        const std::string t = std::to_string(int(std::lround(fps_))) + " FPS";
        canvas_.fillRect(8, kScreenH - 30, float(t.size()) * 16 + 12, 24, SDL_Color{0, 0, 0, 150});
        canvas_.text(14, kScreenH - 26, 2, SDL_Color{120, 255, 120, 255}, t);
    }
}

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
    // The mouse in the menus (gd01's): hovering focuses (only while the window
    // has focus), a left click selects, a right click is Back, the wheel moves.
    if (front_ && (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) &&
        (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.which : e.button.which) != SDL_TOUCH_MOUSEID) {
        const bool click = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        float mx, my;
        const bool inside = windowToMenu(click ? e.button.x : e.motion.x, click ? e.button.y : e.motion.y, mx, my);
        if (menuMouseAllowed()) mouseMovedMs_ = double(SDL_GetTicksNS()) / 1e6;
        const bool focused = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
        if (inside && (click || focused) && (!click || e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT))
            menuMouse(mx, my, !click ? 0 : e.button.button == SDL_BUTTON_LEFT ? 1 : 3);
    } else if (front_ && e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.which != SDL_TOUCH_MOUSEID && e.wheel.y != 0 && menuMouseAllowed()) {
        mouseMovedMs_ = double(SDL_GetTicksNS()) / 1e6;
        menuAction(InputEvent{e.wheel.y > 0 ? Action::Up : Action::Down, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
    } else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        mouseMovedMs_ = -1e9;  // a key or a button hides the pointer
    }
    // A joypad's Y (its yellow face button) opens GAMEPADS on the main menu.
    if (front_ && ports_ && !song_ && !ports_->isOpen() && e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
        e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH && input_.joypadDevice(e.gbutton.which) && front_->flowState() == "main_menu_fs" &&
        !input_.capturing()) {
        front_->soundEvent("ui_sfx_select");
        ports_->open(PortScreens::Id::Gamepads);
        std::vector<InputEvent> drop;
        input_.handle(e, drop);  // the input layer still sees it (its state), the menus do not
        return;
    }
    std::vector<InputEvent> events;
    input_.handle(e, events);
    for (const InputEvent& ev : events) {
        if (walk_ && ev.down) walk_->countPress(ev.action);
        if (song_) {
            if (!front_ && ev.action == Action::Back && ev.down) running_ = false;  // --song alone: Back quits
            songEvent(ev);
            continue;
        }
        if (front_) {
            menuAction(ev);
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

// A button in a song: Start opens GH3's pause menu (gh3_start_pressed),
// which then has the buttons until it resumes.
void App::songEvent(const InputEvent& ev) {
    // The attract mode: any button goes to GH3's check_for_attract_mode_input.
    if (front_ && front_->attractMode()) {
        menuAction(ev);
        return;
    }
    if (front_ && front_->songPaused) {
        menuAction(ev);
        return;
    }
    if (front_ && ev.action == Action::Start && ev.down) {
        mixer_.setPaused(true);
        front_->songPaused = true, pauseSeen_ = false, pausedAtNs_ = SDL_GetTicksNS();
        front_->startPressed();
        return;
    }
    song_->handle(ev);
}

void App::frame() {
    if (song_) {
        canvas_.begin(SDL_Color{0, 0, 0, 255});
        const bool more = song_->frame(canvas_, mixer_, textures_.get());
        if (front_) front_->songClockMs = song_->songMs();
        if (front_) {
            // GH3's scripts and elements over the song (the pause menu over the frozen song).
            const uint64_t nowNs = SDL_GetTicksNS();
            const double dt = lastFrameNs_ ? double(nowNs - lastFrameNs_) / 1e6 : 0;
            lastFrameNs_ = nowNs;
            front_->frame(canvas_, dt);
            if (front_->songPaused) pauseSeen_ = pauseSeen_ || front_->flow.paused;
        }
        overlay();
        captureMenuScreenshot(canvas_, double(SDL_GetTicksNS() - startNs_) / 1e6, song_->songMs());
        canvas_.end();
        if (front_ && front_->songKill) {
            // Quit or restart from the pause menu: the song is abandoned; a
            // restart's start_gem_scroller follows as a new request.
            front_->songKill = false, front_->songPaused = false, front_->songPlaying = false;
            song_.reset();
            mixer_.stop();
            input_.setInGame(false);
            if (walk_) ++walk_->songsEnded;
            return;
        }
        if (front_ && front_->songPaused &&
            ((pauseSeen_ && !front_->flow.paused) || (!pauseSeen_ && SDL_GetTicksNS() - pausedAtNs_ > 3000000000ull))) {
            // Resumed (UnPauseGame), or the scripts never paused.
            SDL_Log("song: resumed (%s)", pauseSeen_ ? "UnPauseGame" : "the scripts never paused");
            front_->songPaused = false;
            mixer_.setPaused(false);
        }
        if (!more && front_) {
            // Back to GH3's own flow (results, setlist).
            FrontEnd::SongResult r;
            const PlayerStats& st = song_->stats();
            r.won = !song_->failed(), r.score = st.score, r.notesHit = st.notesHit, r.totalNotes = st.totalNotes;
            r.bestRun = st.bestRun, r.phrasesHit = st.phrasesCaptured, r.phrasesTotal = song_->phraseCount();
            r.baseScore = song_->baseScore(), r.health = st.health, r.starPower = st.starPower;
            const Match& m = song_->match();
            r.maxNotes = m.maxNotes(0);
            if (m.players() == 2) {
                const PlayerStats& s2 = m.player(1).stats();
                r.twoPlayers = true;
                r.p2.score = s2.score, r.p2.notesHit = s2.notesHit, r.p2.totalNotes = s2.totalNotes, r.p2.bestRun = s2.bestRun;
                r.p2.phrasesHit = s2.phrasesCaptured, r.p2.phrasesTotal = m.phraseCount(1), r.p2.baseScore = m.baseScore(1);
                r.p2.health = s2.health, r.p2.starPower = s2.starPower, r.p2.maxNotes = m.maxNotes(1);
                r.battleKo = m.mode() == GameMode::Battle && (m.outcome() == Match::Outcome::P1Wins || m.outcome() == Match::Outcome::P2Wins);
                for (int p = 0; p < 2; ++p) r.finalBlow[size_t(p)] = m.battle(p).finalBlow, r.cards[size_t(p)] = m.battle(p).cards;
                // Face-off and battle never fail (battle's KO goes through song_failed: battleKo) [C 0x8210FD18].
                if (m.mode() == GameMode::FaceOff || m.mode() == GameMode::ProFaceOff || m.mode() == GameMode::Battle) r.won = true;
            }
            song_.reset();
            mixer_.stop();
            input_.setInGame(false);
            front_->songEnded(r);
            if (walk_) ++walk_->songsEnded;
            return;
        }
        if (!more) {
            const PlayerStats& s = song_->stats();
            std::printf("song: %s, score %lld, %d / %d notes hit, best streak %d\n", s.failed ? "failed" : "complete",
                        static_cast<long long>(s.score), s.notesHit, s.totalNotes, s.bestRun);
            exitCode_ = s.failed ? 1 : 0;
            running_ = false;
        }
        return;
    }
    if (front_ && front_->songRequest.pending) {
        front_->songRequest.pending = false;
        const SongInfo* info = nullptr;
        for (const SongInfo& s : songs_.songs())
            if (s.checksum == front_->songRequest.song) info = &s;
        std::string err;
        song_ = std::make_unique<SongScreen>();
        // Enhanced > CHEATS: Autoplay and No Fail Mode (as --autoplay / --no-fail).
        // The attract mode plays itself (player1_status.bot_play) [C script create_attract_mode].
        const bool autoplay = options_.autoplay || walk_ || SDL_getenv("GD04_AUTOPLAY") || enhanced_.on("autoplay") || front_->attractMode();
        const bool noFail = options_.noFail || enhanced_.on("no_fail");
        // The mode and the two players (start_gem_scroller's globals).
        const FrontEnd::SongRequest& rq = front_->songRequest;
        SongSetup setup;
        setup.mode = rq.players == 2 || rq.boss ? gameModeFromKey(rq.gameMode, rq.boss != 0) : GameMode::Solo;
        setup.difficulty = {rq.difficulty, rq.difficulty2};
        setup.rhythm = {rq.part2Rhythm[0], rq.part2Rhythm[1]};
        setup.bossProps = rq.boss;
        setup.suddenDeath = rq.suddenDeath;
        if (rq.suddenDeath) setup.startHealth = {rq.saveHealth[0], rq.saveHealth[1]};
        setup.startMs = rq.startMs, setup.devilFinish = rq.devilFinish;
        setup.lefty = {rq.lefty[0], rq.lefty[1]};
        setup.lagMs = rq.lagMs;
        // GD04_START_AT=<song ms>: start every song there (development aid; GH3's starttime).
        if (const char* at = SDL_getenv("GD04_START_AT")) setup.startMs = std::max(0.0, std::atof(at));
        if (setup.mode == GameMode::Boss) setup.difficulty[1] = rq.difficulty;  // the 1P menu sets both [C selected_difficulty]
        // A walk's sudden death ends: player 2's autoplay keeps its Death Drains.
        if (walk_ && rq.suddenDeath) setup.autoplayFires[1] = false;
        if (!info || !song_->start(*disc_, *globals_, *info, setup, autoplay, noFail, mixer_, err, textures_.get())) {
            SDL_Log("song: cannot start %s: %s", qbkey::name(front_->songRequest.song).c_str(), err.c_str());
            song_.reset();
            front_->songEnded(FrontEnd::SongResult{});
        } else {
            if (walk_) {
                song_->walkEndMs_ = options_.walk == "lose" ? -1 : 20000.0;
                song_->walkFailMs_ = options_.walk == "lose" ? 10000.0 : -1;
                // Battle and boss walks play long enough for cards and attacks;
                // sudden death (and a boss) plays on to the KO.
                if (setup.mode == GameMode::Battle) song_->walkEndMs_ = setup.suddenDeath ? -1 : 60000.0;
                if (setup.mode == GameMode::Boss) song_->walkEndMs_ = -1;
            }
            front_->songPlaying = true;
            input_.setInGame(true);
            // The HUD's texts in GH3's fonts (num_a9, num_a7, text_a6): scale x the font's line height.
            ui::UiSystem* ui = &front_->ui();
            song_->setHudText(
                [ui](Canvas& c, uint32_t font, const std::string& t, float x, float y, float scale, const std::array<uint8_t, 4>& rgba,
                     uint32_t align, const Hud2D::TextStyle& st) {
                    ui->drawText(c, font, t, x, y, scale * ui->fontLineHeight(font), rgba, align, ui::UiSystem::TextStyle{st.rot, st.sx, st.spacing});
                },
                [ui](uint32_t font, const std::string& t, float scale, float spacing) {
                    return ui->textWidth(font, t, scale * ui->fontLineHeight(font), spacing);
                });
            if (walk_) ++walk_->songsStarted;
        }
        return;
    }
    if (front_ && !front_->movieRequest.empty()) {
        const std::string movie = front_->movieRequest;
        front_->movieRequest.clear();
        // Walks skip the movies (GD04_WALK_MOVIES=1 plays them).
        // A quit during the movie (the window closed, the launcher's
        // GDUL_GAME_QUIT_AFTER) quits the game too.
        // GD04_NO_MOVIES=1 skips them outside walks too (development aid).
        if ((!walk_ && !SDL_getenv("GD04_NO_MOVIES")) || SDL_getenv("GD04_WALK_MOVIES"))
            if (playMovie(*disc_, movie, canvas_, mixer_, input_) == MoviePlayer::Result::Quit) running_ = false;
        lastFrameNs_ = SDL_GetTicksNS();
        return;
    }
    if (front_ && !front_->songPlaying) {
        const uint64_t nowNs = SDL_GetTicksNS();
        const double dt = lastFrameNs_ ? double(nowNs - lastFrameNs_) / 1e6 : 0;
        lastFrameNs_ = nowNs;
        canvas_.begin(SDL_Color{0, 0, 0, 255});
        if (ports_ && ports_->isOpen()) {
            // The port's screen instead of GH3's, whose scripts wait meanwhile.
            ports_->update(dt);
            ports_->draw(canvas_);
            front_->sfx.update();
        } else {
            front_->frame(canvas_, dt);
            if (ports_) ports_->drawItems(canvas_, front_->flowState());
        }
        overlay();
        captureMenuScreenshot(canvas_, double(nowNs - startNs_) / 1e6, -1e9);  // no song
        canvas_.end();
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
    captureMenuScreenshot(canvas_, wallMs, -1e9);  // no song
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
    // Walks keep their own settings (enhanced.json, controllers.json), fresh
    // each run, out of the player's.
    if (!options_.walk.empty() && !prefDir().empty()) {
        saveDir_ = prefDir() + "walktest/";
        std::error_code ec;
        std::filesystem::remove(saveDir_ + "enhanced.json", ec);
        std::filesystem::remove(saveDir_ + "controllers.json", ec);
        // The game's save too (GD04_WALK_KEEP_SAVE=1 keeps it from the last walk).
        if (!SDL_getenv("GD04_WALK_KEEP_SAVE")) {
            std::filesystem::remove(saveDir_ + "save.json", ec);
            std::filesystem::remove(saveDir_ + "save.json.bak", ec);
        }
        std::filesystem::create_directories(saveDir_, ec);
        input_.setSaveDir(saveDir_);
        save::setFolder(saveDir_);
    }
    enhanced_.load(saveDir_.empty() ? prefDir() : saveDir_);
    applyEnhanced("");

    const std::string path = options_.dataPath.empty() ? loadRememberedPath() : options_.dataPath;
    if (!path.empty()) loadData(path);
    if (disc_) {
        // The global pak's textures (the highway, HUD and menu sprites live there) [C data].
        textures_ = std::make_unique<TextureCache>(canvas_.renderer().device());
        const uint64_t t0 = SDL_GetTicksNS();
        const int n = textures_->loadPak(*disc_, "DATA/COMPRESSED/ZONES/global.pak.xen");
        const int a = textures_->loadPak(*disc_, "DATA/COMPRESSED/PAK/axel_1.pak.xen");  // the default guitarist [I]
        SDL_Log("textures: %d from global.pak, %d from axel_1.pak (%.0f ms)", n, a, double(SDL_GetTicksNS() - t0) / 1e6);
    }
    if ((!options_.song.empty() || !options_.walk.empty()) && !disc_) {
        std::fprintf(stderr, "%s: %s\n", app::kProgram, message_.empty() ? "no disc image given" : message_.c_str());
        running_ = false, exitCode_ = 2;
    } else if (!options_.walk.empty() && !(walk_ = std::make_unique<Walk>(options_.walk))->known()) {
        std::fprintf(stderr, "%s: unknown walk '%s' (quickplay, lose, back, career, pause, glitches, coop, faceoff, proface, battle, boss, boot, mouse, gamepads, stickyaxis)\n", app::kProgram,
                     options_.walk.c_str());
        running_ = false, exitCode_ = 2;
    } else if (!options_.song.empty()) {
        const SongInfo* s = songs_.find(options_.song);
        song_ = std::make_unique<SongScreen>();
        std::string err;
        if (!s) {
            std::fprintf(stderr, "%s: no song '%s'\n", app::kProgram, options_.song.c_str());
            running_ = false, exitCode_ = 2;
            song_.reset();
        } else if (SongSetup one; one.difficulty = {options_.difficulty, options_.difficulty},
                   one.startMs = SDL_getenv("GD04_START_AT") ? std::max(0.0, std::atof(SDL_getenv("GD04_START_AT"))) : 0.0,
                   !song_->start(*disc_, *globals_, *s, one, options_.autoplay, options_.noFail, mixer_, err, textures_.get())) {
            std::fprintf(stderr, "%s: %s\n", app::kProgram, err.c_str());
            running_ = false, exitCode_ = 2;
            song_.reset();
        } else {
            input_.setInGame(true);
        }
    } else if (disc_ && textures_ && !SDL_getenv("GD04_NO_FRONTEND")) {
        // GH3's own front end, from its boot script.
        front_ = std::make_unique<FrontEnd>(*disc_, *globals_, *textures_, canvas_.renderer().device());
        if (!front_->start()) {
            SDL_Log("front end: guitar_startup did not start");
            front_.reset();
        } else {
            ports_ = std::make_unique<PortScreens>(input_, *front_, enhanced_, [this](const std::string& k) { applyEnhanced(k); });
        }
    }
    if (walk_ && walk_->twoPlayers()) input_.setSplitKeyboard(true);  // two-player walks: player 2 on the keyboard's other half
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
                // GD04_KEYS: a scripted button as the input layer would report it.
                if (k.mouse) {  // the mouse, as its events would (menu units)
                    if (k.mouse >= 4) menuAction(InputEvent{k.mouse == 4 ? Action::Up : Action::Down, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
                    else menuMouse(k.mx, k.my, k.mouse == 2 ? 1 : k.mouse == 3 ? 3 : 0);
                    continue;
                }
                if (k.keycode) {  // a real key through the input layer
                    walkKey(k.keycode);
                    continue;
                }
                Action a;
                switch (k.button) {
                case 6: a = Action::Confirm; break;
                case 4: a = Action::Back; break;
                case 5: a = Action::Fret2; break;
                case 7: a = Action::Fret3; break;
                case 2: a = Action::Fret4; break;
                case 11: a = Action::Start; break;
                case 8: a = Action::StarPower; break;
                case 12: a = Action::Up; break;
                case 13: a = Action::Right; break;
                case 14: a = Action::Down; break;
                case 15: a = Action::Left; break;
                default: continue;
                }
                for (int edge : {1, -1}) {
                    if ((k.edge == 1 && edge == -1) || (k.edge == -1 && edge == 1)) continue;
                    InputEvent ev{a, edge == 1, SDL_GetTicksNS(), ControllerMode::Guitar, k.player};
                    if (song_) song_->handle(ev);
                    else if (front_) menuAction(ev);
                }
            }
        }
        if (!disc_)
            if (std::string picked = picker_.takePicked(); !picked.empty()) loadData(picked);
        if (walk_ && front_) {
            for (const Walk::Press& p : walk_->update(*front_, front_->songPlaying, double(frameStart - startNs_) / 1e6, this,
                                                     ports_ ? ports_->current() : std::string()))
                for (bool down : {true, false}) {
                    InputEvent ev{p.a, down, SDL_GetTicksNS(), ControllerMode::Guitar, p.player};
                    if (song_) songEvent(ev);
                    else menuAction(ev);
                }
            if (walk_->done() || walk_->failed()) {
                exitCode_ = walk_->finish(*front_);
                running_ = false;
            }
        }
        if (!running_) break;
        if (front_) {
            // The controllers the scripts see (GetActiveControllers): the player slots with a device.
            uint32_t m = 1;
            for (int i = 1; i < 4; ++i)
                if (input_.padConnected(i)) m |= 1u << i;
            front_->activeControllers = m;
        }
        frame();
        if (quitAfter && double(SDL_GetTicksNS() - startNs_) / 1e6 >= std::atof(quitAfter)) running_ = false;
        updatePointer();
        // Enhanced > In-Game Max FPS (the menus keep the engine's own cap).
        const uint64_t minNs = song_ ? uint64_t(1e9 / std::max(30.0, std::atof(enhanced_.get("max_fps").c_str()))) : kMinFrameNs;
        const uint64_t spent = SDL_GetTicksNS() - frameStart;
        if (spent < minNs) SDL_DelayNS(minNs - spent);
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
    ports_.reset();
    front_.reset();
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
