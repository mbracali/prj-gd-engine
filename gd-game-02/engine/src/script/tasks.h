// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// GH2's TaskMgr: the delayed and threaded script tasks the UI and the world
// share (one per Interp, Interp::tasks()).
//   {script_task (units U) (delay D) (name N) (once B) (preserve $v...) (script ...)}
//       runs its script once, D units from now; with (once FALSE) it runs again
//       every poll while the script returns true.
//   {thread_task (units U) (delay D) (name N) (preserve $v...) (script ...)}
//       a script thread: statements run in order until one yields:
//       {$task sleep T} resumes after the current statement, T units later
//       (sleeps add up exactly, so beat loops stay on the beat);
//       {$task wait C} re-runs the current statement next poll while C holds;
//       {$task wait_timeout C T $var} the same for at most T units, $var = timed out;
//       {$task loop} starts over; {$task exit} ends; {$task set_name N} renames.
// Units (system/run/config/macros.dta): kTaskSeconds 0 (song time), kTaskBeats 1,
// kTaskUISeconds 2, kTaskTutorialSeconds 3; the host supplies each clock
// (setTime) and polls. Tasks keep $this and their (preserve ...) variables,
// swapped in while they run. A named task is an object: {exists N}, {delete N}.
// Tasks made while polling start on the next poll; the order is creation
// order, so runs are deterministic.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "script/script.h"

enum TaskUnits : int { kTaskSeconds = 0, kTaskBeats = 1, kTaskUISeconds = 2, kTaskTutorialSeconds = 3, kTaskNumUnits = 4 };

class TaskMgr {
public:
    explicit TaskMgr(Interp& interp);
    ~TaskMgr();

    // The clocks (seconds or beats), set by the host before poll().
    void setTime(int units, double now);
    double time(int units) const;
    // Runs every task that is due, once.
    void poll();
    // taskmgr clear_tasks: ends every task.
    void clear();

    // From a {script_task ...} / {thread_task ...} command: its arrays from `first` on.
    ScriptObject* scriptTask(const DataNode& cmd, size_t first = 1);
    ScriptObject* threadTask(const DataNode& cmd, size_t first = 1);
    // The message form {<obj> thread_task {stmt}...} (GH1 style, still in cheats_funcs.dta).
    ScriptObject* threadTaskFromCode(const std::vector<Value>& args, int units, ScriptObject* self);

    bool isTask(const ScriptObject* o) const;
    bool kill(ScriptObject* task);           // false if it is not a live task
    void killFor(const ScriptObject* self);  // ends the tasks whose $this is `self` (its object is going away)
    size_t count() const;                     // live tasks

private:
    struct Task;
    struct Spec;
    ScriptObject* start(const Spec& spec);
    void run(Task& t, double now);
    void finish(Task& t);

    Interp& in_;
    std::vector<std::unique_ptr<Task>> tasks_;
    std::unique_ptr<ScriptObject> object_;  // "taskmgr"
    double now_[kTaskNumUnits] = {};
    bool polling_ = false;
};
