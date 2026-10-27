// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The UI's script objects: panel components, UIPanel / PanelDir and GHScreen.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "scene/milo.h"
#include "scene/ui_objects.h"
#include "script/tasks.h"
#include "ui/natives.h"
#include "ui/sfx_args.h"
#include "ui/ui_host.h"
#include "ui/ui_objects.h"

namespace ui {

namespace {

std::string nameOf(const Value& v) { return v.type == Value::Type::Object && v.object ? v.object->name() : v.str(); }

bool isDirection(int b) { return b == kPad_DUp || b == kPad_DDown || b == kPad_DLeft || b == kPad_DRight; }

// (key value...) arrays among a message's arguments ({x animate (period 1) (range 0 10)}).
const Value* option(const std::vector<Value>& args, const char* key) {
    for (const Value& a : args)
        if (a.type == Value::Type::Array && a.array && !a.array->empty() && (*a.array)[0].isText() && (*a.array)[0].s == key)
            return &a;
    return nullptr;
}

float optionNumber(const std::vector<Value>& args, const char* key, size_t idx, float def) {
    const Value* o = option(args, key);
    return o && o->array->size() > idx && (*o->array)[idx].isNumber() ? (*o->array)[idx].number() : def;
}

}  // namespace

// ---------------------------------------------------------------- components

ComponentObj::ComponentObj(PanelObj* p, const scene::Object* o, const std::string& name)
    : ScriptObject(name, o ? o->cls : "Component"), panel(p), object(o) {
    if (isTextEntry()) initTextEntry();
}

bool ComponentObj::isButton() const { return object && (object->as<scene::BandButton>() || object->cls == "UIButton"); }
bool ComponentObj::isList() const { return object && object->as<scene::UIList>(); }
bool ComponentObj::isSlider() const { return object && object->as<scene::BandSlider>(); }
bool ComponentObj::isCheck() const { return object && object->as<scene::CheckBox>(); }
bool ComponentObj::isTextEntry() const { return object && object->as<scene::BandTextEntry>(); }

scene::UiState ComponentObj::state() const {
    if (panel->isDisabled(name())) return scene::UiState::Disabled;
    if (panel->selecting == name() || (isSlider() && sliderSelected)) return scene::UiState::Selecting;
    return panel->focus == name() ? scene::UiState::Focused : scene::UiState::Normal;
}

void ComponentObj::setState(scene::UiState s) {
    if (s == scene::UiState::Disabled) panel->disable(name(), kPad_DDown);
    else if (panel->isDisabled(name())) panel->enable(name());
    if (s == scene::UiState::Focused) panel->setFocus(name(), false);
    panel->applyStates();
}

void ComponentObj::setText(const std::string& s) {
    if (panel->view) panel->view->setText(name(), s);
    if (isTextEntry()) entered = s, updateTextEntry();
}

void ComponentObj::initTextEntry() {
    // The type's (characters ...) and (length n) (ui_objects.dta BandTextEntry types).
    if (!panel || !object) return;
    const DataNode* types = nullptr;
    if (const DataNode* objs = panel->ui.bootTree() ? panel->ui.bootTree()->find("objects") : nullptr)
        if (const DataNode* cls = objs->find("BandTextEntry")) types = cls->find("types");
    std::string type = object->header.type.empty() ? "default" : object->header.type;
    if (const DataNode* t = types ? types->find(type) : nullptr) {
        if (const DataNode* c = t->find("characters"); c && c->children.size() > 1 && c->children[1].isText()) charset = c->children[1].s;
        if (const DataNode* l = t->find("length"); l && l->children.size() > 1) maxLength = l->intAt(1);
    }
    if (charset.empty()) charset = "A";
    updateTextEntry();
}

void ComponentObj::updateTextEntry() {
    if (!panel->view) return;
    const bool canPick = !done && int(entered.size()) < maxLength;
    panel->view->setTextEntry(name(), entered, canPick ? std::string(1, charset[size_t(charIndex) % charset.size()]) : "", done);
}

void ComponentObj::refreshList() {
    if (!panel->view || !provider) return;
    if (auto* p = dynamic_cast<ListProvider*>(provider)) {
        std::vector<gfx::UiView::ListRow> rows;
        int selRow = 0;
        p->rows(rows, selected, selRow);
        panel->view->setList(name(), std::move(rows), selRow);
    }
}

bool ComponentObj::buttonDown(int button, int player) {
    UiManager& ui = panel->ui;
    if (isTextEntry()) {
        // BandTextEntry (0x821433B0) [C]: kPad_X adds the picked character,
        // kPad_Tri removes the last one, up / down pick, Start finishes.
        if (done) return true;
        const Value self = Value::obj(this);
        if (button == kPad_X) {
            if (int(entered.size()) >= maxLength) return ui.route("component_bad_selection", {}), true;
            entered += charset[size_t(charIndex) % charset.size()];
            updateTextEntry();
            ui.route("char_added", {});
            return true;
        }
        if (button == kPad_Tri) {
            if (entered.empty()) return ui.route("component_bad_selection", {}), true;
            entered.pop_back();
            updateTextEntry();
            ui.route("char_removed", {});
            return true;
        }
        if (button == kPad_DUp || button == kPad_DDown) {
            if (int(entered.size()) >= maxLength - 1 + 1) return ui.route("component_bad_selection", {}), true;
            const int n = int(charset.size());
            charIndex = ((charIndex + (button == kPad_DDown ? 1 : -1)) % n + n) % n;
            updateTextEntry();
            ui.route("component_scroll", {self});
            return true;
        }
        if (button == kPad_Start) {
            done = true;
            updateTextEntry();
            ui.route("text_entry", {self, Value::string(entered)});
            return true;
        }
        return false;
    }
    if (isList()) {
        auto* p = dynamic_cast<ListProvider*>(provider);
        const int n = p ? p->numData() : 0;
        if (button == kPad_DUp || button == kPad_DDown) {
            if (n <= 0) return true;
            int next = selected + (button == kPad_DDown ? 1 : -1);
            const auto* l = object->as<scene::UIList>();
            if (l->circular) next = (next % n + n) % n;
            next = std::clamp(next, 0, n - 1);
            if (next != selected) {
                selected = next;
                refreshList();
                ui.route("component_scroll", {Value::obj(this)});
            }
            return true;
        }
        if (button == kPad_X && state() == scene::UiState::Focused) return panel->selectStart(this, button, player), true;
        return false;
    }
    if (isSlider()) {
        if (sliderSelected) {
            if (isDirection(button)) {
                const int d = button == kPad_DUp || button == kPad_DRight ? 1 : -1;
                const int next = std::clamp(current + d, 0, std::max(0, steps - 1));
                if (next != current) {
                    current = next;
                    if (panel->view) panel->view->setSlider(name(), current, steps);
                    ui.callScript(panel, "slider_start_msg", {Value::obj(this)});
                }
                return true;
            }
            if (button == kPad_X) {
                sliderSelected = false;
                panel->applyStates();
                return true;
            }
            if (button == kPad_Tri) {
                ui.callScript(panel, "slider_select_cancel", {Value::obj(this)});
                sliderSelected = false;
                if (panel->view) panel->view->setSlider(name(), current, steps);
                panel->applyStates();
                return true;
            }
            return true;
        }
        if (button == kPad_X && state() == scene::UiState::Focused) {
            sliderSelected = true;
            panel->applyStates();
            ui.route("component_select_start", {Value::obj(this), Value::integer(button), Value::integer(player)});
            return true;
        }
        return false;
    }
    if ((isButton() || isCheck() || (object && object->as<scene::UIPicture>())) && button == kPad_X && state() == scene::UiState::Focused)
        return panel->selectStart(this, button, player), true;
    return false;
}

bool ComponentObj::handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    UiManager& ui = panel->ui;
    gfx::UiView* v = panel->view.get();
    const auto argStr = [&](size_t i) { return i < args.size() ? args[i].str() : std::string(); };
    const auto argNum = [&](size_t i, float def = 0) { return i < args.size() && args[i].isNumber() ? args[i].number() : def; };
    r = Value::integer(0);
    if (msg == "set_showing") {
        if (v) v->setShowing(name(), !args.empty() && args[0].truthy());
        return true;
    }
    if (msg == "showing") return r = Value::integer(v && v->showing(name()) ? 1 : 0), true;
    if (msg == "set_text") {
        // A token is localized (UILabel SetTextToken); other text shows as is.
        const std::string s = argStr(0);
        const std::string* loc = ui.locale().find(s);
        setText(args.empty() || !args[0].isText() || !loc ? s : *loc);
        return true;
    }
    if (msg == "set_localized_text" || msg == "set_localized") return setText(argStr(0)), true;
    if (msg == "get_text") return r = Value::string(isTextEntry() ? entered : v ? v->text(name()) : std::string()), true;
    if (msg == "set_wrap_width" || msg == "jitter_text" || msg == "set_caps") return true;
    if (msg == "set_frame") {
        if (v) v->setFrame(name(), argNum(0));
        return true;
    }
    if (msg == "frame") return r = Value::real(v ? v->frame(name()) : 0), true;
    if (msg == "end_frame") return r = Value::real(v ? v->endFrame(name()) : 0), true;
    if (msg == "animate") {
        // RndAnimatable Animate (0x821E9D00): (range a b) (period s) (dest d)
        // (units u); without a period, 30 frames a second.
        if (!v) return true;
        const float end = v->endFrame(name());
        float from = optionNumber(args, "range", 1, 0), to = optionNumber(args, "range", 2, end);
        if (option(args, "dest")) from = v->frame(name()), to = optionNumber(args, "dest", 1, end);
        const float period = optionNumber(args, "period", 1, -1);
        const int units = int(optionNumber(args, "units", 1, float(kTaskUISeconds)));
        const float seconds = period > 0 ? period : std::fabs(to - from) / 30.0f;
        ui.animate(panel, name(), from, to, seconds, units);
        return true;
    }
    if (msg == "stop_animation") return ui.stopAnimation(panel, name()), true;
    if (msg == "set_local_pos" || msg == "set_local_scale") {
        if (v && args.size() >= 3) {
            if (msg == "set_local_pos") v->setLocalPos(name(), argNum(0), argNum(1), argNum(2));
            else v->setLocalScale(name(), argNum(0), argNum(1), argNum(2));
        }
        return true;
    }
    if (msg == "get_state") return r = Value::integer(int(state())), true;
    if (msg == "set_state") return setState(static_cast<scene::UiState>(int(argNum(0)))), true;
    if (msg == "find") {
        ScriptObject* c = panel->component(argStr(0));
        return r = Value::obj(c), true;
    }
    // UIList (0x8237B540)
    if (msg == "set_provider") {
        provider = !args.empty() && args[0].type == Value::Type::Object ? args[0].object : in.findObject(argStr(0));
        if (auto* p = dynamic_cast<ListProvider*>(provider)) selected = std::clamp(selected, 0, std::max(0, p->numData() - 1));
        refreshList();
        return true;
    }
    if (msg == "selected_pos") return r = Value::integer(selected), true;
    if (msg == "selected_sym") {
        auto* p = dynamic_cast<ListProvider*>(provider);
        return r = Value::symbol(p ? p->symbol(selected) : std::string()), true;
    }
    if (msg == "set_selected") {
        auto* p = dynamic_cast<ListProvider*>(provider);
        if (!args.empty() && args[0].isNumber()) selected = args[0].integerValue();
        else if (p) selected = std::max(0, p->indexOf(argStr(0)));
        if (p) selected = std::clamp(selected, 0, std::max(0, p->numData() - 1));
        refreshList();
        return true;
    }
    if (msg == "set_dirty" || msg == "set_first_showing" || msg == "set_data") return refreshList(), true;
    if (msg == "is_scrolling") return r = Value::integer(0), true;
    if (msg == "num_data") {
        auto* p = dynamic_cast<ListProvider*>(provider);
        return r = Value::integer(p ? p->numData() : 0), true;
    }
    // UISlider (0x82375BC0)
    if (msg == "current") return r = Value::integer(current), true;
    if (msg == "set_current") {
        current = std::clamp(int(argNum(0)), 0, std::max(0, steps - 1));
        if (v) v->setSlider(name(), current, steps);
        return true;
    }
    if (msg == "set_num_steps") {
        steps = std::max(1, int(argNum(0, 10)));
        current = std::clamp(current, 0, steps - 1);
        if (v) v->setSlider(name(), current, steps);
        return true;
    }
    if (msg == "num_steps") return r = Value::integer(steps), true;
    // CheckBox (0x82140E98)
    if (msg == "get_check") return r = Value::integer(checked ? 1 : 0), true;
    if (msg == "set_check" || msg == "toggle") {
        checked = msg == "toggle" ? !checked : (!args.empty() && args[0].truthy());
        if (v) v->setCheck(name(), checked);
        return true;
    }
    // BandTextEntry (0x82143B28)
    if (isTextEntry()) {
        if (msg == "is_done") return r = Value::integer(done ? 1 : 0), true;
        if (msg == "resume_input") return done = false, updateTextEntry(), true;
        if (msg == "user_can_scroll") return r = Value::integer(!done && int(entered.size()) < maxLength ? 1 : 0), true;
        if (msg == "no_text_entered") return r = Value::integer(entered.empty() ? 1 : 0), true;
        if (msg == "length") return r = Value::integer(int(entered.size())), true;
    }
    // UIPicture set_tex
    if (msg == "set_tex") {
        if (v) v->setPictureTex(name(), argStr(0));
        return true;
    }
    // A sound cue in a panel ({sync_click.cue play}) or another sound object.
    if (msg == "play") {
        const SfxArgs a = sfxArgs(in, args, 0);
        return ui.playSfx(name(), a.volume, a.pan, a.transpose), true;
    }
    if (msg == "set_mat" || msg == "set_name" || msg == "set_rendered") return true;
    if (msg == "enable") return panel->enable(name()), true;
    if (msg == "disable") return panel->disable(name(), kPad_DDown), true;
    return false;
}

