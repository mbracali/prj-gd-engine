// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "input/input.h"

class FrontEnd;

// --walktest <walk>: plays through GH3's own menus with scripted buttons
// (kickoff rule 1: the real user path), headless with
// SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy. Each step waits for a
// flow state (ui_flow_manager_state[0]) or for a song to be playing / over,
// then presses its buttons. The walk passes when every step was reached.
class Walk {
public:
    explicit Walk(const std::string& name);
    bool known() const { return !steps_.empty(); }
    // Called every frame: the buttons to press now.
    std::vector<Action> update(FrontEnd& fe, bool songPlaying, double nowMs);
    bool done() const { return step_ >= steps_.size(); }
    bool failed() const { return failed_; }
    // The exit code and a summary line.
    int finish(FrontEnd& fe) const;
    // Song counters the walk checks (set by the app).
    int songsStarted = 0, songsEnded = 0;

private:
    struct Step {
        std::string state;          // flow state to wait for ("song" / "song_over": the song)
        std::vector<Action> keys;   // pressed one per 300 ms once reached
        double timeoutMs = 60000;
    };
    std::string name_;
    std::vector<Step> steps_;
    size_t step_ = 0;
    size_t key_ = 0;
    double reachedAt_ = -1, stepStart_ = 0, lastKey_ = 0;
    bool failed_ = false;
    std::string failure_;
    std::vector<std::string> log_;
    // uitest: every flow state with a Create script, started in turn.
    bool uitest_ = false;
    std::vector<uint32_t> states_;
    size_t stateIdx_ = 0;
    double stateAt_ = -1;
    size_t created0_ = 0;
    int empty_ = 0;
    std::vector<std::string> emptyStates_;
    void uiStep(FrontEnd& fe, double now);
};
