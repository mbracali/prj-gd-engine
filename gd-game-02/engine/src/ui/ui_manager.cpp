// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/ui_manager.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

#include "data/game_data.h"
#include "input/input.h"
#include "render/canvas.h"
#include "render/scene_gpu.h"
#include "render/ui_view.h"
#include "scene/milo.h"
#include "scene/ui_objects.h"
#include "scene/ui_styles.h"
#include "script/tasks.h"
#include "ui/natives.h"
#include "ui/screen_text.h"
#include "ui/ui_host.h"
#include "ui/ui_objects.h"

namespace ui {

namespace {

bool isDirection(int b) { return b == kPad_DUp || b == kPad_DDown || b == kPad_DLeft || b == kPad_DRight; }

// The UI manager object ({ui goto_screen x}, handler 0x8236B8B0) with the
// (ui ...) list's handlers (the menu sounds).
class UiObj : public ScriptObject {
public:
    explicit UiObj(UiManager& ui) : ScriptObject("ui", "UIManager"), ui_(ui) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        const auto argStr = [&](size_t i) { return i < args.size() ? args[i].str() : std::string(); };
        r = Value::integer(0);
        if (msg == "goto_screen" || msg == "reset_screen") return ui_.gotoScreen(argStr(0)), true;
        if (msg == "push_screen") return ui_.pushScreen(argStr(0)), true;
        if (msg == "pop_screen") return ui_.popScreen(argStr(0)), true;
        if (msg == "current_screen") return r = Value::obj(ui_.current()), true;
        if (msg == "focus_panel") return r = Value::obj(ui_.focusPanel()), true;
        if (msg == "in_transition") return r = Value::integer(ui_.inTransition() ? 1 : 0), true;
        if (msg == "set_virtual_dpad" || msg == "use_joypad") return true;
        if (msg == "music_start") return true;  // the in-game music start (beatmatch); nothing for the UI
        return false;
    }

private:
    UiManager& ui_;
};

}  // namespace

UiManager::UiManager(const GameData& data, Canvas& canvas, UiHost& host, Input& input)
    : data_(data), canvas_(canvas), host_(host), input_(input), session_(std::make_unique<Session>()) {}

UiManager::~UiManager() {
    // Objects the interpreter owns refer to this manager; drop them first.
    interp_.resolver = nullptr;
    interp_.tasks().clear();
}

void UiManager::logOnce(const std::string& what) {
    if (logged_.insert(what).second) SDL_Log("%s", what.c_str());
}

std::string UiManager::localize(const std::string& token) const {
    if (token == "port_gamepads") return "GAMEPADS";  // the port's own help bar entry
    return locale_ ? locale_->text(token) : token;
}

int UiManager::macroInt(const std::string& name, int def) const {
    auto it = macros_.find(name);
    if (it == macros_.end() || it->second.children.empty()) return def;
    const DataNode& v = it->second.children[0];
    return v.type == DataNode::Type::Int ? v.i : v.type == DataNode::Type::Float ? int(v.f) : def;
}

const std::string& UiManager::savePath() const { return save_->path(); }

std::unique_ptr<gfx::UiView> UiManager::makeView(const std::string& path, std::string* error) {
    if (!data_.exists(path)) {
        if (error) *error = "missing";
        return nullptr;
    }
    if (headless_) {
        // Still parse it (the --uitest checks the components).
        try {
            library_->load(path);
        } catch (const std::exception& e) {
            if (error) *error = e.what();
        }
        return nullptr;
    }
    try {
        auto v = std::make_unique<gfx::UiView>(data_, *library_, *styles_, *locale_, *cache_);
        v->load(path);
        return v;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return nullptr;
    }
}

ScreenText& UiManager::screenText() { return *text_; }

