// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <random>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app_ids.h"
#include "audio/audio_device.h"
#include "audio/mixer.h"
#include "audio/synth.h"
#include "char/midi_parser.h"
#include "data/dta.h"
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
#include "render/core_backend.h"
#include "render/hdr_output.h"
#include "render/highway.h"
#include "render/hud_view.h"
#include "render/scene_gpu.h"
#include "render/track_view.h"
#include "render/anim_rate.h"
#include "render/rb_hud_view.h"
#include "render/rb_track_view.h"
#include "render/vocal_track_view.h"
#include "render/crowd_view.h"
#include "audio/vocal_input.h"
#include "game/vocal_player.h"
#include "game/rb_bonus.h"
#include "render/band_view.h"
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
// the menus, development aid GD05_MODE=faceoff|proface|coop|practice plays
// that mode with a second, autoplaying player (GD05_DIFF2=0..3 its
// difficulty, GD05_PARTS=<p0>,<p1> the co-op parts, e.g. guitar,bass).
enum class DevMode { Single, FaceOff, ProFaceOff, Coop, Practice };
DevMode devMode() {
    const char* m = SDL_getenv("GD05_MODE");
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
std::vector<Mixer::Track> mixerTracks(const SongAudio& a, const std::array<std::string, 2>* parts = nullptr) {
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
        tracks[c].volume = c < a.vols.size() ? std::pow(10.0f, a.vols[c] / 20.0f) : 1.0f;
        tracks[c].songPart = true;
    }
    return tracks;
}

// A player's chart: the instrument's PART track (songs.dtb's instrument
// symbols guitar / bass / drum / vocals) [C data].
Part partFor(const Song& song, const std::string& instrument, bool coop) {
    (void)coop;
    if (instrument == "bass" && song.hasPart[size_t(Part::Bass)]) return Part::Bass;
    if ((instrument == "drum" || instrument == "drums") && song.hasPart[size_t(Part::Drums)]) return Part::Drums;
    if (instrument == "vocals" && song.hasPart[size_t(Part::Vocals)]) return Part::Vocals;
    return Part::Guitar;
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

// The menus' music streams (a song's preview, the meta loops, the credits)
// open off the frame: reading, deciphering and opening a song's mogg took
// 100..240 ms on the main thread, a stutter at every preview the song list
// started (GD05-073). RB streams them from the disc too. One stream opens
// at a time; a request while one opens replaces the waiting one (scrolling
// down the list), and only the latest request is played.
class MusicLoader {
public:
    struct Request {
        std::string key, arkPath;
        double startMs = 0, endMs = 0, fadeMs = 0;
    };
    ~MusicLoader() { join(); }
    void request(const GameData& data, Request r) {
        data_ = &data;
        want_ = std::move(r);
        wanted_ = true;
        if (!thread_.joinable() && !legacyDone_) start();
    }
    void cancel() { wanted_ = false; }
    // The stream of the latest request once it is open (its key in `key`), else null.
    std::unique_ptr<LoopSource> poll(std::string& key) {
        std::unique_ptr<LoopSource> out;
        if ((thread_.joinable() || legacyDone_) && done_) {
            if (thread_.joinable()) thread_.join();
            legacyDone_ = false;
            if (!error_.empty()) SDL_Log("music %s: %s", job_.key.c_str(), error_.c_str());
            if (wanted_ && result_ && job_.key == want_.key) out = std::move(result_), key = job_.key, wanted_ = false;
            result_.reset();
        }
        if (wanted_ && !thread_.joinable() && !legacyDone_) start();
        return out;
    }
    void join() {
        if (thread_.joinable()) thread_.join();
        result_.reset();
    }

private:
    void start() {
        job_ = want_;
        done_ = false;
        error_.clear();
        if (SDL_getenv("GD05_MUSIC_LEGACY")) {  // the code before: opened on the frame that asks
            try {
                result_ = std::make_unique<LoopSource>(std::make_unique<VorbisStream>(data_->read(job_.arkPath)), job_.startMs, job_.endMs, job_.fadeMs);
            } catch (const std::exception& e) {
                error_ = e.what();
            }
            done_ = true;
            legacyDone_ = true;
            return;
        }
        thread_ = std::thread([this, job = job_, data = data_] {
            try {
                auto s = std::make_unique<VorbisStream>(data->read(job.arkPath));
                result_ = std::make_unique<LoopSource>(std::move(s), job.startMs, job.endMs, job.fadeMs);
            } catch (const std::exception& e) {
                error_ = e.what();
            }
            done_ = true;
        });
    }
    const GameData* data_ = nullptr;
    Request want_, job_;
    bool wanted_ = false;
    std::thread thread_;
    std::atomic<bool> done_{false};
    bool legacyDone_ = false;
    std::unique_ptr<LoopSource> result_;
    std::string error_;
};

// Enhanced > Graphics / Audio Backend as this start of the game uses them
// (startupGraphicsBackend / startupAudioBackend in app.h): read from
// enhanced.json once, at the start of App::run (each restart reads it again).
struct StartupBackends {
    bool latched = false;
    std::string graphics = "local", audio = "local";
};
StartupBackends& startupBackends() {
    static StartupBackends b;
    return b;
}
void latchStartupBackends();
// The walk test's own save folder (GD05_SAVE_DIR), made once a process: a
// restart (Restart Now) keeps it.
void prepareWalkDirs();

class App final : public UiHost {
public:
    explicit App(const Options& o) : options_(o), difficulty_(std::clamp(o.difficulty, 0, kDifficultyCount - 1)) {}
    int run();
    bool restartAsked() const { return restart_; }

    // ---- UiHost (GH2's menus around the song)
    const std::vector<SongInfo>& songs() const override { return songs_; }
    bool gameLoad(const GameSetup& setup, std::string& error) override;
    void gameBegin() override;
    bool gameDeviceLost(int slot) const override { return slot >= 0 && slot < 2 && hadDevice_[slot] && !input_.padConnected(slot); }
    void gameEnd() override { endSong(); }
    void gameForceLose() override { failAtMs_ = -1e30f; }
    void gameForceWin() override {
        if (song_) song_->endMs = std::min(song_->endMs, lastT_);
    }
    bool gameLoaded() const override { return player_ != nullptr; }
    void gameSetPaused(bool paused) override {
        paused_ = paused;
        mixer_.setPaused(paused || musicPaused_);
        if (world_) world_->crowd().audio().setPaused(paused);
        if (synth_) synth_->setPaused(paused);
    }
    bool gamePaused() const override { return paused_; }
    bool musicPaused() const override { return musicPaused_; }
    double gameTimeMs() const override { return lastT_; }
    std::vector<std::pair<std::string, double>> gameSfx() const override { return songSfx_; }
    std::vector<std::string> sfxHistory() const override { return sfxHistory_; }
    double introCameraTravel() const override { return introCamTravel_; }
    float gameBeat() const override {
        return song_ ? float(song_->tempo.msToTick(std::max(0.0f, lastT_)) / std::max(1, song_->tempo.division())) : 0.0f;
    }
    float songDurationMs() const override { return song_ ? song_->endMs : 0.0f; }
    std::vector<Section> songSections(const std::string& song, const std::string& part, int difficulty, Section& full) override;
    float gameBackgroundDb() const override { return mode_ == DevMode::Practice ? practiceBackgroundDb() : 0.0f; }
    std::string gameAudioPath() const override { return audio_ ? audio_->moggPath : std::string(); }
    bool playVoice(const std::string& path, float volumeDb) override;
    bool micConnected() override {
        if (mic_ || SDL_getenv("GD05_VOCALS_FILE")) return true;
        // Probed at most every 5 s (opening a capture device is slow).
        const double now = double(SDL_GetTicksNS()) / 1e9;
        if (now - micProbeAt_ > 5.0) {
            micProbeAt_ = now;
            VocalInput probe;
            std::string err;
            micProbed_ = probe.open(startupAudioBackend() == "miniaudio" ? "miniaudio" : "sdl3", err);
        }
        return micProbed_;
    }
    bool micTalking() override { return mic_ && micLoud_; }
    double micProbeAt_ = -1e9;
    bool micProbed_ = false, micLoud_ = false;
    bool voicePlaying() override;
    void stopVoice() override;
    void pauseVoice(bool paused) override;
    unsigned playerFrets(int player) override { return player >= 0 && player < 2 ? padFrets_[size_t(player)] : 0u; }
    int playerStrums(int player) override { return player >= 0 && player < 2 ? padStrums_[size_t(player)] : 0; }
    const Player* gamePlayer(int index) const override {
        if (index >= 1 && size_t(index - 1) < seats_.size()) return seats_[size_t(index - 1)].player.get();
        return index == 0 ? player_.get() : player2_.get();
    }
    SongResult lastResult(int index) const override { return index >= 0 && index < 2 ? lastResult_[size_t(index)] : SongResult{}; }
    std::array<SongResult, 2> lastResult_{};
    const BandPerformer* gameBand() const override { return coopBand_.get(); }
    int gameCounter(int player, const std::string& key) const override;
    // GD05_FORCE_SHOT: a CamShot by name, or #N the venue's N-th BandCamShot by name.
    const scene::CamShot* forcedShot(const std::string& spec) const;
    void setPlayerSwitch(int index, const std::string& name, bool on) override {
        Player* p = index == 0 ? player_.get() : player2_.get();
        if (!p) return;
        if (name == "whammy_star_power") p->setWhammyStarPowerEnabled(on);
        else if (name == "mercury_switch") p->setMercurySwitchEnabled(on);
    }
    void setPlayerStarPower(int index, const std::string& what, float value) override {
        Player* p = index == 0 ? player_.get() : player2_.get();
        if (!p) return;
        // GD05-073's Overdrive walks: the singer's meter, and a shout (loud, no pitch) for `value` ms.
        if (what == "vocal_meter") {
            if (vocal_) vocal_->setOverdriveMeter(value);
            return;
        }
        if (what == "vocal_shout") {
            voiceShoutUntilMs_ = lastT_ + value;
            return;
        }
        if (what == "meter") p->setStarMeter(value);
        else if (what == "deploy_rate") p->setDeployRate(value);
        else if (what == "phrase_boost") p->setPhraseBoost(value);
    }
    void setMusicSpeed(float speed) override { mixer_.setSpeed(std::max(0.0f, speed)); }
    void setMuteFx(bool mute) override {
        fxMuted_ = mute;
        if (synth_) synth_->setFxVolumeDb(mute ? -96.0f : fxVolumeDb_);
    }
    void setMusicVolume(float db, float fadeMs) override { mixer_.setVolumeDb(db, fadeMs); }
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
    void menuMovie(const std::string& name) override {
        if (name.empty() || !data_) return menuMovie_.close();
        if (name == menuMovie_.name()) return;
        std::string error;
        if (!menuMovie_.open(*data_, name, error)) SDL_Log("menu movie %s: %s", name.c_str(), error.c_str());
    }
    bool drawMenuMovie() override { return menuMovie_.draw(canvas_); }
    void applyOptions(float band, float guitar, float fx, bool lefty0, bool lefty1, int syncOffsetMs) override;
    bool padConnected(int pad) override { return input_.padConnected(pad); }
    int controllerType(int pad) override;
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
    void enhancedAction(const std::string& action) override;
    std::string enhancedStatus() override { return userFilesStatus_; }
    int enhancedState(const std::string& key) override;
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
    // --inputcapture (GD05-058): no disc; every raw controller / key event and
    // the song input path's result to input-capture.txt, on screen too.
    void startCapture();
    void frameCapture();
    bool capture_ = false;
    std::ofstream captureOut_;
    std::string captureFile_;
    std::vector<std::string> captureTail_;
    std::array<int, 8> captureCounts_{};  // strums, frets 1..5, whammy, Overdrive
    bool loadSong(const SongInfo& info, int difficulty, bool autoplay, float band, float guitar, bool fastIntro);
    void beginSong();
    bool startSong(size_t index);
    void endSong();
    void setupPractice();
    float practiceBackgroundDb() const;
    void makeGameParsers();
    void dropGameParsers();
    void loadGameParsers(const SongAudio& audio);
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
    bool inputLog_ = false;  // GD05_INPUTLOG=1: every input action
    double mouseMovedMs_ = -1e9;
    void playerAction(const InputEvent& a);
    void updateSong(float t);
    void updateIntro(float t);
    void gameSounds(int player, const std::vector<PlayerEvent>& events);
    void gameCallbacks(int player, const std::vector<PlayerEvent>& events, size_t k);
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
    void saveEnhanced();
    void applyEnhanced();
    // The Enhanced Options that change the picture (post-processing, the
    // camera, particles, lights), on the renderer, the venue and the band.
    void applyEnhancedLook();
    void enhancedFrame(gfx::Renderer& r);
    VenueView::EnhancedLook enhancedLook_;
    float starBlur_ = 0;
    double starBlurAt_ = 0;
    void pollUserFiles();
    void exportUserFiles(const std::string& dir);
    bool importSettings(const std::string& file);
    bool importProgress(const std::string& file);
    void updateBackdrop();
    void drawFpsCounter();
    // Enhanced Options only: "Graphics: X   Audio: Y", the backends this run uses.
    void drawBackendLabel();
    // The graphics backend that draws now (after any fallback): "local" until
    // the gd-engine-core renderer lands (GDCORE-008; the graphics work returns
    // the device's real id here).
    std::string graphicsBackendNow() const { return gfx::backendId(gfx::rendererBackend()); }  // what draws (after a fallback)
    // The audio backend that plays now (after any fallback).
    static std::string audioBackendNow() {
        const std::string b = audiodev::backend();
        return b.empty() ? std::string("local") : b;
    }
    // Enhanced Options > Enable Touch Controls: the track's lanes for the
    // fingers (Input::setTouchLanes) and the on-screen buttons.
    void updateTouchLanes();
    void drawTouchButtons();
    void frameList();
    bool prepareTrack();
    void stopMusic();
    void shootScreens();
    void recordSfx(const std::string& name);  // UiHost::gameSfx / sfxHistory

    Options options_;
    int difficulty_;
    SDL_Window* window_ = nullptr;
    Canvas canvas_;
    Mixer mixer_;
    std::unique_ptr<GameData> data_;
    Rules rules_;
    std::vector<SongInfo> songs_;
    bool running_ = true;
    bool restart_ = false;  // Enhanced's Restart Now: runApp starts the game again
    int exitCode_ = 0;
    // The first screen (no disc image yet): the file dialog and its last error.
    DiscPicker picker_;
    std::string dataMessage_;

    // GH2's front end (ui/ui_manager.h); GD05_DEBUG_MENU=1 shows the debug
    // song list instead.
    std::unique_ptr<ui::UiManager> ui_;
    bool debugMenu_ = false;
    double uiStartMs_ = 0;
    MenuKeys menuKeys_;
    int escapes_ = 0;  // Back presses in a row on the main menu (three quit)
    SDL_JoystickID virtualPad_ = 0;  // GD05_VIRTUAL_PAD's controller (GD05_KEYS "j" items)
    std::unique_ptr<WalkDriver> walk_;
    size_t shotsTaken_ = 0;
    double shotDue_ = -1;
    std::string shotScreen_;

    // The song list.
    size_t cursor_ = 0;
    std::string message_;

    // The song being played.
    const SongInfo* info_ = nullptr;
    const SongAudio* audio_ = nullptr;  // its audio block (song, song_coop)
    // The mode's song_block and skip_intro (GameSetup); practice's speed,
    // its sections' ticks and the song time practice_end comes at.
    std::string gh2Mode_;  // the menus' mode (modes.dta)
    std::string songBlock_;
    bool skipIntro_ = false;
    float practiceSpeed_ = 1;
    int practiceStartTick_ = -1, practiceEndTick_ = -1;
    float practiceStartMs_ = 0, practiceEndMs_ = 1e30f;
    bool practiceEndSent_ = false;
    size_t gameBeat_ = 0;  // the next beat line beat / downbeat go to the game for
    // The game's own MIDI parsers (MidiParserMgr, PS2 0x0023f738):
    // events_parser (the EVENTS track's text events as messages: practice's
    // section names) and click_parser (TRIGGERS 24..26: practice's
    // metronome); the band's are its own (game/band.h).
    std::unique_ptr<MidiParserSet> gameParsers_;
    // The tutorial's voice-over (one at a time) and the players' frets
    // held and strums (UiHost::playerFrets / playerStrums).
    SfxPlayer::VoiceId voice_ = 0;
    double voiceEndMs_ = 0;  // without a device: when it would have ended
    std::array<unsigned, 2> padFrets_{};
    std::array<int, 2> padStrums_{};
    std::unique_ptr<MidiParser> eventsParser_;
    DataNode eventsParserDef_;
    std::unique_ptr<Song> song_;
    bool hadDevice_[2] = {};  // each slot had a device at the song's start (gameDeviceLost)
    std::unique_ptr<Player> player_;
    std::unique_ptr<Autoplay> bot_;
    // Rock Band's vocals (wave 02): the judge, the voice and the track;
    // player_ carries the judge's numbers (Player::setExternal).
    std::unique_ptr<RbBonus> rbBonus_;  // solos, drum fills, the Big Rock Ending (wave 02)
    // Rock Band's band (wave 02): players 2..4, each on its own part, track
    // and judge; player_ stays player 1 [I: RB's band rules, docs/parity.md].
    struct RbSeat {
        std::string part;
        int pad = 1, difficulty = 3;
        std::unique_ptr<Player> player;
        std::unique_ptr<VocalPlayer> vocal;
        std::unique_ptr<VocalInput> mic;
        std::unique_ptr<Autoplay> bot;
        std::unique_ptr<RbTrackView> track;
        std::unique_ptr<RbBonus> bonus;
        float rating = 0.5f, pitch = -1, loud = 0;
        int phrasesHit = 0;
        const char* lastRating = nullptr;
        float lastRatingMs = -1e9f;
        bool out = false;  // failed out, waiting for a save
    };
    std::vector<RbSeat> seats_;
    std::vector<GameSetup::RbMember> rbBandSetup_;
    bool player0Out_ = false;
    float rbTrackDiff_ = -1;  // the mean difficulty the gem tracks were configured for
    std::array<int, 4> fails_{};
    std::array<float, 4> outMs_{};

    int unisonBonuses_ = 0, saves_ = 0;
    std::map<int, std::pair<int, int>> unison_;  // an Overdrive phrase's start tick -> (captured, players with it)
    std::vector<std::pair<std::string, float>> bandNotices_;
    void makeSeats(bool autoplay);
    void updateSeats(float t, bool over, const std::vector<PlayerEvent>& events0);
    void drawSeats(float t, float speed);
    void drawBandHud(float t);
    Player* seatPlayer(int pad);
    std::unique_ptr<VocalPlayer> vocal_;
    std::unique_ptr<VocalInput> mic_;
    VocalTrackView vocalView_;
    float vocalRating_ = 0.5f, voicePitch_ = -1, voiceLoud_ = 0;
    float voiceShoutUntilMs_ = -1e30f;
    float botFillStrikeMs_ = -1e30f;  // the autoplay drummer's last fill strike
    int botFillLane_ = 0;  // a walk's shout into the "microphone" (setPlayerStarPower vocal_shout)
    const char* vocalRatingName_ = nullptr;
    float vocalRatingMs_ = -1e9f;
    int vocalPhrasesHit_ = 0;
    void updateVocals(float t);
    void drawRbTracks(float t, float speed);
    void drawRbNotices(float t);
    void drawRbTrack0(float t, float speed);
    int rbTrackFrames_ = 0;  // frames RB's instrument track drew (the walks)
    // RB's track intro (ui/game.dta intro_start: on_extend at song time 0;
    // intro_skip: at once) and each gem track's intro.trig delay
    // (gem_track.dta play_intro: 0.55 s alone, 0.25 s x index + 0.05 s in a
    // band) [C data].
    float rbExtendMs_ = -1e30f;
    float rbIntroAt(int index) const { return rbExtendMs_ + (seats_.empty() ? 550.0f : 250.0f * float(index) + 50.0f); }
    MenuMovie menuMovie_;       // the menus' background video (movie_panel)
    int songLoads_ = 0;         // songs loaded (the walks: a key must not reload the song, GD05-024)
    int menuVenueSounds_ = 0;   // venue sounds played before the first song (the walks: none at boot, GD05-023)
    float overdriveDrainMs() const;
    int rbStars(const Player* p) const;
    std::unique_ptr<VorbisStream> pendingStream_;
    float bandVolume_ = 1, guitarVolume_ = 1;
    int syncOffsetMs_ = 0;
    // Enhanced Options > Audio / Video Calibration (ms, + late): the heard
    // delay moves the judging, the seen delay draws the highway ahead.
    int audioOffsetMs_ = 0, videoOffsetMs_ = 0;
    // GH2's sync offset (options, the lag screen stores -lag) less the
    // calibrated audio delay: the watcher's input offset (+0x2c).
    float calibrationMs() const { return float(syncOffsetMs_ - audioOffsetMs_); }
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
    std::vector<std::string> sfxHistory_;                   // UiHost::sfxHistory
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
    int slipHits_[2] = {0, 0};
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
    Autoplay* botFor(const Player* p) {
        for (const RbSeat& st : seats_)
            if (st.player.get() == p) return st.bot.get();
        return p == player_.get() ? bot_.get() : bot2_.get();
    }
    bool songLost() const { return coopBand_ ? coopBand_->failed() : player_ && player_->failed(); }
    bool songLostForUi() const override { return songLost(); }
    // Development aid: GD05_FAIL_AT=<song ms> fails the song then.
    float failAtMs_ = 1e30f;
    uint64_t failTicks_ = 0;
    bool exploded_ = false;
    // The UI's song: the game events sent so far, the outro, the counts.
    bool begun_ = false, wonSent_ = false, lostSent_ = false, outroSent_ = false, spReadySent_ = false;
    uint64_t overNs_ = 0, outroAtNs_ = 0;
    float overT_ = 0;
    bool musicPaused_ = false;
    int spPhrases_[2] = {}, spStarted_[2] = {}, streaksBroken_[2] = {};
    float lastT_ = 0;
    // Development aid: GD05_KEYS="<song ms>:<key>,..." (game/key_script.h)
    // plays scripted guitar input in every song, at exact song times.
    KeyScript keys_;
    // The song screen: GH2's own highway and HUD (render/track_view.h,
    // render/hud_view.h), loaded with the first song; gd01's debug highway
    // (render/highway.h) with GD05_DEBUG_HIGHWAY=1, or when GH2's scenes
    // cannot be loaded.
    Highway highway_;
    bool debugHighway_ = false;
    bool trackTried_ = false;
    std::string trackLayout_;
    std::unique_ptr<scene::MiloLibrary> milos_;
    std::unique_ptr<gfx::SceneCache> sceneCache_;
    std::unique_ptr<TrackView> track_;
    std::unique_ptr<HudView> hud_;
    // Rock Band's highway (render/rb_track_view.h), when GH2's is not on the disc.
    std::unique_ptr<RbTrackView> rbTrack_;
    // Rock Band's in-song HUD (render/rb_hud_view.h, GD05-058).
    std::unique_ptr<RbHudView> rbHud_;
    RbHudView::State rbHudState(float t) const;
    void drawRbHud(float t);
    // The walks' HUD check (GD05-058): each element drawn alone, its pixels'
    // box against the box its milo data gives; the shown values against
    // the players' (rb_hud_* counters).
    void rbHudProbe();
    bool rbHudProbeDue_ = false;
    float rbHudNextProbeMs_ = 15000;
    int rbHudProbes_ = 0, rbHudBoxes_ = 0, rbHudBoxErrPx_ = 0, rbHudValueErrors_ = 0;
    int rbNowBarGemPx_ = -1, rbNowBarCoverPct_ = -1;  // the now-bar probe (rbHudProbe)
    // The walks' highway and crowd probes (GD05-070 / GD05-071, rb_highway
    // and rb_crowd; GD05_METER_PROBE / GD05_TAIL_PROBE / GD05_CROWD_PROBE=1):
    // under the frame, each part alone over magenta, its pixels measured.
    void rbProbes();
    struct RbProbeStats {
        std::vector<std::pair<float, float>> meter;  // (song ms, Overdrive meter luma) a frame
        int meterJumps = 0, meterFlickers = 0, meterBeats = 0, meterFrames = 0;
        int starTails = 0, starTailsWrong = 0;   // summed over the frames
        int tailPx = 0, tailChromaPct = -1;       // the last star-tail probe
        int crowdPx = 0, crowdPxMax = 0, crowdProbes = 0, crowdFrames = 0;
        int trackOrderErrors = 0, trackOrderParts = 0, hudOrderInversions = 0, hudOrderDirs = 0;
    } rbProbe_;
    // The venue behind the track (M8): set by the venue code once a venue is
    // loaded; draws the world with the renderer, materials with
    // DrawContext::hdr = `hdr` (GH2's HDR buffer, render/post_fx.h).
    std::function<void(gfx::Renderer& r, bool hdr)> worldDraw_;
    // The venue (game/venue.h, game/world.h, render/venue_view.h): without
    // the menus the song's (venues::forSong, GD05_VENUE), else world_panel's.
    std::unique_ptr<gfx::SceneCache> venueCache_;
    std::unique_ptr<Venue> venue_;
    std::unique_ptr<World> world_;
    std::unique_ptr<VenueView> venueView_;
    // The camera director and the lighting presets (M8 phase 2; game/camera_director.h, game/light_presets.h).
    std::unique_ptr<CameraDirector> director_;
    // Rock Band's shots by category (BandCamShot category: INTRO, NORMAL,
    // directed_guitar, WIN ...), and the VENUE track's cursor (rbShots()).
    std::map<std::string, std::vector<const scene::CamShot*>> rbShots_;
    size_t rbVenueNote_ = 0, rbVenueEvent_ = 0;
    uint32_t rbShotSeed_ = 1;
    bool rbWinShot_ = false, rbLoseShot_ = false;
    float rbNextCamMs_ = -1e9f;
    std::set<const scene::CamShot*> rbShotUsed_;
    const scene::CamShot* rbShot(const std::string& category, const std::function<bool(const scene::CamShot&)>& ok = {});
    void rbShots(float t);
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
        bool framed = false;  // the band shot is picked (updateBackdrop)
        int lightPart = -1;
    };
    std::unique_ptr<Backdrop> backdrop_;
    // Shown by Enhanced Options; loaded at startup and kept in the menus (a
    // song's venue replaces it), so the screen opens on the next frame.
    bool backdropShown_ = false, backdropDue_ = false;
    bool backdropWarm_ = false;  // drawn once unseen (its textures and programs reach the GPU)
    void loadBackdrop();
    // The Enhanced Options (enhanced.json in the save folder), what is in
    // effect, and the FPS counter's average.
    Json enhanced_ = Json::object();
    // Enhanced > HDR > True HDR: the window's surface (chosen at start) and why it is SDR when it is.
    hdr::Surface hdrSurface_ = hdr::Surface::Sdr;
    std::string hdrReason_, hdrApplied_;
    std::string hdrStatus();
    std::string savedHdrMode();  // enhanced.json's HDR before the window exists
    std::string screenModeApplied_;
    // Export / Load User Files: the dialogs' answers (queued from SDL's
    // callback) and the last result shown by the panel.
    std::mutex dialogMutex_;
    std::vector<std::pair<std::string, std::string>> dialogResults_;
    std::string userFilesStatus_;
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
    Music musicPending_ = Music::None;  // the stream musicLoader_ opens
    int musicPlays_ = 0;                 // streams musicLoader_ handed to the mixer (the walks)
    // GD05-073: each frame's work time (ms) and marks at moments that used
    // to stutter (a preview asked for / playing, the song's start), for the
    // walks' frame_spikes checks (ratios to the frames' median: load on the
    // machine moves both). GD05_FRAMELOG=1 logs the frames after a mark.
    std::vector<float> frameWorkMs_;
    std::vector<std::pair<size_t, std::string>> frameMarks_;
    int frameLogLeft_ = 0;
    void markFrame(const std::string& tag) {
        frameMarks_.push_back({frameWorkMs_.size(), tag});
        if (envOn("GD05_FRAMELOG")) frameLogLeft_ = 240, SDL_Log("frames: mark %s at frame %zu", tag.c_str(), frameWorkMs_.size());
    }
    int frameSpikeRatio(const std::string& spec) const;
    int gpuUploads() const { return (venueCache_ ? venueCache_->uploads() : 0) + (sceneCache_ ? sceneCache_->uploads() : 0); }
    int uploadsAtBegin_ = -1;  // gpuUploads() as the song began
    void warmWorld();
    MusicLoader musicLoader_;
    void pollMusic();
    // GH2's sound effects (audio/synth.h): config/sound.dta's banks, the
    // venue's through the world; made with the game data.
    std::unique_ptr<Synth> synth_;
    float fxVolumeDb_ = 0;  // the options' sound effects volume (dB): {options get_fx_volume}
    bool fxMuted_ = false;  // options set_mute_fx
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
    // Development aid: GD05_VIRTUAL_PAD=<vendor>:<product>[:guitar] (hex, e.g.
    // 1430:4748:guitar) plugs in a virtual controller, to test without hardware.
    if (const char* vp = SDL_getenv("GD05_VIRTUAL_PAD")) virtualPad_ = Input::attachVirtualPad(vp);
    inputLog_ = envOn("GD05_INPUTLOG");
    // Enhanced > Graphics Backend (GDCORE-008, render/core_backend.h): local
    // (gd05's OpenGL device) or a gd-engine-core backend, decided before the
    // window: SDL3 GPU and Vulkan need a window made for their API (in the
    // launcher, prepareWindowFor asks it for one). Local and opengl3 keep the
    // OpenGL window.
    debugHighway_ = envOn("GD05_DEBUG_HIGHWAY");
    debugMenu_ = envOn("GD05_DEBUG_MENU");
    if (!options_.walk.empty()) {
        walk_ = std::make_unique<WalkDriver>(options_.walk);
        if (!walk_->known()) {
            std::fprintf(stderr, "%s: unknown walk test '%s' (quickplay, lose, pause, attract, career, back, enhanced, cheats, mouse, gamepads, gamepads_pad, ...)\n", app::kProgram, options_.walk.c_str());
            SDL_Quit();
            return 2;
        }
        debugMenu_ = false;
        prepareWalkDirs();  // before enhanced.json is first read
    }
    // Enhanced > Graphics / Audio Backend: this start's, read once (a change
    // in the menu applies on the next start). Development aid:
    // GD05_AUDIO_BACKEND=<id> overrides the audio one (the walks on each).
    latchStartupBackends();
    SDL_Log("backends: graphics %s, audio %s", startupBackends().graphics.c_str(), startupBackends().audio.c_str());
    {
        std::string audio = startupBackends().audio;
        if (const char* b = SDL_getenv("GD05_AUDIO_BACKEND"); b && *b) audio = b;
        audiodev::setBackend(audio);  // every sound's output (audio/audio_device.h); local is SDL's device
    }
    const gfx::Backend graphicsBackend = gfx::chosenBackend(startupBackends().graphics);
    const bool gpuWindow = graphicsBackend == gfx::Backend::Sdl3 || graphicsBackend == gfx::Backend::Vulkan;
    SDL_WindowFlags baseFlags = SDL_WINDOW_RESIZABLE;
#ifdef __ANDROID__
    baseFlags |= SDL_WINDOW_FULLSCREEN;
#endif
    SDL_WindowFlags flags = baseFlags | gfx::prepareWindowFor(graphicsBackend);  // OpenGL (render/device.h) for local
    // Enhanced > HDR > True HDR: the window's surface is chosen now (render/hdr_output.h).
    // A window or surface that cannot be made with it is made again without (Emulated).
    // The surface probe is local's (an OpenGL context): on the core backends True HDR runs as Emulated.
    hdrReason_ = graphicsBackend != gfx::Backend::Local
                     ? std::string("True HDR is not on the ") + gfx::backendId(graphicsBackend) + " graphics backend yet"
                     : hdr::unsupportedReason();
    const bool trueHdr = savedHdrMode() == "true";
    const bool wantHdr = trueHdr && graphicsBackend == gfx::Backend::Local && hdr::request();
    window_ = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), flags);
    if (wantHdr) {
        if (window_) {
            if (SDL_GLContext probe = SDL_GL_CreateContext(window_)) {
                hdrSurface_ = hdr::obtained(hdrReason_);
                SDL_GL_DestroyContext(probe);
            } else {
                hdrReason_ = hdr::failure("no OpenGL context on an HDR surface", SDL_GetError());
            }
        } else {
            hdrReason_ = hdr::failure("no window with an HDR surface", SDL_GetError());
        }
        if (hdrSurface_ == hdr::Surface::Sdr) {
            if (window_) SDL_DestroyWindow(window_);
            hdr::cancel();
            window_ = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), flags);
        }
    }
    if (trueHdr)
        SDL_Log("hdr: %s", hdrSurface_ == hdr::Surface::ScRgb ? "scRGB surface (linear, RGBA16F)"
                           : hdrSurface_ == hdr::Surface::Pq  ? "BT.2020 PQ surface"
                                                              : ("unavailable (" + hdrReason_ + "), using Emulated").c_str());
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
    gfx::setRendererBackend(graphicsBackend);
    bool rendererOk = window_ && canvas_.init(window_, error);
    if (!rendererOk && graphicsBackend != gfx::Backend::Local) {
        // The core backend did not start: local runs instead (rule 2: never a
        // black screen); an SDL3 GPU / Vulkan window is made again for
        // OpenGL first (in the launcher the shim gets its OpenGL window back).
        SDL_Log("graphics backend: %s did not start (%s); using local", gfx::backendId(graphicsBackend), window_ ? error.c_str() : SDL_GetError());
        if (gpuWindow) {
            SDL_Log("graphics backend: %s: making the window again for OpenGL (local)", gfx::backendId(graphicsBackend));
            if (window_) SDL_DestroyWindow(window_);
            window_ = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), baseFlags | gfx::prepareWindowFor(gfx::Backend::Local));
            if (window_) setAppWindowIcon(window_);
        }
        gfx::setRendererBackend(gfx::Backend::Local);
        error.clear();
        rendererOk = window_ && canvas_.init(window_, error);
    }
    if (!rendererOk) {
        std::fprintf(stderr, "%s: cannot open the window: %s\n", app::kProgram, window_ ? error.c_str() : SDL_GetError());
        SDL_Quit();
        return 2;
    }
    canvas_.renderer().device().setHdrSurface(int(hdrSurface_));
    if (!mixer_.open(error)) {
        std::fprintf(stderr, "%s: cannot open the audio device: %s\n", app::kProgram, error.c_str());
        canvas_.shutdown();
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return 2;
    }
    // A song from the command line shows the Enhanced Options as they are
    // set (the menus load them again in startUi; a walk test has its own).
    if (!options_.song.empty() && options_.walk.empty()) loadEnhanced();

    // The disc image: the command line's, else the remembered one (or an .iso
    // next to the program); without one, the first screen asks for it.
    if (options_.inputCapture) startCapture();
    const std::string path = options_.inputCapture ? std::string() : options_.dataPath.empty() ? loadRememberedPath() : options_.dataPath;
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

    // GD05_PERF=1: a frame-time breakdown every 2 s (docs/renderer.md).
    const bool perf = envOn("GD05_PERF");
    uint64_t perfStart = SDL_GetTicksNS(), perfMaxNs = 0, perfLimitNs = 0;
    int perfFrames = 0;
    if (perf) {
        const gfx::DeviceInfo& di = canvas_.renderer().device().info();
        SDL_Log("perf: %s %s on %s, video driver %s", di.api.c_str(), di.version.c_str(), di.gpu.c_str(), SDL_GetCurrentVideoDriver());
    }
    const int hitchMs = SDL_getenv("GD05_HITCHLOG") ? std::atoi(SDL_getenv("GD05_HITCHLOG")) : 0;
    uint64_t hitchMaxNs = 0;
    int hitches = 0;
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
        pollUserFiles();  // Enhanced Options > Export / Load User Files' dialogs
        if (capture_) frameCapture();
        else if (ui_) frameUi();
        else if (player_) frameSong();
        else if (!data_) frameNeedData();
        else frameList();
        // Without vsync (e.g. SDL_VIDEO_DRIVER=offscreen) keep to about 250
        // frames a second instead of spinning; gd01's frame limiter comes
        // with its renderer.
        const uint64_t spent = SDL_GetTicksNS() - frameStart;
        frameWorkMs_.push_back(float(double(spent) / 1e6));
        if (frameLogLeft_ > 0 && --frameLogLeft_ >= 0)
            SDL_Log("frames: %zu %.2f ms %s%s", frameWorkMs_.size() - 1, double(spent) / 1e6, ui_ ? ui_->currentScreen().c_str() : "-", player_ ? " (song)" : "");
        // Development aid (GD05-034, the load stutter): GD05_HITCHLOG=<ms>
        // logs every frame whose work took longer, with its screen; the
        // longest and the count print at the end.
        if (hitchMs > 0 && spent > uint64_t(hitchMs) * 1'000'000ull) {
            ++hitches;
            hitchMaxNs = std::max(hitchMaxNs, spent);
            SDL_Log("hitch: %.1f ms on %s%s", double(spent) / 1e6, ui_ ? ui_->currentScreen().c_str() : "-", player_ ? " (song)" : "");
        }
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

    // Restart Now as a walk's last step: the walk is over (its checks go on
    // in the restarted game's walk, runApp).
    if (walk_ && ui_ && restart_) walk_->poll(*ui_, double(SDL_GetTicksNS()) / 1e6);
    if (hitchMs > 0) SDL_Log("hitches: %d frames over %d ms, longest %.1f ms", hitches, hitchMs, double(hitchMaxNs) / 1e6);
    if (walk_ && ui_) exitCode_ = walk_->finish(*ui_);
    endSong();
    stopMusic();
    unloadVenue();
    dropGameParsers();
    ui_.reset();
    if (synth_)
        SDL_Log("sound effects: %d played (%d voices, %llu started on the device), %d samples decoded in %.0f ms, %d failures",
                synth_->stats().plays, synth_->stats().voices, (unsigned long long)synth_->player().started(), synth_->stats().decoded,
                synth_->stats().decodeMs, synth_->stats().decodeFailures + synth_->stats().unresolved);
    synth_.reset();
    seats_.clear(), hud_.reset(), track_.reset(), track2_.reset(), rbTrack_.reset(), rbHud_.reset();
    sceneCache_.reset();
    milos_.reset();
    menuMovie_.close();  // its textures go before the device
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
        SDL_Log("archive: gen/main.hdr version 3, %d files in %d parts (%s)", int(data->fileCount()), int(data->archivePartCount()),
                data->description().c_str());
        Rules rules = loadRules(*data);
        std::vector<SongInfo> songs = loadSongs(*data);
        if (songs.empty()) throw std::runtime_error("No songs found in " + path);
        unloadVenue();
        dropGameParsers();
        ui_.reset();
        seats_.clear(), hud_.reset(), track_.reset(), track2_.reset(), rbTrack_.reset(), rbHud_.reset();
        sceneCache_.reset(), milos_.reset();
        trackTried_ = false;
        synth_.reset();
        musicLoader_.cancel(), musicLoader_.join(), musicPending_ = Music::None;
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

