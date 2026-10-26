// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "app_ids.h"
#include "audio/mixer.h"
#include "data/game_data.h"
#include "devaids/menu_keys.h"
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
#include "render/hud_view.h"
#include "render/scene_gpu.h"
#include "render/track_view.h"
#include "scene/milo.h"
#include "script/script.h"
#include "tests/walktest.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"
#include "video/wmv_player.h"

namespace {

// Silence before the audio starts, so the first gems scroll in [I]: GH2's
// own intro timing is not traced yet.
constexpr double kLeadInMs = 2000;
// How long the highway stays after the song's [end] before the song closes
// (longer once the track explodes: track_explode.filt runs 160 frames).
constexpr float kOutroMs = 2000;
constexpr float kExplodeOutroMs = 6000;
// game.dta LOSE_MUSIC_SLOW_SEC: a failed song's music winds down to a stop.
constexpr float kLoseSlowMs = 2000;
// A failed song's outro (the slow-down, then SONG FAILED a moment) [I].
constexpr float kLoseOutroMs = 3000;
constexpr uint64_t kMinFrameNs = 4'000'000;

// songs.dtb's per-channel lists as mixer tracks: vols are dB [I: by name and
// range], pans -1..1, and the channels flagged in `cores` (exactly the
// guitar channels [C data]) are the ones muted while the player misses [I].
// Development aid: GD02_MODE=vs|faceoff|coop|practice shows that mode's
// track panel and HUD (config/modes.dta: hud_vs for face-off and pro
// face-off, hud_coop, hud_practice) with a second, autoplaying player; the
// modes' own rules are milestone M10.
enum class DevMode { Single, Vs, FaceOff, Coop, Practice };
DevMode devMode() {
    const char* m = SDL_getenv("GD02_MODE");
    const std::string v = m ? m : "";
    if (v == "vs") return DevMode::Vs;
    if (v == "faceoff" || v == "fo") return DevMode::FaceOff;
    if (v == "coop") return DevMode::Coop;
    if (v == "practice") return DevMode::Practice;
    return DevMode::Single;
}
bool twoPlayers(DevMode m) { return m == DevMode::Vs || m == DevMode::FaceOff || m == DevMode::Coop; }
const char* hudFile(DevMode m) {
    switch (m) {
    case DevMode::Vs: case DevMode::FaceOff: return "hud_vs.milo";
    case DevMode::Coop: return "hud_coop.milo";
    case DevMode::Practice: return "hud_practice.milo";
    default: return "hud_sp.milo";
    }
}

bool envOn(const char* name) {
    const char* v = SDL_getenv(name);
    return v && *v && *v != '0';
}

std::vector<Mixer::Track> mixerTracks(const SongAudio& a, float band = 1, float guitar = 1) {
    std::vector<Mixer::Track> tracks(a.pans.size());
    for (size_t c = 0; c < tracks.size(); ++c) {
        tracks[c].pan = a.pans[c];
        tracks[c].guitar = c < a.cores.size() && a.cores[c] == 1;
        tracks[c].volume = (c < a.vols.size() ? std::pow(10.0f, a.vols[c] / 20.0f) : 1.0f) * (tracks[c].guitar ? guitar : band);
    }
    return tracks;
}

// A section of a .mogg looped (song_preview: songs.dta (preview a b) with a
// fade in and out [I: GH2's fade times]; the menu music: the whole loop).
class LoopSource final : public AudioSource {
public:
    LoopSource(std::unique_ptr<VorbisStream> s, double startMs, double endMs, double fadeMs)
        : s_(std::move(s)), start_(uint64_t(std::max(0.0, startMs) * s_->sampleRate() / 1000.0)) {
        end_ = endMs > startMs ? uint64_t(endMs * s_->sampleRate() / 1000.0) : s_->lengthFrames();
        end_ = std::min<uint64_t>(std::max<uint64_t>(end_, start_ + 1), s_->lengthFrames() ? s_->lengthFrames() : end_);
        fade_ = uint64_t(fadeMs * s_->sampleRate() / 1000.0);
        s_->seek(start_);
        pos_ = start_;
    }
    int channels() const override { return s_->channels(); }
    int sampleRate() const override { return s_->sampleRate(); }
    int decode(float* const* out, int frames) override {
        int done = 0;
        std::vector<float*> ptr(static_cast<size_t>(channels()));
        while (done < frames) {
            if (pos_ >= end_) {
                s_->seek(start_);
                pos_ = start_;
            }
            for (int c = 0; c < channels(); ++c) ptr[size_t(c)] = out[c] + done;
            const int want = int(std::min<uint64_t>(uint64_t(frames - done), end_ - pos_));
            const int n = s_->decode(ptr.data(), want);
            if (n <= 0) {
                if (pos_ == start_) break;  // nothing decodes
                pos_ = end_;
                continue;
            }
            if (fade_ > 0)
                for (int i = 0; i < n; ++i) {
                    const uint64_t p = pos_ + uint64_t(i);
                    const float g = std::min({1.0f, float(p - start_) / float(fade_), float(end_ - p) / float(fade_)});
                    for (int c = 0; c < channels(); ++c) ptr[size_t(c)][i] *= g;
                }
            pos_ += uint64_t(n);
            done += n;
        }
        return done;
    }

private:
    std::unique_ptr<VorbisStream> s_;
    uint64_t start_ = 0, end_ = 0, pos_ = 0, fade_ = 0;
};

class App final : public UiHost {
public:
    explicit App(const Options& o) : options_(o), difficulty_(std::clamp(o.difficulty, 0, kDifficultyCount - 1)) {}
    int run();

