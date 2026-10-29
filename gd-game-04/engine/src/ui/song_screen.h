// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>

#include "game/autoplay.h"
#include "game/chart.h"
#include "game/note_track.h"
#include "game/player.h"
#include "game/song_db.h"
#include "game/tuning.h"
#include "input/input.h"
#include "render/highway2d.h"
#include "render/hud2d.h"

class Canvas;
class Disc;
class Globals;
class Mixer;
class TextureCache;

// One song played directly (`--song <id>`, the development shortcut, L6):
// the song's stems through the mixer, the player's judge (game/player.h) on
// GH3's 60 Hz step, the guitar stem muted on a miss. Drawn with a plain
// debug highway until GH3's own 2D highway and HUD (highway_2d.qb, M4) are
// reimplemented: a labelled development aid, not GH3's look.
class SongScreen {
public:
    bool start(const Disc& disc, const Globals& g, const SongInfo& song, int difficulty, bool autoplay, bool noFail, Mixer& mixer,
               std::string& error, const TextureCache* textures = nullptr);
    void handle(const InputEvent& e);
    // Advances and draws one frame; false once the song is over.
    bool frame(Canvas& canvas, Mixer& mixer, const TextureCache* textures);
    const PlayerStats& stats() const { return player_->stats(); }
    double songMs() const { return lastMs_; }
    Hud2D& hud() { return hud_; }
    // The base score (50 a gem, no multiplier) and the Star Power phrases [I: GH3's base_score].
    double baseScore() const;
    int phraseCount() const;
    const Highway2D& highway() const { return highway_; }

private:
    void tick(double nowMs, Mixer& mixer);
    void draw(Canvas& canvas, double nowMs, const TextureCache* textures);

    SongInfo song_;
    int difficulty_ = 3;
    Chart chart_;
    Tuning tuning_;
    NoteTrack track_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Autoplay> autoplay_;
    Highway2D highway_;
    Hud2D hud_;
    bool highwayOk_ = false;
    const TextureCache* textures_ = nullptr;
    uint8_t shownHeld_ = 0;
    double endMs_ = 0;
    double stepMs_ = 0;      // the judge's own clock (60 Hz steps)
    double lastMs_ = -1e9;
    // The live input between steps.
    uint8_t held_ = 0;
    bool strum_ = false, star_ = false;
    float whammy_ = 0;
    double finishedAt_ = -1;
    bool forcedFail_ = false;

public:
    // Walk tests end the song early (ms of song time; -1: play it all).
    double walkEndMs_ = -1, walkFailMs_ = -1;

public:
    bool failed() const { return forcedFail_ || player_->stats().failed; }
};
