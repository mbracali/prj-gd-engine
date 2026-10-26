// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "app_ids.h"
#include "audio/mixer.h"
#include "data/game_data.h"
#include "devaids/screenshot.h"
#include "game/autoplay.h"
#include "game/key_script.h"
#include "game/song_db.h"
#include "game/song_setup.h"
#include "input/input.h"
#include "platform/app_icon.h"
#include "platform/disc_picker.h"
#include "render/canvas.h"
#include "render/highway.h"

namespace {

// Silence before the audio starts, so the first gems scroll in [I]: GH2's
// own intro timing is not traced yet.
constexpr double kLeadInMs = 2000;
// How long the highway stays after the song's [end] before the song closes.
constexpr float kOutroMs = 2000;
constexpr uint64_t kMinFrameNs = 4'000'000;

// songs.dtb's per-channel lists as mixer tracks: vols are dB [I: by name and
// range], pans -1..1, and the channels flagged in `cores` (exactly the
// guitar channels [C data]) are the ones muted while the player misses [I].
std::vector<Mixer::Track> mixerTracks(const SongAudio& a) {
    std::vector<Mixer::Track> tracks(a.pans.size());
    for (size_t c = 0; c < tracks.size(); ++c) {
        tracks[c].pan = a.pans[c];
        tracks[c].volume = c < a.vols.size() ? std::pow(10.0f, a.vols[c] / 20.0f) : 1.0f;
        tracks[c].guitar = c < a.cores.size() && a.cores[c] == 1;
    }
    return tracks;
}

class App {
public:
    explicit App(const Options& o) : options_(o), difficulty_(std::clamp(o.difficulty, 0, kDifficultyCount - 1)) {}
    int run();

private:
    bool loadData(const std::string& path);
    void frameNeedData();
    bool startSong(size_t index);
    void endSong();
    void handleEvent(const SDL_Event& e);
    void handleAction(const InputEvent& a);
    void frameSong();
    void frameList();

    Options options_;
    int difficulty_;
    SDL_Window* window_ = nullptr;
    Canvas canvas_;
    Mixer mixer_;
    std::unique_ptr<GameData> data_;
    Rules rules_;
    std::vector<SongInfo> songs_;
    bool running_ = true;
    // The first screen (no disc image yet): the file dialog and its last error.
    DiscPicker picker_;
    std::string dataMessage_;

    // The song list.
    size_t cursor_ = 0;
    std::string message_;

    // The song being played.
    const SongInfo* info_ = nullptr;
    std::unique_ptr<Song> song_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Autoplay> bot_;
    // Development aid: GD02_KEYS="<song ms>:<key>,..." (game/key_script.h)
    // plays scripted guitar input in every song, at exact song times.
    KeyScript keys_;
    Highway highway_;
    // Keyboard, controllers and touch (input/input.h); Start pauses a song.
    Input input_;
    std::vector<InputEvent> actions_;
    bool paused_ = false;
    std::vector<float> flashes_ = std::vector<float>(5, 0.0f);
    double lastFrameMs_ = 0;
    float failMs_ = -1e9f;
};

int App::run() {
    SDL_SetAppMetadata(app::kDisplayName, app::kVersion, app::kAppId);
    Input::prepareSdl();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "%s: cannot start SDL: %s\n", app::kProgram, SDL_GetError());
        return 2;
    }
    // Development aid: GD02_VIRTUAL_PAD=<vendor>:<product>[:guitar] (hex, e.g.
    // 1430:4748:guitar) plugs in a virtual controller, to test without hardware.
    if (const char* vp = SDL_getenv("GD02_VIRTUAL_PAD")) Input::attachVirtualPad(vp);
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | gfx::prepareWindow();  // OpenGL (render/device.h)
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    window_ = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), flags);
    setAppWindowIcon(window_);
    installDesktopEntry();
    std::string error;
    if (!window_ || !canvas_.init(window_, error)) {
        std::fprintf(stderr, "%s: cannot open the window: %s\n", app::kProgram, window_ ? error.c_str() : SDL_GetError());
        SDL_Quit();
        return 2;
    }
    if (!mixer_.open(error)) {
        std::fprintf(stderr, "%s: cannot open the audio device: %s\n", app::kProgram, error.c_str());
        canvas_.shutdown();
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return 2;
    }

    // The disc image: the command line's, else the remembered one (or an .iso
    // next to the program); without one, the first screen asks for it.
    const std::string path = options_.dataPath.empty() ? loadRememberedPath() : options_.dataPath;
    if (!path.empty()) loadData(path);
    int exitCode = 0;
    if (!options_.song.empty() && !data_) {
        std::fprintf(stderr, "%s: %s\n", app::kProgram, dataMessage_.empty() ? "no disc image given" : dataMessage_.c_str());
        running_ = false, exitCode = 2;
    } else if (!options_.song.empty()) {
        auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return s.id == options_.song; });
        if (it == songs_.end()) {
            std::fprintf(stderr, "%s: no song '%s' (see the song list without --song)\n", app::kProgram, options_.song.c_str());
            running_ = false, exitCode = 2;
        } else if (!startSong(size_t(it - songs_.begin()))) {
            std::fprintf(stderr, "%s: %s\n", app::kProgram, message_.c_str());
            running_ = false, exitCode = 2;
        }
    }

    while (running_) {
        const uint64_t frameStart = SDL_GetTicksNS();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (!running_) break;
        if (!data_)
            if (std::string picked = picker_.takePicked(); !picked.empty()) loadData(picked);
        if (player_) frameSong();
        else if (!data_) frameNeedData();
        else frameList();
        // Without vsync (e.g. SDL_VIDEO_DRIVER=offscreen) keep to about 250
        // frames a second instead of spinning; gd01's frame limiter comes
        // with its renderer.
        const uint64_t spent = SDL_GetTicksNS() - frameStart;
        if (spent < kMinFrameNs) SDL_DelayNS(kMinFrameNs - spent);
    }

    endSong();
    // A controller was used: its report (the save folder) for bug reports.
    if (input_.anyPad() && input_.writeReport()) SDL_Log("controller report: %s", input_.reportPath().c_str());
    input_.close();
    mixer_.close();
    canvas_.shutdown();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return exitCode;
}

