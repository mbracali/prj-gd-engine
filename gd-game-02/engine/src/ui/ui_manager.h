// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <utility>

#include "script/script.h"
#include "ui/career.h"

class Canvas;
class GameData;
class Input;
class UiHost;
struct InputEvent;

namespace gfx {
class SceneCache;
class UiView;
}  // namespace gfx
namespace scene {
class Object;
class MiloLibrary;
class UiStyles;
class Locale;
}  // namespace scene

namespace ui {

class PanelObj;
class ScreenObj;
class ComponentObj;
class ScreenText;
struct Session;

// GH2's kPad_* buttons (system/run/config/macros.dta) [C].
enum Pad {
    kPad_L2 = 0, kPad_R2 = 1, kPad_L1 = 2, kPad_R1 = 3, kPad_Tri = 4, kPad_Circle = 5, kPad_X = 6, kPad_Square = 7,
    kPad_Select = 8, kPad_Start = 11, kPad_DUp = 12, kPad_DRight = 13, kPad_DDown = 14, kPad_DLeft = 15,
};

// GH2's front end, run by the game's own UI scripts (ui/gen/*.dtb through
// script/script.h): the UIManager (handler 0x8236B8B0: goto / push / pop /
// reset_screen, current_screen, focus_panel, in_transition...), GHScreen
// (0x8229E630 / 0x8229EED0) and UIPanel / PanelDir objects with their milos
// drawn by render/ui_view.h, transitions (ui/metacam.milo's trans_* groups,
// UITriggers, transition_time 0.5 s), button routing (the ui object's sfx
// handlers, the screen, its focus panel, the focused component), component
// focus by nav_right / nav_down (0x82373A08), selection (kPad_X on a focused
// button, 0x823704F8; select_frames 30), key repeat (gh2.dta joypad
// hold_ms 1000 / repeat_ms 80, a panel's (joypad ...) overrides it), and the
// native objects the scripts call (game, gamecfg, campaign, song_provider,
// memcard, profilemgr, options...). The song itself stays in app.cpp,
// reached through UiHost.
class UiManager {
public:
    UiManager(const GameData& data, Canvas& canvas, UiHost& host, Input& input);
    ~UiManager();
    UiManager(const UiManager&) = delete;
    UiManager& operator=(const UiManager&) = delete;

    // Loads config/gh2.dta and runs (system (init ...)) and (ui (init ...))
    // (every screen and panel, then $first_screen). False with `error`.
    // `headless`: no drawing resources (the --uitest checks).
    bool init(std::string& error, bool headless = false);
    // Boot: GH2's three splash milos (unless `skipSplashes`), then the
    // screen the init script asked for (or `screen`, GD02_SCREEN).
    void start(bool noSplashes, const std::string& screen = "");

    // One frame: clocks and tasks, transitions, polls, key repeat, anims.
    void update();
    void draw();
    // A menu action from the input layer (mapped to kPad buttons, player).
    void action(const InputEvent& e);
    void buttonDown(int button, int player);
    // A BandTextEntry has the focus (the keyboard's Enter is Start there).
    bool textEntryFocused();
    void buttonUp(int button, int player);
    // A game event for the game panel and its sinks (intro_start_msg,
    // game_won_msg, game_lost, game_outro_complete, star_power_ready...).
    void gameEvent(const std::string& msg, std::vector<Value> args = {});
    // A message for the game panel's script alone (extend_track).
    void gameScript(const std::string& msg, std::vector<Value> args = {});

    std::string currentScreen() const;
    bool inTransition() const;
    bool inSplashes() const { return splash_ >= 0; }
    // The game panel is up (the song is the UI's).
    bool inGame() const;
    // Presses of a kPad button so far (the walk test).
    int pressCount(int button) const { return button >= 0 && button < 16 ? presses_[size_t(button)] : 0; }
    // Screens entered so far, in order (the walk test).
    const std::vector<std::string>& history() const { return history_; }
    void gotoScreen(const std::string& name, bool back = false);
    void pushScreen(const std::string& name);
    void popScreen(const std::string& name);
    Interp& interp() { return interp_; }
    Career& career() { return career_; }
    const std::string& savePath() const;