// GH2's front end: the boot splashes and the intro (unless GD05_SKIP_BOOT=1
// or GD05_SCREEN=<screen>, which opens that screen), then the menus.
void App::startUi() {
    if (walk_) prepareWalkDirs();
    loadEnhanced();
    // A save folder of its own (the walk test, development) holds controllers.json too.
    if (const char* d = SDL_getenv("GD05_SAVE_DIR"); d && *d) {
        std::string dir = d;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
        input_.setSaveDir(dir);
    }
    auto ui = std::make_unique<ui::UiManager>(*data_, canvas_, *this, input_);
    std::string error;
    if (!ui->init(error)) {
        SDL_Log("Rock Band's menus could not start (%s); showing the debug song list", error.c_str());
        message_ = error;
        return;
    }
    ui_ = std::move(ui);
    applyEnhanced();  // the menus' own (Sharper Text and Sprites) now that they exist
    ui_->applyOptions();
    makeGameParsers();
    const char* keys = SDL_getenv("GD05_KEYS");
    menuKeys_ = MenuKeys(keys ? keys : "");
    if (!menuKeys_.valid()) SDL_Log("GD05_KEYS: some menu items were not understood and are ignored");
    std::string screen = SDL_getenv("GD05_SCREEN") ? SDL_getenv("GD05_SCREEN") : "";
    if (screen.empty()) screen = options_.startScreen;  // Restart Now: back on Enhanced Options
    if (walk_) screen = walk_->startScreen();
    const bool skip = envOn("GD05_SKIP_BOOT") || !screen.empty();
    if (envOn("GD05_SKIP_BOOT") && screen.empty()) screen = "splash_screen";
    ui_->start(skip, screen);
    loadBackdrop();  // Enhanced Options' backdrop, ready before the menus show
    uiStartMs_ = double(SDL_GetTicksNS()) / 1e6;
}

