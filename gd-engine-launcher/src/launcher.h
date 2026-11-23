// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The launcher: one window for its whole life. The game list (every game of
// games.yaml), disc images (found, picked or dropped, identified by content),
// and the games themselves, run as modules in this same window.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "action_log.h"
#include "catalog.h"
#include "gfx.h"
#include "ui_sounds.h"
#include "updater.h"

// What the command line asked for (main.cpp): a game to play or explore at
// once, with a disc image for this run, and whether to quit afterwards.
struct CommandLine {
    std::string play, explore;  // game ids
    std::string iso;            // --iso: the disc for this run (not saved)
    bool exitAfter = false;     // --exit-after: quit when the game (or the explorer) ends
};

class Launcher {
public:
    Launcher();
    ~Launcher();
    void setCommandLine(const CommandLine& c) { cli_ = c, cliPending_ = !c.play.empty() || !c.explore.empty(); }
    int run();

private:
    enum class Fret { Green, Red, Yellow, Blue, Orange };
    // Row buttons, left to right as drawn: Play (before the name), then the
    // right-hand columns. Mod is a disabled "coming soon" placeholder.
    enum Column { kPlay = 0, kLoad = 1, kAdditive = 2, kMod = 3, kExplore = 4, kColumns = 5 };
    struct Button {
        std::string label;
        Fret fret;
        std::function<void()> action;
    };
    struct Dialog {
        std::string title, body;
        std::vector<Button> buttons;
        int focus = 0;
        bool open = false;
    };
    struct Hit {
        gfx::Rect r;
        int row, col;  // a row's button (col >= 0) or the row (col -1); row -1: a dialog button (col)
    };
    struct ScriptStep {
        double ms;
        std::string cmd, arg;
    };

    bool init();
    void shutdown();
    void frame();
    void handle(const SDL_Event& e);
    void press(Fret f);
    void move(int dRow, int dCol);
    void activate(int row, int col);
    bool enabled(int row, int col) const;

    void play(Game& g);
    void pickDisc(Game& g);
    void handleDisc(const std::string& path, const std::string& wantedId);
    // The Explore models screen (explorer.cpp): the launcher draws the list
    // and drives the camera, the game module (gd_game_explore) draws the model.
    struct Explore;
    std::unique_ptr<Explore> explore_;
    void exploreOpen(Game& g);
    void exploreClose();
    bool exploreHandle(const SDL_Event& e);  // true: used (the list's handling is skipped)
    void explorePress(Fret f);
    void exploreMove(int dRow, int dCol);
    void exploreGroup(int group, int state);  // state: 1 collapse, 0 expand, -1 toggle; group -2: all
    bool exploreScript(const std::string& cmd, const std::string& arg);  // true: a command of the explorer
    void exploreToggleInstrument();
    void exploreCycleInstrumentVariant();  // Rock Band / World Tour-era instruments (needs module hooks)
    void exploreToggleFullscreen();        // hide the list and chrome, the model alone
    void exploreAnim(int dir);             // +1 next animation, -1 previous
    void exploreCapture();                 // a high-res, no-background PNG of the model, for wikis
    void exploreFrame();
    void exploreDraw();
    void exploreDrawList(const gfx::Rect& panel);
    void exploreDrawViewer(const gfx::Rect& view);
    void exploreDrawFooter();
    void exploreDrawLegend();  // the keyboard shortcuts, in a corner (H toggles it)
    void exploreFilter();
    void exploreCursor(int row);
    void exploreLoad();
    gfx::Rect exploreViewRect() const;

    // The "Hear the songs" screen (songs.cpp): a module's songs listed, one
    // played as a short preview through the launcher's audio device.
    struct Songs;
    std::unique_ptr<Songs> songs_;
    void songsOpen(Game& g);
    void songsClose();
    bool songsHandle(const SDL_Event& e);  // true: used
    void songsFrame();
    void songsDraw();
    void songsPlay(int row);
    void songsStop();

