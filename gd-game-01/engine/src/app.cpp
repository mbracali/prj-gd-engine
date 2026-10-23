// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstring>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <optional>

#include "audio/mixer.h"
#include "data/dta.h"
#include "data/game_data.h"
#include "data/json.h"
#include "data/midi.h"
#include "game/autoplay.h"
#include "game/chart.h"
#include "game/player.h"
#include "game/rules.h"
#include "game/song_db.h"
#include "input/input.h"
#include "render/highway.h"
#include "render/scene_renderer.h"
#include "render/track_renderer.h"
#include "render/venue_renderer.h"
#include "render/vk_upscaler.h"
#include "script/script.h"
#include "ui/panel.h"
#include "audio/crowd_audio.h"
#include "audio/synth.h"
#include "game/campaign.h"
#include "video/mpeg2.h"
#include "video/pss.h"
#include "ui/ui_system.h"
#include "scene/scene.h"
#include "scene/particles.h"
#include "platform/gpu_select.h"
#include "platform/paths.h"
#include "platform/app_icon.h"
#include <cctype>
#include <filesystem>
#ifdef __ANDROID__
#include "platform/android_data.h"
#endif

namespace {

constexpr double kLeadInMs = 2000;
// GamePanel start (FUN_00105b80): the clock starts at -(intro shot duration),
// 10000 ms in every venue's camera.dta, and the game panel gets intro_start.
constexpr double kIntroMs = 10000;
// game.dta (track_extend_ms -2000): arena extend_track -> track_panel do_extend_sequence.
constexpr float kTrackExtendMs = -2000;
constexpr float kOutroMs = 2500;
// Window and message box title.
constexpr const char* kAppTitle = "Guitar Dream Engine: Game 01";
const SDL_Color kText{230, 230, 235, 255}, kDim{140, 140, 150, 255}, kAccent{255, 210, 80, 255}, kBad{240, 90, 80, 255};

std::string configPath() {
    return prefDir().empty() ? std::string() : prefDir() + "config.txt";
}

// The career save (one file for the 8 profiles and the high scores, like the
// PS2 memory card's BASLUS-21224/data).
std::string careerPath() {
    return prefDir().empty() ? std::string("career.json") : prefDir() + "career.json";
}

// The port's Enhanced Settings (options > Enhanced Settings), apart from the career.
std::string enhancedPath() {
    return prefDir().empty() ? std::string("enhanced.json") : prefDir() + "enhanced.json";
}

// The graphics, enhancer and control settings each download ships with
// (gd-game-01/README.md, "Default settings"): desktop Linux, Windows and
// macOS; the Steam Deck build (Linux built with -DGD01_STEAM_DECK=ON); Android.
// Steam Deck is also what Enhanced Settings > Apply Steam Deck Settings sets.
std::map<std::string, Json> profileValues(bool deck) {
    auto b = [](bool v) { return Json::boolean(v); };
    auto t = [](const char* v) { return Json::string(v); };
    std::map<std::string, Json> d = {
        // Every build.
        {"antialiasing", t("none")}, {"ai_upscaling", t("none")}, {"bokeh", t("stronger")}, {"particles", b(true)},
        {"cinematic_color", b(true)}, {"color_grade", t("off")}, {"chromatic", b(true)}, {"light_streaks", b(false)},
        {"more_lights", t("subtle")}, {"no_shaky_cam", b(false)},
        // Desktop: Linux, Windows, macOS.
        {"screen_mode", t("windowed")}, {"max_fps", t("120")}, {"fps_counter", b(true)}, {"internal_res", t("1080")},
        {"sharp_ui", b(true)}, {"bloom", t("high")}, {"film_grain", t("light")}, {"ssao", b(true)},
        {"sharpening", t("low")}, {"star_blur", t("high")}, {"shadows_all", b(true)}, {"light_dust", b(true)},
        {"ground_fog", t("low")}, {"touch", b(false)},
    };
    auto handheld = [&] {
        for (auto& [k, v] : std::map<std::string, Json>{
                 {"screen_mode", t("fullscreen")}, {"max_fps", t("60")}, {"fps_counter", b(false)}, {"bloom", t("medium")},
                 {"film_grain", t("off")}, {"ssao", b(false)}, {"shadows_all", b(false)}, {"light_dust", b(false)},
                 {"ground_fog", t("none")}})
            d[k] = v;
    };
#if defined(__ANDROID__)
    (void)deck;
    handheld();
    for (auto& [k, v] : std::map<std::string, Json>{{"internal_res", t("900")}, {"sharp_ui", b(false)},
                                                     {"sharpening", t("low")}, {"star_blur", t("light")}, {"touch", b(true)}})
        d[k] = v;
#else
#if defined(GD01_STEAM_DECK)
    deck = true;
#endif
#if defined(_WIN32) || defined(__APPLE__)
    d["internal_res"] = t("1440");
#endif
    if (deck) {
        handheld();
        for (auto& [k, v] : std::map<std::string, Json>{{"internal_res", t("800")}, {"sharpening", t("off")}, {"star_blur", t("medium")}})
            d[k] = v;
    }
#endif
    return d;
}

// Enhanced Settings defaults (keys of enhanced.json): the build's profile
// over the rest.
const std::map<std::string, Json>& enhancedDefaults() {
    static const std::map<std::string, Json> d = [] {
        std::map<std::string, Json> m = {
        {"mouse", Json::boolean(true)},
        {"touch", Json::boolean(true)},           {"rockband", Json::boolean(false)},
        {"xinput", Json::boolean(true)},          {"screen_mode", Json::string("windowed")},
        {"max_fps", Json::string("120")},         {"fps_counter", Json::boolean(false)},
        {"internal_res", Json::string("1080")},   {"sharp_ui", Json::boolean(true)},
        {"antialiasing", Json::string("none")},   {"ai_upscaling", Json::string("none")},
        {"ai_upscaling_mode", Json::string("quality")}, {"shadows_all", Json::boolean(false)},
        {"more_lights", Json::string("off")},     {"bokeh", Json::string("stronger")},
        {"particles", Json::boolean(true)},       {"ground_fog", Json::string("none")},
        {"light_dust", Json::boolean(true)},
        {"bloom", Json::string("off")},           {"cinematic_color", Json::boolean(false)},
        {"color_grade", Json::string("off")},     {"film_grain", Json::string("off")},
        {"chromatic", Json::boolean(false)},      {"light_streaks", Json::boolean(false)},
        {"ssao", Json::boolean(false)},           {"sharpening", Json::string("off")},
        {"star_blur", Json::string("none")},      {"hopo_mode", Json::string("original")},
        {"cut_content", Json::boolean(false)},    {"no_fail", Json::boolean(false)},
        {"autoplay", Json::boolean(false)},       {"no_shaky_cam", Json::boolean(false)},
        {"cheat_air_guitar", Json::boolean(false)}, {"cheat_gh_guitar", Json::boolean(false)},
        {"cheat_unlock_all", Json::boolean(false)}, {"cheat_full_meter", Json::boolean(false)},
        {"cheat_no_venue", Json::boolean(false)},   {"cheat_monkey_crowd", Json::boolean(false)},
        {"cheat_skull_crowd", Json::boolean(false)}, {"audio_offset", Json::string("0")},
        {"video_offset", Json::string("0")},
        };
        for (auto& [k, v] : profileValues(false)) m[k] = v;
        return m;
    }();
    return d;
}
// The values a choice setting may take (Load User Settings drops anything else).
const std::map<std::string, std::set<std::string>>& enhancedChoices() {
    static const std::map<std::string, std::set<std::string>> c = {
        {"screen_mode", {"windowed", "fullscreen", "borderless"}},
        {"max_fps", {"60", "120", "240"}},
        {"internal_res", {"448", "480", "720", "800", "900", "1080", "1440", "2160"}},
        {"antialiasing", {"none", "fxaa", "ssaa", "taa"}},
        {"ai_upscaling", {"none", "fsr1", "fsr31", "dlss4", "dlss5"}},  // a saved "fsr4" (removed) loads as none
        {"ai_upscaling_mode", {"performance", "balanced", "quality"}},
        {"more_lights", {"off", "subtle", "strong"}},
        {"bokeh", {"none", "regular", "stronger"}},
        {"ground_fog", {"none", "low", "full", "super"}},
        {"color_grade", {"off", "warm", "cold", "vintage", "noir", "neon"}},
        {"film_grain", {"off", "light", "strong"}},
        {"sharpening", {"off", "low", "high"}},
        {"star_blur", {"none", "light", "medium", "high"}},
        {"hopo_mode", {"original", "forgiving"}},
        {"bloom", {"off", "low", "medium", "high"}}};
    return c;
}
// Number settings (as strings, ms) and their range.
const std::map<std::string, std::pair<int, int>>& enhancedRanges() {
    static const std::map<std::string, std::pair<int, int>> r = {{"audio_offset", {-300, 500}}, {"video_offset", {-300, 500}}};
    return r;
}
// Cheats that cannot be on together (one script variable each): turning one
// on turns its partner off.
const std::map<std::string, std::string>& enhancedExclusive() {
    static const std::map<std::string, std::string> x = {
        {"cheat_air_guitar", "cheat_gh_guitar"}, {"cheat_gh_guitar", "cheat_air_guitar"},
        {"cheat_monkey_crowd", "cheat_skull_crowd"}, {"cheat_skull_crowd", "cheat_monkey_crowd"}};
    return x;
}

// The disc image the game starts with: the one picked last time, else an
// .iso lying next to the program (the Steam Deck instructions put it there;
// Game Mode may show no file picker).
std::string loadRememberedPath() {
    std::ifstream in(configPath());
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("data=", 0) == 0) return line.substr(5);
#ifndef __ANDROID__
    if (const char* base = SDL_GetBasePath()) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(base), ec)) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".iso" && e.is_regular_file(ec)) return e.path().string();
        }
    }
#endif
    return {};
}

void rememberPath(const std::string& path) {
    std::ofstream out(configPath());
    out << "data=" << path << "\n";
}

class App {
public:
    explicit App(const Options& o) : opt_(o), difficulty_(std::clamp(o.difficulty, 0, 3)) {}
    int run();

private:
    enum class Screen { NeedData, Menu, Playing, Paused, Results, SceneView, PanelView };

    void tryLoadData(const std::string& path);
    void startSong(size_t index, ControllerMode mode);
    // Tutorials (docs/tutorials_plan.md §2.4, §7.1): basement with no band, the
    // TUTORIAL camera, the tutorial bank; songs load and start on the script's word.
    void tutorialLoad(size_t index);
    void tutorialStart(bool skipIntro, bool startPaused);
    bool tutorial_ = false;
    int escapes_ = 0;  // Back presses in a row on the main menu
    bool attractAutoplay_ = false;  // set_auto_play before the song starts
    // UnmuteBeatMatchAudio (docs/guitar_audio.md): each player's SlipTo calls
    // seen (resyncs of that player's guitar copy), the common-section flag
    // (+0xe0: active players 0 or 3) and its gain, latched with set_volume.
    std::array<int, 2> slipTosSeen_{};
    bool guitarCommon_ = false;
    float commonAtten_ = 1;
    void setupGuitarCopies();
    // Face-off (docs/multiplayer_plan.md): player 2, its highway and autoplay.
    bool multi_ = false;
    std::unique_ptr<Player> player2_;
    std::unique_ptr<TrackRenderer> track2_;
    std::unique_ptr<Autoplay> autoplay2_;
    std::vector<float> flashes2_ = std::vector<float>(5, 0.0f);
    bool hitting2_ = false;
    size_t activeCursor_ = 0;
    int activeMask_ = -1;
    ControllerMode mode2_ = ControllerMode::Guitar;
    Player* playerAt(int p) const { return p == 0 ? player_.get() : p == 1 && multi_ ? player2_.get() : nullptr; }
    void pollPlayer(int p, float t);
    std::unique_ptr<VgsStream> tutorialVgs_;
    std::vector<Mixer::Track> tutorialTracks_;
    uint32_t heldFrets_ = 0;  // player 0's frets and strum bar as levels (the tutorial trainers)
    bool strumHeld_ = false;
    void endSong();
    void captureDebugScreenshot();
    void onAction(const InputEvent& e);
    void frame();
    void drawNeedData();
    void drawLogo();
    void drawTouchButtons();
    int logoTexture_ = -1;  // the badge on the first screen (0: could not load)
    void drawMenu();
    void drawResults();

    Options opt_;
    SDL_Window* window_ = nullptr;
    Renderer renderer_;
    Input input_;
    Mixer mixer_;
    std::unique_ptr<Synth> synth_;        // sound effects (banks, sequences)
    std::unique_ptr<CrowdAudio> crowd_;   // the venue's crowd streams
    std::unique_ptr<Campaign> campaign_;  // career profiles, unlocks, high scores
    std::string careerVenue_, careerCharacter_, careerGuitar_;  // career overrides of the song's quickplay setup
    int baseScore(int player = -1) const;  // over the gems `player` owns (-1: all)
    Highway highway_;
    std::unique_ptr<TrackRenderer> track_;  // the original highway, when its scene loads
    std::unique_ptr<VenueRenderer> venue_;  // the song's venue, when it loads
    // [I] Enhanced Settings' backdrop: the first venue (basement) posed once and
    // frozen, filmed close on the singer's microphone.
    std::unique_ptr<VenueRenderer> backdrop_;
#ifdef __ANDROID__
    AndroidIsoImport isoImport_;
#endif
    std::array<float, 3> backdropMic_{};
    bool drawMenuBackdrop(float& focusX, float& focusY);
    ControllerMode uiMode_ = ControllerMode::Joypad;  // the controller last used in the menus
    size_t venueEvent_ = 0;                 // next song event for the venue
    size_t venueTrigger_ = 0;               // next TRIGGERS message
    float venueMs_ = -1e9f;                 // song time of the last venue update
    void updateVenue(float t);
    bool running_ = true;
    Screen screen_ = Screen::NeedData;
    std::string message_;

    std::unique_ptr<GameData> data_;
    Rules rules_;
    std::vector<SongInfo> songs_;
    size_t selected_ = 0;
    int difficulty_;

    std::unique_ptr<Song> song_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Autoplay> autoplay_;
    size_t playingIndex_ = 0;
    std::vector<float> flashes_ = std::vector<float>(5, 0.0f);
    uint64_t lastFrameNs_ = 0;
    uint64_t fpsWindowNs_ = 0;
    int fpsFrames_ = 0;
    float fps_ = 0, fpsWorst_ = 0;