bool App::loadSong(const SongInfo& info, int difficulty, bool autoplay, float band, float guitar, bool fastIntro) {
    ++songLoads_;
    endSong();
    stopMusic();
    mixer_.setVolumeDb(0);
    try {
        song_ = std::make_unique<Song>(loadChart(*data_, info, rules_));
        difficulty_ = std::clamp(difficulty, 0, kDifficultyCount - 1);
        // Without the menus: GD05_MODE (with the menus gameLoad set the mode).
        if (!ui_) {
            mode_ = devMode();
            difficulties_ = {difficulty_, difficulty_};
            if (const char* d2 = SDL_getenv("GD05_DIFF2")) difficulties_[1] = std::clamp(std::atoi(d2), 0, kDifficultyCount - 1);
            parts_ = {"guitar", "guitar"};
            if (!options_.part.empty()) parts_[0] = options_.part;
            // --part guitar,bass,drum,vocals: a band, one player per part (the others autoplay).
            rbBandSetup_.clear();
            if (parts_[0].find(',') != std::string::npos) {
                std::string rest = parts_[0];
                parts_[0] = rest.substr(0, rest.find(','));
                rest = rest.substr(rest.find(',') + 1);
                for (int pad = 1; !rest.empty() && pad < 4; ++pad) {
                    const size_t c = rest.find(',');
                    rbBandSetup_.push_back({rest.substr(0, c), difficulty_, pad});
                    rest = c == std::string::npos ? std::string() : rest.substr(c + 1);
                }
            }
            if (mode_ == DevMode::Coop) {
                // The second instrument the song has (song_provider get_instrument 1).
                const SongAudio& a = info.coopAudio ? *info.coopAudio : info.audio;
                parts_[1] = a.tracks.count("bass") ? "bass" : a.tracks.count("rhythm") ? "rhythm" : "guitar";
                if (const char* ps = SDL_getenv("GD05_PARTS")) {
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
        // Practice's song_block: song, or song_coop for the bass / rhythm
        // part (modes.dta; 0x822AF468 uses song_practice_N only to rewrite
        // the paths of downloaded songs) [C].
        const bool coopBlock = coop || (mode_ == DevMode::Practice && songBlock_ == "song_coop");
        const SongAudio& audio = coopBlock && info.coopAudio ? *info.coopAudio : info.audio;
        audio_ = &audio;
        pendingStream_ = std::make_unique<VorbisStream>(data_->read(audio.moggPath));
        // Face-off players own the face-off sections' gems: the parser fills
        // player_mask only with separate_parts and two players (0x823450B0)
        // [C]; the others play every gem.
        const bool faceOff = mode_ == DevMode::FaceOff;
        // One player plays the guitar, except in practice (practice_selpart: the guitar or the bass).
        // Rock Band: the player's instrument (the slot's track type) [C data].
        Part part0 = partFor(*song_, parts_[0], coopBlock);
        // Vocals (wave 02): the vocal judge (VocalPlayer) on PART VOCALS, the
        // voice from the microphone (or GD05_VOCALS_FILE); player_ plays the
        // empty gem chart and carries the judge's numbers.
        if (part0 == Part::Vocals && song_->vocals.empty()) part0 = Part::Guitar;
        parts_[0] = part0 == Part::Drums ? "drum" : part0 == Part::Bass ? "bass" : part0 == Part::Vocals ? "vocals" : "guitar";
        // The drums, wave 01 (DECISIONS D6): GH2's joypad watcher on the
        // drum chart, the pads as frets: kick = the orange fret, red, yellow,
        // blue = their frets, the green pad = the green fret (an Xbox 360
        // drum kit's LB, B, Y, X, A) [I: RB's drum watcher is DrumPlayer,
        // not wired yet]. The chart's lanes are permuted to those frets.
        bool anyDrums = part0 == Part::Drums;
        for (const auto& m : rbBandSetup_) anyDrums |= m.part == "drum" || m.part == "drums";
        if (anyDrums)
            for (Chart& c : song_->charts[size_t(Part::Drums)]) {
                for (Gem& g : c.gems) {
                    const uint8_t f = g.frets;
                    g.frets = uint8_t(((f & 1) << 4) | (f & 0x0E) | ((f >> 4) & 1));
                    g.hopo = false;
                }
                splitDrumChords(c);  // RB's drum watcher: a gem per pad (wave 02)
            }
        const Part part1 = partFor(*song_, parts_[1], coop);
        // Two players never fail on their own meters: competitive modes
        // cannot fail, a co-op player cannot end the game (0x822C6B98); the
        // co-op band's meter does (game/band_performer.h) [C].
        // Enhanced Options > No Fail Mode (as --no-fail).
        // A band's players fail out and are saved by the band (updateSeats), never alone.
        const bool noFail = options_.noFail || enhancedValue("no_fail") == "on" || autoplay || mode_ != DevMode::Single || !rbBandSetup_.empty();
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
                                           part0 == Part::Drums ? ControllerMode::Joypad : two ? modeOfPad(pads_[0]) : input_.lastMode(), noFail,
                                           faceOff ? 0 : -1, config);
        player_->setCalibrationMs(calibrationMs());
        player_->setDrumMode(part0 == Part::Drums);
        player_->setBigNotes(song_->bigNotes[size_t(part0)]);
        if (part0 == Part::Bass) player_->setBassRanges(song_->bassRanges);
        player_->setForgivingHopos(enhancedValue("hopo_mode") == "forgiving");
        player_->setEasyChords(enhancedValue("easy_chords") == "on" && part0 != Part::Drums);  // guitar and bass only
        bot_ = autoplay ? std::make_unique<Autoplay>(player_->chart(), faceOff ? 0 : -1) : nullptr;
        vocal_.reset(), mic_.reset();
        rbBonus_ = part0 != Part::Vocals ? std::make_unique<RbBonus>(*song_, part0, static_cast<Difficulty>(difficulties_[0]), rules_) : nullptr;
        seats_.clear();
        vocalRating_ = 0.5f, vocalRatingName_ = nullptr, vocalRatingMs_ = -1e9f, vocalPhrasesHit_ = 0;
        if (part0 == Part::Vocals) {
            vocal_ = std::make_unique<VocalPlayer>(*song_, static_cast<Difficulty>(difficulties_[0]), rules_);
            vocal_->setDrainMs(overdriveDrainMs());
            if (!autoplay || SDL_getenv("GD05_VOCALS_FILE")) {
                mic_ = std::make_unique<VocalInput>();
                std::string err;
                if (mic_->open(startupAudioBackend() == "miniaudio" ? "miniaudio" : "sdl3", err))
                    SDL_Log("vocals: the voice from %s", mic_->name().c_str());
                else
                    SDL_Log("vocals: no microphone (%s): the judge hears silence", err.c_str()), mic_.reset();
            }
            SDL_Log("vocals: %d phrases, %d notes", vocal_->phrases(), int(song_->vocals.notes.size()));
            if (SDL_getenv("GD05_VOCALLOG"))
                for (size_t i = 0; i < 4 && i < song_->vocals.phrases.size(); ++i) {
                    const VocalPhrase& ph = song_->vocals.phrases[i];
                    SDL_Log("phrase %d: %.0f..%.0f notes %d..%d first note %.0f", int(i), double(ph.startMs), double(ph.endMs), ph.firstNote, ph.lastNote,
                            ph.firstNote >= 0 ? double(song_->vocals.notes[size_t(ph.firstNote)].ms) : -1.0);
                }
        }
        player2_.reset(), bot2_.reset(), coopBand_.reset();
        if (!rbBandSetup_.empty()) makeSeats(autoplay);
        sharedMeter_ = {};
        guitarHeard_ = true;
        activeGem_ = 0, activePlayers_ = 0;
        if (two) {
            player2_ = std::make_unique<Player>(*song_, part1, static_cast<Difficulty>(difficulties_[1]), rules_, autoplay || !ui_ ? ControllerMode::Guitar : modeOfPad(pads_[1]),
                                                true, faceOff ? 1 : -1, config);
            player2_->setNumber(1);
            player2_->setCalibrationMs(calibrationMs());
            player2_->setBigNotes(song_->bigNotes[size_t(part1)]);
            if (part1 == Part::Bass) player2_->setBassRanges(song_->bassRanges);
            player2_->setForgivingHopos(enhancedValue("hopo_mode") == "forgiving");
            player2_->setEasyChords(enhancedValue("easy_chords") == "on" && part1 != Part::Drums);  // guitar and bass only
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
        failAtMs_ = SDL_getenv("GD05_FAIL_AT") ? float(std::atof(SDL_getenv("GD05_FAIL_AT"))) : 1e30f;
        // Development aid: GD05_END_AT=<song ms> ends (wins) the song early.
        if (const char* e = SDL_getenv("GD05_END_AT")) song_->endMs = std::min(song_->endMs, float(std::atof(e)));
        if (walk_) {
            song_->endMs = std::min(song_->endMs, walk_->songEndMs());
            failAtMs_ = std::min(failAtMs_, walk_->songFailMs());
        }
        failTicks_ = 0;
        exploded_ = false;
        setupPractice();
        loadGameParsers(audio);
        // Development aid: GD05_LEFTY=1 (player 1), 2 (player 2) or 12 (both)
        // turns on the lefty flip (the options' lefty in the menus).
        if (const char* l = SDL_getenv("GD05_LEFTY")) input_.setLefty(SDL_strchr(l, '1') != nullptr, SDL_strchr(l, '2') != nullptr);
        // GD05_KEYS: with the menus, only its "s<song ms>:" items play in the song.
        const char* k = SDL_getenv("GD05_KEYS");
        keys_ = KeyScript(walk_ && !walk_->songKeys().empty() ? walk_->songKeys() : ui_ ? MenuKeys::songItems(k ? k : "") : (k ? k : ""));
        if (!keys_.valid()) SDL_Log("GD05_KEYS: some items were not understood and are ignored");
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
    mixer_.setPartVolumes(band, guitar);
    // The lead-in until beginSong times GH2's intro clock.
    fastIntro_ = fastIntro;
    leadInMs_ = kLeadInMs;
    extendAtMs_ = float(kTrackExtendMs);
    info_ = &info;
    begun_ = wonSent_ = lostSent_ = outroSent_ = spReadySent_ = false;
    musicPaused_ = false;
    spPhrases_[0] = spPhrases_[1] = spStarted_[0] = spStarted_[1] = streaksBroken_[0] = streaksBroken_[1] = 0;
    paused_ = false;
    lastT_ = float(-leadInMs_);
    std::fill(flashes_.begin(), flashes_.end(), 0.0f);
    lastFrameMs_ = -leadInMs_;
    failMs_ = -1e9f;
    if (prepareTrack()) {
        track_->reset(extendAtMs_);
        track_->setLefty(input_.lefty(0));
        if (track2_) track2_->reset(extendAtMs_), track2_->setLefty(input_.lefty(1));
        // The players' track surfaces (render/track_view.h setSurface).
        const std::string surfaceMode = !gh2Mode_.empty() ? gh2Mode_ : "quickplay";
        track_->setSurface(*data_, surfaceMode, bandOutfit_[0], info.id);
        if (track2_) track2_->setSurface(*data_, surfaceMode, bandOutfit_[1], info.id);
        hud_->reset(extendAtMs_);
    }
    // The venue: without the menus the song's (its quickplay venue or career
    // tier, GD05_VENUE overrides); with them world_panel loads it (worldLoad).
    if (!ui_ && !debugHighway_) {
        const std::string v = venues::fromEnv().empty() ? venues::forSong(*data_, info) : venues::fromEnv();
        const VenueVariant var = mode_ == DevMode::FaceOff || mode_ == DevMode::ProFaceOff ? VenueVariant::Mp
                                 : mode_ == DevMode::Coop                          ? VenueVariant::Coop
                                                                                   : VenueVariant::Single;
        loadVenue(v, var);
    }
    if (!band_ && song_ && info_) startBand(*info_, *song_);
    warmWorld();
    worldDriver_ = WorldDriver(!ui_);
    worldDriver_.reset(float(-leadInMs_));
    return true;
}

// The tutorial's voice-over: the .mogg decoded whole (mono, a few seconds)
// and played as one sound effect voice at its rate.
bool App::playVoice(const std::string& path, float volumeDb) {
    stopVoice();
    if (!synth_ || !data_ || !data_->exists(path)) return false;
    try {
        VorbisStream stream(data_->read(path));
        auto pcm = std::make_shared<SfxPcm>();
        pcm->rate = stream.sampleRate();
        const int ch = std::max(1, stream.channels());
        std::vector<std::vector<float>> buf(size_t(ch), std::vector<float>(4096));
        std::vector<float*> ptrs(static_cast<size_t>(ch));
        for (int c = 0; c < ch; ++c) ptrs[size_t(c)] = buf[size_t(c)].data();
        for (int n; (n = stream.decode(ptrs.data(), 4096)) > 0;) pcm->samples.insert(pcm->samples.end(), buf[0].begin(), buf[0].begin() + n);
        voiceEndMs_ = double(SDL_GetTicksNS()) / 1e6 + double(pcm->samples.size()) * 1000.0 / std::max(1, pcm->rate);
        SfxPlayer::Params p;
        p.gain = volumeDb <= -96.0f ? 0.0f : std::pow(10.0f, volumeDb / 20.0f);
        p.stream = true;  // a stream: the game's pause leaves it (tutorial_32 / _33 end paused on their voice-over)
        voice_ = synth_->player().isOpen() ? synth_->player().play(pcm, p) : 0;
        return true;
    } catch (const std::exception& e) {
        SDL_Log("%s: %s", path.c_str(), e.what());
        return false;
    }
}

bool App::voicePlaying() {
    if (synth_ && synth_->player().isOpen()) return voice_ && synth_->player().playing(voice_);
    return double(SDL_GetTicksNS()) / 1e6 < voiceEndMs_;
}

void App::pauseVoice(bool paused) {
    if (voice_ && synth_) synth_->player().setVoicePaused(voice_, paused);
}

void App::stopVoice() {
    if (voice_ && synth_) synth_->player().stop(voice_);
    voice_ = 0, voiceEndMs_ = 0;
}

// The practice sections of a song (UiHost::songSections, PS2 0x0011b678).
std::vector<UiHost::Section> App::songSections(const std::string& songId, const std::string& part, int difficulty, Section& full) {
    std::vector<Section> out;
    auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return s.id == songId; });
    if (it == songs_.end()) return out;
    Song song;
    try {
        song = loadChart(*data_, *it, rules_);
    } catch (const std::exception& e) {
        SDL_Log("%s: %s", songId.c_str(), e.what());
        return out;
    }
    const TempoMap& tm = song.tempo;
    const int endTick = int(tm.msToTick(song.endMs));
    full.startTick = 0, full.endTick = endTick, full.startMs = 0, full.endMs = song.endMs, full.hasGems = true;
    for (const SongEvent& e : song.events)
        if (e.text.rfind("section ", 0) == 0) {
            if (!out.empty()) out.back().endTick = e.tick, out.back().endMs = e.ms;
            Section sec;
            sec.name = e.text.substr(8);
            sec.startTick = e.tick, sec.startMs = e.ms;
            sec.endTick = endTick, sec.endMs = song.endMs;
            out.push_back(sec);
        }
    const Chart& chart = song.chart(partFor(song, part, false), static_cast<Difficulty>(std::clamp(difficulty, 0, kDifficultyCount - 1)));
    for (Section& sec : out)
        for (const Gem& g : chart.gems)
            if (g.tick >= sec.startTick && g.tick < sec.endTick) {
                sec.hasGems = true;
                break;
            }
    return out;
}

// Practice (PS2 0x00106840) [C]: the clock starts one measure before the
// start section's measure (FUN_002cccc0: measure m - 1, beat 0); in the
// first measure four beats before 0 [I: the PS2's -4 x a time at tick 0];
// practice_end comes at the end section's end, and the player counts the
// gems of the sections only (+0xf0 / +0xf4), the ones before the clock's
// start skipped.
void App::setupPractice() {
    practiceEndSent_ = false;
    practiceStartMs_ = 0, practiceEndMs_ = 1e30f;
    if (mode_ != DevMode::Practice || !song_ || !player_) return;
    const TempoMap& tm = song_->tempo;
    const int startTick = std::max(0, practiceStartTick_);
    const int endTick = practiceEndTick_ >= 0 ? practiceEndTick_ : int(tm.msToTick(song_->endMs));
    const float sectionMs = float(tm.tickToMs(startTick));
    std::vector<float> measures;
    for (const BeatLine& b : song_->beats)
        if (b.measure) measures.push_back(b.ms);
    size_t m = 0;
    while (m + 1 < measures.size() && measures[m + 1] <= sectionMs + 0.5f) ++m;
    const float beatMs = float(tm.tickToMs(tm.division()) - tm.tickToMs(0));
    practiceStartMs_ = m >= 1 ? measures[m - 1] : -4.0f * beatMs;
    practiceEndMs_ = std::min(song_->endMs, float(tm.tickToMs(endTick)));
    const auto& gems = player_->chart().gems;
    int first = 0;
    while (first < int(gems.size()) && gems[size_t(first)].ms < practiceStartMs_) ++first;
    player_->setPracticeRange(first, startTick, endTick);
    SDL_Log("practice: %.0f%% (%s), ticks %d..%d, the clock from %.0f ms, practice_end at %.0f ms", double(practiceSpeed_) * 100,
            audio_ ? audio_->moggPath.c_str() : "?", startTick, endTick, double(practiceStartMs_), double(practiceEndMs_));
}

void App::makeGameParsers() {
    if (!ui_ || gameParsers_) return;
    Interp& in = ui_->interp();
    gameParsers_ = std::make_unique<MidiParserSet>(in, *data_, std::vector<std::string>{"click_parser"});
    // events_parser: type 3 (echo) on EVENTS (0x0023d768) [C].
    eventsParserDef_ = parseDtaText("(events_parser (type echo) (track_name EVENTS))");
    if (!eventsParserDef_.children.empty()) {
        eventsParser_ = std::make_unique<MidiParser>("events_parser", eventsParserDef_.children[0]);
        in.registerObject(eventsParser_.get());
    }
}

void App::dropGameParsers() {
    if (eventsParser_ && ui_) ui_->interp().unregisterName("events_parser", eventsParser_.get());
    eventsParser_.reset();
    gameParsers_.reset();
}

void App::loadGameParsers(const SongAudio& audio) {
    if (!ui_ || !song_) return;
    try {
        const MidiFile midi = parseMidi(data_->read(audio.midiPath));
        if (gameParsers_) gameParsers_->load(midi, song_.get());
        if (eventsParser_) eventsParser_->load(ui_->interp(), midi, song_.get());
    } catch (const std::exception& e) {
        SDL_Log("game parsers: %s", e.what());
    }
}

// Practice's background channels (0x822CE830, 0x823347B8) [C]: silent below
// 100% (unless the dev cheat $stretch_bg, cheats.dta "Stretch Background"),
// always practice_attenuation down (beatmatcher.dta, 7.5).
float App::practiceBackgroundDb() const {
    if (!ui_) return -7.5f;
    Interp& in = ui_->interp();
    if (practiceSpeed_ < 1.0f && !in.var("stretch_bg").truthy()) return -96.0f;
    const DataNode* bm = ui_->bootTree() ? ui_->bootTree()->find("beatmatcher") : nullptr;
    const DataNode* audio = bm ? bm->find("audio") : nullptr;
    const DataNode* att = audio ? audio->find("practice_attenuation") : nullptr;
    return att && att->children.size() > 1 ? -att->floatAt(1) : -7.5f;
}

void App::beginSong() {
    if (!pendingStream_ || !info_) return;
    warmWorld();  // anything loaded since the song's load (a venue world_panel loaded after it)
    markFrame("song_begin");
    uploadsAtBegin_ = gpuUploads();
    // GamePanel's start (0x822CCA60): the intro clock from the INTRO shot's
    // length (CamShot +0xf0 = every keyframe's duration + blend, frames at
    // 30 a second, 0x82226630) [C].
    double startMs = kTrackExtendMs;
    if (!fastIntro_ && director_) {
        const scene::CamShot* shot = director_->drawShot({encoreFx_ ? "INTRO_ENCORE" : "INTRO"});
        // RB: the shot of the category itself (rbShot) [I].
        if (!shot && !rbShots_.empty())
            if ((shot = rbShot("INTRO"))) director_->forceShot(shot);
        if (shot) startMs = std::min(-double(shot->lengthFrames()) / 30.0 * 1000.0, 0.0);
    }
    rbVenueNote_ = rbVenueEvent_ = 0;
    // Practice (0x00106840): the clock starts at the practice start (no
    // intro: skip_intro, the track already out).
    const bool practice = mode_ == DevMode::Practice;
    if (practice) startMs = practiceStartMs_;
    leadInMs_ = std::max(0.0, -startMs);
    extendAtMs_ = float(std::max(startMs, kTrackExtendMs));
    if (skipIntro_) extendAtMs_ = float(startMs) - 10000.0f;
    extendSent_ = skipIntro_;
    rbExtendMs_ = skipIntro_ || practice ? float(startMs) : float(std::max(startMs, 0.0));
    if (rbHud_) rbHud_->reset(rbExtendMs_);  // track_panel.dta on_reset / on_extend (intro.trig)
    controllersLive_ = false;
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
    const SongAudio& audio = *audio_;
    // Practice: the song's own mogg (the _p85.. of songs.dta are not on the
    // disc) played at the speed from the start's place in it, the song
    // clock at the speed (set_music_speed: every channel voice as a
    // playback ratio, 0x826A22B8, and the clock, 0x822E1DF8) [C], so the
    // pitch drops: no pitch shift on the stream, none of the pitch_correct
    // keys in the executable [C] (GD02-034).
    const double speed = practice ? double(practiceSpeed_) : 1.0;
    if (startMs > 0) pendingStream_->seek(uint64_t(startMs / 1000.0 * pendingStream_->sampleRate()));
    // The channels of the part played are the player's track; the others the background (0x82336910) [C].
    const std::array<std::string, 2> practicePart{parts_[0], parts_[0]};
    // One player on another instrument than the guitar: its channels are the player's track.
    const bool ownPart = !player2_ && parts_[0] != "guitar";
    mixer_.setBackgroundDb(practice ? practiceBackgroundDb() : 0.0f);
    mixer_.play(std::move(pendingStream_),
                mixerTracks(audio, player2_ ? &parts_ : practice || ownPart ? &practicePart : nullptr), leadInMs_);
    mixer_.setSpeed(float(speed));
    mixer_.setClock(speed, std::max(0.0, startMs));
    // beatmatch set_paused before the game began (the tutorials' lessons
    // start with it paused: tut_script enters after the game panel).
    if (musicPaused_ || paused_) mixer_.setPaused(true);
    gameBeat_ = 0;
    if (song_)
        while (gameBeat_ < song_->beats.size() && song_->beats[gameBeat_].ms < startMs) ++gameBeat_;
    if (startMs > 0 && song_) {
        const double beat = song_->tempo.msToTick(startMs) / std::max(1, song_->tempo.division());
        if (gameParsers_)
            for (const auto& p : gameParsers_->parsers()) p->skipTo(beat);
        if (eventsParser_) eventsParser_->skipTo(beat);
    }
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
        track_.reset(), track2_.reset(), hud_.reset();
        // Rock Band: its own track (trackpanel.milo / tracksystem.milo), the
        // debug HUD's text over it until RB's HUD is drawn (M4) [I].
        try {
            auto rb = std::make_unique<RbTrackView>();
            rb->load(*data_, *milos_, "guitar");
            rbTrack_ = std::move(rb);
            try {
                auto hud = std::make_unique<RbHudView>();
                hud->load(*data_, *milos_);
                rbHud_ = std::move(hud);
            } catch (const std::exception& ex3) {
                SDL_Log("RB's HUD could not be loaded (%s)", ex3.what());
            }
            SDL_Log("RB's track loaded in %.0f ms", double(SDL_GetTicksNS() - start) / 1e6);
            return false;
        } catch (const std::exception& ex2) {
            SDL_Log("RB's track could not be loaded (%s); using the debug highway", ex2.what());
        }
        seats_.clear(), rbTrack_.reset(), rbHud_.reset(), sceneCache_.reset(), milos_.reset();
        return false;
    }
    SDL_Log("track and HUD loaded in %.0f ms", double(SDL_GetTicksNS() - start) / 1e6);
    return true;
}

void App::endSong() {
    if (!player_) return;
    // RB: the outro's screen change (ui/game.dta game_outro) must come even
    // when the song closes before the outro delay ran out.
    if ((wonSent_ || lostSent_) && !outroSent_ && ui_) {
        outroSent_ = true;
        ui_->gameEvent("game_outro_complete");
        ui_->gameEvent("game_outro");
    }
    input_.setTouchLanes(0, {}), input_.setTouchLanes(1, {});  // no lanes for the fingers until the next song
    SDL_Log("%s: score %d, %d/%d notes, longest streak %d%s (audio underruns: %lld frames)", info_->id.c_str(),
            player_->score(), player_->notesHit(), int(player_->chart().gems.size()), player_->longestStreak(),
            player_->failed() ? ", failed" : "", static_cast<long long>(mixer_.underruns()));
    for (int i = 0; i < 2; ++i) {
        const Player* p = i == 0 ? player_.get() : player2_.get();
        SongResult& res = lastResult_[size_t(i)];
        res = {};
        if (!p) continue;
        res.valid = true, res.failed = p->failed(), res.score = p->score(), res.notesHit = p->notesHit();
        res.gems = int(p->chart().gems.size()), res.longestStreak = p->longestStreak(), res.stars = rbStars(p);
        // lose.dta's "{percent_complete}% complete" asks after the song closed.
        const float at = failMs_ > -1e8f ? failMs_ : lastT_;
        res.percentComplete = song_ && song_->endMs > 0 ? std::clamp(int(at * 100.0f / song_->endMs), 0, 100) : 0;
    }
    if (player2_)
        SDL_Log("player 2: score %d, %d/%d notes, longest streak %d", player2_->score(), player2_->notesHit(),
                int(player2_->chart().gems.size()), player2_->longestStreak());
    if (coopBand_)
        SDL_Log("band: score %d, %d stars, longest streak %d%s", coopBand_->score(), coopBand_->stars(), coopBand_->longestStreak(),
                coopBand_->failed() ? ", failed" : "");
    mixer_.setPaused(false);
    mixer_.setSpeed(1);
    mixer_.setVolumeDb(0);
    mixer_.setBackgroundDb(0);
    mixer_.stop();
    band_.reset();
    if (gameParsers_)
        for (const auto& p : gameParsers_->parsers()) p->load(ui_->interp(), MidiFile{});
    if (eventsParser_) eventsParser_->load(ui_->interp(), MidiFile{});
    audio_ = nullptr;
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
    if (capture_) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
            running_ = false;
            return;
        }
        actions_.clear();
        input_.setInGame(true), input_.setOnePlayerSong(true);  // what a one-player song gets
        input_.handle(e, actions_);
        for (const InputEvent& a : actions_) {
            if (!a.down) continue;
            if (a.action == Action::Strum) ++captureCounts_[0];
            else if (isFret(a.action)) ++captureCounts_[size_t(1 + fretOf(a.action))];
            else if (a.action == Action::Whammy) ++captureCounts_[6];
            else if (a.action == Action::StarPower) ++captureCounts_[7];
        }
        return;
    }
    // A text entry takes typed letters, space and backspace (GH2's key message,
    // 0x82143880). The digits stay the frets, the Enter and Escape their menu keys.
    if (ui_ && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && ui_->textEntryFocused()) {
        const SDL_Keycode k = e.key.key;
        if ((k >= SDLK_A && k <= SDLK_Z) || k == SDLK_SPACE || k == SDLK_BACKSPACE) {
            ui_->typeKey(k == SDLK_BACKSPACE ? 8 : int(k));
            return;
        }
    }
    // GH2's developer keyboard cheats (Enhanced, GD02-045): a key not bound
    // to a keyboard control runs its cheats.dta entry.
    if (ui_ && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !ui_->textEntryFocused() && enhancedValue("dev_key_cheats") == "on") {
        bool bound = false;
        for (const auto& srcs : input_.bindings("keyboard"))
            for (const Input::Source& s : srcs) bound = bound || (s.kind == Input::Source::Kind::Key && s.index == int(e.key.key));
        int code = -1;
        if (e.key.key >= SDLK_F1 && e.key.key <= SDLK_F12) code = 401 + int(e.key.key - SDLK_F1);
        else if (const SDL_Keycode c = SDL_GetKeyFromScancode(e.key.scancode, e.key.mod, false); c >= 32 && c < 127) code = int(c);
        if (!bound && code >= 0 && ui_->keyCheat(code)) return;
    }
    if (e.type == SDL_EVENT_WINDOW_HDR_STATE_CHANGED && canvas_.renderer().ready()) {  // HDR on / off, paper white, headroom
        canvas_.renderer().device().hdrStateChanged();
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
        // gd01's: Escape (Back) three times in a row on the main menu quits
        // (universal-behaviors.md). RB's main menu goes back to the title
        // (splash_screen): the presses in a row that began on the main menu
        // go on counting there (GD05-033).
        if (a.down) {
            const std::string& sc = ui_->currentScreen();
            const bool counts = sc == "main_screen" || (sc == "splash_screen" && escapes_ > 0);
            escapes_ = a.action == Action::Back && counts ? escapes_ + 1 : 0;
        }
        if (escapes_ >= 3) {
            running_ = false;
            return;
        }
        if (player_ && begun_ && !paused_) playerAction(a);
        if (a.player >= 0 && a.player < 2) {
            unsigned& f = padFrets_[size_t(a.player)];
            if (isFret(a.action)) f = a.down ? f | (1u << fretOf(a.action)) : f & ~(1u << fretOf(a.action));
            if (a.action == Action::Strum && a.down) ++padStrums_[size_t(a.player)];
        }
        // One player in a song: every device plays it (playerFor) and every
        // device's Start pauses it, as gd02's (GD05-050: the keyboard, player
        // 2 beside a guitar left plugged in, could not pause).
        InputEvent ev = a;
        if (player_ && !player2_ && seats_.size() <= 1) ev.player = 0;  // a song (also paused)
        // In a song Back (Esc, a joypad's B) pauses like Start, as in gd01:
        // game_screen pauses on kPad_Start only.
        if (ev.action == Action::Back && input_.inGame()) {
            ev.action = Action::Start;
            ui_->action(ev);
            return;
        }
        ui_->action(ev);
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
    if (!seats_.empty()) return pad == pads_[0] ? player_.get() : seatPlayer(pad);
    if (!player2_) return player_.get();
    if (pads_[1] == pad) return player2_.get();
    if (pads_[0] == pad) return player_.get();
    return nullptr;
}

