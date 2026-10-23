// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "script/script.h"

#include <array>

class GameData;
class Panel;
struct TextObj;
class Renderer;

// What the native panel classes need from the UI system.
class SelectHost {
public:
    virtual ~SelectHost() = default;
    virtual Renderer& renderer() = 0;
    virtual const GameData& data() = 0;
    // Sends a message through the UIManager chain (screen, then focus panel).
    virtual void route(const std::string& message, const std::map<std::string, Value>& vars) = 0;
    virtual bool isSelectButton(int button) = 0;
    virtual bool isBackButton(int button) = 0;
    // PlayerConfig(p) fields.
    virtual std::string playerCharacter(int p) = 0;
    virtual void setPlayerCharacter(int p, const std::string& character) = 0;
    virtual std::string playerGuitar(int p) = 0;
    virtual std::string playerSkin(int p) = 0;
    // campaign unlock test (FUN_00137b28) at any difficulty.
    virtual bool unlocked(const std::string& item) = 0;
    // Unlocked at any difficulty (FUN_00138240: quickplay, multiplayer).
    virtual bool unlockedAny(const std::string& item) = 0;
    // The focus panel's focused component and the current screen, for GH_SELECT_MSG / SCROLL_MSG.
    virtual Value focusComponent() = 0;
    virtual Value screenValue() = 0;
    // Sends a script message to a named object.
    virtual Value sendTo(const std::string& object, const std::string& msg, std::vector<Value> args) = 0;
    virtual std::string localize(const std::string& token) = 0;
    // The playing player's state for the HUD (false when no song is playing).
    struct PlayerState {
        int score = 0, multiplier = 1, streak = 0;
        bool starPowerActive = false;
        float rating = 0.5f, energy = 0;
    };
    virtual bool playerState(int p, PlayerState& out) = 0;
    // The HUD's displayed Star Power level (deploying needs it >= ready_level).
    virtual void starMeterShown(int p, float level) = 0;
    // store.dta prices: one item, or the lowest / highest of a category.
    virtual int storePrice(const std::string& category, const std::string& item) = 0;
    virtual int storePriceRange(const std::string& category, bool highest) = 0;
    // FadePanel fade_synth: the SPU master volume (1 = full).
    virtual void setMasterVolume(float v) = 0;
    // track_panel set_frame: the extend_track.tnm frame of the highway camera.
    virtual void trackFrame(float frame) = 0;
    // track_panel pop_smasher / set_smasher_glowing / reset / explode, for the engine's highway.
    virtual void trackMessage(const std::string& msg, const std::vector<Value>& args) { (void)msg, (void)args; }
    // Text in a game font (resources.rnd_ps2) on the 1280 x 720 menu screen:
    // `x`, `y` the anchor, `height` the line height in pixels, `align` as
    // TextStyle (0x11 top left ... 0x44 bottom right).
    virtual void drawText(const std::string& font, const std::string& text, float x, float y, float height,
                          const std::array<float, 4>& rgba, uint32_t align) {
        (void)font, (void)text, (void)x, (void)y, (void)height, (void)rgba, (void)align;
    }
    // Tutorials (TutorialPanel): sound, script funcs, the overlay scene, the pad, the VO stream.
    virtual class Synth* synth() { return nullptr; }
    virtual Value callFunc(const std::string& func, std::vector<Value> args) { (void)func, (void)args; return Value::integer(0); }
    virtual class Panel* panelScene(const std::string& panel) { (void)panel; return nullptr; }
    // Player 0's held frets (bits 0..4) and strum bar as levels.
    virtual void padState(uint32_t& frets, bool& strum) { frets = 0, strum = false; }
    virtual void voicePlay(const std::string& path, float volume) { (void)path, (void)volume; }
    virtual bool voicePlaying() { return false; }
    virtual void voiceStop() {}
    virtual void voicePause(bool paused) { (void)paused; }
    virtual void setTutorialRunning(bool on) { (void)on; }
    // Credits: a resources text template, a font's material colour, the credits music.
    virtual const TextObj* textTemplate(const std::string& name) { (void)name; return nullptr; }
    virtual std::array<float, 4> fontColor(const std::string& font) { (void)font; return {1, 1, 1, 1}; }
    virtual void creditsMusic(bool on) { (void)on; }
    // game multiplayer (the face-off).
    virtual bool multiplayer() const { return false; }
    // The port's Enhanced Settings (saved apart from the career).
    virtual bool enhancedSetting(const std::string& key) { (void)key; return false; }
    virtual void setEnhancedSetting(const std::string& key, bool on) { (void)key, (void)on; }
    virtual std::string enhancedValue(const std::string& key) { (void)key; return ""; }
    virtual void setEnhancedValue(const std::string& key, const std::string& v) { (void)key, (void)v; }
    // Enhanced Settings' user files: opens the export / load dialog; the last result to show.
    virtual void userFiles(const std::string& action) { (void)action; }
    // Enhanced Settings' backdrop (the still venue close-up): wanted while the
    // screen is up; drawMenuBackdrop draws it now, false when there is none.
    virtual void menuBackdrop(bool on) { (void)on; }
    // `focusX` / `focusY`: where the backdrop's subject (the microphone) is, in menu units.
    virtual bool drawMenuBackdrop(float& focusX, float& focusY) { (void)focusX, (void)focusY; return false; }
    virtual std::string userFilesStatus() { return ""; }
    // drawText's width for `text` at line height `height`.
    virtual float textWidth(const std::string& font, const std::string& text, float height) {
        (void)font, (void)text, (void)height;
        return 0;
    }
    // The screen transition (native pages animate with it): phase 0 idle,
    // 1 the screen leaving, 2 entering; t 0..1 through the phase; going back.
    virtual void transition(int& phase, float& t, bool& back) { phase = 0, t = 1, back = false; }
};

