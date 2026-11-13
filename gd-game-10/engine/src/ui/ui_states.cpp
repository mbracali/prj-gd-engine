// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/ui_states.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "data/qbkey.h"
#include "script/globals.h"
#include "script/vm.h"

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }
uint32_t keyOf(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0; }
const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr; }

std::string text(const qb::Value* v) {
    if (!v) return {};
    if (v->type == qb::Type::String || v->type == qb::Type::WString) return v->str;
    if (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr || v->type == qb::Type::StrPtr) return qbkey::name(v->key);
    return {};
}

// `dst` gets `src`'s items, replacing those of the same name.
void merge(qb::Struct& dst, const qb::Struct& src) {
    for (const qb::Item& it : src.items) qs::Vm::set(dst, it.name, it.value);
}

}  // namespace

UiStates::UiStates(qs::Vm& vm) : vm_(vm) {
    const char* v = SDL_getenv("GD10_FLOWLOG");
    log_ = v && *v && *v != '0';
}

std::string UiStates::topBase() const { return stack_.empty() ? std::string() : text(stack_.back().params.find(K("base_name"))); }

UiStates::Entry UiStates::make(uint32_t state, const qb::Struct* data) const {
    Entry e;
    e.state = state;
    auto it = states_.find(state);
    if (it != states_.end()) e.params = it->second.params;
    // The event's data, its included structs flattened (generic_event_choose
    // sends `data = { <data> state = <state> }`) [C scripts].
    if (data)
        for (const qb::Item& item : vm_.globals().expand(*data))
            if (item.name != K("state")) qs::Vm::set(e.params, item.name, item.value);
    return e;
}

// The script "<prefix><base_name>" with the entry's params, run now (it may
// not exist: most states have no ui_init_ / ui_deinit_) [C 0x82309028].
void UiStates::call(const Entry& e, const char* prefix) {
    const std::string base = text(e.params.find(K("base_name")));
    if (base.empty()) return;
    const uint32_t name = qbkey::key(std::string(prefix) + base);
    const qb::Value* g = vm_.global(name);
    if (!g || g->type != qb::Type::Script) return;
    if (log_) SDL_Log("ui state: %s%s", prefix, base.c_str());
    vm_.run(name, e.params);
}

void UiStates::enter(Entry& e, bool first) {
    if (first) call(e, "ui_init_");
    // A state with a pak: ui_state_pak_load pakname = ... [C script].
    if (const qb::Value* pak = e.params.find(K("pakname"))) {
        qb::Struct p;
        qs::Vm::set(p, K("pakname"), *pak);
        vm_.run(K("ui_state_pak_load"), p);
    }
    call(e, "ui_create_");
    // The task menu focuses its menu: the state's focus_id, else
    // ui_task_menu_default_focus_id (current_menu) [C data uistatemachine.qb;
    // I: when the C does it].
    uint32_t fid = keyOf(e.params.find(K("focus_id")));
    if (!fid) fid = keyOf(vm_.global(K("ui_task_menu_default_focus_id")));
    if (fid && focus) focus(fid);
    // The anim-in script (the state's own, else ui_task_menu_default_anim_in_script) [I].
    uint32_t anim = keyOf(e.params.find(K("anim_in_script")));
    if (!anim) anim = keyOf(vm_.global(K("ui_task_menu_default_anim_in_script")));
    if (anim) {
        if (const qb::Value* g = vm_.global(anim); g && g->type == qb::Type::Script) vm_.spawn(anim, e.params);
    }
}

void UiStates::leave(Entry& e, bool remove) {
    call(e, "ui_destroy_");
    if (remove) call(e, "ui_deinit_");
}

void UiStates::handle(uint32_t event, const qb::Struct& data) {
    const uint32_t state = keyOf(data.find(K("state")));
    if (log_) {
        std::string ds;
        for (const qb::Item& it : data.items)
            ds += " " + (it.name ? qbkey::name(it.name) : std::string("_")) + ":" + qb::typeName(it.value.type) +
                  (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr ? "=" + qbkey::name(it.value.key) : std::string());
        SDL_Log("ui event: %s %s {%s }", qbkey::name(event).c_str(), state ? qbkey::name(state).c_str() : "", ds.c_str());
    }
    if (state && !states_.count(state)) {
        vm_.logOnce("UNHANDLED ui state " + qbkey::name(state));
        return;
    }
    if (event == K("menu_change") || event == K("menu_add")) {
        if (!state) return;
        // menu_add keeps the state under it drawn (a pop-up) [I].
        if (!stack_.empty() && event == K("menu_change")) leave(stack_.back(), false);
        stack_.push_back(make(state, &data));
        enter(stack_.back(), true);
    } else if (event == K("menu_replace")) {
        if (!state) return;
        if (!stack_.empty()) {
            leave(stack_.back(), true);
            stack_.pop_back();
        }
        stack_.push_back(make(state, &data));
        enter(stack_.back(), true);
    } else if (event == K("menu_back")) {
        if (stack_.size() <= 1) return;
        // Back to `state` when given (popping every state above it), else
        // num_states states (default one) [C scripts: band_name_logo's
        // event_params {event = menu_back data = {num_states = n}}; I].
        int count = 1;
        if (const qb::Value* n = data.find(K("num_states")); n && n->type == qb::Type::Int && n->i > 0) count = n->i;
        bool inStack = false;
        for (const Entry& e : stack_) inStack = inStack || e.state == state;
        if (state && inStack) {
            // Back to that state: the ones above it go (none when it is the
            // top: GuitarEvent_SongWon_Spawned's menu_back to uistate_gameplay) [I].
            if (stack_.back().state == state) {
                return;  // already there: nothing changes [I]
            } else {
                while (stack_.size() > 1 && stack_.back().state != state) {
                    leave(stack_.back(), true);
                    stack_.pop_back();
                }
            }
        } else {
            do {
                leave(stack_.back(), true);
                stack_.pop_back();
            } while (stack_.size() > 1 && --count > 0);
        }
        Entry& t = stack_.back();
        qb::Struct d = data;
        d.items.erase(std::remove_if(d.items.begin(), d.items.end(), [](const qb::Item& it) { return it.name == K("num_states") || it.name == K("state"); }),
                      d.items.end());
        merge(t.params, qb::Struct{vm_.globals().expand(d)});
        enter(t, false);
    } else if (event == K("menu_refresh")) {
        if (stack_.empty()) return;
        leave(stack_.back(), false);
        merge(stack_.back().params, qb::Struct{vm_.globals().expand(data)});
        enter(stack_.back(), false);
    } else if (event == K("menu_anim_in_done") || event == K("menu_anim_out_done") || event == K("menu_add_data") ||
               event == K("menu_remove_data")) {
        // Notifications for the task's transitions; nothing waits on them here [I].
    } else {
        vm_.logOnce("UNHANDLED ui event " + qbkey::name(event));
    }
}

