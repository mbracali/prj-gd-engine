// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "render/ui_view.h"
#include "script/script.h"
#include "ui/ui_manager.h"

namespace scene {
class Object;
}

// The script objects of the UI (ui_manager.h): panels (UIPanel / PanelDir),
// screens (GHScreen), the components of a panel's milo, and the classes'
// factories (script_classes.cpp).
namespace ui {

class PanelObj;

// One object of a panel's milo, as scripts see it ({tip.lbl set_text ...},
// {ss_song.lst selected_pos}, {splash.view animate ...}). Components hold
// the state their class keeps (list selection and provider, slider
// position, check box, text entry).
class ComponentObj : public ScriptObject {
public:
    ComponentObj(PanelObj* panel, const scene::Object* object, const std::string& name);
    PanelObj* panel;
    const scene::Object* object;  // nullptr: not in the milo (logged once)
    bool directory = false;       // the panel's milo itself (a RndDir: its animatables)

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;
    Value getProp(Interp& in, const std::string& key) override;
    void setProp(Interp& in, const std::string& key, const Value& v) override;

    bool isButton() const;
    bool isList() const;
    bool isSlider() const;
    bool isCheck() const;
    bool isTextEntry() const;
    scene::UiState state() const;
    void setState(scene::UiState s);

    // UIList
    ScriptObject* provider = nullptr;
    int selected = 0;  // provider data index
    void refreshList();
    // UISlider
    std::vector<std::string> faders;  // an Sfx's faders (add_fader)
    int current = 0, steps = 10;
    bool sliderSelected = false;
    // CheckBox
    bool checked = false;
    // BandTextEntry (0x821433B0)
    std::string entered;
    int charIndex = 0;
    bool done = false;
    std::string charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!'. ";
    int maxLength = 8;
    void initTextEntry();
    void updateTextEntry();
    // Handles a button for the focused component (true: handled).
    bool buttonDown(int button, int player);

private:
    void setText(const std::string& s);
};

// A UIPanel (GHPanel, UIPanel and the native panel classes): the milo
// named by its (file ...) (evaluated when loaded) drawn by gfx::UiView, its
// lifecycle (load enter exit poll finish_load unload) and PanelDir's
// focus, navigation and selection.
class PanelObj : public ScriptObject {
public:
    PanelObj(UiManager& ui, const std::string& name, const std::string& cls);
    UiManager& ui;
    std::unique_ptr<gfx::UiView> view;
    std::string path;  // the loaded milo
    bool loaded = false, up = false, paused = false, showing = true;
    std::string focus, lastFocus;
    std::set<std::string> disabled;
    // Selection in progress (select_frames): the component and when it ends.
    std::string selecting;
    double selectEndMs = 0;

    // The component `name` (a milo object, or a known component the panel
    // keeps state for); nullptr when the panel has no such object.
    virtual ComponentObj* component(const std::string& name);
    bool acceptsAnyName() const { return anyComponent_; }
    ComponentObj* focused() { return focus.empty() ? nullptr : component(focus); }
    void setFocus(const std::string& name, bool notify);
    void enable(const std::string& name);
    void disable(const std::string& name, int navButton);
    bool isDisabled(const std::string& name) const { return disabled.count(name) != 0; }
    // {panel enter} / {panel exit} from a script: UIPanel's Enter / Exit
    // (the panel's enter / exit below, which call its script handlers):
    // tutorials.dta's {game exit} ... {game enter} reloads the song,
    // endgame.dta's {unlock_guitar_panel exit} {... enter} the next award
    // [I: the dispatch order; the scripts expect the panel reloaded].
    bool nativeFirst(const std::string& msg) const override { return msg == "enter" || msg == "exit"; }

    // The milo to load: the (file ...) value, evaluated ("" for none).
    virtual std::string fileName();
    virtual void load();
    virtual void enter();
    virtual void exit();
    virtual void poll(double dtMs);
    virtual void unload();
    // PanelDir's button_down: the focused component, then navigation.
    virtual bool buttonDown(int button, int player);
    virtual bool buttonUp(int button, int player);
    // Draws the panel (its view through its camera).
    virtual void draw();
    virtual bool drawsSomething() const { return view != nullptr; }
    // Mouse support (UiManager::mouse): the panel's components under (x, y)
    // (menu units) from its view's last draw: hovering focuses, a click
    // selects; the port's own panels test their rows. True when hit.
    virtual bool mouse(float x, float y, bool click);
    // The help bar's items (HelpBarPanel); false for the others.
    virtual bool helpMouse(float /*x*/, float /*y*/, bool /*click*/) { return false; }
    // Joypad repeat overrides ((joypad (hold_ms) (repeat_ms))).
    bool repeatTiming(float& holdMs, float& repeatMs) const;

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;
    Value getProp(Interp& in, const std::string& key) override;

    // The focus can move (up / down or left / right), or the panel moves
    // its own (the port's GAMEPADS and Enhanced Options): the help bar's UP /
    // DOWN access button (universal_mechanisms.md 1.1.3).
    bool navigable();

    void selectStart(ComponentObj* c, int button, int player);
    void pollSelect();
    void navigate(int button);
    void applyStates();

protected:
    std::map<std::string, std::unique_ptr<ComponentObj>> components_;
    // The panel's milo is the program's (the HUD, the track): any name is
    // one of its components, kept as a name only.
    bool anyComponent_ = false;
};

// A GHScreen (UIScreen): its panels, focus panel and flags; back screen.
class ScreenObj : public ScriptObject {
public:
    ScreenObj(UiManager& ui, const std::string& name, const std::string& cls);
    UiManager& ui;
    ScreenObj* back = nullptr;
    bool backSet = false;
    PanelObj* focusOverride = nullptr;  // set_focus_panel (cleared as the screen enters)
    // GHScreen's black overlay (PS2 draw 0x00146198) [C]: `ambient` its alpha
    // (+0x68: reset_ambient 0, turn_off_ambient 1); an animated exit fades
    // it 0 -> 1 and an animated entrance 1 -> 0 over 0.5 s (0x00145e50:
    // +0x60 start, +0x50 end, +0x64 entrance), then it keeps the end value.
    float ambient = 0;
    double fadeStartMs = -1;  // -1: no fade (+0x60 = 1e30)
    bool fadeIn = false;
    std::vector<PanelObj*> panels() const;
    PanelObj* focusPanel() const;
    bool flag(const std::string& key, bool def) const;
    bool hasPanel(const PanelObj* p) const;
    bool inBackChain(const ScreenObj* s) const;  // 0x8229E8E0
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;
    // GHScreen's button_down (0x8229EED0): the auto back on kPad_Tri.
    virtual bool nativeButtonDown(int button, int player);
    // The screen class's own handling of a routed message (UIManager
    // Handle), before the screen's script as Milo's natives come first
    // (MultiSelectScreen 0x822978C0); false: not handled.
    virtual bool routeNative(const std::string& /*msg*/, std::vector<Value>& /*args*/, Value& /*r*/) { return false; }
    // The screen class's Enter, before its panels enter (MultiSelectScreen
    // 0x82296EF8), and once they have, before the screen's script.
    virtual void nativeEnter() {}
    virtual void nativeEntered() {}
};

// The UIPanel class factories for `new` (script_classes.cpp).
void registerUiClasses(UiManager& ui, Interp& interp);

}  // namespace ui
