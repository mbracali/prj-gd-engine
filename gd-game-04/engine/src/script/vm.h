// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data/qb.h"
#include "script/bytecode.h"
#include "script/globals.h"

// The QScript virtual machine: runs GH3's own compiled scripts (lesson L1),
// with the engine's C functions as natives. docs/qscript.md has the
// semantics and their sources.
//
// A script runs line by line. A line is a call (`Name params...`), an
// assignment to a local (`<arg> x = expr` or `x = expr`), or control flow
// (if / else / elseif / endif with the compiler's forward offsets, begin /
// repeat [n] / break, switch / case / default with short jumps, return).
// Calls take a struct of parameters built from the rest of the line:
// `name = value`, bare names (flags), `<...>` (all the caller's
// parameters), inline structs (merged). A script's parameters are its
// locals; `return x = v` merges into the caller's. Natives get the
// parameters and the calling script and return true / false (what an `if`
// tests). Spawned scripts run each frame until they end; Wait suspends.
namespace qs {

class Vm;
struct Exec;
struct ScriptRun;

// A native (C function): returns the call's truth value.
using Native = std::function<bool(Vm& vm, ScriptRun& run, qb::Struct& params)>;

struct CodeCache {
    std::vector<Token> tokens;
    uint32_t name = 0;
};

// One script running: its call stack of frames.
struct Frame {
    std::shared_ptr<const CodeCache> code;
    size_t pc = 0;  // token index
    std::shared_ptr<qb::Struct> locals;
    struct Loop {
        size_t start;      // token after `begin`
        int64_t remaining; // -1: forever
    };
    std::vector<Loop> loops;
    bool arrivedByJump = false;  // the current token was reached by a jump (elseif)
};

struct ScriptRun {
    uint32_t id = 0;
    uint32_t name = 0;
    std::vector<Frame> frames;
    double waitUntilMs = -1;  // Wait in seconds (the VM clock)
    int waitFrames = 0;       // Wait n frames
    std::function<bool()> waitFor;  // a native's own wait condition (true: go on)
    bool finished = false;
    bool paused = false;
    uint32_t spawnedBy = 0;
    qb::Struct lastReturn;      // the parameters of the last `return` (a condition's value)
    bool returned = false;
    uint32_t type = 0;        // SpawnScriptNow ... id / type (for KillSpawnedScript)
    uint32_t object = 0;      // the object (screen element id) the script runs on, 0: none
};

struct VmStats {
    uint64_t lines = 0, calls = 0, nativeCalls = 0, scriptCalls = 0;
    size_t unhandled = 0;  // distinct unknown names called
};

class Vm {
public:
    explicit Vm(Globals& globals);

    Globals& globals() { return g_; }
    void addNative(const std::string& name, Native fn);
    void addNative(uint32_t key, Native fn) { natives_[key] = std::move(fn); }
    bool hasNative(uint32_t key) const { return natives_.count(key) != 0; }

    // Runs a script to its end or its first wait, synchronously (a call from
    // the engine); returns false if `name` is not a script. Its run keeps
    // going on update() when it waits.
    bool run(uint32_t name, const qb::Struct& params, bool* result = nullptr);
    // Starts a script as a spawned one (runs from the next update).
    uint32_t spawn(uint32_t name, const qb::Struct& params, uint32_t spawnedBy = 0);
    // Advances every spawned script by one game frame of `dtMs`.
    void update(double dtMs);
    double clockMs() const { return clockMs_; }
    void kill(uint32_t id);
    void killByName(uint32_t name);
    bool running(uint32_t name) const;
    std::vector<std::unique_ptr<ScriptRun>>& runs() { return runs_; }

    // Value helpers for natives.
    static const qb::Value* param(const qb::Struct& p, uint32_t name);
    static bool flag(const qb::Struct& p, uint32_t name);  // an unnamed key item
    // The first unnamed item (natives taking one positional value).
    static const qb::Value* positional(const qb::Struct& p, size_t index = 0);
    static void set(qb::Struct& s, uint32_t name, qb::Value v);
    static qb::Value makeInt(int32_t i);
    static qb::Value makeFloat(float f);
    static qb::Value makeKey(uint32_t k);
    static qb::Value makeString(std::string s);
    // A value as a number (int, float), or `fallback`.
    static float number(const qb::Value* v, float fallback = 0);
    // Truth as QScript tests it [I: non-zero numbers, any other value].
    static bool truth(const qb::Value& v);

    // Logging: GD04_UILOG=1 logs every unhandled name and stub once (L1).
    void logOnce(const std::string& what);
    const VmStats& stats() const { return stats_; }
    const std::unordered_set<uint32_t>& unhandled() const { return unhandledNames_; }
    std::mt19937& rng() { return rng_; }

    // Object member calls (`<id>:DoMorph ...`, or `DoMorph ...` in a script
    // running on an object): (vm, run, object id, method, params, handled)
    // -> result. Set by the screen-element system.
    std::function<bool(Vm&, ScriptRun&, uint32_t, uint32_t, qb::Struct&, bool&)> objectCall;
    // A spawned script's object (RunScriptOnScreenElement).
    uint32_t spawnOn(uint32_t name, const qb::Struct& params, uint32_t object);

    // GH3's global tags (GetGlobalTags / SetGlobalTags): the progress and
    // options the save holds, by tag key.
    std::unordered_map<uint32_t, qb::Struct> globalTags;
    // PushGlobalTags / PopGlobalTags sections (a band's career, by section key).
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, qb::Struct>> globalTagSections;

    // Code of a script global (tokenized once).
    std::shared_ptr<const CodeCache> code(uint32_t name);

    // A value of a global (resolving key references).
    const qb::Value* global(uint32_t name) const;
    // Writes a global (the native `change`).
    void setGlobal(uint32_t name, qb::Value v);

private:
    friend struct Exec;
    bool step(ScriptRun& run, int budget);  // runs until wait / end; false: ended
    bool callName(ScriptRun& run, uint32_t name, qb::Struct& params, bool& result);
    // An if / elseif condition: `[not]... <call with params>` or an expression.
    bool evalCondition(Exec& ex);
    // Runs a script called from a condition to its end, nested (its result
    // is true unless it returns a false value) [I].
    bool callForValue(ScriptRun& run, uint32_t name, qb::Struct& params);

    Globals& g_;
    std::unordered_map<uint32_t, Native> natives_;
    std::unordered_map<uint32_t, std::shared_ptr<const CodeCache>> codes_;
    std::vector<std::unique_ptr<ScriptRun>> runs_;
    std::unordered_set<uint32_t> unhandledNames_;
    std::unordered_set<std::string> logged_;
    uint32_t nextId_ = 1;
    double clockMs_ = 0;
    VmStats stats_;
    bool uiLog_ = false;
    std::mt19937 rng_{12345};
};

}  // namespace qs
