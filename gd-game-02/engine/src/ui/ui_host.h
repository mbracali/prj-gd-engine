// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

class BandPerformer;
class Player;
struct SongInfo;
class ScriptObject;
struct Value;

// What the UI (ui/ui_manager.h) asks of the program: the song code (app.cpp
// keeps GH2's track, HUD, player and mixer), audio and videos. GH2's
// GamePanel / TrackPanel / HudPanel scripts call these through their
// native methods.
class UiHost {
public:
    virtual ~UiHost() = default;

    virtual const std::vector<SongInfo>& songs() const = 0;

    // GamePanel enter: loads the song and gets the track and HUD ready (the
    // song clock waits at the lead-in until gameBegin). False with `error`
    // when the song cannot be loaded.
    struct GameSetup {
        std::string song;
        int difficulty = 1;     // 0 Easy .. 3 Expert
        bool autoplay = false;  // gamecfg force_autoplay (attract mode)
        bool lefty = false;
        int syncOffsetMs = 0;   // options sync_offset
        float bandVolume = 1, guitarVolume = 1;
        bool fastIntro = false;  // game set fast_intro (a restart skips most of the lead-in)
        // The players' look (PlayerConfig: outfit, guitar, skin; empty: the song's quickplay one).
        std::array<std::string, 2> outfit, guitar, skin;
        // GamePanel's encore state (0x822CA6E0 / 0x822CA5D0): the song may
        // unlock an encore (the crowd calls for it: game_won_msg $encore,
        // encore_lost, the band's encore spot), the encore itself plays (the
        // encore intro, the crowd full); the career's last song.
        bool encoreCheck = false, encoreFx = false, campaignFinal = false;
        // The mode (gamecfg mode, config/modes.dta): quickplay, career,
        // attract, multi_coop (co-op), multi_vs (face-off: each player owns
        // the face-off sections' gems), multi_fo (pro face-off: both play
        // every gem) [C: docs/multiplayer.md]; its num_players.
        std::string mode = "quickplay";
        int numPlayers = 1;
        // Per player: the difficulty (player 0's is `difficulty`), the part
        // played (song_provider get_instrument: guitar, bass or rhythm; the
        // co-op lead plays PART GUITAR COOP when the chart has it), lefty,
        // and the pad (input slot) that plays it (gamecfg set_player).
        std::array<int, 2> difficulties{1, 1};
        std::array<std::string, 2> parts{"guitar", "guitar"};
        std::array<bool, 2> leftys{false, false};
        std::array<int, 2> pads{0, 1};
    };
    virtual bool gameLoad(const GameSetup& setup, std::string& error) = 0;
    // Starts the music and the clock (after every game panel has entered).
    virtual void gameBegin() = 0;
    // GamePanel exit: stops the song.
    virtual void gameEnd() = 0;
    virtual bool gameLoaded() const = 0;
    virtual void gameSetPaused(bool paused) = 0;
    virtual bool gamePaused() const = 0;
    virtual double gameTimeMs() const = 0;     // the song clock (negative in the lead-in)
    // The sounds played since the song began, with their song times (the
    // first 256; --walktest checks the intro's).
    virtual std::vector<std::pair<std::string, double>> gameSfx() const { return {}; }
    // The camera's distance travelled between the intro's start and song
    // time -2000 ms (the INTRO shot moves in every venue), -1 unknown.
    virtual double introCameraTravel() const { return -1; }
    virtual float gameBeat() const = 0;        // the song position in beats
    virtual float songDurationMs() const = 0;  // to the song's end
    virtual const Player* gamePlayer(int index) const = 0;
    // The co-op band (game/band_performer.h): its score, streak, stars and
    // meter; nullptr outside co-op.
    virtual const BandPerformer* gameBand() const { return nullptr; }
    // Counts the program keeps per player: "sp_phrases" (Star Power phrases
    // captured), "streaks_broken", "gems" (the chart's gem count).
    virtual int gameCounter(int player, const std::string& key) const = 0;
    // beatmatch set_music_speed / set_paused (the failed song winding down).
    virtual void setMusicSpeed(float speed) = 0;
    virtual void setMusicPaused(bool paused) = 0;