Value ComponentObj::getProp(Interp& in, const std::string& key) {
    if (key == "showing") return Value::integer(panel->view && panel->view->showing(name()) ? 1 : 0);
    return ScriptObject::getProp(in, key);
}

void ComponentObj::setProp(Interp& in, const std::string& key, const Value& v) {
    if (key == "showing") {
        if (panel->view) panel->view->setShowing(name(), v.truthy());
        return;
    }
    if (key == "text_token") {
        const std::string s = v.str();
        setText(s.empty() ? std::string() : panel->ui.localize(s));
        return;
    }
    if (key == "diffuse_tex" || key == "color") return;  // .mat set (Mat properties; not drawn yet)
    ScriptObject::setProp(in, key, v);
}

// ---------------------------------------------------------------- panels

PanelObj::PanelObj(UiManager& u, const std::string& name, const std::string& cls) : ScriptObject(name, cls), ui(u) {}

ComponentObj* PanelObj::component(const std::string& name) {
    if (name.empty()) return nullptr;
    if (auto it = components_.find(name); it != components_.end()) return it->second.get();
    const scene::Object* o = view ? view->find(name) : nullptr;
    if (!o && !anyComponent_) return nullptr;
    auto c = std::make_unique<ComponentObj>(this, o, name);
    ComponentObj* raw = c.get();
    components_[name] = std::move(c);
    return raw;
}

