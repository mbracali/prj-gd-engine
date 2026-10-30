// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace ui {
class UiManager;
}

// `gd03 --walktest <scenario> <disc>`: plays through GH2's own menus with
// scripted buttons, waiting for each screen, and checks what happened:
//   quickplay  boot (splashes skipped by a button, the intro skipped) -> any button -> create a
//              save -> guitar_help -> main -> Quick Play -> the song list,
//              down x3 -> confirm -> Easy -> loading -> the song (autoplay,
//              cut short) -> results -> continue -> high score name ->
//              complete -> the song list (autosave) -> back -> main;
//              career.json written with the score.
//   lose       main -> Quick Play -> a song that fails -> lose -> quit -> main
//              (a menu sound plays) -> Options -> Enhanced Options (its
//              backdrop shows the band) -> back -> back.
//   pause      main -> Quick Play -> the song -> pause -> Start resumes ->
//              pause -> restart -> pause -> quit -> the song list.
//   attract    the title screen idles 60 s -> attract mode -> a button ->
//              the title screen again.
//   back       main -> (past Multiplayer) Options -> Audio Settings -> back -> back -> main;
//              the main menu's camera is where it was before (the shared
//              metacam is reset after every transition).
//   career     boot -> main -> Career -> Choose Band (NEW BAND) -> a band named "A" -> Medium -> the
//              character select (Alterna) -> the guitar select (SG, skin
//              sg_white) -> sponsored -> the career menu (autosave); the
//              look in career.json and player 0's config; the name grows
//              left to right, Back to Choose Band and the band again bring
//              the difficulty screen back lit (meta_bricks), and the locked
//              characters' portraits draw char_locked.mat.
//   career_full  career, then the first tier at Medium: three songs passed
//              with GH2's cheat_beat_song, the fourth played (the encore
//              offered), the encore played (battle complete, small1
//              unlocked, status 1, $450), the store (lp_cayenne bought for
//              $250, on the Les Paul) and career.json checked.
//   career_song  one career song (GD03_WALK_SONG, default strutter) at
//              GD03_WALK_DIFF (default Medium): the tier songs before it
//              passed with cheat_beat_song, the song offered and played,
//              its endgame followed; GD03_WALK_END / _FAIL / _CHAR / _GUITAR
//              / _ENHANCED (docs/career_and_store.md "Tests").
//   enhanced   main -> Options -> Enhanced Options (no transition; every
//              option at the original look; the picture 4:3 between black
//              bars) -> MODES: Steam Deck, Original Feel (window and
//              resolution kept), Good Old Days, Original Feel -> Resolution
//              448p -> 480p, saved in enhanced.json -> Widescreen on (16:9,
//              no bars) and off (4:3 again) -> Max FPS Unlimited, then 60 ->
//              Export User Files, Load User Settings (the removed Ground Fog
//              / Light Streaks / XInput keys ignored), Load User Progress
//              (GD03_DIALOG: the walk's folder) -> back -> back -> main;
//              Enhanced Options again; both times the backdrop shows the
//              band, its singer on the right.
//   cheats     at the main menu, GH2's Air Guitar and Hyper Speed codes turn
//              them on; Air Guitar's again, after a pause, turns it off.
//   mouse      the mouse alone: hover OPTIONS on the main menu (focused), click
//              it -> Options -> click Enhanced Options -> MODES > Clean
//              Image clicked -> MODES folded (its title clicked) ->
//              Resolution's dropdown clicked open, 720p clicked ->
//              Widescreen's row clicked (16:9) -> the scrollbar's bottom
//              clicked, Monkey Head Crowd on and off -> the Back button ->
//              Options -> the help bar's BACK clicked -> main; OPTIONS' hit
//              box the same in 4:3 and 16:9, and hovering it focuses it again.
//   gamepads   the keyboard's yellow fret on the main menu -> the Gamepad menu
//              (once the cheat codes' 750 ms are over; no transition) ->
//              Player 1 > Customize inputs -> Key 2 of the keyboard's Whammy
//              bound to E (a real key), working at once, written to
//              controllers.json only on Save -> back -> main; then the help
//              bar's GAMEPADS clicked.
//   gamepads_pad  a virtual Xbox 360 pad plugged in at the main menu (player
//              1, the keyboard player 2) -> the Gamepad menu -> Player 2's
//              Bind to > Detect -> the pad's A: the pad is player 2, the
//              keyboard player 1.
//   stickyaxis an adapter whose D-pad is an axis resting off centre (a
//              virtual pad, 0079:0006:dpadaxis=-32768) plugged in at the main
//              menu: no left / right press in 2 s; moved, one right press.
//   coop       boot (a save created) -> main -> MULTIPLAYER -> the controller select (pad 0 to
//              P1, pad 1 to P2, both confirm) -> CO-OP -> the venue -> the first song (surrender:
//              guitar and bass) -> the parts (P1 guitar, P2 bass) -> the difficulties (P1 Easy, P2
//              Medium) -> both characters and guitars -> the song (both on autoplay, cut short)
//              -> the co-op results (finish_coop_song) -> the bass award -> complete; checks the
//              game's setup and career.json's co-op record and award.
//   faceoff    the same to FACE-OFF (multi_vs): characters, guitars, venue, song, both
//              difficulties -> the song -> multi_compete_screen -> complete.
//   proface    pro face-off unlocked in the save first (a tour won), then PRO FACE-OFF
//              (multi_fo): one difficulty for both -> the song -> multi_compete_fo_screen.
//   career_encore  career's first tier to its encore offer (iran played),
//              declined: the venue's encore_no, then the results.
//   practice   main -> TRAINING -> PRACTICE -> the first song -> the bass ->
//              Medium -> the first section as start and end -> 65% -> the
//              section (autoplay): songs/<s>/<s>_p65.vgs plays, the song
//              clock at 0.65 of the wall clock from the measure before the
//              section, the metronome (click_parser) and one overstrum's
//              miss_bass -> practice_end -> the end screen (the section's
//              gems) -> EXIT -> main.
//   tutorial   main -> TRAINING -> TUTORIALS -> the first tutorial: its
//              voice-over to the tuning, each fret held and strummed three
//              times (the keyboard), tuning_complete, the next lesson ->
//              Start -> QUIT -> main.
//   options    main -> Options -> Audio Settings: each slider one step down
//              with its sound, saved -> Video Settings -> the lag
//              calibration: its countdown, 11 clicks struck in time
//              (success) -> back x3 -> main.
// quickplay also deploys Star Power (claps, sp_depleted) and checks the
// crowd's crowd_begin / crowd_win; lose passes bangyourhead's first big
// note (blew_bignote.cue).
// quickplay and pause check a menu sound plays after the song (the game's
// pause holds only the sounds playing then). quickplay also checks the
// endgame photo GamePanel picked; quickplay and
// career that the fresh career.json has The Glitches.
// Every scenario also fails on any UNHANDLED or UNKNOWN script report, or a
// STUB not listed in tests/ui_stubs.txt (compiled in). Runs headless with
// SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy. GD03_WALK_SHOTS=<dir>
// saves a screenshot of every screen it reaches.
class WalkDriver {
public:
    explicit WalkDriver(const std::string& scenario);
    bool known() const { return !steps_.empty(); }
    // The screen to start on (empty: the boot) and the song's settings.
    std::string startScreen() const;
    float songEndMs() const { return endMs_; }
    float songFailMs() const { return failMs_; }
    // GD03_KEYS song items the walk plays in its songs ("<ms>:strum,...").
    const std::string& songKeys() const { return songKeys_; }
    bool autoplay() const { return autoplay_; }

