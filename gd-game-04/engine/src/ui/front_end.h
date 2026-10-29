// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio/sfx.h"
#include "input/input.h"
#include "script/vm.h"
#include "ui/natives_flow.h"
#include "ui/ui_system.h"

class Canvas;
class Disc;
class Globals;
class TextureCache;

// GH3's front end run from its own scripts (L1): the VM boots with
// guitar_startup, the flow manager builds the screens with screen elements
// (ui/ui_system.h), and the natives the scripts call (script/natives_*.cpp,
// ui/natives_*.cpp) answer for the engine. Game time advances in 60 Hz
// steps.
class FrontEnd {
public:
    FrontEnd(const Disc& disc, Globals& globals, TextureCache& textures, gfx::RenderDevice& device);
    ~FrontEnd();
    bool start();
    void handle(const InputEvent& e);
    void frame(Canvas& c, double dtMs);
    qs::Vm& vm() { return *vm_; }
    ui::UiSystem& ui() { return *ui_; }
    // Buttons pressed since the last GetButtonsPressed (natives).
    struct Pad {
        int makes = 0;
        int device = 0;
    } pad;
    FlowState flow;
    Sfx sfx;
    // A song the scripts started (start_gem_scroller): the app plays it
    // with the native game core, then calls songEnded.
    struct SongRequest {
        bool pending = false;
        uint32_t song = 0;
        int difficulty = 3;
    } songRequest;
    bool songPlaying = false;
    // A movie the scripts asked for (PlayMovie movie = 'atvi'): the app plays
    // it modally (video/movie_player.h).
    std::string movieRequest;
    // The song is over: the flow goes on with win_song / fail_song [C data:
    // GuitarEvent_SongWon_Spawned / GuitarEvent_SongFailed_Spawned].
    // The song's result, as GH3 keeps it in player1_status [C data guitar.qb].
    struct SongResult {
        bool won = false;
        int64_t score = 0;
        int notesHit = 0, totalNotes = 0, bestRun = 0, phrasesHit = 0, phrasesTotal = 0;
        double baseScore = 0;
        float health = 1, starPower = 0;
    };
    void songEnded(const SongResult& r);
    // GetSongTimeMs: the song's time while it plays (set by the app), then
    // running on with the game unless paused (the end-of-song transitions wait on it).
    double songClockMs = 0;
    // The flow state player 1 is in (ui_flow_manager_state[0]), e.g.
    // main_menu_fs; "" before the flow manager starts.
    std::string flowState() const;
    // A GH3 sound event (SoundEvent event = <name>), for the port's screens.
    void soundEvent(const char* event);

private:
    const Disc& disc_;
    Globals& globals_;
    std::unique_ptr<qs::Vm> vm_;
    std::unique_ptr<ui::UiSystem> ui_;
    double accum_ = 0;
    uint32_t lastFlowState_ = 0;
    bool glitchesDone_ = false;
};