    // ---- UiHost (GH2's menus around the song)
    const std::vector<SongInfo>& songs() const override { return songs_; }
    bool gameLoad(const GameSetup& setup, std::string& error) override;
    void gameBegin() override;
    void gameEnd() override { endSong(); }
    bool gameLoaded() const override { return player_ != nullptr; }
    void gameSetPaused(bool paused) override {
        paused_ = paused;
        mixer_.setPaused(paused || musicPaused_);
    }
    bool gamePaused() const override { return paused_; }
    double gameTimeMs() const override { return lastT_; }
    float gameBeat() const override {
        return song_ ? float(song_->tempo.msToTick(std::max(0.0f, lastT_)) / std::max(1, song_->tempo.division())) : 0.0f;
    }
    float songDurationMs() const override { return song_ ? song_->endMs : 0.0f; }
    const Player* gamePlayer(int index) const override { return index == 0 ? player_.get() : player2_.get(); }
    int gameCounter(int player, const std::string& key) const override;
    void setMusicSpeed(float speed) override { mixer_.setSpeed(std::max(0.0f, speed)); }
    void setMusicPaused(bool paused) override {
        musicPaused_ = paused;
        mixer_.setPaused(paused || paused_);
    }
    void trackCommand(const std::string& msg, const std::vector<Value>& args) override;
    void hudCommand(const std::string& msg, const std::vector<Value>& args) override;
    void drawTrack() override;
    void drawHud() override;
    void songPreview(const std::string& songId) override;
    void menuMusic(bool on) override;
    void creditsMusic(bool on) override;
    void playMovie(const std::string& name) override;
    void applyOptions(float band, float guitar, float fx, bool lefty0, bool lefty1, int syncOffsetMs) override;
    void quit() override { running_ = false; }

private:
    bool loadData(const std::string& path);
    void startUi();
    void frameNeedData();
    bool loadSong(const SongInfo& info, int difficulty, bool autoplay, float band, float guitar, bool fastIntro);
    void beginSong();
    bool startSong(size_t index);
    void endSong();
    void handleEvent(const SDL_Event& e);
    void handleAction(const InputEvent& a);
    void playerAction(const InputEvent& a);
    void updateSong(float t);
    float songClock();
    void frameSong();
    void frameUi();
    void drawSong(float t);
    void frameList();
    bool prepareTrack();
    void stopMusic();
    void shootScreens();

    Options options_;
    int difficulty_;
    SDL_Window* window_ = nullptr;
    Canvas canvas_;
    Mixer mixer_;
    std::unique_ptr<GameData> data_;
    Rules rules_;
    std::vector<SongInfo> songs_;
    bool running_ = true;
    int exitCode_ = 0;
    // The first screen (no disc image yet): the file dialog and its last error.
    DiscPicker picker_;
    std::string dataMessage_;

    // GH2's front end (ui/ui_manager.h); GD02_DEBUG_MENU=1 shows the debug
    // song list instead.
    std::unique_ptr<ui::UiManager> ui_;
    bool debugMenu_ = false;
    double uiStartMs_ = 0;
    MenuKeys menuKeys_;
    std::unique_ptr<WalkDriver> walk_;
    size_t shotsTaken_ = 0;
    double shotDue_ = -1;
    std::string shotScreen_;

    // The song list.
    size_t cursor_ = 0;
    std::string message_;

