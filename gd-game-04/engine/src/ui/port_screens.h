// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "input/input.h"
#include "ui/port_draw.h"
#include "ui/port_settings.h"

class Canvas;
class FrontEnd;

// The port's own screens (not in the original game; guidelines/
// universal_launcher.md "GAMEPADS screen, Enhanced menu and mouse"): the
// GAMEPADS screen (ui/port_gamepads.cpp) and Enhanced Options
// (ui/port_enhanced.cpp). GH3's menus are QScript screen elements; these are
// native pages the app draws instead of them while one is open (GH3's
// scripts are paused meanwhile). Reached from GH3's own screens through
// items the port adds to their help bars (drawItems):
//   main_menu_fs: the yellow fret / Y button, or a click on "GAMEPADS";
//   options_select_option_fs: the blue fret / X button, or a click on
//   "ENHANCED" (agents/DECISIONS.md D-entry "Enhanced Options' entry").
// Back closes a screen, back to the one it came from.

// What a screen reaches: the input layer, GH3's fonts (UiSystem), its sound
// events (FrontEnd), the Enhanced settings and the screens' stack.
class PortScreens;
struct PortContext {
    Input& input;
    FrontEnd& fe;
    EnhancedStore& store;
    PortScreens& screens;
    // A change of an Enhanced setting, applied by the app at once.
    std::function<void(const std::string& key)> changed;
    // gd01's sound names: toggle (a move), select, back, error.
    void sound(const char* what) const;
};

// A screen's buttons: the menu's own (gd01's kPad_* roles).
enum class PortButton { Up, Down, Left, Right, Select, Back };

class PortScreen {
public:
    virtual ~PortScreen() = default;
    virtual void enter() {}
    virtual void exit() {}
    // false: not taken (Back then closes the screen).
    virtual bool button(PortButton b) = 0;
    // (x, y) in menu units; `click` a left click, else a hover. true: on something.
    virtual bool mouse(float x, float y, bool click) = 0;
    virtual void update(double dtMs) { (void)dtMs; }
    virtual void draw(PortDraw& d) = 0;
};

std::unique_ptr<PortScreen> makeGamepadScreen(PortContext& ctx);
std::unique_ptr<PortScreen> makeEnhancedScreen(PortContext& ctx);
// For walks: the centre of an Enhanced setting's row (false: not shown), and
// of the open dropdown's option k.
bool enhancedRowCenter(PortScreen& enhanced, const std::string& key, float& x, float& y);
bool enhancedOptionCenter(PortScreen& enhanced, int k, float& x, float& y);

class PortScreens {
public:
    enum class Id { Gamepads, Enhanced };
    PortScreens(Input& input, FrontEnd& fe, EnhancedStore& store, std::function<void(const std::string&)> changed);
    ~PortScreens();

    void open(Id id);
    void back();
    bool isOpen() const { return !stack_.empty(); }
    // The open screen as walks name it: "port_gamepads", "port_enhanced", "".
    std::string current() const;
    PortScreen* screen(Id id) const;

    // An action from the input layer while a screen is open.
    void handle(const InputEvent& e);
    // The mouse while a screen is open: button 0 hover, 1 left click, 3 right click (Back).
    void mouse(float x, float y, int button);
    void update(double dtMs);
    void draw(Canvas& c);

    // The port's items on GH3's own help bars (main menu: GAMEPADS;
    // options: ENHANCED), drawn over GH3's screen; `flowState` is GH3's.
    void drawItems(Canvas& c, const std::string& flowState);
    // The item under (x, y) on that screen: "gamepads", "enhanced" or "".
    std::string itemAt(float x, float y, const std::string& flowState) const;
    // The item's bounds (walks click it).
    bool itemRect(const std::string& flowState, float& x0, float& y0, float& x1, float& y1) const;

    PortContext& context() { return ctx_; }

private:
    // The help bar's item k (0 select, 1 back, 2 up / down) on the port's screens.
    void helpRect(int k, float& x0, float& y0, float& x1, float& y1) const;
    PortContext ctx_;
    std::unique_ptr<PortScreen> gamepads_, enhanced_;
    std::vector<Id> stack_;
    double transMs_ = 1e9;  // since the last screen change
    bool transBack_ = false;
};