bool App::loadData(const std::string& path) {
    try {
        auto data = std::make_unique<GameData>(path);
        Rules rules = loadRules(*data);
        std::vector<SongInfo> songs = loadSongs(*data);
        if (songs.empty()) throw std::runtime_error("No songs found in " + path);
        data_ = std::move(data), rules_ = rules, songs_ = std::move(songs);
    } catch (const std::exception& ex) {
        dataMessage_ = ex.what();
        SDL_Log("%s", dataMessage_.c_str());
        return false;
    }
    cursor_ = 0;
    dataMessage_.clear();
    rememberPath(path);
    SDL_Log("%s %s: %d songs from %s", app::kProgram, app::kVersion, int(songs_.size()), data_->description().c_str());
    return true;
}

bool App::startSong(size_t index) {
    const SongInfo& info = songs_[index];
    try {
        song_ = std::make_unique<Song>(loadChart(*data_, info, rules_));
        auto stream = std::make_unique<VorbisStream>(data_->read(info.audio.moggPath));
        // The watcher follows the device that acted last: a guitar or the
        // keyboard strums, a gamepad's frets strum (GH2's joypad_guitar).
        player_ = std::make_unique<Player>(*song_, Part::Guitar, static_cast<Difficulty>(difficulty_), rules_,
                                           input_.lastMode(), options_.noFail || options_.autoplay);
        bot_ = options_.autoplay ? std::make_unique<Autoplay>(player_->chart()) : nullptr;
        keys_ = KeyScript(SDL_getenv("GD02_KEYS") ? SDL_getenv("GD02_KEYS") : "");
        if (!keys_.valid()) SDL_Log("GD02_KEYS: some items were not understood and are ignored");
        SDL_Log("%s (%s): %d Hz, %d channels, %d %s gems, hopo_threshold %d", info.id.c_str(), info.name.c_str(),
                stream->sampleRate(), stream->channels(), int(player_->chart().gems.size()),
                difficultyName(static_cast<Difficulty>(difficulty_)), hopoThreshold(info.audio, rules_));
        mixer_.play(std::move(stream), mixerTracks(info.audio), kLeadInMs);
    } catch (const std::exception& ex) {
        message_ = info.id + ": " + ex.what();
        SDL_Log("%s", message_.c_str());
        player_.reset(), bot_.reset(), song_.reset();
        return false;
    }
    info_ = &info;
    input_.setInGame(true);
    paused_ = false;
    std::fill(flashes_.begin(), flashes_.end(), 0.0f);
    lastFrameMs_ = -kLeadInMs;
    failMs_ = -1e9f;
    return true;
}

void App::endSong() {
    if (!player_) return;
    SDL_Log("%s: score %d, %d/%d notes, longest streak %d%s (audio underruns: %lld frames)", info_->id.c_str(),
            player_->score(), player_->notesHit(), int(player_->chart().gems.size()), player_->longestStreak(),
            player_->failed() ? ", failed" : "", static_cast<long long>(mixer_.underruns()));
    mixer_.setPaused(false);
    mixer_.stop();
    bot_.reset(), player_.reset(), song_.reset();
    info_ = nullptr;
    input_.setInGame(false);
    paused_ = false;
    if (!options_.song.empty()) running_ = false;  // --song: one song, then quit
}