    // Scene viewer (development).
    void openSceneView();
    void drawSceneView();
    std::unique_ptr<Scene> viewScene_;
    std::unique_ptr<UiSystem> ui_;  // the original menus (ghui scripts)
    Json enhanced_ = Json::object();  // Enhanced Settings (enhanced.json)
    // A setting's value in effect: saved, else its default.
    Json enhanced(const std::string& key) const {
#ifdef __ANDROID__
        if (key == "touch") return Json::boolean(true);  // a phone may have no other control
#endif
        auto def = enhancedDefaults().find(key);
        if (enhanced_.has(key)) return enhanced_[key];
        return def != enhancedDefaults().end() ? def->second : Json();
    }
    bool enhancedOn(const std::string& key) const { return enhanced(key).truthy(); }
    // --av-offset plus the calibrated audio delay (Enhanced Settings).
    double avOffsetMs() const { return opt_.avOffsetMs + enhancedInt("audio_offset"); }
    int enhancedInt(const std::string& key) const { return std::atoi(enhanced(key).str().c_str()); }
    void setEnhanced(const std::string& key, Json value);
    void saveEnhanced();
    void applyEnhanced();
    void postFrameExtras(PostFrame& pf, VenueRenderer& venue, bool song);
    float starBlurFade_ = 0, starBlurTime_ = -1;
    void applyVenueLights(VenueRenderer& venue);
    void applyCheats(const std::string& only = "");
    // Enhanced Settings > Export User Files / Load User Settings / Load User
    // Progress: a system dialog picks the folder or file; its answer (from the
    // dialog's thread) is handled in the main loop.
    void userFiles(const std::string& action);
    void pollUserFiles();
    void exportUserFiles(const std::string& dir);
    bool importSettings(const std::string& file);
    bool importProgress(const std::string& file);
    std::mutex dialogMutex_;
    std::vector<std::pair<std::string, std::string>> dialogResults_;  // (action, path; "" = cancelled)
    std::string userFilesStatus_;
    std::string screenModeApplied_ = "windowed";
    // arena::<object> for the scripts: set_showing / showing on the venue's scenes.
    class VenueObjectProxy : public ScriptObject {
    public:
        VenueObjectProxy(const std::string& name, std::function<SceneObject*(const std::string&)> find)
            : ScriptObject(name), find_(std::move(find)) {}
        bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
            SceneObject* o = find_(name());
            Drawable* d = o ? o->drawable() : nullptr;
            r = Value::integer(0);
            if (msg == "set_showing") return d && (d->showing = !a.empty() && a[0].truthy()), true;
            if (msg == "showing") return r = Value::integer(d && d->showing), true;
            return true;
        }

    private:
        std::function<SceneObject*(const std::string&)> find_;
    };
    std::map<std::string, std::unique_ptr<VenueObjectProxy>> venueProxies_;
    void drawFpsCounter();
    uint64_t uiTicks_ = 0;
    void backToMenu();
    void uiButton(const InputEvent& e);
    void injectDebugKeys(std::vector<InputEvent>& out);
    // Touch: the highways' lanes on screen while a song plays (Input::setTouchLanes).
    void updateTouchLanes();
    void songPreview(const std::string& id);
    int playerStat(const std::string& stat, int player = 0) const;
    float endMs_ = -1;  // song time the song was failed or finished, with the UI
    float songMs_ = 0;  // current song time
    bool hitting_ = false;  // GuitarCharacter "hitting" (hit_gem, cleared by miss / pass)
    float lastSongMs_ = -1e30f;  // song time of the previous frame (intro timeline)
    int arenaExcitement_ = 2;    // the last excitement sent to the arena script
    bool musicStarted_ = false;  // arena music_start sent (measure 2)
    uint64_t endWallNs_ = 0;     // wall clock at game over (the game clock goes realtime)
    void arenaEvents(float t, float lastT);
    void playerEvent(const PlayerEvent& ev, int player = 0);
    void pollMenuMusic();
    bool menuMusicOn_ = false, previewing_ = false;
    std::string menuLoop_;  // the metamusic loop picked when the meta panel loaded
    // meta play_movie (FUN_0024edf8): one picture per NTSC frame, 48 kHz PCM on its
    // own stream, everything else frozen and silent; Green / X / Start skip.
    struct Movie {
        std::unique_ptr<PssMovie> pss;
        std::string path;
        uint64_t pos = 0, startNs = 0, next = 0;
        int texture = 0;
        SDL_AudioStream* audio = nullptr;
        Image frame;
    };
    std::unique_ptr<Movie> movie_;
    void startMovie(const std::string& file);
    void movieFrame();
    void endMovie();
    uint64_t nextPeopleNs_ = 0;
    std::mt19937 musicRng_{std::random_device{}()};
    std::unique_ptr<ScriptLoader> scripts_;
    std::unique_ptr<UiResources> uiRes_;
    std::unique_ptr<Panel> panel_;
    std::unique_ptr<SceneRenderer> viewRenderer_;
    std::vector<std::string> viewRoots_;
    std::string viewCam_;
};

void App::openSceneView() {
    try {
        viewScene_ = std::make_unique<Scene>(data_->read(opt_.viewScene));
        if (size_t slash = opt_.viewScene.rfind('/'); slash != std::string::npos)
            viewScene_->loadExternalTextures(*data_, opt_.viewScene.substr(0, slash));
        viewRenderer_ = std::make_unique<SceneRenderer>(renderer_, *viewScene_);
        viewCam_ = opt_.viewCam.empty() ? viewRenderer_->firstCamera() : opt_.viewCam;
        if (!opt_.viewRoot.empty()) {
            viewRoots_ = {opt_.viewRoot};
        } else {
            std::set<std::string> contained;
            for (const SceneObject& o : viewScene_->objects())
                if (o.view) contained.insert(o.view->draw.draws.begin(), o.view->draw.draws.end());
            for (const SceneObject& o : viewScene_->objects())
                if (o.view && !contained.count(o.name)) viewRoots_.push_back(o.name);
        }
        SDL_Log("Scene %s: %u objects, camera '%s', roots:", opt_.viewScene.c_str(), unsigned(viewScene_->objects().size()), viewCam_.c_str());
        for (const auto& r : viewRoots_) SDL_Log("  %s", r.c_str());
        screen_ = Screen::SceneView;
    } catch (const std::exception& ex) {
        message_ = ex.what();
    }
}

void App::drawSceneView() {
    Mat4 vp;
    if (viewCam_ == "auto") {
        vp = viewRenderer_->overviewCamera(kScreenW / kScreenH);
    } else if (!viewRenderer_->camera(viewCam_, kScreenW / kScreenH, vp)) {
        renderer_.text(20, 20, 2, kBad, "no camera '" + viewCam_ + "'");
        return;
    }
    renderer_.clearDepth();
    for (const auto& r : viewRoots_) viewRenderer_->draw(r, vp);
    renderer_.text(10, 700, 1.5f, kDim, opt_.viewScene + "  cam " + viewCam_);
}

void App::tryLoadData(const std::string& path) {
    try {
        auto data = std::make_unique<GameData>(path);
        Rules rules = loadRules(*data);
        auto songs = loadSongs(*data);
        // Tutorial songs go last: no menu lists them, the tutorials load them by id.
        auto firstTutorial = std::stable_partition(songs.begin(), songs.end(), [](const SongInfo& s) { return s.id.rfind("tutorial", 0) != 0; });
        if (firstTutorial == songs.begin()) throw std::runtime_error("No playable songs found in " + path);
        data_ = std::move(data);
        rules_ = rules;
        songs_ = std::move(songs);
        selected_ = 0;
        screen_ = Screen::Menu;
        message_.clear();
        rememberPath(path);
        try {
            synth_ = std::make_unique<Synth>(mixer_, *data_);
            crowd_ = std::make_unique<CrowdAudio>(mixer_, *data_);
            crowd_->fxVolume = [this] { return campaign_ ? campaign_->profile().options.fx : 1.0f; };
            synth_->loadMenuBanks();
            UiSystem::Hooks hooks;
            hooks.songs = [this]() -> const std::vector<SongInfo>& { return songs_; };
            hooks.startGame = [this](int index, int difficulty, const std::string& venue, const std::string& character,
                                     const std::string& guitar) {
                difficulty_ = difficulty;
                careerVenue_ = venue, careerCharacter_ = character, careerGuitar_ = guitar;
                if (index >= 0 && size_t(index) < songs_.size()) startSong(size_t(index), uiMode_);
            };
            try {
                campaign_ = std::make_unique<Campaign>(*data_);
            } catch (const std::exception& ex) {
                SDL_Log("career unavailable: %s", ex.what());
                campaign_.reset();
            }
            // The memory card is career.json: the boot screens load it (bootup_load) and the
            // autosave / data settings screens save it.
            hooks.loadCampaign = [this] { return campaign_ && campaign_->load(careerPath()); };
            hooks.saveCampaignFile = [this] { return campaign_ && campaign_->save(careerPath()); };
            hooks.saveExists = [this] { return std::ifstream(careerPath()).good(); };
            hooks.campaign = campaign_.get();
            hooks.saveCampaign = [this] {
                if (campaign_ && !campaign_->save(careerPath())) SDL_Log("career: could not save %s", careerPath().c_str());
            };
            hooks.starsOf = [this](int score) {
                int base = baseScore();
                return base <= 0 ? 3 : score >= 3 * base ? 5 : score >= 2 * base ? 4 : 3;
            };
            hooks.songPreview = [this](const std::string& id) { songPreview(id); };
            hooks.menuMusic = [this](bool on) {
                menuMusicOn_ = on;
                if (!on && !previewing_) mixer_.stopMusic(1000);
            };
            hooks.playMovie = [this](const std::string& file) { startMovie(file); };
            hooks.menuMusicLoad = [this] {
                menuLoop_.clear();
                menuMusicOn_ = true;
            };
            hooks.setPaused = [this](bool paused) {
                if (screen_ != Screen::Playing && screen_ != Screen::Paused) return;
                mixer_.setPaused(paused);
                synth_->pauseAll(paused);   // GamePanel SetPaused -> pause_all_sfx
                crowd_->setPaused(paused);  // ArenaPanel_v7
                screen_ = paused ? Screen::Paused : Screen::Playing;
            };
            hooks.leaveGame = [this] {
                if (tutorial_) {
                    tutorial_ = false;
                    tutorialVgs_.reset();
                    mixer_.stopStream(Mixer::kVoice, 0);
                    crowd_->setEnabled(true);
                }
                mixer_.setPaused(false);
                mixer_.stop();
                mixer_.setSongSpeed(1);
                synth_->pauseAll(false);
                crowd_->turnOff();
                synth_->loadMenuBanks();  // MetaPanel: sfx/metagame
                venue_.reset();
                screen_ = Screen::Menu;
            };
            hooks.synth = synth_.get();
            hooks.crowd = crowd_.get();
            hooks.msPerTick = [this] {
                if (!song_) return 1.0f;
                double tick = song_->tempo.msToTick(songMs_);
                return float(song_->tempo.tickToMs(tick + 1) - song_->tempo.tickToMs(tick));
            };
            hooks.musicSpeed = [this](float speed) { mixer_.setSongSpeed(speed); };
            hooks.masterVolume = [this](float v) { mixer_.spu().setMaster(v); };
            hooks.depthOfField = [this](bool on) {
                if (venue_) venue_->setDepthOfField(on);
            };
            hooks.guitaristStarPower = [this] {
                if (venue_) venue_->startStarPower();
            };
            hooks.musicPaused = [this](bool paused) { mixer_.setSongSpeed(paused ? 0.0f : 1.0f); };
            hooks.tutorialLoad = [this](int index) { tutorialLoad(size_t(index)); };
            hooks.padConnected = [this](int slot) { return input_.padConnected(slot); };
            hooks.input = &input_;
            // CreditsPanel FUN_00141e18: credits.vgs, pans -1 / +1, looping, 1 s fade in to 0.8; 1.25 s fade out.
            hooks.creditsMusic = [this](bool on) {
                if (!on) return mixer_.stopStream(Mixer::kMusic, 1250);
                const std::string path = "sfx/streams/credits.vgs";
                if (!data_ || !data_->exists(path)) return;
                try {
                    auto vgs = std::make_unique<VgsStream>(data_->read(path));
                    std::vector<Mixer::Track> tracks(size_t(vgs->channels()));
                    for (size_t c = 0; c < tracks.size(); ++c) tracks[c].pan = c % 2 == 0 ? -1.0f : 1.0f;
                    mixer_.playStream(Mixer::kMusic, std::move(vgs), std::move(tracks), 0, 0, true, 1000, 0.8f);
                } catch (const std::exception& ex) {
                    SDL_Log("credits music: %s", ex.what());
                }
            };
            hooks.venueObject = [this](const std::string& name) -> ScriptObject* {
                if (!venue_ || !venue_->findObject(name)) return nullptr;
                auto& p = venueProxies_[name];
                if (!p) p = std::make_unique<VenueObjectProxy>(name, [this](const std::string& n) { return venue_ ? venue_->findObject(n) : nullptr; });
                return p.get();
            };
            hooks.playerCommand = [this](const std::string& msg, const std::vector<Value>& a) {
                if (!player_) return false;
                auto num = [&](size_t k) { return k < a.size() ? a[k].number() : 0.0f; };
                if (msg == "set_crowd_rating_active") return player_->setCrowdRatingActive(num(0) != 0), true;
                if (msg == "set_crowd_rating") return player_->setCrowdRating(num(0)), true;
                if (msg == "fill_star_power") return player_->setStarPower(1), true;
                if (msg == "empty_star_power") return player_->setStarPower(0), true;
                if (msg == "set_star_power") return player_->setStarPower(num(0)), true;
                if (msg == "set_star_power_deploy_rate") return player_->setDeployRate(num(0)), true;
                if (msg == "set_star_power_phrase_boost") return player_->setPhraseBoost(num(0)), true;
                if (msg == "set_whammystarpowerenabled") return player_->setWhammySpEnabled(num(0) != 0), true;
                if (msg == "set_mercuryswitchenabled") return player_->setMercuryEnabled(num(0) != 0), true;
                return false;
            };
            // player_matcher set_volume -> UnmuteBeatMatchAudio_v40 / FUN_002660c0: the
            // channel volume is recomputed now, with the common attenuation if the
            // active players were 0 or 3 when the volume was set.
            hooks.guitarVolume = [this](float v) {
                for (int k = 0; k < (multi_ ? 2 : 1); ++k) mixer_.setGuitarVolume(v * (guitarCommon_ ? commonAtten_ : 1.0f), k);
            };
            hooks.tutorialStart = [this](bool skipIntro, bool startPaused) { tutorialStart(skipIntro, startPaused); };
            hooks.setTimePaused = [this](bool paused) {
                if (screen_ == Screen::Playing || screen_ == Screen::Paused) mixer_.setPaused(paused);
            };
            hooks.padState = [this](uint32_t& frets, bool& strum) { frets = heldFrets_, strum = strumHeld_; };
            hooks.voicePlay = [this](const std::string& path, float volume) {
                if (!data_ || !data_->exists(path)) {
                    SDL_Log("tutorial: no voice-over %s", path.c_str());
                    return;
                }
                try {
                    auto vgs = std::make_unique<VgsStream>(data_->read(path));
                    std::vector<Mixer::Track> tracks(size_t(vgs->channels()));
                    for (size_t c = 0; c < tracks.size(); ++c) tracks[c].pan = tracks.size() == 2 ? (c == 0 ? -1.0f : 1.0f) : 0.0f;
                    mixer_.playStream(Mixer::kVoice, std::move(vgs), std::move(tracks), 0, 0, false, 0, volume);
                } catch (const std::exception& ex) {
                    SDL_Log("tutorial: voice-over %s: %s", path.c_str(), ex.what());
                }
            };
            hooks.voicePlaying = [this] { return mixer_.streamPlaying(Mixer::kVoice); };
            hooks.voiceStop = [this] { mixer_.stopStream(Mixer::kVoice, 0); };
            hooks.voicePause = [this](bool paused) { mixer_.setStreamPaused(Mixer::kVoice, paused); };
            // The attract mode's player_matcher set_auto_play: the game plays itself.
            hooks.autoPlay = [this](bool on) {
                attractAutoplay_ = on;
                if (player_ && song_) autoplay_ = on ? std::make_unique<Autoplay>(player_->chart()) : nullptr;
            };
            hooks.enhancedSetting = [this](const std::string& key) { return enhancedOn(key); };
            hooks.setEnhancedSetting = [this](const std::string& key, bool on) { setEnhanced(key, Json::boolean(on)); };
            hooks.enhancedValue = [this](const std::string& key) {
                if (key == "upscaler_status") return renderer_.upscalerStatus();  // read-only, for the AI Upscaling help
                return enhanced(key).str();
            };
            hooks.setEnhancedValue = [this](const std::string& key, const std::string& v) { setEnhanced(key, Json::string(v)); };
            hooks.userFiles = [this](const std::string& action) { userFiles(action); };
            hooks.userFilesStatus = [this] { return userFilesStatus_; };
            hooks.menuBackdrop = [this](bool on) {
                backdrop_.reset();
                if (!on || !data_) return;
                try {
                    backdrop_ = std::make_unique<VenueRenderer>(renderer_, *data_, "basement",
                                                                VenueRenderer::Band{"metal", "gibson_sg", 1, "", ""});
                    // About a second at 120 BPM poses everyone, then nothing moves again.
                    VenueFrame f;
                    applyVenueLights(*backdrop_);
                    for (int i = 0; i <= 30; ++i) f.ms = float(i) * 33, f.ticks = f.ms * 480 / 500, backdrop_->update(f);
                    // The stand rises from bone_pos_mic; the microphone itself is at the singer's mouth.
                    std::array<float, 3> head;
                    if (!backdrop_->singerMicPos(backdropMic_) || !backdrop_->singerBonePos("bone_head.mesh", head)) backdrop_.reset();
                    else  // the stand leans toward the singer: its microphone is a third of the way to him, at mouth height
                        backdropMic_ = {backdropMic_[0] + (head[0] - backdropMic_[0]) * 0.35f,
                                        backdropMic_[1] + (head[1] - backdropMic_[1]) * 0.35f, head[2] + 3};
                } catch (const std::exception& ex) {
                    SDL_Log("enhanced settings backdrop: %s", ex.what());
                    backdrop_.reset();
                }
            };
            hooks.drawMenuBackdrop = [this](float& x, float& y) { return drawMenuBackdrop(x, y); };
            hooks.playerStat = [this](const std::string& stat, int p) { return playerStat(stat, p); };
            hooks.playerState = [this](int p, SelectHost::PlayerState& out) {
                Player* pl = playerAt(p);
                if (!pl || (screen_ != Screen::Playing && screen_ != Screen::Paused)) return false;
                out.score = pl->displayScore();
                out.multiplier = pl->multiplier();
                out.streak = pl->streak();
                out.starPowerActive = pl->starPowerActive();
                out.rating = pl->rockMeter();
                out.energy = pl->starMeter();
                return true;
            };
            hooks.starMeterShown = [this](int p, float level) {
                if (Player* pl = playerAt(p)) pl->setHudStarLevel(level);
            };
            hooks.trackFrame = [this](float frame) {
                if (track_) track_->setExtendFrame(frame);
                if (track2_ && multi_) track2_->setExtendFrame(frame);
            };
            hooks.trackMessage = [this](const std::string& msg, const std::vector<Value>& a) {
                // track_panel messages go to every track (both highways in a face-off).
                for (TrackRenderer* tr : {track_.get(), multi_ ? track2_.get() : nullptr}) {
                    if (!tr) continue;
                    auto num = [&](size_t k) { return k < a.size() ? int(a[k].number()) : -1; };
                    if (msg == "pop_smasher") tr->popSmasher(num(0));
                    else if (msg == "set_smasher_glowing") tr->setSmasherGlowing(num(0), a.size() > 1 && a[1].truthy());
                    else if (msg == "reset") tr->reset();
                    else if (msg == "explode") {
                        if (SDL_getenv("GD01_UILOG")) SDL_Log("ui: track_panel explode");
                        tr->explode();
                    }
                }
            };
            ui_ = std::make_unique<UiSystem>(renderer_, *data_, hooks);
            applyCheats();
            bool skipBoot = opt_.skipBoot || SDL_getenv("GD01_SKIP_BOOT");
            if (skipBoot && campaign_) {
                // Development path: straight to the main menu with the saved profiles
                // (a first profile "Rocker" when there are none) and autosave on.
                campaign_->load(careerPath());
                if (campaign_->numProfiles() == 0) campaign_->profile().empty = false, campaign_->profile().name = "Rocker";
                for (int s = 0; s < 8; ++s)
                    if (!campaign_->profile(s).empty) {
                        campaign_->setSlot(s);
                        break;
                    }
                if (const char* slot = SDL_getenv("GD01_SLOT")) campaign_->setSlot(std::atoi(slot));  // e.g. 8: The Glitches
                ui_->interp().var("autosave") = Value::integer(1);
                const char* screen = SDL_getenv("GD01_SCREEN");  // dev aid: open this screen, e.g. credits
                ui_->gotoScreen(screen ? screen : "main");
            } else {
                ui_->boot(false);
            }
        } catch (const std::exception& ex) {
            SDL_Log("Original menus unavailable (%s); using the simple menu", ex.what());
            ui_.reset();
        }
        try {
            track_ = std::make_unique<TrackRenderer>(renderer_, *data_);
        } catch (const std::exception& ex) {
            SDL_Log("Original highway unavailable (%s); using the simple one", ex.what());
            track_.reset();
        }
        SDL_Log("Loaded %u songs from %s", unsigned(songs_.size()), data_->description().c_str());
    } catch (const std::exception& ex) {
        message_ = ex.what();
        screen_ = Screen::NeedData;
    }
}

