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
    // Called when a list takes the provider (UIList set_provider): the data
    // is read again then [I: UIListProvider::InitData; this disc's career
    // song list has no other refresh call].
    virtual void initData() {}
    virtual std::string symbol(int index) = 0;
    virtual int indexOf(const std::string& sym);
    // UIListProvider::IsActive: an inactive row is drawn disabled and cannot be chosen.
    virtual bool active(int /*index*/) { return true; }
    // The list's rows (headers included) for gfx::UiView::setList, and the
    // row of data index `selected`.
    virtual void rows(std::vector<gfx::UiView::ListRow>& out, int selected, int& selectedRow) = 0;
};

// GH2's MsgSource for the players and their matchers: add_sink <obj>
// [msg...] and remove_sink (PS2 0x002c0038 -> 0x002c0548 / 0x002c0738);
// any other message the object does not handle goes to every sink (all
// messages) and to the sinks that named it (0x002c01f8) [C]. The
// tutorials add their lesson objects to hear the gameplay's *_callback
// messages.
class MsgSinks : public SinkHolder {
public:
    ~MsgSinks() override { stopHolding(); }
    // add_sink / remove_sink: true when `msg` was one of them.
    // The sink comes as an object or by name ({player0 add_sink tut_22_callback}).
    bool handle(Interp& in, const std::string& msg, const std::vector<Value>& args);
    // To the sinks that want `msg`, from a copy (a sink removed on the way is skipped).
    void send(Interp& in, const std::string& msg, const std::vector<Value>& args);
    void clear() { sinks_.clear(); }
    void sinkDestroyed(ScriptObject* sink) override;

private:
    std::vector<std::pair<ScriptObject*, std::vector<std::string>>> sinks_;
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
        // gamecfg set_track: the index of the instrument played in the
        // song's (tracks ...) (song_provider get_instrument), 0 the guitar.
        int track = 0;
    };
    std::array<PlayerCfg, 2> players;
    // The PS2 has no controller select: player n plays the pad in port n
    // (this disc's scripts never send gamecfg set_player) [C data].
    Session() { players[1].padnum = 1; }
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
    // The tutorials' voice-over events played, in order (TutorialPanel play_vo; the walks check them).
    std::vector<std::string> voiceEvents;
    // play_vo requests dropped because another VO was playing (0x001407f0).
    std::vector<std::string> voiceDropped;
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
// The instruments of a song's (tracks ((guitar ...) (bass ...))), in order,
// from the mode's song_block, else its `song` block (0x822B38F8) [C].
std::vector<std::string> songInstruments(UiManager& ui, const std::string& song);
// kTrackBass 0, kTrackRhythm 1, kTrackGuitar 2 (config/macros.dta) of an instrument.
int trackTypeOf(const std::string& instrument);
// The song info index of a song symbol (-1 when unknown).
int songInfoIndex(UiManager& ui, const std::string& song);
// The difficulty symbol (easy medium hard expert).
const char* difficultySym(int d);

// A Fader's level now (dB; 0 for one the port does not keep): music_fader,
// which practice's metronome sounds are added to (ComponentObj add_fader).
float faderDb(const std::string& fader);
// A song starts: the faders back at 0 dB, as the music.
void resetFaders();

// helpbar set_display: ((fret1 help_continue) (strum help_updown) ...).
void setHelpbarDisplay(UiManager& ui, const Value& display);
// A game event (UiManager::gameEvent): the game panel's script and native
// handler, then the sinks that asked for it (game add_sink / add_sinks).
void gameEventImpl(UiManager& ui, const std::string& msg, std::vector<Value> args);
// A message to the game panel's script only (extend_track: game.dta handles
// it, so nothing goes on to the sinks).
void gameScriptImpl(UiManager& ui, const std::string& msg, std::vector<Value> args);

// The headline generator (EndGamePanel, 0x82291428): fills a headline
// token's $adj / $noun / $verb... slots.
std::string generateHeadline(UiManager& ui, const std::string& token, const std::vector<Value>& slots);

}  // namespace ui
