// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The launcher: one window for its whole life. The game list (every game of
// games.yaml), disc images (found, picked or dropped, identified by content),
// and the games themselves, run as modules in this same window.
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "action_log.h"
#include "catalog.h"
#include "gfx.h"

class Launcher {
public:
    int run();

private:
    enum class Fret { Green, Red, Yellow, Blue, Orange };
    enum Column { kPlay = 0, kLoad = 1, kExplore = 2, kColumns = 3 };
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
    void exploreModels(Game& g);
    void askQuit();
    void showDialog(const std::string& title, const std::string& body, std::vector<Button> buttons);
    void toast(const std::string& text);
    void restoreWindow();
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
    void drawFooter();
    void drawDialog();
    void drawToast();
    void drawDropOverlay();
    void drawLoading(const Game& g);
    gfx::Rect R(float x, float y, float w, float h) const { return {x * s_, y * s_, w * s_, h * s_}; }
    float text(gfx::FontId f, float px, float x, float y, const std::string& t, gfx::Color c, float tracking = 0);
    float measure(gfx::FontId f, float px, const std::string& t, float tracking = 0);
    static gfx::Color fretColor(Fret f);

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    SDL_Surface* icon_ = nullptr;
    gfx::Renderer gfx_;
    Catalog catalog_;
    ActionLog log_;
    bool running_ = true;

    int pw_ = 0, ph_ = 0;      // window pixels
    float s_ = 1, W_ = 1280, H_ = 720;  // scale and design size
    double now_ = 0;           // seconds since start, at the frame's start
    Uint64 startNs_ = 0;
    int focusRow_ = 0, focusCol_ = kPlay;
    float scroll_ = 0, scrollTarget_ = 0;
    Dialog dialog_;
    std::string toast_;
    double toastAt_ = -100;
    bool dragging_ = false;
    std::vector<Hit> hits_;
    bool mouseUsed_ = false;

    std::map<SDL_JoystickID, SDL_Gamepad*> pads_;
    int heldDir_ = 0;  // gamepad direction held: 1 up, 2 down, 3 left, 4 right
    double repeatAt_ = 0;

    // The system file dialog answers on its own thread.
    std::mutex pickedLock_;
    std::vector<std::pair<std::string, std::string>> picked_;  // (game id, path or "" for cancelled)

    std::vector<ScriptStep> script_;
    size_t scriptPos_ = 0;
    std::string pendingShot_;
};