void App::startSong(size_t index, ControllerMode mode) {
    // Career: the selected venue and the player's character / guitar replace the song's quickplay setup.
    SongInfo info = songs_[index];
    if (!opt_.character.empty()) info.character = opt_.character;
    if (!opt_.venueName.empty()) info.venue = opt_.venueName;
    if (!opt_.guitar.empty()) info.guitar = opt_.guitar;
    if (ui_ && ui_->inGame()) {  // the game config (docs/career.md 2.3)
        if (!careerVenue_.empty()) info.venue = careerVenue_;
        if (!careerCharacter_.empty()) info.character = careerCharacter_;
        if (!careerGuitar_.empty()) info.guitar = careerGuitar_;
    }
    try {
        auto song = std::make_unique<Song>(buildSong(parseMidi(data_->read(info.midiPath))));
        auto vgs = std::make_unique<VgsStream>(data_->read(info.audioPath));
        std::vector<Mixer::Track> tracks(vgs->channels());
        for (size_t c = 0; c < tracks.size(); ++c) {
            if (c < info.vols.size()) tracks[c].volume = info.vols[c];
            if (c < info.pans.size()) tracks[c].pan = info.pans[c];
            if (c < info.cores.size()) tracks[c].guitar = info.cores[c] == 1;
        }
        song_ = std::move(song);
        venue_.reset();
        venueEvent_ = 0;
        venueTrigger_ = 0;
        venueMs_ = -1e9f;
        multi_ = ui_ && ui_->inGame() && ui_->multiplayer();
        std::string character2, guitar2;
        if (multi_) ui_->playerSetup(1, character2, guitar2);
        // $cheat_guitar (long_cheats.dta; MyCharSys_v10 passes it as every guitarist's
        // outfit): kGuitarAir (21) plays without a guitar, kGuitarRedOctane (22)
        // the guitar_red_octane controller.
        std::string guitar1 = info.guitar;
        if (int cheat = ui_ ? ui_->interp().var("cheat_guitar").integerValue() : 0; cheat == 21 || cheat == 22) {
            guitar1 = cheat == 21 ? "" : "guitar_red_octane";
            if (!guitar2.empty()) guitar2 = guitar1;
        }
        // $cheat_crowd (crowd.dta finish_loading): monkey (1) or skull (2) heads.
        CrowdRenderer::setCheatCrowd(ui_ ? ui_->interp().var("cheat_crowd").integerValue() : 0);
        if (opt_.venue && !info.venue.empty()) {
            try {
                venue_ = std::make_unique<VenueRenderer>(renderer_, *data_, info.venue,
                                                         VenueRenderer::Band{info.character, guitar1, info.animTempo, character2, guitar2, info.band});
                venue_->setSong(*song_, song_->charts[size_t(difficulty_)]);
                venue_->setStrongerBokeh(enhanced("bokeh").str() == "stronger");
                venue_->setShakyCamera(!enhancedOn("no_shaky_cam"));
                applyVenueLights(*venue_);
            } catch (const std::exception& ex) {
                SDL_Log("Venue %s unavailable: %s", info.venue.c_str(), ex.what());
            }
        }
        // Enhanced Settings cheats: no fail, autoplay (as --no-fail / --autoplay).
        const bool noFail = opt_.noFail || enhancedOn("no_fail"), autoplay = opt_.autoplay || enhancedOn("autoplay") || attractAutoplay_;
        if (ui_ && !attractAutoplay_) ui_->setTrackShowing(true);  // only the attract mode hides it
        attractAutoplay_ = false;
        player_ = std::make_unique<Player>(*song_, static_cast<Difficulty>(difficulty_), rules_, mode, noFail);
        autoplay_ = autoplay ? std::make_unique<Autoplay>(player_->chart()) : nullptr;
        // Face-off: P1 and P2 own their sections' gems; P2's score is scaled by B0 / B1.
        player2_.reset();
        autoplay2_.reset();
        if (track_) track_->setFaceOff(multi_ ? 0 : -1);
        if (track_ && !info.character.empty()) track_->setSurface(*data_, info.character);  // the guitarist's own highway
        if (multi_) {
            int b0 = baseScore(0), b1 = baseScore(1);
            player_->setFaceOff(0, 1);
            player2_ = std::make_unique<Player>(*song_, static_cast<Difficulty>(difficulty_), rules_, mode2_, true);
            player2_->setFaceOff(1, b1 > 0 ? float(b0) / float(b1) : 1.0f);
            if (autoplay) {
                autoplay_ = std::make_unique<Autoplay>(player_->chart(), 0);
                autoplay2_ = std::make_unique<Autoplay>(player2_->chart(), 1);
            }
            activeCursor_ = 0, activeMask_ = -1;
            if (!track2_) track2_ = std::make_unique<TrackRenderer>(renderer_, *data_);
            track2_->setFaceOff(1);
            if (!character2.empty()) track2_->setSurface(*data_, character2);
            track2_->setExtendFrame(0);
            hitting2_ = false;
        }
        for (Player* p : {player_.get(), player2_.get()})
            if (p) p->setForgivingHopos(enhanced("hopo_mode").str() == "forgiving");
        mixer_.stopMusic(0);
        previewing_ = false;
        endMs_ = -1;
        endWallNs_ = 0;
        hitting_ = false;
        bool uiGame = ui_ && ui_->inGame();
        mixer_.play(std::move(vgs), std::move(tracks), uiGame ? kIntroMs : kLeadInMs);
        setupGuitarCopies();
        playingIndex_ = index;
        screen_ = Screen::Playing;
        message_.clear();
        lastSongMs_ = -1e30f;
        if (uiGame) {
            if (track_) track_->setExtendFrame(0);
            // ArenaPanel: the venue's bank, the song's bank, sfx/ingame (slots 0..2).
            crowd_->setVenue(info.venue);
            synth_->loadGameBanks(crowd_->bankPath(), "sfx/song_default");
            mixer_.setSongSpeed(1);
            ui_->resetGameTasks();
            Interp& in = ui_->interp();
            in.var("arena.game_over") = Value::integer(0);
            in.var("arena.game_won") = Value::integer(0);
            in.var("arena.excitement") = Value::integer(VenueScript::Okay);
            arenaExcitement_ = VenueScript::Okay;
            musicStarted_ = false;
            ui_->gameEvent("arena", "init");
            ui_->sendPanel("game", "intro_start");  // game.dta forwards arena intro_start
        }
    } catch (const std::exception& ex) {
        message_ = std::string("Could not load ") + info.name + ": " + ex.what();
        screen_ = Screen::Menu;
    }
}

void App::tutorialLoad(size_t index) {
    if (index >= songs_.size()) return;
    const SongInfo& info = songs_[index];
    try {
        auto song = std::make_unique<Song>(buildSong(parseMidi(data_->read(info.midiPath))));
        tutorialVgs_ = std::make_unique<VgsStream>(data_->read(info.audioPath));
        tutorialTracks_.assign(size_t(tutorialVgs_->channels()), {});
        for (size_t c = 0; c < tutorialTracks_.size(); ++c) {
            if (c < info.vols.size()) tutorialTracks_[c].volume = info.vols[c];
            if (c < info.pans.size()) tutorialTracks_[c].pan = info.pans[c];
            if (c < info.cores.size()) tutorialTracks_[c].guitar = info.cores[c] == 1;
        }
        mixer_.stop();
        song_ = std::move(song);
        difficulty_ = 0;  // tut_loading: kDifficultyEasy
        if (!tutorial_ || !venue_) {
            tutorial_ = true;
            venue_.reset();
            if (opt_.venue) try {
                    // ArenaPanel in a tutorial loads no guitarist or band; one fixed TUTORIAL shot.
                    venue_ = std::make_unique<VenueRenderer>(renderer_, *data_, "basement", VenueRenderer::Band{"", "", info.animTempo, "", ""});
                    venue_->setTutorial();
                } catch (const std::exception& ex) {
                    SDL_Log("Venue basement unavailable: %s", ex.what());
                }
            synth_->clearSlots();
            synth_->loadSlot(0, "tutorial/tutorial");
            crowd_->turnOff();
            crowd_->setEnabled(false);
        }
        if (venue_) venue_->setSong(*song_, song_->charts[size_t(difficulty_)]);
        venueEvent_ = 0;
        venueTrigger_ = 0;
        venueMs_ = -1e9f;
        player_ = std::make_unique<Player>(*song_, Difficulty(difficulty_), rules_, uiMode_, opt_.noFail);
        player_->setTutorialMode(true);
        autoplay_ = opt_.autoplay ? std::make_unique<Autoplay>(player_->chart()) : nullptr;
        playingIndex_ = index;
        endMs_ = -1;
        endWallNs_ = 0;
        hitting_ = false;
        screen_ = Screen::Playing;
        lastSongMs_ = -1e30f;
    } catch (const std::exception& ex) {
        SDL_Log("tutorial: could not load %s: %s", info.id.c_str(), ex.what());
    }
}

// UnmuteBeatMatchAudio per PlayerMatcher (FUN_00257270 -> FUN_00264880):
// one guitar copy per player; in a face-off, beatmatch.dta (audio (unmute
// (multiplayer_scale 0.33) (multiplayer_attenuation 1.5)
// (multiplayer_common_attenuation 1.5))) pans and attenuates them
// (FUN_002662b0 / FUN_002665c0 / FUN_00267d08).
void App::setupGuitarCopies() {
    float scale = 0.33f, atten = 1.5f, common = 1.5f;
    try {
        DataNode bm = parseDtb(data_->read(dtbPath("config/beatmatch.dta")));
        if (const DataNode* audio = bm.find("audio"))
            if (const DataNode* u = audio->find("unmute")) {
                if (const DataNode* n = u->find("multiplayer_scale"); n && n->children.size() > 1) scale = n->floatAt(1);
                if (const DataNode* n = u->find("multiplayer_attenuation"); n && n->children.size() > 1) atten = n->floatAt(1);
                if (const DataNode* n = u->find("multiplayer_common_attenuation"); n && n->children.size() > 1) common = n->floatAt(1);
            }
    } catch (const std::exception& ex) {
        SDL_Log("beatmatch.dta: %s", ex.what());
    }
    auto gain = [](float db) { return std::pow(10.0f, -db / 20.0f); };
    mixer_.setGuitarCopies(multi_ ? 2 : 1, scale, gain(atten));
    commonAtten_ = gain(common);
    guitarCommon_ = false;
    slipTosSeen_ = {};
    if (player_) slipTosSeen_[0] = player_->slipTos();
    if (multi_ && player2_) slipTosSeen_[1] = player2_->slipTos();
}

