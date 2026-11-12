// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace ui {
class UiManager;
}

// `gd05 --walktest <scenario> <disc>`: plays through GH2's own menus with
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
//   career_song  one career song (GD05_WALK_SONG, default strutter) at
//              GD05_WALK_DIFF (default Medium): the tier songs before it
//              passed with cheat_beat_song, the song offered and played,
//              its endgame followed; GD05_WALK_END / _FAIL / _CHAR / _GUITAR
//              / _ENHANCED (docs/career_and_store.md "Tests").
//   enhanced   main -> Options -> Enhanced Options (universal_mechanisms.md
//              1.2.2): the defaults (the original look), MODES Steam Deck,
//              Original Feel (window and resolution kept), Good Old Days,
//              Original Feel; then every option in the doc's order stepped
//              with the pad, saved in enhanced.json and checked in effect
//              (calibration, Resolution 448p -> 480p, the ranges, Remove All
//              Effects, user files, cheats) -> back; Enhanced Options again;
//              both times the backdrop shows the band, its singer on the right.
//   cheats     at the main menu, GH2's Air Guitar and Hyper Speed codes turn
//              them on; Air Guitar's again, after a pause, turns it off; then
//              the same cheats in Enhanced Options > CHEATS & ENHANCERS.
//   mouse      the mouse alone: hover OPTIONS on the main menu (focused), click
//              it -> Options -> click Enhanced Options -> MODES > Clean Image
//              clicked -> MODES collapsed -> Resolution's dropdown clicked
//              open, 720p clicked -> the scrollbar's bottom clicked (the
//              list's end) -> Monkey Head Crowd clicked on and off -> the
//              Back button -> Options -> the help bar's BACK clicked -> main.
//   gamepads   the keyboard's yellow fret on the main menu -> the Gamepad menu
//              (once the cheat codes' 750 ms are over) -> Player 1 > Customize
//              inputs -> Key 2 of the keyboard's Whammy bound to E (a real
//              key), working at once, written to controllers.json only on
//              Save -> back -> main; then the help bar's GAMEPADS clicked.
//   gamepads_pad  a virtual Xbox 360 pad plugged in at the main menu (player
//              1, the keyboard player 2) -> the Gamepad menu -> Player 2's
//              Bind to > Detect -> the pad's A: the pad is player 2, the
//              keyboard player 1.
//   band       boot -> main -> Career -> NEW BAND -> the name with real keys:
//              Down Down (A -> C), 1 (green: takes it), Down, 1, Enter ->
//              the difficulty screen; the band is "CD" (universal_mechanisms.md 1.1.2).
//   logo_skip  the boot splashes skipped by the red fret (2) -> main ->
//              Options -> Credits (clicked) left by the green fret (1) ->
//              Credits again, left by a click (universal_mechanisms.md 1.1.1).
//   whammy_tail quick play, autoplay holding the sustains: the analog bar fully
//              down swells the held tail to 4x (glow line wider), up again it
//              goes back to 1x; the same with a digital whammy (a key) held.
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
//   career_win  career at Easy: every tier song before the tour's last
//              (fest's losangeles: Easy has no arena and no encores) passed
//              with cheat_beat_song, the last one played -> win_game_screen
//              (beat_easy, crowd_win) -> the results.
//   career_win_medium  the same at Medium (the last song arena's encore
//              playwithme): beat_other, then pro face-off unlocked.
//   career_practice  a career song lost -> PRACTICE (the lose screen's
//              career buttons) -> a section at 100% -> the pause's career
//              buttons: CHANGE SPEED and back, QUIT -> the career menu.
//   tutorial2  the second tutorial (Star Power), unlocked by starting the
//              first: the phrase, the whammy, the wail (button_error before
//              it), to its end (s19901 / s19902) -> main.
//   tutorial3  the third (hammer-ons, pull-offs): the hopo trainer played
//              three times each way with the keyboard, the track's HOPOs on
//              autoplay, to its end -> main.
//   career_encore  career's first tier to its encore offer (iran played),
//              declined: the venue's encore_no, then the results.
//   practice   main -> TRAINING -> PRACTICE -> the first song -> the bass ->
//              Medium -> the first section as start and end -> 65% -> the
//              section (autoplay): songs/<s>/<s>_p65.vgs plays, the song
//              clock at 0.65 of the wall clock from the measure before the
//              section, the metronome (click_parser) and one overstrum's
//              miss_bass -> practice_end -> the end screen (the section's
//              gems) -> EXIT -> main.
//   practice_menus  practice's pause: Start pauses and resumes; CHANGE
//              SPEED (65%: _p65), CHANGE SECTION (the second at 85%: _p85),
//              NEW SONG -> the song list.
//   tutorial   main -> TRAINING -> TUTORIALS -> the first tutorial: its
//              voice-over to the tuning, each fret held and strummed three
//              times (the keyboard), tuning_complete, the next lesson ->
//              Start -> QUIT -> main.
//   options    main -> Options -> Audio Settings: each slider one step down
//              with its sound, saved -> Video Settings -> the lag
//              calibration: its countdown, 11 clicks struck in time
//              (success) -> back x3 -> main.
//   enhanced_backends  main -> Options -> Enhanced Options: the backends
//              this start uses (GD05_WALK_ENHANCED_SEED: an enhanced.json
//              with unknown backends, dropped: local), Graphics Backend SDL3
//              then Vulkan, Audio Backend OpenAL (each asks Restart Now /
//              Later: back is Later), saved, still local; then Graphics SDL3
//              and Restart Now: the game starts again in the same process
//              (runApp) and walks enhanced_backends_restarted: straight on
//              Enhanced Options, on the saved backends (headless SDL3 falls
//              back to local; OpenAL plays on its null device).
// quickplay also deploys Star Power (claps, sp_depleted) and checks the
// crowd's crowd_begin / crowd_win; lose passes bangyourhead's first big
// note (blew_bignote.cue).
// quickplay and pause check a menu sound plays after the song (the game's
// pause holds only the sounds playing then). quickplay also checks the
// endgame photo GamePanel picked; quickplay and
// career that the fresh career.json has The Glitches.
// Every scenario also fails on any UNHANDLED or UNKNOWN script report, or a
// STUB not listed in tests/ui_stubs.txt (compiled in). Runs headless with
// SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy. GD05_WALK_SHOTS=<dir>
// saves a screenshot of every screen it reaches.
class WalkDriver {
public:
    explicit WalkDriver(const std::string& scenario);
    bool known() const { return !steps_.empty(); }
    // The screen to start on (empty: the boot) and the song's settings.
    std::string startScreen() const;
    float songEndMs() const { return endMs_; }
    float songFailMs() const { return failMs_; }
    // GD05_KEYS song items the walk plays in its songs ("<ms>:strum,...").
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
    double markMs_ = 0;
    std::string lastScreen_;           // screen-change times, for load_hold (GD02-155)
    double lastChangeMs_ = 0, lastLoadingMs_ = -1;  // a "mark" wait's time, for "until" waits (steps on a fixed beat)
    bool done_ = false, failed_ = false, autoplay_ = true;
    bool expectQuit_ = false;  // check expect_quit: the game quitting ends the walk well
    float endMs_ = 1e30f, failMs_ = 1e30f;
    std::string songKeys_;
    std::string error_;
    // rb_text_colors / GD05_TEXTCOLORS=1: every screen's texts measured once
    // it has rested (textColors), the ones too dark kept for finish().
    bool textColors_ = false;
    std::set<std::string> textColorsSeen_;
    int songlistHlChecks_ = 0, songlistHlMax_ = 0;  // rb_songlist_highlight (GD05-073)
    std::vector<std::string> textColorsBad_;
    double textColorsSince_ = -1;
    std::array<float, 3> camera_{};  // the main menu's camera (the back walk)
    std::array<float, 4> bricks_{};  // the options' bricks light (the back walk)
    unsigned adapter_ = 0;           // the stickyaxis walk's virtual adapter (SDL_JoystickID)
    int sideBase_ = 0, rightBase_ = 0;
    std::array<double, 2> practiceClock_{-1, 0};  // the practice walk: (wall ms, song ms) first seen
    std::array<int, 3> volumes_{};                 // the options walk: the volumes before the sliders
    double lastNavMs_ = -1e9;                      // focus_to: the last press
    int lagStrikes_ = 0;                           // the options walk: strums on the lag clicks
    bool encoreDeclined_ = false;                  // career_song: NO on the encore offer
    std::string winTarget_;                        // career_win: the tour's last song
    // rb_career (GD05-072): the creator's look before the choices, the
    // cash before the shop / the gig, the gig's stars.
    std::string careerHair0_;
    int careerCash_ = -1, careerStars_ = -1;
    int shotN_ = 0;                                // venue_follow: screenshot number
    // rb_look (GD05-069): each sampled world frame's luma mean, contrast,
    // saturation and grain; the last gameplay frame's background box luma
    // (the pause rule).
    std::vector<std::array<double, 4>> lookFrames_;
    std::vector<float> lookPrePause_;  // the luma (640x360) just before the pause
    std::string lookPauseError_;
    std::array<double, 3> lookPace_{-1, 0, 0};  // wall ms, world draws, frames at the first look frame
    float nameX0_ = 0;               // the career walk's band name: where it starts
    // rb_fuzz: the generator, steps, the main menu's buttons, pacing, the last actions.
    std::mt19937 fuzzRng_;
    int fuzzStep_ = 0, fuzzSteps_ = 0, fuzzChecks_ = 0, fuzzOff_ = 0, fuzzRecovered_ = 0;
    std::set<std::string> fuzzMain_;
    double fuzzNextMs_ = 0, fuzzActMs_ = 0, fuzzOffSince_ = -1;
    bool fuzzSettle_ = false;
    std::string fuzzLog_;
    // rb_keys_play: the next gem, the gems strummed, the frets held, the green key.
    size_t kbGem_ = 0;
    int kbPlayed_ = 0, kbTarget_ = -1;
    unsigned kbHeld_ = 0;
    bool kbOdAsked_ = false;
    unsigned kbGreen_ = '1';  // an SDL_Keycode (SDLK_1)
    // rb_guitar_play / rb_guitar_none / rb_pad_play: the device's own events.
    size_t devGem_ = 0;
    int devPlayed_ = 0, devTarget_ = -1;
    unsigned devHeld_ = 0;
    bool devStrumDown_ = false, devTilt_ = false, devOdAsked_ = false, devWhammied_ = false;
    double devReleaseMs_ = -1, devSustainSince_ = -1;

    void runCheck(ui::UiManager& ui, const Step& s, double nowMs);
};
