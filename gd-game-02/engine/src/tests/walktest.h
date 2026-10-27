// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace ui {
class UiManager;
}

// `gd02 --walktest <scenario> <disc>`: plays through GH2's own menus with
// scripted buttons, waiting for each screen, and checks what happened:
//   quickplay  boot (splashes skipped by a button, the intro skipped) -> any button -> create a
//              save -> guitar_help -> main -> Quick Play -> the song list,
//              down x3 -> confirm -> Easy -> loading -> the song (autoplay,
//              cut short) -> results -> continue -> high score name ->
//              complete -> the song list (autosave) -> back -> main;
//              career.json written with the score.
//   lose       main -> Quick Play -> a song that fails -> lose -> quit -> main.
//   pause      main -> Quick Play -> the song -> pause -> Start resumes ->
//              pause -> restart -> pause -> quit -> the song list.
//   attract    the title screen idles 60 s -> attract mode -> a button ->
//              the title screen again.
//   back       main -> Options -> Audio Settings -> back -> back -> main;
//              the main menu's camera is where it was before (the shared
//              metacam is reset after every transition).
//   career     boot -> main -> Career -> Choose Band (NEW BAND) -> a band named "A" -> Medium -> the
//              character select (Alterna) -> the guitar select (SG, skin
//              sg_white) -> sponsored -> the career menu (autosave); the
//              look in career.json and player 0's config.
//   enhanced   main -> Options -> Enhanced Options -> Internal Resolution
//              changed (the window's own -> 448p) and saved in enhanced.json
//              -> back -> back -> main.
//   cheats     at the main menu, GH2's Air Guitar and Hyper Speed codes turn
//              them on; Air Guitar's again, after a pause, turns it off.
// quickplay also checks the endgame photo GamePanel picked; quickplay and
// career that the fresh career.json has The Glitches.
// Every scenario also fails on any UNHANDLED or UNKNOWN script report, or a
// STUB not listed in tests/ui_stubs.txt (compiled in). Runs headless with
// SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy. GD02_WALK_SHOTS=<dir>
// saves a screenshot of every screen it reaches.
class WalkDriver {
public:
    explicit WalkDriver(const std::string& scenario);
    bool known() const { return !steps_.empty(); }
    // The screen to start on (empty: the boot) and the song's settings.
    std::string startScreen() const;
    float songEndMs() const { return endMs_; }
    float songFailMs() const { return failMs_; }
    bool autoplay() const { return autoplay_; }

    // One frame: waits / presses (through ui.buttonDown / buttonUp).
    void poll(ui::UiManager& ui, double nowMs);
    bool done() const { return done_; }
    // Final checks (after the run): prints a report, returns the exit code.
    int finish(ui::UiManager& ui);

private:
    struct Step {
        enum Kind { Screen, Press, Wait, Hold, Check } kind;
        std::string screen;
        int button = 0;
        double ms = 0;
    };
    std::string scenario_;
    bool seeded_ = false;
    std::vector<Step> steps_;
    std::vector<std::string> expect_;  // screens that must be entered, in order
    size_t step_ = 0;
    double stepStart_ = -1;
    bool done_ = false, failed_ = false, autoplay_ = true;
    float endMs_ = 1e30f, failMs_ = 1e30f;
    std::string error_;
    std::array<float, 3> camera_{};  // the main menu's camera (the back walk)

    void runCheck(ui::UiManager& ui, const Step& s, double nowMs);
};