    // The song being played.
    const SongInfo* info_ = nullptr;
    std::unique_ptr<Song> song_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Autoplay> bot_;
    std::unique_ptr<VorbisStream> pendingStream_;
    float bandVolume_ = 1, guitarVolume_ = 1;
    int syncOffsetMs_ = 0;
    double leadInMs_ = kLeadInMs;
    // GD02_MODE's second player (TODO M10: the modes' own rules).
    DevMode mode_ = DevMode::Single;
    std::unique_ptr<Player> player2_;
    std::unique_ptr<Autoplay> bot2_;
    std::unique_ptr<TrackView> track2_;
    // Development aid: GD02_FAIL_AT=<song ms> fails the song then.
    float failAtMs_ = 1e30f;
    uint64_t failTicks_ = 0;
    bool exploded_ = false;
    // The UI's song: the game events sent so far, the outro, the counts.
    bool begun_ = false, wonSent_ = false, lostSent_ = false, outroSent_ = false, spReadySent_ = false;
    uint64_t overNs_ = 0, outroAtNs_ = 0;
    float overT_ = 0;
    bool musicPaused_ = false;
    int spPhrases_[2] = {}, streaksBroken_[2] = {};
    float lastT_ = 0;
    // Development aid: GD02_KEYS="<song ms>:<key>,..." (game/key_script.h)
    // plays scripted guitar input in every song, at exact song times.
    KeyScript keys_;
    // The song screen: GH2's own highway and HUD (render/track_view.h,
    // render/hud_view.h), loaded with the first song; gd01's debug highway
    // (render/highway.h) with GD02_DEBUG_HIGHWAY=1, or when GH2's scenes
    // cannot be loaded.
    Highway highway_;
    bool debugHighway_ = false;
    bool trackTried_ = false;
    std::unique_ptr<scene::MiloLibrary> milos_;
    std::unique_ptr<gfx::SceneCache> sceneCache_;
    std::unique_ptr<TrackView> track_;
    std::unique_ptr<HudView> hud_;
    // Keyboard, controllers and touch (input/input.h); Start pauses a song.
    Input input_;
    std::vector<InputEvent> actions_;
    bool paused_ = false;
    std::vector<float> flashes_ = std::vector<float>(5, 0.0f);
    double lastFrameMs_ = 0;
    float failMs_ = -1e9f;
    // The menus' music: none, a meta loop or a song's preview.
    enum class Music { None, Menu, Preview, Credits } music_ = Music::None;
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
    debugHighway_ = envOn("GD02_DEBUG_HIGHWAY");
    debugMenu_ = envOn("GD02_DEBUG_MENU");
    if (!options_.walk.empty()) {
        walk_ = std::make_unique<WalkDriver>(options_.walk);
        if (!walk_->known()) {
            std::fprintf(stderr, "%s: unknown walk test '%s' (quickplay, lose, pause, attract)\n", app::kProgram, options_.walk.c_str());
            SDL_Quit();
            return 2;
        }
        debugMenu_ = false;
    }
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
    if ((!options_.song.empty() || walk_) && !data_) {
        std::fprintf(stderr, "%s: %s\n", app::kProgram, dataMessage_.empty() ? "no disc image given" : dataMessage_.c_str());
        running_ = false, exitCode_ = 2;
    } else if (!options_.song.empty()) {
        auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return s.id == options_.song; });
        if (it == songs_.end()) {
            std::fprintf(stderr, "%s: no song '%s' (see the song list without --song)\n", app::kProgram, options_.song.c_str());
            running_ = false, exitCode_ = 2;
        } else if (!startSong(size_t(it - songs_.begin()))) {
            std::fprintf(stderr, "%s: %s\n", app::kProgram, message_.c_str());
            running_ = false, exitCode_ = 2;
        }
    } else if (data_ && !debugMenu_) {
        startUi();
        if (walk_ && !ui_) running_ = false, exitCode_ = 1;
    }

    while (running_) {
        const uint64_t frameStart = SDL_GetTicksNS();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (!running_) break;
        if (!data_)
            if (std::string picked = picker_.takePicked(); !picked.empty()) {
                if (loadData(picked) && !debugMenu_) startUi();
            }
        if (ui_) frameUi();
        else if (player_) frameSong();
        else if (!data_) frameNeedData();
        else frameList();
        // Without vsync (e.g. SDL_VIDEO_DRIVER=offscreen) keep to about 250
        // frames a second instead of spinning; gd01's frame limiter comes
        // with its renderer.
        const uint64_t spent = SDL_GetTicksNS() - frameStart;
        if (spent < kMinFrameNs) SDL_DelayNS(kMinFrameNs - spent);
    }

    if (walk_ && ui_) exitCode_ = walk_->finish(*ui_);
    endSong();
    stopMusic();
    ui_.reset();
    hud_.reset(), track_.reset(), track2_.reset();
    sceneCache_.reset();
    milos_.reset();
    // A controller was used: its report (the save folder) for bug reports.
    if (input_.anyPad() && input_.writeReport()) SDL_Log("controller report: %s", input_.reportPath().c_str());
    input_.close();
    mixer_.close();
    canvas_.shutdown();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return exitCode_;
}