bool UiManager::init(std::string& error, bool headless) {
    headless_ = headless;
    try {
        library_ = std::make_unique<scene::MiloLibrary>(data_);
        styles_ = std::make_unique<scene::UiStyles>();
        styles_->load(data_);
        locale_ = std::make_unique<scene::Locale>();
        locale_->load(data_, "eng");
        if (!headless_) {
            cache_ = std::make_unique<gfx::SceneCache>(canvas_.renderer().device());
            text_ = std::make_unique<ScreenText>(data_, *library_, *cache_);
        }
    } catch (const std::exception& e) {
        error = std::string("UI resources: ") + e.what();
        return false;
    }
    interp_.locale = locale_->strings();
    interp_.language = "eng";
    interp_.seed(uint32_t(std::time(nullptr)));
    if (const char* l = SDL_getenv("GD02_UILOG")) interp_.logReport = *l && *l != '0';
    save_ = std::make_unique<SaveFile>(SDL_getenv("GD02_SAVE_DIR") ? std::string(SDL_getenv("GD02_SAVE_DIR")) : std::string());
    for (int i = 0; i < Career::kHighScores; ++i) career_.dummyNames[size_t(i)] = localize("highscore_dummy_" + std::to_string(i));

    registerUiClasses(*this, interp_);
    installNatives(*this, interp_, natives_);
    auto uiObj = std::make_unique<UiObj>(*this);
    interp_.registerObject(uiObj.get());
    ScriptObject* uiRaw = uiObj.get();
    natives_.push_back(std::move(uiObj));
    interp_.resolver = [this](const std::string& name) { return resolve(name); };

    try {
        loader_ = std::make_unique<ScriptLoader>(data_);
        boot_ = &interp_.keep(loader_->load("config/gh2.dta"));
    } catch (const std::exception& e) {
        error = std::string("config/gh2.dta: ") + e.what();
        return false;
    }
    macros_ = loader_->macros;
    modes_ = boot_->find("modes");
    if (const DataNode* j = boot_->find("joypad")) {
        if (const DataNode* h = j->find("hold_ms")) holdMs_ = h->floatAt(1);
        if (const DataNode* r = j->find("repeat_ms")) repeatMs_ = r->floatAt(1);
    }
    const DataNode* uiList = boot_->find("ui");
    if (uiList) uiRaw->addHandlers(*uiList, 1);
    interp_.run(*boot_);
    for (const char* sub : {"system", "ui"}) {
        const DataNode* list = boot_->find(sub);
        const DataNode* init = list ? list->find("init") : nullptr;
        if (!init) {
            error = std::string("config/gh2.dta: no (") + sub + " (init ...))";
            return false;
        }
        interp_.runList(*list, 1);
        interp_.runList(*init, 1);
    }
    runPortScripts();
    if (firstScreen_.empty()) firstScreen_ = interp_.var("first_screen").str();
    lastWall_ = double(SDL_GetTicksNS()) / 1e6;
    return true;
}

void UiManager::start(bool skipSplashes, const std::string& screen) {
    if (!screen.empty()) firstScreen_ = screen;
    lastWall_ = double(SDL_GetTicksNS()) / 1e6;
    if (!skipSplashes && !headless_) {
        // App init (0x82120208): ui/pub_splash.milo, activision_splash.milo,
        // harmonix_splash.milo, each a still picture while the game loads [C].
        for (const char* s : {"ui/gen/pub_splash.milo_xbox", "ui/gen/activision_splash.milo_xbox", "ui/gen/harmonix_splash.milo_xbox"})
            if (auto v = makeView(s)) splashViews_.push_back(std::move(v));
        if (!splashViews_.empty()) {
            splash_ = 0, splashMs_ = 0;
            return;
        }
    }
    gotoScreen(firstScreen_);
}

extern const char* const kPortScreens;

void UiManager::runPortScripts() {
    // The port's own screens (ui/port_screens.cpp): the Gamepads screen,
    // and (port_patches ...) handlers added to the game's screens.
    try {
        const DataNode& tree = interp_.keep(loader_->preprocessTree(parseDtaText(kPortScreens), "ui/port.dta"));
        interp_.run(tree);
        if (const DataNode* patches = tree.find("port_patches"))
            for (size_t k = 1; k < patches->children.size(); ++k) {
                const DataNode& p = patches->children[k];
                if (!p.isList() || p.children.empty() || !p.children[0].isText()) continue;
                if (ScriptObject* o = interp_.findObject(p.children[0].s)) o->addHandlers(p, 1);
            }
    } catch (const std::exception& e) {
        SDL_Log("ui: the port's screens: %s", e.what());
    }
}

