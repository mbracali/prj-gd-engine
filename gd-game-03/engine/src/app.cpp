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
#include "game/band_performer.h"
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
#include "video/movie_player.h"

namespace {

// GH2's song start (GamePanel start 0x822CCA60, poll 0x822CDEB8) [C]: the
// song clock starts at minus the length of the INTRO shot GamePanel draws
// from the world's picker (INTRO_ENCORE for an encore; a fast intro, the
// restart from the pause menu, at track_extend_sec); `extend_track` runs at
// song time track_extend_sec (game.dta (track_extend_sec -2): at once when
// the clock starts later), and past -25 ms the intro clock hands over to the
// song stream (0x822CB260) and the controllers come on (0x822CED30 /
// 0x822E2510). Without a venue shot the clock starts at track_extend_sec
// [I].
constexpr double kTrackExtendMs = -2000;
constexpr double kLeadInMs = -kTrackExtendMs;
constexpr float kControllersLiveMs = -25;
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
// The song's mode (config/modes.dta, docs/multiplayer.md): GH2's
// multi_vs is face-off (each player owns the face-off sections' gems),
// multi_fo pro face-off (both play every gem), multi_coop co-op. Without
// the menus, development aid GD03_MODE=faceoff|proface|coop|practice plays
// that mode with a second, autoplaying player (GD03_DIFF2=0..3 its
// difficulty, GD03_PARTS=<p0>,<p1> the co-op parts, e.g. guitar,bass).
enum class DevMode { Single, FaceOff, ProFaceOff, Coop, Practice };
DevMode devMode() {
    const char* m = SDL_getenv("GD03_MODE");
    const std::string v = m ? m : "";
    if (v == "faceoff" || v == "vs" || v == "multi_vs") return DevMode::FaceOff;
    if (v == "proface" || v == "pro" || v == "fo" || v == "multi_fo") return DevMode::ProFaceOff;
    if (v == "coop" || v == "multi_coop") return DevMode::Coop;
    if (v == "practice") return DevMode::Practice;
    return DevMode::Single;
}
DevMode modeOf(const std::string& gh2Mode) {
    if (gh2Mode == "multi_vs") return DevMode::FaceOff;
    if (gh2Mode == "multi_fo") return DevMode::ProFaceOff;
    if (gh2Mode == "multi_coop") return DevMode::Coop;
    if (gh2Mode == "practice") return DevMode::Practice;
    return DevMode::Single;
}
bool twoPlayers(DevMode m) { return m == DevMode::FaceOff || m == DevMode::ProFaceOff || m == DevMode::Coop; }
const char* hudFile(DevMode m) {
    switch (m) {
    case DevMode::FaceOff: case DevMode::ProFaceOff: return "hud_vs.milo";
    case DevMode::Coop: return "hud_coop.milo";
    case DevMode::Practice: return "hud_practice.milo";
    default: return "hud_sp.milo";
    }
}

bool envOn(const char* name) {
    const char* v = SDL_getenv(name);
    return v && *v && *v != '0';
}

// Two players (`parts`: their instruments): each player's (tracks
// (<instrument> channels)) are muted while that player misses; the guitar
// volume applies to both [I: GH2's per-player UnmuteBeatMatchAudio].
std::vector<Mixer::Track> mixerTracks(const SongAudio& a, float band = 1, float guitar = 1,
                                      const std::array<std::string, 2>* parts = nullptr) {
    std::vector<Mixer::Track> tracks(a.pans.size());
    auto channelsOf = [&](const std::string& inst) {
        auto it = a.tracks.find(inst);
        return it == a.tracks.end() ? std::vector<int>{} : it->second;
    };
    for (size_t c = 0; c < tracks.size(); ++c) {
        tracks[c].pan = a.pans[c];
        if (parts) {
            for (int ch : channelsOf((*parts)[0])) tracks[c].guitar = tracks[c].guitar || size_t(ch) == c;
            if ((*parts)[1] != (*parts)[0])
                for (int ch : channelsOf((*parts)[1])) tracks[c].guitar2 = tracks[c].guitar2 || size_t(ch) == c;
        } else {
            tracks[c].guitar = c < a.cores.size() && a.cores[c] == 1;
        }
        const bool mine = tracks[c].guitar || tracks[c].guitar2;
        tracks[c].volume = (c < a.vols.size() ? std::pow(10.0f, a.vols[c] / 20.0f) : 1.0f) * (mine ? guitar : band);
    }
    return tracks;
}

// A player's chart (docs/multiplayer.md): the instrument's PART track; the
// co-op lead's is PART GUITAR COOP (midi_suffix COOP) when the chart has it
// [C data], the others fall back to the plain part [I].
Part partFor(const Song& song, const std::string& instrument, bool coop) {
    if (instrument == "bass" && song.hasPart[size_t(Part::Bass)]) return Part::Bass;
    if (instrument == "rhythm" && song.hasPart[size_t(Part::Rhythm)]) return Part::Rhythm;
    if (coop && song.hasPart[size_t(Part::GuitarCoop)]) return Part::GuitarCoop;
    return Part::Guitar;
}

// A section of a .vgs looped (song_preview: songs.dta (preview a b) with a
// fade in and out [I: GH2's fade times]; the menu music: the whole loop).
class LoopSource final : public AudioSource {
public:
    LoopSource(std::unique_ptr<VgsStream> s, double startMs, double endMs, double fadeMs)
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
    std::unique_ptr<VgsStream> s_;
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
    std::vector<std::pair<std::string, double>> gameSfx() const override { return songSfx_; }
    double introCameraTravel() const override { return introCamTravel_; }
    float gameBeat() const override {
        return song_ ? float(song_->tempo.msToTick(std::max(0.0f, lastT_)) / std::max(1, song_->tempo.division())) : 0.0f;
    }
    float songDurationMs() const override { return song_ ? song_->endMs : 0.0f; }
    const Player* gamePlayer(int index) const override { return index == 0 ? player_.get() : player2_.get(); }
    const BandPerformer* gameBand() const override { return coopBand_.get(); }
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
    bool menuBackdropFramed() override;
    void pauseAllSfx(bool paused) override {
        if (synth_) synth_->setPaused(paused);
    }
    int sfxLiveVoices() override { return synth_ ? synth_->liveVoices() : -1; }

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
    // Mouse support in the menus (gd01's): `button` 0 hover, 1 left click, 3
    // right click (back); menu units. menuWheel: -1 up, +1 down. Both only
    // while the menus have the screen (not during a song).
    void menuMouse(float x, float y, int button);
    void menuWheel(int dir);
    bool menuMouseAllowed() const;
    // The mouse pointer: shown while the mouse moves in the menus, hidden
    // after 3 s without it, on a key or controller press and in a song.
    void updateCursor();
    bool cursorShown_ = true;
    bool inputLog_ = false;  // GD03_INPUTLOG=1: every input action
    double mouseMovedMs_ = -1e9;
    void playerAction(const InputEvent& a);
    void updateSong(float t);
    void updateIntro(float t);
    void gameSounds(int player, const std::vector<PlayerEvent>& events);
    float songClock();
    void frameSong();
    void frameUi();
    void drawSong(float t);
    void logPerf(uint64_t periodNs, int frames, uint64_t maxNs, uint64_t limitNs);
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

