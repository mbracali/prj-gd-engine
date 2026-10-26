// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/scripttest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "data/dta.h"
#include "data/game_data.h"
#include "script/script.h"
#include "script/tasks.h"

namespace {

using T = DataNode::Type;

// ---------------------------------------------------------------- helpers

// DTA text with directives, as the compiler keeps them in a .dtb: `#define N`,
// `#include f`, `#merge f`, `#ifdef N`, `#ifndef N`, `#undef N`, `#else`, `#endif`.
void toDirectives(DataNode& list) {
    std::vector<DataNode> out;
    for (size_t k = 0; k < list.children.size(); ++k) {
        DataNode& c = list.children[k];
        if (c.type == T::Symbol && !c.s.empty() && c.s[0] == '#') {
            static const std::map<std::string, T> withName = {{"#define", T::Define}, {"#include", T::Include}, {"#merge", T::Merge},
                                                              {"#ifdef", T::IfDef},   {"#ifndef", T::IfNDef},   {"#undef", T::Undef}};
            DataNode d;
            if (auto it = withName.find(c.s); it != withName.end() && k + 1 < list.children.size()) {
                d.type = it->second;
                d.s = list.children[++k].s;
            } else if (c.s == "#else") {
                d.type = T::Else;
            } else if (c.s == "#endif") {
                d.type = T::EndIf;
            } else {
                out.push_back(std::move(c));
                continue;
            }
            out.push_back(std::move(d));
            continue;
        }
        if (c.isList()) toDirectives(c);
        out.push_back(std::move(c));
    }
    list.children = std::move(out);
}

DataNode parseTestDta(const std::string& text) {
    DataNode n = parseDtaText(text);
    toDirectives(n);
    return n;
}

std::string dump(const DataNode& n) {
    switch (n.type) {
    case T::Int: return std::to_string(n.i);
    case T::Float: {
        char b[32];
        std::snprintf(b, sizeof b, "%g", double(n.f));
        return b;
    }
    case T::Var: return "$" + n.s;
    case T::Symbol: return n.s;
    case T::String: return "\"" + n.s + "\"";
    case T::Unhandled: return "kDataUnhandled";
    case T::Array:
    case T::Command:
    case T::Property: {
        std::string open = n.type == T::Array ? "(" : n.type == T::Command ? "{" : "[";
        std::string close = n.type == T::Array ? ")" : n.type == T::Command ? "}" : "]";
        std::string s = open;
        for (size_t k = 0; k < n.children.size(); ++k) s += (k ? " " : "") + dump(n.children[k]);
        return s + close;
    }
    default: return "#?";
    }
}

std::string dumpTop(const DataNode& n) {
    std::string s;
    for (size_t k = 0; k < n.children.size(); ++k) s += (k ? " " : "") + dump(n.children[k]);
    return s;
}

struct Checker {
    int failures = 0, passed = 0;
    void check(bool ok, const std::string& what) {
        if (ok) {
            ++passed;
        } else {
            ++failures;
            std::printf("  FAIL %s\n", what.c_str());
        }
    }
    void equal(const std::string& got, const std::string& want, const std::string& what) {
        check(got == want, what + ": got '" + got + "', want '" + want + "'");
    }
};

// A test interpreter over in-memory files (archive .dtb path -> DTA text).
struct TestScripts {
    std::map<std::string, std::string> files;
    ScriptLoader loader{ScriptLoader::Reader([this](const std::string& dtb) {
        auto it = files.find(dtb);
        if (it == files.end()) throw std::runtime_error("no file " + dtb);
        return parseTestDta(it->second);
    })};
    Interp in;
    const DataNode& load(const std::string& dtaPath) { return in.keep(loader.load(dtaPath)); }
    // Runs DTA text as the file test/<name>.dta.
    const DataNode& run(const std::string& text) {
        const DataNode& t = in.keep(loader.preprocessTree(parseTestDta(text), "test/run.dta"));
        in.run(t);
        return t;
    }
    Value v(const std::string& name) { return in.var(name); }
};

const char* kTaskMacros =
    "#define kTaskSeconds (0) #define kTaskBeats (1) #define kTaskUISeconds (2) #define kTaskTutorialSeconds (3)\n";

// ---------------------------------------------------------------- unit checks

void checkLoader(Checker& c) {
    TestScripts s;
    s.files["config/gen/a.dtb"] =
        "(x (p 1) (q 2) (n (deep 1))) (y 5) 42\n"
        "#include ..\\other\\c.dta\n"
        "#merge b.dta\n";
    s.files["config/gen/b.dtb"] = "(x (p 9) (r 3) (n (deep 2) (deeper 4))) (z 7) {cmd} 4 (y 6)";
    s.files["other/gen/c.dtb"] = "(from_c 1)";
    DataNode a = s.loader.load("config/a.dta");
    c.equal(dumpTop(a), "(x (p 1) (q 2) (n (deep 1) (deeper 4)) (r 3)) (y 5) 42 (from_c 1) (z 7)",
            "#merge keeps existing keys, recurses, appends new arrays, drops non-arrays; #include with backslashes");
    c.check(s.loader.stats.includes == 1 && s.loader.stats.merges == 1 && s.loader.stats.errors.empty(), "include / merge counts");

    s.files["config/gen/d.dtb"] =
        "#ifdef HX_XBOX (xbox) #else (not_xbox) #endif\n"
        "#ifndef _SHIP (debug) #else (ship) #endif\n"
        "#ifdef HX_EE (ps2) #endif\n"
        "#define FOO (1 2)\n"
        "#ifdef FOO (foo_on FOO) #endif\n"
        "#undef FOO\n"
        "#ifndef FOO (foo_off FOO) #endif\n"
        "#ifdef NOPE #define LATER (9) #ifdef HX_XBOX (nested_wrong) #endif #else (outer_else) #endif\n"
        "(later LATER)\n"
        "#include missing.dta\n";
    DataNode d = s.loader.load("config/d.dta");
    c.equal(dumpTop(d), "(xbox) (ship) (foo_on 1 2) (foo_off FOO) (outer_else) (later LATER)",
            "HX_XBOX / _SHIP predefined; #ifdef / #ifndef / #else / #define / #undef in order");
    c.check(s.loader.stats.errors.size() == 1, "a missing include is reported");

    // Message macros with params (system/run/config/gen/macros.dta style).
    s.files["test/gen/m.dtb"] =
        "#define BUTTON_DOWN_MSG (button_down ($button $player_num))\n"
        "#define ENV_ON (animate (dest 10) (period 0.01))\n"
        "{new Object mo (BUTTON_DOWN_MSG {set $got {+ $button {* 10 $player_num}}} kDataUnhandled)"
        " (animate ($a $b) {set $dest {elem $a 1}} {set $period {elem $b 1}})}\n"
        "{mo button_down 3 4}\n"
        "{mo ENV_ON}\n";
    s.in.run(s.load("test/m.dta"));
    c.equal(s.v("got").str(), "43", "message macro with params as a handler head");
    c.equal(s.v("dest").str() + " " + s.v("period").str(), "10 0.01", "message macro spliced as message and arguments");
    c.check(s.in.unhandledCounts().empty(), "a handler returning kDataUnhandled with no native is not reported");
}

void checkLanguage(Checker& c) {
    TestScripts s;
    s.run(
        "{set $a {switch 3 (1 one) ((2 3) two_or_three) other}}"
        "{set $b {switch 9 (1 one) ((2 3) two_or_three) other}}"
        "{set $c {cond ({== 1 2} first) ({> 5 4} second) third}}"
        "{set $d {cond ({== 1 2} first) third}}"
        "{set $e {if FALSE x}}"
        "{set $f {if_else {! 0} yes no}}"
        "{func add3 ($x $y $z) {+ $x $y $z}}"
        "{set $x 100}"
        "{set $g {add3 1 2 3}}"
        "{set $h {do ($x 5) ($y) {set $y 2} {* $x $y}}}"
        "{set $arr {array 3}}"
        "{set_elem $arr 1 seven}"
        "{set $i {find_elem $arr seven $where}}"
        "{remove_elem $arr seven}"
        "{set $j {size $arr}}"
        "{set $k {pack_color 1 0.5 0}}"
        "{set $l {sprintf \"%s-%03d-%.1f\" abc 7 2.3}}"
        "{set $m {strneq drummer_body drummer 7}}"
        "{set $n {has_substr walk_left walk_}}"
        "{set $o {find ((a (b 1)) (c 2)) a b}}"
        "{set $p {/ 7 2}}"
        "{set $q {mod 7 3}}");
    c.equal(s.v("a").str(), "two_or_three", "switch with a list of keys");
    c.equal(s.v("b").str(), "other", "switch default");
    c.equal(s.v("c").str() + " " + s.v("d").str(), "second third", "cond and its default");
    c.check(s.v("e").isUnhandled() && !s.v("e").truthy(), "a false if is kDataUnhandled (false)");
    c.equal(s.v("f").str(), "yes", "if_else");
    c.equal(s.v("g").str(), "6", "func with params");
    c.equal(s.v("h").str() + " " + s.v("x").str(), "10 100", "do locals are restored");
    c.equal(s.v("i").str() + " " + s.v("where").str() + " " + s.v("j").str(), "1 1 2", "array / set_elem / find_elem / remove_elem");
    c.equal(s.v("k").str(), std::to_string(255 | (127 << 8)), "pack_color");
    c.equal(s.v("l").str(), "abc-007-2.3", "sprintf");
    c.equal(s.v("m").str() + s.v("n").str(), "11", "strneq / has_substr");
    c.equal(s.v("o").str(), "(b 1)", "find path");
    c.equal(s.v("p").str() + " " + s.v("q").str(), "3.5 1", "division and mod");

    // Objects: new, handlers with $this and [props], handle, the L1 report.
    s.run(
        "{new Object counter (refs 1) (bump ($n) {set [refs] {+ [refs] $n}} [refs]) (ping {set $pinged $this})}"
        "{set $r1 {counter bump 2}}"
        "{set $r2 {counter get refs}}"
        "{set $r3 {handle (counter bump 3) (counter not_a_method)}}"
        "{counter missing_method}"
        "{no_such_thing hello}"
        "{new NoSuchClass thing (x 1)}"
        "{handle (counter ping)}");
    c.equal(s.v("r1").str() + " " + s.v("r2").str(), "3 3", "[prop] defaults from the definition, set on the object");
    c.equal(s.v("pinged").str(), "counter", "handle sends with $this");
    c.check(s.in.unhandledCounts().count("counter missing_method") == 1 && s.in.unhandledCounts().size() == 1,
            "UNHANDLED for a direct send only (not for handle)");
    c.check(s.in.unknownCounts().count("no_such_thing") == 1 && s.in.unknownCounts().count("new NoSuchClass") == 1,
            "UNKNOWN for a missing name and class");
    StubObject stub("achievements");
    s.in.registerObject(&stub);
    s.run("{set $st {achievements unlock 3}}");
    c.check(s.in.stubCounts().count("achievements unlock") == 1 && s.v("st").str() == "0", "STUB objects answer 0 and are counted");
    s.in.unregisterObject(&stub);
    s.run("{delete counter} {set $gone {exists counter}}");
    c.equal(s.v("gone").str(), "0", "delete destroys a `new` object");
}

void checkRandom(Checker& c) {
    Interp in;
    in.seed(7);
    bool intOk = true, saw1 = false, saw2 = false, floatOk = true, unitOk = true;
    std::vector<int> first;
    for (int k = 0; k < 10000; ++k) {
        int v = in.randomInt(1, 3);
        intOk = intOk && (v == 1 || v == 2);
        saw1 = saw1 || v == 1;
        saw2 = saw2 || v == 2;
        float f = in.randomFloat(2, 5);
        floatOk = floatOk && f >= 2 && f < 5;
        float u = in.randomFloat(0, 1);
        unitOk = unitOk && u >= 0 && u < 1;
        if (k < 8) first.push_back(v);
    }
    c.check(intOk && saw1 && saw2, "random_int lo hi: lo <= n < hi");
    c.check(floatOk && unitOk, "random_float lo hi: lo <= x < hi");
    in.seed(7);
    std::vector<int> again;
    for (int k = 0; k < 8; ++k) {
        again.push_back(in.randomInt(1, 3));
        in.randomFloat(2, 5);
        in.randomFloat(0, 1);
    }
    c.check(first == again, "the same seed gives the same numbers");
    TestScripts s;
    s.in.seed(3);
    s.run("{set $rf {random_float 6 10}} {set $ri {random_int 5 6}} {set $re {random_elem (a)}}");
    c.check(s.v("rf").number() >= 6 && s.v("rf").number() < 10 && s.v("ri").str() == "5" && s.v("re").str() == "a",
            "random_float / random_int / random_elem from scripts");
}

void checkTasks(Checker& c) {
    TestScripts s;
    TaskMgr& tm = s.in.tasks();
    s.run(std::string(kTaskMacros) +
          "{script_task (units kTaskSeconds) (delay 1) (script {set $fired {taskmgr seconds}})}"
          "{script_task (delay 0.5) (units kTaskUISeconds) (script {++ $ui})}");
    tm.setTime(kTaskSeconds, 0.5);
    tm.poll();
    c.check(s.v("fired").str() == "0", "script_task waits for its delay");
    tm.setTime(kTaskSeconds, 1.0);
    tm.poll();
    c.equal(s.v("fired").str(), "1", "script_task runs at its delay (song seconds)");
    c.check(s.v("ui").str() == "0", "a UI-seconds task does not run on song time");
    tm.setTime(kTaskUISeconds, 0.5);
    tm.poll();
    tm.poll();
    c.equal(s.v("ui").str(), "1", "script_task runs once (UI seconds)");

    // A beat thread that loops (small2's neon_controller shape).
    s.run(std::string(kTaskMacros) +
          "{thread_task (preserve) (name beat_task) (units kTaskBeats)"
          " (script {++ $n} {$task sleep 0.5} {++ $n} {$task sleep 0.5} {$task loop})}");
    std::string seq;
    for (double beat : {0.0, 0.25, 0.5, 0.75, 1.0, 1.5, 1.6}) {
        tm.setTime(kTaskBeats, beat);
        tm.poll();
        seq += s.v("n").str();
    }
    c.equal(seq, "1122344", "thread_task sleeps in beats and loops on the beat");
    s.run("{set $alive {exists beat_task}} {if {exists beat_task} {delete beat_task}} {set $dead {exists beat_task}}");
    c.equal(s.v("alive").str() + s.v("dead").str(), "10", "a named task is an object; delete ends it");
    tm.setTime(kTaskBeats, 3);
    tm.poll();
    c.equal(s.v("n").str(), "4", "a deleted task does not run");

    // wait, wait_timeout, exit; $this and (preserve ...) kept.
    s.run(std::string(kTaskMacros) +
          "{new Object owner (start ($v)"
          "  {thread_task (units kTaskUISeconds) (preserve $v)"
          "   (script {$task wait {! $go}} {set $who $this} {set $seen $v} {$task wait_timeout TRUE 2 $timed_out} {set $after 1} {$task exit} {set $never 1})})}"
          "{owner start 42}");
    tm.setTime(kTaskUISeconds, 1);
    tm.poll();
    c.check(s.v("who").str() == "0", "wait holds while its condition is true");
    s.in.var("go") = Value::integer(1);
    tm.setTime(kTaskUISeconds, 2);
    tm.poll();
    c.equal(s.v("who").str() + " " + s.v("seen").str(), "owner 42", "the task keeps $this and its preserved variables");
    tm.setTime(kTaskUISeconds, 3);
    tm.poll();
    c.check(s.v("after").str() == "0", "wait_timeout waits");
    tm.setTime(kTaskUISeconds, 4.5);
    tm.poll();
    c.equal(s.v("after").str() + s.v("timed_out").str() + s.v("never").str(), "110", "wait_timeout times out; exit ends");

    // (once FALSE) repeats while the script returns true; clear_tasks; the {obj thread_task ...} form.
    s.run(std::string(kTaskMacros) + "{script_task (once FALSE) (script {++ $rep} {< $rep 3})}");
    for (int k = 0; k < 6; ++k) tm.poll();
    c.equal(s.v("rep").str(), "3", "script_task (once FALSE)");
    s.run("{new Object holder} {holder thread_task {++ $old} {$task sleep 1} {++ $old}}");
    tm.setTime(kTaskUISeconds, 10);
    tm.poll();
    c.equal(s.v("old").str(), "1", "{obj thread_task ...} runs in UI seconds");
    s.run("{taskmgr clear_tasks}");
    tm.setTime(kTaskUISeconds, 20);
    tm.poll();
    c.check(s.v("old").str() == "1" && tm.count() == 0, "taskmgr clear_tasks ends every task");
}

// ---------------------------------------------------------------- data checks

std::string dtaOf(const std::string& dtb) {
    // "x/gen/y.dtb" -> "x/y.dta"
    std::string p = dtb.substr(0, dtb.size() - 4) + ".dta";
    size_t g = p.rfind("gen/");
    if (g != std::string::npos && (g == 0 || p[g - 1] == '/')) p.erase(g, 4);
    return p;
}

int leftoverDirectives(const DataNode& n) {
    int k = 0;
    switch (n.type) {
    case T::IfDef: case T::Else: case T::EndIf: case T::Define: case T::Include: case T::Merge: case T::IfNDef:
    case T::Autorun: case T::Undef: ++k; break;
    default: break;
    }
    for (const DataNode& c : n.children) k += leftoverDirectives(c);
    return k;
}

// Static count of what the scripts reference.
struct Census {
    std::set<std::string> funcs, newNames;
    std::map<std::string, int> builtins, funcCalls, classes, heads;
    std::map<std::string, std::map<std::string, int>> methods;  // head -> message -> count
    const Interp* in = nullptr;