bool App::loadData(const std::string& path) {
    try {
        auto data = std::make_unique<GameData>(path);
        Rules rules = loadRules(*data);
        std::vector<SongInfo> songs = loadSongs(*data);
        if (songs.empty()) throw std::runtime_error("No songs found in " + path);
        ui_.reset();
        hud_.reset(), track_.reset(), track2_.reset();
        sceneCache_.reset(), milos_.reset();
        trackTried_ = false;
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

// GH2's front end: the boot splashes and the intro (unless GD02_SKIP_BOOT=1
// or GD02_SCREEN=<screen>, which opens that screen), then the menus.
void App::startUi() {
    if (walk_) {
        // The walk test: no videos, a fresh save of its own, its song settings.
        SDL_setenv_unsafe("GD02_SKIP_MOVIES", "1", 1);
        static std::string dir;
        dir = (std::filesystem::temp_directory_path() / ("gd02-walk-" + std::to_string(SDL_GetTicksNS()))).string() + "/";
        std::filesystem::create_directories(dir);
        SDL_setenv_unsafe("GD02_SAVE_DIR", dir.c_str(), 1);
    }
    auto ui = std::make_unique<ui::UiManager>(*data_, canvas_, *this, input_);
    std::string error;
    if (!ui->init(error)) {
        SDL_Log("GH2's menus could not start (%s); showing the debug song list", error.c_str());
        message_ = error;
        return;
    }
    ui_ = std::move(ui);
    ui_->applyOptions();
    const char* keys = SDL_getenv("GD02_KEYS");
    menuKeys_ = MenuKeys(keys ? keys : "");
    if (!menuKeys_.valid()) SDL_Log("GD02_KEYS: some menu items were not understood and are ignored");
    std::string screen = SDL_getenv("GD02_SCREEN") ? SDL_getenv("GD02_SCREEN") : "";
    if (walk_) screen = walk_->startScreen();
    const bool skip = envOn("GD02_SKIP_BOOT") || !screen.empty();
    if (envOn("GD02_SKIP_BOOT") && screen.empty()) screen = "splash_screen";
    ui_->start(skip, screen);
    uiStartMs_ = double(SDL_GetTicksNS()) / 1e6;
}

bool App::loadSong(const SongInfo& info, int difficulty, bool autoplay, float band, float guitar, bool fastIntro) {
    endSong();
    stopMusic();
    try {
        song_ = std::make_unique<Song>(loadChart(*data_, info, rules_));
        pendingStream_ = std::make_unique<VorbisStream>(data_->read(info.audio.moggPath));
        difficulty_ = std::clamp(difficulty, 0, kDifficultyCount - 1);
        // The watcher follows the device that acted last: a guitar or the
        // keyboard strums, a gamepad's frets strum (GH2's joypad_guitar).
        mode_ = ui_ ? DevMode::Single : devMode();
        // Face-off players own their gems (player_mask); co-op plays lead and
        // bass / rhythm [I: the part pick], the others the guitar part.
        const bool faceOff = mode_ == DevMode::FaceOff;
        Part part0 = Part::Guitar, part1 = Part::Guitar;
        if (mode_ == DevMode::Coop) {
            if (song_->hasPart[size_t(Part::GuitarCoop)]) part0 = Part::GuitarCoop;
            if (song_->hasPart[size_t(Part::Rhythm)]) part1 = Part::Rhythm;
            if (song_->hasPart[size_t(Part::Bass)]) part1 = Part::Bass;
        }
        const bool noFail = options_.noFail || autoplay || mode_ != DevMode::Single;
        player_ = std::make_unique<Player>(*song_, part0, static_cast<Difficulty>(difficulty_), rules_, input_.lastMode(), noFail,
                                           faceOff ? 0 : -1);
        player_->setCalibrationMs(float(syncOffsetMs_));
        bot_ = autoplay ? std::make_unique<Autoplay>(player_->chart(), faceOff ? 0 : -1) : nullptr;
        player2_.reset(), bot2_.reset();
        if (twoPlayers(mode_)) {
            player2_ = std::make_unique<Player>(*song_, part1, static_cast<Difficulty>(difficulty_), rules_, ControllerMode::Guitar,
                                                true, faceOff ? 1 : -1);
            bot2_ = std::make_unique<Autoplay>(player2_->chart(), faceOff ? 1 : -1);
        }
        failAtMs_ = SDL_getenv("GD02_FAIL_AT") ? float(std::atof(SDL_getenv("GD02_FAIL_AT"))) : 1e30f;
        // Development aid: GD02_END_AT=<song ms> ends (wins) the song early.
        if (const char* e = SDL_getenv("GD02_END_AT")) song_->endMs = std::min(song_->endMs, float(std::atof(e)));
        if (walk_) {
            song_->endMs = std::min(song_->endMs, walk_->songEndMs());
            failAtMs_ = std::min(failAtMs_, walk_->songFailMs());
        }
        failTicks_ = 0;
        exploded_ = false;
        // Development aid: GD02_LEFTY=1 (player 1), 2 (player 2) or 12 (both)
        // turns on the lefty flip (the options' lefty in the menus).
        if (const char* l = SDL_getenv("GD02_LEFTY")) input_.setLefty(SDL_strchr(l, '1') != nullptr, SDL_strchr(l, '2') != nullptr);
        // GD02_KEYS: with the menus, only its "s<song ms>:" items play in the song.
        const char* k = SDL_getenv("GD02_KEYS");
        keys_ = KeyScript(ui_ ? MenuKeys::songItems(k ? k : "") : (k ? k : ""));
        if (!keys_.valid()) SDL_Log("GD02_KEYS: some items were not understood and are ignored");
        SDL_Log("%s (%s): %d Hz, %d channels, %d %s gems, hopo_threshold %d", info.id.c_str(), info.name.c_str(),
                pendingStream_->sampleRate(), pendingStream_->channels(), int(player_->chart().gems.size()),
                difficultyName(static_cast<Difficulty>(difficulty_)), hopoThreshold(info.audio, rules_));
    } catch (const std::exception& ex) {
        message_ = info.id + ": " + ex.what();
        SDL_Log("%s", message_.c_str());
        player_.reset(), bot_.reset(), song_.reset(), pendingStream_.reset();
        return false;
    }
    bandVolume_ = band, guitarVolume_ = guitar;
    // A restart from the pause menu skips most of the lead-in (game fast_intro) [I].
    leadInMs_ = fastIntro ? 1000 : kLeadInMs;
    info_ = &info;
    begun_ = wonSent_ = lostSent_ = outroSent_ = spReadySent_ = false;
    musicPaused_ = false;
    spPhrases_[0] = spPhrases_[1] = streaksBroken_[0] = streaksBroken_[1] = 0;
    paused_ = false;
    lastT_ = float(-leadInMs_);
    std::fill(flashes_.begin(), flashes_.end(), 0.0f);
    lastFrameMs_ = -leadInMs_;
    failMs_ = -1e9f;
    if (prepareTrack()) {
        track_->reset(float(-leadInMs_));
        track_->setLefty(input_.lefty(0));
        if (track2_) track2_->reset(float(-leadInMs_)), track2_->setLefty(input_.lefty(1));
        hud_->reset(float(-leadInMs_));
    }
    return true;
}

void App::beginSong() {
    if (!pendingStream_ || !info_) return;
    mixer_.setSpeed(1);
    mixer_.play(std::move(pendingStream_), mixerTracks(info_->audio, bandVolume_, guitarVolume_), leadInMs_);
    input_.setInGame(true);
    begun_ = true;
}

bool App::startSong(size_t index) {
    if (!loadSong(songs_[index], difficulty_, options_.autoplay, 1, 1, false)) return false;
    beginSong();
    return true;
}

// GH2's track and HUD scenes, loaded once (with the first song). On failure
// the debug highway stands in.
bool App::prepareTrack() {
    if (debugHighway_) return false;
    if (trackTried_) return track_ && hud_;
    trackTried_ = true;
    const uint64_t start = SDL_GetTicksNS();
    try {
        milos_ = std::make_unique<scene::MiloLibrary>(*data_);
        sceneCache_ = std::make_unique<gfx::SceneCache>(canvas_.renderer().device());
        // One player: track_panel_1p; two: track_panel_2p's two tracks.
        const bool two = twoPlayers(mode_);
        auto track = std::make_unique<TrackView>();
        track->load(*data_, *milos_, two ? 0 : -1);
        std::unique_ptr<TrackView> track2;
        if (two) {
            track2 = std::make_unique<TrackView>();
            track2->load(*data_, *milos_, 1);
        }
        auto hud = std::make_unique<HudView>();
        hud->load(*data_, *milos_, hudFile(mode_));
        track_ = std::move(track), track2_ = std::move(track2), hud_ = std::move(hud);
    } catch (const std::exception& ex) {
        SDL_Log("GH2's track or HUD could not be loaded (%s); using the debug highway", ex.what());
        track_.reset(), track2_.reset(), hud_.reset(), sceneCache_.reset(), milos_.reset();
        return false;
    }
    SDL_Log("track and HUD loaded in %.0f ms", double(SDL_GetTicksNS() - start) / 1e6);
    return true;
}

void App::endSong() {
    if (!player_) return;
    SDL_Log("%s: score %d, %d/%d notes, longest streak %d%s (audio underruns: %lld frames)", info_->id.c_str(),
            player_->score(), player_->notesHit(), int(player_->chart().gems.size()), player_->longestStreak(),
            player_->failed() ? ", failed" : "", static_cast<long long>(mixer_.underruns()));
    mixer_.setPaused(false);
    mixer_.setSpeed(1);
    mixer_.stop();
    bot_.reset(), player_.reset(), bot2_.reset(), player2_.reset(), song_.reset(), pendingStream_.reset();
    info_ = nullptr;
    input_.setInGame(false);
    paused_ = false;
    begun_ = false;
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
        if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) {
            if (loadData(e.drop.data) && !debugMenu_) startUi();
        } else if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) ||
                   e.type == SDL_EVENT_FINGER_UP ||
                   (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) ||
                   (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)) {
            picker_.open(window_);
        } else if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK)) {
            running_ = false;
        }
        return;
    }
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data && !player_ && !ui_) {  // another disc image
        loadData(e.drop.data);
        message_ = dataMessage_;
        return;
    }
    for (const InputEvent& a : actions_) handleAction(a);
}