    // GH2's front end (ui/ui_manager.h); GD03_DEBUG_MENU=1 shows the debug
    // song list instead.
    std::unique_ptr<ui::UiManager> ui_;
    bool debugMenu_ = false;
    double uiStartMs_ = 0;
    MenuKeys menuKeys_;
    int escapes_ = 0;  // Back presses in a row on the main menu (three quit)
    SDL_JoystickID virtualPad_ = 0;  // GD03_VIRTUAL_PAD's controller (GD03_KEYS "j" items)
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
    std::string walkSaveDir_;  // the walk test's temporary save folder, removed at its end
    std::unique_ptr<VgsStream> pendingStream_;
    float bandVolume_ = 1, guitarVolume_ = 1;
    int syncOffsetMs_ = 0;
    double leadInMs_ = kLeadInMs;
    bool fastIntro_ = false;
    // The intro (beginSong): extend_track's song time, sent yet; the
    // controllers live yet, and the frets held while they were not (synced
    // when they come on).
    float extendAtMs_ = float(kTrackExtendMs);
    bool extendSent_ = false, controllersLive_ = false;
    std::array<uint32_t, 2> heldBeforeLive_{};
    // Without the menus: track_panel.dta's intro sounds, played by the program.
    int introSfx_ = 0;
    std::vector<std::pair<std::string, double>> songSfx_;  // UiHost::gameSfx
    // UiHost::introCameraTravel: the camera at the intro's first frame, and
    // the distance from there by song time -2000 ms.
    bool introCamSet_ = false;
    gfx::Vec3 introCam0_{};
    double introCamTravel_ = -1;
    float pendingAwardMs_ = -1e9f;  // without the menus: sp_awarded's time
    // Two players (docs/multiplayer.md): the mode, per player the
    // difficulty, the part (instrument) and the pad (input slot) that plays
    // it; the co-op band and the shared Star Power meter.
    DevMode mode_ = DevMode::Single;
    std::array<int, 2> difficulties_{3, 3};
    std::array<std::string, 2> parts_{"guitar", "guitar"};
    std::array<int, 2> pads_{0, 1};
    std::unique_ptr<Player> player2_;
    std::unique_ptr<Autoplay> bot2_;
    std::unique_ptr<TrackView> track2_;
    std::unique_ptr<BandPerformer> coopBand_;
    StarMeter sharedMeter_;
    bool guitarHeard_ = true;  // face-off / pro face-off: the shared part's channels
    // Face-off: the next gem of player 0's list and the last player_mask
    // sent with active_players_changed (Game +0xb0 / +0xb4, 0x822CFA70).
    size_t activeGem_ = 0;
    int activePlayers_ = 0;
    Player* playerFor(int pad);
    // track_graphics.dta's speed of the difficulty; with two players the
    // mean of theirs (0x826932D0) [C].
    float trackSpeed() const {
        if (!player2_) return rules_.trackSpeed[size_t(difficulty_)];
        return (rules_.trackSpeed[size_t(difficulties_[0])] + rules_.trackSpeed[size_t(difficulties_[1])]) * 0.5f;
    }
    Autoplay* botFor(const Player* p) { return p == player_.get() ? bot_.get() : bot2_.get(); }
    bool songLost() const { return coopBand_ ? coopBand_->failed() : player_ && player_->failed(); }
    // Development aid: GD03_FAIL_AT=<song ms> fails the song then.
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
    // Development aid: GD03_KEYS="<song ms>:<key>,..." (game/key_script.h)
    // plays scripted guitar input in every song, at exact song times.
    KeyScript keys_;
    // The song screen: GH2's own highway and HUD (render/track_view.h,
    // render/hud_view.h), loaded with the first song; gd01's debug highway
    // (render/highway.h) with GD03_DEBUG_HIGHWAY=1, or when GH2's scenes
    // cannot be loaded.
    Highway highway_;
    bool debugHighway_ = false;
    bool trackTried_ = false;
    std::string trackLayout_;
    std::unique_ptr<scene::MiloLibrary> milos_;
    std::unique_ptr<gfx::SceneCache> sceneCache_;
    std::unique_ptr<TrackView> track_;
    std::unique_ptr<HudView> hud_;
    // The venue behind the track (M8): set by the venue code once a venue is
    // loaded; draws the world with the renderer, materials with
    // DrawContext::hdr = `hdr` (GH2's HDR buffer, render/post_fx.h).
    std::function<void(gfx::Renderer& r, bool hdr)> worldDraw_;
    // The venue (game/venue.h, game/world.h, render/venue_view.h): without
    // the menus the song's (venues::forSong, GD03_VENUE), else world_panel's.
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
    // The Enhanced Options' backdrop (menuBackdrop): a still shot of the
    // second venue with the band caught mid-song (gd01's: a posed band that
    // does not move), through a camera of its own.
    struct Backdrop {
        float simMs = 0;      // the song time run so far (fixed steps)
        bool frozen = false;  // posed: nothing updates any more
        const SongInfo* info = nullptr;
        std::unique_ptr<Song> song;
        WorldDriver driver;
        float fromMs = 0;
        bool framed = false;  // the shot's target and direction are set
        gfx::Vec3 target, dir;
        float dist = 300, spread = 100;
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
    // Development aid: GD03_VIRTUAL_PAD=<vendor>:<product>[:guitar] (hex, e.g.
    // 1430:4748:guitar) plugs in a virtual controller, to test without hardware.
    if (const char* vp = SDL_getenv("GD03_VIRTUAL_PAD")) virtualPad_ = Input::attachVirtualPad(vp);
    inputLog_ = envOn("GD03_INPUTLOG");
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | gfx::prepareWindow();  // OpenGL (render/device.h)
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    debugHighway_ = envOn("GD03_DEBUG_HIGHWAY");
    debugMenu_ = envOn("GD03_DEBUG_MENU");
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
    // Touch: window-normalised positions to menu units (the 16:9 picture).
    input_.setTouchToMenu([this](float nx, float ny, float& mx, float& my) {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window_, &ww, &wh);
        const float scale = std::max(1e-4f, std::min(float(ww) / 1280.0f, float(wh) / 720.0f));
        mx = (nx * float(ww) - (float(ww) - 1280.0f * scale) * 0.5f) / scale;
        my = (ny * float(wh) - (float(wh) - 720.0f * scale) * 0.5f) / scale;
    });
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