std::string PanelObj::fileName() {
    const DataNode* f = handler("file");
    if (!f || f->children.size() < 2) return "";
    const DataNode& v = f->children[1];
    Value val = v.type == DataNode::Type::Command ? ui.interp().eval(v) : ui.interp().toValue(v);
    return val.str();
}

void PanelObj::load() {
    if (loaded) return;
    loaded = true;
    const std::string file = fileName();
    if (!file.empty()) {
        path = scene::miloArkPath(scene::resolveMiloRelative("ui/panel.milo", file));
        std::string error;
        if (path.rfind("world/", 0) == 0) {
            // The venue (world_panel): the program's (game/world.h).
            ui.host().worldLoad(path);
        } else if (!(view = ui.makeView(path, &error)) && !ui.headless()) {
            ui.logOnce("ui: panel " + name() + ": cannot load " + path + ": " + error);
        }
        if (view) ui.panelViewLoaded(this), ui.portPanelLoaded(this);
    }
    ui.callScript(this, "load");
    ui.callScript(this, "finish_load");
}

void PanelObj::unload() {
    if (!loaded) return;
    if (up) exit();
    ui.callScript(this, "unload");
    if (path.rfind("world/", 0) == 0) ui.host().worldUnload();
    components_.clear();
    view.reset();
    loaded = false;
}