    // One frame: waits / presses (through ui.buttonDown / buttonUp).
    void poll(ui::UiManager& ui, double nowMs);
    bool done() const { return done_; }
    // Final checks (after the run): prints a report, returns the exit code.
    int finish(ui::UiManager& ui);

private:
    struct Step {
        // Key: a real key (button: its SDL keycode). Mouse: button 0 hover,
        // 1 click, 3 right click, 4 / 5 wheel up / down, at (x, y), or on the
        // focus panel's component `screen` (its drawn bounds' centre).
        enum Kind { Screen, Press, Wait, Hold, Check, Key, Mouse } kind;
        std::string screen;
        int button = 0;
        double ms = 0;
        int player = 0;  // a press: the player (the pad on the controller select)
        float x = 0, y = 0;
    };
    std::string scenario_;
    bool seeded_ = false;
    std::vector<Step> steps_;
    std::vector<std::string> expect_;  // screens that must be entered, in order
    size_t step_ = 0;
    double stepStart_ = -1;
    bool done_ = false, failed_ = false, autoplay_ = true;
    float endMs_ = 1e30f, failMs_ = 1e30f;
    std::string songKeys_;
    std::string error_;
    std::array<float, 3> camera_{};  // the main menu's camera (the back walk)
    std::array<float, 4> bricks_{};  // the options' bricks light (the back walk)
    unsigned adapter_ = 0;           // the stickyaxis walk's virtual adapter (SDL_JoystickID)
    int sideBase_ = 0, rightBase_ = 0;
    float nameX0_ = 0;               // the career walk's band name: where it starts
    std::array<float, 2> optionsHit_{-1, -1};  // the mouse walk: OPTIONS' hit centre in 4:3
    std::array<double, 2> practiceClock_{-1, 0};  // the practice walk: (wall ms, song ms) first seen
    std::array<int, 3> volumes_{};                 // the options walk: the volumes before the sliders
    double lastNavMs_ = -1e9;                      // focus_to: the last press
    int lagStrikes_ = 0;                           // the options walk: strums on the lag clicks
    bool encoreDeclined_ = false;                  // career_song: NO on the encore offer

    void runCheck(ui::UiManager& ui, const Step& s, double nowMs);
};