void UiManager::portPanelLoaded(PanelObj* p) {
    // Options gets a GAMEPADS button under CREDITS (a copy of it), reached
    // by its navigation (the port's own entry; [I] not in GH2).
    if (p->name() != "options_panel" || !p->view || !p->view->milo()) return;
    scene::Directory& root = p->view->milo()->root();
    if (root.get("port_gamepads.btn")) return;
    auto* credit = root.get("op_credit.btn") ? root.get("op_credit.btn")->as<scene::BandButton>() : nullptr;
    auto* bonus = root.get("op_bonus.btn") ? root.get("op_bonus.btn")->as<scene::BandButton>() : nullptr;
    if (!credit) return;
    auto b = std::make_unique<scene::BandButton>(*credit);
    b->name = "port_gamepads.btn";
    const float step = bonus ? credit->trans.local.pos[2] - bonus->trans.local.pos[2] : -25.0f;
    b->trans.local.pos[2] += step;
    b->navDown = credit->navDown;
    credit->navDown = b->name;
    b->localize = false, b->text = "GAMEPADS";
    root.add(std::move(b));
}

void UiManager::defer(std::function<void()> fn) { deferred_.push_back(std::move(fn)); }

void UiManager::applyOptions() {
    const Career::Options& o = career_.options;
    const auto v = [](int idx) { return float(std::clamp(idx, 0, 11)) / 11.0f; };
    host_.applyOptions(v(o.bandVolumeIdx), v(o.guitarVolumeIdx), v(o.fxVolumeIdx), o.lefty[0], o.lefty[1], o.syncOffsetMs);
}

// ---------------------------------------------------------------- objects

ScreenObj* UiManager::screen(const std::string& name) const { return dynamic_cast<ScreenObj*>(interp_.findObject(name)); }

PanelObj* UiManager::panel(const std::string& name) const { return dynamic_cast<PanelObj*>(interp_.findObject(name)); }

PanelObj* UiManager::focusPanel() const { return current_ ? current_->focusPanel() : nullptr; }

ScriptObject* UiManager::resolve(const std::string& name) {
    // A component of the panel whose code runs ($this), then of the current
    // screen's panels (focus panel first), then of any loaded panel.
    if (name.empty()) return nullptr;
    const Value self = interp_.var("this");
    PanelObj* thisPanel = nullptr;
    if (self.type == Value::Type::Object) {
        thisPanel = dynamic_cast<PanelObj*>(self.object);
        if (!thisPanel)
            if (auto* c = dynamic_cast<ComponentObj*>(self.object)) thisPanel = c->panel;
    }
    if (thisPanel)
        if (ComponentObj* c = thisPanel->component(name)) return c;
    if (ScreenObj* s = next_ ? next_ : current_) {
        if (PanelObj* fp = s->focusPanel(); fp && !fp->acceptsAnyName())
            if (ComponentObj* c = fp->component(name)) return c;
        for (PanelObj* p : s->panels())
            if (!p->acceptsAnyName())
                if (ComponentObj* c = p->component(name)) return c;
    }
    if (thisPanel == nullptr)
        if (ScreenObj* s = dynamic_cast<ScreenObj*>(self.object))
            for (PanelObj* p : s->panels())
                if (!p->acceptsAnyName())
                    if (ComponentObj* c = p->component(name)) return c;
    return nullptr;
}

Value UiManager::callScript(ScriptObject* o, const std::string& msg, std::vector<Value> args) {
    if (!o) return Value::unhandled();
    const DataNode* h = o->handler(msg);
    if (!h) return Value::unhandled();
    try {
        return interp_.callHandler(o, *h, args);
    } catch (const std::exception& e) {
        interp_.onError(o->name() + " " + msg + ": " + e.what());
        return Value::integer(0);
    }
}

Value UiManager::route(const std::string& msg, std::vector<Value> args) {
    // UIManager::Handle: the ui object's handlers (the menu sounds), the
    // screen, then its focus panel.
    Value r = callScript(interp_.findObject("ui"), msg, args);
    if (!r.isUnhandled()) return r;
    if (current_) {
        r = callScript(current_, msg, args);
        if (!r.isUnhandled()) return r;
        if (PanelObj* fp = current_->focusPanel()) {
            r = callScript(fp, msg, args);
            if (!r.isUnhandled()) return r;
        }
    }
    return Value::unhandled();
}