void PanelObj::enter() {
    if (!loaded) load();
    up = true;
    if (path.rfind("world/", 0) == 0) ui.host().worldEnter();  // the venue starts before the panel's script
    // The focus: (focus x) unless the last one is kept (retain_last_focus,
    // default TRUE) and the panel does not reset it (reset_focus).
    const bool retain = handler("retain_last_focus") || props.count("retain_last_focus") ? getProp(ui.interp(), "retain_last_focus").truthy() : true;
    const bool reset = getProp(ui.interp(), "reset_focus").truthy();
    std::string f = (retain && !reset && !lastFocus.empty()) ? lastFocus : getProp(ui.interp(), "focus").str();
    if (f == "0") f.clear();
    if (!f.empty() && !component(f)) f.clear();
    // No (focus ...) at all: the first component that takes the focus
    // (PanelDir Enter [I]; the credits list is then focused, white).
    if (f.empty() && !handler("focus") && view && view->milo())
        for (const scene::UIComponent* c : view->milo()->root().all<scene::UIComponent>())
            if ((c->as<scene::BandButton>() || c->as<scene::UIList>() || c->as<scene::BandSlider>() || c->as<scene::BandTextEntry>()) &&
                !isDisabled(c->name)) {
                f = c->name;
                break;
            }
    focus = f;
    selecting.clear();
    // ui_enter triggers play their anims from the start.
    if (view)
        for (const gfx::UiView::Trigger& t : view->triggers("ui_enter")) ui.animate(this, t.anim->name, 0, t.endFrame, t.endFrame / 30.0f, kTaskUISeconds);
    applyStates();
    ui.callScript(this, "enter");
    applyStates();
    // The panel's own FOCUS_MSG for the focus it enters with [I: UIPanel
    // Enter sets the focus (0x82376140)]: audio_settings_panel's
    // update_help_display shows its help bar this way.
    if (!focus.empty() && handler("component_focus"))
        ui.callScript(this, "component_focus", {Value::obj(component(focus)), Value::obj(nullptr), Value::obj(this)});
}

