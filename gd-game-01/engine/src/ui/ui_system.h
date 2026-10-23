// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "script/script.h"
#include "ui/panel.h"
#include "ui/select_panels.h"
#include "ui/ui_resources.h"

class GameData;
struct SongInfo;

// The original menus (docs/ui_system.md): ghui/ui.dta defines screens and
// panels through new_gh_screen / new_gh_panel / ...; the UI manager (`ui`)
// switches screens with transitions, routes buttons through the screen, its
// focus panel's navigator and script, and the focused component; native
// objects (`game`, `campaign`, `meta`, ...) bridge to the engine.
class UiSystem : private SelectHost {
public:
    // PS2 pad buttons (kPad_*), as the scripts see them.
    enum Button { L2 = 0, R2 = 1, L1 = 2, R1 = 3, Triangle = 4, Circle = 5, Cross = 6, Square = 7, Select = 8,
                  Start = 11, Up = 12, Right = 13, Down = 14, Left = 15 };

    struct Hooks {
        std::function<const std::vector<SongInfo>&()> songs;
        // The game screen came up: play song `index` at `difficulty` (0..3).
        // Career passes the venue, character and guitar (skin); empty = the song's quickplay setup.
        std::function<void(int index, int difficulty, const std::string& venue, const std::string& character,
                           const std::string& guitar)> startGame;
        std::function<void()> quit;
        // song_preview (FUN_00151fd8): loop the song's preview window ("" stops).
        std::function<void(const std::string& songId)> songPreview;
        // meta_music_start / meta_music_stop (config/synth.dta metamusic).
        std::function<void(bool on)> menuMusic;
        // meta play_movie <file> (videos/<file>, MPEG-2 .pss): plays it, the UI frozen meanwhile.
        std::function<void(const std::string& file)> playMovie;
        // The meta panel came up from outside the menus (MetaPanel Load,
        // FUN_0020dcd8 with autostart): pick a new loop and start it.
        std::function<void()> menuMusicLoad;
        // game set_paused; leaving the in-game screens for the menus.
        std::function<void(bool paused)> setPaused;
        std::function<void()> leaveGame;
        // player0::player <stat>: score, percent_complete, notes_hit, streak ...
        std::function<int(const std::string& stat, int player)> playerStat;
        // player0::player setters (Player_v1): true when handled.
        std::function<bool(const std::string& msg, const std::vector<Value>& args)> playerCommand;
        // player0::player_matcher set_volume: the guitar track's volume.
        std::function<void(float)> guitarVolume;
        std::function<void(bool)> autoPlay;  // player_matcher set_auto_play (the attract mode)
        // The HUD's view of the player, and its displayed Star Power level.
        std::function<bool(int p, SelectHost::PlayerState& out)> playerState;
        std::function<void(int p, float level)> starMeterShown;
        std::function<void(float frame)> trackFrame;
        std::function<void(const std::string&, const std::vector<Value>&)> trackMessage;
        std::function<void(float)> masterVolume;  // FadePanel fade_synth
        std::function<void(bool)> depthOfField;     // arena set_depth_of_field
        std::function<void()> guitaristStarPower;  // GuitarCharacter start_starpower
        // The save file as the memory card: load / save the whole campaign; false on failure / no file.
        std::function<bool()> loadCampaign, saveCampaignFile, saveExists;
        class Synth* synth = nullptr;  // sound effects (play_sfx, synth play_sequence)
        class CrowdAudio* crowd = nullptr;
        class Campaign* campaign = nullptr;          // career (null: everything unlocked)
        std::function<void()> saveCampaign;          // autosave
        std::function<int(int score)> starsOf;       // num_stars of the current song
        std::function<float()> msPerTick;          // song tempo now (arena delay_task)
        std::function<void(float)> musicSpeed;     // beatmatch set_music_speed
        std::function<void(bool)> musicPaused;     // beatmatch set_paused
        // The credits' music (sfx/streams/credits.vgs): on fades in, off fades out.
        std::function<void(bool on)> creditsMusic;
        // Pad slot `slot` is connected (the keyboard counts for its slot).
        std::function<bool(int slot)> padConnected;
        // Tutorials: load a song (engine index) keeping the venue; start it at
        // 0 (intro skipped / start paused); pause the song clock only.
        std::function<void(int index)> tutorialLoad;
        std::function<void(bool skipIntro, bool startPaused)> tutorialStart;
        std::function<void(bool)> setTimePaused;
        // An object of the playing venue's scenes as a script object (set_showing), or null.
        std::function<ScriptObject*(const std::string&)> venueObject;
        // Tutorials: player 0's pad levels and the voice-over stream.
        std::function<void(uint32_t& frets, bool& strum)> padState;
        std::function<void(const std::string& path, float volume)> voicePlay;
        std::function<bool()> voicePlaying;
        std::function<void()> voiceStop;
        std::function<void(bool)> voicePause;
        // The port's Enhanced Settings (on / off switches, saved by the app).
        std::function<bool(const std::string&)> enhancedSetting;
        std::function<void(const std::string&, bool)> setEnhancedSetting;
        std::function<std::string(const std::string&)> enhancedValue;
        std::function<void(const std::string&, const std::string&)> setEnhancedValue;
        std::function<void(const std::string& action)> userFiles;  // "export", "load_settings", "load_progress"
        std::function<std::string()> userFilesStatus;
        std::function<void(bool)> menuBackdrop;
        class Input* input = nullptr;  // the Gamepads screen's controllers
        std::function<bool(float&, float&)> drawMenuBackdrop;
    };
    // Sends a gameplay event to a script object: arena (arena_game.dta),
    // player_matcher_handlers (beatmatch.dta), star_power_handlers (player.dta).
    void gameEvent(const std::string& object, const std::string& msg, std::vector<Value> args = {});
    // Drops pending game / arena tasks (a song restart).
    void resetGameTasks();
    // Sends a script message to a panel (e.g. the `game` panel's intro_start).
    void sendPanel(const std::string& panel, const std::string& msg, std::vector<Value> args = {});