void App::playerAction(const InputEvent& a) {
    if (exploded_ || wonSent_ || lostSent_) return;
    Player* p = playerFor(a.player);
    if (!p || botFor(p)) return;  // autoplay owns that player's frets
    // GH2 stamps a press with the song time of the Beatmatcher's last poll
    // (the watcher reads Beatmatcher +0x90 through 0x8233BBF0, written only
    // by its poll 0x8233A208): the frame's game time, the same for every
    // press in a frame (the joypad poll runs before the game's), so its
    // 52 ms strum repeat and chord-by-fret checks compare frame times
    // (GD02-152) [C; the poll order I].
    const float t = lastT_;
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
    for (RbSeat& st : seats_)
        if (st.vocal && st.player.get() == p) {
            return;  // the singer deploys by singing (screamPoll) [C]
        }
    if (vocal_ && p == player_.get()) {
        // The singer deploys by singing loud in a freestyle section (VocalPlayer screamPoll, RB 0x82458F70) [C]: no button.
        return;
    }
    p->update(t);
    // One player, every device (D35): a press of another kind of device
    // (a gamepad after the keyboard worked the menus) switches the watcher.
    if (a.down && (isFret(a.action) || a.action == Action::Strum) && !player2_ && seats_.size() <= 1 && a.mode != p->controllerMode())
        p->setControllerMode(a.mode);
    if (rbBonus_ && p == player_.get() && isFret(a.action) && a.down) rbBonus_->codaStrike(fretOf(a.action), t);
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

// The instrument of local player `pad` (RB's kController*): GD05_INSTRUMENT
// (a list per player: guitar, bass, drum, vocals) first, then the
// player's GAMEPADS choice (Enhanced "instrument_p<n>"), then the device:
// a drum kit (its name says drum) plays the drums [I: the port's
// detection; RB asks the controller's type].
int App::controllerType(int pad) {
    auto typeOf = [](const std::string& v) {
        if (v == "drum" || v == "drums") return 0;
        if (v == "vocals" || v == "mic") return 2;
        if (v == "guitar" || v == "bass") return 1;
        return -1;
    };
    if (const char* e = SDL_getenv("GD05_INSTRUMENT")) {
        std::string v = e;
        for (int i = 0; i < pad && !v.empty(); ++i) {
            const size_t c = v.find(',');
            v = c == std::string::npos ? std::string() : v.substr(c + 1);
        }
        v = v.substr(0, v.find(','));
        if (const int t = typeOf(v); t >= 0) return t;
    }
    if (const int t = typeOf(enhancedValue("instrument_p" + std::to_string(pad + 1))); t >= 0) return t;
    for (const Input::Device& d : input_.devices()) {
        if (d.player != pad) continue;
        // GAMEPADS' instrument (Drums, Vocals) for the player's device.
        const Input::Instrument inst = input_.instrument(d.id);
        if (inst == Input::Instrument::Drums) return 0;
        if (inst == Input::Instrument::Vocals) return 2;
        std::string n = d.name;
        for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (n.find("drum") != std::string::npos) return 0;
    }
    return 1;
}

// ---- Rock Band's band (wave 02) ----------------------------------------
// Players 2..4 (GameSetup::rbBand, or --part a,b,c,d): each its part's
// judge (the gem Player, or the vocal judge carried by an empty-chart
// Player), RB's track for its instrument, its bonuses. A player whose pad
// has no controller autoplays (a development aid for --part lists) [I].
void App::makeSeats(bool autoplay) {
    seats_.clear();
    unison_.clear(), bandNotices_.clear();
    unisonBonuses_ = saves_ = 0;
    player0Out_ = false;
    fails_ = {}, outMs_ = {};
    bool haveVocals = vocal_ != nullptr;
    for (const GameSetup::RbMember& m : rbBandSetup_) {
        RbSeat st;
        st.pad = m.pad, st.difficulty = std::clamp(m.difficulty, 0, kDifficultyCount - 1);
        Part part = partFor(*song_, m.part == "drums" ? "drum" : m.part, false);
        if (part == Part::Vocals && (song_->vocals.empty() || haveVocals)) part = Part::Guitar;
        if (part == Part::Vocals) haveVocals = true;
        st.part = part == Part::Drums ? "drum" : part == Part::Bass ? "bass" : part == Part::Vocals ? "vocals" : "guitar";
        const Rules& r = rules_;
        st.player = std::make_unique<Player>(*song_, part, static_cast<Difficulty>(st.difficulty), r,
                                             part == Part::Drums ? ControllerMode::Joypad : ControllerMode::Guitar, true, -1, nullptr);
        st.player->setNumber(int(seats_.size()) + 1);
        st.player->setDrumMode(part == Part::Drums);
        st.player->setCalibrationMs(calibrationMs());
        const bool bot = autoplay || !input_.padConnected(m.pad);
        if (part == Part::Vocals) {
            st.vocal = std::make_unique<VocalPlayer>(*song_, static_cast<Difficulty>(st.difficulty), rules_);
            st.vocal->setDrainMs(overdriveDrainMs());
            if (!bot) {
                st.mic = std::make_unique<VocalInput>();
                std::string err;
                if (!st.mic->open("sdl3", err)) st.mic.reset();
            }
        } else {
            if (bot) st.bot = std::make_unique<Autoplay>(st.player->chart(), -1);
            st.bonus = std::make_unique<RbBonus>(*song_, part, static_cast<Difficulty>(st.difficulty), rules_);
        }
        if (bot && part == Part::Vocals) st.loud = -2;  // the autoplay sings (updateSeats)
        SDL_Log("band: player %d plays %s (%s)%s", int(seats_.size()) + 2, st.part.c_str(), difficultyName(static_cast<Difficulty>(st.difficulty)),
                bot ? ", autoplay" : "");
        seats_.push_back(std::move(st));
    }
    // Unison: an Overdrive phrase every instrument player has (the same start).
    std::vector<const Chart*> charts;
    if (!vocal_) charts.push_back(&player_->chart());
    for (const RbSeat& st : seats_)
        if (!st.vocal) charts.push_back(&st.player->chart());
    if (charts.size() > 1)
        for (const Chart* c : charts)
            for (const StarPhrase& ph : c->phrases) ++unison_[ph.startTick].second;
}

Player* App::seatPlayer(int pad) {
    for (RbSeat& st : seats_)
        if (st.pad == pad) return st.player.get();
    return nullptr;
}

void App::updateSeats(float t, bool over, const std::vector<PlayerEvent>& events0) {
    const int players = 1 + int(seats_.size());
    // Unison and saves from every player's events.
    auto phraseCaptured = [&](const Player& p, const PlayerEvent& e) {
        if (e.kind != PlayerEvent::Kind::PhraseCaptured || e.gem < 0 || size_t(e.gem) >= p.chart().gems.size()) return;
        const int ph = p.chart().gems[size_t(e.gem)].phrase;
        if (ph < 0 || size_t(ph) >= p.chart().phrases.size()) return;
        auto it = unison_.find(p.chart().phrases[size_t(ph)].startTick);
        if (it == unison_.end() || it->second.second < 2) return;
        // Members out of play neither block nor get it (RB 0x82465950, 0x82465C00) [C].
        int outNow = (!vocal_ && player0Out_) ? 1 : 0;
        for (const RbSeat& q : seats_) outNow += !q.vocal && q.out;
        if (++it->second.first + outNow == it->second.second && it->second.first >= 2) {
            // Every instrument captured it: the Unison bonus, a second
            // phrase's worth of Overdrive each (unison_phrase reward 2.0) [I].
            ++unisonBonuses_;
            bandNotices_.push_back({"UNISON BONUS!", t});
            auto boost = [&](Player& q) { q.setStarMeter(q.starMeter() + rules_.solo.starPower.phrase); };
            if (!vocal_) boost(*player_);
            for (RbSeat& st : seats_)
                if (!st.vocal) boost(*st.player);
        }
    };
    bool deployed = false;
    for (const PlayerEvent& e : events0) {
        phraseCaptured(*player_, e);
        deployed |= e.kind == PlayerEvent::Kind::StarPowerStart;
    }
    for (RbSeat& st : seats_) {
        if (over) break;
        Player& p = *st.player;
        if (st.vocal) {
            float loud = 0, pitch = -1;
            if (st.mic) pitch = st.mic->poll(t, loud);
            else if (st.loud == -2 || st.loud >= 0) {
                pitch = st.vocal->targetPitch(t);
                loud = st.vocal->noteAt(t) ? 0.3f : 0.0f;
            }
            st.pitch = pitch;
            if (!st.mic) st.loud = loud > 0 ? loud : -2;
            else st.loud = loud;
            if (!st.out) st.vocal->sing(t, pitch, loud);
            const bool wasOd = st.vocal->overdriveActive();
            st.vocal->update(t);
            deployed |= !wasOd && st.vocal->overdriveActive();
            for (const VocalPlayer::Event& e : st.vocal->takeEvents()) {
                if (e.kind != VocalPlayer::Event::Kind::PhraseRated) continue;
                st.lastRating = VocalPlayer::ratingName(e.rating), st.lastRatingMs = t;
                st.rating = std::clamp(st.rating + 0.1f * (float(int(e.rating)) / 4.0f - st.rating), 0.0f, 1.0f);  // RB 0x824673D0 [C]
                if (e.rating >= VocalPlayer::Rating::Okay) ++st.phrasesHit;
            }
            int mult = 1;
            if (auto it = rules_.rb.streakMultipliers.find("vocals"); it != rules_.rb.streakMultipliers.end())
                for (const auto& [count, m] : it->second)
                    if (st.vocal->streak() >= count) mult = m;
            p.setExternal(st.vocal->score(), st.vocal->streak(), mult, st.vocal->overdriveActive(), st.vocal->overdrive(), st.rating, st.phrasesHit);
        } else {
            if (st.bot && !st.out) st.bot->feed(p, t);
            p.update(t);
            const std::vector<PlayerEvent> ev = p.takeEvents();
            if (st.bonus) st.bonus->update(p, ev, t);
            for (const PlayerEvent& e : ev) {
                phraseCaptured(p, e);
                deployed |= e.kind == PlayerEvent::Kind::StarPowerStart;
                if (e.kind == PlayerEvent::Kind::Hit && ui_) ui_->gameEvent("hit", {Value::integer(int(&st - seats_.data()) + 1)});
            }
            st.rating = p.rockMeter();
        }
    }
    if (over) return;
    // The band's multiplier: bonuses multiplier[members deploying] for every
    // member (RB 0x8243F810, 0x824415A8) [C].
    {
        int deploying = (vocal_ ? vocal_->overdriveActive() : player_->starPowerActive()) ? 1 : 0;
        for (const RbSeat& st : seats_) deploying += st.vocal ? st.vocal->overdriveActive() : st.player->starPowerActive();
        const auto& bm = rules_.rb.bandMultiplier;
        const int bonus = bm.empty() ? 1 << std::min(deploying, 3) : bm[std::min<size_t>(size_t(deploying), bm.size() - 1)];
        player_->setBandBonus(bonus);
        if (vocal_) vocal_->setBandBonus(bonus);
        for (RbSeat& st : seats_) {
            st.player->setBandBonus(bonus);
            if (st.vocal) st.vocal->setBandBonus(bonus);
        }
    }
    // Failing out and saving (RB's Player states, 0x82444BB8; fail check
    // 0x824418C8, save loop 0x8243F548, deploy 0x82442B20) [C]: a member
    // whose meter falls to the lose level fails out (energy 0, every
    // member's deploy stopped); while out their meter drains below it by
    // crowd_loss_per_sec x (players - 1); another member's deploy saves
    // every member out who has failed at most twice (back at save_level,
    // judged again after time_to_return_from_brink) and costs the deployer
    // save_energy instead of Overdrive; the band fails when the members'
    // weighted meter (equal weights, scoring.dtb (weights)) is under
    // multiplayer_lose_level. [I: the lose level as the meter's 0, the
    // brink kept as muted judging-on.]
    const float t0 = t;
    auto isOut = [&](int idx) { return idx == 0 ? player0Out_ : seats_[size_t(idx - 1)].out; };
    auto setOut = [&](int idx, bool v) { (idx == 0 ? player0Out_ : seats_[size_t(idx - 1)].out) = v; };
    auto rating = [&](int idx) {
        if (idx == 0) return vocal_ ? vocalRating_ : player_->rockMeter();
        return seats_[size_t(idx - 1)].rating;
    };
    auto setRating = [&](int idx, float v) {
        if (idx == 0) { if (vocal_) vocalRating_ = v; else player_->setCrowdRating(v); return; }
        RbSeat& st = seats_[size_t(idx - 1)];
        st.rating = v;
        if (!st.vocal) st.player->setCrowdRating(v);
    };
    auto spend = [&](int idx, float cost) {
        if (idx == 0) { if (vocal_) vocal_->spendSave(cost); else player_->spendSave(cost); return; }
        RbSeat& st = seats_[size_t(idx - 1)];
        if (st.vocal) st.vocal->spendSave(cost); else st.player->spendSave(cost);
    };
    for (int k = 0; k < players; ++k)
        if (!isOut(k) && rating(k) <= 0.0f) {
            setOut(k, true);
            ++fails_[size_t(k)], outMs_[size_t(k)] = t0;
            for (int q = 0; q < players; ++q) spend(q, q == k ? 1.0f : 0.0f);  // its energy gone, every deploy stopped
            bandNotices_.push_back({"PLAYER " + std::to_string(k + 1) + " FAILED!", t0});
        }
    if (deployed) {
        bool saved = false;
        for (int k = 0; k < players; ++k)
            if (isOut(k) && fails_[size_t(k)] <= 2) {
                setOut(k, false), ++saves_, saved = true;
                setRating(k, rules_.rb.saveLevel);
                // Back from the brink after time_to_return_from_brink, at the first
                // gem from then (RB 0x82442060, 0x82452E68) [C].
                Player* who = k == 0 ? player_.get() : seats_[size_t(k - 1)].player.get();
                const auto& gems = who->chart().gems;
                const float back = t0 + rules_.rb.timeToReturnFromBrink * 1000.0f;
                int g = 0;
                while (g < int(gems.size()) && gems[size_t(g)].ms < back) ++g;
                who->skipUntil(g);
                bandNotices_.push_back({"PLAYER " + std::to_string(k + 1) + " SAVED!", t0});
            }
        if (saved)
            for (int q = 0; q < players; ++q) {
                const bool active = q == 0 ? (vocal_ ? vocal_->overdriveActive() : player_->starPowerActive())
                                           : (seats_[size_t(q - 1)].vocal ? seats_[size_t(q - 1)].vocal->overdriveActive() : seats_[size_t(q - 1)].player->starPowerActive());
                if (active) spend(q, rules_.rb.saveEnergy);
            }
    }
    // The band's Big Rock Ending: all bank or all lose once every
    // instrument member resolved it (RB 0x82440838) [C].
    {
        std::vector<std::pair<RbBonus*, Player*>> cod;
        if (rbBonus_ && rbBonus_->hasCoda()) cod.push_back({rbBonus_.get(), player_.get()});
        for (RbSeat& st : seats_)
            if (st.bonus && st.bonus->hasCoda()) cod.push_back({st.bonus.get(), st.player.get()});
        bool all = !cod.empty(), anyBlown = false;
        for (auto& [b, p] : cod) {
            b->setDeferCoda(true);
            all = all && b->codaResolved();
            anyBlown = anyBlown || (b->codaResolved() && !b->codaHit());
        }
        if (all)
            for (auto& [b, p] : cod) anyBlown ? b->loseCoda(t0) : b->bankCoda(*p, t0);
    }
    float sum = 0;
    for (int k = 0; k < players; ++k)
        sum += isOut(k) ? -(t0 - outMs_[size_t(k)]) * rules_.rb.crowdLossPerSec * 0.001f * float(players - 1) : rating(k);
    const bool noFail = options_.noFail || enhancedValue("no_fail") == "on" || bot_ != nullptr;
    if (!noFail && !songLost() && sum / float(players) < rules_.rb.multiplayerLoseLevel) player_->forceFail();
}

void App::drawSeats(float t, float speed) {
    int gemTrack = rbTrack_ && !vocal_ ? 1 : 0;  // gem_track_index
    for (RbSeat& st : seats_) {
        if (st.vocal) {
            VocalTrackView::State vs;
            vs.t = t + float(videoOffsetMs_);
            vs.voicePitch = st.pitch, vs.loudness = std::max(0.0f, st.loud);
            vs.lastRating = st.lastRating, vs.lastRatingMs = st.lastRatingMs;
            vocalView_.draw(canvas_, song_->vocals, *st.vocal, vs);
            continue;
        }
        if (!st.track) continue;
        RbTrackView::PlayerView pv;
        const Player* p = st.player.get();
        pv.chart = &p->chart();
        pv.hit = [p](int i) { return p->gemHit(i) || !p->owned(i); };
        pv.missed = [p, t](int i) { return !p->gemHit(i) && p->chart().gems[size_t(i)].ms < t - 150.0f; };
        pv.phraseIntact = [p](int ph) { return p->phraseIntact(ph); };
        pv.overdriveActive = p->starPowerActive();
        pv.heldGem = p->sustainGem();
        pv.whammy = p->whammyBar();
        pv.heldFrets = p->heldFrets();
        pv.struck = [p](int i) { return p->gemHit(i); };
        if (st.part == "drum") pv.laneOfBit = {4, 1, 2, 3, 0};
        st.track->setIntroAt(rbIntroAt(gemTrack++));
        st.track->draw(canvas_.renderer(), *sceneCache_, *song_, pv, t + float(videoOffsetMs_), speed);
    }
}

// The band's numbers under each track: score, multiplier, the Overdrive and
// crowd meters, FAILED; the band's score at the top [I: a stand-in for RB's
// own HUD (track/gen/scoreboard.milo, band_power_meter.milo, crowd_meter.milo)].
void App::drawBandHud(float t) {
    canvas_.flush();
    struct Row {
        std::string part;
        const Player* p;
        float rating;
        bool out;
    };
    std::vector<Row> rows;
    if (!vocal_) rows.push_back({parts_[0], player_.get(), player_->rockMeter(), player0Out_});
    for (const RbSeat& st : seats_)
        if (!st.vocal) rows.push_back({st.part, st.player.get(), st.rating, st.out});
    const float w = kScreenW / float(std::max<size_t>(1, rows.size()));
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const float x = w * float(i) + 12, y = kScreenH - 46;
        char b[64];
        std::snprintf(b, sizeof b, "%s %d x%d", r.part.c_str(), r.p->score(), r.p->multiplier());
        canvas_.text(x, y, 1.5f, r.out ? SDL_Color{240, 80, 70, 255} : SDL_Color{235, 235, 240, 255}, r.out ? r.part + " FAILED" : std::string(b));
        canvas_.fillRect(x, y + 16, w - 40, 6, {30, 30, 40, 220});
        canvas_.fillRect(x, y + 16, (w - 40) * r.p->starMeter(), 6, r.p->starPowerActive() ? SDL_Color{120, 220, 255, 255} : SDL_Color{240, 200, 70, 255});
        canvas_.fillRect(x, y + 24, w - 40, 4, {30, 30, 40, 220});
        canvas_.fillRect(x, y + 24, (w - 40) * r.rating, 4, r.rating < 0.25f ? SDL_Color{230, 60, 50, 255} : SDL_Color{110, 210, 90, 255});
    }
    char b[48];
    std::snprintf(b, sizeof b, "BAND %d", gameCounter(0, "band_score"));
    canvas_.text(kScreenW - 260, kScreenH - 70, 2, {255, 230, 120, 255}, b);
    for (const auto& [text, ms] : bandNotices_)
        if (t - ms >= 0 && t - ms < 2500) canvas_.textCentered(kScreenW * 0.5f, 290, 3, {255, 235, 120, 255}, text);
}

// RB's stars for a player (RbBonus::stars with the band's instrument set,
// the solo bonus base, the player's difficulty) [C: the rule; I: the base].
int App::rbStars(const Player* p) const {
    std::vector<std::string> set{vocal_ ? "vocals" : parts_[0]};
    for (const RbSeat& st : seats_) set.push_back(st.part);
    for (std::string& x : set)
        if (x == "drum") x = "drum";
    int base = p->baseScore(), bonus = rbBonus_ && p == player_.get() ? rbBonus_->soloBonusMax() : 0;
    if (vocal_ && p == player_.get()) base = int(vocal_->phrases()) * int(rules_.rb.vocals.count("phrase_value") ? rules_.rb.vocals.at("phrase_value")[size_t(std::clamp(difficulties_[0], 0, 3))] : 400);
    for (const RbSeat& st : seats_)
        if (st.player.get() == p && st.bonus) bonus = st.bonus->soloBonusMax();
    const int diff = p == player_.get() ? difficulties_[0] : 3;
    return RbBonus::stars(rules_, p->score(), base, bonus, set, diff);
}

// Overdrive's length on the vocals: band_energy's deploy beats (32,
// scoring.dtb) at the song's mean beat [I: RB drains by the tempo map].
float App::overdriveDrainMs() const {
    if (!song_ || song_->beats.size() < 2) return 16000.0f;
    const float beatMs = (song_->beats.back().ms - song_->beats.front().ms) / float(song_->beats.size() - 1);
    return 32.0f * beatMs;
}

// One frame of the singer: the voice (the microphone, the file, or the
// autoplay singing the target), the judge, its ratings into the crowd
// meter and the HUD's numbers [I: the crowd steps per rating].
void App::updateVocals(float t) {
    float loud = 0, pitch = -1;
    if (mic_) pitch = mic_->poll(t, loud), micLoud_ = pitch > 0;  // talking: a pitch heard [I]
    if (bot_ && !mic_) {
        pitch = vocal_->targetPitch(t);
        loud = vocal_->percussionDue(t) ? 0.6f : vocal_->noteAt(t) ? 0.3f : 0.0f;  // taps the tambourine's gems
    }
    if (t < voiceShoutUntilMs_) loud = 1.0f, pitch = -1;
    voicePitch_ = pitch, voiceLoud_ = loud;
    if (SDL_getenv("GD05_VOCALLOG")) {
        static float next = 0;
        if (t >= next) next = t + 2000, SDL_Log("vocals %.0f: pitch %.1f loud %.2f target %.1f phrase %d score %d drawn %d", double(t), double(pitch), double(loud), double(vocal_->targetPitch(t)), vocal_->phraseAt(t), vocal_->score(), vocalView_.notesDrawn());
    }
    vocal_->sing(t, pitch, loud);
    vocal_->update(t);
    for (const VocalPlayer::Event& e : vocal_->takeEvents()) {
        if (e.kind != VocalPlayer::Event::Kind::PhraseRated) continue;
        vocalRatingName_ = VocalPlayer::ratingName(e.rating);
        vocalRatingMs_ = t;
        // RB 0x824537E8 -> 0x824673D0 [C]: the meter moves toward rating / 4 by
        // the vocals' crowd phrase_weight (0.1, scoring.dtb) each phrase.
        vocalRating_ = std::clamp(vocalRating_ + 0.1f * (float(int(e.rating)) / 4.0f - vocalRating_), 0.0f, 1.0f);
        if (e.rating >= VocalPlayer::Rating::Okay) ++vocalPhrasesHit_;
    }
    int mult = 1;
    if (auto it = rules_.rb.streakMultipliers.find("vocals"); it != rules_.rb.streakMultipliers.end())
        for (const auto& [count, m] : it->second)
            if (vocal_->streak() >= count) mult = m;
    player_->setExternal(vocal_->score(), vocal_->streak(), mult, vocal_->overdriveActive(), vocal_->overdrive(), vocalRating_, vocalPhrasesHit_);
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
            playSfxArgs(kIntro[introSfx_++].sfx, fxMuted_ ? -96.0f : fxVolumeDb_, 0, 0);
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
    for (size_t k = 0; k < events.size(); ++k) {
        const PlayerEvent& e = events[k];
        if (ui_) {
            gameCallbacks(player, events, k);
            continue;
        }
        const char* matcher = nullptr;
        if (e.kind == PlayerEvent::Kind::Miss) matcher = "miss";
        else if (e.kind != PlayerEvent::Kind::PhraseCaptured && e.kind != PlayerEvent::Kind::StarPowerStart &&
                 e.kind != PlayerEvent::Kind::StarPowerEnd)
            continue;
        if (matcher) playSfxArgs(bass ? "miss_bass" : "miss_gtr", fxMuted_ ? -96.0f : fxVolumeDb_, pan, 0);
        else if (e.kind == PlayerEvent::Kind::PhraseCaptured) pendingAwardMs_ = lastT_ + 200.0f;
        else if (e.kind == PlayerEvent::Kind::StarPowerStart) {
            playSfxArgs(multi ? "sp_deployed_multi" : "sp_deployed", fxMuted_ ? -96.0f : fxVolumeDb_, pan, 0);
            if (track_) track_->animateExtend(2000, 2025, 800, lastT_);
        } else {
            playSfxArgs(multi ? "sp_depleted_multi" : "sp_depleted", fxMuted_ ? -96.0f : fxVolumeDb_, pan, 0);
        }
    }
    if (!ui_ && pendingAwardMs_ > -1e8f && lastT_ >= pendingAwardMs_) {
        pendingAwardMs_ = -1e9f;
        playSfxArgs("sp_awarded", fxVolumeDb_, 0, 0);
    }
}