void PanelObj::exit() {
    if (!up) return;
    up = false;
    lastFocus = focus;
    ui.callScript(this, "exit");
    // A panel's pending tasks end with it [I]: highscore_panel's 7 s
    // {ui goto_screen complete_screen} must not fire after the player has
    // moved on (screens keep theirs: post_show_screen's postshow_loop).
    ui.interp().tasks().killFor(this);
}

void PanelObj::poll(double /*dtMs*/) {
    pollSelect();
    if (handler("poll")) ui.callScript(this, "poll");
}

bool PanelObj::repeatTiming(float& holdMs, float& repeatMs) const {
    const DataNode* j = handler("joypad");
    if (!j) return false;
    if (const DataNode* h = j->find("hold_ms")) holdMs = h->floatAt(1);
    if (const DataNode* r = j->find("repeat_ms")) repeatMs = r->floatAt(1);
    return true;
}

void PanelObj::applyStates() {
    if (!view) return;
    view->clearStates();
    view->setFocus(focus);
    for (const std::string& d : disabled) view->setState(d, scene::UiState::Disabled);
    if (!selecting.empty()) view->setState(selecting, scene::UiState::Selecting);
    for (auto& [n, c] : components_)
        if (c->isSlider() && c->sliderSelected) view->setState(n, scene::UiState::Selecting);
}

void PanelObj::setFocus(const std::string& name, bool notify) {
    if (name == focus) return;
    const std::string old = focus;
    focus = name;
    applyStates();
    if (notify) {
        ComponentObj* n = component(name);
        ComponentObj* o = component(old);
        ui.route("component_focus", {Value::obj(n), Value::obj(o), Value::obj(this)});
    }
}

void PanelObj::enable(const std::string& name) {
    disabled.erase(name);
    applyStates();
}

void PanelObj::disable(const std::string& name, int navButton) {
    // PanelDir disable (0x82374500): the focus moves on first.
    if (focus == name) navigate(navButton == 0 ? kPad_DDown : navButton);
    if (focus == name) focus.clear();
    disabled.insert(name);
    applyStates();
}