    UiSystem(Renderer& renderer, const GameData& data, Hooks hooks);
    ~UiSystem();

    void gotoScreen(const std::string& screen);
    // Starts the UI: the boot logos (unless skipped), then $first_screen.
    void boot(bool skipLogos);
    // Runs `fn` after `ms` of UI time.
    void later(float ms, std::function<void()> fn);
    const std::string& currentScreen() const;
    // The current screen shows the game (GAME_PANELS: game, pause, lose, endgame...).
    bool inGame() const;
    // arena set_showing / showing (the No Venue cheat).
    bool arenaShowing();
    void setArenaShowing(bool on);
    // track_panel set_showing (hidden in the attract mode).
    bool trackShowing() const;
    void setTrackShowing(bool on);
    // The endgame newspaper photo of the player's character (FUN_00104fd0).
    void loadWinnerPhoto(Panel& panel);
    // tutorial set_tutorial_running (a tutorial is being played).
    bool tutorialRunning() const { return tutorialRunning_; }
    void update(float dtMs);
    void draw();
    // Text in a game font on the menu screen (SelectHost; also the FPS counter).
    void drawText(const std::string& font, const std::string& text, float x, float y, float height,
                  const std::array<float, 4>& rgba, uint32_t align) override;
    // Mouse support (the port's): hovering a menu entry focuses it like the
    // pad would, a click selects it; CONTINUE / BACK in the help bar press
    // those buttons. `x`, `y` in 1280 x 720 menu units. True when over something.
    bool mouse(float x, float y, bool click);
    // The mouse wheel: one Up (-1) or Down (+1) press on the focused panel.
    void wheel(int dir);
    // `player`: the pad slot (player_num in the scripts).
    void buttonDown(int button, bool guitar, int player = 0);
    void buttonUp(int button, int player = 0);
    // game set_multiplayer (the face-off setup).
    bool multiplayer() const override;
    // PlayerConfig(p): the character and the guitar (skin) chosen.
    void playerSetup(int p, std::string& character, std::string& guitar) const;

    Interp& interp() { return interp_; }