// One player event to the menus' scripts: the matcher's handlers (miss,
// hopo, swingAtHopo: beatmatch.dta) and the player's (phrase_captured,
// phrase_miss, start_using, stop_using, whammy_start, whammy_end,
// star_gem_hit: player.dta), whose {handle ($this x_callback)} reaches
// the object's sinks; and what the matcher sends the sinks itself [C]:
// gem_hit_callback $slot $hopoable $hopoed (0x822E2748: the gem's lowest
// fret, its HOPO flag, hit as a HOPO), gem_miss_callback $slot_hit $real_slot
// $timing_error (0x822E2B10: the fret played, the gem's lowest fret, how far
// the gem is), gem_pass_callback (0x822E2FD0), held_note_released_callback
// $fraction (the player).
void App::gameCallbacks(int player, const std::vector<PlayerEvent>& events, size_t k) {
    const PlayerEvent& e = events[k];
    const Player* pl = player == 0 ? player_.get() : player2_.get();
    Interp& in = ui_->interp();
    ScriptObject* matcher = in.findObject("player_matcher" + std::to_string(player));
    ScriptObject* perf = in.findObject("player" + std::to_string(player));
    const Value num = Value::integer(player);
    const auto slot = [&](int gem) {
        if (!pl || gem < 0 || gem >= int(pl->chart().gems.size())) return -1;
        const unsigned f = pl->chart().gems[size_t(gem)].frets;
        return f ? std::countr_zero(f) : -1;
    };
    using K = PlayerEvent::Kind;
    switch (e.kind) {
    case K::Miss:
        // RB has no matcher handlers: config/player.dta's player handlers
        // take (miss $player_num $slot) and play miss_gtr.cue / miss_bass /
        // miss_kick.cue / miss_drum by the track type [C data]; the port
        // sent GH2's matcher only, so a miss was silent (GD05-057).
        if (matcher && matcher->handler("miss")) ui_->callScript(matcher, "miss", {num});
        else ui_->callScript(perf, "miss", {num, Value::integer(e.slot >= 0 ? e.slot : std::max(0, slot(e.gem)))});
        in.send(matcher, "gem_miss_callback", {Value::integer(e.slot >= 0 ? e.slot : slot(e.gem)), Value::integer(slot(e.gem)), Value::real(e.value)}, false);
        break;
    case K::Hit: {
        bool hopoed = false;
        for (size_t j = k + 1; j < events.size() && !hopoed; ++j) hopoed = events[j].kind == K::Hopo && events[j].gem == e.gem;
        const bool hopoable = pl && e.gem >= 0 && pl->chart().gems[size_t(e.gem)].hopo;
        in.send(matcher, "gem_hit_callback", {Value::integer(slot(e.gem)), Value::integer(hopoable ? 1 : 0), Value::integer(hopoed ? 1 : 0)}, false);
        break;
    }
    case K::Pass: in.send(matcher, "gem_pass_callback", {}, false); break;
    case K::Hopo: ui_->callScript(matcher, "hopo", {}); break;
    case K::SwingAtHopo: ui_->callScript(matcher, "swingAtHopo", {}); break;
    case K::HeldNoteReleased: in.send(perf, "held_note_released_callback", {Value::real(e.value)}, false); break;
    case K::PhraseCaptured: ui_->callScript(perf, "phrase_captured", {num}); break;
    case K::PhraseMissed: ui_->callScript(perf, "phrase_miss", {Value::integer(e.gem)}); break;
    // Rock Band's player.dta: deploy / stop_deploy (rp_deployed_<track>.cue,
    // rp_depleted_<track>.cue, the track's deploy) [C data]; GH2's start_using / stop_using.
    case K::StarPowerStart: ui_->callScript(perf, perf && perf->handler("deploy") ? "deploy" : "start_using", {num}); break;
    case K::StarPowerEnd: ui_->callScript(perf, perf && perf->handler("stop_deploy") ? "stop_deploy" : "stop_using", {num}); break;
    case K::WhammyStart: ui_->callScript(perf, "whammy_start", {}); break;
    case K::WhammyEnd: ui_->callScript(perf, "whammy_end", {}); break;
    case K::StarGemHit: ui_->callScript(perf, "star_gem_hit", {}); break;
    default: break;
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
        // The autoplay drummer plays a shown fill as a player does (a pad
        // every 150 ms, the lanes in turn): RB's crash deploys only after
        // crash_fill_ready_hits strikes in the fill (RbBonus::codaStrike)
        // [test aid, GD05-073: the autoplay hit the crash alone, never deployed].
        if (bot_ && rbBonus_ && rbBonus_->fillActive(t) && t - botFillStrikeMs_ >= 150.0f)
            rbBonus_->codaStrike(botFillLane_++ % 4, t), botFillStrikeMs_ = t;
        // A test aid: GD05_AUTODEPLOY=1 deploys Overdrive as soon as it is ready.
        static const bool autoDeploy = SDL_getenv("GD05_AUTODEPLOY") != nullptr;
        if (bot_ && autoDeploy && player_->starPowerReady()) player_->starPowerButton(t);
        if (t >= failAtMs_ && !songLost()) {
            if (coopBand_) coopBand_->forceFail();
            else player_->forceFail();
        }
        player_->update(t);
        if (vocal_) updateVocals(t);
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
    // A Star Power phrase counts as started once its first gem is hit or
    // passed: the stats' "<captured>/<started>" (Star Power object +0x5c /
    // +0x60, read at 0x822C7060; counted as Encore's 0x0011e9e0) [C].
    auto phraseStart = [](const Player* pl, const PlayerEvent& e) {
        if (!pl || e.gem < 0 || (e.kind != PlayerEvent::Kind::Hit && e.kind != PlayerEvent::Kind::Pass)) return false;
        const Chart& c = pl->chart();
        const int ph = size_t(e.gem) < c.gems.size() ? c.gems[size_t(e.gem)].phrase : -1;
        return ph >= 0 && size_t(ph) < c.phrases.size() && c.phrases[size_t(ph)].firstGem == e.gem;
    };
    if (rbBonus_ && !over) rbBonus_->update(*player_, events, t);
    if (!seats_.empty()) updateSeats(t, over || paused_, events);
    for (const PlayerEvent& e : events) spStarted_[0] += phraseStart(player_.get(), e);
    for (const PlayerEvent& e : events2) spStarted_[1] += phraseStart(player2_.get(), e);
    for (const PlayerEvent& e : events2) {
        if (e.kind == PlayerEvent::Kind::PhraseCaptured) ++spPhrases_[1];
        if (e.kind == PlayerEvent::Kind::EndStreak) ++streaksBroken_[1];
        if (e.kind == PlayerEvent::Kind::StarPowerReady && ui_) ui_->gameEvent("star_power_ready", {Value::integer(1)});
        if (e.kind == PlayerEvent::Kind::BlewBigNote && ui_) ui_->gameEvent("blew_big_note", {Value::integer(1)});
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
        if (e.kind == PlayerEvent::Kind::BlewBigNote && ui_) ui_->gameEvent("blew_big_note", {Value::integer(0)});
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
    // The whammy's bend: each player's slip voice, back to speed 1 once it
    // lags 0.9 x max_slip (270 ms); every hit jumps it back in sync.
    for (int k = 0; k < (player2_ && parts_[0] != parts_[1] ? 2 : 1); ++k) {
        const Player* pl = k == 0 ? player_.get() : player2_.get();
        if (pl->guitarHits() != slipHits_[k]) slipHits_[k] = pl->guitarHits(), mixer_.resyncGuitar(k);
        mixer_.setGuitarSlipSpeed(mixer_.guitarSlipMs(k) > 270 ? 1.0f : pl->bendSpeed(), k);
    }
    if (const char* w = SDL_getenv("GD05_WHAMMYLOG")) {  // development aid: a moving bar, logs the deepest lag so far
        static double most = 0;
        player_->setWhammyValue(0.5f + 0.5f * std::sin(t / 150.0f), t);
        if (*w && mixer_.guitarSlipMs(0) > most + 4) most = mixer_.guitarSlipMs(0), SDL_Log("whammy: guitar lag %.0f ms, speed %.3f", most, player_->bendSpeed());
    }
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
            rbShots(t);
            director_->update();
            // Development aid: GD05_FORCE_SHOT=<CamShot> plays that shot whenever another starts;
            // #N the venue's N-th BandCamShot by name (the rb_look walk's fixed shots).
            if (const char* fs = SDL_getenv("GD05_FORCE_SHOT"))
                if (const auto* shot = forcedShot(fs); shot && director_->shot() != shot) {
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
        // Development aid: GD05_FACE_CAM=<member>[:<distance>[:<degrees>[:<bone>[:<height>]]]] (guitarist0,
        // singer, ...) frames that member's head (or bone) from the director camera's side, turned
        // <degrees> about it (180: from behind), <height> units above it, to check the faces.
        if (const char* fc = SDL_getenv("GD05_FACE_CAM"); fc && *fc && band_) {
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
    // The beatmatcher's beat and downbeat to the game (0x00121e78, $beat
    // the beat's number) [C]: game.dta's claps under Star Power, the
    // players' on_downbeat; the world has its own (game/world_driver.h).
    if (!paused_)
        for (; gameBeat_ < song_->beats.size() && song_->beats[gameBeat_].ms <= t; ++gameBeat_) {
            ui_->interp().var("beat") = Value::integer(int(gameBeat_));
            ui_->gameScript("beat");
            if (song_->beats[gameBeat_].measure) ui_->gameScript("downbeat");
        }
    // The game's parsers at the song's beat (events_parser, click_parser).
    if (!paused_ && t >= 0) {
        const double beat = song_->tempo.msToTick(t) / std::max(1, song_->tempo.division());
        if (gameParsers_) gameParsers_->poll(beat);
        if (eventsParser_) eventsParser_->poll(ui_->interp(), beat);
    }
    // Practice has no game over (game_over_sequence FALSE): practice_end at
    // the end section's end (the task 0x00106840 schedules) [C].
    if (mode_ == DevMode::Practice) {
        if (!practiceEndSent_ && t >= practiceEndMs_) {
            practiceEndSent_ = true;
            ui_->gameEvent("practice_end");
        }
        return;
    }
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
        ui_->gameEvent("game_won");  // RB's game panel (ui/game.dta game_won) [C data]
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
        // RB: game_outro asks band_ui_sink sync_screen for the mode's win /
        // lose screen (ui/game.dta) [C data; the delay is GH2's: I].
        ui_->gameEvent("game_outro");
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
    if (!songLost() && !exploded_ && t >= song_->endMs && mode_ != DevMode::Practice) {
        exploded_ = true;
        if (track_) track_->explode(t);
        if (track2_) track2_->explode(t);
        if (hud_) hud_->won(t);
    }
    drawSong(t);
    // A failed song closes a little after the fail (GH2's lose screen comes later).
    if (songLost() && failMs_ < -1e8f) failMs_ = t, failTicks_ = SDL_GetTicksNS();
    const float outro = exploded_ && gh2 ? kExplodeOutroMs : kOutroMs;
    if (!paused_ && mode_ != DevMode::Practice && (t > song_->endMs + outro || (songLost() && t > failMs_ + 3000))) endSong();
}

// One frame with GH2's menus: the scripted keys, the UI's update (which
// may start or end the song), the song's rules, then the UI's drawing (the
// track and HUD are its game panels).
void App::frameUi() {
    const double now = double(SDL_GetTicksNS()) / 1e6 - uiStartMs_;
    pollMusic();
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
                SDL_Log("GD05_KEYS: a \"j\" item needs GD05_VIRTUAL_PAD");
        } else if (k.padButton >= 0) {
            if (SDL_Joystick* j = virtualPad_ ? SDL_GetJoystickFromID(virtualPad_) : nullptr)
                SDL_SetJoystickVirtualButton(j, k.padButton, k.edge > 0);
            else
                SDL_Log("GD05_KEYS: a \"j\" item needs GD05_VIRTUAL_PAD");
        } else {
            if (k.edge >= 0) ui_->buttonDown(k.button, k.player);
            if (k.edge <= 0) ui_->buttonUp(k.button, k.player);
        }
    }
    if (walk_) {
        walk_->poll(*ui_, now);
        if (walk_->done()) running_ = false;
    }
    // Enhanced Options opened after a song (the backdrop went with the song's
    // venue): the screen has drawn once without it; it loads now and fades in.
    if (backdropDue_) {
        backdropDue_ = false;
        if (backdropShown_) loadBackdrop();
    }
    ui_->update();
    if (!running_) return;
    // The menus get the guitar's green / red as Confirm / Back when no song is being played.
    // (Each mode's game_screen: modes.dta's quick play / career, practice, the tutorials.)
    const std::string screen = ui_->currentScreen();
    input_.setInGame(player_ && begun_ && !paused_ && (screen == "game_screen" || screen == "practice_game_screen" || screen == "tut_script_screen"));
    input_.setOnePlayerSong(player_ && !player2_ && seats_.size() <= 1);  // D34: every device plays it
    if (player_ && begun_) {
        const float t = songClock();
        updateSong(t);
        lastT_ = t;
    } else if (backdrop_) {
        updateBackdrop();
    }
    canvas_.begin({0, 0, 0, 255});
    if (backdropWarm_) {
        // The preloaded backdrop's first draw (~70 ms: uploads, programs)
        // here, under black, not on the frame Enhanced Options opens.
        backdropWarm_ = false;
        if (backdrop_ && worldDraw_) {
            const uint64_t t0 = SDL_GetTicksNS();
            drawWorld();
            canvas_.fillRect(0, 0, 1e5f, 1e5f, {0, 0, 0, 255});
            if (const char* l = SDL_getenv("GD05_UILOG"); l && *l && *l != '0')
                SDL_Log("ui: menu backdrop warmed in %.1f ms", double(SDL_GetTicksNS() - t0) / 1e6);
        }
    }
    ui_->draw();
    drawTouchButtons();
    drawFpsCounter();
    drawBackendLabel();
    shootScreens();
    // GD05_SCREENSHOT_AT: wall-clock ms in the menus, "s<ms>" song times.
    if (captureMenuScreenshot(canvas_, now, player_ && begun_ ? double(lastT_) : -1e9)) running_ = false;
    canvas_.end();
}

// Development aid: GD05_SHOTS=<dir> (or the walk test's GD05_WALK_SHOTS)
// saves each screen the menus reach, once settled, as <dir>/NN_<screen>.bmp.
void App::shootScreens() {
    const char* dir = SDL_getenv("GD05_WALK_SHOTS");
    if (!dir) dir = SDL_getenv("GD05_SHOTS");
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
    gh2Mode_ = setup.mode;
    songBlock_ = setup.songBlock, skipIntro_ = setup.skipIntro;
    practiceSpeed_ = std::clamp(setup.practiceSpeed, 0.1f, 1.0f);
    practiceStartTick_ = setup.practiceStartTick, practiceEndTick_ = setup.practiceEndTick;
    bandOutfit_ = setup.outfit, bandGuitar_ = setup.guitar, bandSkin_ = setup.skin;
    encoreCheck_ = setup.encoreCheck, encoreFx_ = setup.encoreFx, campaignFinal_ = setup.campaignFinal;
    // Enhanced Options > Autoplay (as --autoplay).
    const bool autoplay = setup.autoplay || options_.autoplay || envOn("GD05_AUTOPLAY") || (walk_ && walk_->autoplay()) || enhancedValue("autoplay") == "on";
    // The mode and the players (docs/multiplayer.md).
    mode_ = setup.numPlayers > 1 ? modeOf(setup.mode) : setup.mode == "practice" ? DevMode::Practice : DevMode::Single;
    if (twoPlayers(mode_) != (setup.numPlayers > 1)) mode_ = DevMode::Single;
    difficulties_ = {std::clamp(setup.difficulty, 0, kDifficultyCount - 1), std::clamp(setup.difficulties[1], 0, kDifficultyCount - 1)};
    parts_ = setup.parts;
    pads_ = setup.pads;
    rbBandSetup_ = setup.rbBand;
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
    for (int s = 0; s < 2; ++s) hadDevice_[s] = input_.padConnected(s);
    overNs_ = 0;
    // GamePanel's start (0x822CCA60): an encore begins with every crowd full.
    if (encoreFx_)
        for (int p = 0; p < 2; ++p)
            if (Player* pl = p == 0 ? player_.get() : player2_.get()) pl->setCrowdRating(1.0f);
    beginSong();
}

const scene::CamShot* App::forcedShot(const std::string& spec) const {
    if (!venue_) return nullptr;
    if (spec.empty() || spec[0] != '#') return venue_->find<scene::CamShot>(spec);
    std::vector<scene::BandCamShot*> shots = venue_->all<scene::BandCamShot>();
    // #crowdN: the N-th of those with a crowd (the rb_crowd walk).
    const bool crowd = spec.rfind("#crowd", 0) == 0;
    if (crowd) shots.erase(std::remove_if(shots.begin(), shots.end(), [](const scene::BandCamShot* s) { return s->crowd.empty(); }), shots.end());
    if (shots.empty()) return nullptr;
    std::sort(shots.begin(), shots.end(), [](const scene::BandCamShot* a, const scene::BandCamShot* b) { return a->name < b->name; });
    return shots[size_t(std::max(0, std::atoi(spec.c_str() + (crowd ? 6 : 1)))) % shots.size()];
}

// frame_spike:<tag>:<frames>: for each mark `tag`, the longest frame of
// the `frames` after it against the median frame of the run's frames from
// the first such mark on (x10, an integer). With several marks the median
// of their windows' ratios (one slow frame from the machine's load does not
// decide); "<tag>!" takes the largest instead.
int App::frameSpikeRatio(const std::string& spec) const {
    const size_t c = spec.find(':');
    std::string tag = spec.substr(0, c);
    const bool worst = !tag.empty() && tag.back() == '!';
    if (worst) tag.pop_back();
    const size_t window = c == std::string::npos ? 60 : size_t(std::max(1, std::atoi(spec.c_str() + c + 1)));
    size_t first = frameWorkMs_.size();
    for (const auto& m : frameMarks_)
        if (m.second == tag) first = std::min(first, m.first);
    if (first >= frameWorkMs_.size()) return -1;
    std::vector<float> all(frameWorkMs_.begin() + std::ptrdiff_t(first), frameWorkMs_.end());
    std::nth_element(all.begin(), all.begin() + std::ptrdiff_t(all.size() / 2), all.end());
    const float median = std::max(0.05f, all[all.size() / 2]);
    std::vector<float> ratios;
    for (const auto& m : frameMarks_) {
        if (m.second != tag) continue;
        float mx = 0;
        for (size_t f = m.first; f < std::min(frameWorkMs_.size(), m.first + window); ++f) mx = std::max(mx, frameWorkMs_[f]);
        ratios.push_back(mx / median);
    }
    std::sort(ratios.begin(), ratios.end());
    const float r = worst ? ratios.back() : ratios[ratios.size() / 2];
    SDL_Log("frames: %s: %zu marks, median frame %.2f ms, window max / median: median %.1f, largest %.1f", tag.c_str(), ratios.size(),
            double(median), double(ratios[ratios.size() / 2]), double(ratios.back()));
    return int(std::lround(r * 10));
}

// GD05-073: the world's textures and meshes (the venue and its proxies,
// the band, the crowd) and the track's and the HUD's go to the GPU while
// the song loads, not on the frame that first draws each: that frame
// stalled (a stutter as the song starts, and at a camera cut showing parts
// of the venue not seen yet). The 360 has every texture in memory once the
// milo is loaded [I]. GD05_WARM_LEGACY=1: as before (each on its first draw).
void App::warmWorld() {
    if (SDL_getenv("GD05_WARM_LEGACY")) return;
    const uint64_t t0 = SDL_GetTicksNS();
    const int before = gpuUploads();
    auto warm = [](gfx::SceneCache& c) {
        return [&c](scene::Object& o) {
            if (const auto* t = o.as<scene::Tex>()) c.texture(t);
            else if (const auto* m = o.as<scene::Mesh>()) c.mesh(m);
        };
    };
    if (venueCache_) {
        auto w = warm(*venueCache_);
        if (venue_) venue_->forEach(w);
        if (band_)
            for (const auto& m : band_->members()) {
                if (m->character) m->character->forEachObject(w);
                for (const auto& [name, fx] : m->fx)
                    if (fx && fx->milo) fx->milo->root().forEach(w);
            }
        if (world_)
            for (const Crowd::State& st : world_->crowd().crowds())
                for (const auto& a : st.archetypes)
                    if (a.actor && a.actor->character) a.actor->character->forEachObject(w);
    }
    if (sceneCache_ && milos_) {
        auto w = warm(*sceneCache_);
        milos_->forEachMilo([&](scene::Milo& m) { m.root().forEach(w); });
    }
    if (const int n = gpuUploads() - before; n > 0 && envOn("GD05_UILOG"))
        SDL_Log("world warmed: %d textures and meshes to the GPU in %.1f ms", n, double(SDL_GetTicksNS() - t0) / 1e6);
}

int App::gameCounter(int player, const std::string& key) const {
    if (key == "missing_shaders") return gfx::shaderaudit::count();
    // Textures and meshes sent to the GPU since the song began (warmWorld: none).
    if (key == "song_gpu_uploads") return uploadsAtBegin_ < 0 ? -1 : gpuUploads() - uploadsAtBegin_;
    if (key == "music_plays") return musicPlays_;
    // GD05-073's Overdrive walks.
    if (key == "vocal_od_active") return vocal_ && vocal_->overdriveActive() ? 1 : 0;
    if (key == "vocal_od_meter_pct") return vocal_ ? int(std::lround(vocal_->overdriveMeter() * 100)) : -1;
    if (key == "vocal_freestyle") return vocal_ && vocal_->inFreestyle(lastT_) ? 1 : 0;
    if (key == "fills_deployed") return rbBonus_ ? rbBonus_->fillsDeployed() : 0;
    if (key == "rb_crowd_fx_triggers") return rbHud_ ? rbHud_->crowdMeterTriggers() : 0;
    if (key.rfind("frame_marks:", 0) == 0) {
        int n = 0;
        for (const auto& m : frameMarks_) n += m.second == key.substr(12);
        return n;
    }
    if (key.rfind("frame_spike:", 0) == 0) return frameSpikeRatio(key.substr(12));  // draws without their shader (render/device.h)
    // The world's pacing (render/post_fx.h hold): worlds drawn, frames shown again, the PostProc's emulate_fps.
    if (key == "world_draws") return const_cast<Canvas&>(canvas_).renderer().post().worldDraws();
    if (key == "world_holds") return const_cast<Canvas&>(canvas_).renderer().post().worldHolds();
    if (key == "world_emulate_fps") return int(std::lround(const_cast<Canvas&>(canvas_).renderer().post().selected().emulateFps));
    if (key == "surface_overrides") return track_ ? track_->surfaceOverrides() : 0;
    if (key == "mixer_band_pct") return int(std::lround(bandVolume_ * 100));
    if (key == "mixer_guitar_pct") return int(std::lround(guitarVolume_ * 100));
    if (key == "band_fx_particles") return venueView_ ? venueView_->bandView().fxParticles() : 0;
    if (key.rfind("venue_loaded:", 0) == 0) {  // the walks: is that venue's world the one loaded
        for (VenueVariant var : {VenueVariant::Single, VenueVariant::Mp, VenueVariant::Coop})
            if (venue_ && venuePath_ == venues::worldMilo(key.substr(13), var)) return 1;
        return 0;
    }
    if (key == "spot_shadows") return venueView_ ? venueView_->spotlights().shadowsTotal() : 0;
    // Rock Band (wave 02): the vocals, the band on stage, the part played.
    if (key == "vocal_notes_drawn") return vocalView_.notesDrawn();
    if (key == "vocal_lyrics_drawn") return vocalView_.lyricsDrawn();
    if (key == "vocal_score") return vocal_ ? vocal_->score() : 0;
    if (key == "vocal_phrases_rated") return vocal_ ? vocal_->phrasesRated(VocalPlayer::Rating::Awful) : 0;
    if (key == "band_members") return band_ ? int(band_->members().size()) : 0;
    if (key.rfind("band_piece:", 0) == 0) {  // GD05-072: the members built with that outfit piece (char/.../<piece>.milo_xbox)
        int n = 0;
        if (band_)
            for (const auto& m : band_->members())
                for (const std::string& p : m->pieces) n += p.find("/" + key.substr(11) + ".milo") != std::string::npos;
        return n;
    }
    if (key.rfind("band_flags:", 0) == 0) {  // GD05-072: the members whose play flags hold all of these (the attitude kGenre*)
        const uint32_t want = uint32_t(std::stoul(key.substr(11)));
        int n = 0;
        if (band_)
            for (const auto& m : band_->members()) n += (m->rbFlags & want) == want;
        return n;
    }
    if (key == "crowd_drawn") return venueView_ ? venueView_->crowdView().charactersDrawn() : 0;
    if (key == "band_meshes") return venueView_ ? venueView_->bandView().meshesDrawn() : 0;
    if (key == "score") return player_ ? player_->score() : 0;
    if (key == "part_vocals") return parts_[0] == "vocals" ? 1 : 0;
    if (key == "rb_track_frames") return rbTrackFrames_;
    // GD05-070 / GD05-071 (rbProbes).
    if (key == "meter_frames") return rbProbe_.meterFrames;
    if (key == "meter_jumps") return rbProbe_.meterJumps;
    if (key == "meter_flickers") return rbProbe_.meterFlickers;
    if (key == "rb_star_tails") return rbProbe_.starTails;
    if (key == "rb_star_tails_wrong") return rbProbe_.starTailsWrong;
    if (key == "star_tail_px") return rbProbe_.tailPx;
    if (key == "star_tail_chroma_pct") return rbProbe_.tailChromaPct;
    if (key == "crowd_px") return rbProbe_.crowdPx;
    if (key == "crowd_px_max") return rbProbe_.crowdPxMax;
    if (key == "crowd_probes") return rbProbe_.crowdProbes;
    if (key == "crowd_clips") {
        int n = 0;
        if (world_)
            for (const Crowd::State& st : world_->crowd().crowds())
                for (const auto& a : st.archetypes) n += a.actor ? a.actor->clipsStarted : 0;
        return n;
    }
    if (key == "rb_track_order_errors") return rbProbe_.trackOrderErrors;
    if (key == "rb_track_order_parts") return rbProbe_.trackOrderParts;
    if (key == "rb_hud_order_inversions") return rbProbe_.hudOrderInversions;
    if (key == "rb_hud_order_dirs") return rbProbe_.hudOrderDirs;
    if (key == "rb_hud_probes") return rbHudProbes_;
    if (key == "rb_nowbar_gem_px") return rbNowBarGemPx_;
    if (key == "rb_nowbar_cover_pct") return rbNowBarCoverPct_;
    if (key == "rb_hud_boxes") return rbHudBoxes_;
    if (key == "rb_hud_box_err_px") return rbHudBoxErrPx_;
    if (key == "rb_hud_value_errors") return rbHudValueErrors_;
    // GD05-045: the fret board's effects (render/rb_track_view.h FxStats).
    if (key == "rb_fx_hits") return RbTrackView::fxStats().hits;
    if (key == "rb_fx_hits_no_flame") return RbTrackView::fxStats().hitsNoFx;
    if (key == "rb_fx_deploys") return RbTrackView::fxStats().deploys;
    if (key == "rb_fx_deploys_no_visual") return RbTrackView::fxStats().deploysNoFx;
    if (key == "rb_fx_glows") return RbTrackView::fxStats().glows;
    if (key == "rb_fx_burns") return RbTrackView::fxStats().burns;
    if (key == "song_loads") return songLoads_;
    if (key == "menu_movie_frames") return menuMovie_.framesShown();
    if (key == "menu_venue_sounds") return menuVenueSounds_;
    if (key == "ms_per_beat") return song_ && song_->beats.size() > 1 ? int((song_->beats.back().ms - song_->beats.front().ms) / float(song_->beats.size() - 1)) : 500;
    if (key == "band_players") return 1 + int(seats_.size());
    if (key == "unison_bonuses") return unisonBonuses_;
    if (key == "band_saves") return saves_;
    if (key == "band_score") {
        int sc = player_ ? player_->score() : 0;
        for (const RbSeat& st : seats_) sc += st.player ? st.player->score() : 0;
        return sc;
    }
    if (key == "solos_done") return rbBonus_ ? rbBonus_->solosDone() : 0;
    if (key == "solo_bonus") return rbBonus_ ? rbBonus_->bonusTotal() : 0;
    if (key == "fills_shown") return rbBonus_ ? rbBonus_->fillsShown() : 0;
    if (key == "fills_deployed") return rbBonus_ ? rbBonus_->fillsDeployed() : 0;
    if (key == "coda_pot") return rbBonus_ ? rbBonus_->codaPot() : 0;
    if (key == "stars") return player_ ? rbStars(player_.get()) : 0;
    if (key == "part_drum") return parts_[0] == "drum" ? 1 : 0;
    const int p = std::clamp(player, 0, 1);
    if (key == "sp_phrases") return spPhrases_[p];
    if (key == "sp_started") return spStarted_[p];
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
        if (key == "held_tail_width") return int(std::lround(tv->heldTailWidest() * 1000));
        if (key == "held_glow_scale") return int(std::lround(tv->heldGlowScale() * 1000));
        if (key == "tail_now_ms") return int(tv->tailNowMs());
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
// GD05_POST=0; the track and HUD then go on top, as GH2's final pass leaves
// them out. No venue yet: nothing is drawn and the frame is as before.
void App::drawWorld() {
    if (rbHudProbeDue_) rbHudProbe();  // under the frame, which the world then covers
    rbProbes();
    if (!worldDraw_) return;
    canvas_.flush();
    gfx::Renderer& r = canvas_.renderer();
    const float clear[4] = {0, 0, 0, 1};
    if (r.post().hold()) return;  // the world's frame rate (the PostProc's emulate_fps): the last picture again
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
        world_->onSound = [this](const std::string& name) {
            if (songLoads_ == 0) ++menuVenueSounds_;  // before the first song: only the backdrop's world
            recordSfx(name);
        };
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
    applyEnhancedLook();  // the director's shake, the venue extras
    if (const scene::LightPreset* p = world_->firstPreset()) world_->applyPreset(*p, 0);
    if (song_ && info_) startBand(*info_, *song_);
    // The static shot's PostProc (its `postprocess` flag) [I: the director
    // selects it on every shot from phase 2 on].
    venuePostOn_ = false;
    if (const scene::CamShot* shot = venueView_->shot())
        if (auto* pp = venue_->find<scene::PostProc>(shot->flag("postprocess"))) {
            venuePost_ = gfx::postProcState(*pp, [&](const std::string& t) { return venueCache_->texture(pp, t); });
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
                auto ppState = [this](const scene::PostProc& p) {
                    return gfx::postProcState(p, [&](const std::string& t) { return venueCache_->texture(&p, t); });
                };
                if (pp) {
                    gfx::PostProcState st = ppState(*pp);
                    // pp_blend_task (band_objects.dta start_shot): from the last selection.
                    if (const scene::PostProc* from = director_->postProcFrom()) st = gfx::blendPostProc(ppState(*from), st, director_->postProcBlend());
                    r.post().select(st);
                } else {
                    r.post().reset();
                }
                const CameraDirector::Dof& d = director_->dof();
                const gfx::Camera& c = director_->camera();
                if (d.on && d.maxBlur > 0) r.post().setDof(gfx::DofState::fromShot(d.focus, d.blurDepth, d.maxBlur, c.nearPlane, c.farPlane));
                else r.post().clearDof();
            } else if (venuePostOn_) {
                r.post().select(venuePost_);
            }
            r.post().size(w, h);
        }
        // The band's feet and the clock for the Enhanced Options' venue extras.
        if (enhancedLook_.any()) {
            std::vector<gfx::Vec3> feet;
            if (band_)
                for (const auto& m : band_->members())
                    if (scene::Xfm x; band_->objectXfm(m->slot, "", x)) feet.push_back({x.pos[0], x.pos[1], x.pos[2]});
            venueView_->setBandFeet(std::move(feet));
            // Frozen with the backdrop (its dust stays where it is).
            if (!backdrop_ || !backdrop_->frozen) venueView_->setTimeSec(float(double(SDL_GetTicksNS()) / 1e9));
        }
        venueView_->draw(r, w, h, hdr);
        if (hdr) enhancedFrame(r);
    };
    rbShots_.clear();
    rbShotUsed_.clear();
    rbNextCamMs_ = -1e9f;
    rbWinShot_ = rbLoseShot_ = false;
    rbVenueNote_ = rbVenueEvent_ = 0;
    // RB's world enter (worldbase.dta) [C data]: the mode's shots only
    // (change_proxies keeps coop_ in a band), no PS3-only ones
    // (platform_only 4), and each NORMAL shot re-keyed NORMAL<b><d><g><v>
    // by its focus flags (a far one NORMAL1111); special ones never picked.
    bool anyCoop = false;
    for (const auto& p : venue_->parts())
        for (scene::Object* o : p.milo->root().byClass("BandCamShot")) anyCoop |= o->name.rfind("coop_", 0) == 0;
    for (const auto& p : venue_->parts())
        for (scene::Object* o : p.milo->root().byClass("BandCamShot")) {
            auto* c = o->as<scene::CamShot>();
            if (!c || c->keyframes.empty()) continue;  // a shot with no keyframe frames nothing
            if (anyCoop && c->name.rfind("coop_", 0) != 0) continue;
            if (c->platformOnly == 4 || c->name.find("_ps3_") != std::string::npos) continue;
            std::string cat = c->category;
            if (cat == "NORMAL") {
                if (c->flag("special") == "TRUE" || c->flag("special") == "1") continue;
                auto on = [&](const char* k) { const std::string f = c->flag(k); return f == "TRUE" || f == "1" ? '1' : '0'; };
                cat += c->flag("distance") == "far" ? std::string("1111") : std::string{on("bass_focus"), on("drums_focus"), on("guitar_focus"), on("vocal_focus")};
            }
            rbShots_[cat].push_back(c);
        }
    if (SDL_getenv("GD05_SHOTLOG")) {
        std::map<std::string, int> cats;
        for (const auto& p : venue_->parts())
            for (scene::Object* o : p.milo->root().byClass("BandCamShot"))
                if (auto* c = o->as<scene::CamShot>()) ++cats[c->category];
        std::string line;
        for (const auto& [c, n] : cats) line += " " + c + ":" + std::to_string(n);
        SDL_Log("venue shots:%s", line.c_str());
    }
    if (SDL_getenv("GD05_VENUELOG"))
        for (const auto& part : venue_->parts()) SDL_Log("venue part: %s", part.path.c_str());
    SDL_Log("venue %s %s loaded in %.0f ms: %d milos, %d proxies, shot %s", venue.c_str(), venues::variantName(variant),
            double(SDL_GetTicksNS() - start) / 1e6, int(venue_->parts().size()), int(venue_->proxies().size()),
            venueView_->shot() ? venueView_->shot()->name.c_str() : "none");
    return true;
}

void App::unloadVenue() {
    backdrop_.reset();  // it lives in the venue's world
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
void App::recordSfx(const std::string& name) {
    if (begun_ && player_ && songSfx_.size() < 8192) songSfx_.emplace_back(name, double(lastT_));
    if (sfxHistory_.size() >= 1024) sfxHistory_.erase(sfxHistory_.begin(), sfxHistory_.begin() + 512);
    sfxHistory_.push_back(name);
}

bool App::playSfxArgs(const std::string& name, float volumeDb, float pan, float transpose) {
    // Development aid: GD05_SFXLOG=1 logs every sound the scripts or the game play.
    static const bool sfxLog = envOn("GD05_SFXLOG");
    if (sfxLog) SDL_Log("sfx: %s (%.1f dB, pan %.2f)%s", name.c_str(), double(volumeDb), double(pan), begun_ && player_ ? (" at song " + std::to_string(int(lastT_)) + " ms").c_str() : "");
    // The game's banks, then the venue's (whose playSound records it itself).
    if (synth_ && synth_->play(name, volumeDb, pan, transpose)) return recordSfx(name), true;
    if (synth_ && world_ && world_->venue().find(name)) return world_->playSound(name, nullptr, volumeDb, pan, transpose);
    recordSfx(name);
    return false;
}

void App::stopSfx(const std::string& name) {
    if (!synth_) return;
    if (!synth_->stop(name) && world_) world_->stopSound(name);
}

// The band (game/band.h) once the song begins in a venue: the players'
// outfits, guitars and skins from their configs (the menus: quick play's
// song character, the career's chosen one, ui/char_select.h; without the
// menus the song's quickplay one). GD05_CHAR=<outfit>, GD05_GUITAR=<guitar>
// and GD05_SKIN=<skin> override player 0's; GD05_NO_BAND=1 leaves the
// stage empty.
void App::startBand(const SongInfo& info, const Song& song, bool backdrop) {
    band_.reset();
    if (!world_ || envOn("GD05_NO_BAND")) return;
    Band::Config cfg;
    cfg.players = backdrop ? 1 : twoPlayers(mode_) ? 2 : 1;
    // Co-op: a player on the bass plays the bass track (bass_strum) [C data: char_objects.dta].
    if (!backdrop && mode_ == DevMode::Coop)
        for (int p = 0; p < 2; ++p) cfg.bass[size_t(p)] = parts_[size_t(p)] == "bass";
    if (!backdrop) {
        cfg.character = bandOutfit_, cfg.guitar = bandGuitar_, cfg.skin = bandSkin_;
        cfg.encore = encoreCheck_, cfg.campaignFinal = campaignFinal_;
        if (const char* c = SDL_getenv("GD05_CHAR")) cfg.character[0] = c;
        // Rock Band: the players' own characters (the creator's) on stage in
        // their instrument's slot [I: RB's slot choice].
        if (ui_) {
            Interp& in = ui_->interp();
            for (int p = 0; p < 4; ++p) {
                const std::string part = p == 0 ? parts_[0] : (size_t(p - 1) < rbBandSetup_.size() ? rbBandSetup_[size_t(p - 1)].part : std::string());
                if (part.empty()) continue;
                const Value pc = in.send("game", "get_player_config", {Value::integer(p)});
                const Value ch = pc.object ? in.send(pc.object, "get_char", {}) : Value::integer(0);
                if (!ch.object || ch.object->className() != "TourCharLocal") continue;
                Band::Config::RbLook look;
                look.gender = in.send(ch.object, "get_gender", {}).str();
                look.skin = in.send(ch.object, "skin_color", {}).integerValue();
                look.attitude = uint32_t(in.send(ch.object, "get_attitude", {}).integerValue());
                const Value ol = in.send(ch.object, "outfit_list", {});
                if (ol.array)
                    for (const Value& e : *ol.array)
                        if (e.array && e.array->size() >= 2) look.outfit[(*e.array)[0].str()] = (*e.array)[1].str();
                if (!look.outfit.empty()) cfg.rbLooks[part == "drums" ? "drum" : part] = look;
            }
        }
        if (const char* g = SDL_getenv("GD05_GUITAR")) cfg.guitar[0] = g, cfg.skin[0].clear();
        if (const char* k = SDL_getenv("GD05_SKIN")) cfg.skin[0] = k;
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
            if (mode_ == DevMode::FaceOff || mode_ == DevMode::ProFaceOff) {
                band_->lead = [this](int p) {
                    const Player* me = p == 0 ? player_.get() : player2_.get();
                    const Player* other = p == 0 ? player2_.get() : player_.get();
                    return me && other ? other->score() - me->score() : 0;
                };
                // 0x822C6DC0, 0x822BB358 [C]: the higher average multiplier x the normalised base score / 30.
                band_->leadLimit = [this] {
                    if (!player_ || !player2_) return 1e30f;
                    return std::max(player_->averageMultiplier(), player2_->averageMultiplier()) * float(player_->baseScore()) * player_->normalisation() * (1.0f / 30.0f);
                };
            }
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

// A Rock Band shot of `category`, at random among the venue's, not the
// one playing when there is another [I: RB's BandDirector picks by the
// band members in view and the shot's flags, not read yet].
const scene::CamShot* App::rbShot(const std::string& category, const std::function<bool(const scene::CamShot&)>& ok) {
    auto it = rbShots_.find(category);
    if (it == rbShots_.end() || it->second.empty()) return nullptr;
    // RB's picker (0x8229F0D0) [C]: the unused shots that pass the filter
    // first (all of them reset when none is left), weighted by
    // selection_weight; the current shot is not restarted.
    std::vector<const scene::CamShot*> pool;
    for (int pass = 0; pass < 2 && pool.empty(); ++pass) {
        if (pass == 1)
            for (const scene::CamShot* c : it->second) rbShotUsed_.erase(c);
        for (const scene::CamShot* c : it->second)
            if ((!ok || ok(*c)) && !rbShotUsed_.count(c)) pool.push_back(c);
    }
    if (pool.empty()) return nullptr;
    float total = 0;
    for (const scene::CamShot* c : pool) total += std::max(0.0f, c->selectionWeight);
    rbShotSeed_ = rbShotSeed_ * 1664525u + 1013904223u;
    float r = float((rbShotSeed_ >> 8) & 0xFFFF) / 65536.0f * total;
    const scene::CamShot* pick = pool.back();
    for (const scene::CamShot* c : pool)
        if ((r -= std::max(0.0f, c->selectionWeight)) < 0) {
            pick = c;
            break;
        }
    rbShotUsed_.insert(pick);
    return pick;
}

// The VENUE track's camera (worldbase.dta pick_new_shot, midi_parsers.dta's
// world_camera_parser / world_focus_parser) [C data]: note 60 picks a
// NORMAL<bass drums guitar vocal> shot from the held focus notes 61..64 and
// the held 70..73 (no_behind, force_far, force_closeup, no_closeup), facing
// alternating and no far after far; [do_directed_cut <cat>] forces that
// category's shot and holds note-60 picks for its min_time + zero_time;
// LOSE when the band fails, WIN after the song. [I: the 1/8-beat lead, the
// intro chains, the shots' band targets.]
void App::rbShots(float t) {
    if (rbShots_.empty() || !director_ || !song_) return;
    const scene::CamShot* next = nullptr;
    auto held = [&](int pitch) {
        for (const VenueNote& n : song_->venueNotes)
            if (n.track == VenueNote::Track::Venue && n.pitch == pitch && n.ms <= t + 1 && (n.endMs < 0 ? n.ms + 1 >= t : n.endMs > t)) return true;
        return false;
    };
    auto pickNormal = [&] {
        const scene::CamShot* cur = director_->shot();
        const std::string facing = cur ? cur->flag("facing") : std::string();
        const std::string dist = cur ? cur->flag("distance") : std::string();
        bool focus[4] = {held(61), held(62), held(63), held(64)};  // bass, drums, guitar, vocal
        if (focus[1] && focus[0] + focus[2] + focus[3] >= 2) focus[1] = false;
        const bool any = focus[0] || focus[1] || focus[2] || focus[3];
        const bool forceFar = held(71), forceClose = held(72), noClose = held(73) || (forceClose && focus[0] + focus[1] + focus[2] + focus[3] > 1);
        const bool noBehind = held(70) || dist == "behind";
        bool farOk = dist != "far" && !any;
        std::string cat;
        if (forceFar) cat = "NORMAL1111";
        else if (any) cat = std::string("NORMAL") + (focus[0] ? '1' : '0') + (focus[1] ? '1' : '0') + (focus[2] ? '1' : '0') + (focus[3] ? '1' : '0');
        else {
            static const char* kCats[] = {"0001", "0010", "0100", "1000", "1111", "1100", "0110", "0011"};
            rbShotSeed_ = rbShotSeed_ * 1664525u + 1013904223u;
            cat = std::string("NORMAL") + kCats[(rbShotSeed_ >> 8) % 8];
        }
        auto filter = [&](const scene::CamShot& c) {
            const std::string f = c.flag("facing"), d = c.flag("distance");
            if (c.flag("one_time") == "TRUE" && rbShotUsed_.count(&c)) return false;
            if (dist != "behind" && !facing.empty() && facing != "null" && f == facing) return false;  // alternate left / right
            if (forceFar) return d == "far";
            if (forceClose && !noClose) return d == "closeup";
            if (d == "far" && !farOk) return false;
            if (d == "behind" && noBehind) return false;
            if (d == "closeup" && noClose) return false;
            return true;
        };
        const scene::CamShot* s = rbShot(cat, filter);
        if (!s) s = rbShot(cat);
        if (!s) s = rbShot("NORMAL1111", filter);
        return s;
    };
    while (rbVenueNote_ < song_->venueNotes.size() && song_->venueNotes[rbVenueNote_].ms <= t) {
        const VenueNote& n = song_->venueNotes[rbVenueNote_++];
        if (n.track == VenueNote::Track::Venue && n.pitch == 60 && t >= rbNextCamMs_ && !rbLoseShot_)
            if (const scene::CamShot* s = pickNormal()) next = s;
    }
    while (rbVenueEvent_ < song_->venueEvents.size() && song_->venueEvents[rbVenueEvent_].ms <= t) {
        const std::string& e = song_->venueEvents[rbVenueEvent_++].text;
        const std::string key = "[do_directed_cut ";
        if (e.compare(0, key.size(), key) == 0 && !rbLoseShot_) {
            std::string cat = e.substr(key.size());
            while (!cat.empty() && (cat.back() == ']' || cat.back() == ' ')) cat.pop_back();
            if (const scene::CamShot* s = rbShot(cat)) {
                next = s;
                const float minT = std::atof(s->flag("min_time").c_str()), zero = std::atof(s->flag("zero_time").c_str());
                rbNextCamMs_ = t + (minT + zero) / 30.0f * 1000.0f;
            }
        }
    }
    if (songLost() && !rbLoseShot_) {
        rbLoseShot_ = true;  // game_lost: LOSE at the moment the band fails [C data]
        if (const scene::CamShot* s = rbShot("LOSE")) next = s;
    }
    if (t >= song_->endMs && !rbWinShot_ && !rbLoseShot_) {
        rbWinShot_ = true;
        if (const scene::CamShot* s = rbShot("WIN")) next = s;
    }
    if (t < 0) rbWinShot_ = rbLoseShot_ = false, rbNextCamMs_ = -1e9f;
    if (next && next != director_->shot()) {
        static const bool log = SDL_getenv("GD05_SHOTLOG") != nullptr;
        if (log) SDL_Log("shot %.0f: %s (%s, %s %s)", double(t), next->name.c_str(), next->category.c_str(), next->flag("distance").c_str(), next->flag("facing").c_str());
        if (const auto* bs = dynamic_cast<const scene::BandCamShot*>(next)) {
            std::vector<std::pair<std::string, std::string>> groups;
            std::vector<std::pair<std::string, scene::Xfm>> moves;
            static const bool noTeleport = SDL_getenv("GD05_NO_TELEPORT") != nullptr;
            for (const auto& tg : bs->bandTargets) {
                if (log) SDL_Log("  target %s anim %s teleport %d", tg.target.c_str(), tg.anim.c_str(), int(tg.teleport));
                if (!tg.anim.empty()) groups.emplace_back(tg.target, tg.anim);
                if (tg.teleport && !noTeleport) moves.emplace_back(tg.target, tg.to);
            }
            if (band_) band_->rbShotTargets(groups, moves);
        }
        director_->forceShot(next);
    }
}

// world_panel's milo: ../world/<v>/<v>[_mp|_coop].milo (GD05_VENUE replaces the venue).
void App::worldLoad(const std::string& path) {
    if (backdrop_) unloadVenue();  // a song: a fresh world, even in the backdrop's venue
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
        // Audio / Video Calibration: the highway drawn ahead by the video delay.
        const float drawT = lastT_ + float(videoOffsetMs_);
        track_->draw(canvas_.renderer(), *sceneCache_, *song_, *player_, drawT, speed);
        if (track2_ && player2_) track2_->draw(canvas_.renderer(), *sceneCache_, *song_, *player2_, drawT, speed);
        updateTouchLanes();
    } else if ((vocal_ || rbTrack_) && sceneCache_ && !debugHighway_) {
        drawRbTracks(lastT_, speed);
        if (seats_.empty() && !rbHud_) highway_.drawHud(canvas_, *player_, info_ ? info_->name : "");
    } else {
        highway_.draw(canvas_, *song_, *player_, lastT_, speed, info_ ? info_->name : "", flashes_);
    }
}

// Rock Band's tracks (wave 02: the same through the menus and --song):
// the vocal track, or the instrument's (trackpanel's TrackDir guitar /
// bass / drum).
void App::drawRbTracks(float t, float speed) {
    if (const char* tr = SDL_getenv("GD05_TRACK"); tr && *tr == '0') return;  // development aid: the venue alone
    if (!seats_.empty()) {
        // The instruments' tracks side by side under the vocals [I: RB's
        // layout per player count, track_panel.dta's positions, not read].
        std::vector<RbTrackView*> tracks;
        for (RbSeat& st : seats_)  // loaded here: the milo library comes with the song's track (prepareTrack)
            if (!st.vocal && !st.track && milos_) try {
                    auto tv = std::make_unique<RbTrackView>();
                    tv->load(*data_, *milos_, st.part);
                    st.track = std::move(tv);
                } catch (const std::exception& e) {
                    SDL_Log("band: %s track: %s", st.part.c_str(), e.what());
                    st.part += "!";  // not again
                }
        if (rbTrack_ && !vocal_) tracks.push_back(rbTrack_.get());
        for (RbSeat& st : seats_)
            if (st.track) tracks.push_back(st.track.get());
        const float n = float(tracks.size());
        // RB's HUD places the tracks under their meters (RbHudView::trackRects).
        const auto* rects = rbHud_ ? &rbHud_->trackRects() : nullptr;
        if (tracks.size() > 1)
            for (size_t i = 0; i < tracks.size(); ++i)
                tracks[i]->setScreenRect(rects && rects->size() == tracks.size() ? (*rects)[i] : std::array<float, 4>{float(i) / n, 0.6f, 1.0f / n, 0.4f});
    }
    // The panel's configuration for every gem track (track_paneldir.dta
    // update / configure_tracks): the camera and y_per_second by the gem
    // players' mean difficulty [C data].
    if (rbHud_ && rbHud_->loaded()) {
        float sum = 0;
        int n = 0;
        if (rbTrack_ && !vocal_) sum += float(difficulty_), ++n;
        for (const RbSeat& st : seats_)
            if (st.track) sum += float(st.difficulty), ++n;
        const float mean = n ? sum / float(n) : 3.0f;
        if (mean != rbTrackDiff_) {
            rbTrackDiff_ = mean;
            if (rbTrack_) rbTrack_->configure(rbHud_->trackConfig(), mean);
            for (RbSeat& st : seats_)
                if (st.track) st.track->configure(rbHud_->trackConfig(), mean);
        }
        auto ensure = [&](RbTrackView* tv) {
            if (tv && tv->loaded() && !tv->configured()) tv->configure(rbHud_->trackConfig(), mean);
        };
        ensure(rbTrack_.get());
        for (RbSeat& st : seats_) ensure(st.track.get());
    }
    drawRbTrack0(t, speed);
    if (!seats_.empty()) drawSeats(t, speed);
    if (rbHud_) drawRbHud(t);
    else if (!seats_.empty()) drawBandHud(t);
}

// What RB's HUD shows (render/rb_hud_view.h): each gem track's player, the
// band's score, stars and crowd [C: the elements' inputs; I: the band's
// rating as the mean of the players' when several play].
RbHudView::State App::rbHudState(float t) const {
    RbHudView::State s;
    auto maxMult = [&](const std::string& part) {
        int m = 4;
        if (auto it = rules_.rb.streakMultipliers.find(part); it != rules_.rb.streakMultipliers.end())
            for (const auto& [count, mult] : it->second) m = std::max(m, mult);
        return m;
    };
    auto track = [&](const std::string& part, const Player* p, float rating, bool out, const RbBonus* bonus) {
        RbHudView::Track tr;
        tr.part = part;
        tr.introAtMs = rbIntroAt(int(s.tracks.size()));
        tr.name = "Player " + std::to_string(s.tracks.size() + 1);  // [I: RB shows the gamertag]
        tr.streak = p->streak();
        tr.streakMultiplier = p->baseMultiplier();
        tr.totalMultiplier = p->multiplier();
        tr.maxMultiplier = maxMult(part);
        tr.energy = p->starMeter();
        tr.deploying = p->starPowerActive();
        tr.rating = rating;
        tr.failed = out;
        if (bonus) {
            const float share = bonus->soloShare(t);
            tr.soloActive = share >= 0, tr.soloPercent = share >= 0 ? int(share * 100) : 0;
        }
        s.tracks.push_back(tr);
    };
    if (!vocal_) track(parts_[0], player_.get(), player_->rockMeter(), player0Out_, rbBonus_.get());
    for (const RbSeat& st : seats_)
        if (!st.vocal && st.player) track(st.part, st.player.get(), st.rating, st.out, st.bonus.get());
    s.vocals = vocal_ != nullptr;
    for (const RbSeat& st : seats_) s.vocals = s.vocals || st.vocal != nullptr;
    s.participants = 1 + int(seats_.size());
    s.bandScore = seats_.empty() ? player_->score() : gameCounter(0, "band_score");
    s.stars = float(rbStars(player_.get()));
    float sum = 0;
    int n = 0;
    if (!vocal_) sum += player_->rockMeter(), ++n;
    for (const RbSeat& st : seats_) sum += st.rating, ++n;
    if (vocal_) sum += vocalRating_, ++n;
    s.crowd = n ? std::clamp(sum / float(n), 0.0f, 1.0f) : 0.5f;
    // The excitement 0..4 by fifths of the meter (kExcitementPeak 4) [I].
    s.excitement = std::clamp(int(s.crowd * 5.0f), 0, 4);
    s.bandMultiplier = 1;
    for (const RbHudView::Track& tr : s.tracks)
        if (tr.deploying) s.bandMultiplier = std::max(s.bandMultiplier, tr.totalMultiplier / std::max(1, tr.streakMultiplier));
    s.lost = songLost();
    s.coda = rbBonus_ && rbBonus_->inCoda(t) && !rbBonus_->codaBanked();
    s.codaScore = rbBonus_ ? rbBonus_->codaPot() : 0;
    s.tempo = song_ ? &song_->tempo : nullptr;  // the triggers' beat rates (render/anim_rate.h)
    return s;
}

void App::drawRbHud(float t) {
    if (const char* h = SDL_getenv("GD05_HUD"); h && *h == '0') return;  // development aid: the track alone
    canvas_.flush();
    if (rbHud_->introAt() != rbExtendMs_) rbHud_->reset(rbExtendMs_);
    const RbHudView::State st = rbHudState(t);
    rbHud_->draw(canvas_.renderer(), *sceneCache_, st, t);
    // The walks (or GD05_HUD_PROBE=1): a probe at 15 s and every 20 s after,
    // on the next frame, under its picture.
    if ((walk_ || envOn("GD05_HUD_PROBE")) && t >= rbHudNextProbeMs_ && rbHudProbes_ < 6 && !paused_) {
        rbHudProbeDue_ = true;
        rbHudNextProbeMs_ = t + 20000;
        // The values now: what the elements show against the players' state.
        int errors = 0;
        auto fmt = [](int n) {
            std::string d = std::to_string(std::max(0, n)), out;
            for (size_t i = 0; i < d.size(); ++i) out += (i > 0 && (d.size() - i) % 3 == 0 ? "," : ""), out += d[i];
            return out;
        };
        const RbHudView::Shown sh = rbHud_->shown(0);
        const int score = seats_.empty() ? player_->score() : gameCounter(0, "band_score");
        if (sh.score != fmt(score)) ++errors;
        std::string mult;
        if (!st.tracks.empty()) {
            const RbHudView::Track& t0 = st.tracks[0];
            const Player* p0 = vocal_ ? seats_.front().player.get() : player_.get();
            if (t0.totalMultiplier > 1) mult = std::to_string(p0 ? p0->multiplier() : 0) + "x";
            if (sh.multiplier != mult) ++errors;
            if (std::fabs(sh.energyFrame - (p0 ? p0->starMeter() : -1)) > 0.001f) ++errors;
        }
        if (std::fabs(sh.crowdFrame - st.crowd) > 0.001f) ++errors;
        SDL_Log("rb hud values at %.0f ms: score '%s' (player %d), multiplier '%s' (player '%s'), Overdrive frame %.3f (player %.3f), "
                "crowd frame %.3f (band %.3f), wipe %.2f: %d errors",
                double(t), sh.score.c_str(), score, sh.multiplier.c_str(), mult.c_str(), double(sh.energyFrame),
                double(st.tracks.empty() ? -1.0f : st.tracks[0].energy), double(sh.crowdFrame), double(st.crowd), double(sh.wipeFrame), errors);
        rbHudValueErrors_ += errors;
    }
}

void App::rbProbes() {
    static const bool meterOn = envOn("GD05_METER_PROBE"), tailOn = envOn("GD05_TAIL_PROBE"), crowdOn = envOn("GD05_CROWD_PROBE");
    if (!meterOn && !tailOn && !crowdOn) return;
    if (!sceneCache_ || paused_) return;
    canvas_.flush();
    gfx::Renderer& r = canvas_.renderer();
    const float key[4] = {1, 0, 1, 1};
    // The picture's pixels other than the magenta it was cleared to.
    struct Px {
        long n = 0;
        double luma = 0, chroma = 0;
    };
    auto measure = [&](bool overBlack = false) {
        Px p;
        SDL_Surface* raw = canvas_.readPixels();
        SDL_Surface* c = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : nullptr;
        if (raw) SDL_DestroySurface(raw);
        if (!c) return p;
        for (int y = 0; y < c->h; ++y) {
            const uint8_t* row = static_cast<const uint8_t*>(c->pixels) + size_t(y) * size_t(c->pitch);
            for (int x = 0; x < c->w; ++x) {
                const uint8_t* q = row + size_t(x) * 4;
                if (overBlack ? std::max({q[0], q[1], q[2]}) <= 24 : 255 - q[0] + q[1] + 255 - q[2] <= 24) continue;
                const int mx = std::max({q[0], q[1], q[2]}), mn = std::min({q[0], q[1], q[2]});
                ++p.n, p.luma += 0.2126 * q[0] + 0.7152 * q[1] + 0.0722 * q[2], p.chroma += mx > 0 ? double(mx - mn) / mx : 0.0;
            }
        }
        SDL_DestroySurface(c);
        if (p.n) p.luma /= double(p.n), p.chroma /= double(p.n);
        return p;
    };
    // The draw-order checks from the last frame (render/rb_track_view.h, rb_hud_view.h).
    if (rbTrack_ && !vocal_ && !rbTrack_->drawOrder().empty()) {
        const std::vector<std::string> want = rbTrack_->expectedDrawOrder(), &got = rbTrack_->drawOrder();
        int err = int(std::max(want.size(), got.size()) - std::min(want.size(), got.size()));
        for (size_t i = 0; i < std::min(want.size(), got.size()); ++i) err += want[i] != got[i];
        if (err && rbProbe_.trackOrderErrors == 0) {
            std::string w, g;
            for (const auto& n : want) w += " " + n;
            for (const auto& n : got) g += " " + n;
            SDL_Log("rb probe: track draw order\n  data:%s\n  drawn:%s", w.c_str(), g.c_str());
        }
        rbProbe_.trackOrderErrors = std::max(rbProbe_.trackOrderErrors, err);
        rbProbe_.trackOrderParts = std::max(rbProbe_.trackOrderParts, int(want.size()));
        rbProbe_.starTails += rbTrack_->starTails(), rbProbe_.starTailsWrong += rbTrack_->starTails() - rbTrack_->starTailsBonus();
    }
    if (rbHud_) {
        rbProbe_.hudOrderInversions = std::max(rbProbe_.hudOrderInversions, rbHud_->drawOrderInversions());
        rbProbe_.hudOrderDirs = std::max(rbProbe_.hudOrderDirs, rbHud_->drawOrderDirs());
    }
    // The Overdrive meter (star_power_01) alone, every frame: its luma; a
    // jump is a change of more than 6% of the brightest seen. be_ready's
    // be_color_pulse loops once a beat (k1_fpb): at most one jump a beat.
    if (meterOn && rbHud_ && begun_) {
        r.device().clear(key, true);
        rbHud_->drawOnly(r, *sceneCache_, "star_power_01");
        const Px p = measure();
        auto& m = rbProbe_.meter;
        if (p.n > 0) m.emplace_back(lastT_, float(p.luma));
        if (m.size() > 240) m.erase(m.begin());
        rbProbe_.meterFrames = int(m.size());
        float hi = 0;
        for (const auto& e : m) hi = std::max(hi, e.second);
        // A flicker: a jump straight back (up then down, or down then up, on
        // the next frame). The pulse's wrap is one jump, then a slope that
        // may fall up to 40% of the brightest a beat (be_color_pulse's keys:
        // 0.99 0.80 0.21 -> 0.69 0.53 0.01), so a step counts as a jump past
        // 6% plus that slope over the step's beats (slow frames under load).
        int jumps = 0, flickers = 0;
        auto jump = [&](size_t i) {
            const double beats = song_ ? gfx::beatAt(&song_->tempo, m[i].first) - gfx::beatAt(&song_->tempo, m[i - 1].first) : 0.0;
            return std::fabs(m[i].second - m[i - 1].second) > hi * float(0.06 + 0.4 * std::max(0.0, beats));
        };
        for (size_t i = 1; i < m.size(); ++i) {
            if (!jump(i)) continue;
            ++jumps;
            const float d = m[i].second - m[i - 1].second;
            if (i + 1 < m.size() && jump(i + 1)) flickers += (m[i + 1].second - m[i].second > 0) != (d > 0);
        }
        rbProbe_.meterJumps = jumps, rbProbe_.meterFlickers = flickers;
        rbProbe_.meterBeats = m.size() > 1 && song_ ? int(std::ceil(gfx::beatAt(&song_->tempo, m.back().first) - gfx::beatAt(&song_->tempo, m.front().first))) : 0;
    }
    // The star tails alone (the last frame's) over black (they add), their
    // chroma: tail_bonus.mat's track_flash_style.tex is near white.
    if (tailOn && rbTrack_ && !vocal_ && rbTrack_->starTails() > 0) {
        const float black[4] = {0, 0, 0, 1};
        r.device().clear(black, true);
        rbTrack_->drawLastTails(r, *sceneCache_, true);
        const Px p = measure(true);
        if (p.n > 50) rbProbe_.tailPx = int(p.n), rbProbe_.tailChromaPct = int(std::lround(p.chroma * 100));
    }
    // The audience alone through the shot's camera, every 30th frame.
    if (crowdOn && venueView_ && ++rbProbe_.crowdFrames % 30 == 0) {
        r.device().clear(key, true);
        int x = 0, y = 0, w = 0, h = 0;
        r.pictureRect(x, y, w, h);
        venueView_->setOnlyCrowd(true);
        venueView_->draw(r, w, h, false);
        venueView_->setOnlyCrowd(false);
        const Px p = measure();
        rbProbe_.crowdPx = int(p.n), rbProbe_.crowdPxMax = std::max(rbProbe_.crowdPxMax, int(p.n)), ++rbProbe_.crowdProbes;
        SDL_Log("crowd probe: %s %s: %ld px, %d characters drawn", venue_ ? venue_->name().c_str() : "-",
                director_ && director_->shot() ? director_->shot()->name.c_str() : "-", p.n, venueView_->crowdView().charactersDrawn());
    }
    r.device().clear(nullptr, true);
    if (static int n = 0; (meterOn || tailOn) && ++n % 300 == 0)
        SDL_Log("rb probe at %.0f ms: Overdrive meter %d frames, %d jumps (%d straight back) over %d beats; star tails %d (%d not tail_bonus.mat), "
                "chroma %d%% over %d px; track draw order %d off of %d; HUD %d pairs out of draw order in %d directories",
                double(lastT_), rbProbe_.meterFrames, rbProbe_.meterJumps, rbProbe_.meterFlickers, rbProbe_.meterBeats, rbProbe_.starTails, rbProbe_.starTailsWrong,
                rbProbe_.tailChromaPct, rbProbe_.tailPx, rbProbe_.trackOrderErrors, rbProbe_.trackOrderParts, rbProbe_.hudOrderInversions,
                rbProbe_.hudOrderDirs);
}

void App::rbHudProbe() {
    rbHudProbeDue_ = false;
    if (!rbHud_ || !sceneCache_) return;
    canvas_.flush();
    gfx::Renderer& r = canvas_.renderer();
    // Cleared to magenta, which no HUD part draws: dark parts count too.
    const float black[4] = {0, 0, 0, 1}, key[4] = {1, 0, 1, 1};
    int worst = 0, boxes = 0;
    for (const RbHudView::Box& e : rbHud_->expectedBoxes()) {
        r.device().clear(key, true);
        rbHud_->drawOnly(r, *sceneCache_, e.name);
        SDL_Surface* raw = canvas_.readPixels();
        SDL_Surface* c = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : nullptr;
        if (raw) SDL_DestroySurface(raw);
        if (!c) continue;
        int x0 = c->w, y0 = c->h, x1 = -1, y1 = -1;
        for (int y = 0; y < c->h; ++y) {
            const uint8_t* row = static_cast<const uint8_t*>(c->pixels) + size_t(y) * size_t(c->pitch);
            for (int x = 0; x < c->w; ++x) {
                const uint8_t* px = row + size_t(x) * 4;
                if (255 - px[0] + px[1] + 255 - px[2] > 24) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
            }
        }
        const float sx = 1280.0f / float(c->w), sy = 720.0f / float(c->h);
        SDL_DestroySurface(c);
        int err = 999;
        float dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;
        if (x1 >= 0) {
            dx0 = float(x0) * sx, dy0 = float(y0) * sy, dx1 = float(x1 + 1) * sx, dy1 = float(y1 + 1) * sy;
            err = e.strict ? int(std::lround(std::max({std::fabs(dx0 - e.x0), std::fabs(dy0 - e.y0), std::fabs(dx1 - e.x1), std::fabs(dy1 - e.y1)})))
                           : int(std::lround(std::max({0.0f, e.x0 - dx0, e.y0 - dy0, dx1 - e.x1, dy1 - e.y1})));  // inside the box
        }
        SDL_Log("rb hud box %-20s expected %4.0f %4.0f - %4.0f %4.0f  drawn %4.0f %4.0f - %4.0f %4.0f  off %d px%s (%s)", e.name.c_str(), double(e.x0),
                double(e.y0), double(e.x1), double(e.y1), double(dx0), double(dy0), double(dx1), double(dy1), err, e.strict ? "" : " outside", rbHud_->configuration().c_str());
        worst = std::max(worst, err);
        ++boxes;
    }
    // The now bar (GD05-064): a gem in each lane at the now bar, alone, then
    // with every HUD element shown drawn over it as the game draws them; the
    // share of the gems' pixels the HUD covers. 0.0.6's track sat under the
    // Overdrive meter and the multiplier (the panel's camera was not applied).
    if (rbTrack_ && !vocal_ && rbTrack_->configured()) {
        auto grab = [&]() {
            SDL_Surface* raw = canvas_.readPixels();
            SDL_Surface* c = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : nullptr;
            if (raw) SDL_DestroySurface(raw);
            return c;
        };
        r.device().clear(key, true);
        const int lanes = rbTrack_->drawNowBarGems(r, *sceneCache_);
        SDL_Surface* a = grab();
        r.device().clear(key, true);
        rbTrack_->drawNowBarGems(r, *sceneCache_);
        for (const RbHudView::Box& e : rbHud_->expectedBoxes()) rbHud_->drawOnly(r, *sceneCache_, e.name);
        SDL_Surface* b = grab();
        if (a && b && a->w == b->w && a->h == b->h) {
            long gem = 0, covered = 0;
            int gy0 = a->h, gy1 = -1;
            for (int y = 0; y < a->h; ++y) {
                const uint8_t* ra = static_cast<const uint8_t*>(a->pixels) + size_t(y) * size_t(a->pitch);
                const uint8_t* rb = static_cast<const uint8_t*>(b->pixels) + size_t(y) * size_t(b->pitch);
                for (int x = 0; x < a->w; ++x) {
                    const uint8_t* pa = ra + size_t(x) * 4;
                    const uint8_t* pb = rb + size_t(x) * 4;
                    if (255 - pa[0] + pa[1] + 255 - pa[2] <= 24) continue;  // magenta: no gem here
                    ++gem, gy0 = std::min(gy0, y), gy1 = std::max(gy1, y);
                    if (std::abs(pa[0] - pb[0]) + std::abs(pa[1] - pb[1]) + std::abs(pa[2] - pb[2]) > 24) ++covered;
                }
            }
            const int pct = gem ? int(std::lround(100.0 * double(covered) / double(gem))) : 100;
            SDL_Log("rb now bar: %d lanes, %ld gem px (rows %d..%d of %d, now bar at %.3f), %ld under the HUD (%d%%)", lanes, gem, gy0, gy1, a->h,
                    double(rbTrack_->nowBarScreenY()), covered, pct);
            rbNowBarGemPx_ = gem ? int(gem) : 0;
            rbNowBarCoverPct_ = std::max(rbNowBarCoverPct_, pct);
        }
        if (a) SDL_DestroySurface(a);
        if (b) SDL_DestroySurface(b);
    }
    r.device().clear(black, true);
    ++rbHudProbes_;
    rbHudBoxes_ = boxes;
    rbHudBoxErrPx_ = std::max(rbHudBoxErrPx_, worst);
}

void App::drawRbTrack0(float t, float speed) {
    if (vocal_) {
        VocalTrackView::State vs;
        vs.t = t + float(videoOffsetMs_);
        vs.voicePitch = voicePitch_, vs.loudness = voiceLoud_;
        vs.lastRating = vocalRatingName_, vs.lastRatingMs = vocalRatingMs_;
        vocalView_.draw(canvas_, song_->vocals, *vocal_, vs);
        return;
    }
    if (!rbTrack_) return;
    if (rbTrack_->part() != parts_[0] && milos_) try {
            auto rb = std::make_unique<RbTrackView>();
            rb->load(*data_, *milos_, parts_[0]);
            rbTrack_ = std::move(rb);
        } catch (const std::exception& e) {
            SDL_Log("RB's %s track: %s", parts_[0].c_str(), e.what());
        }
    RbTrackView::PlayerView pv;
    const Player* p = player_.get();
    pv.chart = &p->chart();
    pv.hit = [p](int i) { return p->gemHit(i) || !p->owned(i); };
    pv.missed = [p, t](int i) { return !p->gemHit(i) && p->chart().gems[size_t(i)].ms < t - 150.0f; };  // [I] the miss window
    pv.phraseIntact = [p](int ph) { return p->phraseIntact(ph); };
    pv.overdriveActive = p->starPowerActive();
    pv.heldGem = p->sustainGem();
    pv.whammy = p->whammyBar();
    pv.heldFrets = p->heldFrets();
    pv.struck = [p](int i) { return p->gemHit(i); };
    // The drums' permuted lanes back to the chart's (kick = lane 0).
    if (parts_[0] == "drum") pv.laneOfBit = {4, 1, 2, 3, 0};
    rbTrack_->setLefty(input_.lefty(0));
    rbTrack_->setIntroAt(rbIntroAt(0));
    rbTrack_->draw(canvas_.renderer(), *sceneCache_, *song_, pv, t + float(videoOffsetMs_), speed);
    ++rbTrackFrames_;
    if (!rbHud_) drawRbNotices(t);
}

// The bonuses' notices over the track [I: RB shows them with the track's
// own widgets (track/gen/player_feedback.milo), not ported yet].
void App::drawRbNotices(float t) {
    if (!rbBonus_) return;
    canvas_.flush();
    const float share = rbBonus_->soloShare(t);
    if (share >= 0) {
        char b[48];
        std::snprintf(b, sizeof b, "SOLO! %d%%", int(share * 100));
        canvas_.textCentered(kScreenW * 0.5f, 200, 3, {120, 200, 255, 255}, b);
    }
    if (rbBonus_->fillActive(t)) canvas_.textCentered(kScreenW * 0.5f, 200, 3, {255, 200, 80, 255}, "FILL! HIT THE CRASH");
    if (rbBonus_->inCoda(t) && !rbBonus_->codaBanked()) {
        char b[48];
        std::snprintf(b, sizeof b, "BIG ROCK ENDING %d", rbBonus_->codaPot());
        canvas_.textCentered(kScreenW * 0.5f, 200, 3, {255, 120, 60, 255}, b);
    }
    for (const RbBonus::Notice& n : rbBonus_->notices())
        if (t - n.ms >= 0 && t - n.ms < 2500) canvas_.textCentered(kScreenW * 0.5f, 250, 3, {255, 235, 120, 255}, n.text);
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
    musicPending_ = Music::None;
    musicLoader_.cancel();
}

// The stream musicLoader_ opened, played once it is ready (GD05-073).
void App::pollMusic() {
    std::string key;
    std::unique_ptr<LoopSource> src = musicLoader_.poll(key);
    if (!src) return;
    const Music kind = musicPending_;
    musicPending_ = Music::None;
    if (player_ || kind == Music::None) return;
    std::vector<Mixer::Track> tracks(static_cast<size_t>(src->channels()));
    if (kind == Music::Preview) {
        auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return "preview:" + s.id == key; });
        if (it == songs_.end()) return;
        tracks = mixerTracks(it->audio);
    } else if (kind == Music::Menu) {
        const int ch = src->channels();
        for (int c = 0; c < ch; ++c) tracks[size_t(c)].pan = ch == 2 ? (c == 0 ? -1.0f : 1.0f) : 0.0f, tracks[size_t(c)].volume = 0.25f;  // -12 dB
    } else {
        // Channels 0 / 2 left, 1 / 3 right (CreditsPanel 0x8229B310) [C].
        for (size_t c = 0; c < tracks.size(); ++c) tracks[c].pan = c % 2 ? 1.0f : -1.0f, tracks[c].volume = 0.8f;
    }
    mixer_.play(std::move(src), tracks, 0);
    music_ = kind;
    ++musicPlays_;
    if (kind == Music::Preview) markFrame("preview_play");
}

void App::songPreview(const std::string& songId) {
    if (player_) return;
    if (songId.empty()) {
        if (music_ == Music::Preview || musicPending_ == Music::Preview) stopMusic();
        return;
    }
    auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return s.id == songId; });
    if (it == songs_.end() || !data_) return;
    // The (preview a b) section with a 1 s fade in and out [I: GH2's fade times].
    musicLoader_.request(*data_, {"preview:" + songId, it->audio.moggPath, it->previewStartMs, it->previewEndMs, 1000});
    musicPending_ = Music::Preview;
    markFrame("preview");
}