void App::tutorialStart(bool skipIntro, bool startPaused) {
    if (!tutorialVgs_) return;
    mixer_.stopMusic(0);
    previewing_ = false;
    mixer_.play(std::move(tutorialVgs_), std::move(tutorialTracks_), skipIntro ? 0.0 : kIntroMs);
    setupGuitarCopies();
    if (track_) track_->setExtendFrame(0);
    mixer_.setSongSpeed(1);
    ui_->resetGameTasks();
    Interp& in = ui_->interp();
    in.var("arena.game_over") = Value::integer(0);
    in.var("arena.game_won") = Value::integer(0);
    in.var("arena.excitement") = Value::integer(VenueScript::Okay);
    arenaExcitement_ = VenueScript::Okay;
    musicStarted_ = skipIntro;
    ui_->gameEvent("arena", "init");
    // GamePanel FUN_00105b80: skip_intro sends intro_skip; start_paused pauses the clock.
    ui_->sendPanel("game", skipIntro ? "intro_skip" : "intro_start");
    if (startPaused) mixer_.setPaused(true);
}

// Development aid: GD01_SCREENSHOT=<file.bmp> [GD01_SCREENSHOT_AT=<song ms>]
// saves one frame and quits (works with SDL_VIDEO_DRIVER=offscreen).
// GD01_SCREENSHOT_WALL=<ms>[,<ms>...]: wait that long after start (menus);
// with several times, each shot is saved as <file>_<ms>.bmp and the last one quits.
void App::captureDebugScreenshot() {
    const char* file = SDL_getenv("GD01_SCREENSHOT");
    if (!file) return;
    const char* at = SDL_getenv("GD01_SCREENSHOT_AT");
    if (at && !(screen_ == Screen::Playing && mixer_.songTimeMs() >= std::atof(at))) return;
    static std::vector<double> walls;
    static size_t next = 0;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        if (const char* w = SDL_getenv("GD01_SCREENSHOT_WALL"))
            for (const char* p = w; *p;) {
                walls.push_back(std::atof(p));
                const char* comma = std::strchr(p, ',');
                if (!comma) break;
                p = comma + 1;
            }
    }
    if (next < walls.size() && SDL_GetTicks() < uint64_t(walls[next])) return;
    std::string out = file;
    if (walls.size() > 1) {
        out = out.substr(0, out.rfind('.')) + "_" + std::to_string(int(walls[next])) + ".bmp";
    }
    Image img = renderer_.readPixels();
    if (SDL_Surface* shot = SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_RGBA32, img.rgba.data(), img.width * 4)) {
        SDL_SaveBMP(shot, out.c_str());
        SDL_DestroySurface(shot);
    }
    if (++next >= walls.size()) running_ = false;
}

void App::updateVenue(float t) {
    VenueFrame f;
    f.ms = t;
    f.ticks = static_cast<float>(song_->tempo.msToTick(t));
    float lastTicks = static_cast<float>(song_->tempo.msToTick(venueMs_));
    while (venueEvent_ < song_->events.size() && song_->events[venueEvent_].ms <= t)
        f.events.push_back(song_->events[venueEvent_++].text);
    while (venueTrigger_ < song_->triggers.size() && song_->triggers[venueTrigger_].ms <= t)
        f.events.push_back(song_->triggers[venueTrigger_++].text);
    // FUN_0010f700: sections are announced one bar (1920 ticks) ahead.
    for (const SongEvent& e : song_->events) {
        float at = static_cast<float>(e.tick - 1920);
        if ((e.text == "verse" || e.text == "chorus" || e.text == "solo") && at > lastTicks && at <= f.ticks)
            f.upcoming.push_back(e.text);
    }
    // FUN_002589b8: upcoming_<event> fires 480 ticks (a beat) early.
    for (const SongEvent& e : song_->events) {
        float at = static_cast<float>(e.tick - 480);
        if (at > lastTicks && at <= f.ticks) f.soon.push_back({e.text, static_cast<float>(e.tick) / 480.0f});
    }
    for (size_t j = 0; j < song_->beats.size(); ++j) {
        const BeatLine& b = song_->beats[j];
        if (!(b.ms > venueMs_ && b.ms <= t)) continue;
        if (b.measure) f.downbeat = true;
        int left = 0;  // beats to the next measure line (0 on one)
        if (!b.measure)
            for (size_t k = j + 1; k < song_->beats.size(); ++k) {
                ++left;
                if (song_->beats[k].measure) break;
            }
        f.beatLines.push_back({left, int(j)});
    }
    venueMs_ = t;
    f.excitement = VenueScript::excitementFor(player_->rockMeter(), player_->warningLevel());
    f.hitting = hitting_;
    f.hitting2 = hitting2_;
    // FUN_00258b20: when the first gem of a new player section reaches the now
    // line, active_players changes (camera_multiplayer.dta, the hands).
    if (multi_ && player_) {
        const auto& gems = player_->chart().gems;
        while (activeCursor_ < gems.size() && gems[activeCursor_].ms <= t) {
            int mask = gems[activeCursor_++].players;
            if (mask != activeMask_) {
                activeMask_ = mask;
                ui_->interp().var("active_players") = Value::integer(mask);
                venue_->activePlayers(mask);
            }
        }
    }
    f.starMode = player_ && player_->starPowerActive();
    venue_->update(f);
}

void App::updateTouchLanes() {
    for (int p = 0; p < 2; ++p) {
        Input::TouchLanes l;
        TrackRenderer* tr = p == 0 ? track_.get() : multi_ ? track2_.get() : nullptr;
        l.active = tr && screen_ == Screen::Playing && (!ui_ || (ui_->inGame() && ui_->trackShowing()));
        // The strike line, and 25 units up the highway (its nearer part, where the gems are readable).
        for (int f = 0; f < 5 && l.active; ++f)
            l.active = tr->laneScreen(f, 0, l.nearX[size_t(f)], l.nearY[size_t(f)]) && tr->laneScreen(f, 25, l.farX[size_t(f)], l.farY[size_t(f)]);
        input_.setTouchLanes(p, l);
    }
}

// Development aid: GD01_KEYS="ms:key,..." presses and releases `key` at `ms`
// after start; "+key" only presses it, "-key" only releases it (held keys);
// "p2key" is player 2's pad; "hoverX_Y" / "clickX_Y" move / click the mouse
// at menu units (1280 x 720); "touchX_Y" taps a finger there, "+touchX_Y" /
// "-touchX_Y" put it down / lift it.
void App::injectDebugKeys(std::vector<InputEvent>& out) {
    struct Key {
        uint64_t at;
        Action action;
        int edge;  // 0 press + release, 1 press, -1 release
        int player;
        float mx = -1, my = -1;  // "hoverX_Y" / "clickX_Y": the mouse at menu units (edge 2 = hover, 3 = click;
                                 // "touchX_Y": 4 finger down, 5 up, 6 tap)
    };
    static std::vector<Key> keys, songKeys;  // songKeys: "s<song ms>:<key>", played as a guitar
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        if (const char* k = SDL_getenv("GD01_KEYS")) {
            std::string all = k;
            size_t start = 0;
            while (start < all.size()) {
                size_t comma = all.find(',', start);
                std::string item = all.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                size_t colon = item.find(':');
                if (colon != std::string::npos) {
                    std::string name = item.substr(colon + 1);
                    int edge = 0, player = 0;
                    if (!name.empty() && (name[0] == '+' || name[0] == '-')) edge = name[0] == '+' ? 1 : -1, name = name.substr(1);
                    if (name.rfind("p2", 0) == 0) player = 1, name = name.substr(2);  // player 2's pad
                    static const std::map<std::string, Action> kNames = {
                        {"up", Action::Up}, {"down", Action::Down}, {"left", Action::Left}, {"right", Action::Right},
                        {"confirm", Action::Confirm}, {"back", Action::Back}, {"green", Action::Fret0}, {"red", Action::Fret1},
                        {"yellow", Action::Fret2}, {"blue", Action::Fret3}, {"orange", Action::Fret4}, {"strum", Action::Strum},
                        {"start", Action::Start}, {"starpower", Action::StarPower}};
                    if (auto it = kNames.find(name); it != kNames.end() && item[0] == 's')
                        songKeys.push_back({uint64_t(std::atof(item.substr(1, colon - 1).c_str())), it->second, edge, player});
                    else if (it != kNames.end())
                        keys.push_back({uint64_t(std::atof(item.substr(0, colon).c_str())), it->second, edge, player});
                    else if (name.rfind("hover", 0) == 0 || name.rfind("click", 0) == 0) {
                        Key k{uint64_t(std::atof(item.substr(0, colon).c_str())), Action::Confirm, name[0] == 'h' ? 2 : 3, 0};
                        std::sscanf(name.c_str() + 5, "%f_%f", &k.mx, &k.my);
                        keys.push_back(k);
                    } else if (name.rfind("touch", 0) == 0) {  // "touchX_Y" taps, "+touchX_Y" / "-touchX_Y" hold / lift
                        Key k{uint64_t(std::atof(item.substr(0, colon).c_str())), Action::Confirm, edge == 1 ? 4 : edge == -1 ? 5 : 6, 0};
                        std::sscanf(name.c_str() + 5, "%f_%f", &k.mx, &k.my);
                        keys.push_back(k);
                    }
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
    }
    if (screen_ == Screen::Playing && !mixer_.paused()) {
        // Each at its exact song time (the event's timestamp is moved back to it).
        double songNow = mixer_.songTimeMs();
        uint64_t nowNs = SDL_GetTicksNS();
        while (!songKeys.empty() && double(songKeys.front().at) <= songNow) {
            Key key = songKeys.front();
            songKeys.erase(songKeys.begin());
            uint64_t ts = nowNs - uint64_t((songNow - double(key.at)) * 1e6);
            if (key.edge >= 0) out.push_back({key.action, true, ts, ControllerMode::Guitar, key.player});
            if (key.edge <= 0) out.push_back({key.action, false, ts + 1000000, ControllerMode::Guitar, key.player});
        }
    }
    uint64_t now = SDL_GetTicks();
    while (!keys.empty() && keys.front().at <= now) {
        Key key = keys.front();
        keys.erase(keys.begin());
        if (key.edge >= 4) {  // a synthetic finger at menu units (the inverse of windowToMenu)
            float x0, y0, x1, y1;
            renderer_.windowToMenu(0, 0, x0, y0);
            renderer_.windowToMenu(1, 1, x1, y1);
            int w = 1, h = 1;
            SDL_GetWindowSize(window_, &w, &h);
            SDL_Event ev{};
            ev.type = key.edge == 5 ? SDL_EVENT_FINGER_UP : SDL_EVENT_FINGER_DOWN;
            ev.tfinger.timestamp = SDL_GetTicksNS();
            ev.tfinger.touchID = 1;
            ev.tfinger.fingerID = SDL_FingerID(1 + int(key.mx) * 1000 + int(key.my));
            ev.tfinger.x = (key.mx - x0) / (x1 - x0) / float(w);
            ev.tfinger.y = (key.my - y0) / (y1 - y0) / float(h);
            input_.handle(ev, out);
            if (key.edge == 6) {  // a tap lifts 80 ms later
                Key up = key;
                up.edge = 5, up.at = now + 80;
                keys.insert(std::upper_bound(keys.begin(), keys.end(), up, [](const Key& a, const Key& b) { return a.at < b.at; }), up);
            }
            continue;
        }
        if (key.edge >= 2) {
            if (ui_) ui_->mouse(key.mx, key.my, key.edge == 3);
            continue;
        }
        if (key.edge >= 0) out.push_back({key.action, true, SDL_GetTicksNS(), ControllerMode::Joypad, key.player});
        if (key.edge <= 0) out.push_back({key.action, false, SDL_GetTicksNS(), ControllerMode::Joypad, key.player});
    }
}

void App::startMovie(const std::string& file) {
    std::string path = "videos/" + file;
    if (!data_ || !data_->exists(path)) {
        SDL_Log("movie %s: not on the disc", path.c_str());
        return;
    }
    auto m = std::make_unique<Movie>();
    m->path = path;
    Movie* mp = m.get();
    try {
        m->pss = std::make_unique<PssMovie>([this, mp](uint8_t* dst, size_t n) {
            size_t got = data_->readRange(mp->path, mp->pos, dst, n);
            mp->pos += got;
            return got;
        });
        m->pss->prefetchAudio(24000);  // ~0.5 s queued before playing (0x18000 bytes on the PS2)
    } catch (const std::exception& ex) {
        SDL_Log("movie %s: %s", path.c_str(), ex.what());
        return;
    }
    SDL_AudioSpec spec{SDL_AUDIO_S16, PssMovie::kAudioChannels, PssMovie::kAudioRate};
    m->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (m->audio) SDL_ResumeAudioStreamDevice(m->audio);
    mixer_.stopMusic(0);  // the movie plays alone
    m->startNs = SDL_GetTicksNS();
    movie_ = std::move(m);
}

void App::movieFrame() {
    Movie& m = *movie_;
    std::vector<int16_t> pcm;
    if (m.pss->takeAudio(pcm) && m.audio) SDL_PutAudioStreamData(m.audio, pcm.data(), int(pcm.size() * sizeof(int16_t)));
    double elapsed = double(SDL_GetTicksNS() - m.startNs) / 1e9;
    uint64_t target = uint64_t(elapsed * PssMovie::kFrameRate);
    bool changed = false;
    try {
        while (m.next <= target) {
            const Mpeg2Frame* f = m.pss->nextFrame();
            if (!f) {
                endMovie();
                return;
            }
            ++m.next;
            if (m.next <= target) continue;  // late: skip the conversion
            m.frame.width = f->width, m.frame.height = f->height;
            m.frame.rgba.resize(size_t(f->width) * size_t(f->height) * 4);
            mpeg2ToRgba(*f, m.frame.rgba.data(), size_t(f->width) * 4, !f->progressive);
            changed = true;
        }
    } catch (const std::exception& ex) {
        SDL_Log("movie %s: %s", m.path.c_str(), ex.what());
        endMovie();
        return;
    }
    if (m.frame.width == 0) return;
    if (!m.texture) m.texture = renderer_.createTexture(m.frame);
    else if (changed) renderer_.updateTexture(m.texture, m.frame);
    // The PS2 picture is 4:3: pillarboxed in the window.
    float h = kScreenH, w = h * 4.0f / 3.0f;
    renderer_.image(m.texture, (kScreenW - w) * 0.5f, 0, w, h, {255, 255, 255, 255}, Blend::Opaque);
}

// Enhanced Settings "FPS Counter": the frame rate in green, bottom left.
void App::drawFpsCounter() {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d FPS", int(std::lround(fps_)));
    if (ui_) {
        ui_->drawText("impactor.font", buf, 25, 697, 24, {0, 0, 0, 0.6f}, 0x41);  // shadow
        ui_->drawText("impactor.font", buf, 23, 695, 24, {0.2f, 1, 0.3f, 1}, 0x41);
    } else {
        renderer_.text(23, 672, 1.5f, {50, 255, 80, 255}, buf);
    }
}

void App::endMovie() {
    if (!movie_) return;
    if (movie_->audio) SDL_DestroyAudioStream(movie_->audio);
    movie_.reset();
    uiTicks_ = SDL_GetTicksNS();  // the UI clock was frozen
}

// song_preview: loop the song's (preview start end) window with the
// song's mix, fading over 1 s (config/synth.dta song_select fade_rate 1.0).
void App::songPreview(const std::string& id) {
    auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return s.id == id; });
    if (id.empty() || it == songs_.end()) {
        previewing_ = false;
        mixer_.stopMusic(1000);
        return;
    }
    try {
        auto vgs = std::make_unique<VgsStream>(data_->read(it->audioPath));
        std::vector<Mixer::Track> tracks(vgs->channels());
        for (size_t c = 0; c < tracks.size(); ++c) {
            if (c < it->vols.size()) tracks[c].volume = it->vols[c];
            if (c < it->pans.size()) tracks[c].pan = it->pans[c];
        }
        mixer_.playMusic(std::move(vgs), std::move(tracks), it->previewStartMs, it->previewEndMs, true, 1000, 1.0f);
        previewing_ = true;
    } catch (const std::exception& ex) {
        SDL_Log("preview %s: %s", id.c_str(), ex.what());
    }
}