Value UiManager::foreachPlayer(const std::vector<Value>& args) {
    // {x foreach_player $p body...}: $p by name, then the body (lazy code).
    if (args.empty()) return Value::integer(0);
    const std::string var = args[0].str().size() > 1 && args[0].str()[0] == '$' ? args[0].str().substr(1) : args[0].str();
    const int n = std::max(1, gameCfgValue(*this, "num_players").integerValue());
    Value last = Value::integer(0);
    for (int p = 0; p < n; ++p) {
        Value saved = interp_.var(var);
        interp_.var(var) = Value::obj(interp_.findObject("player" + std::to_string(p)));
        for (size_t k = 1; k < args.size(); ++k) last = interp_.evalValue(args[k]);
        interp_.var(var) = saved;
    }
    return last;
}

void UiManager::playSfx(const std::string& name) {
    // GH2's sound banks (sfx/gen/*_bank.milo_xbox: SynthSample records,
    // XMA data) are not decoded yet: the sounds are reported once.
    interp_.onStub("synth", "sfx " + name, Value::integer(0));
}

// ---------------------------------------------------------------- screens

std::string UiManager::currentScreen() const {
    if (splash_ >= 0) return "(splash)";
    return current_ ? current_->name() : std::string();
}

bool UiManager::inTransition() const { return phase_ == Phase::Out || phase_ == Phase::In || !pending_.empty(); }

bool UiManager::inGame() const {
    PanelObj* g = panel("game");
    return g && g->up;
}

void UiManager::gotoScreen(const std::string& name, bool back) {
    if (!screen(name)) {
        interp_.onUnknown("screen " + name);
        return;
    }
    pending_ = name;
    pendingBack_ = back;
}

void UiManager::pushScreen(const std::string& name) {
    if (current_) stack_.push_back(current_);
    gotoScreen(name);
}

void UiManager::popScreen(const std::string& name) {
    if (!name.empty() && screen(name)) return gotoScreen(name, true);
    if (stack_.empty()) return;
    ScreenObj* s = stack_.back();
    stack_.pop_back();
    gotoScreen(s->name(), true);
}

void UiManager::loadPanel(PanelObj* p) {
    if (!p->loaded) p->load();
}

void UiManager::enterPanel(PanelObj* p) {
    if (p->up) return;
    p->enter();
}

void UiManager::exitPanel(PanelObj* p) {
    if (!p->up) return;
    for (const char* ev : {"ui_exit", back_ ? "ui_exit_back" : "ui_exit_forward"})
        if (p->view)
            for (const gfx::UiView::Trigger& t : p->view->triggers(ev)) p->view->setFrame(t.anim->name, t.endFrame);
    p->exit();
}

void UiManager::beginTransition(ScreenObj* next, bool back) {
    previous_ = current_;
    next_ = next;
    back_ = back || backwardsAnim_ || (current_ && current_->inBackChain(next));
    backwardsAnim_ = false;
    history_.push_back(next->name());
    if (const char* l = SDL_getenv("GD02_UILOG"); l && *l && *l != '0') SDL_Log("ui: goto_screen %s%s", next->name().c_str(), back_ ? " (back)" : "");
    const bool animOut = current_ && current_->flag("animate_transition", true) && !headless_;
    if (animOut) {
        // The outgoing panels' ui_exit triggers.
        for (PanelObj* p : current_->panels())
            if (p->up && !next->hasPanel(p) && p->view)
                for (const char* ev : {"ui_exit", back_ ? "ui_exit_back" : "ui_exit_forward"})
                    for (const gfx::UiView::Trigger& t : p->view->triggers(ev))
                        animate(p, t.anim->name, 0, t.endFrame, float(transitionMs_ / 1000.0), kTaskUISeconds);
        phase_ = Phase::Out;
        phaseMs_ = 0;
        phaseLen_ = transitionMs_;
        return;
    }
    switchScreens();
}

