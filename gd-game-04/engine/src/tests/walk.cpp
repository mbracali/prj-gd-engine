// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/walk.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdio>

#include "data/qbkey.h"
#include "ui/front_end.h"

namespace {

// The flow state player 1 is in (ui_flow_manager_state[0]).
std::string flowState(FrontEnd& fe) {
    const qb::Value* a = fe.vm().global(qbkey::key("ui_flow_manager_state"));
    if (!a || a->type != qb::Type::Array || !a->arr || a->arr->values.empty()) return "";
    return qbkey::name(a->arr->values[0].key);
}

}  // namespace

Walk::Walk(const std::string& name) : name_(name) {
    const Action C = Action::Confirm, D = Action::Down, B = Action::Back;
    // Boot -> press any button -> save -> main menu -> Quickplay ->
    // difficulty -> setlist -> song -> results [C: flow_path.md].
    std::vector<Step> boot = {
        {"bootup_press_any_button_fs", {C}, 30000},
        {"bootup_using_guitar_controller_fs", {C}, 60000},
        {"main_menu_fs", {}, 60000},
    };
    if (name == "quickplay" || name == "lose") {
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});  // career, co-op career, QUICKPLAY
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({name == "lose" ? "quickplay_fail_song_fs" : "quickplay_win_song_fs", {}, 30000});
    } else if (name == "back") {
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {B}, 20000});
        steps_.push_back({"main_menu_fs", {}, 20000});
    } else if (name == "boot") {
        steps_ = boot;
    } else if (name == "uitest") {
        steps_ = boot;
        uitest_ = true;
    }
}

void Walk::uiStep(FrontEnd& fe, double now) {
    if (states_.empty() && stateIdx_ == 0) {
        // Every global struct ending in _fs with a Create script [C data].
        for (const auto& name : fe.vm().globals().names()) {
            const std::string n = qbkey::name(name);
            if (n.size() < 4 || n.compare(n.size() - 3, 3, "_fs") != 0) continue;
            const qb::Value* v = fe.vm().globals().find(name);
            if (!v || !v->st || !v->st->find(qbkey::key("Create"))) continue;
            states_.push_back(name);
        }
        std::sort(states_.begin(), states_.end(), [](uint32_t a, uint32_t b) { return qbkey::name(a) < qbkey::name(b); });
    }
    if (stateAt_ >= 0 && now - stateAt_ < 500) return;
    if (stateAt_ >= 0) {
        const size_t made = size_t(fe.ui().stats().created) - created0_;
        if (made == 0) ++empty_, emptyStates_.push_back(qbkey::name(states_[stateIdx_ - 1]));
    }
    if (stateIdx_ >= states_.size()) {
        step_ = steps_.size();  // done
        return;
    }
    const uint32_t st = states_[stateIdx_++];
    created0_ = size_t(fe.ui().stats().created);
    qb::Struct p;
    qs::Vm::set(p, qbkey::key("flow_state"), qs::Vm::makeKey(st));
    fe.vm().spawn(qbkey::key("start_flow_manager"), p);
    stateAt_ = now;
}

std::vector<Action> Walk::update(FrontEnd& fe, bool songPlaying, double now) {
    std::vector<Action> out;
    if (uitest_ && step_ >= steps_.size() - 1 && reachedAt_ >= 0) {
        if (!done()) uiStep(fe, now);
        return out;
    }
    if (done() || failed_) return out;
    const Step& s = steps_[step_];
    if (reachedAt_ < 0) {
        bool reached;
        if (s.state == "song") reached = songPlaying;
        else if (s.state == "song_over") reached = !songPlaying && songsEnded > 0;
        else reached = flowState(fe) == s.state;
        if (reached) {
            reachedAt_ = now;
            lastKey_ = now;
            key_ = 0;
            log_.push_back(s.state);
            SDL_Log("walk: reached %s at %.0f ms", s.state.c_str(), now);
        } else if (now - stepStart_ > s.timeoutMs) {
            failed_ = true;
            failure_ = "waited " + std::to_string(int(s.timeoutMs)) + " ms for " + s.state + " (flow state: " + flowState(fe) + ")";
            SDL_Log("walk: FAILED: %s", failure_.c_str());
            return out;
        }
        return out;
    }
    // Reached: its buttons one by one (a screen gets 1 s to build first).
    if (key_ < s.keys.size()) {
        if (now - lastKey_ >= (key_ == 0 ? 1000.0 : 300.0)) {
            out.push_back(s.keys[key_++]);
            lastKey_ = now;
        }
        return out;
    }
    if (now - lastKey_ >= 300.0) {
        ++step_;
        reachedAt_ = -1;
        stepStart_ = now;
    }
    return out;
}

int Walk::finish(FrontEnd& fe) const {
    (void)fe;
    std::string path;
    for (const std::string& s : log_) path += (path.empty() ? "" : " > ") + s;
    if (failed_ || !done()) {
        std::printf("walktest %s: FAILED (%s); reached: %s\n", name_.c_str(),
                    failure_.empty() ? "did not finish in time" : failure_.c_str(), path.c_str());
        return 1;
    }
    if (uitest_) {
        std::printf("uitest: %zu flow states started, %d built no element:", states_.size(), empty_);
        for (const std::string& n : emptyStates_) std::printf(" %s", n.c_str());
        std::printf("\nuitest: every screen was started through the flow manager\n");
        return 0;
    }
    std::printf("walktest %s: passed (%s); songs started %d, ended %d\n", name_.c_str(), path.c_str(), songsStarted, songsEnded);
    return 0;
}
