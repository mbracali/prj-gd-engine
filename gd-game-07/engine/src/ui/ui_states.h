// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/qb.h"

namespace qs {
class Vm;
}

// gh4's UI state machine (docs/ui.md "States"), which replaces GH3's flow
// manager: the scripts register states (RegisterState {name Task = {name =
// task_menu params = {base_name ...}}}), create the composite object `ui`
// (eventcache, statemachinemanager, menustack) and post events to it
// (ui_event -> `ui : eventcache_add event_id = menu_change event_data =
// {state ...}`). The C side keeps a stack of states and, for a task_menu,
// runs the scripts named after its base_name [C: the prefixes "ui_init_",
// "ui_create_", "ui_destroy_", "ui_deinit_", "ui_return_" at 0x82025698 ..
// 0x820256E4, used by 0x823090D8 .. 0x8230A7C0; the script call
// 0x82309028 merges the task's params with the event's]. The order of
// calls on change / back / replace is [I] (D4).
class UiStates {
public:
    explicit UiStates(qs::Vm& vm);
    void addNatives();
    // Handles the queued events (once per script step).
    void update();
    // The state on top of the stack (0 before the first), its base name.
    uint32_t top() const { return stack_.empty() ? 0 : stack_.back().state; }
    std::string topBase() const;
    size_t depth() const { return stack_.size(); }
    // Focuses a screen element (UiSystem::focusElement), set by the front end.
    std::function<bool(uint32_t id)> focus;

private:
    struct State {
        uint32_t task = 0;
        qb::Struct params;  // the task's params (base_name, anim_in_script, pakname, ...)
    };
    struct Entry {
        uint32_t state = 0;
        qb::Struct params;  // task params + the event's data
    };
    void enter(Entry& e, bool first);
    void leave(Entry& e, bool remove);
    void call(const Entry& e, const char* prefix);
    Entry make(uint32_t state, const qb::Struct* data) const;
    void handle(uint32_t event, const qb::Struct& data);

    qs::Vm& vm_;
    std::unordered_map<uint32_t, State> states_;
    std::vector<Entry> stack_;  // back() is the top
    std::deque<std::pair<uint32_t, qb::Struct>> events_;
    bool log_ = false;
};
