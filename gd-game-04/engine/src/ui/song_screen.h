// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <string>

#include "data/qb.h"
#include "game/autoplay.h"
#include "game/chart.h"
#include "game/match.h"
#include "game/note_track.h"
#include "game/player.h"
#include "game/song_db.h"
#include "game/tuning.h"
#include "input/input.h"
#include "render/highway2d.h"
#include "render/hud2d.h"
#include "render/hud2p.h"

class Canvas;
class Disc;
class Globals;
class Mixer;
class TextureCache;
class VenueStage;

// One song played directly (`--song <id>`, the development shortcut, L6):
// the song's stems through the mixer, the player's judge (game/player.h) on
// GH3's 60 Hz step, the guitar stem muted on a miss. Drawn with a plain
// debug highway until GH3's own 2D highway and HUD (highway_2d.qb, M4) are
// reimplemented: a labelled development aid, not GH3's look.
// Two players (GH3's modes, game/match.h): the start's setup.
struct SongSetup {
    GameMode mode = GameMode::Solo;
    std::array<bool, 2> rhythm{false, false};  // playerN_status.part = rhythm
    std::array<int, 2> difficulty{3, 3};
    uint32_t bossProps = 0;
    bool suddenDeath = false;
    std::array<float, 2> startHealth{1.0f, 1.0f};
    std::array<bool, 2> autoplayFires{true, true};
    // starttime (ms): a mid-song start; Lou's finish (devil_finish: player 2's
    // highway and HUD off screen); the players' lefty flip [C start_gem_scroller].
    double startMs = 0;
    double endMs = -1;  // the gems end here and the song finishes (practice's section end; -1: the song's)
    // Practice's speed and its guitar pitch correction (FrontEnd::SongRequest; Mixer::setSlomo).
    float speed = 1, slowPitch = 1;
    bool devilFinish = false;
    std::array<bool, 2> lefty{false, false};
    // user_options.lag_calibration (ms): input_offset = the song's - this [C script gem_scroller].
    double lagMs = 0;
    // Enhanced > Audio / Video Calibration (gd01's, #69): the audio delay
    // (also added to lagMs by the app) moves the highway with the judging,
    // so the gems reach the line when they are heard; the video delay draws
    // it that much ahead. GH3's lag_calibration moves the judging only.
    double audioOffsetMs = 0;
    double videoOffsetMs = 0;
    // The highway's draw time less the song time.
    double drawOffsetMs() const { return videoOffsetMs - audioOffsetMs; }
    bool endCredits = false;  // end_credits = 1 (the credits song) [C script start_gem_scroller]
    // current_transition's key (0: fastintro): the intro's length (its
    // Default_<t>_Transition time) and cameras (Common_<t>_Transition's
    // Transition_CameraCut prefix) [C script start_gem_scroller].
    uint32_t transition = 0;
};

// A song's venue loaded on a worker thread while GH3's loading screen is up
// (owner 1.7: the loading screen froze, then the song started): its scene,
// band, crowd, particles and peds read from a snapshot of the globals and
// its own chart (both kept alive with the stage); the GPU upload stays on
// the first frame. GH3 itself loads the song pak and the band async
// (load_songqpak / create_band async = 1) and destroys the loading screen
// after [C script start_gem_scroller].
struct VenuePreload {
    std::string song, zone, cameras;
    double leadInMs = 0, endMs = -1, startMs = 0;
    std::shared_ptr<const Globals> globals;
    std::unique_ptr<Chart> chart;
    std::unique_ptr<VenueStage> stage;
    std::future<void> work;
    std::atomic<bool> finished{false};
    bool ready() const { return finished.load(); }
    ~VenuePreload();
    // Starts loading `songName`'s venue for `transition` (current_transition) on a worker.
    static std::shared_ptr<VenuePreload> launch(const Disc& disc, const Globals& live, const std::string& songName, uint32_t transition,
                                                double endMs, double startMs);
};