    // GD03_PERF=1: a frame-time breakdown every 2 s (docs/renderer.md).
    const bool perf = envOn("GD03_PERF");
    uint64_t perfStart = SDL_GetTicksNS(), perfMaxNs = 0, perfLimitNs = 0;
    int perfFrames = 0;
    if (perf) {
        const gfx::DeviceInfo& di = canvas_.renderer().device().info();
        SDL_Log("perf: %s %s on %s, video driver %s", di.api.c_str(), di.version.c_str(), di.gpu.c_str(), SDL_GetCurrentVideoDriver());
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
        if (perf) {
            const uint64_t now = SDL_GetTicksNS();
            perfMaxNs = std::max(perfMaxNs, now - frameStart);
            perfLimitNs += now - frameStart - spent;
            ++perfFrames;
            if (now - perfStart >= 2'000'000'000ull && perfFrames > 0) logPerf(now - perfStart, perfFrames, perfMaxNs, perfLimitNs);
            if (now - perfStart >= 2'000'000'000ull) perfStart = now, perfFrames = 0, perfMaxNs = perfLimitNs = 0;
        }
    }

    if (walk_ && ui_) exitCode_ = walk_->finish(*ui_);
    // The walk's own save folder goes with it (its checks have read it).
    if (!walkSaveDir_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(walkSaveDir_, ec);
        walkSaveDir_.clear();
    }
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

// GH2's front end: the boot splashes and the intro (unless GD03_SKIP_BOOT=1
// or GD03_SCREEN=<screen>, which opens that screen), then the menus.
void App::startUi() {
    if (walk_) {
        // The walk test: no videos, a fresh save of its own, its song settings.
        SDL_setenv_unsafe("GD03_SKIP_MOVIES", "1", 1);
        walkSaveDir_ = (std::filesystem::temp_directory_path() / ("gd03-walk-" + std::to_string(SDL_GetTicksNS()))).string() + "/";
        std::filesystem::create_directories(walkSaveDir_);
        SDL_setenv_unsafe("GD03_SAVE_DIR", walkSaveDir_.c_str(), 1);
    }
    loadEnhanced();
    // A save folder of its own (the walk test, development) holds controllers.json too.
    if (const char* d = SDL_getenv("GD03_SAVE_DIR"); d && *d) {
        std::string dir = d;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
        input_.setSaveDir(dir);
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
    const char* keys = SDL_getenv("GD03_KEYS");
    menuKeys_ = MenuKeys(keys ? keys : "");
    if (!menuKeys_.valid()) SDL_Log("GD03_KEYS: some menu items were not understood and are ignored");
    std::string screen = SDL_getenv("GD03_SCREEN") ? SDL_getenv("GD03_SCREEN") : "";
    if (walk_) screen = walk_->startScreen();
    const bool skip = envOn("GD03_SKIP_BOOT") || !screen.empty();
    if (envOn("GD03_SKIP_BOOT") && screen.empty()) screen = "splash_screen";
    ui_->start(skip, screen);
    uiStartMs_ = double(SDL_GetTicksNS()) / 1e6;
}

bool App::loadSong(const SongInfo& info, int difficulty, bool autoplay, float band, float guitar, bool fastIntro) {
    endSong();
    stopMusic();
    try {
        song_ = std::make_unique<Song>(loadChart(*data_, info, rules_));
        difficulty_ = std::clamp(difficulty, 0, kDifficultyCount - 1);
        // Without the menus: GD03_MODE (with the menus gameLoad set the mode).
        if (!ui_) {
            mode_ = devMode();
            difficulties_ = {difficulty_, difficulty_};
            if (const char* d2 = SDL_getenv("GD03_DIFF2")) difficulties_[1] = std::clamp(std::atoi(d2), 0, kDifficultyCount - 1);
            parts_ = {"guitar", "guitar"};
            if (mode_ == DevMode::Coop) {
                // The second instrument the song has (song_provider get_instrument 1).
                const SongAudio& a = info.coopAudio ? *info.coopAudio : info.audio;
                parts_[1] = a.tracks.count("bass") ? "bass" : a.tracks.count("rhythm") ? "rhythm" : "guitar";
                if (const char* ps = SDL_getenv("GD03_PARTS")) {
                    const std::string v = ps;
                    const size_t comma = v.find(',');
                    if (comma != std::string::npos) parts_ = {v.substr(0, comma), v.substr(comma + 1)};
                }
            }
            pads_ = {0, 1};
        }
        difficulties_[0] = difficulty_;
        const bool coop = mode_ == DevMode::Coop, two = twoPlayers(mode_);
        // Co-op plays the song_coop block's audio (modes.dta song_block) when
        // the song has one, else the song's [I: the fallback].
        const SongAudio& audio = coop && info.coopAudio ? *info.coopAudio : info.audio;
        pendingStream_ = std::make_unique<VgsStream>(data_->read(audio.audioPath));
        // Face-off players own the face-off sections' gems: the parser fills
        // player_mask only with separate_parts and two players (0x823450B0)
        // [C]; the others play every gem.
        const bool faceOff = mode_ == DevMode::FaceOff;
        const Part part0 = two ? partFor(*song_, parts_[0], coop) : Part::Guitar;
        const Part part1 = partFor(*song_, parts_[1], coop);
        // Two players never fail on their own meters: competitive modes
        // cannot fail, a co-op player cannot end the game (0x822C6B98); the
        // co-op band's meter does (game/band_performer.h) [C].
        const bool noFail = options_.noFail || autoplay || mode_ != DevMode::Single;
        const ScoreConfig* config = coop ? &rules_.coop : &rules_.solo;  // modes.dta score_config
        // The watcher follows the device that acted last: a guitar or the
        // keyboard strums, a gamepad's frets strum (GH2's joypad_guitar).
        // Two players: each the watcher of the controller in its pad's slot
        // (GH2's beatmatch controller type per player, 0x822B3C50): a
        // joypad_guitar type is the joypad watcher.
        auto modeOfPad = [&](int pad) {
            for (const Input::Device& d : input_.devices())
                if (d.player == pad && d.id != "touch" && (d.id != "keyboard" || input_.padConnected(pad)))
                    if (input_.gh2Type(d.id).find("joypad") != std::string::npos) return ControllerMode::Joypad;
            return ControllerMode::Guitar;
        };
        player_ = std::make_unique<Player>(*song_, part0, static_cast<Difficulty>(difficulties_[0]), rules_,
                                           two ? modeOfPad(pads_[0]) : input_.lastMode(), noFail, faceOff ? 0 : -1, config);
        player_->setCalibrationMs(float(syncOffsetMs_));
        bot_ = autoplay ? std::make_unique<Autoplay>(player_->chart(), faceOff ? 0 : -1) : nullptr;
        player2_.reset(), bot2_.reset(), coopBand_.reset();
        sharedMeter_ = {};
        guitarHeard_ = true;
        activeGem_ = 0, activePlayers_ = 0;
        if (two) {
            player2_ = std::make_unique<Player>(*song_, part1, static_cast<Difficulty>(difficulties_[1]), rules_, autoplay || !ui_ ? ControllerMode::Guitar : modeOfPad(pads_[1]),
                                                true, faceOff ? 1 : -1, config);
            player2_->setNumber(1);
            player2_->setCalibrationMs(float(syncOffsetMs_));
            // Without the menus the second player always autoplays.
            if (autoplay || !ui_) bot2_ = std::make_unique<Autoplay>(player2_->chart(), faceOff ? 1 : -1);
            normaliseScores(*player_, *player2_);
            if (coop) {
                // The band (0x822C9E80): its crowd at the harder difficulty,
                // the easier player's targets mapped onto it; one meter.
                coopBand_ = std::make_unique<BandPerformer>(*song_, rules_, static_cast<Difficulty>(difficulties_[0]),
                                                            static_cast<Difficulty>(difficulties_[1]), info.id);
                linkCoop(*player_, *player2_, *coopBand_, sharedMeter_);
            }
        }
        failAtMs_ = SDL_getenv("GD03_FAIL_AT") ? float(std::atof(SDL_getenv("GD03_FAIL_AT"))) : 1e30f;
        // Development aid: GD03_END_AT=<song ms> ends (wins) the song early.
        if (const char* e = SDL_getenv("GD03_END_AT")) song_->endMs = std::min(song_->endMs, float(std::atof(e)));
        if (walk_) {
            song_->endMs = std::min(song_->endMs, walk_->songEndMs());
            failAtMs_ = std::min(failAtMs_, walk_->songFailMs());
        }
        failTicks_ = 0;
        exploded_ = false;
        // Development aid: GD03_LEFTY=1 (player 1), 2 (player 2) or 12 (both)
        // turns on the lefty flip (the options' lefty in the menus).
        if (const char* l = SDL_getenv("GD03_LEFTY")) input_.setLefty(SDL_strchr(l, '1') != nullptr, SDL_strchr(l, '2') != nullptr);
        // GD03_KEYS: with the menus, only its "s<song ms>:" items play in the song.
        const char* k = SDL_getenv("GD03_KEYS");
        keys_ = KeyScript(walk_ && !walk_->songKeys().empty() ? walk_->songKeys() : ui_ ? MenuKeys::songItems(k ? k : "") : (k ? k : ""));
        if (!keys_.valid()) SDL_Log("GD03_KEYS: some items were not understood and are ignored");
        SDL_Log("%s (%s): %d Hz, %d channels, %d %s gems, hopo_threshold %d", info.id.c_str(), info.name.c_str(),
                pendingStream_->sampleRate(), pendingStream_->channels(), int(player_->chart().gems.size()),
                difficultyName(static_cast<Difficulty>(difficulty_)), hopoThreshold(info.audio, rules_));
        if (player2_)
            SDL_Log("two players (%s): %s %s / %s %s, %d / %d gems, normalisation %.4f / %.4f",
                    mode_ == DevMode::Coop ? "co-op" : mode_ == DevMode::FaceOff ? "face-off" : "pro face-off", parts_[0].c_str(),
                    difficultyName(static_cast<Difficulty>(difficulties_[0])), parts_[1].c_str(),
                    difficultyName(static_cast<Difficulty>(difficulties_[1])), int(player_->chart().gems.size()),
                    int(player2_->chart().gems.size()), double(player_->normalisation()), double(player2_->normalisation()));
    } catch (const std::exception& ex) {
        message_ = info.id + ": " + ex.what();
        SDL_Log("%s", message_.c_str());
        player_.reset(), bot_.reset(), song_.reset(), pendingStream_.reset();
        return false;
    }
    bandVolume_ = band, guitarVolume_ = guitar;
    // The lead-in until beginSong times GH2's intro clock.
    fastIntro_ = fastIntro;
    leadInMs_ = kLeadInMs;
    extendAtMs_ = float(kTrackExtendMs);
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
        track_->reset(extendAtMs_);
        track_->setLefty(input_.lefty(0));
        if (track2_) track2_->reset(extendAtMs_), track2_->setLefty(input_.lefty(1));
        hud_->reset(extendAtMs_);
    }
    // The venue: without the menus the song's (its quickplay venue or career
    // tier, GD03_VENUE overrides); with them world_panel loads it (worldLoad).
    if (!ui_ && !debugHighway_) {
        const std::string v = venues::fromEnv().empty() ? venues::forSong(*data_, info) : venues::fromEnv();
        const VenueVariant var = mode_ == DevMode::FaceOff || mode_ == DevMode::ProFaceOff ? VenueVariant::Mp
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
    // GamePanel's start (0x822CCA60): the intro clock from the INTRO shot's
    // length (CamShot +0xf0 = every keyframe's duration + blend, frames at
    // 30 a second, 0x82226630) [C].
    double startMs = kTrackExtendMs;
    if (!fastIntro_ && director_)
        if (const scene::CamShot* shot = director_->drawShot({encoreFx_ ? "INTRO_ENCORE" : "INTRO"}))
            startMs = std::min(-double(shot->lengthFrames()) / 30.0 * 1000.0, 0.0);
    leadInMs_ = -startMs;
    extendAtMs_ = float(std::max(startMs, kTrackExtendMs));
    extendSent_ = controllersLive_ = false;
    heldBeforeLive_ = {};
    introSfx_ = 0;
    songSfx_.clear();
    introCamSet_ = false;
    introCamTravel_ = -1;
    lastT_ = float(startMs);
    lastFrameMs_ = startMs;
    if (track_ && hud_) {
        track_->reset(extendAtMs_);
        if (track2_) track2_->reset(extendAtMs_);
        hud_->reset(extendAtMs_);
    }
    worldDriver_.reset(float(startMs));
    // The world's clock moves to the intro clock now: with the menus
    // intro_start_msg follows at once and the world picks the INTRO shot,
    // which must start at this time, not at the previous clock.
    if (world_) world_->setClock(startMs / 1000.0, startMs / 500.0);
    SDL_Log("intro: the song clock starts at %.0f ms, the track extends at %.0f ms", startMs, double(extendAtMs_));
    mixer_.setSpeed(1);
    const bool coop = mode_ == DevMode::Coop;
    const SongAudio& audio = coop && info_->coopAudio ? *info_->coopAudio : info_->audio;
    mixer_.play(std::move(pendingStream_), mixerTracks(audio, bandVolume_, guitarVolume_, player2_ ? &parts_ : nullptr), leadInMs_);
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
    // The layout follows the mode: one track and hud_sp, or two tracks and
    // hud_vs / hud_coop (loaded again when the mode changes).
    const std::string layout = std::string(hudFile(mode_)) + (twoPlayers(mode_) ? ":2" : ":1");
    if (trackTried_ && (!track_ || !hud_ || layout == trackLayout_)) return track_ && hud_;
    trackTried_ = true;
    trackLayout_ = layout;
    const uint64_t start = SDL_GetTicksNS();
    try {
        if (!milos_) milos_ = std::make_unique<scene::MiloLibrary>(*data_);
        if (!sceneCache_) sceneCache_ = std::make_unique<gfx::SceneCache>(canvas_.renderer().device());
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
    if (player2_)
        SDL_Log("player 2: score %d, %d/%d notes, longest streak %d", player2_->score(), player2_->notesHit(),
                int(player2_->chart().gems.size()), player2_->longestStreak());
    if (coopBand_)
        SDL_Log("band: score %d, %d stars, longest streak %d%s", coopBand_->score(), coopBand_->stars(), coopBand_->longestStreak(),
                coopBand_->failed() ? ", failed" : "");
    mixer_.setPaused(false);
    mixer_.setSpeed(1);
    mixer_.stop();
    band_.reset();
    bot_.reset(), player_.reset(), bot2_.reset(), player2_.reset(), coopBand_.reset(), song_.reset(), pendingStream_.reset();
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
    } else if (ui_ && (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) && e.motion.which != SDL_TOUCH_MOUSEID) {
        // Mouse support (gd01's): hovering focuses (only while the window has
        // the input focus), a left click selects, a right click goes back.
        const bool click = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (enhancedValue("mouse") == "off") return;  // Enhanced Options > Enable Mouse Selection
        if (click && e.button.button != SDL_BUTTON_LEFT && e.button.button != SDL_BUTTON_RIGHT) return;
        if (!click && !(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS)) return;
        const float wx = click ? e.button.x : e.motion.x, wy = click ? e.button.y : e.motion.y;
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window_, &ww, &wh);
        const float scale = std::min(float(ww) / 1280.0f, float(wh) / 720.0f);
        if (scale <= 0) return;
        const float mx = (wx - (float(ww) - 1280.0f * scale) * 0.5f) / scale, my = (wy - (float(wh) - 720.0f * scale) * 0.5f) / scale;
        if (menuMouseAllowed()) mouseMovedMs_ = double(SDL_GetTicksNS()) / 1e6;
        menuMouse(mx, my, !click ? 0 : e.button.button == SDL_BUTTON_LEFT ? 1 : 3);
    } else if (ui_ && e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.which != SDL_TOUCH_MOUSEID && e.wheel.y != 0 && enhancedValue("mouse") != "off") {
        if (menuMouseAllowed()) mouseMovedMs_ = double(SDL_GetTicksNS()) / 1e6;
        menuWheel(e.wheel.y > 0 ? -1 : 1);
    } else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        mouseMovedMs_ = -1e9;  // a key or a button hides the pointer
        // A gamepad's yellow Y button on the main menu: GAMEPADS (its yellow
        // fret, RB, does it through the menus too).
        if (ui_ && e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH && input_.joypadDevice(e.gbutton.which))
            ui_->yellowButton();
    }
    // A tap in the menus is a click where it landed (Input::takeMenuTaps).
    for (auto [tx, ty] : input_.takeMenuTaps()) menuMouse(tx, ty, 1);
    for (const InputEvent& a : actions_) {
        if (inputLog_) SDL_Log("input: %s %s player %d%s", actionName(a.action), a.down ? "down" : "up", a.player, a.joypad ? " (joypad)" : "");
        handleAction(a);
    }
}

bool App::menuMouseAllowed() const {
    // Not while a song is being played (the pause menu takes it again).
    return ui_ && !ui_->inSplashes() && !input_.inGame();
}

void App::menuMouse(float x, float y, int button) {
    if (!menuMouseAllowed()) return;
    if (button == 3) {  // right click: back, as the red fret
        ui_->buttonDown(ui::kPad_Tri, 0);
        ui_->buttonUp(ui::kPad_Tri, 0);
        return;
    }
    ui_->mouse(x, y, button == 1);
}

void App::menuWheel(int dir) {
    if (menuMouseAllowed()) ui_->wheel(dir);
}

void App::updateCursor() {
    const double now = double(SDL_GetTicksNS()) / 1e6;
    const bool show = menuMouseAllowed() && now - mouseMovedMs_ < 3000;
    if (show == cursorShown_) return;
    cursorShown_ = show;
    if (show) SDL_ShowCursor();
    else SDL_HideCursor();
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
        if (player_ && begun_ && !paused_) playerAction(a);
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

// The player a pad plays for: with one player every device; with two the
// player whose pad (gamecfg set_player) it is.
Player* App::playerFor(int pad) {
    if (!player2_) return player_.get();
    if (pads_[1] == pad) return player2_.get();
    if (pads_[0] == pad) return player_.get();
    return nullptr;
}

void App::playerAction(const InputEvent& a) {
    if (exploded_ || wonSent_ || lostSent_) return;
    Player* p = playerFor(a.player);
    if (!p || botFor(p)) return;  // autoplay owns that player's frets
    const float t = float(mixer_.songTimeMs(a.timestampNs));
    // On the intro clock the beat-match controllers are off and drop every
    // button (0x822CED30; on again in 0x822E2510 once the clock hands over)
    // [C]: no strum, Star Power or miss before the track is up.
    if (!controllersLive_) {
        const size_t i = p == player2_.get() ? 1 : 0;
        if (isFret(a.action)) {
            const uint32_t bit = 1u << fretOf(a.action);
            heldBeforeLive_[i] = a.down ? heldBeforeLive_[i] | bit : heldBeforeLive_[i] & ~bit;
        }
        return;
    }
    p->update(t);
    if (isFret(a.action)) {
        if (a.down) p->fretDown(fretOf(a.action), t);
        else p->fretUp(fretOf(a.action), t);
    } else if (a.action == Action::Strum && a.down) {
        p->strum(t);
    } else if (a.action == Action::StarPower) {
        p->starPowerEngage(a.down, t);  // Select / tilt (force_mercury): engaged while held
    } else if (a.action == Action::Whammy && !a.analog) {
        p->setWhammy(a.down);  // a key or button; an analog bar is sampled every frame
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
// The intro after beginSong: extend_track at its song time (game.dta runs
// track_panel do_extend_sequence: track_unfurl, the smashers popping with
// nowbar_1..5, then slide_meter_in with meter_slide; without the menus the
// program plays those sounds at the scripts' times), then the controllers at
// -25 ms, the frets held until then synced (fret downs, no strum) [I: the
// sync follows GH1's FUN_0025c070].
void App::updateIntro(float t) {
    if (!begun_) return;
    if (!extendSent_ && t >= extendAtMs_) {
        extendSent_ = true;
        if (ui_) ui_->gameScript("extend_track");
    }
    if (!ui_) {
        // track_panel.dta do_extend_sequence and hud_panel.dta slide_meter_in [C data].
        static const struct {
            float atMs;
            const char* sfx;
        } kIntro[] = {{0, "track_unfurl"}, {1300, "nowbar_1"}, {1400, "nowbar_2"}, {1500, "nowbar_3"},
                      {1600, "nowbar_4"},  {1700, "nowbar_5"}, {2050, "meter_slide"}};
        while (introSfx_ < int(std::size(kIntro)) && t >= extendAtMs_ + kIntro[introSfx_].atMs)
            playSfxArgs(kIntro[introSfx_++].sfx, fxVolumeDb_, 0, 0);
    }
    if (!controllersLive_ && t > kControllersLiveMs) {
        controllersLive_ = true;
        for (int p = 0; p < 2; ++p) {
            Player* pl = p == 0 ? player_.get() : player2_.get();
            if (!pl || botFor(pl)) continue;
            for (int f = 0; f < 5; ++f)
                if (heldBeforeLive_[size_t(p)] & (1u << f)) pl->fretDown(f, t);
        }
    }
}

// The players' events to their script handlers, which play the game's
// sounds [C]: the PlayerMatcher's miss sink (0x822E2B10: an overstrum or a
// wrong fret, not a passed gem) sends `miss` (player) to beatmatch.dta's
// player_matcher_handlers: miss_gtr, or miss_bass on a bass track; the
// pass sink (0x822E2FD0) looks up miss_whoosh, which no bank on the disc
// has, so a passed gem is silent [C data]. No code sends the handlers'
// `hit` (no "hit" symbol in the executable), so sp_gemhit never plays [C
// by absence]. The Player sends player.dta's handlers phrase_captured
// (sp_awarded 0.2 s later), start_using (sp_deployed, the crowd's
// crowd_upto_good, the track's Star Power bump) and stop_using
// (sp_depleted). Without the menus the program plays the same sounds.
void App::gameSounds(int player, const std::vector<PlayerEvent>& events) {
    const Player* pl = player == 0 ? player_.get() : player2_.get();
    if (!pl) return;
    const bool multi = player2_ != nullptr;
    const float pan = multi ? (player == 0 ? -1.0f : 1.0f) : 0.0f;  // system_script.dta player_pan
    const bool bass = multi && parts_[size_t(player)] == "bass";
    for (const PlayerEvent& e : events) {
        const char* matcher = nullptr;
        const char* handler = nullptr;
        if (e.kind == PlayerEvent::Kind::Miss) matcher = "miss";
        else if (e.kind == PlayerEvent::Kind::PhraseCaptured) handler = "phrase_captured";
        else if (e.kind == PlayerEvent::Kind::StarPowerStart) handler = "start_using";
        else if (e.kind == PlayerEvent::Kind::StarPowerEnd) handler = "stop_using";
        else continue;
        if (ui_) {
            const std::string obj = std::string(matcher ? "player_matcher" : "player") + std::to_string(player);
            ui_->callScript(ui_->interp().findObject(obj), matcher ? matcher : handler, {Value::integer(player)});
            continue;
        }
        if (matcher) playSfxArgs(bass ? "miss_bass" : "miss_gtr", fxVolumeDb_, pan, 0);
        else if (e.kind == PlayerEvent::Kind::PhraseCaptured) pendingAwardMs_ = lastT_ + 200.0f;
        else if (e.kind == PlayerEvent::Kind::StarPowerStart) {
            playSfxArgs(multi ? "sp_deployed_multi" : "sp_deployed", fxVolumeDb_, pan, 0);
            if (track_) track_->animateExtend(2000, 2025, 800, lastT_);
        } else {
            playSfxArgs(multi ? "sp_depleted_multi" : "sp_depleted", fxVolumeDb_, pan, 0);
        }
    }
    if (!ui_ && pendingAwardMs_ > -1e8f && lastT_ >= pendingAwardMs_) {
        pendingAwardMs_ = -1e9f;
        playSfxArgs("sp_awarded", fxVolumeDb_, 0, 0);
    }
}

void App::updateSong(float t) {
    updateIntro(t);
    // The analog whammy bar, sampled every frame (Player's movement detection).
    if (!paused_) {
        if (!player2_) {
            if (!bot_ && input_.analogWhammy()) player_->setWhammyValue(input_.whammyValue(), t);
        } else {
            for (int p = 0; p < 2; ++p) {
                Player& pl = p == 0 ? *player_ : *player2_;
                if (!botFor(&pl) && input_.analogWhammy(pads_[size_t(p)])) pl.setWhammyValue(input_.whammyValue(pads_[size_t(p)]), t);
            }
        }
    }
    const bool over = exploded_ || wonSent_ || lostSent_;
    if (!over && !paused_) {
        keys_.feed(*player_, t);
        if (bot_) bot_->feed(*player_, t);
        if (t >= failAtMs_ && !songLost()) {
            if (coopBand_) coopBand_->forceFail();
            else player_->forceFail();
        }
        player_->update(t);
        if (player2_) {
            if (bot2_) bot2_->feed(*player2_, t);
            player2_->update(t);
        }
        if (coopBand_) coopBand_->poll(t);  // 0x822DEAD0: each co-op player's poll polls the band [C]
    }
    const float dt = std::clamp(float(t - lastFrameMs_), 0.0f, 100.0f);
    lastFrameMs_ = t;
    for (float& f : flashes_) f = std::max(0.0f, f - dt / 150.0f);
    const std::vector<PlayerEvent> events = player_->takeEvents();
    const std::vector<PlayerEvent> events2 = player2_ ? player2_->takeEvents() : std::vector<PlayerEvent>{};
    const std::vector<PlayerEvent> bandEvents = coopBand_ ? coopBand_->takeEvents() : std::vector<PlayerEvent>{};
    for (const PlayerEvent& e : events2) {
        if (e.kind == PlayerEvent::Kind::PhraseCaptured) ++spPhrases_[1];
        if (e.kind == PlayerEvent::Kind::EndStreak) ++streaksBroken_[1];
        if (e.kind == PlayerEvent::Kind::StarPowerReady && ui_) ui_->gameEvent("star_power_ready", {Value::integer(1)});
    }
    // Face-off (0x822CFA70) [C]: the game walks the gem list by time and,
    // when the next gem's player_mask differs from the last, sends
    // active_players_changed (mask) to its sinks (the world's camera
    // facing, the characters). Only face-off has masks (0x823450B0).
    if (mode_ == DevMode::FaceOff && !over) {
        const auto& gems = player_->chart().gems;
        while (activeGem_ < gems.size() && t >= gems[activeGem_].ms) ++activeGem_;
        if (activeGem_ < gems.size() && gems[activeGem_].players != activePlayers_) {
            activePlayers_ = gems[activeGem_].players;
            if (ui_) ui_->gameEvent("active_players_changed", {Value::integer(activePlayers_)});
            else if (world_) world_->message("active_players_changed", {Value::integer(activePlayers_)});
        }
    }
    // Co-op: every engage sends sp_engage (player, state) to the game
    // (0x822C80F8; multi_coop's handler plays sp_deploystart.cue).
    if (ui_ && mode_ == DevMode::Coop)
        for (int p = 0; p < 2; ++p)
            for (const PlayerEvent& e : p == 0 ? events : events2)
                if (e.kind == PlayerEvent::Kind::StarPowerEngage)
                    ui_->gameEvent("sp_engage", {Value::integer(p), Value::integer(e.value > 0 ? 1 : 0)});
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
        if (coopBand_) hud_->onEvents(bandEvents, -1, t);
    }
    gameSounds(0, events);
    if (player2_) gameSounds(1, events2);
    if (!player2_) {
        mixer_.setGuitarAudible(player_->guitarAudible());
    } else if (parts_[0] != parts_[1]) {
        mixer_.setGuitarAudible(player_->guitarAudible());
        mixer_.setGuitar2Audible(player2_->guitarAudible());
    } else {
        // One part for both: the latest hit or miss of either [I].
        for (const auto* list : {&events, &events2})
            for (const PlayerEvent& e : *list) {
                if (e.kind == PlayerEvent::Kind::Hit || e.kind == PlayerEvent::Kind::Hopo) guitarHeard_ = true;
                if (e.kind == PlayerEvent::Kind::Miss || e.kind == PlayerEvent::Kind::EndStreak) guitarHeard_ = false;
                if (e.kind == PlayerEvent::Kind::HeldNoteReleased && e.value < 1.0f) guitarHeard_ = false;
            }
        mixer_.setGuitarAudible(guitarHeard_);
    }
    // The venue: the song's events, its clock, anims and particles.
    if (world_ && venueView_) {
        worldDriver_.update(*world_, *song_, t, player_.get(), events, player2_.get(), events2, coopBand_.get());
        if (songLost()) worldDriver_.gameLost(*world_);
        else if (t >= song_->endMs) worldDriver_.gameWon(*world_);
        world_->update();
        if (lighting_) lighting_->update();
        if (band_) {
            const gfx::Vec3 c = venueView_->camera().position();
            const std::array<float, 3> cam{c.x, c.y, c.z};
            band_->update(world_->seconds(), world_->beats(), &cam);
        }
        if (director_) {
            director_->setStarPower(player_->starPowerActive() || (player2_ && player2_->starPowerActive()));
            director_->update();
            // Development aid: GD03_FORCE_SHOT=<CamShot> plays that shot whenever another starts.
            if (const char* fs = SDL_getenv("GD03_FORCE_SHOT"))
                if (const auto* shot = venue_->find<scene::CamShot>(fs); shot && director_->shot() != shot) {
                    director_->forceShot(shot);
                    director_->update();
                }
            if (director_->hasCamera()) venueView_->setCamera(director_->camera());
        }
        if (begun_ && t <= float(kTrackExtendMs)) {
            const gfx::Vec3 c = venueView_->camera().position();
            if (!introCamSet_) introCamSet_ = true, introCam0_ = c;
            const double dx = c.x - introCam0_.x, dy = c.y - introCam0_.y, dz = c.z - introCam0_.z;
            introCamTravel_ = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        // Development aid: GD03_FACE_CAM=<member>[:<distance>[:<degrees>[:<bone>[:<height>]]]] (guitarist0,
        // singer, ...) frames that member's head (or bone) from the director camera's side, turned
        // <degrees> about it (180: from behind), <height> units above it, to check the faces.
        if (const char* fc = SDL_getenv("GD03_FACE_CAM"); fc && *fc && band_) {
            std::vector<std::string> f;
            for (std::string spec = fc;;) {
                const size_t colon = spec.find(':');
                f.push_back(spec.substr(0, colon));
                if (colon == std::string::npos) break;
                spec = spec.substr(colon + 1);
            }
            const std::string& who = f[0];
            const float dist = f.size() > 1 ? std::strtof(f[1].c_str(), nullptr) : 22.0f;
            const float turn = f.size() > 2 ? std::strtof(f[2].c_str(), nullptr) * 3.14159265f / 180 : 0.0f;
            const float up = f.size() > 4 ? std::strtof(f[4].c_str(), nullptr) : 0.0f;
            scene::Xfm head;
            const bool found = f.size() > 3 && !f[3].empty() ? band_->objectXfm(who, f[3], head)
                                                             : band_->objectXfm(who, "bone_head.mesh", head) || band_->objectXfm(who, "bone_head.trans", head);
            if (found) {
                const gfx::Vec3 target{head.pos[0], head.pos[1], head.pos[2]};
                // From the side the director camera is on, level with the head.
                gfx::Vec3 dir = venueView_->camera().position() - target;
                dir.z = 0;
                if (gfx::dot(dir, dir) < 1e-6f) dir = {0, -1, 0};
                dir = gfx::normalize(dir);
                dir = {dir.x * std::cos(turn) - dir.y * std::sin(turn), dir.x * std::sin(turn) + dir.y * std::cos(turn), 0};
                const gfx::Vec3 eye = target + dir * dist + gfx::Vec3{0, 0, up};
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
    if (!wonSent_ && !lostSent_ && !songLost() && t >= song_->endMs) {
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
    if (!wonSent_ && !lostSent_ && songLost()) {
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
    if (!songLost() && !exploded_ && t >= song_->endMs) {
        exploded_ = true;
        if (track_) track_->explode(t);
        if (track2_) track2_->explode(t);
        if (hud_) hud_->won(t);
    }
    drawSong(t);
    // A failed song closes a little after the fail (GH2's lose screen comes later).
    if (songLost() && failMs_ < -1e8f) failMs_ = t, failTicks_ = SDL_GetTicksNS();
    const float outro = exploded_ && gh2 ? kExplodeOutroMs : kOutroMs;
    if (!paused_ && (t > song_->endMs + outro || (songLost() && t > failMs_ + 3000))) endSong();
}

// One frame with GH2's menus: the scripted keys, the UI's update (which
// may start or end the song), the song's rules, then the UI's drawing (the
// track and HUD are its game panels).
void App::frameUi() {
    const double now = double(SDL_GetTicksNS()) / 1e6 - uiStartMs_;
    updateCursor();
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
        } else if (k.mouse) {  // the mouse, as its events would (menu units)
            if (k.mouse >= 4) menuWheel(k.mouse == 4 ? -1 : 1);
            else menuMouse(k.mx, k.my, k.mouse == 2 ? 1 : k.mouse == 3 ? 3 : 0);
        } else if (k.padAxis >= 0) {
            if (SDL_Joystick* j = virtualPad_ ? SDL_GetJoystickFromID(virtualPad_) : nullptr)
                SDL_SetJoystickVirtualAxis(j, k.padAxis, Sint16(std::clamp(k.axisValue, -32768, 32767)));
            else
                SDL_Log("GD03_KEYS: a \"j\" item needs GD03_VIRTUAL_PAD");
        } else if (k.padButton >= 0) {
            if (SDL_Joystick* j = virtualPad_ ? SDL_GetJoystickFromID(virtualPad_) : nullptr)
                SDL_SetJoystickVirtualButton(j, k.padButton, k.edge > 0);
            else
                SDL_Log("GD03_KEYS: a \"j\" item needs GD03_VIRTUAL_PAD");
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
    // GD03_SCREENSHOT_AT: wall-clock ms in the menus, "s<ms>" song times.
    if (captureMenuScreenshot(canvas_, now, player_ && begun_ ? double(lastT_) : -1e9)) running_ = false;
    canvas_.end();
}

// Development aid: GD03_SHOTS=<dir> (or the walk test's GD03_WALK_SHOTS)
// saves each screen the menus reach, once settled, as <dir>/NN_<screen>.bmp.
void App::shootScreens() {
    const char* dir = SDL_getenv("GD03_WALK_SHOTS");
    if (!dir) dir = SDL_getenv("GD03_SHOTS");
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
    const bool autoplay = setup.autoplay || options_.autoplay || envOn("GD03_AUTOPLAY") || (walk_ && walk_->autoplay());
    // The mode and the players (docs/multiplayer.md).
    mode_ = setup.numPlayers > 1 ? modeOf(setup.mode) : setup.mode == "practice" ? DevMode::Practice : DevMode::Single;
    if (twoPlayers(mode_) != (setup.numPlayers > 1)) mode_ = DevMode::Single;
    difficulties_ = {std::clamp(setup.difficulty, 0, kDifficultyCount - 1), std::clamp(setup.difficulties[1], 0, kDifficultyCount - 1)};
    parts_ = setup.parts;
    pads_ = setup.pads;
    input_.setLefty(setup.leftys[0] || setup.lefty, setup.leftys[1]);
    if (!loadSong(*it, setup.difficulty, autoplay, setup.bandVolume, setup.guitarVolume, setup.fastIntro)) {
        error = message_;
        return false;
    }
    if (track_) track_->setLefty(setup.lefty || setup.leftys[0]);
    if (track2_) track2_->setLefty(setup.leftys[1]);
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
    // The track's Star Power phrase effects (--walktest star_phrase).
    if (const TrackView* tv = p == 1 ? track2_.get() : track_.get()) {
        if (key == "star_smashes") return tv->starSmashes();
        if (key == "star_glow_lines") return tv->starGlowLines();
        if (key == "star_combos") return tv->comboStarts();
    }
    return 0;
}

void App::trackCommand(const std::string& msg, const std::vector<Value>& args) {
    // A player's extend anim (player.dta start_using's Star Power bump) or
    // the panel's (game.dta beat with the barf cheat): animate (range a b)
    // (period s) [(units u)]; the panel's own range 0..1920 is the extend
    // sequence the track view times itself (render/track_view.h).
    if (msg == "player_animate" || msg == "animate") {
        float from = -1, to = -1, period = 0;
        for (size_t k = msg == "animate" ? 0 : 1; k < args.size(); ++k) {
            const Value& v = args[k];
            if (v.type != Value::Type::Array || !v.array || v.array->empty()) continue;
            const std::vector<Value>& l = *v.array;
            const std::string key = l[0].str();
            if (key == "range" && l.size() >= 3) from = l[1].number(), to = l[2].number();
            if (key == "period" && l.size() >= 2) period = l[1].number();
        }
        if (from < 1920 || to < from) return;
        const int p = msg == "animate" ? -1 : args.empty() ? 0 : args[0].integerValue();
        if (track_ && p != 1) track_->animateExtend(from, to, period * 1000.0f, lastT_);
        if (track2_ && p != 0) track2_->animateExtend(from, to, period * 1000.0f, lastT_);
        return;
    }
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
// GD03_POST=0; the track and HUD then go on top, as GH2's final pass leaves
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
                                          mode_ == DevMode::FaceOff      ? "multi_vs"
                                          : mode_ == DevMode::ProFaceOff ? "multi_fo"
                                          : mode_ == DevMode::Coop       ? "multi_coop"
                                                                         : "single",
                                          parts_);
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
    if (begun_ && player_ && songSfx_.size() < 256) songSfx_.emplace_back(name, double(lastT_));
    // Development aid: GD03_SFXLOG=1 logs every sound the scripts or the game play.
    static const bool sfxLog = envOn("GD03_SFXLOG");
    if (sfxLog) SDL_Log("sfx: %s (%.1f dB, pan %.2f)%s", name.c_str(), double(volumeDb), double(pan), begun_ && player_ ? (" at song " + std::to_string(int(lastT_)) + " ms").c_str() : "");
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
// menus the song's quickplay one). GD03_CHAR=<outfit>, GD03_GUITAR=<guitar>
// and GD03_SKIN=<skin> override player 0's; GD03_NO_BAND=1 leaves the
// stage empty.
void App::startBand(const SongInfo& info, const Song& song, bool backdrop) {
    band_.reset();
    if (!world_ || envOn("GD03_NO_BAND")) return;
    Band::Config cfg;
    cfg.players = backdrop ? 1 : twoPlayers(mode_) ? 2 : 1;
    // Co-op: a player on the bass plays the bass track (bass_strum) [C data: char_objects.dta].
    if (!backdrop && mode_ == DevMode::Coop)
        for (int p = 0; p < 2; ++p) cfg.bass[size_t(p)] = parts_[size_t(p)] == "bass";
    if (!backdrop) {
        cfg.character = bandOutfit_, cfg.guitar = bandGuitar_, cfg.skin = bandSkin_;
        cfg.encore = encoreCheck_, cfg.campaignFinal = campaignFinal_;
        if (const char* c = SDL_getenv("GD03_CHAR")) cfg.character[0] = c;
        if (const char* g = SDL_getenv("GD03_GUITAR")) cfg.guitar[0] = g, cfg.skin[0].clear();
        if (const char* k = SDL_getenv("GD03_SKIN")) cfg.skin[0] = k;
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

// world_panel's milo: ../world/<v>/<v>[_mp|_coop].milo (GD03_VENUE replaces the venue).
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
    float speed = trackSpeed();
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
    std::vector<PerformerState> players{player_->state()};
    if (player2_) players.push_back(player2_->state());
    const PerformerState band = coopBand_ ? coopBand_->state() : PerformerState{};
    hud_->draw(canvas_.renderer(), *sceneCache_, canvas_, players, coopBand_ ? &band : nullptr, lastT_);
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
        auto s = std::make_unique<VgsStream>(data_->read(it->audio.audioPath));
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
    // config/synth.dta (metamusic (volume -7) (music metaloop_bangyourhead ...))
    // [C data]: a random loop from sfx/streams.
    static const char* const kLoops[] = {"metaloop_bangyourhead", "metaloop_iran", "metaloop_noonelikeyou", "metaloop_wegotthebeat",
                                         "metaloop_shakin", "metaloop_radarlove", "metaloop_holydiver"};
    const std::string name = kLoops[SDL_GetTicks() % (sizeof kLoops / sizeof kLoops[0])];
    try {
        auto s = std::make_unique<VgsStream>(data_->read("sfx/streams/" + name + ".vgs"));
        const int ch = s->channels();
        std::vector<Mixer::Track> tracks(static_cast<size_t>(ch));
        for (int c = 0; c < ch; ++c) tracks[size_t(c)].pan = ch == 2 ? (c == 0 ? -1.0f : 1.0f) : 0.0f, tracks[size_t(c)].volume = 0.4467f;  // -7 dB
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
        auto s = std::make_unique<VgsStream>(data_->read("sfx/streams/credits.vgs"));
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
    if (::playMovie(*data_, name, canvas_, mixer_, input_) == MoviePlayer::Result::Quit) running_ = false;
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
    if (!SDL_getenv("GD03_LEFTY")) input_.setLefty(lefty0, lefty1);
}

// ---- the Enhanced Options (ui/enhanced_panel.h)

namespace {

std::string enhancedPath() {
    const char* dir = SDL_getenv("GD03_SAVE_DIR");
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
            if (j.type == Json::Type::Object && j["format"].str() == "gd03-enhanced") enhanced_ = j;
        } catch (const std::exception& e) {
            SDL_Log("enhanced options: %s: %s (the defaults are used)", enhancedPath().c_str(), e.what());
        }
    }
    applyEnhanced();
}

// The Steam Deck defaults (gd01's "Steam Deck" column, gd-game-01/README.md):
// full screen, 60 FPS, no FPS counter, 800 lines; the Steam Deck download
// (-DGD03_STEAM_DECK=ON) and a launcher that sees a Deck (GD03_HANDHELD=1,
// module_entry.cpp). Saved settings still win.
static const char* deckDefault(const std::string& key) {
#if defined(GD03_STEAM_DECK)
    const bool deck = true;
#else
    const char* h = SDL_getenv("GD03_HANDHELD");
    const bool deck = h && h[0] == '1';
#endif
    if (!deck) return nullptr;
    if (key == "screen_mode") return "fullscreen";
    if (key == "max_fps") return "60";
    if (key == "fps_counter") return "off";
    if (key == "internal_res") return "800";
    return nullptr;
}

std::string App::enhancedValue(const std::string& key) {
    const ui::EnhancedSetting* e = enhancedSetting(key);
    if (!e) return {};
    const std::string& v = enhanced_[key].str();
    for (const char* ok : e->values)
        if (v == ok) return v;
    if (const char* d = deckDefault(key)) return d;
    return e->values.front();  // unset or not valid: the default
}

void App::setEnhancedValue(const std::string& key, const std::string& value) {
    if (!enhancedSetting(key)) return;
    enhanced_.set("format", Json::string("gd03-enhanced"));
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
    if (!SDL_getenv("GD03_FPS")) dev.setFrameCap(std::clamp(std::atoi(enhancedValue("max_fps").c_str()), 30, 1000));
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
        if (!SDL_getenv("GD03_FPS")) dev.setFrameCap(std::clamp(std::atoi(enhancedValue("max_fps").c_str()), 30, 1000));  // the new mode's refresh rate
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

// The backdrop: the second venue (venues::names()[1], small1) with the band
// caught a quarter into the first song of its tier, silent and still.
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
    backdrop_ = std::move(b);
    // The band first: it enters with the world (World::start), as in a song.
    startBand(*backdrop_->info, *backdrop_->song, true);
    world_->start(false, false);
    world_->message("excitement", {Value::integer(3)});
    // About 1.5 s of the song in fixed steps poses the band and frames the
    // shot; then nothing moves again (gd01's backdrop). A band that has not
    // shown its singer yet gets up to 5 s more: the backdrop must always
    // show the band (GD03-074).
    for (int i = 0; backdrop_ && (i < 45 || (!backdrop_->framed && i < 195)); ++i) updateBackdrop();
    if (!band_) SDL_Log("enhanced options: backdrop %s has no band", venue.c_str());
    else if (backdrop_ && !backdrop_->framed) SDL_Log("enhanced options: backdrop %s: the singer never showed", venue.c_str());
    if (backdrop_) backdrop_->frozen = true;
}

// The backdrop's band is there, the camera on its singer, and the singer's
// head in the right half of the picture (the Enhanced walk's check).
bool App::menuBackdropFramed() {
    if (!backdrop_ || !backdrop_->framed || !band_ || !venueView_) return false;
    scene::Xfm head;
    if (!band_->objectXfm("singer", "bone_head.mesh", head)) return false;
    const gfx::Camera& cam = venueView_->camera();
    const gfx::Mat4 vp = cam.viewProj(false);
    float c[4];
    for (int r = 0; r < 4; ++r) c[r] = vp.at(r, 0) * head.pos[0] + vp.at(r, 1) * head.pos[1] + vp.at(r, 2) * head.pos[2] + vp.at(r, 3);
    return c[3] > 0 && c[0] / c[3] > 0 && c[0] / c[3] < 1 && std::fabs(c[1] / c[3]) < 1;
}

bool App::drawMenuBackdrop() {
    if (!backdrop_ || !worldDraw_) return false;
    drawWorld();
    return true;
}

void App::updateBackdrop() {
    Backdrop& b = *backdrop_;
    if (!world_ || !venueView_ || b.frozen) return;
    b.simMs += 33;
    const float ms = b.fromMs + b.simMs;
    b.driver.update(*world_, *b.song, ms, nullptr, {}, nullptr, {});
    world_->update();
    // The lights: the venue's chorus and verse presets in turn, 20 s each,
    // not the song's cues (their blackouts left the band in the dark).
    if (lighting_) {
        const int part = 0;
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
    // The shot (guidelines/universal_launcher.md, gd01's Enhanced pose): a
    // still close-up of the singer, on the right of the picture beside the
    // options' box, from the audience's side a little to one flank so the
    // rest of the band stands behind and beside them.
    if (!b.framed && band_) {
        scene::Xfm singer, other;
        if (band_->objectXfm("singer", "bone_head.mesh", singer)) {
            b.target = {singer.pos[0], singer.pos[1], singer.pos[2] - 4};
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
            b.spread = spread;
            // Close on the singer, with the band behind: about two of the
            // band's half widths away [I: the port's framing].
            b.dist = std::clamp(spread * 2.6f, 140.0f, 220.0f);
            const gfx::Vec3 shot = venueView_->camera().position() - b.target;
            // No farther than the venue's shot of the moment (the audience's
            // fences and rigging stand behind it), but never into the band:
            // a close shot at that moment put the camera inside the singer
            // and the backdrop showed no one.
            b.dist = std::max(110.0f, std::min(b.dist, 0.8f * std::sqrt(shot.x * shot.x + shot.y * shot.y)));
            SDL_Log("enhanced options: backdrop on %s, spread %.0f, distance %.0f", b.info->id.c_str(), spread, b.dist);
            b.framed = true;
        }
    }
    if (b.framed) {
        // Following the singer while the band poses (then frozen).
        if (scene::Xfm singer; band_ && band_->objectXfm("singer", "bone_head.mesh", singer))
            b.target = b.target + (gfx::Vec3{singer.pos[0], singer.pos[1], singer.pos[2] - 4} - b.target) * (1 - std::exp(-0.033f / 0.3f));
        constexpr float kYaw = -0.3f;                 // the camera this far round to the singer's side (radians)
        constexpr float kFocusX = 0.3f, kFocusY = 0.25f;  // where the singer's face lands, in NDC (right, up)
        constexpr float kFov = 0.6f;
        const gfx::Vec3 d{b.dir.x * std::cos(kYaw) - b.dir.y * std::sin(kYaw), b.dir.x * std::sin(kYaw) + b.dir.y * std::cos(kYaw), 0};
        gfx::Vec3 eye = b.target + d * b.dist;
        eye.z = b.target.z + b.dist * 0.06f;
        // Look to the left of and below the singer so the face sits at the focus.
        const float t = std::tan(kFov * 0.5f), aspect = 16.0f / 9.0f;
        const gfx::Vec3 fwd = gfx::normalize(b.target - eye);
        const gfx::Vec3 right = gfx::normalize(gfx::cross(fwd, gfx::Vec3{0, 0, 1}));
        const gfx::Vec3 up = gfx::cross(right, fwd);
        const float range = std::sqrt(gfx::dot(b.target - eye, b.target - eye));
        gfx::Camera cam = gfx::Camera::lookAt(eye, b.target - right * (kFocusX * range * t * aspect) - up * (kFocusY * range * t));
        cam.yFov = kFov;
        venueView_->setCamera(cam);
    }
    venueView_->update();
}

// One frame of the song screen without the menus (--song, the debug list):
// GH2's track and HUD (or the debug highway), then the 2D text on top.
// GD03_PERF=1 (to measure on the Mac): per frame on average over the
// period, where the time goes: update (input, the song, the world, the band,
// the menus' scripts; what is left), draw (building and issuing the draws on
// the CPU), present (the internal frame's blit, the pacing's sleep and
// SDL_GL_SwapWindow, where a driver waits for the GPU or the display) and
// gpu wait (the fence on the frame before last: the GPU is the bottleneck).
void App::logPerf(uint64_t periodNs, int frames, uint64_t maxNs, uint64_t limitNs) {
    gfx::RenderDevice& dev = canvas_.renderer().device();
    const gfx::DevicePerf p = dev.takePerf();
    const double f = double(std::max(frames, 1)), ms = 1e-6;
    const double frameMs = double(periodNs) * ms / f;
    const double drawMs = double(canvas_.drawNs) * ms / f, presentMs = double(p.presentNs) * ms / f, fenceMs = double(p.fenceNs) * ms / f;
    const double limitMs = double(limitNs) * ms / f;
    const double updateMs = std::max(0.0, frameMs - drawMs - presentMs - fenceMs - limitMs);
    int pw = 0, ph = 0, dw = 0, dh = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    dev.drawableSize(dw, dh);
    const double pf = double(std::max(p.frames, 1));
    SDL_Log("perf: %.1f fps (frame %.2f ms, worst %.1f) | update %.2f ms | draw %.2f ms: %.0f draws, %.0f KB uniforms, %.0f KB vertices, "
            "%.0f KB textures, %.1f created | present %.2f ms (pacing %.2f) | gpu wait %.2f ms | window %dx%d px, drawn at %dx%d, display scale %.2f",
            f * 1e9 / double(periodNs), frameMs, double(maxNs) * ms, updateMs, drawMs, double(p.draws) / pf, double(p.uniformBytes) / pf / 1024,
            double(p.bufferBytes) / pf / 1024, double(p.textureBytes) / pf / 1024, double(p.created) / pf, presentMs, double(p.paceNs) * ms / f, fenceMs, pw, ph, dw, dh,
            double(SDL_GetWindowDisplayScale(window_)));
    canvas_.drawNs = 0, canvas_.drawnFrames = 0;
}

void App::drawSong(float t) {
    const bool gh2 = track_ && hud_ && !debugHighway_;
    canvas_.begin(gh2 ? SDL_Color{0, 0, 0, 255} : SDL_Color{12, 10, 16, 255});
    const std::string title = info_->name + " - " + info_->artist + "  [" + difficultyName(static_cast<Difficulty>(difficulty_)) +
                              (bot_ ? ", autoplay]" : "]");
    const float speed = trackSpeed();
    if (gh2) {
        const uint64_t drawStartNs = SDL_GetTicksNS();
        drawWorld();
        // Two players: the track speed is the mean of their difficulties'
        // (0x826932D0) [C].
        // Development aid: GD03_TRACK=0 hides the track (to look at the band).
        const char* showTrack = SDL_getenv("GD03_TRACK");
        if (!showTrack || *showTrack != '0') {
            track_->draw(canvas_.renderer(), *sceneCache_, *song_, *player_, t, speed);
            if (track2_ && player2_) track2_->draw(canvas_.renderer(), *sceneCache_, *song_, *player2_, t, speed);
        }
        // Development aid: GD03_HUD=0 hides the HUD (to look at the track).
        const char* showHud = SDL_getenv("GD03_HUD");
        std::vector<PerformerState> players{player_->state()};
        if (player2_) players.push_back(player2_->state());
        const PerformerState band = coopBand_ ? coopBand_->state() : PerformerState{};
        if (!showHud || *showHud != '0') hud_->draw(canvas_.renderer(), *sceneCache_, canvas_, players, coopBand_ ? &band : nullptr, t);
        // Development aid: GD03_STATS=1 prints the frame rate and particles.
        if (SDL_getenv("GD03_STATS")) {
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
    if (songLost() && !gh2) canvas_.textCentered(kScreenW * 0.5f, 330, 4, {230, 60, 60, 255}, "SONG FAILED");
    else if (paused_) canvas_.textCentered(kScreenW * 0.5f, 330, 4, {235, 235, 240, 255}, "PAUSED");
    if (captureDebugScreenshot(canvas_, true, t)) running_ = false;
    canvas_.end();
}

// The first screen, shown until there is a disc image. All its drawing is
// here, in one place, for the renderer that replaces the debug canvas.
void App::frameNeedData() {
    if (SDL_getenv("GD03_DIALOG") && !picker_.busy() && dataMessage_.empty()) picker_.open(window_);  // headless tests
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

// The debug song list (GD03_DEBUG_MENU=1, or when GH2's menus cannot start).
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