void UiManager::switchScreens() {
    ScreenObj* from = current_;
    ScreenObj* to = next_;
    if (from) callScript(from, "exit");
    if (from)
        for (PanelObj* p : from->panels())
            if (!to->hasPanel(p)) exitPanel(p);
    current_ = to;
    // GHScreen Enter (0x822A0408): the back screen is the one we came from,
    // when that one allows going back (and we are not going back).
    if (!back_ && from && from != to && from->flag("allow_back", true)) to->back = from;
    const bool animIn = to->flag("animate_transition", true) && !headless_;
    for (PanelObj* p : to->panels()) loadPanel(p);
    // The help bar shows the screen's (helpbar ...) (none: empty); the
    // panels' and the screen's enter scripts may set it after (the dialogs).
    updateHelpbar();
    entered_.clear();
    for (PanelObj* p : to->panels()) {
        if (p->up) continue;
        enterPanel(p);
        entered_.push_back(p);
        if (p->view && !animIn) p->view->setFrame("trans_in_forward.grp", p->view->endFrame("trans_in_forward.grp"));
    }
    callScript(to, "enter");
    route("screen_change", {Value::obj(to), Value::obj(from), Value::integer(back_ ? 1 : 0)});
    if (animIn) {
        phase_ = Phase::In;
        phaseMs_ = 0;
        phaseLen_ = transitionMs_;
        applyTransitionPose(0);
    } else {
        phase_ = Phase::Complete;
    }
}

void UiManager::finishTransition() {
    ScreenObj* to = current_;
    phase_ = Phase::Idle;
    next_ = nullptr;
    if (!to) return;
    // TRANSITION_COMPLETE_MSG to the screen and its panels.
    const std::vector<Value> args{Value::obj(to), Value::obj(previous_)};
    callScript(interp_.findObject("ui"), "transition_complete", args);
    callScript(to, "transition_complete", args);
    if (current_ == to)
        for (PanelObj* p : to->panels())
            if (p->up) callScript(p, "transition_complete", args);
}

void UiManager::applyTransitionPose(float t) {
    // GHScreen's metacam groups (0x822A1070): trans_out_* on the outgoing
    // panels, trans_in_* on the incoming ones; panels in both stay.
    const std::string dir = back_ ? "backward" : "forward";
    if (phase_ == Phase::Out && current_) {
        for (PanelObj* p : current_->panels())
            if (p->view && !(next_ && next_->hasPanel(p))) p->view->setFrame("trans_out_" + dir + ".grp", t * p->view->endFrame("trans_out_" + dir + ".grp"));
    } else if (phase_ == Phase::In && current_) {
        for (PanelObj* p : entered_)
            if (p->view)
                p->view->setFrame("trans_in_" + dir + ".grp", t * p->view->endFrame("trans_in_" + dir + ".grp"));
    }
}

void UiManager::updateTransition() {
    if (phase_ == Phase::Out || phase_ == Phase::In) {
        phaseMs_ += frameDt_;
        const float t = float(std::clamp(phaseMs_ / std::max(1.0, phaseLen_), 0.0, 1.0));
        applyTransitionPose(t);
        if (phaseMs_ >= phaseLen_) {
            if (phase_ == Phase::Out) switchScreens();
            else phase_ = Phase::Complete;
        }
        return;
    }
    if (phase_ == Phase::Complete) {
        finishTransition();
        return;
    }
    if (!pending_.empty()) {
        ScreenObj* s = screen(pending_);
        const bool back = pendingBack_;
        pending_.clear();
        pendingBack_ = false;
        if (s) beginTransition(s, back);
    }
}

void UiManager::updateHelpbar() {
    PanelObj* hb = panel("helpbar");
    if (!hb || !current_ || !current_->hasPanel(hb)) return;
    const DataNode* h = current_->handler("helpbar");
    Value display = Value::list();
    if (h) {
        PanelObj* fp = current_->focusPanel();
        const std::string focus = fp ? fp->focus : std::string();
        const DataNode* pick = nullptr;
        for (size_t k = 1; k < h->children.size(); ++k) {
            const DataNode& e = h->children[k];
            if (!e.isList() || e.children.size() < 2 || !e.children[0].isText()) continue;
            if (e.children[0].s == focus) pick = &e;
            else if (e.children[0].s == "default" && !pick) pick = &e;
        }
        if (pick) display = interp_.toValue(pick->children[1]);
    }
    setHelpbarDisplay(*this, display);
}

// ---------------------------------------------------------------- per frame

double UiManager::clockSeconds(int units) const {
    switch (units) {
    case kTaskUISeconds: return uiMs_ / 1000.0;
    case kTaskBeats: return host_.gameLoaded() ? host_.gameBeat() : 0.0;
    default: return secondsMs_ / 1000.0;
    }
}