// One input action. With the menus: the player's controller layer while
// the song plays, and every action to the menus (GH2's kPad buttons: Start
// pauses through game_screen's script). Without them (--song, the debug
// list): Start pauses, then Start or green / Confirm resumes, Back quits the
// song; in the list Up / Down pick, Left / Right the difficulty, Confirm
// plays, Back quits.
void App::handleAction(const InputEvent& a) {
    if (ui_) {
        if (player_ && begun_ && !paused_ && !bot_) playerAction(a);
        ui_->action(a);
        return;
    }
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
        playerAction(a);
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

void App::playerAction(const InputEvent& a) {
    if (exploded_ || wonSent_ || lostSent_) return;
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
}

// The song clock for this frame: the mixer's, then (after the song is over)
// the wall clock from there: GH2's `game set_realtime TRUE` keeps the track
// moving while the music winds down or stops.
float App::songClock() {
    if (!begun_) return float(-leadInMs_);
    if (overNs_ && !paused_) return overT_ + float(double(SDL_GetTicksNS() - overNs_) / 1e6);
    if (overNs_) return lastT_;
    return float(mixer_.frameTimeMs());
}

// One frame of the song's rules: scripted and autoplay input, the players,
// the track and HUD events, and (with the menus) GH2's game events.
void App::updateSong(float t) {
    // The analog whammy bar, sampled every frame (Player's movement detection).
    if (!bot_ && !paused_ && input_.analogWhammy()) player_->setWhammyValue(input_.whammyValue(), t);
    const bool over = exploded_ || wonSent_ || lostSent_;
    if (!over && !paused_) {
        keys_.feed(*player_, t);
        if (bot_) bot_->feed(*player_, t);
        if (t >= failAtMs_ && !player_->failed()) player_->forceFail();
        player_->update(t);
        if (player2_) bot2_->feed(*player2_, t), player2_->update(t);
    }
    const float dt = std::clamp(float(t - lastFrameMs_), 0.0f, 100.0f);
    lastFrameMs_ = t;
    for (float& f : flashes_) f = std::max(0.0f, f - dt / 150.0f);
    const std::vector<PlayerEvent> events = player_->takeEvents();
    const std::vector<PlayerEvent> events2 = player2_ ? player2_->takeEvents() : std::vector<PlayerEvent>{};
    for (const PlayerEvent& e : events) {
        if (e.kind == PlayerEvent::Kind::PhraseCaptured) ++spPhrases_[0];
        if (e.kind == PlayerEvent::Kind::EndStreak) ++streaksBroken_[0];
        if (e.kind == PlayerEvent::Kind::StarPowerReady && ui_) ui_->gameEvent("star_power_ready", {Value::integer(0)});
        if (e.kind != PlayerEvent::Kind::Hit || e.gem < 0) continue;
        const Gem& g = player_->chart().gems[size_t(e.gem)];
        for (int f = 0; f < 5; ++f)
            if (g.frets & (1u << f)) flashes_[size_t(f)] = 1.0f;
    }
    const bool gh2 = track_ && hud_ && !debugHighway_;
    if (gh2) {
        track_->onEvents(events, *player_, t);
        hud_->onEvents(events, 0, t);
        if (track2_ && player2_) track2_->onEvents(events2, *player2_, t), hud_->onEvents(events2, 1, t);
    }
    mixer_.setGuitarAudible(player_->guitarAudible());
    if (!ui_) return;
    // GH2's game events: the song won at its end (game_won_msg: the track
    // explodes, the music stops), or lost (game_lost: the music winds down);
    // then game_outro_complete takes the menus on.
    const uint64_t now = SDL_GetTicksNS();
    if (!wonSent_ && !lostSent_ && !player_->failed() && t >= song_->endMs) {
        wonSent_ = true, overNs_ = now, overT_ = t, outroAtNs_ = now + uint64_t(kExplodeOutroMs * 1e6);
        ui_->gameEvent("game_over");
        ui_->gameEvent("game_won_msg", {Value::integer(0)});
    }
    if (!wonSent_ && !lostSent_ && player_->failed()) {
        lostSent_ = true, overNs_ = now, overT_ = t, outroAtNs_ = now + uint64_t(kLoseOutroMs * 1e6);
        ui_->gameEvent("game_over");
        ui_->gameEvent("game_lost");
    }
    if ((wonSent_ || lostSent_) && !outroSent_ && !paused_ && now >= outroAtNs_) {
        outroSent_ = true;
        ui_->gameEvent("game_outro_complete");
    }
}

void App::frameSong() {
    float t = float(mixer_.frameTimeMs());
    actions_.clear();
    input_.poll(SDL_GetTicksNS(), actions_);  // touch chords
    for (const InputEvent& a : actions_) handleAction(a);
    if (!player_) return;
    // A failed song: the music winds down over 2 s and stops (game.dta
    // slow_music); the screen runs on the wall clock from then on.
    if (failTicks_) {
        const float since = float(double(SDL_GetTicksNS() - failTicks_) / 1e6);
        mixer_.setSpeed(std::max(0.0f, (kLoseSlowMs - since) / kLoseSlowMs));
        if (since >= kLoseSlowMs) mixer_.setPaused(true);
        t = failMs_ + since;
    }
    updateSong(t);
    lastT_ = t;
    // The song is won at its end: the track explodes (game.dta game_won_msg:
    // {track_panel explode}, beatmatch paused).
    const bool gh2 = track_ && hud_ && !debugHighway_;
    if (!player_->failed() && !exploded_ && t >= song_->endMs) {
        exploded_ = true;
        if (track_) track_->explode(t);
        if (track2_) track2_->explode(t);
        if (hud_) hud_->won(t);
    }
    drawSong(t);
    // A failed song closes a little after the fail (GH2's lose screen comes later).
    if (player_->failed() && failMs_ < -1e8f) failMs_ = t, failTicks_ = SDL_GetTicksNS();
    const float outro = exploded_ && gh2 ? kExplodeOutroMs : kOutroMs;
    if (!paused_ && (t > song_->endMs + outro || (player_->failed() && t > failMs_ + 3000))) endSong();
}

// One frame with GH2's menus: the scripted keys, the UI's update (which
// may start or end the song), the song's rules, then the UI's drawing (the
// track and HUD are its game panels).
void App::frameUi() {
    const double now = double(SDL_GetTicksNS()) / 1e6 - uiStartMs_;
    actions_.clear();
    input_.poll(SDL_GetTicksNS(), actions_);
    for (const InputEvent& a : actions_) handleAction(a);
    for (const MenuKeys::Key& k : menuKeys_.due(now)) {
        if (k.edge >= 0) ui_->buttonDown(k.button, k.player);
        if (k.edge <= 0) ui_->buttonUp(k.button, k.player);
    }
    if (walk_) {
        walk_->poll(*ui_, now);
        if (walk_->done()) running_ = false;
    }
    ui_->update();
    if (!running_) return;
    // The menus get the guitar's green / red as Confirm / Back when no song is being played.
    input_.setInGame(player_ && begun_ && !paused_ && ui_->currentScreen() == "game_screen");
    if (player_ && begun_) {
        const float t = songClock();
        updateSong(t);
        lastT_ = t;
    }
    canvas_.begin({0, 0, 0, 255});
    ui_->draw();
    shootScreens();
    // GD02_SCREENSHOT_AT: wall-clock ms in the menus, "s<ms>" song times.
    if (captureMenuScreenshot(canvas_, now, player_ && begun_ ? double(lastT_) : -1e9)) running_ = false;
    canvas_.end();
}

// Development aid: GD02_SHOTS=<dir> (or the walk test's GD02_WALK_SHOTS)
// saves each screen the menus reach, once settled, as <dir>/NN_<screen>.bmp.
void App::shootScreens() {
    const char* dir = SDL_getenv("GD02_WALK_SHOTS");
    if (!dir) dir = SDL_getenv("GD02_SHOTS");
    if (!dir || !ui_) return;
    const auto& h = ui_->history();
    const double now = double(SDL_GetTicksNS()) / 1e6;
    if (h.size() > shotsTaken_ && !ui_->inTransition()) {
        if (shotDue_ < 0 || shotScreen_ != h.back()) shotDue_ = now + 700, shotScreen_ = h.back();
        if (now >= shotDue_ && ui_->currentScreen() == shotScreen_) {
            char name[32];
            std::snprintf(name, sizeof name, "%02zu_", h.size());
            const std::string path = std::string(dir) + "/" + name + shotScreen_ + ".bmp";
            if (saveScreenshot(canvas_, path)) SDL_Log("screen shot: %s", path.c_str());
            shotsTaken_ = h.size();
            shotDue_ = -1;
        }
    }
}

// ---- UiHost

bool App::gameLoad(const GameSetup& setup, std::string& error) {
    auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return s.id == setup.song; });
    if (it == songs_.end()) {
        error = "no song '" + setup.song + "'";
        return false;
    }
    syncOffsetMs_ = setup.syncOffsetMs;
    const bool autoplay = setup.autoplay || options_.autoplay || envOn("GD02_AUTOPLAY") || (walk_ && walk_->autoplay());
    if (!loadSong(*it, setup.difficulty, autoplay, setup.bandVolume, setup.guitarVolume, setup.fastIntro)) {
        error = message_;
        return false;
    }
    if (track_) track_->setLefty(setup.lefty);
    overNs_ = 0;
    return true;
}