void PanelObj::navigate(int button) {
    // 0x82374160 / 0x82373A08 [C]: down / right follow nav_down / nav_right,
    // up / left the component that names this one; disabled ones are skipped.
    if (!view || focus.empty()) return;
    const auto navOf = [&](const std::string& n, bool down) -> std::string {
        const auto* c = view->find(n) ? view->find(n)->as<scene::UIComponent>() : nullptr;
        return c ? (down ? c->navDown : c->navRight) : std::string();
    };
    const auto inverse = [&](const std::string& n, bool down) -> std::string {
        for (const scene::UIComponent* c : view->milo()->root().all<scene::UIComponent>())
            if ((down ? c->navDown : c->navRight) == n) return c->name;
        return {};
    };
    std::string cur = focus;
    for (int guard = 0; guard < 64; ++guard) {
        std::string next;
        switch (button) {
        case kPad_DDown: next = navOf(cur, true); break;
        case kPad_DRight: next = navOf(cur, false); break;
        case kPad_DUp: next = inverse(cur, true); break;
        case kPad_DLeft: next = inverse(cur, false); break;
        default: return;
        }
        if (next.empty() || next == focus) return;
        cur = next;
        if (!isDisabled(cur) && (!view->find(cur) || view->showing(cur))) break;
    }
    if (cur != focus && !isDisabled(cur)) setFocus(cur, true);
}

void PanelObj::selectStart(ComponentObj* c, int button, int player) {
    // UIComponent select (0x8236F058): Selecting, component_select_start
    // now, component_select after select_frames (30 frames, ui_objects.dta).
    selecting = c->name();
    selectEndMs = ui.nowMs() + 500;
    applyStates();
    ui.route("component_select_start", {Value::obj(c), Value::integer(button), Value::integer(player)});
}

void PanelObj::pollSelect() {
    if (selecting.empty() || ui.nowMs() < selectEndMs) return;
    const std::string name = selecting;
    selecting.clear();
    applyStates();
    if (up && ui.current() && ui.current()->hasPanel(this)) ui.route("component_select", {Value::obj(component(name))});
}

bool PanelObj::buttonDown(int button, int player) {
    if (!selecting.empty()) return true;  // one selection at a time
    if (ComponentObj* c = focused(); c && !isDisabled(c->name()) && c->buttonDown(button, player)) return true;
    if (isDirection(button)) {
        const std::string before = focus;
        navigate(button);
        return focus != before;
    }
    return false;
}

bool PanelObj::buttonUp(int, int) { return false; }

void PanelObj::draw() {
    if (showing && path.rfind("world/", 0) == 0) ui.host().worldDraw();
    if (!view || !showing) return;
    ui.drawView(*view);
}

bool PanelObj::handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    const auto argStr = [&](size_t i) { return i < args.size() ? nameOf(args[i]) : std::string(); };
    r = Value::integer(0);
    if (msg == "is_up") return r = Value::integer(up ? 1 : 0), true;
    if (msg == "load") return load(), true;
    if (msg == "unload") return unload(), true;
    if (msg == "enter") return enter(), true;
    if (msg == "exit") return exit(), true;
    // The world panel's directory is the venue's `world` (finish_load names it so).
    ScriptObject* world = path.rfind("world/", 0) == 0 ? ui.host().worldScript() : nullptr;
    if (msg == "loaded_dir") return r = Value::obj(world ? world : this), true;
    if (msg == "set_name") return true;  // {{$this loaded_dir} set_name world main}
    if (msg == "paused") return r = Value::integer(paused ? 1 : 0), true;
    if (msg == "set_paused") return paused = !args.empty() && args[0].truthy(), true;
    if (msg == "set_showing") return showing = !args.empty() && args[0].truthy(), true;
    if (msg == "showing") return r = Value::integer(showing ? 1 : 0), true;
    if (msg == "set_focus") {
        const std::string n = argStr(0);
        if (n.empty() || component(n)) setFocus(n, false);
        return true;
    }
    if (msg == "focus_name") return r = Value::symbol(focus), true;
    if (msg == "find") return r = Value::obj(component(argStr(0))), true;
    if (msg == "enable") return enable(argStr(0)), true;
    if (msg == "disable") return disable(argStr(0), args.size() > 1 && args[1].isNumber() ? args[1].integerValue() : kPad_DDown), true;
    if (msg == "foreach_player") return r = ui.foreachPlayer(args), true;
    // The world panel passes the rest to its directory, the venue (world_panel
    // enter: {handle ($this single_player)}).
    if (world) {
        r = in.send(world, msg, args, false);
        return !r.isUnhandled();
    }
    return false;
}