void App::menuMusic(bool on) {
    if (player_) return;
    if (!on) {
        if (music_ == Music::Menu || musicPending_ == Music::Menu) stopMusic();
        return;
    }
    if (music_ == Music::Menu || musicPending_ == Music::Menu || !data_) return;
    // RB's config/synth.dta (metamusic (volume -12.0) (fade_time 2.0) (music
    // shellmusic_29fingers ...)) [C data]: a random loop from sfx/streams
    // [I: the fade-in is not applied].
    static const char* const kLoops[] = {"shellmusic_29fingers", "shellmusic_blooddoll", "shellmusic_cantletgo", "shellmusic_daylate",
                                         "shellmusic_imsosick", "shellmusic_nightmare", "shellmusic_pleasure", "shellmusic_seven",
                                         "shellmusic_epic", "shellmusic_suffragette", "shellmusic_thehandthatfeeds", "shellmusic_gowiththeflow",
                                         "shellmusic_missqueen"};
    const std::string name = kLoops[SDL_GetTicks() % (sizeof kLoops / sizeof kLoops[0])];
    musicLoader_.request(*data_, {"menu:" + name, "sfx/streams/" + name + ".mogg", 0, 0, 0});
    musicPending_ = Music::Menu;
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
        if (music_ == Music::Credits || musicPending_ == Music::Credits) stopMusic();
        return;
    }
    if (!data_) return;
    musicLoader_.request(*data_, {"credits", "sfx/streams/credits.mogg", 0, 0, 0});
    musicPending_ = Music::Credits;
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
    if (synth_) synth_->setFxVolumeDb(fxMuted_ ? -96.0f : fxVolumeDb_);
    bandVolume_ = band, guitarVolume_ = guitar;
    // The sliders act at once on the song or preview playing (0x822B0F40 ->
    // 0x822CE830 / 0x822CE938) [C].
    mixer_.setPartVolumes(band, guitar);
    syncOffsetMs_ = syncOffsetMs;
    if (!SDL_getenv("GD05_LEFTY")) input_.setLefty(lefty0, lefty1);
}