class SongScreen {
public:
    // The venue preloaded for this song (VenuePreload), used by the next start() when it matches.
    void usePreload(std::shared_ptr<VenuePreload> p) { preload_ = std::move(p); }
    SongScreen();
    ~SongScreen();
    // `textures`: the players' highway textures are loaded into it (their body paks).
    bool start(const Disc& disc, const Globals& g, const SongInfo& song, int difficulty, bool autoplay, bool noFail, Mixer& mixer,
               std::string& error, TextureCache* textures = nullptr);
    bool start(const Disc& disc, const Globals& g, const SongInfo& song, const SongSetup& setup, bool autoplay, bool noFail, Mixer& mixer,
               std::string& error, TextureCache* textures = nullptr);
    void handle(const InputEvent& e);
    // Advances and draws one frame; false once the song is over (a won
    // song with holdAfterWin goes on until the caller ends it).
    bool frame(Canvas& canvas, Mixer& mixer, const TextureCache* textures);
    // The song played to its end: song_won was due (end marker + Song_Win_Delay) [C script win_song].
    bool won() const { return won_; }
    // The end marker reached (win_song's first wait over; a walk's forced end too).
    bool endReached() const { return forcedEnd_ || lastMs_ >= songEndMs_; }
    double songEndMs() const { return songEndMs_; }
    // GH3's scripts run the end: a won or decided song (failed, a battle
    // KO, a boss beaten) keeps playing and drawing under them until
    // kill_gem_scroller (the app ends it) [C: song_won / song_failed fire
    // at once; GuitarEvent_SongFailed_Spawned, GuitarEvent_SongWon_Spawned].
    void setHoldAfterWin(bool on) { holdAfterWin_ = on; }
    // Decided before the end marker: failed, a battle KO, a boss beaten (not practice's end).
    bool decided() const { return finishedAt_ >= 0 && !won_ && !practiceEnd_; }
    // The end's teardown: destroy_hud (the HUD), disable_highway (the gem
    // containers and HUD at alpha 0) [C scripts play_outro, disable_bg_viewport].
    void hideHud() { hudHidden_ = true; }
    void hideHighway() { highwayHidden_ = hudHidden_ = true; }
    bool highwayHidden() const { return highwayHidden_; }
    const PlayerStats& stats() const { return player_->stats(); }
    double songMs() const { return lastMs_; }
    Hud2D& hud() { return hud_; }
    // The HUD texts' font functions (both HUDs).
    void setHudText(Hud2D::TextFn text, Hud2D::WidthFn width) {
        hud_.setText(text, width);
        introText_ = text;
        hud2p_.setText(std::move(text), std::move(width));
    }
    // The base score (50 a gem, no multiplier) and the Star Power phrases [I: GH3's base_score].
    double baseScore() const { return match_->baseScore(0); }
    int phraseCount() const { return match_->phraseCount(0); }
    const Highway2D& highway() const { return highway_; }
    const Highway2D& highway2() const { return highway2_; }
    double startMs() const { return startMs_; }
    float speed() const { return speed_; }  // practice's speed (1 normal)
    // The first gem's arrival (SongScreen::frame; walk practice checks it).
    struct SyncCheck {
        bool measured = false;
        uint64_t wall0 = 0;  // the first frame's wall clock (ns) and song time
        double song0 = 0, noteMs = 0, lateMs = 0, clockErrMs = 0, audioErrMs = 0;
        float speed = 1;
    };
    const SyncCheck& syncCheck() const { return sync_; }
    const std::string& introCameras() const { return introCameras_; }
    // The intro transition's play_intro props (current_intro); empty: no play_intro.
    const std::string& introProps() const { return introProps_; }
    const Match& match() const { return *match_; }
    const Hud2P& hud2p() const { return hud2p_; }
    const Chart& chart() const { return chart_; }
    const SongInfo& song() const { return song_; }
    bool twoPlayers() const { return twoPlayers_; }
    // The bar's position, 0 rest .. 1: analog bars set it every frame, a key
    // or button through handle() (held: 1). Kept apart, so a connected pad's
    // resting bar does not cancel the keyboard's Whammy key (#70, GD04-115).
    void setWhammy(int player, float value) { whammyBar_[size_t(player & 1)] = value; }
    // A strum waiting for the next judge step (the self-test's check).
    bool strumPending(int player) const { return strum_[size_t(player & 1)]; }
    float whammy(int player) const {
        const size_t p = size_t(player & 1);
        return whammyKey_[p] ? 1.0f : whammyBar_[p];
    }
    // The player's highway (its sustain tails' wibble: SetNewWhammyValue /
    // SetAllWhammyValues): player 0 the left / only one, 1 the right.
    Highway2D& highwayOf(int player) { return (player & 1) && twoPlayers_ ? highway2_ : highway_; }
    // Player p's guitar muted (guitar_volume 0) after its last judged note.
    bool guitarMuted(int p) const { return muted_[size_t(p & 1)]; }
    VenueStage* venue() { return venue_.get(); }
    // Called on every 60 Hz judge step after the match ran, before its
    // events are cleared: GH3's song scripts run from them (ui/song_hooks.h).
    std::function<void(SongScreen&, double stepMs)> onStep;

private:
    void tick(double nowMs, Mixer& mixer);
    void draw(Canvas& canvas, double nowMs, const TextureCache* textures);