void App::gameBegin() {
    overNs_ = 0;
    beginSong();
}

int App::gameCounter(int player, const std::string& key) const {
    const int p = std::clamp(player, 0, 1);
    if (key == "sp_phrases") return spPhrases_[p];
    if (key == "streaks_broken") return streaksBroken_[p];
    if (key == "gems") {
        const Player* pl = gamePlayer(p);
        return pl ? int(pl->chart().gems.size()) : 0;
    }
    return 0;
}

void App::trackCommand(const std::string& msg, const std::vector<Value>& args) {
    (void)args;
    // The track view keeps its own intro, extend and smasher animations
    // (render/track_view.h); explode is the scripts' (game_won_msg).
    if (msg == "explode" && !exploded_) {
        exploded_ = true;
        if (track_) track_->explode(lastT_);
        if (track2_) track2_->explode(lastT_);
    }
}

void App::hudCommand(const std::string& msg, const std::vector<Value>& args) {
    // set_win_showing TRUE: the HUD's win message (HudView::won); the meters
    // and SONG FAILED are the HUD view's own.
    if (msg == "set_win_showing" && !args.empty() && args[0].truthy() && hud_) hud_->won(lastT_);
}

void App::drawTrack() {
    if (!player_ || !song_) return;
    canvas_.flush();
    canvas_.renderer().device().clear(nullptr, true);
    const float speed = rules_.trackSpeed[size_t(difficulty_)];
    if (track_ && !debugHighway_) {
        track_->draw(canvas_.renderer(), *sceneCache_, *song_, *player_, lastT_, speed);
        if (track2_ && player2_) track2_->draw(canvas_.renderer(), *sceneCache_, *song_, *player2_, lastT_, speed);
    } else {
        highway_.draw(canvas_, *song_, *player_, lastT_, speed, info_ ? info_->name : "", flashes_);
    }
}