// Metamusic (FUN_0020dcd8 / FUN_0020e588): when the meta panel loads it
// picks one of config/synth.dta (metamusic (volume 0.6) (music meta_loop1a
// ... meta_loop1g)) at random and plays it looping; meta_music_stop /
// meta_music_start (and the meta panel's Exit) stop and restart that loop.
void App::pollMenuMusic() {
    // MetaPanel_v10 / FUN_0014ca70: while the menu music plays, the meta_people
    // sequence every U(10, 20) s.
    if (menuMusicOn_ && screen_ == Screen::Menu && synth_) {
        uint64_t now = SDL_GetTicksNS();
        if (nextPeopleNs_ == 0) nextPeopleNs_ = now + uint64_t(std::uniform_real_distribution<float>(10, 20)(musicRng_) * 1e9f);
        if (now >= nextPeopleNs_) {
            synth_->playSequence("meta_people");
            nextPeopleNs_ = now + uint64_t(std::uniform_real_distribution<float>(10, 20)(musicRng_) * 1e9f);
        }
    }
    if (!menuMusicOn_ || previewing_ || screen_ != Screen::Menu || mixer_.musicPlaying()) return;
    static const char* const kLoops[] = {"meta_loop1a", "meta_loop1b", "meta_loop1c", "meta_loop1d", "meta_loop1e", "meta_loop1g"};
    if (menuLoop_.empty()) menuLoop_ = kLoops[std::uniform_int_distribution<int>(0, 5)(musicRng_)];
    std::string path = "sfx/streams/" + menuLoop_ + ".vgs";
    try {
        auto vgs = std::make_unique<VgsStream>(data_->read(path));
        std::vector<Mixer::Track> tracks(vgs->channels());
        for (size_t c = 0; c < tracks.size(); ++c) tracks[c].pan = tracks.size() == 2 ? (c == 0 ? -1.0f : 1.0f) : 0.0f;
        mixer_.playMusic(std::move(vgs), std::move(tracks), 0, 0, true, 1000, 0.6f);
    } catch (const std::exception& ex) {
        SDL_Log("menu music %s: %s", path.c_str(), ex.what());
        menuMusicOn_ = false;
    }
}

// Leaving a song: back to the song list of the original menus.
// The song's timeline as arena script events (arena_game.dta): extend_track
// at -2000 ms (game.dta track_extend_ms), beat / downbeat, music_start when
// the beat tracker reaches measure 2 (BeatMatch_v1), and the excitement
// level with its transition messages.
void App::arenaEvents(float t, float lastT) {
    if (lastT < kTrackExtendMs && kTrackExtendMs <= t) ui_->gameEvent("arena", "extend_track");
    for (const BeatLine& b : song_->beats) {
        if (b.ms <= lastT) continue;
        if (b.ms > t) break;
        ui_->gameEvent("arena", "beat");
        if (b.measure) ui_->gameEvent("arena", "downbeat");
    }
    if (!musicStarted_ && t > 0 && song_->tempo.tickToMeasure(song_->tempo.msToTick(t)) >= 2) {
        musicStarted_ = true;
        ui_->gameEvent("arena", "music_start");
    }
    if (t < 0 || endMs_ >= 0) return;
    int level = VenueScript::excitementFor(player_->rockMeter(), player_->warningLevel());
    if (level == arenaExcitement_) return;
    int old = arenaExcitement_;
    arenaExcitement_ = level;
    ui_->interp().var("arena.excitement") = Value::integer(level);
    ui_->gameEvent("arena", "excitement");
    const char* transition = nullptr;
    switch (level) {
    case VenueScript::Boot: transition = "excitement_boot"; break;
    case VenueScript::Bad: transition = level > old ? "excitement_bad_rising" : "excitement_bad_falling"; break;
    // venue.dta: Okay is "rising" only from Bad (Boot -> Okay plays the falling reaction).
    case VenueScript::Okay: transition = old == VenueScript::Bad ? "excitement_okay_rising" : "excitement_okay_falling"; break;
    case VenueScript::Great: transition = level > old ? "excitement_great" : nullptr; break;
    default: break;
    }
    if (transition) ui_->gameEvent("arena", transition);
}

// The player's beat-match and star power events as the scripts' handlers
// (config/beatmatch.dta player_matcher_handlers, config/player.dta star_power_handlers).
void App::playerEvent(const PlayerEvent& ev, int player) {
    using K = PlayerEvent::Kind;
    const Value p0 = Value::integer(player);
    switch (ev.kind) {
    case K::Hit: {
        const Gem& g = playerAt(player)->chart().gems[size_t(ev.gem)];
        ui_->gameEvent("player_matcher_handlers", "hit",
                       {Value::integer(std::countr_zero(unsigned(g.frets))), Value::integer(g.phrase >= 0), Value::integer(g.hopo),
                        Value::integer(0), p0});
        break;
    }
    case K::Miss: ui_->gameEvent("player_matcher_handlers", "miss", {p0}); break;
    case K::Pass: ui_->gameEvent("player_matcher_handlers", "pass", {}); break;
    case K::Hopo: ui_->gameEvent("player_matcher_handlers", "hopo", {}); break;
    case K::SwingAtHopo: ui_->gameEvent("player_matcher_handlers", "swingAtHopo", {}); break;
    case K::HeldNoteReleased: ui_->gameEvent("player_handlers", "held_note_released", {Value::real(ev.value)}); break;
    case K::WhammyStart: ui_->gameEvent("player_handlers", "whammy_start", {}); break;
    case K::WhammyEnd: ui_->gameEvent("player_handlers", "whammy_end", {}); break;
    case K::StarGemHit: ui_->gameEvent("star_power_handlers", "star_gem_hit", {}); break;
    case K::PhraseCaptured: ui_->gameEvent("star_power_handlers", "phrase_captured", {p0}); break;
    case K::PhraseMissed: ui_->gameEvent("star_power_handlers", "phrase_miss", {Value::integer(ev.gem)}); break;
    case K::StarPowerStart:
        if (SDL_getenv("GD01_UILOG")) SDL_Log("star power start at %.0f ms", songMs_);
        ui_->gameEvent("star_power_handlers", "start_using", {p0});
        break;
    case K::StarPowerEnd: ui_->gameEvent("star_power_handlers", "stop_using", {p0}); break;
    default: break;
    }
}

// The song's base max score B (MultiplayerAnalyzer @0x131bd0): per gem
// (50 + tail 25 * ticks / 480, truncated, only for sustains >= 161 ticks) * frets.
int App::baseScore(int player) const {
    if (!player_) return 0;
    int base = 0;
    for (const Gem& g : player_->chart().gems) {
        if (player >= 0 && !g.ownedBy(player)) continue;
        int tail = !g.noSustain && g.durationTicks >= 161 ? 25 * g.durationTicks / 480 : 0;
        base += (50 + tail) * g.fretCount();
    }
    return base;
}

// player0::player stats for the result screens (Player_v1).
int App::playerStat(const std::string& stat, int p) const {
    Player* pl = playerAt(p);
    if (!pl) return 0;
    const Player* player = pl;  // P2 in a face-off: owned gems, handicapped score
    int total = player->ownedCount();
    if (stat == "score") return player->displayScore();
    if (stat == "longest_streak") return player->longestStreak();
    // Player_v1: (int)(min(1, now / end) * 100), end = the EVENTS "end" time.
    if (stat == "percent_complete")
        return song_ && song_->endMs > 0 ? int(std::min(1.0f, std::max(0.0f, songMs_) / song_->endMs) * 100) : 0;
    if (stat == "percent_hit") return total ? int(float(player->notesHit()) / float(total) * 100) : 0;
    if (stat == "star_power_ready") return player->starPowerReady();
    if (stat == "in_star_mode") return player->starPowerActive();
    if (stat == "num_stars") {
        // FUN_0010b5e0: 5 stars at score >= 3.0 B, 4 at >= 2.0 B, else 3
        // (config/scoring.dta star_ratings). B = the song's base max score
        // (MultiplayerAnalyzer @0x131bd0): per gem (50 + tail 25 * ticks / 480,
        // truncated, only for sustains of >= 161 ticks) * frets.
        int base = baseScore(multi_ ? p : -1);
        if (base <= 0) return 3;
        int s = player->displayScore();
        return s >= 3 * base ? 5 : s >= 2 * base ? 4 : 3;
    }
    return 0;
}

void App::backToMenu() {
    screen_ = Screen::Menu;
    if (ui_) ui_->gotoScreen("qp_selsong");
}

// Menu input as PS2 pad buttons (docs/ui_system.md 4): a guitar selects with
// the green fret (R2) and backs with the red fret (Circle); a joypad uses
// Cross and Triangle.
void App::uiButton(const InputEvent& e) {
    bool guitar = e.mode == ControllerMode::Guitar;
    // The main menu's GAMEPADS help item: the blue fret opens the Gamepads screen.
    if (e.action == Action::Fret3 && e.down && ui_->currentScreen() == "main") return ui_->gotoScreen("gamepads");
    if (e.player == 1) mode2_ = e.mode;
    else uiMode_ = e.mode;
    int b = -1;
    switch (e.action) {
    case Action::Up: b = UiSystem::Up; break;
    case Action::Down: b = UiSystem::Down; break;
    case Action::Left: b = UiSystem::Left; break;
    case Action::Right: b = UiSystem::Right; break;
    case Action::Confirm: b = guitar ? UiSystem::R2 : UiSystem::Cross; break;
    case Action::Fret0: b = UiSystem::R2; guitar = true; break;
    case Action::Back: b = guitar ? UiSystem::Circle : UiSystem::Triangle; break;
    case Action::Fret1: b = UiSystem::Circle; guitar = true; break;
    case Action::Start: b = UiSystem::Start; break;
    default: break;
    }
    if (b < 0) return;
    if (e.down) ui_->buttonDown(b, guitar, e.player);
    else ui_->buttonUp(b, e.player);
}

void App::setEnhanced(const std::string& key, Json value) {
    if (auto x = enhancedExclusive().find(key); x != enhancedExclusive().end() && value.truthy()) enhanced_.set(x->second, Json::boolean(false));
    enhanced_.set(key, std::move(value));
    saveEnhanced();
    applyEnhanced();  // every setting takes effect at once
    if (key.rfind("cheat_", 0) == 0) applyCheats(key);
}

// The original cheats (config/long_cheats.dta) from Enhanced Settings: the
// same script variables the button codes on the main menu set. Applied for
// the cheat changed (`only`), or all of them at start and on Load User
// Settings, so a cheat entered with its button code stays as it is.
void App::applyCheats(const std::string& only) {
    auto is = [&](const char* k) { return only.empty() || only == k; };
    if (ui_) {
        Interp& in = ui_->interp();
        // $cheat_guitar: every guitarist's guitar becomes outfit kGuitarAir (21: none) or kGuitarRedOctane (22).
        if (is("cheat_air_guitar") || is("cheat_gh_guitar"))
            in.var("cheat_guitar") = Value::integer(enhancedOn("cheat_air_guitar") ? 21 : enhancedOn("cheat_gh_guitar") ? 22 : 0);
        if (is("cheat_full_meter")) in.var("cheat_meter") = Value::integer(enhancedOn("cheat_full_meter") ? 1 : 0);
        if (is("cheat_monkey_crowd") || is("cheat_skull_crowd"))
            in.var("cheat_crowd") = Value::integer(enhancedOn("cheat_monkey_crowd") ? 1 : enhancedOn("cheat_skull_crowd") ? 2 : 0);
        if (is("cheat_no_venue")) ui_->setArenaShowing(!enhancedOn("cheat_no_venue"));
    }
    // campaign set_all_access; never saved (the career save has no such field).
    if (campaign_ && is("cheat_unlock_all")) campaign_->allAccess = enhancedOn("cheat_unlock_all");
}