    // The Additive Music management tool (additive.cpp; GDUL-036): a table of
    // the playable games with per-game "additive" and "custom songs" toggles,
    // and a folder tree for each game's root custom-songs folder.
    struct Additive;
    std::unique_ptr<Additive> additive_;
    void additiveOpen(Game* focus);  // focus: the row that opened it (may be null)
    void additiveClose();
    bool additiveHandle(const SDL_Event& e);  // true: used
    void additiveFrame();
    void additiveDraw();
    void additiveSelect(int row);             // select a game row; count its songs, show its folder
    void additiveBuildTree();                 // (re)scan the selected game's custom-songs folder
    void additiveEvaluate(int node);          // quick-validate one song (-1: all)
    void additiveSave();                      // persist the per-game flags (with a status message)
    bool additiveWriteFile();                 // write additive_music.json (flags + cached song counts), silently
    int additiveCountSongs(const Game& g);    // songs on the game's disc (gd_game_songs), or -1
    void additiveOpenInOs();                  // open the custom-songs folder in the OS file manager
    void additivePromptNewFolder();           // open the "new folder" text prompt
    void additivePromptCommit();              // apply the create / rename prompt
    void additiveContextAction(int item);     // a right-click menu item (0 remove,1 evaluate,2 rename,3 open)
    // Other games' discs to hand `host` when it plays, per additive_music.json:
    // the playable games whose EXPOSE flag is on, when `host`'s is on too.
    std::vector<std::pair<std::string, std::string>> exposedDiscsFor(const Game& host) const;

    void askQuit();
    void openWebsite();
    void showDiscFolders(const std::string& title);
    void loadSounds();
    void discsChanged();
    void showDialog(const std::string& title, const std::string& body, std::vector<Button> buttons);
    void toast(const std::string& text);
    void restoreWindow();
    void rememberWindow();
    // A game module's request_window (sdk/gd_host.h, module_host.h WindowSwitch).
    bool switchWindowApi(uint32_t api, std::string& error);
    SDL_Window* makeWindow(uint32_t api, int w, int h, int x, int y, SDL_WindowFlags keep);
    bool startGl(std::string& error);
    void stopGl();
    void leaveFullscreen();
    void toggleFullscreen();  // the header's full-screen / windowed button (and F11)
    bool duplicatePress(const SDL_Event& e);  // true: drop it
    void openGamepads();
    void closeGamepads();
    void runScript();
    void screenshot(const std::string& path);

    // Drawing (design units: a 1280 x 720 screen scaled to the window).
    void draw();
    void drawBackground();
    void drawHeader();
    void drawList();
    void drawRow(int index, float y);
    void drawButton(const gfx::Rect& r, const std::string& label, Fret fret, bool focused, bool enabled, int row, int col);
    // An icon-only button (row columns). `cp` is a gfx::icon:: codepoint;
    // `tint` its colour when enabled. A disabled button draws grey.
    void drawIconButton(const gfx::Rect& r, uint32_t cp, gfx::Color tint, bool focused, bool enabled, int row, int col);
    void icon(uint32_t cp, float cx, float cy, float px, gfx::Color c);  // one icon, centred on (cx, cy)
    void drawColumnHeaders();
    void computeLayout();  // fills the column geometry below (design units), once per frame
    void drawScrollbars(float top, float bottom, float view, float content);
    void drawDreamForge();  // the header's disabled "Dream forge engine" button
    void drawTooltip();     // a "Coming soon" hint over a disabled control
    bool hovering(const gfx::Rect& rPx) const;  // the mouse is over rPx (window pixels), window focused, no modal
    void drawFooter();
    void drawDialog();
    void drawToast();
    void drawDropOverlay();
    void drawLoading(const Game& g, const std::string& what = "Loading");
    gfx::Rect R(float x, float y, float w, float h) const { return {x * s_, y * s_, w * s_, h * s_}; }
    float text(gfx::FontId f, float px, float x, float y, const std::string& t, gfx::Color c, float tracking = 0);
    float measure(gfx::FontId f, float px, const std::string& t, float tracking = 0);
    static gfx::Color fretColor(Fret f);

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;  // null while a game has the window for another API
    uint32_t windowApi_ = 0;           // GD_WINDOW_API_* the window is made for (0: OpenGL)
    SDL_Surface* icon_ = nullptr;
    gfx::Renderer gfx_;
    Catalog catalog_;
    ActionLog log_;
    bool running_ = true;
    CommandLine cli_;
    bool cliPending_ = false;   // its game not started yet
    bool cliExplore_ = false;   // quit when the explorer closes (--explore --exit-after)
    int exitCode_ = 0;          // the process's: the game's, with --play --exit-after
    int lastExit_ = 0;          // the last game run's exit code (1 when it did not start)
    void runCommandLine();
    void cliFail(const std::string& why);