void App::drawHud() {
    if (!player_ || !hud_ || debugHighway_) return;
    canvas_.flush();
    canvas_.renderer().device().clear(nullptr, true);
    std::vector<const Player*> players{player_.get()};
    if (player2_) players.push_back(player2_.get());
    hud_->draw(canvas_.renderer(), *sceneCache_, canvas_, players, lastT_);
}

void App::stopMusic() {
    if (music_ != Music::None && !player_) mixer_.stop();
    music_ = Music::None;
}

void App::songPreview(const std::string& songId) {
    if (player_) return;
    if (songId.empty()) {
        if (music_ == Music::Preview) stopMusic();
        return;
    }
    auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return s.id == songId; });
    if (it == songs_.end()) return;
    try {
        auto s = std::make_unique<VorbisStream>(data_->read(it->audio.moggPath));
        auto loop = std::make_unique<LoopSource>(std::move(s), it->previewStartMs, it->previewEndMs, 1000);
        mixer_.play(std::move(loop), mixerTracks(it->audio, bandVolume_, guitarVolume_), 0);
        music_ = Music::Preview;
    } catch (const std::exception& e) {
        SDL_Log("song_preview %s: %s", songId.c_str(), e.what());
    }
}

void App::menuMusic(bool on) {
    if (player_) return;
    if (!on) {
        if (music_ == Music::Menu) stopMusic();
        return;
    }
    if (music_ == Music::Menu) return;
    // config/synth.dta (metamusic (volume -6) (music metaloop1_warpigs ...)):
    // a random loop from sfx/streams.
    static const char* const kLoops[] = {"metaloop1_warpigs", "metaloop2_psycho", "metaloop3_whowas", "metaloop_artillery", "metaloop_crowns",
                                         "metaloop_ftk", "metaloop_hoods", "metaloop_rawdog", "metaloop_voivod"};
    const std::string name = kLoops[SDL_GetTicks() % (sizeof kLoops / sizeof kLoops[0])];
    try {
        auto s = std::make_unique<VorbisStream>(data_->read("sfx/streams/" + name + ".mogg"));
        const int ch = s->channels();
        std::vector<Mixer::Track> tracks(static_cast<size_t>(ch));
        for (int c = 0; c < ch; ++c) tracks[size_t(c)].pan = ch == 2 ? (c == 0 ? -1.0f : 1.0f) : 0.0f, tracks[size_t(c)].volume = 0.5f;
        mixer_.play(std::make_unique<LoopSource>(std::move(s), 0, 0, 0), tracks, 0);
        music_ = Music::Menu;
    } catch (const std::exception& e) {
        SDL_Log("meta_music %s: %s", name.c_str(), e.what());
    }
}