// The C++ side of a panel class the scripts create with a verb of their own
// (docs/select_panels.md): CharsysPanel, GuitarDisplayPanel,
// GuitarSelectPanel, MultiCharSelPanel. The generic panel (scene, script,
// navigator) calls into it.
class NativePanel {
public:
    virtual ~NativePanel() = default;
    // Native messages; false when not one of the class's.
    virtual bool handle(const std::string& msg, std::vector<Value>& args, Value& result) {
        (void)msg, (void)args, (void)result;
        return false;
    }
    // button_down from the UIPanel Handle chain; true when handled.
    virtual bool buttonDown(int button, int player) {
        (void)button, (void)player;
        return false;
    }
    virtual void enter() {}    // before the script's enter
    virtual void entered() {}  // after it
    virtual void exit() {}
    virtual void poll(float dtMs) { (void)dtMs; }
    virtual void setPaused(bool paused) { (void)paused; }
    // Mouse (Enhanced: mouse support), in 1280 x 720 menu units; true when handled.
    virtual bool mouse(float x, float y, bool click) { (void)x, (void)y, (void)click; return false; }
    virtual void drawBefore() {}  // under the panel's own scene
    virtual void drawAfter() {}   // over it
    virtual bool drawsScene() const { return true; }  // false: the panel's scene is drawn elsewhere
    void setScene(Panel* scene) { scene_ = scene; }

protected:
    Panel* scene_ = nullptr;
};

// The in-game panels (game_panels.cpp): new_hud_panel, new_track_panel.
std::unique_ptr<NativePanel> makeGamePanel(const std::string& verb, SelectHost& host, const std::map<std::string, Value>& props);

// new_tutorial_panel (tutorial_panel.cpp).
std::unique_ptr<NativePanel> makeTutorialPanel(SelectHost& host);

// The native part for a panel verb (new_charsys_panel, ...), or null.
std::unique_ptr<NativePanel> makeNativePanel(const std::string& verb, const std::string& name, SelectHost& host,
                                             const std::map<std::string, Value>& props);