// ---- the Enhanced Options (ui/enhanced_panel.h)

namespace {

std::string enhancedPath() {
    const char* dir = SDL_getenv("GD05_SAVE_DIR");
    return (dir && *dir ? std::string(dir) : prefDir()) + "enhanced.json";
}

}  // namespace

// enhanced.json as loadEnhanced reads it (its format, migrateEnhanced: a
// backend this build cannot offer is gone), the two backends only.
void latchStartupBackends() {
    StartupBackends& b = startupBackends();
    b = StartupBackends{};
    b.latched = true;
    std::ifstream f(enhancedPath(), std::ios::binary);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        Json j = Json::parse(ss.str());
        if (j.type != Json::Type::Object || j["format"].str() != "gd05-enhanced") return;
        ui::migrateEnhanced(j);
        for (auto [key, out] : {std::pair<const char*, std::string*>{"graphics_backend", &b.graphics}, {"audio_backend", &b.audio}}) {
            const ui::EnhancedSetting* e = ui::enhancedSetting(key);
            if (j.has(key) && e && ui::enhancedValid(*e, j[key].str())) *out = j[key].str();
        }
    } catch (const std::exception&) {
    }
}

void prepareWalkDirs() {
    // No videos, a fresh save of its own, its song settings.
    SDL_setenv_unsafe("GD05_SKIP_MOVIES", "1", 1);
    static std::string dir;
    if (!dir.empty()) return;
    // SDL's ticks before SDL_Init are nanoseconds from the first call (all
    // walks ~130 ms): parallel or earlier walks could share or reuse a folder
    // (another walk's controllers.json or save). A random name, emptied.
    std::random_device rd;
    char tag[32];
    std::snprintf(tag, sizeof tag, "%08x%08x", unsigned(rd()), unsigned(rd()));
    dir = (std::filesystem::temp_directory_path() / ("gd05-walk-" + std::to_string(SDL_GetTicksNS()) + "-" + tag)).string() + "/";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    SDL_setenv_unsafe("GD05_SAVE_DIR", dir.c_str(), 1);
    // GD05_WALK_ENHANCED_SEED=<json>: the walk's enhanced.json before the game reads it.
    if (const char* seed = SDL_getenv("GD05_WALK_ENHANCED_SEED"); seed && *seed) std::ofstream(dir + "enhanced.json") << seed << "\n";
    // Export / Load User Files answer with a folder of the walk's own.
    static std::string files;
    files = dir + "user-files/";
    std::filesystem::create_directories(files);
    SDL_setenv_unsafe("GD05_DIALOG", files.c_str(), 1);
}

namespace {

// The handheld column of the defaults: the Steam Deck download
// (-DGD05_STEAM_DECK=ON, in enhancedProfile) and a launcher that sees a
// Deck (GD05_HANDHELD=1, module_entry.cpp). Saved settings still win.
bool handheldHost() {
    const char* h = SDL_getenv("GD05_HANDHELD");
    return h && h[0] == '1';
}

// Export User Files' names (gd01's GD01-user-*.json).
constexpr const char* kSettingsFile = "GD05-user-settings.json";
constexpr const char* kProgressFile = "GD05-user-progress.json";
// The Gamepad menu's Export / Import (gd01's GD01-controllers.json).
constexpr const char* kControllersFile = "GD05-controllers.json";

// A file dialog's answer (SDL_DialogFileCallback may run on another thread).
struct DialogRequest {
    std::mutex* mutex;
    std::vector<std::pair<std::string, std::string>>* results;
    std::string action;
};
void SDLCALL dialogDone(void* user, const char* const* files, int) {
    std::unique_ptr<DialogRequest> req(static_cast<DialogRequest*>(user));
    std::lock_guard<std::mutex> lock(*req->mutex);
    req->results->push_back({req->action, files && files[0] ? files[0] : ""});
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
            if (j.type == Json::Type::Object && j["format"].str() == "gd05-enhanced") {
                ui::migrateEnhanced(j);  // 0.1.7 and older values
                enhanced_ = j;
            }
        } catch (const std::exception& e) {
            SDL_Log("enhanced options: %s: %s (the defaults are used)", enhancedPath().c_str(), e.what());
        }
    }
    applyEnhanced();
}

// enhanced.json's HDR value, read before the window exists (True HDR picks its surface then).
std::string App::savedHdrMode() {
    std::ifstream f(enhancedPath(), std::ios::binary);
    if (!f) return "none";
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        Json j = Json::parse(ss.str());
        if (j.type != Json::Type::Object || j["format"].str() != "gd05-enhanced") return "none";
        const ui::EnhancedSetting* e = ui::enhancedSetting("hdr");
        const std::string v = j.has("hdr") ? j["hdr"].str() : "none";
        return e && ui::enhancedValid(*e, v) ? v : "none";
    } catch (const std::exception&) {
        return "none";
    }
}

// Enhanced > HDR's "Now:" line: what the image is doing, and why True HDR
// fell back to Emulated when it did (universal_mechanisms.md "HDR").
std::string App::hdrStatus() {
    const std::string mode = enhancedValue("hdr");
    if (enhancedValue("no_effects") == "on" && mode != "none") return "off: Remove All Effects is on";
    if (mode == "emulated") return "Emulated: HDR rendering tone mapped to your screen";
    if (mode != "true") return "";
    std::string why;
    if (hdrSurface_ == hdr::Surface::Sdr) {
        why = !hdrReason_.empty() ? "unavailable: " + hdrReason_ : "takes effect the next time the game starts";
    } else {
        const std::string live = canvas_.renderer().device().hdrStatus();
        if (live.rfind("off:", 0) != 0) return "True HDR: " + live;
        why = live.substr(5);
    }
    return "True HDR " + why + ". Using Emulated.";
}

std::string App::enhancedValue(const std::string& key) {
    if (key == "hdr_status") return hdrStatus();  // read-only, for the HDR help
    if (key == "graphics_backend_now") return graphicsBackendNow();  // read-only: what draws (after a fallback)
    if (key == "audio_backend_now") return audioBackendNow();        // read-only: what plays
    const ui::EnhancedSetting* e = ui::enhancedSetting(key);
    if (!e) return {};
    if (enhanced_.has(key))
        if (const std::string& v = enhanced_[key].str(); ui::enhancedValid(*e, v)) return v;
    return ui::enhancedDefault(key, handheldHost());  // unset or not valid: the build's default
}

void App::setEnhancedValue(const std::string& key, const std::string& value) {
    if (key == "restart_now") {  // not a setting: Enhanced's Restart Now
        restart_ = true, running_ = false;
        return;
    }
    const ui::EnhancedSetting* e = ui::enhancedSetting(key);
    if (!e || !ui::enhancedValid(*e, value)) return;
    enhanced_.set(key, Json::string(value));
    saveEnhanced();
    applyEnhanced();  // every setting takes effect at once
}

// Written atomically, as the save (ui/career.h).
void App::saveEnhanced() {
    enhanced_.set("format", Json::string("gd05-enhanced"));
    const std::string path = enhancedPath(), tmp = path + ".tmp";
    std::error_code ec;
    if (const std::filesystem::path dir = std::filesystem::path(path).parent_path(); !dir.empty()) std::filesystem::create_directories(dir, ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << enhanced_.dump(1) << "\n";
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) SDL_Log("enhanced options: cannot write %s: %s", path.c_str(), ec.message().c_str());
}

// Enhanced > MODES (a preset, once; every option stays editable), Export /
// Load User Files and the Gamepad menu's Export / Import (gd01's App::userFiles).
void App::enhancedAction(const std::string& action) {
    userFilesStatus_.clear();
    if (action.rfind("mode_", 0) == 0) {
        const auto preset = ui::enhancedPreset(action.substr(5));
        for (const auto& [key, value] : preset) enhanced_.set(key, Json::string(value));
        saveEnhanced();
        applyEnhanced();
        userFilesStatus_ = preset.empty() ? "" : "Mode applied";
        SDL_Log("enhanced options: %s %s", action.c_str(), userFilesStatus_.c_str());
        return;
    }
    if (action != "export" && action != "load_settings" && action != "load_progress" && action != "export_controllers" &&
        action != "import_controllers")
        return;
    // Development aid: GD05_DIALOG=<path> answers the dialog at once (the
    // walk tests); a folder answers the load dialogs with its exported file.
    if (const char* forced = SDL_getenv("GD05_DIALOG")) {
        std::string path = forced;
        std::error_code ec;
        if (action != "export" && action != "export_controllers" && std::filesystem::is_directory(path, ec))
            path = (std::filesystem::path(path) / (action == "load_settings" ? kSettingsFile : action == "import_controllers" ? kControllersFile
                                                                                                                         : kProgressFile)).string();
        std::lock_guard<std::mutex> lock(dialogMutex_);
        dialogResults_.push_back({action, path});
        return;
    }
    auto* req = new DialogRequest{&dialogMutex_, &dialogResults_, action};
    if (action == "export" || action == "export_controllers") {
        SDL_ShowOpenFolderDialog(dialogDone, req, window_, nullptr, false);
    } else {
        static const SDL_DialogFileFilter kJson[] = {{"GD05 user files", "json"}};
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
        if (action == "export") exportUserFiles(path);
        else if (action == "export_controllers")  // the Gamepad menu's Export
            userFilesStatus_ = input_.exportTo((std::filesystem::path(path) / kControllersFile).string()) ? std::string("Exported to ") + kControllersFile
                                                                                                         : "Could not write the controllers file";
        else if (action == "import_controllers")  // the Gamepad menu's Import
            userFilesStatus_ = input_.importFrom(path) ? "Controllers loaded" : "Could not load that controllers file";
        else if (action == "load_settings") userFilesStatus_ = importSettings(path) ? "Settings loaded" : "Could not load that settings file";
        else if (action == "load_progress") userFilesStatus_ = importProgress(path) ? "Progress loaded" : "Could not load that progress file";
        SDL_Log("enhanced options: %s: %s", action.c_str(), userFilesStatus_.c_str());
    }
}

// GD05-user-settings.json: every Enhanced Option (its value or default);
// GD05-user-progress.json: the career save (bands, progress, high scores).
void App::exportUserFiles(const std::string& dir) {
    std::string base = dir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
    Json all = Json::object();
    all.set("format", Json::string("gd05-enhanced"));
    for (const ui::EnhancedSetting& e : ui::enhancedSettings()) all.set(e.key, Json::string(enhancedValue(e.key)));
    bool ok = false;
    {
        std::ofstream out(base + kSettingsFile, std::ios::binary | std::ios::trunc);
        out << all.dump(1) << "\n";
        ok = bool(out);
    }
    if (ui_) {
        std::ofstream out(base + kProgressFile, std::ios::binary | std::ios::trunc);
        out << ui_->career().toJson().dump(1) << "\n";
        ok = ok && bool(out);
    }
    userFilesStatus_ = ok ? "Exported to " + dir : "Could not write to " + dir;
}

// Each known setting with a value it takes is kept; anything odd or missing
// gets its default. Applied and saved at once.
bool App::importSettings(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    Json j;
    try {
        j = Json::parse(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    } catch (const std::exception& ex) {
        SDL_Log("load user settings %s: %s", file.c_str(), ex.what());
        return false;
    }
    if (j.type != Json::Type::Object) return false;
    ui::migrateEnhanced(j);  // a file exported by 0.1.7 or older
    Json next = Json::object();
    for (const ui::EnhancedSetting& e : ui::enhancedSettings()) {
        if (!j.has(e.key)) continue;
        const Json& v = j[e.key];
        if (v.type == Json::Type::String && ui::enhancedValid(e, v.str())) next.set(e.key, v);
        else SDL_Log("load user settings: %s is not valid, keeping the default", e.key);
    }
    enhanced_ = std::move(next);
    saveEnhanced();
    applyEnhanced();
    return true;
}

// The progress replaces the career save (Career::fromJson checks it) and
// is written at once, with The Glitches as every save has them.
bool App::importProgress(const std::string& file) {
    if (!ui_) return false;
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    Career c;
    try {
        if (!c.fromJson(Json::parse(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>())))) return false;
    } catch (const std::exception& ex) {
        SDL_Log("load user progress %s: %s", file.c_str(), ex.what());
        return false;
    }
    c.glitchesLook = ui_->career().glitchesLook;
    c.ensureGlitches();
    ui_->career() = c;
    std::string error;
    if (!ui_->saveFile().write(ui_->career(), error)) SDL_Log("load user progress: cannot save: %s", error.c_str());
    return true;
}