    void collect(const DataNode& n) {
        if (n.type == T::Command && n.children.size() > 1 && n.children[0].type == T::Symbol) {
            if (n.children[0].s == "func" && n.children[1].isText()) funcs.insert(n.children[1].s);
            if (n.children[0].s == "new" && n.children.size() > 2 && n.children[2].isText()) {
                newNames.insert(n.children[2].s);
                ++classes[n.children[1].s];
            }
        }
        for (const DataNode& c : n.children) collect(c);
    }

    static std::string headKey(const DataNode& h) {
        switch (h.type) {
        case T::Var: return "$" + h.s;
        case T::Property: return "[prop]";
        case T::Command: return "{expr}";
        default: return h.s;
        }
    }

    void count(const DataNode& n) {
        if (n.type == T::Command && !n.children.empty()) {
            const DataNode& h = n.children[0];
            std::string head = headKey(h);
            std::string msg = n.children.size() > 1 && n.children[1].type == T::Symbol ? n.children[1].s : "";
            if (h.type == T::Symbol && in->hasBuiltin(h.s)) {
                ++builtins[h.s];
            } else if (h.type == T::Symbol && funcs.count(h.s)) {
                ++funcCalls[h.s];
            } else {
                // Objects: group components by their extension, `new` objects as one row.
                std::string key = head;
                if (h.type == T::Symbol || h.type == T::String) {
                    size_t dot = head.rfind('.');
                    if (newNames.count(head)) key = "<new objects>";
                    else if (dot != std::string::npos && dot > 0) key = "*" + head.substr(dot);
                }
                ++heads[key];
                if (!msg.empty()) ++methods[key][msg];
            }
        }
        for (const DataNode& c : n.children) count(c);
    }
};

void printCounts(const char* title, const std::map<std::string, int>& m, size_t limit = 1000) {
    std::vector<std::pair<int, std::string>> v;
    for (const auto& [k, n] : m) v.push_back({-n, k});
    std::sort(v.begin(), v.end());
    std::printf("%s (%zu):", title, m.size());
    size_t col = 0;
    for (size_t k = 0; k < v.size() && k < limit; ++k) {
        std::string item = " " + v[k].second + " " + std::to_string(-v[k].first);
        if (col + item.size() > 110) {
            std::printf("\n   ");
            col = 0;
        }
        std::printf("%s", item.c_str());
        col += item.size();
    }
    if (v.size() > limit) std::printf(" ...");
    std::printf("\n");
}

// The game's classes (ui/gen counts in the M7 plan, world classes in M8).
const char* kKnownClasses[] = {"GHScreen", "GHPanel", "UIPanel", "MultiSelectPanel", "GuitarDisplayPanel", "EndGamePanel",
                               "MultiSelectScreen", "SliderPanel", "CharsysPanel", "GuitarSelectPanel", "StorePanel",
                               "SelSongPanel", "StatsProvider", "MidiLoaderPanel", "GamePanel", "FadePanel", "TrackBudgetScreen",
                               "HudPanel", "MultiCharSelPanel", "CreditsPanel", "LagPanel", "MultiSelectListPanel", "HelpBarPanel",
                               "MetaPanel", "TrackPanel", "TrackMaskPanel", "TutorialPanel", "WorldPanel", "UIScreen"};

}  // namespace