    Highway2D::Effects effects(int player, double now) const;
    // The highway texture of player `p` (0 / 1): its character's
    // sys_<name>_1_highway material, its body pak loaded first [C script create_guitarist].
    uint32_t highwayTexture(const Disc& disc, const Globals& g, TextureCache& tex, int p, uint32_t bossProps) const;

    SongInfo song_;
    int difficulty_ = 3;
    Chart chart_;
    Tuning tuning_;
    std::unique_ptr<Match> match_;
    Player* player_ = nullptr;  // player 1's judge (match_->player(0))
    Highway2D highway_, highway2_;
    Hud2D hud_;
    Hud2P hud2p_;
    IntroText intro_;
    Hud2D::TextFn introText_;
    double startMs_ = 0;      // current_starttime
    float speed_ = 1;         // setslomo's speed (practice)
    SyncCheck sync_;
    bool devilFinish_ = false;
    std::array<bool, 2> muted_{true, true};  // guitar_volume = 0 (a miss then pulses the highway)
    bool twoPlayers_ = false, hud2pOk_ = false;
    bool highwayOk_ = false;
    const TextureCache* textures_ = nullptr;
    std::array<uint8_t, 2> shownHeld_{};
    std::array<double, 2> lastHammer_{-1e9, -1e9};  // Match::BattleSide::highwayPushAt last seen
    std::array<bool, 2> spSeen_{};  // Star Power on at the last step (handle_star_power's edge)
    double stepMs_ = 0;      // the judge's own clock (60 Hz steps)
    double lastMs_ = -1e9;
    // The live input between steps, per player.
    std::array<uint8_t, 2> held_{};
    std::array<bool, 2> strum_{}, star_{}, tilt_{};
    std::array<float, 2> whammyBar_{};
    std::array<bool, 2> whammyKey_{};
    double finishedAt_ = -1;
    // The SongWon / SongLost transition's length (Default_SongWon_Transition
    // time 8000 [C data]); the outro started (the venue told).
    double transitionMs_ = 8000;
    bool holdClock_ = false;  // the mixer paused until the venue is up (frame)
    double drawOffsetMs_ = 0;  // the highway's draw time - now (video - audio calibration)
    double leadInMs_ = 3000;  // the intro transition's time before the music
    std::string introCameras_ = "cameras_fastintro";
    bool introFast_ = true;
    std::string introProps_ = "fastintro_sequence_props";
    bool outro_ = false;
    std::string outroPrefix_;
public:
    // After the song: the venue alone at `ms` (song time) while GH3's
    // transition runs; false once there is nothing to draw.
    bool drawOutro(Canvas& c, double ms);
    // GuitarEvent_SongWon_Spawned plays PreEncore / PreBoss when the tier's
    // unlock set current_transition (Progression_TierEncoreUnlock /
    // TierBossUnlock): the same win stances, their own cameras [C data
    // Common_PreEncore_Transition, Common_PreBoss_Transition].
    void outroCameras(double ms, const std::string& prefix);
    void playClip(int role, uint32_t clip, uint32_t stance, bool cycle, double ms);
    // A band member by its composite object's name (Guitarist, Bassist,
    // Drummer, vocalist): whether the song has it, and its handle_play_anim
    // (play_battle_anim's data: Anim, no_wait) [C scripts Band_PlayAttackAnim, Band_PlayResponseAnim].
    bool hasMember(uint32_t object) const;
    bool memberPlayAnim(uint32_t object, const qb::Struct& params, double ms);
    double lastMs() const { return lastMs_; }
    double transitionMs() const { return transitionMs_; }
private:
    bool forcedFail_ = false, forcedEnd_ = false, practiceEnd_ = false;
    double songEndMs_ = 0, winDelayMs_ = 2000;  // get_song_end_time, Song_Win_Delay (ms)
    bool won_ = false, holdAfterWin_ = false, reported_ = false;
    bool highwayHidden_ = false, hudHidden_ = false;
    // The venue behind the highway (render/venue_stage.h; GD04_NO_VENUE=1: none).
    std::shared_ptr<VenuePreload> preload_;  // owns the preloaded stage's globals and chart: before venue_
    std::unique_ptr<VenueStage> venue_;
    const Disc* disc_ = nullptr;

public:
    // Walk tests end the song early (ms of song time; -1: play it all).
    double walkEndMs_ = -1, walkFailMs_ = -1;

public:
    bool failed() const { return forcedFail_ || match_->outcome() == Match::Outcome::Failed; }
    // Decided before the end (a battle KO, a boss beaten): Match::Outcome.
    Match::Outcome outcome() const { return match_->outcome(); }
};