    class PanelObj;
    class ScreenObj;
    class ComponentObj;

private:
    friend class PanelObj;
    friend class ScreenObj;
    friend class ComponentObj;
    class ManagerObj;
    class GameObj;
    class StubObj;
    class BeatmatchObj;
    class MemcardObj;
    std::vector<std::pair<float, std::function<void()>>> timed_;
    std::vector<std::unique_ptr<Panel>> logos_;
    float logoMs_ = 0;
    bool updateBoot(float dtMs);
    struct InterpTask {
        std::string var;
        float ms, elapsed, from, to;
        std::vector<const DataNode*> body;
    };
    std::vector<InterpTask> interpTasks_;
    struct TextCache {
        int mesh = 0;
        float width = 0, height = 0;
        uint64_t used = 0;
    };
    std::map<std::string, TextCache> textCache_;  // drawText meshes by font|text
    uint64_t textFrame_ = 0;
    bool tutorialRunning_ = false;
    std::vector<std::string> multiOnlyVenues_;
    int gameDifficulty() const;
    std::string gameVenue() const;  // arena/venues.dta (multi_only TRUE)
    class ProviderObj;

    // SelectHost (the native panels' view of the UI).
    Renderer& renderer() override { return renderer_; }
    const GameData& data() override { return data_; }
    void route(const std::string& message, const std::map<std::string, Value>& vars) override;
    bool isSelectButton(int button) override;
    bool isBackButton(int button) override;
    std::string playerCharacter(int p) override;
    void setPlayerCharacter(int p, const std::string& character) override;
    std::string playerGuitar(int p) override;
    std::string playerSkin(int p) override;
    bool unlocked(const std::string& item) override;
    bool unlockedAny(const std::string& item) override;
    Value focusComponent() override;
    Value sendTo(const std::string& object, const std::string& msg, std::vector<Value> args) override {
        return interp_.send(object, msg, std::move(args));
    }
    std::string localize(const std::string& token) override { return res_->localize(token); }
    bool playerState(int p, PlayerState& out) override { return hooks_.playerState && hooks_.playerState(p, out); }
    int storePrice(const std::string& category, const std::string& item) override;
    int storePriceRange(const std::string& category, bool highest) override;
    void setMasterVolume(float v) override {
        if (hooks_.masterVolume) hooks_.masterVolume(v);
    }
    void trackFrame(float frame) override {
        if (hooks_.trackFrame) hooks_.trackFrame(frame);
    }
    void trackMessage(const std::string& msg, const std::vector<Value>& args) override {
        if (hooks_.trackMessage) hooks_.trackMessage(msg, args);
    }
    void starMeterShown(int p, float level) override {
        if (hooks_.starMeterShown) hooks_.starMeterShown(p, level);
    }
    class Synth* synth() override { return hooks_.synth; }
    Value callFunc(const std::string& func, std::vector<Value> args) override {
        return interp_.hasFunc(func) ? interp_.callFunc(func, std::move(args)) : Value::integer(0);
    }
    class Panel* panelScene(const std::string& panel) override;
    void padState(uint32_t& frets, bool& strum) override {
        frets = 0, strum = false;
        if (hooks_.padState) hooks_.padState(frets, strum);
    }
    void voicePlay(const std::string& path, float volume) override {
        if (hooks_.voicePlay) hooks_.voicePlay(path, volume);
    }
    bool voicePlaying() override { return hooks_.voicePlaying && hooks_.voicePlaying(); }
    void voiceStop() override {
        if (hooks_.voiceStop) hooks_.voiceStop();
    }
    void voicePause(bool paused) override {
        if (hooks_.voicePause) hooks_.voicePause(paused);
    }
    void setTutorialRunning(bool on) override { tutorialRunning_ = on; }
    const TextObj* textTemplate(const std::string& name) override { return res_ ? res_->textTemplate(name) : nullptr; }
    std::array<float, 4> fontColor(const std::string& font) override {
        return res_ ? res_->fontMatColor(font) : std::array<float, 4>{1, 1, 1, 1};
    }
    void creditsMusic(bool on) override {
        if (hooks_.creditsMusic) hooks_.creditsMusic(on);
    }
    bool enhancedSetting(const std::string& key) override { return hooks_.enhancedSetting && hooks_.enhancedSetting(key); }
    void setEnhancedSetting(const std::string& key, bool on) override {
        if (hooks_.setEnhancedSetting) hooks_.setEnhancedSetting(key, on);
    }
    std::string enhancedValue(const std::string& key) override { return hooks_.enhancedValue ? hooks_.enhancedValue(key) : ""; }
    void setEnhancedValue(const std::string& key, const std::string& v) override {
        if (hooks_.setEnhancedValue) hooks_.setEnhancedValue(key, v);
    }
    void userFiles(const std::string& action) override {
        if (hooks_.userFiles) hooks_.userFiles(action);
    }
    std::string userFilesStatus() override { return hooks_.userFilesStatus ? hooks_.userFilesStatus() : ""; }
    void menuBackdrop(bool on) override {
        if (hooks_.menuBackdrop) hooks_.menuBackdrop(on);
    }
    bool drawMenuBackdrop(float& focusX, float& focusY) override {
        return hooks_.drawMenuBackdrop && hooks_.drawMenuBackdrop(focusX, focusY);
    }
    float textWidth(const std::string& font, const std::string& text, float height) override;
    // The screen transition for native pages: phase 0 idle, 1 out, 2 in; t 0..1; going back.
    void transition(int& phase, float& t, bool& back) override;
    Value screenValue() override;

