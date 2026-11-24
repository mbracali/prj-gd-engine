// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/tutorial.h"

#include <string>

#include "data/qbkey.h"
#include "script/globals.h"

namespace tutorial {

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

std::string nameOf(uint32_t k) {
    const std::string* n = qbkey::names().find(k);
    return n ? *n : std::string();
}

// A Key / KeyPtr value's key, or 0.
uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0;
}

int intOf(const qb::Value* v, int fallback) {
    if (!v) return fallback;
    if (v->type == qb::Type::Int) return v->i;
    if (v->type == qb::Type::Float) return int(v->f[0]);
    return fallback;
}

bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

// Is `call` a wait-for-the-player gate? The disc's gates are bare (no-timing)
// steps whose call loops `Wait 1 gameframe` until a judge-fed counter reaches a
// threshold: the note/strum/pad/star-power and layout/orientation prompts.
bool isGate(const std::string& call) {
    return contains(call, "wait_for") || contains(call, "watch_for") || contains(call, "_task") ||
           contains(call, "orientation") || contains(call, "prompt_for");
}

// The gate's pass threshold where the gate bytecode is known (see tutorial.h).
// Everything else is one scripted-input unit (threshold 1).
int gateThreshold(const std::string& call) {
    if (call == "training_1_3_wait_for_hit_notes") return 8;   // notes_hit >= 8 (lessons 3-6)
    if (call == "training_1_7_wait_for_hit_notes") return 4;   // notes_hit >= 4 (lesson 7)
    if (call == "training_2_3_watch_for_notes_hit") return 8;  // notes_hit >= 8
    return 1;
}

// Parse one training_<item>_tutorial_script array into ordered steps.
std::vector<Step> parseSteps(const Globals& g, const qb::Value& scriptArr) {
    std::vector<Step> out;
    const qb::Value* arr = g.resolve(scriptArr);
    if (!arr || arr->type != qb::Type::Array || !arr->arr) return out;
    for (const qb::Value& e : arr->arr->values) {
        if (e.type != qb::Type::Struct || !e.st) continue;
        Step s;
        s.call = keyOf(e.st->find(K("call")));
        if (const qb::Value* l = e.st->find(K("lesson"))) s.lesson = intOf(l, 0);
        if (const qb::Value* t = e.st->find(K("time"))) { s.hasTime = true; s.timeMs = intOf(t, 0); }
        if (const qb::Value* r = e.st->find(K("rel_time"))) { s.hasRelTime = true; s.relTimeMs = intOf(r, 0); }
        std::string callName = nameOf(s.call);
        if (isGate(callName)) { s.gate = true; s.gateThreshold = gateThreshold(callName); }
        out.push_back(s);
    }
    return out;
}

// Group steps into lessons (a `lesson` marker starts one).
std::vector<Lesson> groupLessons(const std::vector<Step>& steps) {
    std::vector<Lesson> out;
    for (const Step& s : steps) {
        if (s.lesson != 0) {
            Lesson l;
            l.number = s.lesson;
            out.push_back(l);
        }
        if (out.empty()) continue;  // pre-lesson startup/title steps
        Lesson& cur = out.back();
        cur.steps.push_back(s);
        if (s.gate && !cur.hasGate) {
            cur.hasGate = true;
            cur.gateCall = s.call;
            cur.gateThreshold = s.gateThreshold;
        }
    }
    return out;
}

}  // namespace

const char* categoryName(Category c) {
    switch (c) {
        case Category::Guitar: return "guitar";
        case Category::Drums: return "drums";
        case Category::Vocals: return "vocals";
        case Category::Band: return "band";
    }
    return "?";
}

const Tutorial* Menu::find(uint32_t scriptName) const {
    for (const Tutorial& t : tutorials)
        if (t.scriptName == scriptName) return &t;
    return nullptr;
}

