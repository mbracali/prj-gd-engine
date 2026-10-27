// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app_ids.h"
#include "audio/mixer.h"
#include "audio/synth.h"
#include "data/game_data.h"
#include "data/json.h"
#include "devaids/menu_keys.h"
#include "devaids/screenshot.h"
#include "game/autoplay.h"
#include "game/band.h"
#include "game/camera_director.h"
#include "game/key_script.h"
#include "game/light_presets.h"
#include "game/song_db.h"
#include "game/song_setup.h"
#include "game/venue.h"
#include "game/crowd.h"
#include "game/world.h"
#include "game/world_driver.h"
#include "input/input.h"
#include "platform/app_icon.h"
#include "platform/disc_picker.h"
#include "platform/paths.h"
#include "render/canvas.h"
#include "render/highway.h"
#include "render/hud_view.h"
#include "render/scene_gpu.h"
#include "render/track_view.h"
#include "render/venue_view.h"
#include "scene/milo.h"
#include "script/script.h"
#include "tests/walktest.h"
#include "ui/enhanced_panel.h"
#include "ui/screen_text.h"
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
        if (world_) world_->crowd().audio().setPaused(paused);
        if (synth_) synth_->setPaused(paused);
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
    void worldLoad(const std::string& path) override;
    void worldEnter() override;
    void worldDraw() override { drawWorld(); }
    void worldUnload() override { unloadVenue(); }
    ScriptObject* worldScript() override { return world_ ? world_->script() : nullptr; }
    void songPreview(const std::string& songId) override;
    void menuMusic(bool on) override;
    void creditsMusic(bool on) override;
    void playMovie(const std::string& name) override;
    void applyOptions(float band, float guitar, float fx, bool lefty0, bool lefty1, int syncOffsetMs) override;
    void quit() override { running_ = false; }
    bool playSfx(const std::string& name) override { return playSfxArgs(name, 0, 0, 0); }
    bool playSfxArgs(const std::string& name, float volumeDb, float pan, float transpose) override;
    void setSfxMasterDb(float db) override {
        if (synth_) synth_->setMasterVolumeDb(db);
    }
    float sfxMasterDb() override { return synth_ ? synth_->masterVolumeDb() : 0.0f; }
    void stopSfx(const std::string& name) override;
    void stopAllSfx() override {
        if (synth_) synth_->stopAll();
    }
    bool saveScreenshot(const std::string& path) override { return ::saveScreenshot(canvas_, path); }
    std::string enhancedValue(const std::string& key) override;
    void setEnhancedValue(const std::string& key, const std::string& value) override;
    void menuBackdrop(bool on) override;
    bool drawMenuBackdrop() override;
    void pauseAllSfx(bool paused) override {
        if (synth_) synth_->setPaused(paused);
    }

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
    void drawWorld();
    bool loadVenue(const std::string& venue, VenueVariant variant);
    void unloadVenue();
    void startBand(const SongInfo& info, const Song& song, bool backdrop = false);
    void loadEnhanced();
    void applyEnhanced();
    void updateBackdrop();
    void drawFpsCounter();
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
    int escapes_ = 0;  // Back presses in a row on the main menu (three quit)
    SDL_JoystickID virtualPad_ = 0;  // GD02_VIRTUAL_PAD's controller (GD02_KEYS "j" items)
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
    // The venue behind the track (M8): set by the venue code once a venue is
    // loaded; draws the world with the renderer, materials with
    // DrawContext::hdr = `hdr` (GH2's HDR buffer, render/post_fx.h).
    std::function<void(gfx::Renderer& r, bool hdr)> worldDraw_;
    // The venue (game/venue.h, game/world.h, render/venue_view.h): without
    // the menus the song's (venues::forSong, GD02_VENUE), else world_panel's.
    std::unique_ptr<gfx::SceneCache> venueCache_;
    std::unique_ptr<Venue> venue_;
    std::unique_ptr<World> world_;
    std::unique_ptr<VenueView> venueView_;
    // The camera director and the lighting presets (M8 phase 2; game/camera_director.h, game/light_presets.h).
    std::unique_ptr<CameraDirector> director_;
    // The band on stage (M9, game/band.h): made when the song begins in the venue.
    std::unique_ptr<Band> band_;
    // The players' look from the menus (GameSetup), for the band.
    std::array<std::string, 2> bandOutfit_, bandGuitar_, bandSkin_;
    bool encoreCheck_ = false, encoreFx_ = false, campaignFinal_ = false;  // GameSetup's encore state
    std::unique_ptr<LightPresets> lighting_;
    WorldDriver worldDriver_;
    // The Enhanced Options' live backdrop (menuBackdrop): the second venue
    // with the band playing a song of its tier, silently, on the wall clock,
    // through a camera of its own.
    struct Backdrop {
        const SongInfo* info = nullptr;
        std::unique_ptr<Song> song;
        WorldDriver driver;
        uint64_t startNs = 0;
        float fromMs = 0;
        bool framed = false;  // the shot's target and direction are set
        gfx::Vec3 target, dir;
        float dist = 300, spread = 100, aim = 0;
        uint64_t lastNs = 0;
        int lightPart = -1;
    };
    std::unique_ptr<Backdrop> backdrop_;
    // The Enhanced Options (enhanced.json in the save folder), what is in
    // effect, and the FPS counter's average.
    Json enhanced_ = Json::object();
    std::string screenModeApplied_;
    bool fpsCounter_ = false;
    double fps_ = 0;
    uint64_t fpsLastNs_ = 0;
    std::string venuePath_;
    gfx::PostProcState venuePost_;  // the static shot's PostProc (the director's from M8 phase 2)
    bool venuePostOn_ = false;
    // Keyboard, controllers and touch (input/input.h); Start pauses a song.
    Input input_;
    std::vector<InputEvent> actions_;
    bool paused_ = false;
    std::vector<float> flashes_ = std::vector<float>(5, 0.0f);
    double lastFrameMs_ = 0;
    float failMs_ = -1e9f;
    // The menus' music: none, a meta loop or a song's preview.
    enum class Music { None, Menu, Preview, Credits } music_ = Music::None;
    // GH2's sound effects (audio/synth.h): config/sound.dta's banks, the
    // venue's through the world; made with the game data.
    std::unique_ptr<Synth> synth_;
    float fxVolumeDb_ = 0;  // the options' sound effects volume (dB): {options get_fx_volume}
    // The menus' background sounds while their music plays (MetaMusic,
    // 0x822A23F8 reads config/synth.dta (background_sfx (sequence
    // meta_people) (min_delay_sec 10) (max_delay_sec 20))) [C data]; a
    // uniform delay between the two (0x822A1440) [C]: when the next one is due (s).
    double metaSfxDue_ = -1;
    void metaBackgroundSfx();
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
    if (const char* vp = SDL_getenv("GD02_VIRTUAL_PAD")) virtualPad_ = Input::attachVirtualPad(vp);
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | gfx::prepareWindow();  // OpenGL (render/device.h)
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    debugHighway_ = envOn("GD02_DEBUG_HIGHWAY");
    debugMenu_ = envOn("GD02_DEBUG_MENU");
    if (!options_.walk.empty()) {
        walk_ = std::make_unique<WalkDriver>(options_.walk);
        if (!walk_->known()) {
            std::fprintf(stderr, "%s: unknown walk test '%s' (quickplay, lose, pause, attract, career, back, enhanced, cheats)\n", app::kProgram, options_.walk.c_str());
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
        if (synth_) {
            synth_->update();
            metaBackgroundSfx();
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
    unloadVenue();
    ui_.reset();
    if (synth_)
        SDL_Log("sound effects: %d played (%d voices, %llu started on the device), %d samples decoded in %.0f ms, %d failures",
                synth_->stats().plays, synth_->stats().voices, (unsigned long long)synth_->player().started(), synth_->stats().decoded,
                synth_->stats().decodeMs, synth_->stats().decodeFailures + synth_->stats().unresolved);
    synth_.reset();
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
        unloadVenue();
        ui_.reset();
        hud_.reset(), track_.reset(), track2_.reset();
        sceneCache_.reset(), milos_.reset();
        trackTried_ = false;
        synth_.reset();
        data_ = std::move(data), rules_ = rules, songs_ = std::move(songs);
        // The sound effects: the banks decode here (all of them: ~0.1 s).
        const uint64_t t0 = SDL_GetTicksNS();
        synth_ = std::make_unique<Synth>();
        for (const auto& p : synth_->loadBanks(*data_)) SDL_Log("sound effects: %s", p.c_str());
        synth_->player().open();
        synth_->setFxVolumeDb(fxVolumeDb_);
        SDL_Log("sound effects: %d banks, %d samples decoded in %.0f ms", int(synth_->banks().size()), synth_->stats().decoded,
                double(SDL_GetTicksNS() - t0) / 1e6);
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
    loadEnhanced();
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
    // The venue: without the menus the song's (its quickplay venue or career
    // tier, GD02_VENUE overrides); with them world_panel loads it (worldLoad).
    if (!ui_ && !debugHighway_) {
        const std::string v = venues::fromEnv().empty() ? venues::forSong(*data_, info) : venues::fromEnv();
        const VenueVariant var = mode_ == DevMode::Vs || mode_ == DevMode::FaceOff ? VenueVariant::Mp
                                 : mode_ == DevMode::Coop                          ? VenueVariant::Coop
                                                                                   : VenueVariant::Single;
        loadVenue(v, var);
    }
    if (!band_ && song_ && info_) startBand(*info_, *song_);
    worldDriver_ = WorldDriver(!ui_);
    worldDriver_.reset(float(-leadInMs_));
    return true;
}

void App::beginSong() {
    if (!pendingStream_ || !info_) return;
    mixer_.setSpeed(1);
    mixer_.play(std::move(pendingStream_), mixerTracks(info_->audio, bandVolume_, guitarVolume_), leadInMs_);
    input_.setInGame(true);
    begun_ = true;
    // A song starts without a shot's PostProc or depth of field (rnd reset_postproc).
    canvas_.renderer().post().reset();
    canvas_.renderer().post().clearDof();
    // Without the menus the world starts here (world_panel enter otherwise).
    if (!ui_ && world_) world_->start(mode_ != DevMode::Single);
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
    band_.reset();
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
    // A click or tap skips the boot splashes, like a button (gd01's logos).
    if (ui_ && ui_->inSplashes() &&
        ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.which != SDL_TOUCH_MOUSEID) || e.type == SDL_EVENT_FINGER_UP)) {
        ui_->buttonDown(ui::kPad_Start, 0);
        ui_->buttonUp(ui::kPad_Start, 0);
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
        // gd01's: Escape (Back) three times in a row on the main menu quits.
        if (a.down) escapes_ = a.action == Action::Back && ui_->currentScreen() == "main_screen" ? escapes_ + 1 : 0;
        if (escapes_ >= 3) {
            running_ = false;
            return;
        }
        if (player_ && begun_ && !paused_ && !bot_) playerAction(a);
        // In a song Back (Esc, a joypad's B) pauses like Start, as in gd01:
        // game_screen pauses on kPad_Start only.
        if (a.action == Action::Back && input_.inGame()) {
            InputEvent start = a;
            start.action = Action::Start;
            ui_->action(start);
            return;
        }
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
            if (world_) world_->crowd().audio().setPaused(paused_);
            return;
        }
        if (paused_) {
            if (!a.down) return;
            if (a.action == Action::Back || a.action == Action::Fret1) endSong();
            else if (a.action == Action::Confirm || a.action == Action::Fret0) {
                paused_ = false, mixer_.setPaused(false);
                if (world_) world_->crowd().audio().setPaused(false);
            }
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
    case Action::Confirm: case Action::Start: case Action::Fret0: message_.clear(), startSong(cursor_); break;
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
    // The venue: the song's events, its clock, anims and particles.
    if (world_ && venueView_) {
        worldDriver_.update(*world_, *song_, t, player_.get(), events, player2_.get(), events2);
        if (player_->failed()) worldDriver_.gameLost(*world_);
        else if (t >= song_->endMs) worldDriver_.gameWon(*world_);
        world_->update();
        if (lighting_) lighting_->update();
        if (band_) {
            const gfx::Vec3 c = venueView_->camera().position();
            const std::array<float, 3> cam{c.x, c.y, c.z};
            band_->update(world_->seconds(), world_->beats(), &cam);
        }
        if (director_) {
            director_->setStarPower(player_->starPowerActive());
            director_->update();
            // Development aid: GD02_FORCE_SHOT=<CamShot> plays that shot whenever another starts.
            if (const char* fs = SDL_getenv("GD02_FORCE_SHOT"))
                if (const auto* shot = venue_->find<scene::CamShot>(fs); shot && director_->shot() != shot) {
                    director_->forceShot(shot);
                    director_->update();
                }
            if (director_->hasCamera()) venueView_->setCamera(director_->camera());
        }
        // Development aid: GD02_FACE_CAM=<member>[:<distance>] (guitarist0, singer, ...) frames that
        // member's head from the director camera's side, to check the faces.
        if (const char* fc = SDL_getenv("GD02_FACE_CAM"); fc && *fc && band_) {
            const std::string spec = fc;
            const size_t colon = spec.find(':');
            const std::string who = spec.substr(0, colon);
            const float dist = colon == std::string::npos ? 22.0f : std::strtof(spec.c_str() + colon + 1, nullptr);
            scene::Xfm head;
            if (band_->objectXfm(who, "bone_head.mesh", head) || band_->objectXfm(who, "bone_head.trans", head)) {
                const gfx::Vec3 target{head.pos[0], head.pos[1], head.pos[2]};
                // From the side the director camera is on, level with the head.
                gfx::Vec3 dir = venueView_->camera().position() - target;
                dir.z = 0;
                if (gfx::dot(dir, dir) < 1e-6f) dir = {0, -1, 0};
                const gfx::Vec3 eye = target + gfx::normalize(dir) * dist;
                gfx::Camera cam = gfx::Camera::lookAt(eye, target);
                cam.yFov = 0.6f;
                venueView_->setCamera(cam);
            }
        }
        venueView_->update();
    }
    if (!ui_) return;
    // GH2's game events: the song won at its end (game_won_msg: the track
    // explodes, the music stops), or lost (game_lost: the music winds down);
    // then game_outro_complete takes the menus on.
    const uint64_t now = SDL_GetTicksNS();
    if (!wonSent_ && !lostSent_ && !player_->failed() && t >= song_->endMs) {
        wonSent_ = true, overNs_ = now, overT_ = t, outroAtNs_ = now + uint64_t(kExplodeOutroMs * 1e6);
        // GH2's order: the result, then game_over (0x822CB5B8).
        if (band_) band_->setResult(true);
        // $encore: the crowd calls for an encore (0x822CA6E0), every player's crowd full.
        if (encoreCheck_)
            for (int p = 0; p < 2; ++p)
                if (Player* pl = p == 0 ? player_.get() : player2_.get()) pl->setCrowdRating(1.0f);
        ui_->gameEvent("game_won_msg", {Value::integer(encoreCheck_ ? 1 : 0)});
        ui_->gameEvent("game_over");
    }
    if (!wonSent_ && !lostSent_ && player_->failed()) {
        lostSent_ = true, overNs_ = now, overT_ = t, outroAtNs_ = now + uint64_t(kLoseOutroMs * 1e6);
        if (band_) band_->setResult(false);
        ui_->gameEvent("game_lost");
        if (encoreCheck_) ui_->gameEvent("encore_lost");
        ui_->gameEvent("game_over");
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
        if (k.keycode) {  // a real key, through the input layer (next frame's events)
            for (int edge : {1, -1}) {
                if ((edge > 0 && k.edge < 0) || (edge < 0 && k.edge > 0)) continue;
                SDL_Event e{};
                e.type = edge > 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                e.key.timestamp = SDL_GetTicksNS();
                e.key.key = SDL_Keycode(k.keycode);
                e.key.scancode = SDL_GetScancodeFromKey(e.key.key, nullptr);
                e.key.down = edge > 0;
                SDL_PushEvent(&e);
            }
        } else if (k.padButton >= 0) {
            if (SDL_Joystick* j = virtualPad_ ? SDL_GetJoystickFromID(virtualPad_) : nullptr)
                SDL_SetJoystickVirtualButton(j, k.padButton, k.edge > 0);
            else
                SDL_Log("GD02_KEYS: a \"j\" item needs GD02_VIRTUAL_PAD");
        } else {
            if (k.edge >= 0) ui_->buttonDown(k.button, k.player);
            if (k.edge <= 0) ui_->buttonUp(k.button, k.player);
        }
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
    } else if (backdrop_) {
        updateBackdrop();
    }
    canvas_.begin({0, 0, 0, 255});
    ui_->draw();
    drawFpsCounter();
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
            if (::saveScreenshot(canvas_, path)) SDL_Log("screen shot: %s", path.c_str());
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
    bandOutfit_ = setup.outfit, bandGuitar_ = setup.guitar, bandSkin_ = setup.skin;
    encoreCheck_ = setup.encoreCheck, encoreFx_ = setup.encoreFx, campaignFinal_ = setup.campaignFinal;
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
    // GamePanel's start (0x822CCA60): an encore begins with every crowd full.
    if (encoreFx_)
        for (int p = 0; p < 2; ++p)
            if (Player* pl = p == 0 ? player_.get() : player2_.get()) pl->setCrowdRating(1.0f);
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

// The world: drawn into GH2's HDR buffer and post-processed (bloom, DOF,
// luminance map; render/post_fx.h), or straight to the screen with
// GD02_POST=0; the track and HUD then go on top, as GH2's final pass leaves
// them out. No venue yet: nothing is drawn and the frame is as before.
void App::drawWorld() {
    if (!worldDraw_) return;
    canvas_.flush();
    gfx::Renderer& r = canvas_.renderer();
    const float clear[4] = {0, 0, 0, 1};
    if (r.post().begin(clear)) {
        worldDraw_(r, true);
        r.post().end();
    } else {
        worldDraw_(r, false);
    }
}

// The venue: its milos (game/venue.h), the world runtime on the UI's
// interpreter (or its own without the menus) and its view, drawn by
// drawWorld from the static first shot (INTRO) and the INTRO lighting preset
// until the director (M8 phase 2).
bool App::loadVenue(const std::string& venue, VenueVariant variant) {
    const std::string path = venues::worldMilo(venue, variant);
    if (venue_ && path == venuePath_) return true;
    unloadVenue();
    const uint64_t start = SDL_GetTicksNS();
    try {
        venue_ = std::make_unique<Venue>(*data_, venue, variant, ui_ ? ui_->interp().language : std::string("eng"));
        for (const auto& p : venue_->problems()) SDL_Log("venue: %s", p.c_str());
        const DataNode* objects = ui_ && ui_->bootTree() ? ui_->bootTree()->find("objects") : nullptr;
        world_ = std::make_unique<World>(*venue_, ui_ ? &ui_->interp() : nullptr, objects);
        if (synth_) {
            world_->setSynth(synth_.get());
            // The venue bank's samples decode now, not at their first cue.
            for (const auto& p : venue_->parts()) synth_->preload(p.milo->root());
        }
        venueCache_ = std::make_unique<gfx::SceneCache>(canvas_.renderer().device());
        venueView_ = std::make_unique<VenueView>(*world_, *venueCache_);
        lighting_ = std::make_unique<LightPresets>(*world_);
        director_ = std::make_unique<CameraDirector>(*world_);
        // The crowd: each shot's crowd3d / rotate (game/crowd.h), and its audio.
        director_->onShotStarted = [this](const scene::CamShot& s) { world_->crowd().startShot(&s); };
        world_->crowd().audio().open();
        if (!ui_)
            director_->provideGameObjects(variant != VenueVariant::Single,
                                          mode_ == DevMode::FaceOff || mode_ == DevMode::Vs ? "multi_vs" : variant == VenueVariant::Coop ? "coop" : "single");
    } catch (const std::exception& e) {
        SDL_Log("venue %s: %s", path.c_str(), e.what());
        unloadVenue();
        return false;
    }
    venuePath_ = path;
    if (const scene::LightPreset* p = world_->firstPreset()) world_->applyPreset(*p, 0);
    if (song_ && info_) startBand(*info_, *song_);
    // The static shot's PostProc (its `postprocess` flag) [I: the director
    // selects it on every shot from phase 2 on].
    venuePostOn_ = false;
    if (const scene::CamShot* shot = venueView_->shot())
        if (auto* pp = venue_->find<scene::PostProc>(shot->flag("postprocess"))) {
            const gfx::TextureId lum = pp->luminanceMap.empty() ? gfx::TextureId{} : venueCache_->texture(pp, pp->luminanceMap);
            venuePost_ = gfx::postProcState(*pp, lum);
            venuePostOn_ = true;
        }
    worldDraw_ = [this](gfx::Renderer& r, bool hdr) {
        if (!venueView_) return;
        int x = 0, y = 0, w = 0, h = 0;
        r.pictureRect(x, y, w, h);
        if (hdr) {
            // The current shot's PostProc and depth of field (the director's;
            // before the first shot, the static one's).
            if (director_ && director_->shot()) {
                const scene::PostProc* pp = director_->postProc();
                if (pp) r.post().select(gfx::postProcState(*pp, pp->luminanceMap.empty() ? gfx::TextureId{} : venueCache_->texture(pp, pp->luminanceMap)));
                else r.post().reset();
                const CameraDirector::Dof& d = director_->dof();
                const gfx::Camera& c = director_->camera();
                if (d.on && d.maxBlur > 0) r.post().setDof(gfx::DofState::fromShot(d.focus, d.blurDepth, d.maxBlur, c.nearPlane, c.farPlane));
                else r.post().clearDof();
            } else if (venuePostOn_) {
                r.post().select(venuePost_);
            }
            r.post().size(w, h);
        }
        venueView_->draw(r, w, h, hdr);
    };
    SDL_Log("venue %s %s loaded in %.0f ms: %d milos, %d proxies, shot %s", venue.c_str(), venues::variantName(variant),
            double(SDL_GetTicksNS() - start) / 1e6, int(venue_->parts().size()), int(venue_->proxies().size()),
            venueView_->shot() ? venueView_->shot()->name.c_str() : "none");
    return true;
}

void App::unloadVenue() {
    band_.reset();
    worldDraw_ = nullptr;
    director_.reset();
    lighting_.reset();
    venueView_.reset();
    world_.reset();
    venue_.reset();
    venueCache_.reset();
    venuePath_.clear();
}

// GH2's play_sfx / synth play_sequence: the game's banks, then the venue's
// (crowd_win, encore_yes... from the UI's scripts).
bool App::playSfxArgs(const std::string& name, float volumeDb, float pan, float transpose) {
    if (!synth_) return false;
    if (synth_->play(name, volumeDb, pan, transpose)) return true;
    return world_ && world_->venue().find(name) && world_->playSound(name, nullptr, volumeDb, pan, transpose);
}

void App::stopSfx(const std::string& name) {
    if (!synth_) return;
    if (!synth_->stop(name) && world_) world_->stopSound(name);
}

// The band (game/band.h) once the song begins in a venue: the players'
// outfits, guitars and skins from their configs (the menus: quick play's
// song character, the career's chosen one, ui/char_select.h; without the
// menus the song's quickplay one). GD02_CHAR=<outfit>, GD02_GUITAR=<guitar>
// and GD02_SKIN=<skin> override player 0's; GD02_NO_BAND=1 leaves the
// stage empty.
void App::startBand(const SongInfo& info, const Song& song, bool backdrop) {
    band_.reset();
    if (!world_ || envOn("GD02_NO_BAND")) return;
    Band::Config cfg;
    cfg.players = backdrop ? 1 : twoPlayers(mode_) ? 2 : 1;
    if (!backdrop) {
        cfg.character = bandOutfit_, cfg.guitar = bandGuitar_, cfg.skin = bandSkin_;
        cfg.encore = encoreCheck_, cfg.campaignFinal = campaignFinal_;
        if (const char* c = SDL_getenv("GD02_CHAR")) cfg.character[0] = c;
        if (const char* g = SDL_getenv("GD02_GUITAR")) cfg.guitar[0] = g, cfg.skin[0].clear();
        if (const char* k = SDL_getenv("GD02_SKIN")) cfg.skin[0] = k;
    }
    // The Air Guitar cheat: a symbol in $cheat_guitar is every guitarist's
    // guitar (change_guitar 0x822B6250) [C]; long_cheats.dta sets air_guitar.
    if (ui_)
        if (const Value g = ui_->interp().var("cheat_guitar"); g.type == Value::Type::Symbol && !g.s.empty())
            for (int p = 0; p < cfg.players; ++p) cfg.guitar[size_t(p)] = g.s, cfg.skin[size_t(p)].clear();
    const DataNode* objects = ui_ && ui_->bootTree() ? ui_->bootTree()->find("objects") : nullptr;
    try {
        band_ = std::make_unique<Band>(*world_, *data_, info, cfg, objects);
        if (backdrop) {
            band_->rating = [](int) { return 0.8f; };
            band_->excitement = [] { return 3; };  // kExcitementGreat: the crowd and the band at their liveliest
        } else {
            band_->rating = [this](int p) {
                const Player* pl = p == 0 ? player_.get() : player2_.get();
                return pl ? pl->rockMeter() : 0.5f;
            };
            band_->excitement = [this] { return worldDriver_.excitementLevel(); };
        }
        band_->loadSong(parseMidi(data_->read(info.audio.midiPath)), &song);
        // The characters enter with the world (World::start); a world already
        // started (the menus' world_panel) gets them at once.
        if (world_->started()) band_->enter();
        for (const auto& p : band_->problems()) SDL_Log("band: %s", p.c_str());
    } catch (const std::exception& e) {
        SDL_Log("band: %s", e.what());
        band_.reset();
    }
}

// world_panel's milo: ../world/<v>/<v>[_mp|_coop].milo (GD02_VENUE replaces the venue).
void App::worldLoad(const std::string& path) {
    for (const std::string& v : venues::names())
        for (VenueVariant var : {VenueVariant::Single, VenueVariant::Mp, VenueVariant::Coop})
            if (venues::worldMilo(v, var) == path) {
                const std::string over = venues::fromEnv();
                loadVenue(over.empty() ? v : over, var);
                return;
            }
    SDL_Log("world_panel: %s is not a venue", path.c_str());
}

void App::worldEnter() {
    // WORLD_OBJECT_BASE enter and the `start` triggers; the panel's script
    // sends single_player / multi_player itself.
    if (world_) world_->start(false, false);
}

void App::drawTrack() {
    if (!player_ || !song_) return;
    canvas_.flush();
    canvas_.renderer().device().clear(nullptr, true);
    float speed = rules_.trackSpeed[size_t(difficulty_)];
    // The Hyper Speed cheat: track_speed x 1.5 (0x826967B8) [C].
    if (ui_ && ui_->cheatOn("cheat_hyper_speed_mode")) speed *= 1.5f;
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

void App::metaBackgroundSfx() {
    constexpr double kMinDelay = 10, kMaxDelay = 20;  // config/synth.dta background_sfx
    const double now = double(SDL_GetTicksNS()) / 1e9;
    if (music_ != Music::Menu) {
        metaSfxDue_ = -1;
        return;
    }
    const double delay = kMinDelay + (kMaxDelay - kMinDelay) * double(SDL_randf());  // RandomFloat(min, max) 0x822A1440 [C]
    if (metaSfxDue_ < 0) metaSfxDue_ = now + delay;
    else if (now >= metaSfxDue_) {
        synth_->play("meta_people");
        metaSfxDue_ = now + delay;
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
    // The sound effects volume: the options' index (fx x 11) through
    // config/sound.dta's slider dB table.
    // GH2 passes it as the volume of its game sounds (play_game_sfx, the
    // crowd's reactions), not to every sound (audio/synth.h).
    fxVolumeDb_ = data_ ? Synth::optionsVolumeDb(*data_, int(std::lround(fx * 11.0f))) : 20.0f * std::log10(std::max(fx, 1e-5f));
    if (synth_) synth_->setFxVolumeDb(fxVolumeDb_);
    bandVolume_ = band, guitarVolume_ = guitar;
    syncOffsetMs_ = syncOffsetMs;
    if (!SDL_getenv("GD02_LEFTY")) input_.setLefty(lefty0, lefty1);
}

// ---- the Enhanced Options (ui/enhanced_panel.h)

namespace {

std::string enhancedPath() {
    const char* dir = SDL_getenv("GD02_SAVE_DIR");
    return (dir && *dir ? std::string(dir) : prefDir()) + "enhanced.json";
}

const ui::EnhancedSetting* enhancedSetting(const std::string& key) {
    for (const ui::EnhancedSetting& e : ui::enhancedSettings())
        if (key == e.key) return &e;
    return nullptr;
}

}  // namespace

void App::loadEnhanced() {
    enhanced_ = Json::object();
    std::ifstream f(enhancedPath(), std::ios::binary);
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        try {
            Json j = Json::parse(ss.str());
            if (j.type == Json::Type::Object && j["format"].str() == "gd02-enhanced") enhanced_ = j;
        } catch (const std::exception& e) {
            SDL_Log("enhanced options: %s: %s (the defaults are used)", enhancedPath().c_str(), e.what());
        }
    }
    applyEnhanced();
}

std::string App::enhancedValue(const std::string& key) {
    const ui::EnhancedSetting* e = enhancedSetting(key);
    if (!e) return {};
    const std::string& v = enhanced_[key].str();
    for (const char* ok : e->values)
        if (v == ok) return v;
    return e->values.front();  // unset or not valid: the default
}

void App::setEnhancedValue(const std::string& key, const std::string& value) {
    if (!enhancedSetting(key)) return;
    enhanced_.set("format", Json::string("gd02-enhanced"));
    enhanced_.set(key, Json::string(value));
    // Written atomically, as the save (ui/career.h).
    const std::string path = enhancedPath(), tmp = path + ".tmp";
    std::error_code ec;
    if (const std::filesystem::path dir = std::filesystem::path(path).parent_path(); !dir.empty()) std::filesystem::create_directories(dir, ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << enhanced_.dump(1) << "\n";
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) SDL_Log("enhanced options: cannot write %s: %s", path.c_str(), ec.message().c_str());
    applyEnhanced();
}

// Puts the Enhanced Options in effect (at start and on every change), as gd01's App::applyEnhanced.
void App::applyEnhanced() {
    fpsCounter_ = enhancedValue("fps_counter") == "on";
    gfx::RenderDevice& dev = canvas_.renderer().device();
    dev.setInternalHeight(std::clamp(std::atoi(enhancedValue("internal_res").c_str()), 0, 4320));
    if (!SDL_getenv("GD02_FPS")) dev.setFrameCap(std::clamp(std::atoi(enhancedValue("max_fps").c_str()), 30, 1000));
    // Screen mode: windowed, fullscreen (the display's mode at the desktop
    // size) or borderless (fullscreen at the desktop's own mode).
    std::string mode = enhancedValue("screen_mode");
#ifdef __ANDROID__
    mode = "borderless";  // always full screen on phones
#endif
    if (window_ && mode != screenModeApplied_) {
        bool ok = true;
        if (mode == "windowed") {
            ok = SDL_SetWindowFullscreen(window_, false);
        } else {
            const SDL_DisplayMode* fullMode = nullptr;
            SDL_DisplayMode closest;
            if (mode == "fullscreen") {
                const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
                if (const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(display);
                    desk && SDL_GetClosestFullscreenDisplayMode(display, desk->w, desk->h, desk->refresh_rate, true, &closest))
                    fullMode = &closest;
            }
            ok = SDL_SetWindowFullscreenMode(window_, fullMode) && SDL_SetWindowFullscreen(window_, true);
        }
        if (!ok) SDL_Log("enhanced options: screen mode %s: %s", mode.c_str(), SDL_GetError());
        // The first time only when not windowed (the window opens windowed).
        screenModeApplied_ = mode;
        if (!SDL_getenv("GD02_FPS")) dev.setFrameCap(std::clamp(std::atoi(enhancedValue("max_fps").c_str()), 30, 1000));  // the new mode's refresh rate
    }
}

// Bottom left, in GH2's menu font: the frames drawn a second (averaged).
void App::drawFpsCounter() {
    const uint64_t now = SDL_GetTicksNS();
    if (fpsLastNs_ && now > fpsLastNs_) {
        const double f = 1e9 / double(now - fpsLastNs_);
        fps_ = fps_ <= 0 ? f : fps_ * 0.95 + f * 0.05;
    }
    fpsLastNs_ = now;
    if (!fpsCounter_ || !ui_ || ui_->headless()) return;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d FPS", int(std::lround(fps_)));
    canvas_.flush();
    ui::ScreenText& t = ui_->screenText();
    t.begin(canvas_.renderer());
    t.draw(canvas_.renderer().materials(), "helveticablack.font", buf, 25, 697, 22, {0, 0, 0, 0.6f}, 0x41);
    t.draw(canvas_.renderer().materials(), "helveticablack.font", buf, 23, 695, 22, {0.2f, 1, 0.3f, 1}, 0x41);
}

// The live backdrop: the second venue (venues::names()[1], small1) with the
// band playing the first song of its tier, without sound, looping.
void App::menuBackdrop(bool on) {
    if (!on) {
        if (!backdrop_) return;
        backdrop_.reset();
        if (!player_) unloadVenue();
        return;
    }
    if (backdrop_ || player_ || !data_ || songs_.empty() || ui_->headless()) return;
    const std::string venue = venues::names()[1];
    auto b = std::make_unique<Backdrop>();
    for (const SongInfo& s : songs_)
        if (venues::forSong(*data_, s) == venue) {
            b->info = &s;
            break;
        }
    if (!b->info) b->info = &songs_.front();
    if (!loadVenue(venue, VenueVariant::Single)) return;
    try {
        b->song = std::make_unique<Song>(loadChart(*data_, *b->info, rules_));
    } catch (const std::exception& e) {
        SDL_Log("enhanced options: backdrop %s: %s", b->info->id.c_str(), e.what());
        unloadVenue();
        return;
    }
    b->fromMs = b->song->endMs * 0.25f;
    b->driver.reset(b->fromMs);
    b->startNs = SDL_GetTicksNS();
    backdrop_ = std::move(b);
    // The band first: it enters with the world (World::start), as in a song.
    startBand(*backdrop_->info, *backdrop_->song, true);
    world_->start(false, false);
    world_->message("excitement", {Value::integer(3)});
}

bool App::drawMenuBackdrop() {
    if (!backdrop_ || !worldDraw_) return false;
    drawWorld();
    return true;
}

void App::updateBackdrop() {
    Backdrop& b = *backdrop_;
    if (!world_ || !venueView_) return;
    float ms = b.fromMs + float(double(SDL_GetTicksNS() - b.startNs) / 1e6);
    if (ms > b.song->endMs - 3000) {  // around again
        b.startNs = SDL_GetTicksNS();
        b.driver.reset(b.fromMs);
        b.lightPart = -1;
        ms = b.fromMs;
        startBand(*b.info, *b.song, true);
    }
    b.driver.update(*world_, *b.song, ms, nullptr, {}, nullptr, {});
    world_->update();
    // The lights: the venue's chorus and verse presets in turn, 20 s each,
    // not the song's cues (their blackouts left the band in the dark).
    if (lighting_) {
        const int part = int(double(SDL_GetTicksNS() - b.startNs) / 20e9);
        if (part != b.lightPart) {
            b.lightPart = part;
            lighting_->ignoreEvents(true);
            lighting_->setExcitement(3);
            lighting_->setLighting({part % 2 ? "VERSE" : "CHORUS", "CHORUS", "VERSE"}, nullptr, false);
        }
        lighting_->update();
    }
    if (band_) {
        const gfx::Vec3 c = venueView_->camera().position();
        const std::array<float, 3> cam{c.x, c.y, c.z};
        band_->update(world_->seconds(), world_->beats(), &cam);
    }
    // The shot: from the audience's side, on the singer's chest, wide enough
    // for the guitarist and the drummer, swaying slowly from side to side.
    if (!b.framed && band_) {
        scene::Xfm singer, other;
        if (band_->objectXfm("singer", "bone_head.mesh", singer)) {
            b.target = {singer.pos[0], singer.pos[1], singer.pos[2] - 14};
            // The stage's front: from the drummer (at the back) to the
            // singer; else towards the venue's first shot.
            gfx::Vec3 dir = venueView_->camera().position() - b.target;
            if (scene::Xfm drummer; band_->objectXfm("drummer", "bone_head.mesh", drummer))
                dir = gfx::Vec3{singer.pos[0] - drummer.pos[0], singer.pos[1] - drummer.pos[1], 0};
            dir.z = 0;
            if (gfx::dot(dir, dir) < 1e-6f) dir = {0, -1, 0};
            b.dir = gfx::normalize(dir);
            const gfx::Vec3 side{-b.dir.y, b.dir.x, 0};
            float spread = 40;
            for (const char* who : {"guitarist0", "drummer", "bassist"})
                if (band_->objectXfm(who, "bone_head.mesh", other)) {
                    const gfx::Vec3 p{other.pos[0], other.pos[1], other.pos[2]};
                    spread = std::max(spread, std::fabs(gfx::dot(p - b.target, side)));
                }
            // Wide enough for the band; the camera then looks a little to
            // the singer's left so the band stands right of the list.
            b.spread = spread;
            b.dist = (spread + 34) / 0.45f;  // tan(yFov / 2) x 16:9 = 0.45 at yFov 0.5
            // No farther than the venue's own first shot (the audience's
            // fences and rigging stand behind it).
            const gfx::Vec3 shot = venueView_->camera().position() - b.target;
            b.dist = std::min(b.dist, 0.8f * std::sqrt(shot.x * shot.x + shot.y * shot.y));
            b.aim = 0.32f * 0.45f * b.dist;  // looking that far to the singer's left: the band on the right
            SDL_Log("enhanced options: backdrop on %s, spread %.0f, distance %.0f", b.info->id.c_str(), spread, b.dist);
            b.framed = true;
        }
    }
    if (b.framed) {
        // Following the singer as they move about the stage, smoothly.
        if (scene::Xfm singer; band_ && band_->objectXfm("singer", "bone_head.mesh", singer)) {
            const gfx::Vec3 now{singer.pos[0], singer.pos[1], singer.pos[2] - 14};
            const uint64_t ns = SDL_GetTicksNS();
            const float dt = b.lastNs ? float(double(ns - b.lastNs) / 1e9) : 0.0f;
            b.lastNs = ns;
            b.target = b.target + (now - b.target) * (1 - std::exp(-std::min(dt, 0.25f) / 1.2f));
        }
        const float t = float(double(SDL_GetTicksNS() - b.startNs) / 1e9);
        const float a = 0.14f * std::sin(t * 6.2831853f / 26.0f);
        const gfx::Vec3 d{b.dir.x * std::cos(a) - b.dir.y * std::sin(a), b.dir.x * std::sin(a) + b.dir.y * std::cos(a), 0};
        gfx::Vec3 eye = b.target + d * b.dist;
        eye.z = b.target.z + b.dist * 0.12f + 3 * std::sin(t * 0.37f);  // above the crowd's heads
        const gfx::Vec3 side{-d.y, d.x, 0};
        gfx::Camera cam = gfx::Camera::lookAt(eye, b.target - side * b.aim + gfx::Vec3{0, 0, 6});
        cam.yFov = 0.5f;
        venueView_->setCamera(cam);
    }
    venueView_->update();
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
        drawWorld();
        // Two players: the track speed is the mean of their difficulties'
        // (0x826932D0) [C]; both play one difficulty here.
        // Development aid: GD02_TRACK=0 hides the track (to look at the band).
        const char* showTrack = SDL_getenv("GD02_TRACK");
        if (!showTrack || *showTrack != '0') {
            track_->draw(canvas_.renderer(), *sceneCache_, *song_, *player_, t, speed);
            if (track2_ && player2_) track2_->draw(canvas_.renderer(), *sceneCache_, *song_, *player2_, t, speed);
        }
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