// A three-quarter close-up from the audience's side (the stage faces -Y), the
// microphone placed in the frame's upper right, clear of the settings boxes.
bool App::drawMenuBackdrop(float& focusX, float& focusY) {
    if (!backdrop_) return false;
    const auto& m = backdropMic_;
    // Wide enough to show the band's lights, shadows and the fog around the singer.
    const std::array<float, 3> eye{m[0] - 32, m[1] - 80, m[2] + 4}, target{m[0] + 3, m[1], m[2] - 6};
    std::array<float, 3> f{target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& v : f) v /= fl;
    std::array<float, 3> r{f[1], -f[0], 0};
    float rl = std::hypot(r[0], r[1]);
    r[0] /= rl, r[1] /= rl;
    std::array<float, 3> u{r[1] * f[2], -r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    Xfm cam;
    cam.rot = {r[0], r[1], r[2], f[0], f[1], f[2], u[0], u[1], u[2]};
    cam.pos = eye;
    constexpr float kFocusX = 0.75f, kFocusY = 0.3f;  // of the 16:9 frame, from the top left
    float aspect = renderer_.beginWide(16.0f / 9.0f);
    Mat4 vp = Mat4::perspective(0.7f, aspect, 5.0f, 5000.0f) * Mat4::lookAt(eye, target);
    // Shift in clip space so the target lands at the focus point.
    Mat4 shift;
    shift.at(0, 3) = (kFocusX * 2 - 1), shift.at(1, 3) = (1 - kFocusY * 2);
    vp = shift * vp;
    backdrop_->setAspect(aspect);
    backdrop_->setStillView(vp, cam);
    // The same Enhanced Settings effects as in a song, so every change shows here.
    const bool post = renderer_.beginScene();
    renderer_.clearDepth();
    backdrop_->draw();
    if (enhanced("bokeh").str() != "none") {
        // As the shots' focus (VenueCamera::setFocusScale): 2x the subject distance, 1.3x when stronger.
        bool stronger = enhanced("bokeh").str() == "stronger";
        const float k = stronger ? 1.3f : 2.0f;
        const std::array<float, 3> p{eye[0] + (m[0] - eye[0]) * k, eye[1] + (m[1] - eye[1]) * k, eye[2] + (m[2] - eye[2]) * k};
        const auto& c = vp.m;
        float cz = c[2] * p[0] + c[6] * p[1] + c[10] * p[2] + c[14], cw = c[3] * p[0] + c[7] * p[1] + c[11] * p[2] + c[15];
        float depth = cw > 1e-4f ? std::clamp(cz / cw * 0.5f + 0.5f, 0.0f, 1.0f) : 0;
        renderer_.depthBlur(depth, (stronger ? 4.8f : 3.0f) * renderer_.viewportHeight() / 448.0f);
    }
    if (post) {
        PostFrame pf;
        pf.viewProj = vp.m;
        pf.hasCamera = true;
        if (!backdrop_->stageFloorZ(pf.floorZ)) pf.floorZ = 0;
        pf.timeSec = float(SDL_GetTicksNS() / 1000000) / 1000.0f;
        postFrameExtras(pf, *backdrop_, false);
        renderer_.endScene(pf);
    }
    renderer_.endWide();
    renderer_.clearDepth();
    focusX = kFocusX * kScreenW, focusY = kFocusY * kScreenH;
    return true;
}

// The venue's part of the post-processing frame: Super Fog's light cones and
// Blur on Star Power's fade (in over 0.25 s while any player's Star Power is
// on, out over 0.5 s after it ends; GD01_STARBLUR=1 forces it, a development aid).
void App::postFrameExtras(PostFrame& pf, VenueRenderer& venue, bool song) {
    if (renderer_.post().fog >= 3)
        for (const auto& l : venue.beamLights()) pf.lights.push_back({{l[0], l[1], l[2]}, {l[3], l[4], l[5]}, {l[6], l[7], l[8]}});
    const float now = pf.timeSec, dt = starBlurTime_ >= 0 ? std::clamp(now - starBlurTime_, 0.0f, 0.1f) : 0.0f;
    starBlurTime_ = now;
    bool on = false;
    if (song && renderer_.post().starBlur)
        for (int p = 0; p < 2; ++p)
            if (Player* pl = playerAt(p); pl && pl->starPowerActive()) on = true;
    if (const char* force = SDL_getenv("GD01_STARBLUR"); force && *force == '1') on = true, starBlurFade_ = 1;
    starBlurFade_ = on ? std::min(1.0f, starBlurFade_ + dt / 0.25f) : std::max(0.0f, starBlurFade_ - dt / 0.5f);
    pf.starBlur = starBlurFade_;
}

void App::saveEnhanced() {
    std::ofstream out(enhancedPath());
    out << enhanced_.dump(2) << "\n";
    if (!out) SDL_Log("enhanced settings: could not save %s", enhancedPath().c_str());
}

namespace {
constexpr const char* kSettingsFile = "GD01-user-settings.json";
constexpr const char* kProgressFile = "GD01-user-progress.json";

struct DialogRequest {
    std::mutex* mutex;
    std::vector<std::pair<std::string, std::string>>* results;
    std::string action;
};

// SDL_DialogFileCallback: may run on another thread; queues the answer.
void SDLCALL dialogDone(void* user, const char* const* files, int) {
    std::unique_ptr<DialogRequest> req(static_cast<DialogRequest*>(user));
    std::lock_guard<std::mutex> lock(*req->mutex);
    req->results->push_back({req->action, files && files[0] ? files[0] : ""});
}
}  // namespace

void App::userFiles(const std::string& action) {
    userFilesStatus_.clear();
    if (action == "steam_deck") {  // Apply Steam Deck Settings: the handheld profile, once; every option stays editable
        for (auto& [key, value] : profileValues(true)) enhanced_.set(key, value);
        saveEnhanced();
        applyEnhanced();
        userFilesStatus_ = "Steam Deck settings applied";
        return;
    }
    // Development aid: GD01_DIALOG=<path> answers the dialog at once (headless tests).
    if (const char* forced = SDL_getenv("GD01_DIALOG")) {
        std::lock_guard<std::mutex> lock(dialogMutex_);
        dialogResults_.push_back({action, forced});
        return;
    }
    auto* req = new DialogRequest{&dialogMutex_, &dialogResults_, action};
    if (action == "export") {
        SDL_ShowOpenFolderDialog(dialogDone, req, window_, nullptr, false);
    } else if (action == "locate_iso") {
        // The first screen: pick the disc image instead of dropping it on the window.
        static const SDL_DialogFileFilter kIso[] = {{"Guitar Hero (PS2) disc image", "iso;ISO"}};
        SDL_ShowOpenFileDialog(dialogDone, req, window_, kIso, 1, nullptr, false);
    } else {
        static const SDL_DialogFileFilter kJson[] = {{"GD01 settings", "json"}};
        SDL_ShowOpenFileDialog(dialogDone, req, window_, kJson, 1, nullptr, false);
    }
}

void App::pollUserFiles() {
    std::vector<std::pair<std::string, std::string>> done;
    {
        std::lock_guard<std::mutex> lock(dialogMutex_);
        done.swap(dialogResults_);
    }
    for (const auto& [action, path] : done) {
        if (path.empty()) continue;  // cancelled
        if (action == "locate_iso") tryLoadData(path);
        else if (action == "export") exportUserFiles(path);
        else if (action == "load_settings") userFilesStatus_ = importSettings(path) ? "Settings loaded" : "Could not load that settings file";
        else if (action == "load_progress") userFilesStatus_ = importProgress(path) ? "Progress loaded" : "Could not load that progress file";
    }
}

// GD01-user-settings.json: every Enhanced Setting; GD01-user-progress.json:
// the career save (bands, progress, high scores).
void App::exportUserFiles(const std::string& dir) {
    std::string base = dir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
    Json all = Json::object();
    for (const auto& [key, def] : enhancedDefaults()) all.set(key, enhanced_.has(key) ? enhanced_[key] : def);
    std::ofstream out(base + kSettingsFile);
    out << all.dump(2) << "\n";
    bool ok = bool(out);
    out.close();
    ok = campaign_ && campaign_->save(base + kProgressFile) && ok;
    userFilesStatus_ = ok ? "Exported to " + dir : "Could not write to " + dir;
    SDL_Log("enhanced settings: %s", userFilesStatus_.c_str());
}

// Each known setting of the right type and value is taken; anything odd or
// missing gets its default. Applied at once.
bool App::importSettings(const std::string& file) {
    std::ifstream in(file);
    if (!in) return false;
    Json j;
    try {
        j = Json::parse(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    } catch (const std::exception& ex) {
        SDL_Log("load user settings %s: %s", file.c_str(), ex.what());
        return false;
    }
    if (j.type != Json::Type::Object) return false;
    Json next = Json::object();
    for (const auto& [key, def] : enhancedDefaults()) {
        if (!j.has(key)) continue;
        const Json& v = j[key];
        bool ok = v.type == def.type;
        if (ok && def.type == Json::Type::String) {
            auto c = enhancedChoices().find(key);
            auto n = enhancedRanges().find(key);
            if (n != enhancedRanges().end()) {
                char* end = nullptr;
                long ms = std::strtol(v.s.c_str(), &end, 10);
                ok = !v.s.empty() && end && *end == 0 && ms >= n->second.first && ms <= n->second.second;
            } else {
                ok = c != enhancedChoices().end() && c->second.count(v.s);
            }
        }
        if (ok) next.set(key, v);
        else SDL_Log("load user settings: %s is not valid, keeping the default", key.c_str());
    }
    for (const auto& [a, b] : enhancedExclusive())  // both of a pair on: neither
        if (next.has(a) && next.has(b) && next[a].truthy() && next[b].truthy()) next.set(a, Json::boolean(false)), next.set(b, Json::boolean(false));
    enhanced_ = std::move(next);
    saveEnhanced();
    applyEnhanced();
    applyCheats();
    return true;
}

// The progress replaces the career save (checked property by property, see
// Campaign::load) and is saved at once; the band in play stays if it exists.
bool App::importProgress(const std::string& file) {
    if (!campaign_) return false;
    int slot = campaign_->slot();
    if (!campaign_->load(file)) return false;
    if (campaign_->profile(slot).empty)
        for (int s = 0; s < 8; ++s)
            if (!campaign_->profile(s).empty) {
                slot = s;
                break;
            }
    campaign_->setSlot(slot);
    if (!campaign_->save(careerPath())) SDL_Log("career: could not save %s", careerPath().c_str());
    return true;
}

// Puts the Enhanced Settings in effect (at start and on every change).
// Enhanced Settings for a venue's lighting (at its creation and on every change).
void App::applyVenueLights(VenueRenderer& venue) {
    venue.setBandShadows(enhancedOn("shadows_all"));
    const std::string more = enhanced("more_lights").str();
    venue.setMoreLights(more == "strong" ? 2 : more == "subtle" ? 1 : 0);
    venue.setLightDust(enhancedOn("light_dust"));
}

void App::applyEnhanced() {
    setParticlesEnabled(enhancedOn("particles"));
    renderer_.setUiEnhance(enhancedOn("sharp_ui"));
    renderer_.setInternalHeight(std::clamp(enhancedInt("internal_res"), 0, 4320));
    renderer_.setFrameCap(std::clamp(enhancedInt("max_fps"), 30, 1000));
    // Post-processing of the 3D game frame (render/post_fx.h). FSR 3.1 and
    // DLSS 4 run in Vulkan next to the GL renderer (render/vk_upscaler.h) and
    // fall back to FSR 1; DLSS 5 is not available. Upscalers output at the
    // screen's resolution (Renderer::frameSize) from the vendors' scales.
    {
        PostSettings post;
        const std::string aa = enhanced("antialiasing").str(), up = enhanced("ai_upscaling").str(), mode = enhanced("ai_upscaling_mode").str();
        post.aa = aa == "fxaa" ? PostSettings::AA::Fxaa : aa == "ssaa" ? PostSettings::AA::Ssaa : aa == "taa" ? PostSettings::AA::Taa : PostSettings::AA::None;
        post.upscaler = up == "fsr1"    ? PostSettings::Upscaler::Fsr1
                        : up == "fsr31" ? PostSettings::Upscaler::Fsr31
                        : up == "dlss4" ? PostSettings::Upscaler::Dlss4
                                        : PostSettings::Upscaler::None;
        post.quality = mode == "performance" ? 0 : mode == "balanced" ? 1 : 2;
        post.fsrScale = mode == "performance" ? 1 / 2.0f : mode == "balanced" ? 1 / 1.7f : 1 / 1.5f;
        post.bloom = enhanced("bloom").str() == "high" ? 3 : enhanced("bloom").str() == "medium" ? 2 : enhanced("bloom").str() == "low" ? 1 : 0;
        post.cinematic = enhancedOn("cinematic_color");
        const std::string fog = enhanced("ground_fog").str(), grade = enhanced("color_grade").str();
        post.fog = fog == "super" ? 3 : fog == "full" ? 2 : fog == "low" ? 1 : 0;
        post.grade = grade == "warm" ? 1 : grade == "cold" ? 2 : grade == "vintage" ? 3 : grade == "noir" ? 4 : grade == "neon" ? 5 : 0;
        post.grain = enhanced("film_grain").str() == "strong" ? 2 : enhanced("film_grain").str() == "light" ? 1 : 0;
        post.chromatic = enhancedOn("chromatic");
        post.streaks = enhancedOn("light_streaks");
        post.ssao = enhancedOn("ssao");
        post.sharpen = enhanced("sharpening").str() == "high" ? 2 : enhanced("sharpening").str() == "low" ? 1 : 0;
        const std::string blur = enhanced("star_blur").str();
        post.starBlur = blur == "high" ? 3 : blur == "medium" ? 2 : blur == "light" ? 1 : 0;
        renderer_.setPost(post);
    }
    // Controls: touch (SDL's touch-made mouse events only while it is off), XInput joypads, Rock Band guitars.
    input_.setTouch(enhancedOn("touch"));
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, enhancedOn("touch") ? "0" : "1");
    input_.setGamepadPlay(enhancedOn("xinput"));
    input_.setRockBand(enhancedOn("rockband"));
    if (venue_) {
        venue_->setStrongerBokeh(enhanced("bokeh").str() == "stronger");
        venue_->setShakyCamera(!enhancedOn("no_shaky_cam"));
        applyVenueLights(*venue_);
    }
    if (backdrop_) applyVenueLights(*backdrop_);  // Enhanced Settings shows the change at once
    // Screen mode: windowed, fullscreen (the display's mode at the desktop
    // size) or borderless (fullscreen at the desktop's own mode).
    std::string mode = enhanced("screen_mode").str();
#ifdef __ANDROID__
    if (mode == "windowed") mode = "borderless";  // always full screen (immersive) on phones
#endif
    if (window_ && mode != screenModeApplied_) {
        bool ok = true;
        if (mode == "windowed") {
            ok = SDL_SetWindowFullscreen(window_, false);
        } else {
            const SDL_DisplayMode* fullMode = nullptr;
            SDL_DisplayMode closest;
            if (mode == "fullscreen") {
                SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
                if (const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(display);
                    desk && SDL_GetClosestFullscreenDisplayMode(display, desk->w, desk->h, desk->refresh_rate, true, &closest))
                    fullMode = &closest;
            }
            ok = SDL_SetWindowFullscreenMode(window_, fullMode) && SDL_SetWindowFullscreen(window_, true);
        }
        if (!ok) SDL_Log("enhanced settings: screen mode %s: %s", mode.c_str(), SDL_GetError());
        screenModeApplied_ = mode;
        renderer_.setFrameCap(std::clamp(enhancedInt("max_fps"), 30, 1000));  // the new display mode's refresh rate
    }
}

void App::endSong() {
    mixer_.stop();
    screen_ = Screen::Results;
}

void App::onAction(const InputEvent& e) {
    // Escape three times in a row on the main menu quits.
    if (e.down) {
        if (e.action == Action::Back && ui_ && screen_ == Screen::Menu && ui_->currentScreen() == "main") {
            if (++escapes_ >= 3) running_ = false;
        } else {
            escapes_ = 0;
        }
    }
    if (isFret(e.action)) heldFrets_ = e.down ? heldFrets_ | (1u << fretOf(e.action)) : heldFrets_ & ~(1u << fretOf(e.action));
    if (e.action == Action::Strum) strumHeld_ = e.down;
    switch (screen_) {
    case Screen::NeedData:
        if (e.down && e.action == Action::Back) running_ = false;
#ifdef __ANDROID__
        if (e.down && (e.action == Action::Confirm || e.action == Action::Start || e.action == Action::Fret0)) isoImport_.pick(window_);
#else
        if (e.down && (e.action == Action::Confirm || e.action == Action::Start || e.action == Action::Fret0)) userFiles("locate_iso");
#endif
        break;
    case Screen::Menu:
        if (ui_) {
            uiButton(e);
            break;
        }
        if (!e.down) break;
        if (e.action == Action::Up) selected_ = (selected_ + songs_.size() - 1) % songs_.size();
        else if (e.action == Action::Down) selected_ = (selected_ + 1) % songs_.size();
        else if (e.action == Action::Left) difficulty_ = std::max(0, difficulty_ - 1);
        else if (e.action == Action::Right) difficulty_ = std::min(3, difficulty_ + 1);
        else if (e.action == Action::Confirm) startSong(selected_, e.mode);
        else if (e.action == Action::Back) running_ = false;
        break;
    case Screen::Playing: {
        // The attract mode (game.dta BUTTON_DOWN_MSG): any button leaves it.
        if (ui_ && ui_->inGame() && ui_->interp().var("attract_mode").truthy()) {
            if (e.down) ui_->buttonDown(e.action == Action::Start ? UiSystem::Start : UiSystem::Cross, e.mode == ControllerMode::Guitar);
            break;
        }
        if ((e.action == Action::Back || e.action == Action::Start) && ui_ && ui_->inGame()) {
            // game screen: kPad_Start goes to the pause screen.
            if (e.down) ui_->buttonDown(UiSystem::Start, e.mode == ControllerMode::Guitar);
            else ui_->buttonUp(UiSystem::Start);
            break;
        }
        if ((e.action == Action::Back || e.action == Action::Start) && e.down) {
            mixer_.setPaused(true);
            screen_ = Screen::Paused;
            break;
        }
        if (autoplay_ && !(e.action == Action::StarPower && e.down)) break;  // autoplay still deploys on request
        if (tutorial_ && mixer_.paused()) break;  // set_time_paused: BeatMatch is paused too
        // Player i reads pad port i (FUN_00108260).
        Player* pl = playerAt(multi_ ? e.player : 0);
        if (!pl) break;
        float t = static_cast<float>(mixer_.songTimeMs(e.timestampNs) - avOffsetMs());
        pl->update(t);
        if (isFret(e.action)) {
            if (e.down) pl->fretDown(fretOf(e.action), t);
            else pl->fretUp(fretOf(e.action), t);
        } else if (e.action == Action::Strum && e.down) {
            pl->strum(t);
        } else if (e.action == Action::StarPower && e.down) {
            pl->starPowerButton(t);
        } else if (e.action == Action::Whammy) {
            pl->setWhammy(e.down);
        }
        break;
    }
    case Screen::Paused:
        if (ui_ && ui_->inGame()) {
            uiButton(e);
            break;
        }
        if (!e.down) break;
        if (e.action == Action::Confirm) {
            mixer_.setPaused(false);
            screen_ = Screen::Playing;
        } else if (e.action == Action::Back) {
            mixer_.setPaused(false);
            mixer_.stop();
            backToMenu();
        }
        break;
    case Screen::Results:
        if (e.down && (e.action == Action::Confirm || e.action == Action::Back)) backToMenu();
        break;
    case Screen::SceneView:
    case Screen::PanelView:
        if (e.down && e.action == Action::Back) running_ = false;
        break;
    }
}

// The touch screen's on-screen buttons (Input::touchButtons): a dark,
// see-through disc with a light rim and its symbol, over everything.
void App::drawTouchButtons() {
    for (const Input::TouchButton& b : input_.touchButtons()) {
        std::vector<Vertex> v;
        auto disc = [&](float r, SDL_Color c) {
            constexpr int kSeg = 32;
            for (int i = 0; i < kSeg; ++i) {
                float a0 = 6.2831853f * float(i) / kSeg, a1 = 6.2831853f * float(i + 1) / kSeg;
                v.push_back({b.x, b.y, 0, 0, 0, c.r, c.g, c.b, c.a});
                v.push_back({b.x + r * std::cos(a0), b.y + r * std::sin(a0), 0, 0, 0, c.r, c.g, c.b, c.a});
                v.push_back({b.x + r * std::cos(a1), b.y + r * std::sin(a1), 0, 0, 0, c.r, c.g, c.b, c.a});
            }
        };
        disc(b.r, {230, 230, 235, 120});      // the rim
        disc(b.r - 3, {10, 10, 14, 120});     // the face
        const SDL_Color ink{240, 240, 245, 220};
        auto tri = [&](float x0, float y0, float x1, float y1, float x2, float y2) {
            v.push_back({x0, y0, 0, 0, 0, ink.r, ink.g, ink.b, ink.a});
            v.push_back({x1, y1, 0, 0, 0, ink.r, ink.g, ink.b, ink.a});
            v.push_back({x2, y2, 0, 0, 0, ink.r, ink.g, ink.b, ink.a});
        };
        const float k = b.r * 0.45f;
        const bool song = screen_ == Screen::Playing && b.action == Action::Start && b.y < 100;
        if (b.action == Action::Up) tri(b.x, b.y - k, b.x - k, b.y + k * 0.7f, b.x + k, b.y + k * 0.7f);
        if (b.action == Action::Down) tri(b.x - k, b.y - k * 0.7f, b.x + k, b.y - k * 0.7f, b.x, b.y + k);
        if (song) {  // pause: two bars
            const float w = k * 0.35f, h = k;
            for (float x : {b.x - k * 0.55f, b.x + k * 0.2f}) tri(x, b.y - h, x + w, b.y - h, x + w, b.y + h), tri(x, b.y - h, x + w, b.y + h, x, b.y + h);
        }
        renderer_.draw(v.data(), v.size(), 0, Blend::Alpha);
        if (b.action == Action::Start && !song) renderer_.textCentered(b.x, b.y - 7, 1.0f, ink, "START");
    }
}

// The Guitar Dream Engine badge above the first screen's text.
void App::drawLogo() {
    if (logoTexture_ < 0) {
        Image icon;
        logoTexture_ = appIconImage(icon) ? renderer_.createTexture(icon) : 0;
    }
    if (logoTexture_ > 0) renderer_.image(logoTexture_, kScreenW / 2 - 100, 70, 200, 200, {255, 255, 255, 255}, Blend::Alpha);
    else renderer_.textCentered(kScreenW / 2, 240, 3, kAccent, "GD-Engine");
}

void App::drawNeedData() {
#ifdef __ANDROID__
    if (std::string iso = isoImport_.takeReady(); !iso.empty()) {
        tryLoadData(iso);
        if (screen_ != Screen::NeedData) return;
    }
    drawLogo();
    renderer_.textCentered(kScreenW / 2, 310, 2, kText, "Tap the screen to choose your Guitar Hero (PS2) disc image (.iso),");
    renderer_.textCentered(kScreenW / 2, 340, 2, kText, "or copy it into " + AndroidIsoImport::folder());
    renderer_.textCentered(kScreenW / 2, 390, 1.5f, kDim, "Game data is read from your own copy; nothing is included with this program.");
    std::string status = isoImport_.status();
    if (!status.empty()) renderer_.textCentered(kScreenW / 2, 430, 1.5f, kText, status);
    if (!message_.empty()) renderer_.textCentered(kScreenW / 2, 460, 1.5f, kBad, message_);
    return;
#endif
    drawLogo();
    renderer_.textCentered(kScreenW / 2, 310, 2, kText, "Click here (or press Enter) to choose your Guitar Hero (PS2) disc image (.iso),");
    renderer_.textCentered(kScreenW / 2, 340, 2, kText, "or drop the .iso or the extracted game folder onto this window.");
    renderer_.textCentered(kScreenW / 2, 390, 1.5f, kDim, "Game data is read from your own copy; nothing is included with this program.");
    if (!message_.empty()) renderer_.textCentered(kScreenW / 2, 460, 1.5f, kBad, message_);
}

void App::drawMenu() {
    renderer_.text(60, 40, 3, kAccent, "SELECT A SONG");
    std::string diff = std::string("<  ") + difficultyName(static_cast<Difficulty>(difficulty_)) + "  >";
    renderer_.text(900, 48, 2, kText, diff);
    const int rows = 14;
    size_t top = selected_ >= rows / 2 ? std::min(selected_ - rows / 2, songs_.size() > rows ? songs_.size() - rows : 0) : 0;
    for (size_t i = top; i < std::min(songs_.size(), top + rows); ++i) {
        float y = 110 + float(i - top) * 38;
        bool sel = i == selected_;
        if (sel) renderer_.fillRect(50, y - 8, 1180, 34, {60, 50, 30, 255});
        renderer_.text(70, y, 2, sel ? kAccent : kText, songs_[i].name);
        renderer_.text(760, y + 2, 1.5f, kDim, songs_[i].artist);
    }
    renderer_.text(60, 670, 1.5f, kDim,
             "Up/Down: song   Left/Right: difficulty   Enter / Start: play   Esc: quit");
    if (!message_.empty()) renderer_.text(60, 640, 1.5f, kBad, message_);
}


// One player's frame (FUN_00116498 per player): autoplay, whammy, the
// judgment, then its events to the highway, the venue and the scripts.
void App::pollPlayer(int p, float t) {
    Player& pl = *playerAt(p);
    Autoplay* ap = p == 0 ? autoplay_.get() : autoplay2_.get();
    TrackRenderer* tr = p == 0 ? track_.get() : track2_.get();
    bool& hitting = p == 0 ? hitting_ : hitting2_;
    auto& flashes = p == 0 ? flashes_ : flashes2_;
    const bool uiGame = ui_ && ui_->inGame();
    if (ap && !endWallNs_) ap->feed(pl, t);
    if (!endWallNs_) {
        // The bar: the guitar's analog whammy, or a wiggle while a digital whammy is held.
        // Development aid: GD01_WHAMMY=1 holds a digital whammy all song.
        static const bool devWhammy = SDL_getenv("GD01_WHAMMY") != nullptr;
        if (devWhammy) pl.setWhammy(true);
        float w = input_.whammyValue(p);
        if (pl.whammyHeld()) w = std::max(w, 0.3f + 0.3f * std::sin(t * 0.025f));
        pl.setWhammyValue(w, t);
        pl.update(t);
    }
    // The player's SlipTo 0 calls (a hit, an own gem's time in hit state):
    // its guitar copy jumps back in sync.
    if (pl.slipTos() != slipTosSeen_[size_t(p)]) {
        slipTosSeen_[size_t(p)] = pl.slipTos();
        mixer_.resyncGuitar(p);
    }
    std::vector<PlayerEvent> events = pl.takeEvents();
    for (const PlayerEvent& ev : events) {
        if (ev.kind == PlayerEvent::Kind::Miss || ev.kind == PlayerEvent::Kind::Pass) {
            hitting = false;  // miss / pass
            if (tr) tr->stopHitEffects();
        }
        if (ev.kind == PlayerEvent::Kind::Hit) {
            hitting = true;
            const Gem& g = pl.chart().gems[ev.gem];
            for (int f = 0; f < 5; ++f)
                if (g.frets & (1u << f)) flashes[f] = 1.0f;
            if (tr) {
                // StarPower +0x44: 3 when this hit captured the phrase, 2 inside an intact phrase.
                bool captured = std::any_of(events.begin(), events.end(), [&](const PlayerEvent& e) {
                    return e.kind == PlayerEvent::Kind::PhraseCaptured && e.gem == ev.gem;
                });
                int sp = captured ? 3 : g.phrase >= 0 && pl.phraseIntact(g.phrase) ? 2 : 0;
                tr->hitGem(t, ev.gem, g.frets, pl.starPowerActive(), sp, !g.noSustain);
            }
            // PlayerMatcher_v3: $slot = the gem's lowest fret, {arena hit_gem}.
            if (venue_ && p == 0)
                for (int f = 0; f < 5; ++f)
                    if (g.frets & (1u << f)) {
                        venue_->hitGem(f);
                        break;
                    }
        }
        if (uiGame) playerEvent(ev, p);
        else if (ev.kind == PlayerEvent::Kind::StarPowerStart && venue_) venue_->startStarPower();
    }
}

void App::drawResults() {
    const SongInfo& info = songs_[playingIndex_];
    int total = static_cast<int>(player_->chart().gems.size());
    int pct = total ? player_->notesHit() * 100 / total : 0;
    char buf[96];
    renderer_.textCentered(kScreenW / 2, 140, 3, player_->failed() ? kBad : kAccent,
                     player_->failed() ? "SONG FAILED" : "SONG COMPLETE");
    renderer_.textCentered(kScreenW / 2, 210, 2, kText, info.name + " - " + info.artist);
    std::snprintf(buf, sizeof buf, "Score %d", player_->score());
    renderer_.textCentered(kScreenW / 2, 290, 3, kText, buf);
    std::snprintf(buf, sizeof buf, "Notes hit %d / %d (%d%%)", player_->notesHit(), total, pct);
    renderer_.textCentered(kScreenW / 2, 350, 2, kText, buf);
    std::snprintf(buf, sizeof buf, "Longest streak %d", player_->longestStreak());
    renderer_.textCentered(kScreenW / 2, 390, 2, kText, buf);
    renderer_.textCentered(kScreenW / 2, 470, 1.5f, kDim, "Press Enter to continue");
}

void App::frame() {
    uint64_t nowNs = SDL_GetTicksNS();
    float dt = lastFrameNs_ ? (nowNs - lastFrameNs_) / 1e9f : 0;
    lastFrameNs_ = nowNs;
    if (dt > 0.1f && SDL_getenv("GD01_UILOG")) SDL_Log("frame: %.0f ms stall", dt * 1000.0f);
    // FPS: frames counted over half-second windows (the FPS counter, GD01_FRAMELOG).
    fpsFrames_ += 1;
    fpsWorst_ = std::max(fpsWorst_, dt);
    if (nowNs - fpsWindowNs_ >= 500000000ull) {
        if (fpsWindowNs_) {
            fps_ = float(fpsFrames_) * 1e9f / float(nowNs - fpsWindowNs_);
            static const bool log = SDL_getenv("GD01_FRAMELOG") != nullptr;
            if (log) SDL_Log("fps %.1f worst %.1f ms", fps_, fpsWorst_ * 1000.0f);
        }
        fpsWindowNs_ = nowNs, fpsFrames_ = 0, fpsWorst_ = 0;
    }
    if (synth_) synth_->poll(std::min(dt * 1000.0f, 100.0f));

    if (screen_ == Screen::Playing || screen_ == Screen::Paused) {
        float t = static_cast<float>(mixer_.frameTimeMs() - avOffsetMs());
        const float drawT = t + float(enhancedInt("video_offset"));  // Audio / Video Calibration
        // game_won / game_lost set the game clock to realtime (the music slows and stops).
        if (endWallNs_ && screen_ == Screen::Playing) t = endMs_ + float(nowNs - endWallNs_) / 1e6f;
        const bool uiGame = ui_ && ui_->inGame();
        if (screen_ == Screen::Playing) {
            pollPlayer(0, t);
            if (multi_ && player2_) pollPlayer(1, t);
            // Each player's UnmuteBeatMatchAudio drives that player's guitar copy:
            // _v31 its whammy bend (back to speed 1 once it lags 0.9 x max_slip,
            // 270 ms), StopSlip / SlipTo whether it is heard.
            for (int k = 0; k < (multi_ && player2_ ? 2 : 1); ++k) {
                Player* pl = playerAt(k);
                float speed = pl->bendSpeed();
                if (mixer_.guitarSlipMs(k) > 270) speed = 1;
                mixer_.setGuitarSlipSpeed(speed, k);
                mixer_.setGuitarAudible(pl->guitarAudible(), k);
            }
            // FUN_00258b20's active players (0 = no section, 3 = both) as the
            // audio's +0xe0 flag; it only takes effect at the next set_volume.
            if (!multi_ && !guitarCommon_ && !player_->chart().gems.empty() && player_->chart().gems.front().ms <= t) guitarCommon_ = true;
            if (multi_ && activeMask_ >= 0) guitarCommon_ = activeMask_ == 0 || activeMask_ == 3;
            songMs_ = t;
            if (uiGame) {
                arenaEvents(t, lastSongMs_);
                // Player poll FUN_00116498 wins once the "end" event passes; a
                // failed rock meter loses (FUN_0016a640 -> arena game_over, and
                // venue.dta game_won / game_lost). game_outro follows when the
                // guitarist's win / lose clip ends (without a venue, kOutroMs).
                // Tutorials never end the game: the lesson scripts stop at 97 %.
                if (!tutorial_ && endMs_ < 0 && (player_->failed() || t > song_->endMs)) {
                    endMs_ = t;
                    endWallNs_ = nowNs;
                    bool won = !player_->failed();
                    if (venue_) {
                        // final_win_sequence (FUN_00282870): won the career's final song.
                        bool career = ui_->interp().send("meta", "get", {Value::symbol("mode")}).str() == "career";
                        // Only above Easy (Easy plays the plain win; docs/career_e2e_plan.md G6).
                        bool finalWin = won && career && difficulty_ > 0 && campaign_ &&
                                        campaign_->finalSong(difficulty_, songs_[playingIndex_].id);
                        if (multi_ && player2_) {
                            // guitarist.dta: each guitarist wins when its score >= both (a draw: both win).
                            int s0 = player_->displayScore(), s1 = player2_->displayScore(), top = std::max(s0, s1);
                            venue_->gameOverMulti(s0 >= top, s1 >= top);
                        } else {
                            venue_->gameOver(won, finalWin);
                        }
                        // The WIN_FINAL clip's enter_event (config/macros.dta:84-88): the wingame crowd stream.
                        if (finalWin && crowd_) crowd_->playStream(1.0f, "wingame");
                    }
                    Interp& in = ui_->interp();
                    in.var("arena.game_over") = Value::integer(1);
                    in.var("arena.game_won") = Value::integer(won);
                    ui_->gameEvent("arena", "game_over");
                    ui_->gameEvent("arena", won ? "game_won" : "game_lost");
                }
                bool outro = endMs_ >= 0 && endMs_ < 1e29f && (venue_ ? venue_->outroDone() : t >= endMs_ + kOutroMs);
                if (outro) {
                    endMs_ = 1e30f;
                    ui_->gameEvent("arena", "game_outro");  // stop_all_sfx, hud off, goto post_show / lose_screen
                }
            } else if (player_->failed() || t > song_->endMs + kOutroMs) {
                endSong();
            }
            lastSongMs_ = t;
        }
        for (float& f : flashes_) f = std::max(0.0f, f - dt * 5);
        for (float& f : flashes2_) f = std::max(0.0f, f - dt * 5);
        if (screen_ != Screen::Results) {
            const SongInfo& info = songs_[playingIndex_];
            std::string title = info.name + " - " + info.artist + (autoplay_ ? "  [autoplay]" : "");
            if (venue_ && screen_ == Screen::Playing) updateVenue(t);
            if (venue_ && (!ui_ || ui_->arenaShowing())) {  // arena set_showing 0: the No Venue cheat
                // Menus and the HUD stay 16:9; the venue may fill an ultrawide window up to 21:9.
                venue_->setAspect(renderer_.beginWide(21.0f / 9.0f));
                const bool post = renderer_.beginScene();  // Enhanced Settings post-processing, when on
                renderer_.clearDepth();
                venue_->draw();
                // The 3 px taps were sized for the PS2's 448 lines.
                // Enhanced Settings > Bokeh Effect: None skips the depth-of-field blur.
                if (enhanced("bokeh").str() != "none")
                    renderer_.depthBlur(venue_->dofDepth(), (venue_->strongerBokeh() ? 4.8f : 3.0f) * renderer_.viewportHeight() / 448.0f);
                if (post) {
                    PostFrame pf;
                    pf.viewProj = venue_->viewProj().m;
                    pf.hasCamera = true;
                    if (!venue_->stageFloorZ(pf.floorZ)) pf.floorZ = 0;
                    pf.timeSec = float(SDL_GetTicksNS() / 1000000) / 1000.0f;
                    postFrameExtras(pf, *venue_, true);
                    renderer_.endScene(pf);
                }
                renderer_.endWide();
            }
            // arena_game.dta game_lost / game_outro: the track and the HUD go away.
            if (track_ && track_->exploding()) {
                renderer_.clearDepth();  // track_panel explode (arena game_won)
                track_->draw(*player_, drawT, rules_.trackSpeed[difficulty_], rules_.slopMs, *song_, flashes_.data());
                if (multi_ && track2_ && player2_) track2_->draw(*player2_, drawT, rules_.trackSpeed[difficulty_], rules_.slopMs, *song_, flashes2_.data());
            } else if ((ui_ && endMs_ >= 0) || SDL_getenv("GD01_GUITARIST_CAM") || SDL_getenv("GD01_NOTRACK")) {
                // the game is over (or the guitarist close-up dev view)
            } else if (track_ && ui_ && ui_->inGame() && !ui_->trackShowing()) {
                // track_panel set_showing FALSE (the attract mode)
            } else if (track_) {
                renderer_.clearDepth();
                track_->draw(*player_, drawT, rules_.trackSpeed[difficulty_], rules_.slopMs, *song_, flashes_.data());
                if (multi_ && track2_ && player2_) {
                    renderer_.clearDepth();
                    track2_->draw(*player2_, drawT, rules_.trackSpeed[difficulty_], rules_.slopMs, *song_, flashes2_.data());
                }
                if (!(ui_ && ui_->inGame())) highway_.drawHud(renderer_, *player_, title);  // the UI's hud panel otherwise
            } else {
                highway_.draw(renderer_, *song_, *player_, t, rules_.trackSpeed[difficulty_], title, flashes_);
            }
            if (t < 0 && !(ui_ && ui_->inGame()))
                renderer_.textCentered(kScreenW / 2, 330, 4, kAccent, std::to_string(int(-t / 1000) + 1));
            if (ui_ && ui_->inGame()) {
                uint64_t now = SDL_GetTicksNS();
                float udt = uiTicks_ ? float(now - uiTicks_) / 1e6f : 16.0f;
                uiTicks_ = now;
                ui_->update(std::min(udt, 100.0f));
                if (ui_->inGame()) ui_->draw();
            } else if (screen_ == Screen::Paused) {
                renderer_.fillRect(0, 0, kScreenW, kScreenH, {0, 0, 0, 160});
                renderer_.textCentered(kScreenW / 2, 300, 4, kText, "PAUSED");
                renderer_.textCentered(kScreenW / 2, 370, 2, kDim, "Enter: resume    Esc: quit song");
            }
        }
    }
    if (screen_ == Screen::SceneView) drawSceneView();
    if (screen_ == Screen::PanelView && panel_) panel_->draw();
    if (screen_ == Screen::NeedData) {
        pollUserFiles();
        if (screen_ == Screen::NeedData) drawNeedData();
    } else if (screen_ == Screen::Menu && ui_) {
        uint64_t now = SDL_GetTicksNS();
        float dt = uiTicks_ ? float(now - uiTicks_) / 1e6f : 16.0f;
        uiTicks_ = now;
        ui_->update(std::min(dt, 100.0f));
        pollMenuMusic();
        pollUserFiles();
        if (screen_ == Screen::Menu) ui_->draw();
    } else if (screen_ == Screen::Menu) drawMenu();
    else if (screen_ == Screen::Results) drawResults();
}

int App::run() {
    // The Enhanced Settings first (before SDL starts).
    if (std::ifstream in(enhancedPath()); in) {
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        try {
            enhanced_ = Json::parse(text);
        } catch (const std::exception& ex) {
            SDL_Log("enhanced settings: %s: %s", enhancedPath().c_str(), ex.what());
        }
        if (enhanced_.type != Json::Type::Object) enhanced_ = Json::object();
    }
    gpu::preferDedicated();  // hybrid machines: the dedicated GPU, before SDL picks one
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");  // the back button is the game's Back, not "leave the app"
#endif
    // Started by Steam (Game Mode, or any game Steam starts), Steam hides the
    // real controllers from the game (SDL_GAMECONTROLLER_IGNORE_DEVICES) and
    // offers its virtual gamepads instead, which SDL skips unless allowed: take
    // them, or the Steam Deck's controls would be missing.
    if (SDL_getenv("SDL_GAMECONTROLLER_IGNORE_DEVICES") || SDL_getenv("SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"))
        SDL_setenv_unsafe("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1", 0);  // read from the environment (SDL_gamepad.c)
    // The app id ties the window to its launcher entry (the taskbar icon on
    // Wayland; Linux downloads write io.github.mbracali.gd01.desktop, installDesktopEntry).
    SDL_SetAppMetadata("GD Engine 01", GD01_VERSION, "io.github.mbracali.gd01");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kAppTitle, SDL_GetError(), nullptr);
        return 1;
    }
    input_.setSplitKeyboard(opt_.splitKeyboard || SDL_getenv("GD01_KB2P"));
    input_.setTouchToMenu([this](float nx, float ny, float& mx, float& my) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        renderer_.windowToMenu(nx * float(w), ny * float(h), mx, my);
    });
