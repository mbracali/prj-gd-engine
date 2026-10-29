// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/ui_manager.h"

#include "audio/audio_device.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <unordered_set>

#include "data/game_data.h"
#include "input/input.h"
#include "render/canvas.h"
#include "render/scene_gpu.h"
#include "render/ui_view.h"
#include "scene/milo.h"
#include "scene/ui_objects.h"
#include "scene/ui_styles.h"
#include "script/tasks.h"
#include "ui/campaign.h"
#include "ui/char_select.h"
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
    if (token == "port_gamepads") return "GAMEPADS";  // the port's main-menu help item
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
    if (const char* l = SDL_getenv("GD03_UILOG")) interp_.logReport = *l && *l != '0';
    save_ = std::make_unique<SaveFile>(SDL_getenv("GD03_SAVE_DIR") ? std::string(SDL_getenv("GD03_SAVE_DIR")) : std::string());
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
    // $syscfg: the system config (config/gh2.dta and its includes), read
    // by {find $syscfg ...} (modes.dta practice's pre_processed_audio,
    // cheats_funcs.dta) [C data].
    interp_.var("syscfg") = interp_.toValue(*boot_);
    // The players' and their matchers' handlers: gh2.dta (player (handlers
    // ...)) (Player, 0x822C9720) and (beatmatch (player_matcher_handlers
    // ...)) (PlayerMatcher, 0x822E44B0) [C].
    {
        const DataNode* player = boot_->find("player");
        const DataNode* ph = player ? player->find("handlers") : nullptr;
        const DataNode* beat = boot_->find("beatmatch");
        const DataNode* mh = beat ? beat->find("player_matcher_handlers") : nullptr;
        for (int p = 0; p < 2; ++p) {
            if (ScriptObject* o = interp_.findObject("player" + std::to_string(p)); o && ph) o->addHandlers(*ph, 1);
            if (ScriptObject* o = interp_.findObject("player_matcher" + std::to_string(p)); o && mh) o->addHandlers(*mh, 1);
        }
    }
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
    // The long cheats: ((<buttons>) {script}) each, the buttons kPad_*
    // through config/long_cheats.dta's G_GREEN.. macros (HX_XBOX).
    if (const DataNode* lc = boot_->find("long_cheats"))
        for (size_t k = 1; k < lc->children.size(); ++k) {
            const DataNode& c = lc->children[k];
            if (!c.isList() || c.children.size() < 2 || !c.children[0].isList()) continue;
            LongCheat cheat;
            for (const DataNode& b : c.children[0].children)
                cheat.buttons.push_back(b.type == DataNode::Type::Int ? b.i : b.isText() ? macroInt(b.s, -1) : -1);
            cheat.script = &c.children[1];
            if (!cheat.buttons.empty()) longCheats_.push_back(std::move(cheat));
        }
    // The port's band in every save (Career::kGlitches): GH2's default look,
    // main.dta's reset_player_settings (punk1) with its first guitar.
    {
        Session::PlayerCfg cfg;
        chars::validate(*this, cfg);
        career_.glitchesLook = {cfg.character, cfg.outfit, cfg.guitar, cfg.skin};
        career_.clear();
        campaign::unlockGlitches(*this);  // everything unlocked (a fresh save's default: not marked)
        career_.needsSave = false;
    }
    if (firstScreen_.empty()) firstScreen_ = interp_.var("first_screen").str();
    lastWall_ = double(SDL_GetTicksNS()) / 1e6;
    return true;
}