    // {<obj> anim_task <anim> ms from to}: plays an anim's frames over time.
    void animTask(const std::vector<Value>& args);
    struct AnimTask {
        Value target;
        float ms, elapsed, from, to;
    };
    std::vector<AnimTask> animTasks_;

    void installVerbs();
    void defineObjects(const DataNode& script);
    Value send(ScriptObject* o, const std::string& message, const std::map<std::string, Value>& vars, bool& handled);
    // The UIManager routing of a message (docs/ui_system.md 1.5): route() above.
    void enterScreen(ScreenObj* next);
    void exitScreen(ScreenObj* cur, ScreenObj* next);
    PanelObj* panel(const std::string& name) const;
    ScreenObj* screen(const std::string& name) const;
    PanelObj* focusPanel() const;

    Renderer& renderer_;
    const GameData& data_;
    Hooks hooks_;
    ScriptLoader loader_;
    Interp interp_;
    std::unique_ptr<UiResources> res_;
    std::vector<DataNode> scripts_;  // kept alive: handlers point into them
    std::map<std::string, std::unique_ptr<PanelObj>> panels_;
    std::map<std::string, std::unique_ptr<ScreenObj>> screens_;
    std::vector<std::unique_ptr<ScriptObject>> natives_;
    // Screen transitions (UIManager poll FUN_00209ec0, GHScreen anims
    // FUN_00143290): the old screen's out-anim, then the new one's in-anim.
    enum class Phase { Idle, Out, In };
    void startIn();
    void finishTransition();
    void applyTransitionAnims(float t);
    std::unique_ptr<ScriptObject> makeSceneProxy(Panel& panel, const std::string& name);
    Phase phase_ = Phase::Idle;
    float phaseMs_ = 0;
    bool forward_ = true, goingBack_ = false, entering_ = false;
    std::string pending_;  // goto_screen requested during a transition
    float holdUntilMs_ = 0;  // the loading screen stays up at least this long (frameMs_)
    ScreenObj* current_ = nullptr;
    ScreenObj* target_ = nullptr;
    ScreenObj* drawn_ = nullptr;
    ScreenObj* transitionFrom_ = nullptr;
    std::vector<ScreenObj*> stack_;
    std::unique_ptr<Panel> common_;  // ghui/common.gh
    std::map<std::string, std::unique_ptr<ScriptObject>> commonObjects_;
    float frameMs_ = 0;
    bool guitar_ = false;
    // Menu key repeat (JoypadClient, FUN_00221bd0): a held direction sends
    // button_down again after hold_ms, then every repeat_ms (gh.dta joypad
    // 1000 / 80; a panel's (joypad (hold_ms) (repeat_ms)) overrides it while
    // it is up, sel_song 500 / 80). Per player.
    struct HeldButton {
        int button = -1;
        float ms = 0;
        bool repeating = false;
    };
    std::array<HeldButton, 2> held_{};
    void repeatHeld(float dtMs);
    void repeatTiming(float& holdMs, float& repeatMs);
    std::string none_;
};
