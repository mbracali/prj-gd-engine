// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data/qb.h"

class Globals;

// Guitar Hero: World Tour's Tutorial / Training mode — the teaching layer that
// wraps the (faithful, pinned) gameplay judge. Rendering-free; the port of the
// disc's own scripted training system.
//
// The disc drives every lesson with ONE interpreter, the "training script"
// engine (scripts\tutorials\training.qb):
//   run_training_script    — fires step 0, then spawns training_script_update.
//   training_script_update  — once per gameframe: for the current step, fire
//     its `call` when due: a `time` step at absolute ms, a `rel_time` step at
//     ms since the last fired step, and a bare step (no timing) as soon as the
//     PREVIOUS step's call script has finished running (ScriptIsRunning). A
//     `lesson = N` marker on a step records g_training_last_lesson so a paused
//     lesson can Restart_Lesson from its first step. When the last step fires,
//     kill_training_script_system ends the session. [C scripts training.qb]
//
// A training script is an array (training_<item>_tutorial_script) of structs,
// each { lesson? time?/rel_time? call }. The playable set and their menu
// grouping come straight from scripts\guitar\menu\menu_tutorial_select.qb
// (tutorial_lessons = [guitar_lessons drum_lessons vocals_lessons band_lessons];
// menu_tutorial_select_choose: FormatText 'training_%i_tutorial_script' i=item).
//
// The "wait-for-the-player" lessons are bare steps whose `call` is a gate script
// that loops `Wait 1 gameframe` until a global counter the gameplay judge feeds
// reaches a threshold (training_1_3_wait_for_hit_notes: notes_hit>=8; _1_7: >=4;
// training_2_x_*_star_power: training_received_star_power==1; the drum/vocals
// *_task gates: LESSON_COMPLETE==1 once every pad/note has been played). Those
// thresholds live in the gate bytecode; this port reproduces the gate/advance
// behaviour and the data-derived counts, and is driven by the same note-hit /
// pad / star-power events the judge already emits. [C disassembly of the gates]
namespace tutorial {

enum class Category { Guitar, Drums, Vocals, Band };
const char* categoryName(Category c);

// One scripted step: an element of a training_<item>_tutorial_script array.
struct Step {
    uint32_t call = 0;         // the $call script name (Key / KeyPtr)
    int lesson = 0;            // the `lesson` marker (0 = none; a new lesson starts here)
    bool hasTime = false;      // a `time` step: fires at absolute ms
    int timeMs = 0;
    bool hasRelTime = false;   // a `rel_time` step: fires at ms since the last step
    int relTimeMs = 0;
    bool gate = false;         // the call waits for the player (a pass-gate)
    int gateThreshold = 1;     // player actions to satisfy the gate (data-derived where known, else 1)
};

// One lesson = the steps from a `lesson = N` marker up to the next marker.
struct Lesson {
    int number = 0;
    std::vector<Step> steps;
    bool hasGate = false;      // the lesson has a wait-for-the-player gate
    uint32_t gateCall = 0;
    int gateThreshold = 1;
};

// One tutorial track (guitar basic, drum intermediate, ...).
struct Tutorial {
    Category category = Category::Guitar;
    uint32_t item = 0;         // the menu item key ('basic', 'star_power', 'drum_int', ...)
    uint32_t scriptName = 0;   // the training_<item>_tutorial_script global's key
    std::vector<Step> steps;   // the whole script array, in order
    std::vector<Lesson> lessons;
};

// The whole Tutorial-select menu, built from the disc's own menu + scripts.
struct Menu {
    std::vector<Tutorial> tutorials;  // in tutorial_lessons category order
    const Tutorial* find(uint32_t scriptName) const;
};

// Builds the menu and parses every training script from the loaded globals
// (qb.pak). Invents nothing: the set, the grouping and every step come from
// the disc data.
Menu load(const Globals& g);

// Runs ONE tutorial exactly as run_training_script / training_script_update do
// (rendering-free). Fires steps on the simulated clock; blocks at a gate step
// until gateInput() has mimicked the scripted/autoplayed player input enough
// times to satisfy it, then advances. The same control flow the disc uses.
class Session {
public:
    explicit Session(const Tutorial& t);

    // Advance the simulated clock by `dtMs` and fire every step now due.
    void tick(int dtMs);
    // Mimic one unit of the input the current gate is waiting for (a hit note,
    // a strum, a drum pad, a star-power activation). A no-op when not at a gate.
    void gateInput();

    bool atGate() const { return running_ && curGate_; }
    bool done() const { return stepIdx_ >= int(t_.steps.size()) && !running_; }
    int currentStepIndex() const { return stepIdx_; }
    int currentLesson() const { return lastLesson_; }      // g_training_last_lesson
    int lessonsCompleted() const { return lessonsDone_; }  // g_training_lessons_completed
    int clockMs() const { return clockMs_; }
    int gateActions() const { return gateActions_; }

private:
    void fireNext();

    const Tutorial& t_;
    int stepIdx_ = 0;
    int clockMs_ = 0;
    int lastCallMs_ = 0;
    int lastLesson_ = 0;       // g_training_last_lesson (0 until the first lesson fires)
    int lessonsDone_ = 0;
    bool running_ = false;     // the last fired step's call is still running
    bool curGate_ = false;     // ... and it is a gate
    int gateActions_ = 0;
    int gateThreshold_ = 1;
    bool started_ = false;
};

}  // namespace tutorial