    // GH2's track_panel and hud panels: the track and HUD the program draws
    // (render/track_view.h, render/hud_view.h).
    virtual void trackCommand(const std::string& msg, const std::vector<Value>& args) = 0;
    virtual void hudCommand(const std::string& msg, const std::vector<Value>& args) = 0;
    virtual void drawTrack() = 0;
    virtual void drawHud() = 0;

    // GH2's world_panel: the venue (game/world.h, render/venue_view.h).
    // worldLoad gets the panel's milo (world/<v>/gen/<v>[_mp|_coop].milo_xbox)
    // when it loads, worldEnter comes as it enters (before its script),
    // worldDraw where it draws, worldUnload when it unloads. worldScript is
    // the `world` script object while a venue is up (nullptr otherwise).
    virtual void worldLoad(const std::string& /*path*/) {}
    virtual void worldEnter() {}
    virtual void worldDraw() {}
    virtual void worldUnload() {}
    virtual ScriptObject* worldScript() { return nullptr; }

    // GH2's Synth (audio/synth.h): plays the bank sound `name` (the game's
    // banks of config/sound.dta, then the venue's); false when none has it.
    virtual bool playSfx(const std::string& /*name*/) { return false; }
    // The same with the caller's volume (dB), pan and transpose (ui/sfx_args.h).
    virtual bool playSfxArgs(const std::string& name, float /*volumeDb*/, float /*pan*/, float /*transpose*/) { return playSfx(name); }
    // `synth set_master_vol` / `master_vol` (dB; audio/synth.h).
    virtual void setSfxMasterDb(float /*db*/) {}
    virtual float sfxMasterDb() { return 0; }
    virtual void stopSfx(const std::string& /*name*/) {}
    virtual void stopAllSfx() {}
    virtual void pauseAllSfx(bool /*paused*/) {}
    // Sound effect voices playing and not paused (-1: no sound player); tests.
    virtual int sfxLiveVoices() { return -1; }

    // song_preview: loops the song's (preview a b) section; "" stops it.
    virtual void songPreview(const std::string& songId) = 0;
    // meta_music start / stop: the menus' music loops (config/synth.dta metamusic).
    virtual void menuMusic(bool on) = 0;
    // The credits' music (sfx/streams/credits.mogg, CreditsPanel 0x8229B310).
    virtual void creditsMusic(bool on) = 0;
    // meta play_movie: plays videos/<name> modally.
    virtual void playMovie(const std::string& name) = 0;
    // The options' volumes (0..1) for the songs, and lefty per player.
    virtual void applyOptions(float bandVolume, float guitarVolume, float fxVolume, bool lefty0, bool lefty1, int syncOffsetMs) = 0;
    // The port's Enhanced Options (ui/enhanced_panel.h): a setting's value
    // (enhanced.json in the save folder; its default when unset) and a
    // change, saved and put in effect at once.
    virtual std::string enhancedValue(const std::string& /*key*/) { return {}; }
    // The last frame drawn as a .bmp (the walk test's shots); false if none.
    virtual bool saveScreenshot(const std::string& /*path*/) { return false; }
    virtual void setEnhancedValue(const std::string& /*key*/, const std::string& /*value*/) {}
    // An Enhanced Options button: steam_deck (the Steam Deck profile, once),
    // export (a folder dialog), load_settings / load_progress (a file
    // dialog). enhancedStatus: the last one's result ("" none), shown a few
    // seconds by the panel.
    virtual void enhancedAction(const std::string& /*action*/) {}
    virtual std::string enhancedStatus() { return {}; }
    // What the Enhanced Options put in effect, for the walk tests (-1:
    // unknown): "particles", "shake", "touch", "rockband", "xinput",
    // "sharp_ui", "post_original", "post_dof", "post_key", "fx_frames",
    // "fx:<pass>" (the port's chain ran that pass last frame), "shadows",
    // "beams", "motes" (the venue extras last frame), "calibration_ms".
    virtual int enhancedState(const std::string& /*key*/) { return -1; }
    // The Enhanced Options' backdrop: a still shot of the second venue with
    // the band posed, while the screen is up; drawMenuBackdrop draws it
    // (false when there is none).
    virtual void menuBackdrop(bool /*on*/) {}
    virtual bool drawMenuBackdrop() { return false; }
    // The backdrop's band is posed and its singer framed on the right (tests).
    virtual bool menuBackdropFramed() { return false; }
    virtual void quit() = 0;
};
