// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render/ui_view.h"
#include "script/script.h"

// The native objects GH2's UI scripts call (natives_game.cpp,
// natives_meta.cpp, panels.cpp) and the session state they share.
namespace ui {

class UiManager;
class PanelObj;

// A list's data (UIList set_provider): song_provider, stats_provider,
// the credits.
class ListProvider : public ScriptObject {
public:
    using ScriptObject::ScriptObject;
    virtual int numData() = 0;
    virtual std::string symbol(int index) = 0;
    virtual int indexOf(const std::string& sym);
    // UIListProvider::IsActive: an inactive row is drawn disabled and cannot be chosen.
    virtual bool active(int /*index*/) { return true; }
    // The list's rows (headers included) for gfx::UiView::setList, and the
    // row of data index `selected`.
    virtual void rows(std::vector<gfx::UiView::ListRow>& out, int selected, int& selectedRow) = 0;
};

// GameConfig (0x822B8B18) and the players' configs: what the menus chose.
struct Session {
    std::string mode = "quickplay";     // gamecfg get mode (config/modes.dta)
    std::map<std::string, Value> cfgSet;  // gamecfg set <key> <value>
    std::string song;                   // game get_song
    int songIndex = -1;                 // game set_song_index (the provider's index)
    // PlayerConfig (handler 0x822BDDC0) [C]: the character (+0x38) and its
    // outfit (+0x3c, set_character 0x822BD8B8), the guitar (+0x40) and
    // skin (+0x44, set_guitar 0x822BDAA0), kept valid against the guitars
    // the player has (0x822BD490, ui/char_select.h).
    struct PlayerCfg {
        int difficulty = 1;  // kDifficultyMedium
        std::string character = "punk", outfit = "punk1";  // main.dta reset_player_settings
        std::string guitar, skin;
        int padnum = 0;
        int trackType = 2;  // kTrackGuitar (0x822BD6E8)
    };
    std::array<PlayerCfg, 2> players;
    bool allAccess = false;  // campaign set_all_access (the cheat): every item unlocked
    int profileSlot = -1;    // campaign profile_slot (Campaign +0x48): the band chosen
    // The guitars a tour just awarded (Campaign +0x7c, 0x822A8078), shown
    // by unlock_guitar_screen (next_guitar_award takes the last).
    std::vector<std::string> guitarAwards;
    // The endgame photos GamePanel picked as the song loaded (0x822CC730):
    // per player ui/image/og/photo_<outfit><0..3>_keep.bmp.
    std::vector<std::string> winnerPhotos;
    std::string venue;
    bool fastIntro = false;
    bool tutorialRunning = false;
    // The game panel's state.
    bool gameOver = false;
    std::string lastPreview;
};

// The game classes (natives_game.cpp): GamePanel, TrackPanel, HudPanel,
// TrackMaskPanel, MidiLoaderPanel; nullptr for other classes.
std::unique_ptr<ScriptObject> makeGameClass(UiManager& ui, const std::string& cls, const std::string& name);
// The game's named objects (gamecfg, player0, player1, band, beatmatch...).
void installGameNatives(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned);

// Creates the object for {new <Class> <name> ...} (panels, screens,
// providers); nullptr for an unknown class.
std::unique_ptr<ScriptObject> makeClassObject(UiManager& ui, const std::string& cls, const std::string& name);
// The classes makeClassObject knows.
const std::vector<std::string>& knownClasses();
// The named native objects and builtins (game config, campaign, memcard,
// profiles, options, sound, ...).
void installNatives(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned);

// gamecfg get <key>: a (key value) set by script, else the mode's
// (modes.dta), else the defaults'.
Value gameCfgValue(UiManager& ui, const std::string& key);
// The song info index of a song symbol (-1 when unknown).
int songInfoIndex(UiManager& ui, const std::string& song);
// The difficulty symbol (easy medium hard expert).
const char* difficultySym(int d);

// helpbar set_display: ((fret1 help_continue) (strum help_updown) ...).
void setHelpbarDisplay(UiManager& ui, const Value& display);
// A game event (UiManager::gameEvent): the game panel's script and native
// handler, then the sinks that asked for it (game add_sink / add_sinks).
void gameEventImpl(UiManager& ui, const std::string& msg, std::vector<Value> args);

// The headline generator (EndGamePanel, 0x82291428): fills a headline
// token's $adj / $noun / $verb... slots.
std::string generateHeadline(UiManager& ui, const std::string& token, const std::vector<Value>& slots);

}  // namespace ui