    // ---- for the objects (ui/*.cpp)
    const GameData& data() const { return data_; }
    UiHost& host() { return host_; }
    Input& input() { return input_; }
    Canvas& canvas() { return canvas_; }
    scene::MiloLibrary& library() { return *library_; }
    // The GPU copies of the scenes the menus draw (null headless).
    gfx::SceneCache* sceneCache() { return cache_.get(); }
    const scene::Locale& locale() const { return *locale_; }
    std::string localize(const std::string& token) const;
    bool headless() const { return headless_; }
    // A new view of a panel milo (nullptr headless or on failure; logged).
    std::unique_ptr<gfx::UiView> makeView(const std::string& path, std::string* error = nullptr);
    ScreenObj* screen(const std::string& name) const;
    PanelObj* panel(const std::string& name) const;
    ScreenObj* current() const { return current_; }
    PanelObj* focusPanel() const;
    // Routes a message the way UIManager::Handle does: the ui object's
    // handlers (sfx), the current screen, its focus panel. Returns the
    // first answer that is not kDataUnhandled.
    Value route(const std::string& msg, std::vector<Value> args);
    // Calls a script handler of `o` if it has one (silently), else returns kDataUnhandled.
    Value callScript(ScriptObject* o, const std::string& msg, std::vector<Value> args = {});
    // Plays an anim / group of a panel over time: frames from..to over
    // `seconds` (units: kTaskUISeconds, kTaskSeconds...); looping by the data.
    // `task`: the task's (name ...), an object `exists` finds while it runs.
    void animate(PanelObj* panel, const std::string& object, float from, float to, float seconds, int units, const std::string& task = "");
    void stopAnimation(PanelObj* panel, const std::string& object);
    // A task clock in seconds (kTaskSeconds: game time, paused with the
    // game; kTaskUISeconds: the menus'; kTaskBeats: the song's beats).
    double clockSeconds(int units) const;
    // kTaskSeconds now: the song in a game, else the menus' seconds.
    double taskSeconds() const;
    // A sound effect (logged as a STUB until GH2's banks are decoded).
    void playSfx(const std::string& name, float volumeDb = 0, float pan = 0, float transpose = 0);
    void logOnce(const std::string& what);
    ScreenText& screenText();
    double nowMs() const { return uiMs_; }
    // A GH2 boot macro's int value (kMCFileExists...), else `def`.
    int macroInt(const std::string& name, int def) const;
    // config/gh2.dta's (modes ...).
    const DataNode* modes() const { return modes_; }
    const DataNode* bootTree() const { return boot_; }
    // Objects reachable by name for scripts: components of panels resolve
    // through this ({tip.lbl ...}).
    ScriptObject* resolve(const std::string& name);
    // Runs `fn` at the start of the next update (memcard results).
    void defer(std::function<void()> fn);
    // The options (volumes, lefty, sync offset) to the program.
    void applyOptions();
    // The port's changes to a panel just loaded (Options' Enhanced Options button, Audio Settings' poster).
    void portPanelLoaded(PanelObj* p);
    // A panel's view was just made (its copy of the metacam follows the others).
    void panelViewLoaded(PanelObj* p);
    bool loadSave(int& result);
    SaveFile& saveFile() { return *save_; }
    // Every object `new` made (the --uitest checks).
    std::vector<ScriptObject*>& madeObjects() { return made_; }
    Session& session() { return *session_; }
    // Draws a panel view through its camera (depth cleared first), then its screen masks.
    // A 3D display panel (GuitarDisplayPanel, CharsysPanel) drew: the next
    // panel keeps its depth, so the store's guitar and characters stay in
    // front of store.milo's wall [I: GH2's panels share the z-buffer].
    void keepDepthOnce() { keepDepth_ = true; }
    void drawView(gfx::UiView& v);
    // GHScreen backwards_anim: the next transition plays backwards.
    void setBackwardsAnim() { backwardsAnim_ = true; }
    // The running (or last) transition goes back (GH2's direction flag,
    // which picks ui_enter_back / ui_exit_back over the _forward events).
    bool transitionBack() const { return back_; }
    // The transition for the port's own screens (gd01's slide): phase 0
    // none, 1 the current screen leaving, 2 it entering; t 0..1; back.
    void transitionState(int& phase, float& t, bool& back) const {
        phase = phase_ == Phase::Out ? 1 : phase_ == Phase::In ? 2 : 0;
        t = phase ? float(std::clamp(phaseMs_ / std::max(1.0, phaseLen_), 0.0, 1.0)) : 1.0f;
        back = back_;
    }
    // {x foreach_player $p body...}: runs the body with $p = player0 / player1.
    Value foreachPlayer(const std::vector<Value>& args);
    // The help bar shows what the current screen's (helpbar ...) says.
    void updateHelpbar();
    // GH2's long cheats (config/long_cheats.dta, CheatsManager 0x8235B5B0):
    // every button pressed, any player; see feedCheats.
    void feedCheats(int button);
    // The cheats' names (cheat_* locale tokens) and whether each is on now,
    // as the scripts' variables say (the Enhanced Options' CHEATS section).
    bool cheatOn(const std::string& id) const;
    // The yellow fret (any controller) on the main menu: GAMEPADS (yellowButton
    // waits out the cheat codes; openGamepads opens it now).
    void yellowButton();
    void openGamepads();
    // Mouse support (gd01's UiSystem::mouse): at (x, y) in menu units,
    // hovering focuses the component under it (a list takes clicks only), a
    // click selects it; the help bar's items act as their buttons; the
    // port's screens hit-test their own rows. True when something was hit.
    bool mouse(float x, float y, bool click);
    // The wheel: one Up (-1) or Down (+1) press.
    void wheel(int dir);
    // Turns a cheat on or off as its button code does (Enhanced Options).
    void setCheat(const std::string& id, bool on);

private:
    friend class PanelObj;
    friend class ScreenObj;
    void runPortScripts();
    void beginTransition(ScreenObj* next, bool back);
    void switchScreens();
    void finishTransition();
    void releaseSharedFrames();
    void updateTransition();
    void applyTransitionPose(float t);
    void updateAnims();
    void updateRepeat(double dtMs);
    void skipSplashes();  // the splashes are over: the first screen
    bool enterIsStart_ = false;  // the keyboard's Enter, pressed on a text entry: Start until released
    void setMetacamFrame(const std::string& group, float t);  // t 0..1 of the group, on every panel's metacam
    void resetMetacam();
    void setViewFrame(PanelObj* p, const std::string& object, float frame);
    void routeButton(int button, int player);
    void enterPanel(PanelObj* p);
    void exitPanel(PanelObj* p);
    void loadPanel(PanelObj* p);
    void drawPanels(ScreenObj* s, bool outgoing);
    void drawSplash();
    void drawMasks(const gfx::UiView& v);

