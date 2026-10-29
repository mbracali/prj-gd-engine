// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/walk.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <sstream>

#include "data/json.h"
#include "data/qbkey.h"
#include "script/vm.h"
#include "ui/front_end.h"
#include "ui/port_screens.h"

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
        {"bootup_using_guitar_controller_fs", {C}, 60000, true},  // only without a save [C data: flow_path.md]
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
    } else if (name == "career") {
        // Career: band -> difficulty -> character hub -> venue -> setlist ->
        // the first song -> won -> newspaper -> autosave -> setlist [C: the
        // flow states GH3's own scripts walk, GD04_FLOWLOG].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});  // CAREER
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});  // not on the first play
        // The name: three letters (NEXT) then ACCEPT (Start) [C script menu_ebn handlers].
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});  // first play
        steps_.push_back({"career_character_hub_fs", {C}, 20000});
        steps_.push_back({"career_select_venue_fs", {C}, 20000});
        steps_.push_back({"career_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({"career_newspaper_fs", {C}, 30000});
        steps_.push_back({"career_setlist_fs", {}, 30000});
    } else if (name == "back") {
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {B}, 20000});
        steps_.push_back({"main_menu_fs", {}, 20000});
    } else if (name == "mouse") {
        // The mouse alone: hover OPTIONS on the main menu (focused), click it
        // -> Options -> click the help bar's ENHANCED -> In-Game Max FPS's
        // dropdown clicked open, 60 clicked -> right click -> Options -> the
        // help bar's BACK clicked -> main (guidelines/universal_launcher.md).
        steps_ = boot;
        steps_.push_back({"main_menu_fs",
                          {"hover:menu:main_menu_options_text", "check:selected:main_menu_options_text", "click:menu:main_menu_options_text"},
                          10000});
        steps_.push_back({"options_select_option_fs", {"click:item"}, 20000});
        steps_.push_back({"port_enhanced",
                          {"hover:row:max_fps", "click:row:max_fps", "click:option:0", "check:enhanced:max_fps=60", "rclick"}, 10000});
        steps_.push_back({"options_select_option_fs", {"click:help:BACK"}, 10000});
        steps_.push_back({"main_menu_fs", {}, 20000});
    } else if (name == "gamepads") {
        // The keyboard's yellow fret (3) on the main menu -> GAMEPADS -> the
        // keyboard's Whammy rebound to E (a real key), saved in
        // controllers.json -> back -> main; then the help bar's GAMEPADS
        // clicked -> GAMEPADS.
        const Action U = Action::Up;
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"key:3"}, 10000});
        // Up from the keyboard's row wraps to its last rows: 11 up is Whammy.
        steps_.push_back({"port_gamepads", {U, U, U, U, U, U, U, U, U, U, U, C, "key:E", "check:whammy_is_e", B}, 10000});
        steps_.push_back({"main_menu_fs", {"click:item"}, 10000});
        steps_.push_back({"port_gamepads", {}, 10000});
    } else if (name == "stickyaxis") {
        // An adapter whose D-pad is an axis resting off centre (a virtual
        // pad, 0079:0006:dpadaxis=-32768) plugged in at the main menu: no
        // left / right press in 2 s; moved, one right press.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"adapter_attach", Walk::Op("check:no_sideways", 2000), "adapter_move", "check:one_right"}, 10000});
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

std::vector<Action> Walk::update(FrontEnd& fe, bool songPlaying, double now, WalkHost* host, const std::string& port) {
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
        else reached = (port.empty() ? flowState(fe) : port) == s.state;
        if (!reached && s.optional && step_ + 1 < steps_.size() && flowState(fe) == steps_[step_ + 1].state) {
            log_.push_back("(" + s.state + " skipped)");
            ++step_;
            stepStart_ = now;
            return out;
        }
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
        const Op& op = s.keys[key_];
        if (now - lastKey_ >= (key_ == 0 ? std::max(1000.0, op.delayMs) : op.delayMs)) {
            ++key_;
            lastKey_ = now;
            if (op.act.empty()) {
                out.push_back(op.a);
            } else if (!host || !act(op.act, fe, *host)) {
                if (failure_.empty()) failure_ = op.act + ": no host";
                failed_ = true;
                SDL_Log("walk: FAILED: %s", failure_.c_str());
            }
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

// The port's walks' acts (mouse, gamepads, stickyaxis).
bool Walk::act(const std::string& what, FrontEnd& fe, WalkHost& host) {
    auto fail = [&](const std::string& why) {
        failure_ = why;
        return false;
    };
    auto rest = [&](const char* prefix) { return what.substr(std::strlen(prefix)); };
    auto starts = [&](const char* prefix) { return what.rfind(prefix, 0) == 0; };
    // The mouse on something: "<verb>:menu:<element>" (GH3's element, at its
    // drawn bounds), "<verb>:help:<TEXT>" (GH3's help bar), "<verb>:item"
    // (the port's help bar item), "<verb>:row:<key>" / "<verb>:option:<k>"
    // (Enhanced Options).
    if (starts("hover:") || starts("click:")) {
        const int button = starts("click:") ? 1 : 0;
        const std::string target = what.substr(6);
        float x = 0, y = 0;
        bool found = false;
        if (target.rfind("menu:", 0) == 0) {
            float x0, y0, x1, y1;
            found = fe.ui().drawnBounds(qbkey::key(target.substr(5).c_str()), x0, y0, x1, y1);
            x = (x0 + x1) * 0.5f, y = (y0 + y1) * 0.5f;
        } else if (target.rfind("help:", 0) == 0) {
            if (const ui::Element* box = fe.ui().find(qbkey::key("user_control_container")))
                for (const auto& c : box->children)
                    if (c->text == target.substr(5) && c->drawnIn == fe.ui().drawCount())
                        x = (c->bx0 + c->bx1) * 0.5f, y = (c->by0 + c->by1) * 0.5f, found = true;
        } else if (target == "item" && host.walkPorts()) {
            float x0, y0, x1, y1;
            found = host.walkPorts()->itemRect(flowState(fe), x0, y0, x1, y1);
            x = (x0 + x1) * 0.5f, y = (y0 + y1) * 0.5f;
        } else if (target.rfind("row:", 0) == 0 && host.walkPorts()) {
            found = enhancedRowCenter(*host.walkPorts()->screen(PortScreens::Id::Enhanced), target.substr(4), x, y);
        } else if (target.rfind("option:", 0) == 0 && host.walkPorts()) {
            found = enhancedOptionCenter(*host.walkPorts()->screen(PortScreens::Id::Enhanced), std::atoi(target.c_str() + 7), x, y);
        }
        if (!found) return fail("the mouse: " + target + " is not on the screen");
        SDL_Log("walk: %s at %.0f, %.0f", what.c_str(), x, y);
        host.walkMouse(x, y, 0);
        if (button) host.walkMouse(x, y, 1);
        return true;
    }
    if (what == "rclick") return host.walkMouse(640, 360, 3), true;
    if (starts("key:")) {
        const SDL_Keycode k = SDL_GetKeyFromName(rest("key:").c_str());
        if (k == SDLK_UNKNOWN) return fail("no key " + rest("key:"));
        host.walkKey(int(k));
        return true;
    }
    if (starts("check:selected:")) {
        // The focused menu's selected item stands for that element (its focus handler names it).
        const uint32_t want = qbkey::key(rest("check:selected:").c_str());
        std::vector<const ui::Element*> stack{&fe.ui().root()};
        while (!stack.empty()) {
            const ui::Element* e = stack.back();
            stack.pop_back();
            for (const auto& c : e->children) stack.push_back(c.get());
            if (!e->focused || e->selected < 0 || size_t(e->selected) >= e->children.size()) continue;
            const ui::Element& sel = *e->children[size_t(e->selected)];
            if (sel.id == want) return true;
            for (const ui::Element::Handler& h : sel.handlers)
                if (const qb::Value* v = qs::Vm::param(h.params, qbkey::key("id")); v && v->key == want) {
                    std::printf("  mouse: the hover focused %s\n", rest("check:selected:").c_str());
                    return true;
                }
        }
        return fail("hovering did not focus " + rest("check:selected:"));
    }
    if (starts("check:enhanced:")) {
        const std::string kv = rest("check:enhanced:");
        const size_t eq = kv.find('=');
        const std::string v = host.walkEnhanced(kv.substr(0, eq));
        std::printf("  mouse: %s is %s\n", kv.substr(0, eq).c_str(), v.c_str());
        return v == kv.substr(eq + 1) ? true : fail("Enhanced " + kv.substr(0, eq) + " is " + v);
    }
    if (what == "check:whammy_is_e") {
        bool bound = false;
        for (const Input::Source& src : host.walkInput().bindings("keyboard")[size_t(Input::Control::Whammy)])
            bound = bound || (src.kind == Input::Source::Kind::Key && src.index == int(SDLK_E));
        std::ifstream f(host.walkSaveDir() + "controllers.json");
        std::stringstream ss;
        ss << f.rdbuf();
        bool saved = false;
        try {
            const Json root = Json::parse(ss.str());
            for (const Json& j : root["devices"]["keyboard"]["bindings"]["whammy"].a)
                saved = saved || (j["kind"].str() == "key" && int(j["index"].num()) == int(SDLK_E));
        } catch (const std::exception&) {
        }
        std::printf("  gamepads: Whammy %s, controllers.json %s\n", bound ? "on E" : "not on E", saved ? "has it" : "does not");
        return bound && saved ? true : fail("the rebinding of the keyboard's Whammy to E did not take or was not saved");
    }
    if (what == "adapter_attach") {
        adapter_ = unsigned(Input::attachVirtualPad("0079:0006:dpadaxis=-32768"));
        sideBase_ = presses_[size_t(Action::Left)] + presses_[size_t(Action::Right)];
        rightBase_ = presses_[size_t(Action::Right)];
        return adapter_ ? true : fail("no virtual adapter");
    }
    if (what == "check:no_sideways") {
        const int n = presses_[size_t(Action::Left)] + presses_[size_t(Action::Right)] - sideBase_;
        std::printf("  stickyaxis: %d left / right presses at rest\n", n);
        return n == 0 ? true : fail("the resting adapter pressed left / right by itself");
    }
    if (what == "adapter_move") {
        if (SDL_Joystick* j = SDL_GetJoystickFromID(SDL_JoystickID(adapter_))) SDL_SetJoystickVirtualAxis(j, 6, 0);
        return true;
    }
    if (what == "check:one_right") {
        const int r = presses_[size_t(Action::Right)] - rightBase_;
        std::printf("  stickyaxis: %d right press(es) once moved\n", r);
        if (SDL_Joystick* j = SDL_GetJoystickFromID(SDL_JoystickID(adapter_))) SDL_SetJoystickVirtualAxis(j, 6, -32768);
        return r == 1 ? true : fail("the moved adapter did not press right once");
    }
    return fail("unknown act " + what);
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