Menu load(const Globals& g) {
    Menu menu;
    const qb::Value* list = g.find(K("tutorial_lessons"));
    if (!list || list->type != qb::Type::Array || !list->arr) return menu;

    const Category cats[] = {Category::Guitar, Category::Drums, Category::Vocals, Category::Band};
    size_t ci = 0;
    for (const qb::Value& catRef : list->arr->values) {
        const qb::Value* catArr = g.resolve(catRef);
        Category cat = ci < 4 ? cats[ci] : Category::Guitar;
        ++ci;
        if (!catArr || catArr->type != qb::Type::Array || !catArr->arr) continue;
        for (const qb::Value& entry : catArr->arr->values) {
            if (entry.type != qb::Type::Struct || !entry.st) continue;
            // A category header (focusable = -1) is a label, not a lesson.
            if (entry.st->find(K("focusable"))) continue;
            // The menu stores `item` as a string ('basic', 'drum_int', ...).
            const qb::Value* iv = entry.st->find(K("item"));
            std::string itemName;
            if (iv && iv->type == qb::Type::String)
                itemName = iv->str;
            else if (iv)
                itemName = nameOf(keyOf(iv));
            if (itemName.empty()) continue;
            uint32_t item = K(itemName.c_str());
            // menu_tutorial_select_choose: 'training_%i_tutorial_script'.
            uint32_t scriptName = K(("training_" + itemName + "_tutorial_script").c_str());
            const qb::Value* script = g.find(scriptName);
            if (!script) continue;
            Tutorial t;
            t.category = cat;
            t.item = item;
            t.scriptName = scriptName;
            t.steps = parseSteps(g, *script);
            if (t.steps.empty()) continue;
            t.lessons = groupLessons(t.steps);
            menu.tutorials.push_back(std::move(t));
        }
    }
    return menu;
}

// --- Session ---------------------------------------------------------------

Session::Session(const Tutorial& t) : t_(t) {}

void Session::fireNext() {
    const Step& s = t_.steps[stepIdx_];
    if (s.lesson != 0) lastLesson_ = s.lesson;  // g_training_last_lesson
    if (s.gate) {
        running_ = true;
        curGate_ = true;
        gateActions_ = 0;
        gateThreshold_ = s.gateThreshold;
    } else {
        running_ = false;
        curGate_ = false;
    }
    lastCallMs_ = clockMs_;
    const int fired = stepIdx_;
    ++stepIdx_;
    // g_training_lessons_completed: a numbered lesson finishes when its last
    // step has fired (the next step opens a new lesson, or the script ends).
    // Steps before the first `lesson` marker (startup/title) do not count.
    const int n = int(t_.steps.size());
    const bool lessonEnded = (fired + 1 >= n) || (t_.steps[fired + 1].lesson != 0);
    if (lessonEnded && lastLesson_ >= 1) ++lessonsDone_;
}

void Session::tick(int dtMs) {
    clockMs_ += dtMs;
    if (!started_) {
        started_ = true;
        if (!t_.steps.empty()) fireNext();  // run_training_script fires step 0
    }
    const int n = int(t_.steps.size());
    for (int guard = 0; guard < 100000; ++guard) {
        if (stepIdx_ >= n) {
            running_ = false;  // kill_training_script_system
            break;
        }
        if (running_ && curGate_) break;        // blocked on the player
        if (running_ && !curGate_) running_ = false;  // instant narration finished
        const Step& s = t_.steps[stepIdx_];
        bool due;
        if (s.hasTime)
            due = clockMs_ > s.timeMs;
        else if (s.hasRelTime)
            due = (clockMs_ - lastCallMs_) > s.relTimeMs;
        else
            due = !running_;  // the previous call has finished
        if (!due) break;
        fireNext();
    }
}

void Session::gateInput() {
    if (!(running_ && curGate_)) return;
    ++gateActions_;
    if (gateActions_ >= gateThreshold_) {
        running_ = false;
        curGate_ = false;
    }
}

}  // namespace tutorial