void UiManager::animate(PanelObj* panel, const std::string& object, float from, float to, float seconds, int units) {
    stopAnimation(panel, object);
    if (!panel || !panel->view) return;
    panel->view->setFrame(object, from);
    if (seconds <= 0) {
        panel->view->setFrame(object, to);
        return;
    }
    anims_.push_back({panel, object, from, to, clockSeconds(units), double(seconds), units});
}

void UiManager::stopAnimation(PanelObj* panel, const std::string& object) {
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [&](const Anim& a) { return a.panel == panel && a.object == object; }),
                 anims_.end());
}

void UiManager::updateAnims() {
    for (size_t i = 0; i < anims_.size();) {
        Anim& a = anims_[i];
        const double t = std::clamp((clockSeconds(a.units) - a.start) / a.length, 0.0, 1.0);
        if (a.panel->view) a.panel->view->setFrame(a.object, a.from + float(t) * (a.to - a.from));
        if (t >= 1) anims_.erase(anims_.begin() + long(i));
        else ++i;
    }
}

void UiManager::update() {
    const double wall = double(SDL_GetTicksNS()) / 1e6;
    frameDt_ = std::clamp(wall - lastWall_, 0.0, 100.0);
    lastWall_ = wall;
    uiMs_ += frameDt_;
    if (!(host_.gameLoaded() && host_.gamePaused())) secondsMs_ += frameDt_;
    if (splash_ >= 0) {
        splashMs_ += frameDt_;
        if (splashMs_ >= 3000) {
            splashMs_ = 0;
            if (++splash_ >= int(splashViews_.size())) {
                splash_ = -1;
                splashViews_.clear();
                gotoScreen(firstScreen_);
            }
        }
        if (splash_ >= 0) return;
    }
    TaskMgr& tm = interp_.tasks();
    tm.setTime(kTaskUISeconds, uiMs_ / 1000.0);
    tm.setTime(kTaskSeconds, secondsMs_ / 1000.0);
    tm.setTime(kTaskTutorialSeconds, secondsMs_ / 1000.0);
    tm.setTime(kTaskBeats, host_.gameLoaded() ? host_.gameBeat() : 0.0);
    tm.poll();
    if (!deferred_.empty()) {
        auto run = std::move(deferred_);
        deferred_.clear();
        for (auto& fn : run) fn();
    }
    updateAnims();
    updateTransition();
    if (current_ && phase_ == Phase::Idle) {
        for (PanelObj* p : current_->panels())
            if (p->up) p->poll(frameDt_);
        if (current_ && current_->handler("poll")) callScript(current_, "poll");
        updateRepeat(frameDt_);
    }
}

void UiManager::updateRepeat(double dtMs) {
    // JoypadClient: a held direction repeats after hold_ms, then every
    // repeat_ms (the focus panel's (joypad ...) overrides gh2.dta's).
    float hold = holdMs_, rep = repeatMs_;
    if (PanelObj* fp = focusPanel()) fp->repeatTiming(hold, rep);
    for (int p = 0; p < 2; ++p) {
        Held& h = held_[size_t(p)];
        if (h.button < 0) continue;
        h.ms += dtMs;
        if (!h.repeating && h.ms >= hold) {
            h.repeating = true, h.ms = 0;
            routeButton(h.button, p);
        } else if (h.repeating && h.ms >= rep) {
            h.ms -= rep;
            routeButton(h.button, p);
        }
    }
}

// ---------------------------------------------------------------- input

void UiManager::action(const InputEvent& e) {
    int pad = -1;
    switch (e.action) {
    case Action::Up: pad = kPad_DUp; break;
    case Action::Down: pad = kPad_DDown; break;
    case Action::Left: pad = kPad_DLeft; break;
    case Action::Right: pad = kPad_DRight; break;
    case Action::Confirm: pad = kPad_X; break;
    case Action::Back: pad = kPad_Tri; break;
    case Action::Start: pad = kPad_Start; break;
    case Action::Fret2: pad = kPad_Circle; break;   // yellow
    case Action::Fret3: pad = kPad_Square; break;   // blue
    case Action::Fret4: pad = kPad_L1; break;       // orange
    case Action::StarPower: pad = kPad_Select; break;
    default: break;  // green / red come as Confirm / Back; strum as Up / Down
    }
    if (pad < 0) return;
    if (e.down) buttonDown(pad, e.player);
    else buttonUp(pad, e.player);
}