#ifdef __ANDROID__
    const bool gles = true;
#else
    const bool gles = SDL_getenv("GD01_GLES") != nullptr;  // development aid: the Android (OpenGL ES 3) renderer
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gles ? 0 : 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, gles ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
    if (!gles) SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // required on macOS
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Development aid: GD01_WINDOW=<w>x<h> sets the starting window size (e.g. ultrawide tests).
    int winW = int(kScreenW), winH = int(kScreenH);
    if (const char* ws = SDL_getenv("GD01_WINDOW")) std::sscanf(ws, "%dx%d", &winW, &winH);
    window_ = SDL_CreateWindow(kAppTitle, winW, winH,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
#ifdef __ANDROID__
                                   | SDL_WINDOW_FULLSCREEN
#endif
    );
    setAppWindowIcon(window_);
    installDesktopEntry();
    std::string glError;
    bool glOk = window_ && renderer_.init(window_, glError);
    gpu::started(glOk, renderer_.gpuName());
    if (!glOk) {
        std::string msg = "Cannot start graphics: " + (window_ ? glError : std::string(SDL_GetError()));
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kAppTitle, msg.c_str(), window_);
        return 1;
    }
    applyEnhanced();

    std::string audioError;
    if (!mixer_.open(audioError)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kAppTitle, ("Cannot open audio: " + audioError).c_str(), window_);
        return 1;
    }

    std::string path = !opt_.dataPath.empty() ? opt_.dataPath : loadRememberedPath();