int runScriptUnitChecks() {
    Checker c;
    for (auto* fn : {checkLoader, checkLanguage, checkRandom, checkTasks}) {
        try {
            fn(c);
        } catch (const std::exception& ex) {
            c.check(false, std::string("exception: ") + ex.what());
        }
    }
    std::printf("script checks: %d passed, %d failed\n", c.passed, c.failures);
    return c.failures;
}

int runScriptTest(const std::string& dataPath) {
    int failures = runScriptUnitChecks();
    std::unique_ptr<GameData> data;
    try {
        data = std::make_unique<GameData>(dataPath);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "%s\n", ex.what());
        return 1;
    }

    // 1. The game's boot script, config/gh2.dta (it includes ui/ui.dta,
    // objects.dta with the world, char, track and ui objects, songs.dta ...).
    ScriptLoader boot(*data);
    DataNode bootTree;
    int loadFailures = 0, leftovers = 0;
    try {
        bootTree = boot.load("config/gh2.dta");
        leftovers += leftoverDirectives(bootTree);
    } catch (const std::exception& ex) {
        std::printf("  load config/gh2.dta: %s\n", ex.what());
        ++loadFailures;
    }
    for (const std::string& e : boot.stats.errors) std::printf("  config/gh2.dta: %s\n", e.c_str());
    const ScriptLoader::Stats& bs = boot.stats;
    std::printf("boot config/gh2.dta: %d files, %d #include, %d #merge, %d #define, %d #undef, %d #ifdef/#ifndef, %d #autorun, "
                "%d macro uses, %zu macros, %zu include/merge errors\n",
                bs.includes + bs.merges + 1, bs.includes, bs.merges, bs.defines, bs.undefs, bs.conditionals, bs.autoruns,
                bs.macroUses, boot.macros.size(), bs.errors.size());

    // 2. Every .dtb in the archive on its own, with the boot macros defined.
    std::vector<std::string> dtbs;
    for (const std::string& p : data->paths())
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".dtb") == 0) dtbs.push_back(p);
    std::sort(dtbs.begin(), dtbs.end());
    ScriptLoader::Stats total;
    int loaded = 0;
    std::vector<std::string> unshipped;  // includes of files the archive does not have (tool configs)
    for (const std::string& p : dtbs) {
        std::string dta = dtaOf(p);
        if (dtbPath(dta) != p) {
            std::printf("  %s: not a gen/ script path\n", p.c_str());
            ++loadFailures;
            continue;
        }
        ScriptLoader l(*data);
        l.macros = boot.macros;
        try {
            DataNode t = l.load(dta);
            leftovers += leftoverDirectives(t);
            ++loaded;
        } catch (const std::exception& ex) {
            std::printf("  load %s: %s\n", dta.c_str(), ex.what());
            ++loadFailures;
        }
        for (const std::string& e : l.stats.errors) unshipped.push_back(dta + ": " + e);
        total.includes += l.stats.includes, total.merges += l.stats.merges, total.defines += l.stats.defines;
        total.undefs += l.stats.undefs, total.conditionals += l.stats.conditionals, total.autoruns += l.stats.autoruns;
        total.macroUses += l.stats.macroUses;
    }
    std::printf("all scripts: %zu .dtb files, %d loaded, %d failed; %d #include, %d #merge, %d #define, %d #undef, "
                "%d #ifdef/#ifndef, %d #autorun, %d macro uses, %d directives left over\n",
                dtbs.size(), loaded, loadFailures, total.includes, total.merges, total.defines, total.undefs, total.conditionals,
                total.autoruns, total.macroUses, leftovers);
    std::printf("  %zu includes of files not on the disc, all in tool / test configs the game does not load:\n", unshipped.size());
    for (const std::string& e : unshipped) std::printf("    %s\n", e.c_str());

    // 3. Run gh2.dta's top level, (system (init ...)) (system_script.dta,
    // cheats_funcs.dta, long_cheats_funcs.dta) and (ui (init ...)) (every
    // screen and panel, then the boot commands) with stub objects.
    Interp in;
    std::map<std::string, std::unique_ptr<StubObject>> stubs;
    in.resolver = [&](const std::string& name) -> ScriptObject* {
        auto& s = stubs[name];
        if (!s) s = std::make_unique<StubObject>(name, "Stub");
        return s.get();
    };
    std::vector<ScriptObject*> made;  // every object `new` made
    for (const char* cls : kKnownClasses)
        in.registerClass(cls, [cls, &made](Interp&, const std::string& name, const DataNode&) {
            auto o = std::make_unique<StubObject>(name, cls);
            made.push_back(o.get());
            return o;
        });
    in.registerClass("Object", [&made](Interp&, const std::string& name, const DataNode&) {
        auto o = std::make_unique<ScriptObject>(name);
        made.push_back(o.get());
        return o;
    });
    int runFailures = 0;
    const DataNode& tree = in.keep(bootTree);
    in.run(tree);
    for (const char* sub : {"system", "ui"}) {
        const DataNode* list = tree.find(sub);
        const DataNode* init = list ? list->find("init") : nullptr;
        if (!init) {
            std::printf("  config/gh2.dta: no (%s (init ...))\n", sub);
            ++runFailures;
            continue;
        }
        in.runList(*list, 1);
        in.runList(*init, 1);
    }
    in.tasks().setTime(kTaskUISeconds, 1);
    in.tasks().poll();
    printCounts("run: objects made by new, per class", in.newCounts());
    printCounts("run: UNKNOWN", in.unknownCounts());
    printCounts("run: UNHANDLED", in.unhandledCounts());
    printCounts("run: STUB (host objects called)", in.stubCounts());
    std::printf("run: %zu stub objects referenced, %zu script errors, %zu tasks pending\n", stubs.size(), in.errors().size(),
                in.tasks().count());
    for (const std::string& e : in.errors()) std::printf("  error: %s\n", e.c_str());
    size_t bootErrors = in.errors().size();

    // Smoke run: every script handler of every object made so far, sent once
    // without arguments, then the tasks they start, run for a while. Shows
    // that the interpreter gets through all of GH2's UI code; the errors are
    // listed but expected ({fail} on arguments the stubs cannot give).
    in.resetReport();
    std::vector<ScriptObject*> targets = made;
    int sent = 0;
    for (ScriptObject* o : targets) {
        const DataNode* def = o->typeDef();
        if (!def) continue;
        for (size_t k = 3; k < def->children.size(); ++k) {
            const DataNode& h = def->children[k];
            if (h.type != T::Array || h.children.empty() || !h.children[0].isText()) continue;
            bool code = std::any_of(h.children.begin() + 1, h.children.end(), [](const DataNode& c) { return c.type == T::Command; });
            if (!code) continue;
            try {
                in.send(o, h.children[0].s, {});
            } catch (const std::exception& ex) {
                in.onError(o->name() + " " + h.children[0].s + ": " + ex.what());
            }
            ++sent;
        }
    }
    for (int step = 1; step <= 50; ++step) {
        for (int u = 0; u < kTaskNumUnits; ++u) in.tasks().setTime(u, 1 + step * 2.0);
        in.tasks().poll();
    }
    std::printf("smoke: %d handlers of %zu objects sent; %zu UNKNOWN, %zu UNHANDLED, %zu STUB keys; %zu script errors, %zu tasks left\n",
                sent, targets.size(), in.unknownCounts().size(), in.unhandledCounts().size(), in.stubCounts().size(),
                in.errors().size(), in.tasks().count());
    printCounts("smoke: UNKNOWN", in.unknownCounts(), 60);
    printCounts("smoke: UNHANDLED", in.unhandledCounts(), 60);
    for (size_t k = 0; k < in.errors().size() && k < 20; ++k) std::printf("  error: %s\n", in.errors()[k].c_str());

    // 4. What the game's scripts reference (PLAN §3 style), over the boot tree.
    Census census;
    census.in = &in;
    census.collect(tree);
    census.count(tree);
    printCounts("refs: builtins", census.builtins);
    printCounts("refs: classes in new", census.classes);
    std::printf("refs: %zu funcs defined, %zu called\n", census.funcs.size(), census.funcCalls.size());
    std::vector<std::pair<int, std::string>> objs;
    for (const auto& [k, n] : census.heads) objs.push_back({-n, k});
    std::sort(objs.begin(), objs.end());
    std::printf("refs: %zu objects / heads (sends, then messages; components grouped by extension):\n", objs.size());
    for (const auto& [n, k] : objs) {
        if (k.empty()) continue;
        std::string line = "  " + k + " " + std::to_string(-n) + ":";
        std::vector<std::pair<int, std::string>> ms;
        for (const auto& [m, mc] : census.methods[k]) ms.push_back({-mc, m});
        std::sort(ms.begin(), ms.end());
        for (size_t j = 0; j < ms.size() && j < 24; ++j) line += " " + ms[j].second + (ms[j].first < -1 ? "x" + std::to_string(-ms[j].first) : "");
        if (ms.size() > 24) line += " (+" + std::to_string(ms.size() - 24) + ")";
        std::printf("%s\n", line.c_str());
    }

    bool ok = failures == 0 && loadFailures == 0 && bs.errors.empty() && leftovers == 0 && runFailures == 0 && bootErrors == 0;
    std::printf("scripttest: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