void UiManager::buttonDown(int button, int player) {
    player = std::clamp(player, 0, 1);
    if (isDirection(button)) held_[size_t(player)] = {button, 0, false};
    routeButton(button, player);
}

void UiManager::routeButton(int button, int player) {
    if (splash_ >= 0 || !current_ || phase_ != Phase::Idle || !pending_.empty()) return;
    const std::vector<Value> args{Value::integer(button), Value::integer(player)};
    // The ui object, the screen's script, GHScreen's auto back, the focus
    // panel's script, then PanelDir (the focused component, navigation).
    if (!callScript(interp_.findObject("ui"), "button_down", args).isUnhandled()) return;
    ScreenObj* s = current_;
    if (!callScript(s, "button_down", args).isUnhandled()) return;
    if (s != current_ || !pending_.empty()) return;
    if (s->nativeButtonDown(button, player)) return;
    PanelObj* fp = s->focusPanel();
    if (!fp || !fp->up) return;
    if (!callScript(fp, "button_down", args).isUnhandled()) return;
    if (s != current_ || !pending_.empty()) return;
    const std::string before = fp->focus;
    if (fp->buttonDown(button, player) && fp->focus != before && s->handler("helpbar")) updateHelpbar();
}

void UiManager::buttonUp(int button, int player) {
    player = std::clamp(player, 0, 1);
    if (held_[size_t(player)].button == button) held_[size_t(player)] = {};
    if (splash_ >= 0 || !current_ || phase_ != Phase::Idle) return;
    const std::vector<Value> args{Value::integer(button), Value::integer(player)};
    if (!callScript(current_, "button_up", args).isUnhandled()) return;
    if (PanelObj* fp = current_->focusPanel(); fp && fp->up) {
        callScript(fp, "button_up", args);
        fp->buttonUp(button, player);
    }
}

void UiManager::gameEvent(const std::string& msg, std::vector<Value> args) { gameEventImpl(*this, msg, std::move(args)); }

// ---------------------------------------------------------------- drawing

void UiManager::drawView(gfx::UiView& v) {
    gfx::Renderer& r = canvas_.renderer();
    canvas_.flush();
    r.device().clear(nullptr, true);
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    r.materials().setFrame(v.camera(), v.environment(), vw, vh);
    v.draw(r.materials());
    drawMasks(v);
}

void UiManager::drawMasks(const gfx::UiView& v) {
    for (const gfx::UiView::MaskDraw& m : v.masks()) {
        const SDL_Color c{Uint8(std::clamp(m.rgba[0], 0.0f, 1.0f) * 255), Uint8(std::clamp(m.rgba[1], 0.0f, 1.0f) * 255),
                          Uint8(std::clamp(m.rgba[2], 0.0f, 1.0f) * 255), Uint8(std::clamp(m.rgba[3], 0.0f, 1.0f) * 255)};
        canvas_.fillRect(m.rect[0] * kScreenW, m.rect[1] * kScreenH, m.rect[2] * kScreenW, m.rect[3] * kScreenH, c);
    }
    canvas_.flush();
}

void UiManager::drawSplash() {
    if (splash_ < 0 || splash_ >= int(splashViews_.size())) return;
    drawView(*splashViews_[size_t(splash_)]);
}

void UiManager::drawPanels(ScreenObj* s, bool outgoing) {
    if (!s) return;
    for (PanelObj* p : s->panels()) {
        if (!p->up && !outgoing) continue;
        if (!p->showing) continue;
        p->draw();
    }
}

void UiManager::draw() {
    if (headless_) return;
    if (splash_ >= 0) {
        drawSplash();
    } else {
        drawPanels(current_, phase_ == Phase::Out);
    }
    canvas_.flush();
    if (text_) text_->collect();
}

bool UiManager::loadSave(int& result) {
    bool corrupt = false;
    if (save_->read(career_, corrupt)) {
        result = 0;
        return true;
    }
    result = corrupt ? macroInt("kMCCorrupt", -105) : macroInt("kMCFileNotFound", -301);
    return false;
}

}  // namespace ui