void App::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) {
        running_ = false;
        return;
    }
    actions_.clear();
    input_.handle(e, actions_);  // always: it opens the controllers as they connect
    if (!data_) {
        // The first screen: a click, a tap, Enter or a gamepad's A opens the
        // file dialog; a disc image dropped onto the window is used at once.
        if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) loadData(e.drop.data);
        else if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) ||
                 e.type == SDL_EVENT_FINGER_UP ||
                 (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) ||
                 (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH))
            picker_.open(window_);
        else if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK)) running_ = false;
        return;
    }
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data && !player_) {  // another disc image
        loadData(e.drop.data);
        message_ = dataMessage_;
        return;
    }
    for (const InputEvent& a : actions_) handleAction(a);
}

// One game action: in a song the player's controller layer (Player), Start
// pauses (then Start or green / Confirm resumes, Back quits the song); in the
// song list Up / Down pick, Left / Right the difficulty, Confirm plays, Back quits.
void App::handleAction(const InputEvent& a) {
    if (player_) {
        if (a.action == Action::Back && a.down && !paused_) {
            endSong();
            return;
        }
        if (a.action == Action::Start && a.down) {
            paused_ = !paused_;
            mixer_.setPaused(paused_);
            return;
        }
        if (paused_) {
            if (!a.down) return;
            if (a.action == Action::Back || a.action == Action::Fret1) endSong();
            else if (a.action == Action::Confirm || a.action == Action::Fret0) paused_ = false, mixer_.setPaused(false);
            return;
        }
        if (bot_) return;  // autoplay owns the frets
        const float t = float(mixer_.songTimeMs(a.timestampNs));
        player_->update(t);
        if (isFret(a.action)) {
            if (a.down) player_->fretDown(fretOf(a.action), t);
            else player_->fretUp(fretOf(a.action), t);
        } else if (a.action == Action::Strum && a.down) {
            player_->strum(t);
        } else if (a.action == Action::StarPower && a.down) {
            player_->starPowerButton(t);  // Select / tilt (force_mercury)
        } else if (a.action == Action::Whammy && !a.analog) {
            player_->setWhammy(a.down);  // a key or button; an analog bar is sampled every frame
        }
        return;
    }
    if (!a.down || songs_.empty()) return;
    switch (a.action) {
    case Action::Up: cursor_ = (cursor_ + songs_.size() - 1) % songs_.size(); break;
    case Action::Down: cursor_ = (cursor_ + 1) % songs_.size(); break;
    case Action::Left: difficulty_ = std::max(0, difficulty_ - 1); break;
    case Action::Right: difficulty_ = std::min(kDifficultyCount - 1, difficulty_ + 1); break;
    case Action::Confirm: case Action::Start: message_.clear(), startSong(cursor_); break;
    case Action::Back: running_ = false; break;
    default: break;
    }
}

void App::frameSong() {
    const float t = float(mixer_.frameTimeMs());
    actions_.clear();
    input_.poll(SDL_GetTicksNS(), actions_);  // touch chords
    for (const InputEvent& a : actions_) handleAction(a);
    if (!player_) return;
    // The analog whammy bar, sampled every frame (Player's movement detection).
    if (!bot_ && !paused_ && input_.analogWhammy()) player_->setWhammyValue(input_.whammyValue(), t);
    keys_.feed(*player_, t);
    if (bot_) bot_->feed(*player_, t);
    player_->update(t);
    const float dt = std::clamp(float(t - lastFrameMs_), 0.0f, 100.0f);
    lastFrameMs_ = t;
    for (float& f : flashes_) f = std::max(0.0f, f - dt / 150.0f);
    for (const PlayerEvent& e : player_->takeEvents()) {
        if (e.kind != PlayerEvent::Kind::Hit || e.gem < 0) continue;
        const Gem& g = player_->chart().gems[size_t(e.gem)];
        for (int f = 0; f < 5; ++f)
            if (g.frets & (1u << f)) flashes_[size_t(f)] = 1.0f;
    }
    mixer_.setGuitarAudible(player_->guitarAudible());

    canvas_.begin({12, 10, 16, 255});
    std::string title = info_->name + " - " + info_->artist + "  [" + difficultyName(static_cast<Difficulty>(difficulty_)) +
                        (bot_ ? ", autoplay]" : "]");
    highway_.draw(canvas_, *song_, *player_, t, rules_.trackSpeed[size_t(difficulty_)], title, flashes_);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d:%02d / %d:%02d", int(std::max(0.0f, t) / 60000), int(std::max(0.0f, t) / 1000) % 60,
                  int(song_->endMs / 60000), int(song_->endMs / 1000) % 60);
    canvas_.text(24, 44, 1.5f, {160, 160, 170, 255}, buf);
    if (player_->failed()) canvas_.textCentered(kScreenW * 0.5f, 330, 4, {230, 60, 60, 255}, "SONG FAILED");
    else if (paused_) canvas_.textCentered(kScreenW * 0.5f, 330, 4, {235, 235, 240, 255}, "PAUSED");
    if (captureDebugScreenshot(canvas_, true, t)) running_ = false;
    canvas_.end();
    // A failed song closes a little after the fail (GH2's lose screen comes later).
    if (player_->failed() && failMs_ < -1e8f) failMs_ = t;
    if (!paused_ && (t > song_->endMs + kOutroMs || (player_->failed() && t > failMs_ + 3000))) endSong();
}

