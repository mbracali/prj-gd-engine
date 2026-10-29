// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>

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
    bool devilFinish = false;
    std::array<bool, 2> lefty{false, false};
    // user_options.lag_calibration (ms): input_offset = the song's - this [C script gem_scroller].
    double lagMs = 0;
};

class SongScreen {
public:
    SongScreen();
    ~SongScreen();
    // `textures`: the players' highway textures are loaded into it (their body paks).
    bool start(const Disc& disc, const Globals& g, const SongInfo& song, int difficulty, bool autoplay, bool noFail, Mixer& mixer,
               std::string& error, TextureCache* textures = nullptr);
    bool start(const Disc& disc, const Globals& g, const SongInfo& song, const SongSetup& setup, bool autoplay, bool noFail, Mixer& mixer,
               std::string& error, TextureCache* textures = nullptr);
    void handle(const InputEvent& e);
    // Advances and draws one frame; false once the song is over.
    bool frame(Canvas& canvas, Mixer& mixer, const TextureCache* textures);
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
    const Match& match() const { return *match_; }
    const Hud2P& hud2p() const { return hud2p_; }

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
    bool devilFinish_ = false;
    std::array<bool, 2> muted_{true, true};  // guitar_volume = 0 (a miss then pulses the highway)
    bool twoPlayers_ = false, hud2pOk_ = false;
    bool highwayOk_ = false;
    const TextureCache* textures_ = nullptr;
    std::array<uint8_t, 2> shownHeld_{};
    double endMs_ = 0;
    double stepMs_ = 0;      // the judge's own clock (60 Hz steps)
    double lastMs_ = -1e9;
    // The live input between steps, per player.
    std::array<uint8_t, 2> held_{};
    std::array<bool, 2> strum_{}, star_{};
    std::array<float, 2> whammy_{};
    double finishedAt_ = -1;
    bool forcedFail_ = false;
    // The venue behind the highway (render/venue_stage.h; GD04_NO_VENUE=1: none).
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