    int pw_ = 0, ph_ = 0;      // window pixels
    float s_ = 1, W_ = 1280, H_ = 720;  // scale and design size
    double now_ = 0;           // seconds since start, at the frame's start
    Uint64 startNs_ = 0;
    int focusRow_ = 0, focusCol_ = kPlay;
    float scroll_ = 0, scrollTarget_ = 0;         // vertical list scroll (design units)
    float hscroll_ = 0, hscrollTarget_ = 0;       // horizontal list scroll (design units)
    float tableW_ = 0;                            // the table's natural width in design units
    bool vbarDrag_ = false, hbarDrag_ = false;    // dragging a scrollbar thumb
    float barDragOff_ = 0;                        // grab offset within the thumb (px)
    // Scrollbar geometry (window pixels), set by drawScrollbars() each frame, read on drag.
    gfx::Rect vThumb_{}, hThumb_{};
    float vTrackY_ = 0, vTrackH_ = 0, vThumbH_ = 0, vRange_ = 0;  // vertical: px + design-unit range
    float hTrackX_ = 0, hTrackW_ = 0, hThumbW_ = 0, hRange_ = 0;  // horizontal
    std::string tooltip_;                         // a hover tooltip to draw this frame ("" none)
    gfx::Rect tooltipAt_{};                       // anchored above this rect (px)
    // Column geometry (design units), set by computeLayout() each frame.
    float lPlayX_ = 0, lPlayW_ = 0, lNameX_ = 0;  // Play button and the name column
    float lColCx_[kColumns] = {};                 // centre x of each right column (kPlay unused)
    float lColW_ = 0;                             // right-column width
    float lListRight_ = 0;                        // right edge available to rows (left of the scrollbar)
    float lScrollX_ = 0;                          // vertical scrollbar centre x
    Dialog dialog_;
    std::string toast_;
    double toastAt_ = -100;
    bool dragging_ = false;
    std::vector<Hit> hits_;
    bool mouseUsed_ = false;

    std::map<SDL_JoystickID, SDL_Gamepad*> pads_;
    struct RecentPress {
        Uint64 ns = 0;
        SDL_JoystickID device = 0;
    };
    std::array<RecentPress, SDL_GAMEPAD_BUTTON_COUNT> recentPress_{};
    std::set<std::pair<SDL_JoystickID, int>> dropped_;  // presses dropped: their release too
    struct SavedWindow {
        bool valid = false, maximized = false;
        int x = 0, y = 0, w = 1280, h = 720;
    } saved_;
    bool userFullscreen_ = false;  // the header's full-screen toggle (desktop; not Big Picture / Android)
    int heldDir_ = 0;  // gamepad direction held: 1 up, 2 down, 3 left, 4 right
    double repeatAt_ = 0;

    // The system file dialog answers on its own thread.
    std::mutex pickedLock_;
    std::vector<std::pair<std::string, std::string>> picked_;  // (game id, path or "" for cancelled)

    UiSounds sounds_;
    bool soundsTried_ = false;
    int framesDrawn_ = 0;
    bool startupNotice_ = false;
    std::string missingModules_;  // ready games whose module the download lacks (said at start)
    static std::string noModuleText(const Game& g);

    // Updates from inside the launcher (update_ui.cpp, updater.h; GDUL-031).
    update::Updater updater_;
    update::Updater::State updateSeen_ = update::Updater::State::Idle;
    bool updateCheckPending_ = false;  // the start-up check, once the window shows
    bool updatePromptPending_ = false; // a newer version found while a dialog was open
    bool updateApplyPending_ = false;  // Ready: swap on the next frame (after the card is drawn)
    std::string updatedFrom_;          // this run follows an update from that version
    void updateInit();
    void updateFrame();
    void updateMenu();
    void updatePrompt();
    void updateProgressDialog();

    std::vector<ScriptStep> script_;
    size_t scriptPos_ = 0;
    std::string pendingShot_;
};
