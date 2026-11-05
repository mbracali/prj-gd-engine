// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>

#include "input/input.h"

class FrontEnd;

// Enhanced > Enable Cut Content's venue pick on quick play's set list (the
// owner's GD04-095; not in the original game): the yellow fret on the
// SETLIST tab (GH3's own yellow there only re-selects that tab: change_tab
// returns at once) opens GH3's popup (create_popup_warning_menu) listing
// the venues LevelZones keeps `$debug_only` (z_soundcheck, z_credits; not
// z_viewer, a zone with no scene). The venue picked is the next song's only:
// start_song finds it in current_level, set over quickplay_start_song's
// random venue, then quick play draws its own again. A help bar pill
// (add_user_control_helper, yellow) names it.
class CutVenues {
public:
    explicit CutVenues(FrontEnd& fe);
    // A menu action before GH3 sees it: true when taken.
    bool button(const InputEvent& e);
    // A script starting (Vm::onScriptEnter).
    void scriptEnter(uint32_t name);
    // Once a frame in the menus: the help bar's pill.
    void update();
    // The level the next song plays in (LevelZoneArray's key), 0: quick play's own.
    uint32_t pending() const { return pending_; }

private:
    bool available() const;
    void open();
    void close();
    FrontEnd& fe_;
    uint32_t pending_ = 0;
    bool open_ = false;
    bool pillWanted_ = false;
};