void UiManager::start(bool noSplashes, const std::string& screen) {
    if (!screen.empty()) firstScreen_ = screen;
    lastWall_ = double(SDL_GetTicksNS()) / 1e6;
    if (!noSplashes && !headless_) {
        // App init (0x82120208): ui/pub_splash.milo, activision_splash.milo,
        // harmonix_splash.milo, each a still picture while the game loads [C].
        for (const char* s : {"ui/gen/pub_splash.milo_ps2", "ui/gen/activision_splash.milo_ps2", "ui/gen/harmonix_splash.milo_ps2"})
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
    // Audio Settings' poster (game_settings.milo) stands 40 units higher
    // than Video Settings' (its mesh at z 20, not -20) and is taller: its top
    // and top tape (z 223) were above the 16:9 picture's top (z ~216). The
    // port lowers the poster, its tape, title and sliders by 24 units (the
    // walls stay) so all of it shows [I: the original's framing not
    // compared with a capture].
    if (p->name() == "audio_settings_panel" && p->view && p->view->milo()) {
        scene::Directory& root = p->view->milo()->root();
        if (!root.get("vs_poster.mesh") || audioPosterLowered_.count(&root)) return;
        audioPosterLowered_.insert(&root);
        for (const auto& o : root.objects) {
            if (o->name.rfind("vs_wall", 0) == 0) continue;
            if (auto* t = const_cast<scene::Trans*>(gfx::transOf(o.get())); t && t->parent.empty()) t->local.pos[2] -= 24;
        }
        return;
    }
    // Options gets Enhanced Options (the port's own entry; [I] not in GH2)
    // after Video Settings, as a copy of its button. GH2's six buttons keep
    // their order and their staggered x; the seven share the six's column
    // down to a row below Credits, each scaled to the closer rows. The rows
    // keep options.milo's own gaps (audio 42, video 12.5, data -20, memory
    // card -52, bonus -84, credits -109 [C data]: Credits sits 25 below
    // Bonus Videos, not 32); Enhanced Options takes a copy of the video ->
    // data gap.
    if (p->name() != "options_panel" || !p->view || !p->view->milo()) return;
    scene::Directory& root = p->view->milo()->root();
    if (root.get("port_enhanced.btn")) return;
    auto button = [&](const char* name) { return root.get(name) ? root.get(name)->as<scene::BandButton>() : nullptr; };
    scene::BandButton* video = button("video_settings.btn");
    scene::BandButton* audio = button("op_audio.btn");
    scene::BandButton* credit = button("op_credit.btn");
    scene::BandButton* data = button("op_data.btn");
    if (!video || !audio || !credit || !data) return;
    auto b = std::make_unique<scene::BandButton>(*data);  // Manage Band's, the flattest tilt
    b->name = "port_enhanced.btn";
    b->localize = false, b->text = "Enhanced Options", b->textToken.clear();
    b->band.width = 250;
    b->trans.local.pos[0] = 30;
    b->navDown = video->navDown;
    video->navDown = b->name;
    const float enhancedGap = video->trans.local.pos[2] - data->trans.local.pos[2];
    root.add(std::move(b));
    const char* const order[] = {"op_audio.btn", "video_settings.btn", "port_enhanced.btn", "op_data.btn",
                                 "memory_card.btn",  "op_bonus.btn", "op_credit.btn"};
    // Each row's distance below Audio Settings in GH2's layout.
    std::vector<std::pair<scene::BandButton*, float>> rows;
    float at = 0, prev = audio->trans.local.pos[2];
    for (const char* n : order) {
        scene::BandButton* bb = button(n);
        if (!bb) continue;
        if (bb != audio) {
            const bool port = std::string(n) == "port_enhanced.btn";
            at += port ? enhancedGap : prev - bb->trans.local.pos[2];
            if (!port) prev = bb->trans.local.pos[2];
        }
        rows.push_back({bb, at});
    }
    const float top = audio->trans.local.pos[2] + 4, bottom = credit->trans.local.pos[2] - 26;
    const float fit = at > 0 ? (top - bottom) / at : 1;
    const float step = (top - bottom) / 6, scale = 0.8f * std::min(1.0f, step / 31.5f);  // GH2's rows are 31.5 apart
    for (auto& [bb, d] : rows) {
        bb->trans.local.pos[2] = top - d * fit;
        bb->band.width *= scale, bb->band.height *= scale, bb->band.textSize *= scale;
    }
}

void UiManager::defer(std::function<void()> fn) { deferred_.push_back(std::move(fn)); }

void UiManager::applyOptions() {
    const Career::Options& o = career_.options;
    const auto v = [](int idx) { return float(std::clamp(idx, 0, 11)) / 11.0f; };
    host_.applyOptions(v(o.bandVolumeIdx), v(o.guitarVolumeIdx), v(o.fxVolumeIdx), o.lefty[0], o.lefty[1], o.syncOffsetMs);
    audiodev::setMono(!o.stereo);
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
        if (current_->routeNative(msg, args, r)) return r;
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

void UiManager::playSfx(const std::string& name, float volumeDb, float pan, float transpose) {
    // GH2's Synth (audio/synth.h, through the host): a name no bank has is
    // reported once.
    if (!host_.playSfxArgs(name, volumeDb, pan, transpose)) interp_.onStub("synth", "sfx " + name, Value::integer(0));
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
    pushing_ = true;
    gotoScreen(name);
}

void UiManager::popScreen(const std::string& name) {
    if (!name.empty() && screen(name)) return gotoScreen(name, true);
    if (stack_.empty()) return;
    ScreenObj* s = stack_.back();
    stack_.pop_back();
    popping_ = true;
    gotoScreen(s->name(), true);
}

void UiManager::loadPanel(PanelObj* p) {
    if (!p->loaded) p->load();
}

void UiManager::panelViewLoaded(PanelObj* p) {
    // It joins the shared metacam where it is (setMetacamFrame).
    viewPanels_.insert(p->name());
    for (const auto& [group, t] : metacam_) p->view->setFrame(group, t * p->view->endFrame(group));
    // And the shared directories' animations where they are.
    for (const auto& [o, nf] : sharedFrames_)
        if (p->view->find(nf.first) == o) p->view->setFrame(nf.first, nf.second);
}

// One animation frame on a panel's view. GH2 loads a milo once (ObjDirPtr
// reuses a loaded directory), so the subdirectories panels share, such as
// meta_bricks.milo under main, options, training and most menus, are one
// object: its meta_light_off.mnm (ui_exit_back) darkens every panel that
// shows the bricks until a ui_enter_back plays meta_light_on.mnm. The port's
// views pose their own copies, so the frame goes to every view showing the
// same object (before, a panel left by Back kept its own copy dark and came
// back darker the next time).
void UiManager::setViewFrame(PanelObj* p, const std::string& object, float frame) {
    if (!p || !p->view) return;
    p->view->setFrame(object, frame);
    const scene::Object* o = p->view->find(object);
    if (!o || o->dir == (p->view->milo() ? &p->view->milo()->root() : nullptr)) return;  // the panel's own
    sharedFrames_[o] = {object, frame};
    for (const std::string& name : viewPanels_)
        if (PanelObj* q = panel(name); q && q != p && q->view && q->view->find(object) == o) q->view->setFrame(object, frame);
}

// GH2 counts each panel's screens (0x823768E0): a panel no screen holds
// unloads (UIPanel Unload 0x82376C90 frees its directory), and with it the
// last reference to a shared subdirectory such as meta_bricks.milo; the next
// panel that loads it gets the file's state again, its light on. The port
// keeps its panels loaded, so once a transition is over, a shared object no
// panel of the current (or a pushed) screen shows forgets its frames: the
// career's difficulty screen, left with Back (meta_light_off) for Choose
// Band, came back dark until a later Back played meta_light_on
// (gd-backlog GD03-077 / GD03-081) [C: refcounts; I: the moment, the end
// of the transition].
void UiManager::releaseSharedFrames() {
    if (sharedFrames_.empty() || !current_) return;
    std::vector<ScreenObj*> held(stack_.begin(), stack_.end());
    held.push_back(current_);
    for (auto it = sharedFrames_.begin(); it != sharedFrames_.end();) {
        const scene::Object* o = it->first;
        bool shown = false;
        for (ScreenObj* s : held)
            for (PanelObj* p : s->panels()) shown = shown || (p->view && p->view->find(it->second.first) == o);
        if (shown) {
            ++it;
            continue;
        }
        std::unordered_set<const scene::Object*> objects;
        if (o->dir)
            for (const auto& x : o->dir->objects) objects.insert(x.get());
        objects.insert(o);
        for (const std::string& name : viewPanels_)
            if (PanelObj* q = panel(name); q && q->view && q->view->find(it->second.first) == o) q->view->pose().forget(objects);
        it = sharedFrames_.erase(it);
    }
}

void UiManager::enterPanel(PanelObj* p) {
    if (p->up) return;
    p->enter();
}

void UiManager::exitPanel(PanelObj* p) {
    if (!p->up) return;
    for (const char* ev : {"ui_exit", back_ ? "ui_exit_back" : "ui_exit_forward"})
        if (p->view)
            for (const gfx::UiView::Trigger& t : p->view->triggers(ev)) setViewFrame(p, t.anim->name, t.endFrame);
    p->exit();
}

void UiManager::beginTransition(ScreenObj* next, bool back) {
    previous_ = current_;
    next_ = next;
    back_ = back || backwardsAnim_ || (current_ && current_->inBackChain(next));
    backwardsAnim_ = false;
    history_.push_back(next->name());
    if (const char* l = SDL_getenv("GD03_UILOG"); l && *l && *l != '0') SDL_Log("ui: goto_screen %s%s", next->name().c_str(), back_ ? " (back)" : "");
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
    const bool push = pushing_, pop = popping_;
    pushing_ = popping_ = false;
    if (from && !push) {
        callScript(from, "exit");
        for (PanelObj* p : from->panels())
            if (!to->hasPanel(p)) exitPanel(p);
    }
    // A plain goto from a pushed screen leaves the screens below too [I].
    if (!push && !pop)
        while (!stack_.empty()) {
            ScreenObj* below = stack_.back();
            stack_.pop_back();
            if (below == to) continue;
            callScript(below, "exit");
            for (PanelObj* p : below->panels())
                if (!to->hasPanel(p)) exitPanel(p);
        }
    current_ = to;
    // The keyboard's split layout (P2 on H J K L ;, N / M, B, O) while the
    // two-player menus need a second controller: from the controller select
    // with fewer than two pads, until the main menu [I: the port's].
    if (to->name() == "multi_controller_select_screen") {
        int pads = 0;
        for (const Input::Device& d : input_.devices()) pads += d.id != "keyboard" ? 1 : 0;
        if (pads < 2) input_.setSplitKeyboard(true);
    } else if (to->name() == "main_screen") {
        input_.setSplitKeyboard(false);
    }
    if (pop) {
        // The screen below was never exited: it goes on as it was.
        updateHelpbar();
        route("screen_change", {Value::obj(to), Value::obj(from), Value::integer(1)});
        phase_ = Phase::Complete;
        return;
    }
    // GHScreen Enter (0x822A0408): the back screen is the one we came from,
    // when that one allows going back (and we are not going back).
    if (!back_ && from && from != to && from->flag("allow_back", true)) to->back = from;
    const bool animIn = to->flag("animate_transition", true) && !headless_;
    to->focusOverride = nullptr;
    for (PanelObj* p : to->panels()) loadPanel(p);
    // The help bar shows the screen's (helpbar ...) (none: empty); the
    // panels' and the screen's enter scripts may set it after (the dialogs).
    updateHelpbar();
    entered_.clear();
    to->nativeEnter();
    for (PanelObj* p : to->panels()) {
        if (p->up) continue;
        enterPanel(p);
        entered_.push_back(p);
    }
    if (!animIn) resetMetacam();
    to->nativeEntered();
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
    resetMetacam();
    releaseSharedFrames();
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

// GH2 keeps one metacam (ui/metacam.milo, loaded once by 0x822A1070) that
// every panel's camera and bricks hang from; the port's panel views each
// pose their own copy, so the metacam's frames go to all of them. Posing
// only some copies left the others where their last transition put them:
// going back, the main menu came back with the camera of its forward exit
// still applied (off to one side) under the backward entrance.
void UiManager::setMetacamFrame(const std::string& group, float t) {
    auto it = std::find_if(metacam_.begin(), metacam_.end(), [&](const auto& g) { return g.first == group; });
    if (it != metacam_.end()) metacam_.erase(it);
    metacam_.emplace_back(group, t);
    for (const std::string& name : viewPanels_)
        if (PanelObj* p = panel(name); p && p->view) p->view->setFrame(group, t * p->view->endFrame(group));
}

// After every transition (and at boot) GH2 sets trans_in_forward.grp to its
// end, the metacam's resting pose (0x8229FC98 / 0x822A1070).
void UiManager::resetMetacam() {
    metacam_.clear();
    setMetacamFrame("trans_in_forward.grp", 1);
}

void UiManager::applyTransitionPose(float t) {
    // GHScreen's metacam groups (0x8229FB20): one group per phase,
    // trans_out_* leaving the old screen, trans_in_* entering the new one.
    const std::string dir = back_ ? "backward" : "forward";
    if (phase_ == Phase::Out && current_) setMetacamFrame("trans_out_" + dir + ".grp", t);
    else if (phase_ == Phase::In && current_) setMetacamFrame("trans_in_" + dir + ".grp", t);
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
    if (!hb || !current_) return;
    const DataNode* h = current_->handler("helpbar");
    // A pushed screen without the help bar sets the one below's (store_bought_screen).
    if (!current_->hasPanel(hb) && (!hb->up || !h)) return;
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
    // The port's main menu also offers GAMEPADS on the yellow fret, before
    // the strum bar, as gd01's does (not in GH2).
    if (current_->name() == "main_screen" && display.type == Value::Type::Array && display.array) {
        std::vector<Value> items = *display.array;
        auto strum = std::find_if(items.begin(), items.end(), [](const Value& e) {
            return e.type == Value::Type::Array && e.array && !e.array->empty() && (*e.array)[0].str() == "strum";
        });
        items.insert(strum, Value::list({Value::symbol("fret3"), Value::symbol("port_gamepads")}));
        display = Value::list(std::move(items));
    }
    setHelpbarDisplay(*this, display);
}

// ---------------------------------------------------------------- per frame

double UiManager::clockSeconds(int units) const {
    switch (units) {
    case kTaskUISeconds: return uiMs_ / 1000.0;
    case kTaskBeats: return host_.gameLoaded() ? host_.gameBeat() : 0.0;
    default: return taskSeconds();
    }
}

// kTaskSeconds: in a game GH2's Game sets the TaskMgr's seconds (and beats)
// from the song: the lead-in at the start (0x822CBD18), then every poll
// (0x822CDEB8, through 0x823118F8) [C]. So the venue's, the band's and the
// faces' kTaskSeconds tasks (the FaceFX lip sync and expressions) run on the
// song clock, not on the time spent in the menus. Outside a game: the
// menus' own seconds.
double UiManager::taskSeconds() const {
    return host_.gameLoaded() ? host_.gameTimeMs() / 1000.0 : secondsMs_ / 1000.0;
}

void UiManager::animate(PanelObj* panel, const std::string& object, float from, float to, float seconds, int units, const std::string& task) {
    stopAnimation(panel, object);
    if (!panel || !panel->view) return;
    setViewFrame(panel, object, from);
    if (seconds <= 0) {
        setViewFrame(panel, object, to);
        return;
    }
    anims_.push_back({panel, object, from, to, clockSeconds(units), double(seconds), units, task});
    // A named task is an object while it runs ({exists unlock_anim}, unlock_venue_panel's poll).
    if (!task.empty() && !namedTasks_.count(task)) {
        auto o = std::make_unique<ScriptObject>(task, "Task");
        interp_.registerObject(o.get());
        namedTasks_[task] = std::move(o);
    }
}

void UiManager::endTask(const std::string& task) {
    if (task.empty()) return;
    for (const Anim& a : anims_)
        if (a.task == task) return;
    if (auto it = namedTasks_.find(task); it != namedTasks_.end()) {
        interp_.unregisterObject(it->second.get());
        namedTasks_.erase(it);
    }
}

void UiManager::stopAnimation(PanelObj* panel, const std::string& object) {
    std::vector<std::string> ended;
    for (const Anim& a : anims_)
        if (a.panel == panel && a.object == object) ended.push_back(a.task);
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [&](const Anim& a) { return a.panel == panel && a.object == object; }),
                 anims_.end());
    for (const std::string& t : ended) endTask(t);
}

void UiManager::updateAnims() {
    for (size_t i = 0; i < anims_.size();) {
        Anim& a = anims_[i];
        const double t = std::clamp((clockSeconds(a.units) - a.start) / a.length, 0.0, 1.0);
        setViewFrame(a.panel, a.object, a.from + float(t) * (a.to - a.from));
        if (t >= 1) {
            const std::string task = a.task;
            anims_.erase(anims_.begin() + long(i));
            endTask(task);
        } else {
            ++i;
        }
    }
}

void UiManager::update() {
    const double wall = double(SDL_GetTicksNS()) / 1e6;
    frameDt_ = std::clamp(wall - lastWall_, 0.0, 100.0);
    lastWall_ = wall;
    uiMs_ += frameDt_;
    if (!(host_.gameLoaded() && host_.gamePaused())) secondsMs_ += frameDt_;
    if (gamepadsAtMs_ > 0 && wall >= gamepadsAtMs_) openGamepads();
    if (splash_ >= 0) {
        splashMs_ += frameDt_;
        if (splashMs_ >= 3000) {
            splashMs_ = 0;
            if (++splash_ >= int(splashViews_.size())) skipSplashes();
        }
        if (splash_ >= 0) return;
    }
    TaskMgr& tm = interp_.tasks();
    tm.setTime(kTaskUISeconds, uiMs_ / 1000.0);
    tm.setTime(kTaskSeconds, taskSeconds());
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
    } else {
        for (Held& h : held_) h.ms = 0, h.repeating = false;  // a held direction restarts after a transition (gd01)
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
    // The yellow fret on the main menu opens GAMEPADS (gd01's help item).
    if (e.action == Action::Fret2 && e.down) yellowButton();
    int pad = -1;
    switch (e.action) {
    case Action::Up: pad = kPad_DUp; break;
    case Action::Down: pad = kPad_DDown; break;
    case Action::Left: pad = kPad_DLeft; break;
    case Action::Right: pad = kPad_DRight; break;
    // The keyboard's Enter is always Start on a text entry (the owner's rule,
    // every build): it finishes the band's name, which a keyboard otherwise
    // cannot (the original has no keyboard); X / green still add letters.
    // (Its release goes as the same button as its press.)
    case Action::Confirm:
        if (e.enterKey && e.down) enterIsStart_ = textEntryFocused();
        pad = e.enterKey && enterIsStart_ ? kPad_Start : kPad_X;
        break;
    case Action::Back: pad = kPad_Tri; break;
    case Action::Start: pad = kPad_Start; break;
    // The frets are the 360 guitar's buttons (hx_guitar_xbox_new: A B Y X LB
    // [C]): green selects and red goes back on every controller, as in gd01
    // (App::uiButton). A joypad's yellow / blue / orange are RB / RT / A,
    // not its Y / X / LB, so only its green / red count (A already selects).
    case Action::Fret0: pad = kPad_X; break;                        // green
    case Action::Fret1: pad = kPad_Tri; break;                      // red
    case Action::Fret2: pad = e.joypad ? -1 : kPad_Circle; break;   // yellow
    case Action::Fret3: pad = e.joypad ? -1 : kPad_Square; break;   // blue
    case Action::Fret4: pad = e.joypad ? -1 : kPad_L1; break;       // orange
    case Action::StarPower: pad = kPad_Select; break;
    default: break;  // strum comes as Up / Down
    }
    // The frets enter the cheats' codes as the PS2 guitar's buttons
    // (long_cheats.dta: G_GREEN kPad_R2, G_RED kPad_Circle, G_YELLOW
    // kPad_Tri, G_BLUE kPad_X, G_ORANGE kPad_Square [C data]), whatever
    // menu button the fret also is; the menu button then feeds nothing.
    fretFedCheat_ = false;
    if (e.down && splash_ < 0 && e.action >= Action::Fret0 && e.action <= Action::Fret4) {
        static constexpr int kFretPads[5] = {kPad_R2, kPad_Circle, kPad_Tri, kPad_X, kPad_Square};
        feedCheats(kFretPads[int(e.action) - int(Action::Fret0)]);
        fretFedCheat_ = true;
    }

    if (pad < 0) return;
    // Two players: the scripts' $player_num is the player whose pad acted
    // (gamecfg set_player), except on the controller select itself, where
    // it is the pad [I: GH2 maps pads to players after set_player].
    int player = e.player;
    if (gameCfgValue(*this, "num_players").integerValue() > 1 && currentScreen() != "multi_controller_select_screen")
        for (int p = 0; p < 2; ++p)
            if (session_->players[size_t(p)].padnum == e.player) {
                player = p;
                break;
            }
    if (e.down) buttonDown(pad, player);
    else buttonUp(pad, player);
}

bool UiManager::textEntryFocused() {
    PanelObj* fp = current_ ? current_->focusPanel() : nullptr;
    ComponentObj* c = fp && fp->up ? fp->focused() : nullptr;
    return c && c->isTextEntry();
}

void UiManager::buttonDown(int button, int player) {
    player = std::clamp(player, 0, 1);
    if (button >= 0 && button < 16) ++presses_[size_t(button)];
    if (isDirection(button)) held_[size_t(player)] = {button, 0, false};
    if (splash_ < 0 && !fretFedCheat_) feedCheats(button);
    fretFedCheat_ = false;
    if (splash_ >= 0) {
        // The original's splashes can't be skipped [I]; the port skips them
        // all on select, back or Start, as gd01 skips its boot logos.
        if (button == kPad_X || button == kPad_Tri || button == kPad_Circle || button == kPad_Start) skipSplashes();
        return;
    }
    routeButton(button, player);
}

// CheatsManager (0x8235B5B0) [C]: a press more than 750 ms after the last
// one starts the buttons afresh (and completes no cheat); the last 16 are
// kept; when the latest ones are a cheat's buttons its script runs (the
// script checks it is allowed: the main menu, no transition) and the
// buttons start afresh.
void UiManager::feedCheats(int button) {
    const double now = double(SDL_GetTicksNS()) / 1e6;
    if (button != kPad_Tri) gamepadsAtMs_ = -1;  // another button (yellow feeds kPad_Tri): a cheat's code, not GAMEPADS
    bool fresh = false;
    if (!cheatButtons_.empty() && now - cheatLastMs_ > 750) cheatButtons_.clear(), fresh = true;
    cheatLastMs_ = now;
    cheatButtons_.push_back(button);
    while (cheatButtons_.size() > 16) cheatButtons_.pop_front();
    if (fresh) return;
    for (const LongCheat& c : longCheats_) {
        if (c.buttons.size() > cheatButtons_.size()) continue;
        if (!std::equal(c.buttons.begin(), c.buttons.end(), cheatButtons_.end() - std::ptrdiff_t(c.buttons.size()))) continue;
        cheatButtons_.clear();
        gamepadsAtMs_ = -1;
        try {
            interp_.eval(*c.script);
        } catch (const std::exception& e) {
            SDL_Log("ui: long cheat: %s", e.what());
        }
        return;
    }
}

// The scripts' cheat variables (config/long_cheats.dta, reset_all_cheats).
bool UiManager::cheatOn(const std::string& id) const {
    auto& in = const_cast<Interp&>(interp_);
    if (id == "cheat_unlock_all") return in.var("cheat_enabled_unlock_all").truthy();
    if (id == "cheat_air_guitar") return in.var("cheat_guitar").str() == "air_guitar";
    if (id == "cheat_performance_mode") return in.var("cheat_enabled_performance_mode").truthy();
    if (id == "cheat_hyper_speed_mode") return in.var("cheat_enabled_hyper_speed_mode").truthy();
    if (id == "cheat_flaming_head") return in.var("cheat_enabled_flaming_head").truthy();
    if (id == "cheat_eyeball_head_crowd") return in.var("cheat_crowd_heads").str() == "eyeball_crowd_heads";
    if (id == "cheat_monkey_head_crowd") return in.var("cheat_crowd_heads").str() == "monkey_crowd_heads";
    return false;
}

void UiManager::setCheat(const std::string& id, bool on) {
    if (cheatOn(id) == on) return;
    if (id == "cheat_unlock_all") {
        // The code's script turns it on; reset_all_cheats off.
        interp_.var("cheat_enabled_unlock_all") = Value::integer(on ? 1 : 0);
        interp_.var("disable_save") = Value::integer(on ? 1 : 0);
        interp_.var("disable_career") = Value::integer(on ? 1 : 0);
        session_->allAccess = on;
    } else if (id == "cheat_air_guitar") {
        interp_.var("cheat_guitar") = on ? Value::symbol("air_guitar") : Value::integer(0);
    } else if (id == "cheat_performance_mode" || id == "cheat_hyper_speed_mode" || id == "cheat_flaming_head") {
        const std::string var = id == "cheat_flaming_head" ? "cheat_enabled_flaming_head" : "cheat_enabled_" + id.substr(6);
        interp_.var(var) = Value::integer(on ? 1 : 0);
    } else if (id == "cheat_eyeball_head_crowd" || id == "cheat_monkey_head_crowd") {
        interp_.var("cheat_crowd_heads") = on ? Value::symbol(id == "cheat_eyeball_head_crowd" ? "eyeball_crowd_heads" : "monkey_crowd_heads") : Value::integer(0);
    } else {
        return;
    }
    playSfx(on ? "cheat" : "cheat_off");
}

// The port's GAMEPADS on the main menu's yellow fret. GH2's cheat codes are
// entered there with the same frets (CheatsManager), so it opens once no
// other button follows within the codes' 750 ms; a code that completes, or
// any other button, cancels it. The help bar's item (a click) opens it at once.
void UiManager::yellowButton() {
    if (splash_ >= 0 || phase_ != Phase::Idle || currentScreen() != "main_screen") return;
    gamepadsAtMs_ = double(SDL_GetTicksNS()) / 1e6 + 750;
}

void UiManager::openGamepads() {
    gamepadsAtMs_ = -1;
    if (splash_ >= 0 || phase_ != Phase::Idle || !pending_.empty() || currentScreen() != "main_screen") return;
    playSfx("button_select");
    // Back returns to the main menu (which allows no back of its own, so the
    // screen would not take it as its back screen).
    if (ScreenObj* g = screen("gamepads_screen")) g->back = current_;
    gotoScreen("gamepads_screen");
}

bool UiManager::mouse(float x, float y, bool click) {
    if (splash_ >= 0) {
        if (click) skipSplashes();
        return click;
    }
    if (!current_ || phase_ != Phase::Idle || !pending_.empty()) return false;
    for (PanelObj* p : current_->panels())
        if (p->up && p->helpMouse(x, y, click)) return true;
    PanelObj* fp = current_->focusPanel();
    return fp && fp->up && fp->mouse(x, y, click);
}

void UiManager::wheel(int dir) {
    if (splash_ >= 0 || !current_ || phase_ != Phase::Idle || !pending_.empty()) return;
    const int b = dir < 0 ? kPad_DUp : kPad_DDown;
    buttonDown(b, 0);
    buttonUp(b, 0);
}

void UiManager::skipSplashes() {
    splash_ = -1, splashMs_ = 0;
    splashViews_.clear();
    gotoScreen(firstScreen_);
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
void UiManager::gameScript(const std::string& msg, std::vector<Value> args) { gameScriptImpl(*this, msg, std::move(args)); }

// ---------------------------------------------------------------- drawing

static void fillMask(Canvas& canvas_, const gfx::UiView::MaskDraw& m) {
    if (m.blend == 0) return;  // kBlendDest: nothing written
    const Uint8 c[4] = {Uint8(std::clamp(m.rgba[0], 0.0f, 1.0f) * 255), Uint8(std::clamp(m.rgba[1], 0.0f, 1.0f) * 255),
                        Uint8(std::clamp(m.rgba[2], 0.0f, 1.0f) * 255), Uint8(std::clamp(m.rgba[3], 0.0f, 1.0f) * 255)};
    // The rect is a fraction of the picture (4:3 or 16:9 in the frame).
    const float px = canvas_.pictureX0(), pw = canvas_.pictureW();
    const float x = px + m.rect[0] * pw, y = m.rect[1] * kScreenH, w = m.rect[2] * pw, h = m.rect[3] * kScreenH;
    const Vertex q[6] = {{x, y, 0, 0, 0, c[0], c[1], c[2], c[3]},         {x + w, y, 0, 1, 0, c[0], c[1], c[2], c[3]},
                         {x + w, y + h, 0, 1, 1, c[0], c[1], c[2], c[3]}, {x, y, 0, 0, 0, c[0], c[1], c[2], c[3]},
                         {x + w, y + h, 0, 1, 1, c[0], c[1], c[2], c[3]}, {x, y + h, 0, 0, 1, c[0], c[1], c[2], c[3]}};
    // Src (1) opaque, Add / SrcAlphaAdd (2, 4) additive, the rest alpha [I: subtract / multiply as alpha].
    const Blend b = m.blend == 1 ? Blend::Opaque : m.blend == 2 || m.blend == 4 ? Blend::Add : Blend::Alpha;
    canvas_.draw(q, 6, int(m.texture), b);
}

void UiManager::drawView(gfx::UiView& v) {
    gfx::Renderer& r = canvas_.renderer();
    canvas_.flush();
    if (!keepDepth_) r.device().clear(nullptr, true);
    keepDepth_ = false;
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    r.materials().setFrame(v.camera(), v.environment(), vw, vh);
    // The panel's own masks at their place in the draw order: what was drawn,
    // the mask over it, then the 3D frame again.
    v.maskNow = [&](const gfx::UiView::MaskDraw& m) {
        fillMask(canvas_, m);
        canvas_.flush();
        r.materials().setFrame(v.camera(), v.environment(), vw, vh);
    };
    v.draw(r.materials());
    v.maskNow = nullptr;
    drawMasks(v);
}


void UiManager::drawMasks(const gfx::UiView& v) {
    for (const gfx::UiView::MaskDraw& m : v.masks()) fillMask(canvas_, m);
    canvas_.flush();
}

void UiManager::drawSplash() {
    if (splash_ < 0 || splash_ >= int(splashViews_.size())) return;
    drawView(*splashViews_[size_t(splash_)]);
}

void UiManager::drawPanels(ScreenObj* s, bool outgoing) {
    if (!s) return;
    // The screens below a pushed one draw first [I: GH2 draws the stack].
    if (s == current_)
        for (ScreenObj* below : stack_)
            if (below != s)
                for (PanelObj* p : below->panels())
                    if (p->up && p->showing && !s->hasPanel(p)) p->draw();
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
        campaign::unlockGlitches(*this);  // an older Glitches band (or one just added) gets everything
        result = 0;
        return true;
    }
    result = corrupt ? macroInt("kMCCorrupt", -105) : macroInt("kMCFileNotFound", -301);
    return false;
}

}  // namespace ui