// Puts the Enhanced Options in effect (at start and on every change), as gd01's App::applyEnhanced.
void App::applyEnhanced() {
    auto on = [&](const char* key) { return enhancedValue(key) == "on"; };
    fpsCounter_ = on("fps_counter");
    audioOffsetMs_ = std::atoi(enhancedValue("audio_offset").c_str());
    videoOffsetMs_ = std::atoi(enhancedValue("video_offset").c_str());
    if (player_) player_->setCalibrationMs(calibrationMs());
    if (player2_) player2_->setCalibrationMs(calibrationMs());
    gfx::RenderDevice& dev = canvas_.renderer().device();
    dev.setInternalHeight(std::clamp(std::atoi(enhancedValue("internal_res").c_str()), 0, 4320));
    // Rock Band's display gamma ramp: rnd (gamma 0.85) from config/default.dta,
    // set on the Xbox as the gamma ramp (pow(i / 256, gamma), RB 0x8246BD90),
    // which the XDK composes with its sRGB-to-TV conversion (RB 0x8253DFB8)
    // [C]: the device shows what the console sends an HDTV (render/device.h
    // setOutputGamma; D25). GD05_GAMMA overrides the exponent (0: none).
    {
        float gamma = 0.85f;
        if (const DataNode* rnd = ui_ && ui_->bootTree() ? ui_->bootTree()->find("rnd") : nullptr)
            if (const DataNode* g = rnd->find("gamma"); g && g->children.size() > 1) gamma = g->floatAt(1);
        if (const char* e = SDL_getenv("GD05_GAMMA")) gamma = float(std::atof(e));
        dev.setOutputGamma(gamma);
    }
    auto frameCap = [&] { return enhancedValue("max_fps") == "unlimited" ? 0 : std::clamp(std::atoi(enhancedValue("max_fps").c_str()), 30, 1000); };
    if (!SDL_getenv("GD05_FPS")) dev.setFrameCap(frameCap());
    // Controls: touch (always on on Android); Rock Band guitars and gamepads
    // always play (universal_mechanisms.md: no switches since 0.1.8).
#ifdef __ANDROID__
    input_.setTouch(true);
#else
    input_.setTouch(on("touch"));
    input_.setShakeWhammy(on("shake_whammy"));
#endif
    input_.setRockBand(true);
    input_.setGamepadPlay(true);
    // Sharper Text and Sprites: the menus' textures made from now on.
    if (ui_ && ui_->sceneCache()) ui_->sceneCache()->setEnhance(on("sharp_ui"));
    applyEnhancedLook();
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
        if (!SDL_getenv("GD05_FPS")) dev.setFrameCap(frameCap());  // the new mode's refresh rate
    }
}

// The Enhanced Options that change the picture, on the renderer's post
// chain, the camera director and the particles (and, through
// enhancedLook_, the venue's extra lights and shadows each frame).
void App::applyEnhancedLook() {
    auto v = [&](const char* key) { return enhancedValue(key); };
    auto on = [&](const char* key) { return v(key) == "on"; };
    auto level = [&](const char* key, std::initializer_list<const char*> values) {
        int i = 0;
        for (const char* x : values) {
            if (v(key) == x) return i;
            ++i;
        }
        return 0;
    };
    const bool none = on("no_effects");  // Remove All Effects
    gfx::PostSettings p;
    if (!none) {
        p.bokeh = level("bokeh", {"none", "light", "medium", "strong", "stronger"});
        const std::string aa = v("antialiasing");
        p.aa = aa == "fxaa" ? gfx::PostSettings::AA::Fxaa : aa == "ssaa" ? gfx::PostSettings::AA::Ssaa : aa == "taa" ? gfx::PostSettings::AA::Taa : gfx::PostSettings::AA::None;
        p.fsr1 = v("ai_upscaling") == "fsr1";
        const std::string q = v("ai_upscaling_mode");
        p.fsrScale = q == "performance" ? 0.5f : q == "balanced" ? 1 / 1.7f : 1 / 1.5f;
        if (p.fsr1 && p.aa == gfx::PostSettings::AA::Ssaa) p.aa = gfx::PostSettings::AA::None;  // SSAA does not combine with an upscaler (gd01)
        // The doc's ranges (universal_mechanisms.md 1.2.2), weakest first.
        p.bloom = level("bloom", {"none", "original", "2x", "3x"});
        p.cinematic = on("cinematic_color");
        p.grade = level("color_grade", {"off", "warm", "cold", "vintage", "noir", "neon", "sepia"});
        p.grain = level("film_grain", {"none", "light", "medium", "strong", "stronger"});
        p.chromatic = level("chromatic", {"none", "light", "medium", "strong"});
        p.ssao = level("ssao", {"none", "light", "medium", "strong", "stronger"});
        p.sharpen = level("sharpening", {"off", "low", "high"});
        p.starBlur = level("star_blur", {"none", "light", "medium", "strong", "stronger"});
        // HDR: True HDR needs the window's HDR surface (made at start); without it, Emulated.
        p.hdr = level("hdr", {"none", "emulated", "true"});
    } else {
        p.original = false, p.bokeh = 0;
    }
    canvas_.renderer().post().setSettings(p);
    if (const std::string mode = none ? "none" : v("hdr"); mode != hdrApplied_) {
        hdrApplied_ = mode;
        const std::string st = hdrStatus();
        SDL_Log("hdr: mode %s%s", mode.c_str(), st.empty() ? "" : (" - " + st).c_str());
    }
    gfx::ParticleRenderer::setEnabled(v("particles") != "none");
    gfx::ParticleRenderer::setRate(v("particles") == "2x" ? 2.0f : 1.0f);
    if (director_) director_->setShake(!on("no_shaky_cam") && !none);
    enhancedLook_.shadowsAll = on("shadows_all");
    enhancedLook_.moreLights = level("more_lights", {"none", "1.5x", "2x", "2.5x"});
    enhancedLook_.lightDust = level("light_dust", {"none", "1.5x", "2x", "2.5x"});
    if (venueView_) venueView_->setEnhancedLook(enhancedLook_);
}

// What this frame's port effects need from the venue (render/enhanced_fx.h):
// the camera, the time and Star Power's blur (eased in and out).
void App::enhancedFrame(gfx::Renderer& r) {
    gfx::PostFrame f;
    if (venueView_) {
        gfx::Camera c = venueView_->camera();
        c.jitterX = c.jitterY = 0;
        f.viewProj = c.viewProj(false);
        f.hasCamera = true;
    }
    f.timeSec = float(double(SDL_GetTicksNS()) / 1e9);
    const bool sp = !backdrop_ && ((player_ && player_->starPowerActive()) || (player2_ && player2_->starPowerActive()));
    const double now = double(SDL_GetTicksNS()) / 1e9;
    const float dt = starBlurAt_ > 0 ? float(std::clamp(now - starBlurAt_, 0.0, 0.1)) : 0.0f;
    starBlurAt_ = now;
    starBlur_ = std::clamp(starBlur_ + (sp ? dt / 0.4f : -dt / 0.6f), 0.0f, 1.0f);  // in over 0.4 s, out over 0.6 s
    f.starBlur = starBlur_;
    r.post().setFrame(f);
}

void App::updateTouchLanes() {
    for (int p = 0; p < 2; ++p) {
        Input::TouchLanes l;
        const TrackView* tv = p == 0 ? track_.get() : track2_.get();
        if (tv && (p == 0 || player2_)) l.active = tv->lanes(l.nearX, l.nearY, l.farX, l.farY);
        input_.setTouchLanes(p, l);
    }
}

// The on-screen buttons (Input::touchButtons, menu units): a translucent
// disc with an arrow, or START, in the canvas's 1280 x 720 [I: gd01's look].
void App::drawTouchButtons() {
    const std::vector<Input::TouchButton> buttons = input_.touchButtons();
    if (buttons.empty() || (ui_ && ui_->headless())) return;
    canvas_.flush();
    std::vector<Vertex> v;
    auto tri = [&](float ax, float ay, float bx, float by, float cx, float cy, SDL_Color c) {
        for (auto [x, y] : {std::pair{ax, ay}, std::pair{bx, by}, std::pair{cx, cy}}) v.push_back({x, y, 0, 0, 0, c.r, c.g, c.b, c.a});
    };
    for (const Input::TouchButton& b : buttons) {
        constexpr int kSeg = 32;
        for (int i = 0; i < kSeg; ++i) {
            const float a0 = float(i) / kSeg * 6.2831853f, a1 = float(i + 1) / kSeg * 6.2831853f;
            tri(b.x, b.y, b.x + b.r * std::cos(a0), b.y + b.r * std::sin(a0), b.x + b.r * std::cos(a1), b.y + b.r * std::sin(a1), {0, 0, 0, 110});
        }
        const SDL_Color w{255, 255, 255, 200};
        const float k = b.r * 0.45f;
        if (b.action == Action::Up) tri(b.x - k, b.y + k * 0.5f, b.x + k, b.y + k * 0.5f, b.x, b.y - k * 0.7f, w);
        else if (b.action == Action::Down) tri(b.x - k, b.y - k * 0.5f, b.x + k, b.y - k * 0.5f, b.x, b.y + k * 0.7f, w);
    }
    if (!v.empty()) canvas_.draw(v.data(), v.size(), 0, Blend::Alpha);
    for (const Input::TouchButton& b : buttons)
        if (b.action == Action::Start) canvas_.textCentered(b.x, b.y - 4, 1, {255, 255, 255, 220}, b.y < 100 ? "II" : "START");
}

int App::enhancedState(const std::string& key) {
    gfx::PostFx& post = canvas_.renderer().post();
    const gfx::EnhancedFx* fx = post.enhanced();
    const gfx::EnhancedLights* lights = venueView_ ? venueView_->enhancedLights() : nullptr;
    if (key == "particles") return gfx::ParticleRenderer::enabled() ? 1 : 0;
    if (key == "particle_rate") return int(gfx::ParticleRenderer::rate() * 10 + 0.5f);  // x10
    if (key == "shake") return director_ ? (director_->shake() ? 1 : 0) : -1;
    if (key == "touch") return input_.touch() ? 1 : 0;
    if (key == "rockband") return input_.rockBand() ? 1 : 0;
    if (key == "xinput") return input_.gamepadPlay() ? 1 : 0;
    if (key == "sharp_ui") return ui_ && ui_->sceneCache() ? (ui_->sceneCache()->enhancing() ? 1 : 0) : -1;
    if (key == "post_original") return post.settings().original ? 1 : 0;
    if (key == "fx_on") return post.settings().any() ? 1 : 0;
    if (key == "hdr") return post.settings().hdr;
    if (key == "post_dof") return post.lastDof() ? 1 : 0;
    if (key == "post_key") return int(post.lastKey());
    if (key == "fx_frames") return fx ? fx->frames() : 0;
    if (key.rfind("fx:", 0) == 0) {
        if (!fx) return 0;
        const std::vector<std::string>& passes = fx->lastPasses();
        return std::find(passes.begin(), passes.end(), key.substr(3)) != passes.end() ? 1 : 0;
    }
    if (key == "shadows") return lights ? lights->shadowsDrawn() : 0;
    if (key == "beams") return lights ? lights->beamsDrawn() : 0;
    if (key == "motes") return lights ? lights->motesDrawn() : 0;
    if (key == "calibration_ms") return int(calibrationMs());
    return -1;
}

// Enhanced Options only: the graphics and audio backends this run uses (after
// any fallback to local), in small white text above the FPS counter
// (universal-menus.md "Backends"); GH2's help bar legend sits beside the
// counter and reaches its left, so the label stays on the line above it even
// when the counter is off. A chosen one that could not start shows as "local (<name>
// could not start)".
void App::drawBackendLabel() {
    if (!ui_ || ui_->headless() || player_ || ui_->currentScreen() != "enhanced_options_screen") return;
    auto label = [](const std::string& id) -> std::string {
        static const std::map<std::string, std::string> k = {{"sdl3", "SDL3"},          {"vulkan", "Vulkan"}, {"opengl3", "OpenGL3"},
                                                             {"miniaudio", "miniaudio"}, {"openal", "OpenAL"}, {"faudio", "FAudio"}};
        auto it = k.find(id);
        return it == k.end() ? std::string("local") : it->second;
    };
    auto shown = [&](const std::string& running, const std::string& chosen) {
        return label(running) + (chosen != running && label(chosen) != "local" ? " (" + label(chosen) + " could not start)" : "");
    };
    std::string audio = startupBackends().audio;
    if (const char* b = SDL_getenv("GD05_AUDIO_BACKEND"); b && *b) audio = b;
    const std::string s = "Graphics: " + shown(graphicsBackendNow(), startupBackends().graphics) + "   Audio: " + shown(audioBackendNow(), audio);
    const float y = 650;  // above the help bar's legend (SELECT / BACK start at x 205): the counter's own line would run into it
    canvas_.flush();
    ui::ScreenText& t = ui_->screenText();
    t.begin(canvas_.renderer());
    t.draw(canvas_.renderer().materials(), "helveticablack.font", s, 25, y + 2, 18, {0, 0, 0, 0.6f}, 0x41);
    t.draw(canvas_.renderer().materials(), "helveticablack.font", s, 23, y, 18, {1, 1, 1, 0.9f}, 0x41);
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
    backdropShown_ = on;
    backdropDue_ = on && !backdrop_;  // normally loaded at startup (loadBackdrop)
}

// The backdrop's venue, band and shot. It needs the GL context and the
// game's one world, so it loads on the main thread: at startup, before the
// menus, and again (the next frame, behind the open screen) after a song.
void App::loadBackdrop() {
    if (backdrop_ || player_ || !data_ || songs_.empty() || !ui_ || ui_->headless()) return;
    const uint64_t t0 = SDL_GetTicksNS();
    const std::string venue = venues::names()[1];
    auto b = std::make_unique<Backdrop>();
    for (const SongInfo& s : songs_)
        if (venues::forSong(*data_, s) == venue) {
            b->info = &s;
            break;
        }
    if (!b->info) b->info = &songs_.front();
    if (!loadVenue(venue, VenueVariant::Single)) return;
    world_->silent = true;  // silent and still: no crowd cheer at boot (GD05-023)
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
    // show the band (GD02-074).
    for (int i = 0; backdrop_ && (i < 45 || (!backdrop_->framed && i < 195)); ++i) updateBackdrop();
    if (!band_) SDL_Log("enhanced options: backdrop %s has no band", venue.c_str());
    else if (backdrop_ && !backdrop_->framed) SDL_Log("enhanced options: backdrop %s: no band shot", venue.c_str());
    if (backdrop_) backdrop_->frozen = true, backdropWarm_ = !backdropShown_;
    if (const char* l = SDL_getenv("GD05_UILOG"); l && *l && *l != '0')
        SDL_Log("ui: menu backdrop loaded in %.1f ms", double(SDL_GetTicksNS() - t0) / 1e6);
}

// The backdrop's band is there, the shot picked, and the singer's head in
// the picture (the Enhanced walk's check).
bool App::menuBackdropFramed() {
    if (!backdrop_ || !backdrop_->framed || !band_ || !venueView_) return false;
    scene::Xfm head;
    if (!band_->objectXfm("singer", "bone_head.mesh", head)) return false;
    const gfx::Camera& cam = venueView_->camera();
    const gfx::Mat4 vp = cam.viewProj(false);
    float c[4];
    for (int r = 0; r < 4; ++r) c[r] = vp.at(r, 0) * head.pos[0] + vp.at(r, 1) * head.pos[1] + vp.at(r, 2) * head.pos[2] + vp.at(r, 3);
    return c[3] > 0 && std::fabs(c[0] / c[3]) < 1 && std::fabs(c[1] / c[3]) < 1;
}

bool App::drawMenuBackdrop() {
    if (!backdrop_ || !backdropShown_ || !worldDraw_) return false;
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
    // The shot (D31, GD05-053): one of the venue's own band shots, as RB's
    // picker has them (rbShots: NORMAL1111, the far shots of the whole band,
    // then directed_all), the one that shows most of the band's heads where
    // the Enhanced Options' boxes leave the picture free (the right half,
    // above the help box) [C: the shots and their playback; I: the choice].
    // It plays like a song's shot (its band targets' clips, the director),
    // then freezes with the band.
    if (!b.framed && band_ && director_ && b.simMs >= 500) {  // the band posed half a second first
        std::vector<scene::Xfm> heads;
        for (const auto& m : band_->members()) {
            scene::Xfm h;
            if (band_->objectXfm(m->slot, "bone_head.mesh", h) || band_->objectXfm(m->slot, "", h)) heads.push_back(h);
            if (SDL_getenv("GD05_UILOG")) SDL_Log("enhanced options: member %s at %.0f %.0f %.0f", m->slot.c_str(), h.pos[0], h.pos[1], h.pos[2]);
        }
        const scene::CamShot* best = nullptr;
        int bestScore = -1;
        for (const char* cat : {"NORMAL1111", "directed_all"}) {
            auto it = rbShots_.find(cat);
            if (it == rbShots_.end()) continue;
            for (const scene::CamShot* c : it->second) {
                const gfx::Camera cam = CameraDirector::evaluate(*world_, *c, 0);
                const gfx::Mat4 vp = cam.viewProj(false);
                int score = 0;
                for (const scene::Xfm& head : heads) {
                    float q[4];
                    for (int r = 0; r < 4; ++r) q[r] = vp.at(r, 0) * head.pos[0] + vp.at(r, 1) * head.pos[1] + vp.at(r, 2) * head.pos[2] + vp.at(r, 3);
                    if (q[3] <= 0) continue;
                    const float x = q[0] / q[3], y = q[1] / q[3];
                    if (x > -1 && x < 1 && y > -1 && y < 1) score += 1;          // in the picture
                    if (x > -0.05f && x < 0.95f && y > -0.2f && y < 0.9f) score += 2;  // beside the options' boxes
                }
                if (score > bestScore) best = c, bestScore = score;
            }
            if (bestScore >= int(heads.size()) * 3) break;
        }
        if (best) {
            if (const auto* bs = dynamic_cast<const scene::BandCamShot*>(best)) {
                std::vector<std::pair<std::string, std::string>> groups;
                std::vector<std::pair<std::string, scene::Xfm>> moves;
                for (const auto& tg : bs->bandTargets) {
                    if (!tg.anim.empty()) groups.emplace_back(tg.target, tg.anim);
                    if (tg.teleport) moves.emplace_back(tg.target, tg.to);
                }
                band_->rbShotTargets(groups, moves);
            }
            director_->forceShot(best);
            SDL_Log("enhanced options: backdrop on %s, shot %s (%s), score %d", b.info->id.c_str(), best->name.c_str(), best->category.c_str(), bestScore);
            b.framed = true;
        }
    }
    if (b.framed && director_) {
        director_->update();
        if (director_->hasCamera()) venueView_->setCamera(director_->camera());
    }
    venueView_->update();
}

// One frame of the song screen without the menus (--song, the debug list):
// GH2's track and HUD (or the debug highway), then the 2D text on top.
// GD05_PERF=1 (to measure on the Mac): per frame on average over the
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
        // Development aid: GD05_TRACK=0 hides the track (to look at the band).
        const char* showTrack = SDL_getenv("GD05_TRACK");
        if (!showTrack || *showTrack != '0') {
            track_->draw(canvas_.renderer(), *sceneCache_, *song_, *player_, t + float(videoOffsetMs_), speed);
            if (track2_ && player2_) track2_->draw(canvas_.renderer(), *sceneCache_, *song_, *player2_, t + float(videoOffsetMs_), speed);
            updateTouchLanes();
        }
        // Development aid: GD05_HUD=0 hides the HUD (to look at the track).
        const char* showHud = SDL_getenv("GD05_HUD");
        std::vector<PerformerState> players{player_->state()};
        if (player2_) players.push_back(player2_->state());
        const PerformerState band = coopBand_ ? coopBand_->state() : PerformerState{};
        if (!showHud || *showHud != '0') hud_->draw(canvas_.renderer(), *sceneCache_, canvas_, players, coopBand_ ? &band : nullptr, t);
        // Development aid: GD05_STATS=1 prints the frame rate and particles.
        if (SDL_getenv("GD05_STATS")) {
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
    } else if ((vocal_ || rbTrack_) && sceneCache_) {
        drawWorld();
        drawRbTracks(t, speed);
        if (seats_.empty() && !rbHud_) highway_.drawHud(canvas_, *player_, title);
    } else {
        highway_.draw(canvas_, *song_, *player_, t, speed, title, flashes_);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d:%02d / %d:%02d", int(std::max(0.0f, t) / 60000), int(std::max(0.0f, t) / 1000) % 60,
                      int(song_->endMs / 60000), int(song_->endMs / 1000) % 60);
        canvas_.text(24, 44, 1.5f, {160, 160, 170, 255}, buf);
    }
    // GH2's HUD shows "SONG FAILED!" itself (HudView).
    if (songLost() && !gh2 && !rbHud_) canvas_.textCentered(kScreenW * 0.5f, 330, 4, {230, 60, 60, 255}, "SONG FAILED");
    else if (paused_) canvas_.textCentered(kScreenW * 0.5f, 330, 4, {235, 235, 240, 255}, "PAUSED");
    if (captureDebugScreenshot(canvas_, true, t)) running_ = false;
    canvas_.end();
}

// The first screen, shown until there is a disc image. All its drawing is
// here, in one place, for the renderer that replaces the debug canvas.
void App::startCapture() {
    capture_ = true;
    std::string dir = input_.reportPath();
    dir = dir.substr(0, dir.find_last_of("/\\") + 1);
    captureFile_ = dir + "input-capture.txt";
    captureOut_.open(captureFile_);
    captureOut_ << app::kProgram << " " << app::kVersion << " input capture (send this file with a bug report)\n";
    int v = SDL_GetVersion();
    captureOut_ << "SDL " << SDL_VERSIONNUM_MAJOR(v) << "." << SDL_VERSIONNUM_MINOR(v) << "." << SDL_VERSIONNUM_MICRO(v) << ", video "
                << (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?") << "\n"
                << "Each line: a raw event (the device's own numbers), the gamepad button SDL maps it to, then what a\n"
                << "one-player song does with it (-> action) or why it is ignored.\n\n";
    input_.setTraceSink([this](const std::string& l) {
        captureOut_ << l << "\n";
        captureOut_.flush();
        captureTail_.push_back(l);
        if (captureTail_.size() > 16) captureTail_.erase(captureTail_.begin());
    });
    SDL_Log("input capture: %s", captureFile_.c_str());
}

void App::frameCapture() {
    input_.drainKernel();  // the report's kernel line (GD05-061)
    const SDL_Color text{235, 235, 240, 255}, dim{150, 150, 165, 255}, accent{255, 210, 80, 255};
    canvas_.begin({14, 12, 20, 255});
    canvas_.textCentered(kScreenW * 0.5f, 40, 3, text, "Input capture");
    canvas_.textCentered(kScreenW * 0.5f, 85, 1.8f, accent, "Strum down 5 times, strum up 5 times, press each fret (green to orange),");
    canvas_.textCentered(kScreenW * 0.5f, 110, 1.8f, accent, "move the whammy bar, tilt the guitar up, press Start. Then Esc.");
    std::string devs;
    for (const Input::Device& d : input_.devices())
        if (d.id != "touch") devs += (devs.empty() ? "" : "   ") + d.name + " (" + (d.player < 0 ? "no player" : "player " + std::to_string(d.player + 1)) + ")";
    canvas_.textCentered(kScreenW * 0.5f, 145, 1.4f, dim, devs);
    char c[160];
    std::snprintf(c, sizeof c, "strums %d   frets %d %d %d %d %d   whammy %d   Overdrive %d", captureCounts_[0], captureCounts_[1], captureCounts_[2],
                  captureCounts_[3], captureCounts_[4], captureCounts_[5], captureCounts_[6], captureCounts_[7]);
    canvas_.textCentered(kScreenW * 0.5f, 180, 2, text, c);
    float y = 230;
    for (const std::string& l : captureTail_) canvas_.text(60, y, 1.3f, dim, l), y += 26;
    canvas_.textCentered(kScreenW * 0.5f, 690, 1.3f, dim, "Saved to " + captureFile_);
    if (captureDebugScreenshot(canvas_, false, 0)) running_ = false;
    canvas_.end();
}

void App::frameNeedData() {
    if (SDL_getenv("GD05_DIALOG") && !picker_.busy() && dataMessage_.empty()) picker_.open(window_);  // headless tests
    const SDL_Color text{235, 235, 240, 255}, dim{150, 150, 165, 255}, accent{255, 210, 80, 255}, bad{240, 90, 80, 255};
    canvas_.begin({14, 12, 20, 255});
    canvas_.textCentered(kScreenW * 0.5f, 150, 4, text, app::kWindowTitle);
    canvas_.textCentered(kScreenW * 0.5f, 200, 2, dim, std::string(app::kProgram) + " " + app::kVersion +
                                                         ": Rock Band (Xbox 360) reimplementation");
    canvas_.fillRect(190, 280, 900, 150, {40, 34, 60, 255});
#ifdef __ANDROID__
    canvas_.textCentered(kScreenW * 0.5f, 310, 2.5f, accent, "Tap the screen to choose your");
#else
    canvas_.textCentered(kScreenW * 0.5f, 310, 2.5f, accent, "Click or press Enter to choose your");
#endif
    canvas_.textCentered(kScreenW * 0.5f, 345, 2.5f, accent, "Rock Band (Xbox 360) disc image (.iso)");
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

// The debug song list (GD05_DEBUG_MENU=1, or when GH2's menus cannot start).
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

// Enhanced > Graphics / Audio Backend > Restart Now (universal-menus.md
// "Backends"): the App ends and starts again in the same process (in the
// launcher too: the module keeps running and gets the launcher's window
// again), no boot screens, back on Enhanced Options. A walk goes on as
// "<walk>_restarted" (tests/walktest.h).
int runApp(const Options& options) {
    Options o = options;
    for (;;) {
        bool restart = false;
        int r;
        {
            App app(o);
            r = app.run();
            restart = app.restartAsked();
        }
        if (!restart || r != 0) return r;
        SDL_Log("restart: Enhanced Options asked for a restart (the new backends)");
        const std::string walk = o.walk;
        o = options;
        o.song.clear();
        o.startScreen = "enhanced_options_screen";
        if (!walk.empty()) o.walk = walk + "_restarted";
    }
}

std::string startupGraphicsBackend() {
    if (!startupBackends().latched) latchStartupBackends();  // asked before App::run read it
    return startupBackends().graphics;
}

std::string startupAudioBackend() {
    if (!startupBackends().latched) latchStartupBackends();
    return startupBackends().audio;
}