#ifdef __ANDROID__
    if (path.empty() || !std::filesystem::exists(path)) path = AndroidIsoImport::findIso();
#endif
    if (!path.empty()) tryLoadData(path);
    if (screen_ == Screen::Menu && !opt_.viewScene.empty()) openSceneView();
    if (screen_ == Screen::Menu && !opt_.panel.empty()) {
        try {
            scripts_ = std::make_unique<ScriptLoader>(*data_);
            uiRes_ = std::make_unique<UiResources>(renderer_, *data_, *scripts_);
            panel_ = std::make_unique<Panel>(renderer_, *data_, *uiRes_, opt_.panel);
            screen_ = Screen::PanelView;
        } catch (const std::exception& ex) {
            SDL_Log("panel %s: %s", opt_.panel.c_str(), ex.what());
        }
    }
    if (screen_ == Screen::Menu && !opt_.song.empty()) {
        auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return s.id == opt_.song; });
        if (it != songs_.end()) startSong(size_t(it - songs_.begin()), ControllerMode::Guitar);
        else message_ = "Unknown song id: " + opt_.song;
    }

    std::vector<InputEvent> actions;
    while (running_) {
        updateTouchLanes();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running_ = false;
            else if (e.type == SDL_EVENT_DROP_FILE && e.drop.data && screen_ != Screen::Playing) tryLoadData(e.drop.data);
#ifdef __ANDROID__
            else if (e.type == SDL_EVENT_FINGER_UP && screen_ == Screen::NeedData) isoImport_.pick(window_);
#else
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && screen_ == Screen::NeedData)
                userFiles("locate_iso");
#endif
            // Mouse support: hover focuses menu entries (only while the window has
            // focus), a left click selects, the wheel scrolls.
            else if ((e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_WHEEL ||
                      (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)) &&
                     ui_ && !movie_ && (screen_ == Screen::Menu || ui_->inGame()) && enhancedOn("mouse")) {
                if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                    if (e.wheel.y != 0) ui_->wheel(e.wheel.y > 0 ? -1 : 1);
                    continue;
                }
                bool click = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                if (!click && !(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS)) continue;
                float mx, my;
                renderer_.windowToMenu(click ? e.button.x : e.motion.x, click ? e.button.y : e.motion.y, mx, my);
                ui_->mouse(mx, my, click);
            }
            input_.handle(e, actions);
        }
        input_.poll(SDL_GetTicksNS(), actions);
        for (auto [mx, my] : input_.takeMenuTaps())  // a tap in the menus is a click
            if (ui_ && !movie_) ui_->mouse(mx, my, true);
            else if (movie_) endMovie();  // a tap skips a movie, like a button
        injectDebugKeys(actions);
        if (movie_) {
            for (const InputEvent& a : actions)
                if (a.down && (a.action == Action::Confirm || a.action == Action::Back || a.action == Action::Start || a.action == Action::Fret0)) endMovie();
        } else {
            for (const InputEvent& a : actions) onAction(a);
        }
        actions.clear();

        renderer_.beginFrame({movie_ ? uint8_t(0) : uint8_t(12), movie_ ? uint8_t(0) : uint8_t(10), movie_ ? uint8_t(0) : uint8_t(16), 255});
        if (movie_) movieFrame();
        else frame();
        if (!movie_) drawTouchButtons();
        if (enhancedOn("fps_counter")) drawFpsCounter();
        captureDebugScreenshot();
        renderer_.endFrame();
    }

    mixer_.close();
    input_.close();
    renderer_.shutdown();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return 0;
}

}  // namespace

int runApp(const Options& options) { return App(options).run(); }

int runSelfTest(const Options& options) {
    std::string path = options.dataPath.empty() ? loadRememberedPath() : options.dataPath;
    try {
        GameData data(path);
        Rules rules = loadRules(data);
        auto songs = loadSongs(data);
        std::printf("%u songs in %s; slop %.0f ms, head %d, tail %d\n", unsigned(songs.size()), data.description().c_str(),
                    rules.slopMs, rules.headPoints, rules.tailPoints);
        int failures = 0, hopos = 0;
        for (const SongInfo& info : songs) {
            Song song = buildSong(parseMidi(data.read(info.midiPath)));
            std::printf("%-26s", info.id.c_str());
            for (int d = 0; d < kDifficultyCount; ++d) {
                // Every gem strummed, then HOPOs hammered on / pulled off.
                Player s(song, static_cast<Difficulty>(d), rules, ControllerMode::Guitar, false);
                Player p(song, static_cast<Difficulty>(d), rules, ControllerMode::Guitar, false);
                Autoplay strummer(s.chart(), -1, false), bot(p.chart());
                for (float t = -1000; t < song.endMs + 5000; t += 4) {
                    strummer.feed(s, t), bot.feed(p, t);
                    s.update(t), p.update(t);
                    for (const PlayerEvent& e : p.takeEvents()) hopos += e.kind == PlayerEvent::Kind::Hopo;
                    s.takeEvents();
                }
                int total = static_cast<int>(p.chart().gems.size());
                bool ok = p.notesHit() == total && !p.failed() && s.notesHit() == total && !s.failed();
                if (s.notesHit() != total) std::fprintf(stderr, "  strummed only: %d/%d\n", s.notesHit(), total);
                failures += !ok;
                std::printf("  %c%4d/%-4d %7d", difficultyName(static_cast<Difficulty>(d))[0], p.notesHit(), total, p.score());
                if (!ok) {
                    std::printf("!");
                    const auto& gems = p.chart().gems;
                    for (int i = 0; i < total; ++i) {
                        if (p.gemHit(i)) continue;
                        std::fprintf(stderr, "  missed %s %s gem %d at %.1f ms, frets %02x%s, gap %.1f ms\n",
                                     info.id.c_str(), difficultyName(static_cast<Difficulty>(d)), i, gems[i].ms,
                                     gems[i].frets, gems[i].hopo ? " hopo" : "", i ? gems[i].ms - gems[i - 1].ms : 0.0f);
                    }
                }
            }
            std::printf("\n");
        }
        std::printf("%d gems hit as hammer-ons / pull-offs\n", hopos);
        std::printf(failures ? "%d chart(s) not fully hit\n" : "all charts fully hit\n", failures);
        return failures ? 1 : 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "self-test failed: %s\n", ex.what());
        return 2;
    }
}

int runSceneTest(const Options& options) {
    std::string path = options.dataPath.empty() ? loadRememberedPath() : options.dataPath;
    try {
        GameData data(path);
        int files = 0, objects = 0, failed = 0;
        std::map<std::string, int> decoded;
        for (const std::string& p : data.paths()) {
            bool rnd = p.size() >= 8 && p.compare(p.size() - 8, 8, ".rnd_ps2") == 0;
            bool gh = p.size() >= 3 && p.compare(p.size() - 3, 3, ".gh") == 0;
            if (!rnd && !gh) continue;
            ++files;
            try {
                Scene scene(data.read(p));
                for (const auto& o : scene.objects()) {
                    ++objects;
                    if (o.tex || o.mat || o.mesh || o.view || o.cam || o.environment || o.light || o.multiMesh || o.anim ||
                        o.font || o.text || o.ui)
                        ++decoded[o.type];
                }
                failed += scene.undecoded();
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), ex.what());
                ++failed;
            }
        }
        std::printf("%d scene files, %d objects\n", files, objects);
        for (const auto& [type, n] : decoded) std::printf("  %-10s %6d decoded\n", type.c_str(), n);
        std::printf("  %d objects not decoded (classes without a C++ decoder, or errors above)\n", failed);
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "scene test failed: %s\n", ex.what());
        return 2;
    }
}