    const GameData& data_;
    Canvas& canvas_;
    bool keepDepth_ = false;
    UiHost& host_;
    Input& input_;
    bool headless_ = false;
    std::unique_ptr<scene::MiloLibrary> library_;
    std::unique_ptr<scene::UiStyles> styles_;
    std::unique_ptr<scene::Locale> locale_;
    std::unique_ptr<gfx::SceneCache> cache_;
    std::unique_ptr<ScreenText> text_;
    std::unique_ptr<ScriptLoader> loader_;
    Interp interp_;
    const DataNode* boot_ = nullptr;
    const DataNode* modes_ = nullptr;
    std::map<std::string, DataNode> macros_;
    std::vector<std::unique_ptr<ScriptObject>> natives_;
    std::unique_ptr<Session> session_;
    Career career_;
    std::unique_ptr<SaveFile> save_;
    std::set<std::string> logged_;
    std::set<const void*> audioPosterLowered_;  // portPanelLoaded: Audio Settings' poster moved once
    // The long cheats: each one's buttons and script; the buttons pressed
    // lately (16 at most, oldest first) and when the last one was.
    struct LongCheat {
        std::vector<int> buttons;
        const DataNode* script = nullptr;
    };
    std::vector<LongCheat> longCheats_;
    std::deque<int> cheatButtons_;
    double cheatLastMs_ = -1e9;
    double gamepadsAtMs_ = -1;
    std::array<int, 16> presses_{};  // yellowButton: GAMEPADS opens then (SDL ms), -1 none

    // screens
    ScreenObj* current_ = nullptr;
    ScreenObj* next_ = nullptr;  // being transitioned to
    ScreenObj* previous_ = nullptr;
    bool back_ = false;
    std::string pending_;
    bool pendingBack_ = false;
    enum class Phase { Idle, Out, In, Complete } phase_ = Phase::Idle;
    double phaseMs_ = 0, phaseLen_ = 500;
    bool backwardsAnim_ = false;
    // The shared metacam's groups since its last reset, in order (frame 0..1).
    std::vector<std::pair<std::string, float>> metacam_{{"trans_in_forward.grp", 1.0f}};
    // The last frame of each animation in a directory several panels share
    // (setViewFrame), for views made later.
    std::map<const scene::Object*, std::pair<std::string, float>> sharedFrames_;
    std::set<std::string> viewPanels_;  // the panels that have made a view
    std::vector<ScreenObj*> stack_;  // push_screen
    // GH2's PushScreen (0x8236B800) enters the new screen over the current
    // one without exiting it; PopScreen (0x8236AE40) exits the pushed screen
    // and the one below goes on as it was (not entered again).
    bool pushing_ = false, popping_ = false;
    std::vector<std::string> history_;
    double transitionMs_ = 500;  // default.dta (ui (transition_time 0.5))

    // boot splashes
    int splash_ = -1;
    double splashMs_ = 0;
    std::vector<std::unique_ptr<gfx::UiView>> splashViews_;
    std::string firstScreen_;

    // clocks (ms)
    double uiMs_ = 0, secondsMs_ = 0, lastWall_ = 0, frameDt_ = 0;
    std::vector<PanelObj*> entered_;
    std::vector<ScriptObject*> made_;
    std::vector<std::function<void()>> deferred_;  // panels the current transition entered

    // anims over time
    struct Anim {
        PanelObj* panel;
        std::string object;
        float from, to;
        double start, length;  // in the units' clock (s)
        int units;
        std::string task;  // (name ...): registered while it runs
    };
    std::vector<Anim> anims_;
    std::map<std::string, std::unique_ptr<ScriptObject>> namedTasks_;
    void endTask(const std::string& task);

    // key repeat (JoypadClient): per player the held direction.
    struct Held {
        int button = -1;
        double ms = 0;
        bool repeating = false;
    };
    std::array<Held, 2> held_{};
    float holdMs_ = 1000, repeatMs_ = 80;
};

}  // namespace ui
