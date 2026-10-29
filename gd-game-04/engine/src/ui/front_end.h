// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
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
    void songEnded(bool won);

private:
    const Disc& disc_;
    Globals& globals_;
    std::unique_ptr<qs::Vm> vm_;
    std::unique_ptr<ui::UiSystem> ui_;
    double accum_ = 0;
    uint32_t lastFlowState_ = 0;
};