void App::creditsMusic(bool on) {
    if (player_) return;
    if (!on) {
        if (music_ == Music::Credits) stopMusic();
        return;
    }
    try {
        auto s = std::make_unique<VorbisStream>(data_->read("sfx/streams/credits.mogg"));
        // Channels 0 / 2 left, 1 / 3 right (CreditsPanel 0x8229B310) [C].
        std::vector<Mixer::Track> tracks(static_cast<size_t>(s->channels()));
        for (size_t c = 0; c < tracks.size(); ++c) tracks[c].pan = c % 2 ? 1.0f : -1.0f, tracks[c].volume = 0.8f;
        mixer_.play(std::make_unique<LoopSource>(std::move(s), 0, 0, 0), tracks, 0);
        music_ = Music::Credits;
    } catch (const std::exception& e) {
        SDL_Log("credits music: %s", e.what());
    }
}

void App::playMovie(const std::string& name) {
    stopMusic();
    if (name.empty() || !data_) return;
    if (::playMovie(*data_, name, canvas_, mixer_, input_) == WmvPlayer::Result::Quit) running_ = false;
    mixer_.stop();
}

void App::applyOptions(float band, float guitar, float fx, bool lefty0, bool lefty1, int syncOffsetMs) {
    (void)fx;  // the sound effects are not played yet
    bandVolume_ = band, guitarVolume_ = guitar;
    syncOffsetMs_ = syncOffsetMs;
    if (!SDL_getenv("GD02_LEFTY")) input_.setLefty(lefty0, lefty1);
}

// One frame of the song screen without the menus (--song, the debug list):
// GH2's track and HUD (or the debug highway), then the 2D text on top.
void App::drawSong(float t) {
    const bool gh2 = track_ && hud_ && !debugHighway_;
    canvas_.begin(gh2 ? SDL_Color{0, 0, 0, 255} : SDL_Color{12, 10, 16, 255});
    const std::string title = info_->name + " - " + info_->artist + "  [" + difficultyName(static_cast<Difficulty>(difficulty_)) +
                              (bot_ ? ", autoplay]" : "]");
    const float speed = rules_.trackSpeed[size_t(difficulty_)];
    if (gh2) {
        const uint64_t drawStartNs = SDL_GetTicksNS();
        // Two players: the track speed is the mean of their difficulties'
        // (0x826932D0) [C]; both play one difficulty here.
        track_->draw(canvas_.renderer(), *sceneCache_, *song_, *player_, t, speed);
        if (track2_ && player2_) track2_->draw(canvas_.renderer(), *sceneCache_, *song_, *player2_, t, speed);
        // Development aid: GD02_HUD=0 hides the HUD (to look at the track).
        const char* showHud = SDL_getenv("GD02_HUD");
        std::vector<const Player*> players{player_.get()};
        if (player2_) players.push_back(player2_.get());
        if (!showHud || *showHud != '0') hud_->draw(canvas_.renderer(), *sceneCache_, canvas_, players, t);
        // Development aid: GD02_STATS=1 prints the frame rate and particles.
        if (SDL_getenv("GD02_STATS")) {
            static uint64_t last = 0, work = 0;
            static int frames = 0, particles = 0;
            ++frames;
            const uint64_t now = SDL_GetTicksNS();
            work += now - drawStartNs;
            particles = std::max(particles, track_->particlesDrawn() + (track2_ ? track2_->particlesDrawn() : 0));
            if (!last) last = now;
            if (now - last >= 1'000'000'000ull) {
                SDL_Log("stats: %.1f fps, %.2f ms a frame to draw the track and HUD (CPU), up to %d track particles",
                        double(frames) * 1e9 / double(now - last), double(work) / 1e6 / frames, particles);
                frames = 0, last = now, work = 0, particles = 0;
            }
        }
    } else {
        highway_.draw(canvas_, *song_, *player_, t, speed, title, flashes_);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d:%02d / %d:%02d", int(std::max(0.0f, t) / 60000), int(std::max(0.0f, t) / 1000) % 60,
                      int(song_->endMs / 60000), int(song_->endMs / 1000) % 60);
        canvas_.text(24, 44, 1.5f, {160, 160, 170, 255}, buf);
    }
    // GH2's HUD shows "SONG FAILED!" itself (HudView).
    if (player_->failed() && !gh2) canvas_.textCentered(kScreenW * 0.5f, 330, 4, {230, 60, 60, 255}, "SONG FAILED");
    else if (paused_) canvas_.textCentered(kScreenW * 0.5f, 330, 4, {235, 235, 240, 255}, "PAUSED");
    if (captureDebugScreenshot(canvas_, true, t)) running_ = false;
    canvas_.end();
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

// The debug song list (GD02_DEBUG_MENU=1, or when GH2's menus cannot start).
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