void UiStates::update() {
    // Events posted while handling one wait for the next update.
    size_t n = events_.size();
    while (n-- && !events_.empty()) {
        auto ev = std::move(events_.front());
        events_.pop_front();
        handle(ev.first, ev.second);
        if (log_) {
            std::string st;
            for (const Entry& e : stack_) st += " " + text(e.params.find(K("base_name")));
            const qb::Value* gm = vm_.global(K("game_mode"));
            SDL_Log("ui stack:%s (game_mode %s)", st.c_str(), gm ? qbkey::name(gm->key).c_str() : "-");
        }
        vm_.launchEvent(K("taskmenu_callback"));  // releases ui_event_block's Block [I]
    }
}

void UiStates::addNatives() {
    using qs::ScriptRun;
    using qs::Vm;
    // RegisterState {name = <state> Task = {name = task_menu params = {...}}}.
    vm_.addNative("RegisterState", [this](Vm&, ScriptRun&, qb::Struct& p) {
        const uint32_t name = keyOf(Vm::param(p, K("name")));
        if (!name) return false;
        State s;
        if (const qb::Struct* task = structOf(Vm::param(p, K("Task")))) {
            s.task = keyOf(task->find(K("name")));
            if (const qb::Struct* ps = structOf(task->find(K("params")))) s.params = *ps;
        }
        states_[name] = std::move(s);
        return true;
    });
    // The `ui` object's members; everything else goes to the earlier handler.
    auto next = vm_.objectCall;
    vm_.objectCall = [this, next](Vm& v, ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled) -> bool {
        if (obj != K("ui")) return next ? next(v, run, obj, method, p, handled) : (handled = false);
        handled = true;
        if (method == K("eventcache_add")) {
            const uint32_t ev = keyOf(Vm::param(p, K("event_id")));
            qb::Struct data;
            if (const qb::Struct* d = structOf(Vm::param(p, K("event_data")))) data = *d;
            if (log_) {
                std::string from;
                for (const qs::ScriptRun* r = &run; r; r = r->caller)
                    for (const auto& f : r->frames)
                        if (f.code) from += " < " + qbkey::name(f.code->name);
                SDL_Log("ui event posted: %s%s", qbkey::name(ev).c_str(), from.c_str());
            }
            if (ev) events_.emplace_back(ev, std::move(data));
            return true;
        }
        if (method == K("fsm_set")) {
            const uint32_t st = keyOf(Vm::param(p, K("state")));
            if (!st) return false;
            stack_.clear();
            const qb::Struct* ps = structOf(Vm::param(p, K("params")));
            stack_.push_back(make(st, ps));
            return true;
        }
        if (method == K("menustack_getstackcontents")) {
            // stack = [{base_name state ...}, ...], the top first [I: order
            // from ui_event_get_top's stack[0]].
            qb::Value arr;
            arr.type = qb::Type::Array;
            arr.arr = std::make_shared<qb::Array>();
            arr.arr->type = qb::Type::Struct;
            for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
                qb::Value s;
                s.type = qb::Type::Struct;
                s.st = std::make_shared<qb::Struct>(it->params);
                Vm::set(*s.st, K("state"), Vm::makeKey(it->state));
                arr.arr->values.push_back(std::move(s));
            }
            if (!run.frames.empty()) {
                if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
                // ... and stack_size (ui_event_exists_in_stack repeats
                // <stack_size> times: without it Music Studio's sign-in took
                // the boot path to the drum tip) [C script].
                Vm::set(*run.frames.back().locals, K("stack_size"), Vm::makeInt(int(stack_.size())));
                Vm::set(*run.frames.back().locals, K("stack"), std::move(arr));
            }
            return true;
        }
        if (method == K("unpause") || method == K("menustack_printcontents") || method == K("fsm_printstatemachine")) return true;
        v.logOnce("UNHANDLED ui member " + qbkey::name(method));
        return false;
    };
}