// The first screen, shown until there is a disc image. All its drawing is
// here, in one place, for the renderer that replaces the debug canvas.
void App::frameNeedData() {
    if (SDL_getenv("GD02_DIALOG") && !picker_.busy() && dataMessage_.empty()) picker_.open(window_);  // headless tests
    const SDL_Color text{235, 235, 240, 255}, dim{150, 150, 165, 255}, accent{255, 210, 80, 255}, bad{240, 90, 80, 255};
    canvas_.begin({14, 12, 20, 255});
    canvas_.textCentered(kScreenW * 0.5f, 150, 4, text, app::kWindowTitle);
    canvas_.textCentered(kScreenW * 0.5f, 200, 2, dim, std::string(app::kProgram) + " " + app::kVersion +
                                                         ": Guitar Hero II (Xbox 360) reimplementation");
    canvas_.fillRect(190, 280, 900, 150, {40, 34, 60, 255});
#ifdef __ANDROID__
    canvas_.textCentered(kScreenW * 0.5f, 310, 2.5f, accent, "Tap the screen to choose your");
#else
    canvas_.textCentered(kScreenW * 0.5f, 310, 2.5f, accent, "Click or press Enter to choose your");
#endif
    canvas_.textCentered(kScreenW * 0.5f, 345, 2.5f, accent, "Guitar Hero II (Xbox 360) disc image (.iso)");
#ifndef __ANDROID__
    canvas_.textCentered(kScreenW * 0.5f, 390, 2, dim, "or drop it onto this window");
#endif
    canvas_.textCentered(kScreenW * 0.5f, 480, 1.5f, dim, "No game data is included: you need your own copy of the game.");
    canvas_.textCentered(kScreenW * 0.5f, 505, 1.5f, dim, "The game remembers where it is.");
    if (const std::string status = picker_.status(); !status.empty()) canvas_.textCentered(kScreenW * 0.5f, 560, 2, text, status);
    if (!dataMessage_.empty()) canvas_.textCentered(kScreenW * 0.5f, 600, 1.5f, bad, dataMessage_);
    if (captureDebugScreenshot(canvas_, false, 0)) running_ = false;
    canvas_.end();
}

void App::frameList() {
    canvas_.begin({14, 12, 20, 255});
    canvas_.text(40, 30, 3, {235, 235, 240, 255}, std::string(app::kWindowTitle) + "  " + app::kVersion);
    canvas_.text(40, 70, 1.5f, {150, 150, 165, 255},
                 "Up/Down: song   Left/Right: difficulty   Enter: play   Esc: quit   In a song: 1-5 frets, Enter/Up/Down strum, Tab pause, Esc back");
    canvas_.text(40, 100, 2, {255, 210, 80, 255}, std::string("Difficulty: ") + difficultyName(static_cast<Difficulty>(difficulty_)));
    const int rows = 26;
    const size_t first = cursor_ >= size_t(rows / 2) ? std::min(cursor_ - size_t(rows / 2), songs_.size() > size_t(rows) ? songs_.size() - size_t(rows) : 0) : 0;
    for (size_t i = first; i < songs_.size() && i < first + size_t(rows); ++i) {
        const SongInfo& s = songs_[i];
        const bool sel = i == cursor_;
        if (sel) canvas_.fillRect(36, 136 + float(i - first) * 21 - 3, 1208, 20, {50, 44, 70, 255});
        canvas_.text(44, 136 + float(i - first) * 21, 2, sel ? SDL_Color{255, 255, 255, 255} : SDL_Color{180, 180, 190, 255},
                     s.name + "  -  " + s.artist);
    }
    if (!message_.empty()) canvas_.text(40, 694, 1.5f, {230, 90, 90, 255}, message_);
    if (captureDebugScreenshot(canvas_, false, 0)) running_ = false;
    canvas_.end();
}

}  // namespace

int runApp(const Options& options) { return App(options).run(); }