Value PanelObj::getProp(Interp& in, const std::string& key) { return ScriptObject::getProp(in, key); }

// ---------------------------------------------------------------- screens

ScreenObj::ScreenObj(UiManager& u, const std::string& name, const std::string& cls) : ScriptObject(name, cls), ui(u) {}

std::vector<PanelObj*> ScreenObj::panels() const {
    std::vector<PanelObj*> out;
    const DataNode* p = handler("panels");
    if (!p) return out;
    for (size_t k = 1; k < p->children.size(); ++k) {
        const DataNode& n = p->children[k];
        if (!n.isText()) continue;
        if (PanelObj* panel = ui.panel(n.s)) out.push_back(panel);
        else ui.logOnce("ui: screen " + name() + ": no panel " + n.s);
    }
    return out;
}

PanelObj* ScreenObj::focusPanel() const {
    const DataNode* f = handler("focus");
    if (f && f->children.size() > 1 && f->children[1].isText()) return ui.panel(f->children[1].s);
    return nullptr;
}

bool ScreenObj::flag(const std::string& key, bool def) const {
    if (auto it = props.find(key); it != props.end()) return it->second.truthy();
    const DataNode* h = handler(key);
    if (!h || h->children.size() < 2) return def;
    const DataNode& v = h->children[1];
    if (v.type == DataNode::Type::Int) return v.i != 0;
    if (v.isText()) return v.s != "FALSE" && v.s != "0";
    return def;
}

bool ScreenObj::hasPanel(const PanelObj* p) const {
    const auto ps = panels();
    return std::find(ps.begin(), ps.end(), p) != ps.end();
}

bool ScreenObj::inBackChain(const ScreenObj* s) const {
    const ScreenObj* cur = back;
    for (int guard = 0; cur && guard < 64; ++guard) {
        if (cur == s) return true;
        if (!cur->flag("allow_back", true)) return false;
        cur = cur->back;
    }
    return false;
}

bool ScreenObj::nativeButtonDown(int button, int player) {
    // GHScreen button_down (0x8229EED0) [C]: kPad_Tri with allow_back and a
    // back screen: the focus panel may answer first, else back to it, and
    // screen_back to the UI.
    ScreenObj* target = back;
    if (Value b = const_cast<ScreenObj*>(this)->getProp(ui.interp(), "back_screen"); b.isText() || b.type == Value::Type::Object)
        if (ScreenObj* s = ui.screen(nameOf(b))) target = s;
    if (button != kPad_Tri || !flag("allow_back", true) || !target) return false;
    if (PanelObj* fp = focusPanel()) {
        Value r = ui.callScript(fp, "button_down", {Value::integer(button), Value::integer(player)});
        if (!r.isUnhandled()) return true;
    }
    ui.gotoScreen(target->name(), true);
    ui.route("screen_back", {});
    return true;
}

bool ScreenObj::handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) {
    r = Value::integer(0);
    if (msg == "backwards_anim") return ui.setBackwardsAnim(), true;
    if (msg == "reset_ambient" || msg == "turn_off_ambient" || msg == "wait_for_sfx") return true;
    if (msg == "enable" || msg == "disable") {
        if (!args.empty() && args[0].type == Value::Type::Object)
            if (auto* c = dynamic_cast<ComponentObj*>(args[0].object)) {
                if (msg == "enable") c->panel->enable(c->name());
                else c->panel->disable(c->name(), args.size() > 1 && args[1].isNumber() ? args[1].integerValue() : kPad_DDown);
            }
        return true;
    }
    if (msg == "focus_name") {
        PanelObj* p = focusPanel();
        return r = Value::symbol(p ? p->focus : std::string()), true;
    }
    if (msg == "is_up") return r = Value::integer(ui.current() == this ? 1 : 0), true;
    if (msg == "foreach_player") return r = ui.foreachPlayer(args), true;
    // Other messages go on to the focus panel: its natives (credits_screen's
    // {$this num_lines}), then its script (sel_guitar_new_screen's {$this
    // choose_skins}) [I: UIScreen::Handle's forwarding].
    if (PanelObj* fp = focusPanel()) {
        if (fp->handleNative(ui.interp(), msg, args, r)) return true;
        if (fp->handler(msg)) return r = ui.callScript(fp, msg, args), true;
    }
    return false;
}

}  // namespace ui
