// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <vector>

namespace ui {
class UiManager;
}

// `gd02 --walktest <scenario> <disc>`: plays through GH2's own menus with
// scripted buttons, waiting for each screen, and checks what happened:
//   quickplay  boot (splashes, the intro skipped) -> any button -> create a
//              save -> guitar_help -> main -> Quick Play -> the song list,
//              down x3 -> confirm -> Easy -> loading -> the song (autoplay,
//              cut short) -> results -> continue -> high score name ->
//              complete -> the song list (autosave) -> back -> main;
//              career.json written with the score.
//   lose       main -> Quick Play -> a song that fails -> lose -> quit -> main.
//   pause      main -> Quick Play -> the song -> pause -> restart -> pause
//              -> quit -> the song list.
//   attract    the title screen idles 60 s -> attract mode -> a button ->
//              the title screen again.
//   career     boot -> main -> Career -> a band named "A" -> Medium -> the
//              character select (Alterna) -> the guitar select (SG, skin
//              sg_white) -> sponsored -> the career menu (autosave); the
//              look in career.json and player 0's config.
// quickplay also checks the endgame photo GamePanel picked.
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
        enum Kind { Screen, Press, Wait, Hold } kind;
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
};
