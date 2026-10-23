#include "ui/ui_system.h"
#include "ui/pc_strings.h"
#include "ui/gamepad_panel.h"
#include "ui/select_panels.h"
#include "ui/career_objects.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "audio/crowd_audio.h"
#include "audio/synth.h"
#include "data/game_data.h"
#include "game/campaign.h"
#include "game/song_db.h"

namespace {

constexpr float kTransitionMs = 500;  // GHScreen transition anims (FUN_00143290)
constexpr int kSelectFrames = 10;     // buttonex (select_frames 10)

Value sym(const std::string& s) { return Value::symbol(s); }
Value unhandled() { return Value::symbol("kDataUnhandled"); }

// A literal property value: (key v) -> v, (key a b ...) -> (a b ...).
Value propValue(Interp& in, const DataNode& prop) {
    if (prop.children.size() == 2) return in.toValue(prop.children[1]);
    std::vector<Value> v;
    for (size_t k = 1; k < prop.children.size(); ++k) v.push_back(in.toValue(prop.children[k]));
    return Value::list(std::move(v));
}

std::vector<std::string> names(const Value& v) {
    std::vector<std::string> out;
    if (v.type == Value::Type::Array)
        for (const Value& e : *v.array) out.push_back(e.str());
    else if (v.isText())
        out.push_back(v.s);
    return out;
}

}  // namespace

// ---------------------------------------------------------------- objects

// A component of a panel's scene (ButtonEx, LabelEx, List ...).
class UiSystem::ComponentObj : public ScriptObject {
public:
    ComponentObj(UiSystem& ui, PanelObj& panel, const std::string& name, UiObj::Kind kind)
        : ScriptObject(name), ui_(ui), panel_(panel), kind_(kind) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& result) override;
    bool button(int b);  // focused component input
    // RndScrollList with the "song" style: one song_element.view row per
    // provider item, `spacing` apart going down (docs/ui_system.md 2).
    void drawList(const UiObj& list, const Mat4& vp, const Mat4& world);
    // RndSlider (docs/slider.md): track, pod, label in the slider's space.
    void drawSlider(const UiObj& slider, const Mat4& vp, const Mat4& world);
    void poll();
    UiObj::Kind kind() const { return kind_; }

    UiSystem& ui_;
    PanelObj& panel_;
    UiObj::Kind kind_;
    int selectFrames_ = 0;
    // List
    ScriptObject* provider_ = nullptr;
    int selected_ = 0;
    // TextEntry: the entered text, the letter being chosen, accepted.
    std::string entered_;
    int letter_ = 0;
    bool done_ = false;
    // Slider: value, step count, editing (the "selected" style state), whether
    // the pod was ever placed (until then it sits at its template x).
    int pos_ = 0, steps_ = 1;
    bool editing_ = false, posSet_ = false;
    // PictureToggle: on / off, shown as the style's tex_on / tex_off picture.
    bool on_ = false, toggleShown_ = false;
    void showToggle();
    std::string entryText() const;
    void showEntry();
};

class UiSystem::ScreenObj : public ScriptObject {
public:
    ScreenObj(UiSystem& ui, const std::string& name, const DataNode& def) : ScriptObject(name), ui_(ui) {
        for (size_t k = 2; k < def.children.size(); ++k) {
            const DataNode& p = def.children[k];
            if (!p.isList() || p.children.empty() || !p.children[0].isText()) continue;
            props[p.children[0].s] = propValue(ui.interp_, p);
        }
        addHandlers(def, 2);
    }
    std::vector<std::string> panelNames() { return names(props["panels"]); }
    bool flag(const char* key, bool def) {
        auto it = props.find(key);
        return it == props.end() ? def : it->second.truthy();
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& result) override;
    void poll(float dtMs) {
        for (size_t k = 0; k < tasks_.size();) {
            tasks_[k].remaining -= dtMs;
            if (tasks_[k].remaining > 0) {
                ++k;
                continue;
            }
            Task t = tasks_[k];
            tasks_.erase(tasks_.begin() + long(k));
            Value saved = ui_.interp_.var("this");
            ui_.interp_.var("this") = Value::obj(this);
            for (const DataNode* n : t.body) ui_.interp_.eval(*n);
            ui_.interp_.var("this") = saved;
        }
    }
    struct Task {
        float remaining;
        std::vector<const DataNode*> body;
    };
    std::vector<Task> tasks_;
    UiSystem& ui_;
    ScreenObj* back_ = nullptr;
};

// A (preserve $v ...) entry: the variable node arrives unevaluated (Code).
std::string preserveName(const Value& v) {
    std::string name = v.type == Value::Type::Code && v.code ? v.code->s : v.str();
    return !name.empty() && name[0] == '$' ? name.substr(1) : name;
}

// ThreadTask (0x241958 / Poll 0x241a40 / messages 0x241bd0): a script thread.
// Each poll runs its top-level statements in order until one yields: `wait C`
// re-runs the same statement next poll while C holds, `sleep ms` resumes after
// the current statement, `exit` ends it after the current statement returns.
struct ThreadTask : ScriptObject {
    explicit ThreadTask(UiSystem&) : ScriptObject("task") {}
    std::vector<const DataNode*> body;
    std::vector<std::pair<std::string, Value>> preserved;
    size_t index = 0;
    float wake = 0, deadline = -1;
    bool yield = false;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if (msg == "wait") {
            yield = !a.empty() && in.evalValue(a[0]).truthy();
            return true;
        }
        if (msg == "wait_timeout") {  // wait_timeout C ms [$var]
            if (deadline < 0) deadline = wake + (a.size() > 1 ? in.evalValue(a[1]).number() : 0);
            bool timedOut = deadline <= wake;
            yield = !timedOut && !a.empty() && in.evalValue(a[0]).truthy();
            if (!yield) {
                deadline = -1;
                if (a.size() > 2) in.var(a[2].str()) = Value::integer(timedOut ? 1 : 0);
            }
            return true;
        }
        if (msg == "sleep") {
            yield = true;
            ++index;
            wake += a.empty() ? 0 : in.evalValue(a[0]).number();
            skipAdvance = true;
            return true;
        }
        if (msg == "exit") {
            yield = true;
            index = body.size();
            skipAdvance = true;
            return true;
        }
        if (msg == "current") return r = Value::integer(int(index)), true;
        if (msg == "set_current") return index = size_t(a.empty() ? 0 : a[0].integerValue()), skipAdvance = true, true;
        return false;
    }
    bool skipAdvance = false;  // sleep / exit / set_current moved the index themselves
};

class UiSystem::PanelObj : public ScriptObject {
public:
    PanelObj(UiSystem& ui, const std::string& name, const DataNode& def) : ScriptObject(name), ui_(ui), def_(&def) {
        // {new_gh_panel name (prop ...) (handler ...) ...}
        for (size_t k = 2; k < def.children.size(); ++k) {
            const DataNode& p = def.children[k];
            if (!p.isList() || p.children.empty() || !p.children[0].isText()) continue;
            props[p.children[0].s] = propValue(ui.interp_, p);
        }
        addHandlers(def, 2);
        if (const DataNode* nav = def.find("navigator")) {
            for (const DataNode& n : nav->children) {
                if (!n.isList() || n.children.empty()) continue;
                std::vector<std::string> list;
                for (size_t k = 1; k < n.children.size(); ++k) list.push_back(n.children[k].s);
                if (n.children[0].s == "vertical") vertical_ = list;
                else if (n.children[0].s == "horizontal") horizontal_ = list;
                else if (n.children[0].s == "wrap" && n.children.size() > 1) wrap_ = n.children[1].s != "FALSE" && n.children[1].i != 0;
            }
        }
    }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& result) override;

    void load() {
        if (loaded_) return;
        loaded_ = true;
        std::string file = "ghui/" + name() + ".gh";
        if (auto it = props.find("rnd_file"); it != props.end()) {
            // A script expression is evaluated at each load (StatusPanel: {sprint "ghui/status_" {campaign status} ".gh"}).
            std::string f = ui_.interp_.evalValue(it->second).str();
            loadedFile_ = f;
            if (f.empty()) return;
            // "ghui/x.gh" or a name relative to ghui
            file = f.find('/') == std::string::npos ? "ghui/" + f : f;
            // UIPanel::Load: "dir/x.rnd" -> "dir/gen/x.rnd_ps2".
            if (file.size() > 4 && file.compare(file.size() - 4, 4, ".rnd") == 0)
                file = file.substr(0, file.rfind('/') + 1) + "gen/" + file.substr(file.rfind('/') + 1) + "_ps2";
        }
        if (!ui_.data_.exists(file)) return;
        std::string base = file.substr(file.rfind('/') + 1);
        base = base.substr(0, base.rfind('.'));
        try {
            scene_ = std::make_unique<Panel>(ui_.renderer_, ui_.data_, *ui_.res_, file.rfind("ghui/", 0) == 0 ? base : file,
                                             ui_.common_ ? &ui_.common_->scene() : nullptr);
        } catch (const std::exception& ex) {
            SDL_Log("ui: panel %s: %s", name().c_str(), ex.what());
            return;
        }
        if (native_) native_->setScene(scene_.get());
        if (name() == "helpbar") addFretIcons();
        // Upload the panel's textures now (B20): created on first draw, each
        // upload stalled the first frames of the screen's transition.
        for (const SceneObject& o : scene_->scene().objects())
            if (o.mat)
                for (const MatStage& st : o.mat->stages)
                    if (!st.tex.empty()) scene_->renderer().textureHandle(st.tex);
        if (name() == "options") addEnhancedButton();
        if (name() == "chooseprof" || name() == "loadprof") addGlitchesButton();
        // Enhanced Settings' guitar spins on black: some ambient so its unlit side still reads.
        if (name() == "enh_guitar_display")
            if (SceneObject* e = scene_->scene().find("guitar_setup.env"); e && e->environment)
                e->environment->ambient = {0.3f, 0.3f, 0.32f, 1};
        for (SceneObject& o : scene_->scene().objects())
            if (o.ui) components_[o.name] = std::make_unique<ComponentObj>(ui_, *this, o.name, o.ui->kind);
        scene_->setListDraw([this](const SceneObject& list, const Mat4& vp, const Mat4& world) {
            if (ComponentObj* c = component(list.name)) {
                if (list.ui->kind == UiObj::Kind::Slider) c->drawSlider(*list.ui, vp, world);
                else c->drawList(*list.ui, vp, world);
            }
        });
        view_ = base + ".view";
        if (!scene_->scene().find(view_))
            for (const SceneObject& o : scene_->scene().objects())
                if (o.view) {
                    view_ = o.name;
                    break;
                }
        Value r;
        bool h;
        ui_.send(this, "finish_load", {}, h);
    }

    void enter() {
        // StatusPanel_v13 re-evaluates its file on every load: a new status, a new page.
        if (name() == "status" && loaded_)
            if (auto it = props.find("rnd_file"); it != props.end() && ui_.interp_.evalValue(it->second).str() != loadedFile_) {
                scene_.reset();
                components_.clear();
                loaded_ = false;
            }
        load();
        if (name() == "status" && scene_) fillStatus();
        up_ = true;
        std::string f = focus_;
        if (f.empty() || (props.count("retain_last_focus") && !props["retain_last_focus"].truthy())) {
            if (auto it = props.find("focus"); it != props.end()) f = it->second.str();
        }
        focus_.clear();
        if (!f.empty()) setFocus(f);
        paused_ = false;
        if (native_) native_->enter();
        if (name() == "meta" && ui_.hooks_.menuMusicLoad) ui_.hooks_.menuMusicLoad();
        if ((name() == "endgame" || name() == "multi_compete") && scene_) ui_.loadWinnerPhoto(*scene_);
        bool h;
        ui_.send(this, "enter", {}, h);
        if (native_) native_->entered();
    }

    void exit() {
        up_ = false;
        // MetaPanel Exit (MetaPanel_v4): meta_music_stop.
        if (name() == "meta" && ui_.hooks_.menuMusic) ui_.hooks_.menuMusic(false);
        bool h;
        ui_.send(this, "exit", {}, h);
        if (native_) native_->exit();
    }

    void setFocus(const std::string& c) {
        if (c == focus_) return;
        std::string old = focus_;
        ui_.route("component_focus", {{"new_focus", sym(c)}, {"old_focus", sym(old)}, {"panel", Value::obj(this)},
                                      {"screen", Value::obj(ui_.current_)}});
        if (c == "cp_band8.btn") props["button_idx"] = Value::integer(Campaign::kGlitchesSlot);  // not in the script's switch
        if (scene_) {
            if (!old.empty() && scene_->state(old) != UiResources::Disabled) scene_->setState(old, UiResources::Normal);
            if (scene_->state(c) != UiResources::Disabled) scene_->setState(c, UiResources::Focused);
        }
        focus_ = c;
    }

    // Navigator (FUN_002074a0): the list holding the focus, wrapping by default,
    // skipping disabled components.
    bool navigate(int button) {
        const std::vector<std::string>* list = nullptr;
        int step = 0;
        if (button == Up || button == Down) list = &vertical_, step = button == Down ? 1 : -1;
        if (button == Left || button == Right) list = &horizontal_, step = button == Right ? 1 : -1;
        if (!list || list->empty()) return false;
        auto it = std::find(list->begin(), list->end(), focus_);
        if (it == list->end()) return false;
        int n = int(list->size()), i = int(it - list->begin());
        for (int k = 0; k < n; ++k) {
            i += step;
            if (wrap_) i = (i % n + n) % n;
            else if (i < 0 || i >= n) return false;
            if ((*list)[size_t(i)] == focus_) return false;
            if (!scene_ || scene_->state((*list)[size_t(i)]) != UiResources::Disabled) {
                setFocus((*list)[size_t(i)]);
                return true;
            }
        }
        return false;
    }

    // UIPanel Handle: navigator, script, focused component.
    // SliderPanel (0x151900): the slider being edited, if any.
    ComponentObj* editingSlider() {
        for (auto& [n, c] : components_)
            if (c->kind() == UiObj::Kind::Slider && c->editing_) return c.get();
        return nullptr;
    }

    bool handle(const std::string& msg, const std::map<std::string, Value>& vars) {
        // SliderPanel: Up / Down / Back go to the slider being edited before the navigator.
        if (msg == "button_down")
            if (ComponentObj* sl = editingSlider()) {
                int b = vars.at("button").integerValue();
                if (b == Up || b == Down || ui_.isBackButton(b)) return sl->button(b);
            }
        if (msg == "button_down" && native_) {
            auto pn = vars.find("player_num");
            if (native_->buttonDown(vars.at("button").integerValue(), pn == vars.end() ? 0 : pn->second.integerValue()))
                return true;
        }
        if (msg == "button_down" && navigate(vars.at("button").integerValue())) return true;
        if (msg == "button_down" && name() == "options" && focus_ == "op_enh.btn" && ui_.isSelectButton(vars.at("button").integerValue())) {
            ui_.interp_.send("synth", "play_sequence", {Value::symbol("button_select")});
            ui_.gotoScreen("enhanced_settings");
            return true;
        }
        bool handled = false;
        if (handler(msg)) {
            ui_.send(this, msg, vars, handled);
            if (handled) return true;
        }
        if (msg == "button_down" && !focus_.empty())
            if (auto it = components_.find(focus_); it != components_.end())
                return it->second->button(vars.at("button").integerValue());
        return false;
    }

    void poll(float dtMs) {
        if (paused_) return;  // UIScreen Poll skips paused panels (0x2085f0); they are still drawn
        if (native_) native_->poll(dtMs);
        clock_ += dtMs;
        pollThreads();
        for (size_t k = 0; k < tasks_.size();) {
            tasks_[k].remaining -= dtMs;
            if (tasks_[k].remaining <= 0) {
                Task t = tasks_[k];
                tasks_.erase(tasks_.begin() + long(k));
                Value saved = ui_.interp_.var("this");
                ui_.interp_.var("this") = Value::obj(this);
                for (const auto& [v, val] : t.preserved) ui_.interp_.var(v) = val;
                for (const DataNode* n : t.body) ui_.interp_.eval(*n);
                ui_.interp_.var("this") = saved;
            } else {
                ++k;
            }
        }
        bool h;
        if (handler("poll")) ui_.send(this, "poll", {}, h);
        for (auto& [n, c] : components_) c->poll();
        if (scene_ && !loops_.empty()) {  // 30 fps, wrapping at the anim's end
            loopFrame_ += dtMs * 0.03f;
            for (const std::string& o : loops_) {
                float end = scene_->endFrame(o);
                scene_->setFrame(o, end > 0 ? std::fmod(loopFrame_, end) : loopFrame_);
            }
        }
    }

    void draw() {
        if (scene_) scene_->clearHits();
        if (!showing_) return;
        if (native_) native_->drawBefore();
        if (scene_ && (!native_ || native_->drawsScene())) {
            if (name() == "helpbar") drawHelpbar();
            else scene_->draw(view_);
        }
        if (native_) native_->drawAfter();
    }

    // HelpBarPanel: (elem token) pairs (fret1 fret2 strum start), each an
    // icon (hb_<elem>.mat) then its localized label, from the screen's
    // (helpbar (default ...) (<component> ...)) or {helpbar set_display ...}.
    // Layout per FUN_00149d88 / the HBElement draws (docs/boot_profiles_memcard.md §4).
    void drawHelpbar();
    std::vector<std::pair<std::string, std::string>> helpItems();
    std::vector<std::pair<std::string, Panel::Hit>> helpElems_;  // mouse: each help bar element's rectangle
    Panel::Hit helpHit_;
    bool hasOverride_ = false;
    Value override_;

    ComponentObj* component(const std::string& n) {
        auto it = components_.find(n);
        return it == components_.end() ? nullptr : it->second.get();
    }
    // Any other object of the panel's scene (views, meshes) for set_showing etc.
    ScriptObject* sceneObject(const std::string& n);
    std::map<std::string, std::unique_ptr<ScriptObject>> proxies_;

    UiSystem& ui_;
    std::unique_ptr<Panel> scene_;
    std::map<std::string, std::unique_ptr<ComponentObj>> components_;
    std::vector<std::string> vertical_, horizontal_;
    const DataNode* def_;      // the panel's definition (kept alive in scripts_)
    std::string loadedFile_;  // the rnd_file the scene was loaded from

    // StatusPanel_v16 FinishLoad: the player's character picture into
    // status_char1.tex, then (text (status<N> (band|char (label l) [(token t)]) ...))
    // (FUN_00153018): the band name or the character's name, through the token's %s.
    void fillStatus();

    // The port's main-menu help items GAMEPADS (blue) and MODS (orange): the
    // disc has only the green and red help icons, so hb_fret4 / hb_fret5 are
    // the green one recoloured.
    void addFretIcons() {
        Scene& sc = scene_->scene();
        const SceneObject* tex = sc.find("hb_fret1.tex");
        const SceneObject* mat = sc.find("hb_fret1.mat");
        if (!tex || !tex->tex || !mat || !mat->mat || tex->tex->image.width <= 0) return;
        for (int f : {3, 4, 5}) {
            SceneObject t;
            t.type = "Tex", t.name = "hb_fret" + std::to_string(f) + ".tex";
            t.tex = std::make_unique<TexObj>(*tex->tex);
            auto& px = t.tex->image.rgba;
            for (size_t i = 0; i + 3 < px.size(); i += 4) {
                uint8_t r = px[i], g = px[i + 1], b = px[i + 2];
                if (f == 3) px[i] = uint8_t(std::min(255.0f, g * 1.1f)), px[i + 1] = g, px[i + 2] = uint8_t(b * 0.25f);  // green -> yellow
                else if (f == 4) px[i] = r, px[i + 1] = b, px[i + 2] = g;  // green -> blue
                else px[i] = uint8_t(std::min(255.0f, g * 1.25f)), px[i + 1] = uint8_t(std::min(255.0f, g * 0.72f)), px[i + 2] = uint8_t(b * 0.3f);  // green -> orange
            }
            SceneObject m;
            m.type = "Mat", m.name = "hb_fret" + std::to_string(f) + ".mat";
            m.mat = std::make_unique<MatObj>(*mat->mat);
            if (!m.mat->stages.empty()) m.mat->stages[0].tex = t.name;
            sc.add(std::move(t));
            sc.add(std::move(m));
        }
    }

    // The port's ninth band, The Glitches (Campaign::kGlitchesSlot), below the eight slots.
    void addGlitchesButton() {
        Scene& sc = scene_->scene();
        const SceneObject* b6 = sc.find("cp_band6.btn");
        const SceneObject* b7 = sc.find("cp_band7.btn");
        if (!b6 || !b7 || !b6->ui || !b7->ui) return;
        SceneObject o;
        o.type = b7->type, o.name = "cp_band8.btn";
        o.ui = std::make_unique<UiObj>(*b7->ui);
        o.ui->text = "The Glitches";
        o.ui->jitterSeed += 7;
        for (int k = 0; k < 3; ++k) {
            float d = b7->ui->trans.local.pos[size_t(k)] - b6->ui->trans.local.pos[size_t(k)];
            o.ui->trans.local.pos[size_t(k)] += d, o.ui->trans.world.pos[size_t(k)] += d;
        }
        if (!sc.add(std::move(o))) return;
        for (SceneObject& v : sc.objects())
            if (Drawable* d = v.drawable()) {
                auto it = std::find(d->draws.begin(), d->draws.end(), "cp_band7.btn");
                if (it != d->draws.end()) d->draws.insert(it + 1, "cp_band8.btn");
            }
        vertical_.push_back("cp_band8.btn");
    }
    // The port's options entry "Enhanced Settings" (enhanced_settings screen),
    // between Game Settings and Data Settings: a copy of op_data.btn in its
    // place, the entries below it one step (41 units) lower.
    void addEnhancedButton() {
        Scene& sc = scene_->scene();
        const SceneObject* src = sc.find("op_data.btn");
        if (!src || !src->ui) return;
        SceneObject o;
        o.type = src->type, o.name = "op_enh.btn";
        o.ui = std::make_unique<UiObj>(*src->ui);
        o.ui->text = "Enhanced Settings";
        o.ui->jitterSeed = 21;
        o.ui->trans.local.pos[0] = o.ui->trans.world.pos[0] = -80.0f;
        if (!sc.add(std::move(o))) return;
        for (const char* b : {"op_data.btn", "op_bonus.btn", "op_credit.btn"})
            if (SceneObject* x = sc.find(b); x && x->ui) x->ui->trans.local.pos[2] -= 41, x->ui->trans.world.pos[2] -= 41;
        for (SceneObject& v : sc.objects())
            if (Drawable* d = v.drawable()) {
                auto it = std::find(d->draws.begin(), d->draws.end(), "op_data.btn");
                if (it != d->draws.end()) d->draws.insert(it, "op_enh.btn");
            }
        auto g = std::find(vertical_.begin(), vertical_.end(), "op_game.btn");
        if (g != vertical_.end()) vertical_.insert(g + 1, "op_enh.btn");
    }
    bool wrap_ = true, loaded_ = false, up_ = false, showing_ = true;
    std::string focus_, view_;
    struct Task {
        float remaining;
        std::vector<const DataNode*> body;
        std::vector<std::pair<std::string, Value>> preserved;
    };
    std::vector<Task> tasks_;
    std::vector<std::unique_ptr<ThreadTask>> threads_;
    float clock_ = 0;  // the panel's TaskMgr time (stops while paused)
    void pollThreads() {
        size_t n = threads_.size();  // threads made during this poll start next poll
        for (size_t k = 0; k < n && k < threads_.size(); ++k) {
            ThreadTask* t = threads_[k].get();
            if (t->index >= t->body.size() || t->wake > clock_) continue;
            t->yield = false;
            t->wake = clock_;
            Value savedTask = ui_.interp_.var("task");
            std::vector<std::pair<std::string, Value>> saved;
            for (const auto& [v, val] : t->preserved) saved.push_back({v, ui_.interp_.var(v)}), ui_.interp_.var(v) = val;
            ui_.interp_.var("task") = Value::obj(t);
            try {
                while (t->index < t->body.size()) {
                    t->skipAdvance = false;
                    ui_.interp_.eval(*t->body[t->index]);
                    if (t->yield) break;
                    if (!t->skipAdvance) ++t->index;
                }
            } catch (const std::exception& ex) {
                SDL_Log("ui: %s thread: %s", name().c_str(), ex.what());
                t->index = t->body.size();
            }
            ui_.interp_.var("task") = savedTask;
            for (const auto& [v, val] : saved) ui_.interp_.var(v) = val;
        }
        threads_.erase(std::remove_if(threads_.begin(), threads_.end(),
                                      [](const std::unique_ptr<ThreadTask>& t) { return t->index >= t->body.size(); }),
                       threads_.end());
    }
    std::vector<std::string> loops_;  // animate_forever_30fps objects
    float loopFrame_ = 0;
    std::unique_ptr<NativePanel> native_;  // the panel class's C++ side (select_panels.h)
    bool paused_ = false;
};

// `ui` (UIManager_t1_v1 0x20aa28).
class UiSystem::ManagerObj : public ScriptObject {
public:
    explicit ManagerObj(UiSystem& ui) : ScriptObject("ui"), ui_(ui) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        if (msg == "music_start") return true;  // UIManager: tells the screens; nothing listens in GH1's ghui
        if ((msg == "goto_screen" || msg == "transition_to_screen") && !a.empty()) {
            ui_.gotoScreen(a[0].str());
            return true;
        }
        if (msg == "push_screen" && !a.empty()) {
            if (ui_.current_) ui_.stack_.push_back(ui_.current_);
            ui_.gotoScreen(a[0].str());
            return true;
        }
        if (msg == "pop_screen") {
            if (!a.empty()) ui_.gotoScreen(a[0].str());
            else if (!ui_.stack_.empty()) {
                ScreenObj* s = ui_.stack_.back();
                ui_.stack_.pop_back();
                ui_.gotoScreen(s->name());
            }
            return true;
        }
        if (msg == "screen" && !a.empty()) return r = Value::obj(ui_.screen(a[0].str())), true;
        if (msg == "panel" && !a.empty()) return r = Value::obj(ui_.panel(a[0].str())), true;
        if (msg == "component" && !a.empty()) {
            for (auto& [n, p] : ui_.panels_)
                if (ComponentObj* c = p->component(a[0].str())) return r = Value::obj(c), true;
            return r = Value::integer(0), true;
        }
        if (msg == "current_screen") return r = Value::obj(ui_.current_), true;
        if (msg == "focus_panel") return r = Value::obj(ui_.focusPanel()), true;
        if (msg == "frame") return r = Value::integer(int(ui_.frameMs_)), true;
        if (msg == "in_transition") return r = Value::integer(ui_.phase_ != Phase::Idle ? 1 : 0), true;
        if (msg == "use_joypad") return true;
        return false;
    }
    UiSystem& ui_;
};

// Objects the scripts call that the port does not implement yet: answer
// harmlessly (0 / handled).
// `synth` (Synth_v1): play_sequence name (volume)(transpose)(pan), stop_all_sfx, pause_all_sfx.
class SynthObj : public ScriptObject {
public:
    explicit SynthObj(Synth* synth) : ScriptObject("synth"), synth_(synth) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if (!synth_) return true;
        if (msg == "play_sequence" && !a.empty()) {
            float vol = 1, tr = 0, pan = 0;
            for (size_t k = 1; k < a.size(); ++k) {
                if (a[k].type != Value::Type::Array || a[k].array->size() < 2) continue;
                Value v = (*a[k].array)[1];
                if (v.type == Value::Type::Code && v.code) v = in.eval(*v.code);
                std::string key = (*a[k].array)[0].str();
                if (key == "volume") vol = v.number();
                else if (key == "transpose") tr = v.number();
                else if (key == "pan") pan = v.number();
            }
            synth_->playSequence(a[0].str(), vol, tr, pan);
        } else if (msg == "stop_all_sfx") {
            synth_->stopAll();
        } else if (msg == "pause_all_sfx") {
            synth_->pauseAll(!a.empty() && a[0].truthy());
        }
        return true;  // set_fx, set_fx_vol, set_sfx_global_volume: not used by GH1 scripts
    }

private:
    Synth* synth_;
};

// `arena` (the venue's world object): arena/arena_game.dta's handlers run on
// it; its delay_task counts song ticks (CLAP_DELAY_TICKS) [I: the unit].
class ArenaObj : public ScriptObject {
public:
    ArenaObj(std::function<float()> msPerTick, std::function<void(bool)> dof)
        : ScriptObject("arena"), msPerTick_(std::move(msPerTick)), dof_(std::move(dof)) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if (msg == "delay_task" && !a.empty()) {
            Task t{a[0].number() * (msPerTick_ ? msPerTick_() : 1.0f), {}};
            for (size_t k = 1; k < a.size(); ++k)
                if (a[k].type == Value::Type::Code) t.body.push_back(a[k].code);
            tasks_.push_back(std::move(t));
            return true;
        }
        (void)in;
        if (msg == "set_depth_of_field" && !a.empty() && dof_) dof_(a[0].truthy());
        // The No Venue cheat (long_cheats.dta): the venue's scenes are not drawn.
        if (msg == "set_showing" && !a.empty()) showing_ = a[0].truthy();
        if (msg == "showing") r = Value::integer(showing_ ? 1 : 0);
        // cam_msg, cam_cmd, intro_start ...: the engine's venue handles these.
        return true;
    }
    bool showing() const { return showing_; }
    void setShowing(bool on) { showing_ = on; }
    void poll(Interp& in, float dtMs) {
        for (size_t k = 0; k < tasks_.size();) {
            tasks_[k].remaining -= dtMs;
            if (tasks_[k].remaining > 0) {
                ++k;
                continue;
            }
            Task t = tasks_[k];
            tasks_.erase(tasks_.begin() + long(k));
            for (const DataNode* n : t.body) in.eval(*n);
        }
    }
    void clearTasks() { tasks_.clear(); }

private:
    struct Task {
        float remaining;
        std::vector<const DataNode*> body;
    };
    std::vector<Task> tasks_;
    std::function<float()> msPerTick_;
    std::function<void(bool)> dof_;
    bool showing_ = true;
};

// `crowd_audio` (CrowdAudio_v1).
class CrowdAudioObj : public ScriptObject {
public:
    explicit CrowdAudioObj(CrowdAudio* crowd) : ScriptObject("crowd_audio"), crowd_(crowd) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if (!crowd_) return true;
        if (msg == "play_intro") crowd_->playIntro();
        else if (msg == "set_excitement" && !a.empty()) crowd_->setExcitement(a[0].integerValue());
        else if (msg == "play_stream" && a.size() >= 2) crowd_->playStream(a[0].number(), a[1].str());
        else if (msg == "turn_off") crowd_->turnOff();
        else if (msg == "set_paused" && !a.empty()) crowd_->setPaused(a[0].truthy());
        else if (msg == "set_volume" && !a.empty()) crowd_->setVolume(a[0].number());
        else if (msg == "set_enabled" && !a.empty()) crowd_->setEnabled(a[0].truthy());
        else if (msg == "enabled") r = Value::integer(crowd_->enabled() ? 1 : 0);
        return true;
    }

private:
    CrowdAudio* crowd_;
};

// `player_mgr`: foreach_player $p {...} / foreach_player_matcher $m {...} over the one player.
class PlayerMgrObj : public ScriptObject {
public:
    explicit PlayerMgrObj(std::function<bool()> multiplayer) : ScriptObject("player_mgr"), multiplayer_(std::move(multiplayer)) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if ((msg == "foreach_player" || msg == "foreach_player_matcher") && a.size() >= 2) {
            std::string var = a[0].str();
            if (!var.empty() && var[0] == '$') var = var.substr(1);
            Value saved = in.var(var);
            int n = multiplayer_ && multiplayer_() ? 2 : 1;  // both players in a face-off
            for (int p = 0; p < n; ++p) {
                // The matcher is the object running config/beatmatch.dta's player_matcher_handlers.
                ScriptObject* o = msg == "foreach_player" ? in.findObject("player" + std::to_string(p) + "::player")
                                                          : in.findObject("player_matcher_handlers");
                if (!o) o = in.findObject("player_matcher");
                in.var(var) = Value::obj(o);
                for (size_t k = 1; k < a.size(); ++k)
                    if (a[k].type == Value::Type::Code && a[k].code) in.eval(*a[k].code);
            }
            in.var(var) = saved;
        }
        return true;
    }

private:
    std::function<bool()> multiplayer_;
};

class PlayerObj : public ScriptObject {
public:
    explicit PlayerObj(std::function<void()> starPower) : ScriptObject("player"), starPower_(std::move(starPower)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        std::string what = a.empty() ? "" : a[0].str();
        if (msg == "has") return r = Value::integer(what == "guitarist"), true;
        if (msg == "get" && what == "guitarist") return r = Value::obj(this), true;  // answers start_starpower
        if (msg == "start_starpower") {
            if (starPower_) starPower_();
            return r = Value::integer(0), true;
        }
        r = Value::integer(0);
        return true;
    }

private:
    std::function<void()> starPower_;
};

// A block of script handlers from a config file (player_matcher_handlers,
// star_power_handlers): the engine sends gameplay events to it.
class HandlerSetObj : public ScriptObject {
public:
    explicit HandlerSetObj(const std::string& name, std::function<void(const std::string&)> native = {})
        : ScriptObject(name), native_(std::move(native)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>&, Value& r) override {
        r = Value::integer(0);
        if (native_) native_(msg);
        return true;  // events without a handler
    }

private:
    std::function<void(const std::string&)> native_;
};

// `tips` (Tips_v1, config/tips.dta): random_tip gives a loading_tipN token,
// skipping tips whose (diff ...) list leaves out the game's difficulty.
class TipsObj : public ScriptObject {
public:
    TipsObj(DataNode tips, std::function<int()> difficulty, std::function<std::string(const std::string&)> localize)
        : ScriptObject("tips"), tips_(std::move(tips)), difficulty_(std::move(difficulty)), localize_(std::move(localize)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>&, Value& r) override {
        r = Value::integer(0);
        if (msg != "random_tip") return false;
        static const std::map<std::string, int> kDiffs = {
            {"kDifficultyEasy", 0}, {"kDifficultyMedium", 1}, {"kDifficultyHard", 2}, {"kDifficultyExpert", 3}};
        int diff = difficulty_ ? difficulty_() : 0;
        std::vector<std::string> ok;
        for (const DataNode& t : tips_.children) {
            if (!t.isList() || t.children.empty() || !t.children[0].isText()) continue;
            bool allowed = true;
            if (const DataNode* d = t.find("diff")) {
                allowed = false;
                for (size_t k = 1; k < d->children.size(); ++k) {
                    const DataNode& v = d->children[k];
                    int n = v.type == DataNode::Type::Int ? v.i : kDiffs.count(v.s) ? kDiffs.at(v.s) : -1;
                    allowed = allowed || n == diff;
                }
            }
            if (allowed) ok.push_back(t.children[0].s);
        }
        // The localized text (the script passes it to set_localized_text).
        if (const char* forced = SDL_getenv("GD01_TIP")) return r = Value::string(localize_(forced)), true;  // dev aid
        if (!ok.empty()) r = Value::string(localize_(ok[std::uniform_int_distribution<size_t>(0, ok.size() - 1)(rng_)]));
        return true;
    }

private:
    DataNode tips_;
    std::function<int()> difficulty_;
    std::function<std::string(const std::string&)> localize_;
    std::mt19937 rng_{std::random_device{}()};
};

// `memcard` (MemcardMgr_v1 @0x14b368; docs/boot_profiles_memcard.md §3): the
// memory card is the save file. load_data / save_data / format answer
// `memcard_result $result $space_free` to the calling screen a moment later
// (the scripts' progress screens show meanwhile).
class UiSystem::MemcardObj : public ScriptObject {
public:
    explicit MemcardObj(UiSystem& ui) : ScriptObject("memcard"), ui_(ui) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        constexpr int kSpaceNeeded = 0x33000, kFree = 8 * 1024 * 1024;
        r = Value::integer(0);
        auto reply = [&](int result) {
            ScriptObject* obj = a.empty() ? nullptr : a[0].object;
            if (!obj && !a.empty()) obj = ui_.interp_.findObject(a[0].str());
            ui_.later(kOperationMs, [this, obj, result] {
                if (obj) ui_.interp_.send(obj, "memcard_result", {Value::integer(result), Value::integer(kFree)});
            });
        };
        if (msg == "get_info") return true;  // kMCNoError: the card is always there
        if (msg == "load_data") {
            // kMCNoError, or (no save yet) a general error: the scripts offer to create it.
            reply(ui_.hooks_.loadCampaign && ui_.hooks_.loadCampaign() ? 0 : -110);
            return true;
        }
        if (msg == "save_data") {
            reply(ui_.hooks_.saveCampaignFile && ui_.hooks_.saveCampaignFile() ? 0 : -104);
            return true;
        }
        if (msg == "format") return reply(-121), true;  // kMCAlreadyFormatted
        if (msg == "space_needed") return r = Value::integer(kSpaceNeeded), true;
        if (msg == "space_available") return r = Value::integer(kFree), true;
        if (msg == "slot") return r = Value::string("1"), true;
        if (msg == "check_save_game") return r = Value::integer(ui_.hooks_.saveExists && ui_.hooks_.saveExists() ? 0 : -110), true;
        if (msg == "localize") {  // <tok>_ps2 with {SLOT} and {REQ_SPACE}
            std::string tok = a.empty() ? "" : a[0].str();
            std::string t = ui_.res_->localize(tok + "_ps2");
            if (t == tok + "_ps2") t = ui_.res_->localize(tok);
            for (auto [from, to] : {std::pair<std::string, std::string>{"{SLOT}", "1"}, {"{REQ_SPACE}", "204KB"}})
                for (size_t p = t.find(from); p != std::string::npos; p = t.find(from)) t.replace(p, from.size(), to);
            return r = Value::string(t), true;
        }
        return false;
    }

private:
    static constexpr float kOperationMs = 600;  // the card I/O phase
    UiSystem& ui_;
};

// `beatmatch`: set_music_speed (the lose slow-down), set_paused (the song audio).
class UiSystem::BeatmatchObj : public ScriptObject {
public:
    explicit BeatmatchObj(UiSystem& ui) : ScriptObject("beatmatch"), ui_(ui) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if (msg == "set_music_speed" && !a.empty() && ui_.hooks_.musicSpeed) ui_.hooks_.musicSpeed(a[0].number());
        if (msg == "set_paused" && !a.empty() && ui_.hooks_.musicPaused) ui_.hooks_.musicPaused(a[0].truthy());
        return true;
    }
    UiSystem& ui_;
};

class UiSystem::StubObj : public ScriptObject {
public:
    StubObj(const std::string& name, std::map<std::string, Value> answers = {})
        : ScriptObject(name), answers_(std::move(answers)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>&, Value& r) override {
        auto it = answers_.find(msg);
        r = it == answers_.end() ? Value::integer(0) : it->second;
        return true;
    }
    std::map<std::string, Value> answers_;
};

// song_provider (SongProvider, FUN_00118400): rows of tier headers
// (localize song_header_<venue>) and songs in config/campaign.dta (order ...)
// order, then the remaining songs under "Bonus Tracks".
// song_provider (SongProvider, ctor FUN_00118400; docs/career.md 2.3): tier
// headers and songs. Quickplay: the first two tiers plus every song unlocked
// in any difficulty; career: the unlocked songs of the current difficulty
// (the newest tier only while at the newest venue), with score / star
// columns and a footer; store mode: the store songs. Without a campaign,
// every song.
class UiSystem::ProviderObj : public ScriptObject {
public:
    explicit ProviderObj(UiSystem& ui) : ScriptObject("song_provider"), ui_(ui) {}
    struct Row {
        bool header;
        std::string text;
        int song = -1;  // index into the engine's song list
        std::string score, stars, footer;
        bool beaten = false;
    };
    static Row makeRow(bool header, std::string text, int song) {
        Row r;
        r.header = header, r.text = std::move(text), r.song = song;
        return r;
    }
    void invalidate() { built_ = false; }
    const std::vector<Row>& rows() {
        int diff = ui_.gameDifficulty();
        std::string venue = ui_.gameVenue();
        // FUN_0013d698 reads the current venue too (the newest-tier filter, the footer).
        if (built_ && diff == builtDiff_ && venue == builtVenue_) return rows_;
        built_ = true;
        builtDiff_ = diff;
        builtVenue_ = venue;
        rows_.clear();
        songRows_.clear();
        const auto& songs = ui_.hooks_.songs();
        Campaign* c = ui_.hooks_.campaign;
        std::vector<bool> used(songs.size());
        auto add = [&](const std::string& id) {
            for (size_t k = 0; k < songs.size(); ++k)
                if (!used[k] && songs[k].id == id) {
                    used[k] = true;
                    Row r = makeRow(false, songs[k].name, int(k));
                    if (c && !quickplay_) {  // columns 1 and 2 (FUN_00118c58 / FUN_00118cb8)
                        r.score = "0";  // "%i", unplayed songs too
                        if (const Campaign::Item* it = c->item(diff, id)) {
                            r.score = std::to_string(it->score);
                            if (it->stars >= 3) r.stars = std::string(size_t(it->stars), '*');
                            r.beaten = it->beaten && !c->isStoreSong(id);
                        }
                    }
                    rows_.push_back(r);
                }
        };
        auto header = [&](const std::string& venue) { rows_.push_back(makeRow(true, ui_.res_->localize("song_header_" + venue), -1)); };
        if (!c) {
            try {
                DataNode campaign = ui_.loader_.load("config/campaign.dta");
                if (const DataNode* order = campaign.find("order"))
                    for (size_t t = 1; t < order->children.size(); ++t) {
                        const DataNode& tier = order->children[t];
                        if (!tier.isList() || tier.children.empty()) continue;
                        header(tier.children[0].s);
                        for (size_t k = 1; k < tier.children.size(); ++k) add(tier.children[k].s);
                    }
            } catch (const std::exception& ex) {
                SDL_Log("ui: campaign order: %s", ex.what());
            }
            bool bonus = false;
            for (size_t k = 0; k < songs.size(); ++k)
                if (!used[k] && songs[k].id.rfind("tutorial", 0) != 0) {
                    if (!bonus) header("store"), bonus = true;
                    rows_.push_back(makeRow(false, songs[k].name, int(k)));
                }
        } else if (storeMode_) {
            header("store");
            for (const std::string& s : c->storeSongs()) add(s);
        } else if (quickplay_) {  // FUN_00137d58
            const auto& venues = c->venues();
            for (size_t v = 0; v < venues.size(); ++v) {
                std::vector<std::string> list;
                for (const std::string& s : c->songsOf(venues[v]))
                    if (v < 2 || c->isUnlockedAny(s)) list.push_back(s);
                if (list.empty()) continue;
                header(venues[v]);
                for (const std::string& s : list) add(s);
            }
            std::vector<std::string> bonus;
            for (const std::string& s : c->storeSongs())
                if (c->isUnlockedAny(s)) bonus.push_back(s);
            // Column 4 (FUN_00118dd8): song_quickplay on the row before the
            // first bonus song (or the last row), unless Medium..Expert has
            // status >= numVenues - 1.
            bool far = false;
            for (int d = 1; d <= 3; ++d) far = far || c->status(d) >= c->numVenues(d) - 1;
            if (!far && !rows_.empty()) rows_.back().footer = ui_.res_->localize("song_quickplay");
            if (!bonus.empty()) header("store");
            for (const std::string& s : bonus) add(s);
        } else {  // career (FUN_0013d698 filter)
            int n = c->numVenues(diff), status = c->status(diff);
            bool complete = c->completedVenues(diff) == n;
            int cur = c->venueIndex(venue);
            const auto& venues = c->venues();
            int lastCareerRow = -1;
            for (int v = 0; v < n && v < int(venues.size()); ++v) {
                if (!complete && cur < status && v == status) continue;
                std::vector<std::string> list;
                for (const std::string& s : c->songsOf(venues[size_t(v)]))
                    if (c->isUnlocked(diff, s)) list.push_back(s);
                if (list.empty()) continue;
                header(venues[size_t(v)]);
                for (const std::string& s : list) add(s);
                lastCareerRow = int(rows_.size()) - 1;
            }
            // Column 4, the footer on the last career row (FUN_00118dd8).
            if (lastCareerRow >= 0) {
                std::string& f = rows_[size_t(lastCareerRow)].footer;
                if (status >= n) f.clear();
                else if (cur < status) f = ui_.res_->localize("song_old_venue");
                else if (cur == n - 1) f = ui_.res_->localize("song_last_venue");
                else {
                    char buf[128];
                    std::snprintf(buf, sizeof buf, ui_.res_->localize("song_current_venue").c_str(),
                                  c->required(diff, cur >= 0 ? venues[size_t(cur)] : venues[0]));
                    f = buf;
                }
            }
            std::vector<std::string> bonus;
            for (const std::string& s : c->storeSongs())
                if (c->isUnlocked(diff, s)) bonus.push_back(s);
            if (!bonus.empty()) header("store");
            for (const std::string& s : bonus) add(s);
        }
        for (size_t r = 0; r < rows_.size(); ++r)
            if (!rows_[r].header) songRows_.push_back(int(r));
        return rows_;
    }
    // The rows the list shows: like RndScrollList, which sets its element
    // texts only on set_provider and refresh (docs/song_list.md 3), so the
    // sel_song exit's {song_provider set_quickplay FALSE} doesn't swap the
    // career columns in while the panel animates out.
    void snapshot() { shown_ = rows(); }
    const std::vector<Row>& shown() const { return shown_; }
    int songCount() { return rows(), int(songRows_.size()); }
    int rowOf(int songIndex) { rows(); return songIndex >= 0 && songIndex < int(songRows_.size()) ? songRows_[size_t(songIndex)] : 0; }
    int engineSong(int songIndex) { rows(); return songIndex >= 0 && songIndex < int(songRows_.size()) ? rows_[size_t(songRows_[size_t(songIndex)])].song : -1; }

    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        const auto& songs = ui_.hooks_.songs();
        int i = a.empty() ? 0 : a[0].integerValue();
        int e = engineSong(i);
        if (msg == "list_length" || msg == "num_data") return r = Value::integer(songCount()), true;
        if (msg == "get_text") return r = Value::string(e >= 0 ? songs[size_t(e)].name : ""), true;
        if (msg == "get_symbol") return r = Value::symbol(e >= 0 ? songs[size_t(e)].id : ""), true;
        if (msg == "get_artist") return r = Value::string(e >= 0 ? songs[size_t(e)].artist : ""), true;
        if (msg == "get_quickplay") return r = Value::integer(quickplay_), true;
        if (msg == "set_quickplay") {
            bool q = !a.empty() && a[0].truthy();
            if (q != quickplay_) quickplay_ = q, built_ = false;
            return true;
        }
        if (msg == "set_store_mode") {
            bool st = !a.empty() && a[0].truthy();
            if (st != storeMode_) storeMode_ = st, built_ = false;
            return true;
        }
        return false;
    }
    UiSystem& ui_;
    bool built_ = false, quickplay_ = false, storeMode_ = false;  // ctor FUN_00118400: +0x10 / +0x14 = 0
    int builtDiff_ = -1;
    std::string builtVenue_;
    std::vector<Row> rows_, shown_;
    std::vector<int> songRows_;
};

// `game` (GameConfig 0x113e18): the player's selections.
// PlayerConfig (PlayerConfig_v1): the player's character, guitar and skin.
// Descriptions are the locale tokens "%s_desc" (FUN_00117dc0).
class PlayerConfigObj : public ScriptObject {
public:
    PlayerConfigObj(const std::string& name, const DataNode& guitars) : ScriptObject(name) {
        for (const DataNode& g : guitars.children) {
            if (!g.isList() || g.children.empty() || !g.children[0].isText()) continue;
            std::vector<std::string> skins;
            if (const DataNode* sk = g.find("skins"))
                for (size_t k = 1; k < sk->children.size(); ++k)
                    if (sk->children[k].isList() && !sk->children[k].children.empty()) skins.push_back(sk->children[k].children[0].s);
            guitars_.push_back({g.children[0].s, skins});
        }
        setGuitar(0);
    }
    void setGuitar(size_t i) {
        if (i >= guitars_.size()) return;
        guitar = guitars_[i].first;
        skin = guitars_[i].second.empty() ? "" : guitars_[i].second[0];
    }
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        if (msg == "get_character") return r = sym(character), true;
        if (msg == "set_character" && !a.empty()) return character = a[0].str(), true;
        if (msg == "get_guitar") return r = sym(guitar), true;
        if (msg == "get_guitar_skin") return r = sym(skin), true;
        if (msg == "get_guitar_desc") return r = sym((a.empty() ? guitar : a[0].str()) + "_desc"), true;
        if (msg == "get_guitar_skin_desc") return r = sym((a.empty() ? skin : a[0].str()) + "_desc"), true;
        if (msg == "set_guitar_index" && !a.empty()) return setGuitar(size_t(std::max(0, a[0].integerValue()))), true;
        if (msg == "set_guitar" && !a.empty()) {
            for (size_t i = 0; i < guitars_.size(); ++i)
                if (guitars_[i].first == a[0].str()) setGuitar(i);
            return true;
        }
        if (msg == "set_guitar_skin" && !a.empty()) {
            for (const auto& [g, skins] : guitars_)
                if (std::find(skins.begin(), skins.end(), a[0].str()) != skins.end()) guitar = g, skin = a[0].str();
            return true;
        }
        return false;
    }
    std::string character = "metal", guitar, skin;

private:
    std::vector<std::pair<std::string, std::vector<std::string>>> guitars_;
};

// player0::player: the playing player's stats, as the result screens read them.
class PlayerStatsObj : public ScriptObject {
public:
    PlayerStatsObj(int player, std::function<int(const std::string&, int)> stat,
                   std::function<bool(const std::string&, const std::vector<Value>&)> command)
        : ScriptObject("player" + std::to_string(player) + "::player"), player_(player), stat_(std::move(stat)),
          command_(std::move(command)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        if (!stat_) return false;
        // The setters (set_crowd_rating_active, fill_star_power, ...): the app's player.
        if (player_ == 0 && command_ && command_(msg, a)) return r = Value::integer(0), true;
        // star_rating: the 3_star / 4_star / 5_star locale token.
        if (msg == "star_rating") return r = Value::symbol(std::to_string(stat_("num_stars", player_)) + "_star"), true;
        return r = Value::integer(stat_(msg, player_)), true;
    }

private:
    int player_;
    std::function<int(const std::string&, int)> stat_;
    std::function<bool(const std::string&, const std::vector<Value>&)> command_;
};

// player_matcher (PlayerMatcher_v1): set_volume mutes the guitar track (the tutorial demos).
class PlayerMatcherObj : public ScriptObject {
public:
    PlayerMatcherObj(std::function<void(float)> volume, std::function<void(bool)> autoPlay)
        : ScriptObject("player_matcher"), volume_(std::move(volume)), autoPlay_(std::move(autoPlay)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        if (msg == "set_volume" && !a.empty() && volume_) volume_(a[0].number());
        if (msg == "set_auto_play" && autoPlay_) autoPlay_(a.empty() || a[0].truthy());  // the attract mode
        return true;
    }

private:
    std::function<void(float)> volume_;
    std::function<void(bool)> autoPlay_;
};

class UiSystem::GameObj : public ScriptObject {
public:
    GameObj(UiSystem& ui, const DataNode& guitars) : ScriptObject("game"), ui_(ui) {
        for (int p = 0; p < 2; ++p) players[p] = std::make_unique<PlayerConfigObj>("player_config" + std::to_string(p), guitars);
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& r) override {
        static const char* const kDiff[] = {"easy", "medium", "hard", "expert"};
        const auto& songs = ui_.hooks_.songs();
        auto* provider = static_cast<ProviderObj*>(ui_.interp_.findObject("song_provider"));
        // The chosen song is kept as the engine's song, not only as a list row:
        // the list is rebuilt when the difficulty changes, and rows move.
        auto pick = [&](int index) {
            songIndex = index;
            engineSongId = provider->engineSong(index);
        };
        auto song = [&]() -> const SongInfo* {
            int e = engineSongId >= 0 ? engineSongId : provider->engineSong(songIndex);
            return e >= 0 && e < int(songs.size()) ? &songs[size_t(e)] : nullptr;
        };
        if (msg == "get_controller") return r = sym(ui_.guitar_ ? "guitar" : "joypad"), true;
        // FUN_00113910: both pads (slots) present; is_missing_controller: pad 0, and pad 1 in multiplayer.
        auto present = [&](int slot) { return !ui_.hooks_.padConnected || ui_.hooks_.padConnected(slot); };
        if (msg == "is_missing_controller") return r = Value::integer(!present(0) || (multiplayer && !present(1))), true;
        if (msg == "is_missing_multi_controller") return r = Value::integer(!(present(0) && present(1))), true;
        if (msg == "multiplayer") return r = Value::integer(multiplayer ? 1 : 0), true;
        if (msg == "set_multiplayer") return multiplayer = !a.empty() && a[0].truthy(), true;
        if (msg == "set_difficulty" && !a.empty()) return difficulty = std::clamp(a[0].integerValue(), 0, 3), true;
        if (msg == "get_difficulty") return r = Value::integer(difficulty), true;
        if (msg == "get_difficulty_sym") return r = sym(kDiff[difficulty]), true;
        if (msg == "set_song_index" && !a.empty()) return pick(a[0].integerValue()), true;
        if (msg == "get_song_index") return r = Value::integer(songIndex), true;
        // Tutorial songs are in no list (tut_loading / the lessons' load_song).
        auto tutorialSong = [&](const std::string& id) {
            for (size_t k = 0; k < songs.size(); ++k)
                if (songs[k].id == id) return int(k);
            return -1;
        };
        if (msg == "set_song" && !a.empty()) {
            if (a[0].str().rfind("tutorial", 0) == 0) return engineSongId = tutorialSong(a[0].str()), true;
            bool found = false;
            for (int k = 0; k < provider->songCount(); ++k)
                if (int e = provider->engineSong(k); e >= 0 && songs[size_t(e)].id == a[0].str()) pick(k), found = true;
            // Before any song list was built (the attract mode's random song
            // from the title screen) the song is found in the engine's list.
            if (!found)
                for (size_t k = 0; k < songs.size(); ++k)
                    if (songs[k].id == a[0].str()) engineSongId = int(k);
            return true;
        }
        // GamePanel_v1: load_song / loaded / start_song / set_time_paused (the tutorial lessons).
        if (msg == "load_song" && !a.empty()) {
            engineSongId = tutorialSong(a[0].str());
            if (engineSongId >= 0 && ui_.hooks_.tutorialLoad) ui_.hooks_.tutorialLoad(engineSongId);
            return true;
        }
        if (msg == "loaded") return r = Value::integer(1), true;  // songs load synchronously
        if (msg == "start_song") {
            if (ui_.hooks_.tutorialStart) ui_.hooks_.tutorialStart(skipIntro, startPaused);
            return true;
        }
        if (msg == "set_time_paused") {
            timePaused = !a.empty() && a[0].truthy();
            if (ui_.hooks_.setTimePaused) ui_.hooks_.setTimePaused(timePaused);
            return true;
        }
        if (msg == "time_paused") return r = Value::integer(timePaused ? 1 : 0), true;
        if (msg == "set_skip_intro") return skipIntro = !a.empty() && a[0].truthy(), true;
        if (msg == "set_start_paused") return startPaused = !a.empty() && a[0].truthy(), true;
        if (msg == "get_song") return r = sym(song() ? song()->id : ""), true;
        if (msg == "get_song_text") return r = Value::string(song() ? song()->name : ""), true;
        if (msg == "get_song_artist_text") return r = Value::string(song() ? song()->artist : ""), true;
        // Career venue selection (sel_venue); quickplay uses the song's venue.
        if (msg == "set_venue" && !a.empty()) return venue = a[0].str(), true;
        if (msg == "get_venue") return r = sym(!venue.empty() ? venue : song() ? song()->venue : "basement"), true;
        Campaign* camp = ui_.hooks_.campaign;
        if (msg == "get_venue_index") return r = Value::integer(camp ? std::max(0, camp->venueIndex(venue)) : 0), true;
        if (msg == "is_venue_available") {  // FUN_00113d78: no (multi_only TRUE)
            std::string v = a.empty() ? "" : a[0].str();
            return r = Value::integer(std::find(ui_.multiOnlyVenues_.begin(), ui_.multiOnlyVenues_.end(), v) == ui_.multiOnlyVenues_.end()), true;
        }
        if (msg == "set_career_venue" && camp) {  // FUN_00113c30
            int n = camp->numVenues(difficulty);
            bool complete = camp->completedVenues(difficulty) == n;
            if (!(complete && camp->isUnlocked(difficulty, venue) && camp->venueIndex(venue) >= 0)) {
                int last = 0;
                for (int i = 0; i < n && camp->isUnlocked(difficulty, camp->venues()[size_t(i)]); ++i) last = i;
                venue = camp->venues()[size_t(last)];
            }
            return true;
        }
        if (msg == "set_career_song" && camp) {  // FUN_00113b80
            int count = provider->songCount();
            for (int k = std::max(0, songIndex); k < count; ++k) {
                int e = provider->engineSong(k);
                const Campaign::Item* it = e >= 0 ? camp->item(difficulty, songs[size_t(e)].id) : nullptr;
                if (!(it && it->beaten && !camp->isStoreSong(it->sym))) {
                    pick(k);
                    break;
                }
            }
            return true;
        }
        if (msg == "video_provider") return r = Value::obj(ui_.interp_.findObject("video_provider")), true;
        if (msg == "get_player_config")
            return r = Value::obj(players[size_t(std::clamp(a.empty() ? 0 : a[0].integerValue(), 0, 1))].get()), true;
        // {game set_quickplay} (quickplay.dta qp_diff): the song's (quickplay (character)(guitar)(venue))
        // becomes the game config; career keeps the player's choices and the selected venue.
        if (msg == "set_quickplay") {
            quickplay = a.empty() || a[0].truthy();
            if (quickplay)
                if (const SongInfo* sg = song()) {
                    players[0]->character = sg->character;
                    std::vector<Value> g{Value::symbol(sg->guitar)};
                    Value ignored;
                    players[0]->handleNative(in, "set_guitar", g, ignored);
                    venue = sg->venue;
                }
            return true;
        }
        // The song's sound-bank loader (sfx banks are not loaded yet; `reset` is a no-op).
        if (msg == "get_bank_loader") return r = Value::obj(ui_.interp_.findObject("bank_loader")), true;
        // The player messages act on player 1 (GameConfig forwards them).
        if (players[0]->handleNative(in, msg, a, r)) return true;
        if (msg == "set_paused") {
            if (ui_.hooks_.setPaused) ui_.hooks_.setPaused(!a.empty() && a[0].truthy());
            return true;
        }
        if (msg == "restart") return restartPending = true, true;
        if (msg == "is_up") return r = Value::integer(0), true;
        // The game panel's TaskMgr runs `game delay_task` / `game anim_task`.
        if (msg == "delay_task")
            if (PanelObj* gp = ui_.panel("game")) return gp->handleNative(in, msg, a, r);
        if (msg == "anim_task") return ui_.animTask(a), true;
        // {game interp_task ms $var from to {...}}: $var goes from -> to over ms, running the body each frame.
        if (msg == "interp_task" && a.size() >= 4) {
            InterpTask t{a[1].str(), std::max(a[0].number(), 1.0f), 0, a[2].number(), a[3].number(), {}};
            if (!t.var.empty() && t.var[0] == '$') t.var = t.var.substr(1);
            for (size_t k = 4; k < a.size(); ++k)
                if (a[k].type == Value::Type::Code) t.body.push_back(a[k].code);
            ui_.interpTasks_.push_back(std::move(t));
            return true;
        }
        if (msg.rfind("set_", 0) == 0 || msg == "delay_task") return true;  // venue, character ... (quickplay decides)
        return false;
    }
    UiSystem& ui_;
    int difficulty = 1, songIndex = 0;
    int engineSongId = -1;  // index into the engine's song list
    bool quickplay = true, restartPending = false;
    bool skipIntro = false, startPaused = false, timePaused = false;
    bool multiplayer = false;  // GameConfig+0x8c
    std::string venue;
    std::unique_ptr<PlayerConfigObj> players[2];
};

// A view or mesh of a panel scene, as scripts address it by name.
class SceneProxy : public ScriptObject {
public:
    SceneProxy(Panel& panel, const std::string& name) : ScriptObject(name), panel_(panel) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        SceneObject* o = panel_.scene().find(name());
        if (!o) return false;
        if (msg == "set_showing") return panel_.setShowing(name(), !a.empty() && a[0].truthy()), true;
        if (msg == "showing") return r = Value::integer(o->drawable() && o->drawable()->showing), true;
        if (msg == "set_frame") return panel_.setFrame(name(), a.empty() ? 0.0f : a[0].number()), true;
        if (msg == "frame") return r = Value::real(panel_.frame(name())), true;  // sel_venue's map scroll starts here
        auto v3 = [&] {
            return std::array<float, 3>{a.size() > 0 ? a[0].number() : 0, a.size() > 1 ? a[1].number() : 0, a.size() > 2 ? a[2].number() : 0};
        };
        if (msg == "set_local_pos") return panel_.setLocalPos(name(), v3()), true;
        if (msg == "set_local_scale") return panel_.setLocalScale(name(), v3()), true;
        if (msg == "set_local_rot" || msg == "unhook_anim_parents") return true;
        // Mat: set_stage_tex <stage> <tex> (the tutorial guitar diagrams).
        if (msg == "set_stage_tex" && o->mat && a.size() >= 2) {
            size_t st = size_t(std::max(0, a[0].integerValue()));
            if (st < o->mat->stages.size()) o->mat->stages[st].tex = a[1].str();
            return true;
        }
        return false;
    }
    Panel& panel_;
};

ScriptObject* UiSystem::PanelObj::sceneObject(const std::string& n) {
    if (!scene_ || !scene_->scene().find(n)) return nullptr;
    auto& p = proxies_[n];
    if (!p) p = std::make_unique<SceneProxy>(*scene_, n);
    return p.get();
}

std::unique_ptr<ScriptObject> UiSystem::makeSceneProxy(Panel& panel, const std::string& name) {
    return std::make_unique<SceneProxy>(panel, name);
}

std::vector<std::pair<std::string, std::string>> UiSystem::PanelObj::helpItems() {
    std::vector<std::pair<std::string, std::string>> out;
    Value list;
    if (hasOverride_) {
        list = override_;
    } else if (ui_.current_) {
        Value hb = ui_.current_->props["helpbar"];
        if (hb.type != Value::Type::Array || hb.array->empty()) return out;
        if ((*hb.array)[0].isText()) hb = Value::list({hb});  // a single (key elems) entry
        std::string focus;
        if (PanelObj* fp = ui_.focusPanel()) focus = fp->focus_;
        const Value* chosen = nullptr;
        for (const Value& e : *hb.array)
            if (e.type == Value::Type::Array && !e.array->empty()) {
                std::string key = (*e.array)[0].str();
                if (key == focus) chosen = &e;
                else if (key == "default" && !chosen) chosen = &e;
            }
        if (!chosen || chosen->array->size() < 2) return out;
        list = (*chosen->array)[1];
    }
    if (list.type != Value::Type::Array) return out;
    for (const Value& e : *list.array)
        if (e.type == Value::Type::Array && e.array->size() >= 2) out.push_back({(*e.array)[0].str(), (*e.array)[1].str()});
    // The port's main menu also offers GAMEPADS (blue) and MODS (orange), before the strum bar.
    if (!hasOverride_ && ui_.current_ && ui_.current_->name() == "main") {
        auto strumAt = std::find_if(out.begin(), out.end(), [](const auto& e) { return e.first == "strum"; });
        out.insert(strumAt, {{"fret4", "GAMEPADS"}, {"fret5", "MODS"}});
    }
    return out;
}

void UiSystem::PanelObj::drawHelpbar() {
    Mat4 vp;
    if (!scene_->viewProj(vp)) return;
    // Enter (HelpBarPanel_v3): the hidden placeholders give the start of the
    // row and the fixed spot of the strum bar.
    const SceneObject* start = scene_->scene().find("help_bar_starting.mesh");
    const SceneObject* strum = scene_->scene().find("help_bar_strumbar.mesh");
    if (!start || !start->mesh) return;
    UiResources& res = *ui_.res_;
    auto num = [&](const char* k, float d) { auto it = props.find(k); return it == props.end() ? d : it->second.number(); };
    float buttonSpacing = num("button_spacing", 40), strumSpacing = num("strumbar_spacing", 80), textSpacing = num("text_spacing", 25);
    // The elements copy resources.rnd_ps2 objects and only move them
    // (FUN_001da730 sets the local position), so rotation and scale stay.
    auto local = [&](const std::string& n) {
        const SceneObject* o = res.scene().find(n);
        return o && o->mesh ? o->mesh->trans.local : o && o->text ? o->text->trans.local : Xfm{};
    };
    auto at = [](Xfm x, const std::array<float, 3>& pos) { return Mat4::fromRowXfm(x.rot, pos); };
    // The rounded box: help_box_mid stretched to the element's width
    // (FUN_00148898: x scale = width / the mesh's width), help_box_left at
    // the element (FUN_00148948), help_box_right at its end (FUN_001489a8).
    float midWidth = 16;
    if (const SceneObject* m = res.scene().find("help_box_mid.mesh"); m && m->mesh && m->mesh->verts.size() > 2)
        midWidth = m->mesh->verts[2].pos[0] - m->mesh->verts[0].pos[0];
    auto mid = [&](const std::array<float, 3>& pos, float width) {
        Xfm x = local("help_box_mid.mesh");
        float sx = width / midWidth;
        for (int k = 0; k < 3; ++k) x.rot[size_t(k)] *= sx;
        res.drawMeshAt("help_box_mid.mesh", vp, at(x, pos));
    };
    std::array<float, 3> cursor = start->mesh->trans.world.pos;
    const Xfm textLocal = local("help_bar.txt");
    const float textScale = std::hypot(textLocal.rot[0], textLocal.rot[1], textLocal.rot[2]);
    // The port's extra main-menu items (GAMEPADS, MODS) do not fit before the
    // strum bar's fixed spot: there the strum bar follows the row, every gap
    // twice text_spacing.
    const auto items = helpItems();
    const bool flowStrum = std::any_of(items.begin(), items.end(), [](const auto& i) { return i.first == "fret3" || i.first == "fret4"; });
    const float gap = flowStrum ? textSpacing * 2 : textSpacing;
    int k = 0;
    // Button / strum element (ButtonHBElement_v5): box, then the icon.
    auto button = [&](const std::string& elem, bool isStrum) {
        float width = isStrum ? strumSpacing : buttonSpacing;
        mid(cursor, width);
        res.drawMeshAt("help_box_left.mesh", vp, at(local("help_box_left.mesh"), cursor));
        scene_->drawMesh("help_bar_starting.mesh", "hb_" + elem + ".mat", vp,
                         at(local(isStrum ? "help_bar_strum.mesh" : "help_bar.mesh"), cursor));
        cursor[0] += width;
    };
    // Label element (LabelHBElement_v5): box as wide as the text, the right cap, the text.
    auto label = [&](const std::string& token) {
        std::string text = res.localize(token);
        float width = scene_->textWidth(text, "help_bar.txt") * textScale;  // LabelHBElement_v4
        mid(cursor, width);
        res.drawMeshAt("help_box_right.mesh", vp, at(local("help_box_right.mesh"), {cursor[0] + width, cursor[1], cursor[2]}));
        size_t before = scene_->hits().size();
        scene_->drawText("helpbar" + std::to_string(k++), text, "help_bar.txt", {0.847f, 0.847f, 0.847f}, vp, at(textLocal, cursor));
        if (scene_->hits().size() > before) helpHit_ = scene_->hits().back();
        cursor[0] += width + gap;
    };
    helpElems_.clear();
    for (const auto& [elem, token] : items) {
        bool hasIcon = true;
        if (elem == "strum") {
            if (strum && strum->mesh && !flowStrum) cursor = strum->mesh->trans.world.pos;
            button(elem, true);
        } else if (elem == "fret1" || elem == "fret2" || elem == "start" || elem == "fret3" || elem == "fret4" || elem == "fret5") {
            button(elem, false);
        } else {
            hasIcon = false;
        }
        helpHit_ = {};
        label(token);
        // Mouse: the label's rectangle grown left over its button.
        if (!helpHit_.key.empty()) {
            Panel::Hit h = helpHit_;
            if (hasIcon) h.x0 -= (h.y1 - h.y0) * 1.6f;
            helpElems_.push_back({elem, h});
        }
    }
}

bool UiSystem::ScreenObj::handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& result) {
    if (msg == "backwards_anim") return ui_.forward_ = false, result = Value::integer(1), true;
    if (msg == "delay_task" && !a.empty()) {  // the screen's TaskMgr
        // Memory-card dialogs (panel `dialog`): a PC saves instantly, so each
        // phase stays at least 2 s (the PS2 showed 1 s plus the card time).
        auto names = panelNames();
        bool dialog = std::find(names.begin(), names.end(), "dialog") != names.end();
        Task t{dialog ? std::max(a[0].number(), 2000.0f) : a[0].number(), {}};
        for (size_t k = 1; k < a.size(); ++k)
            if (a[k].type == Value::Type::Code) t.body.push_back(a[k].code);
        tasks_.push_back(std::move(t));
        return true;
    }
    if (msg == "is_up") return result = Value::integer(ui_.current_ == this ? 1 : 0), true;
    if (msg == "anim_task") return ui_.animTask(a), true;
    // UIScreen_v1 (0x208240): what the screen doesn't handle goes to its focus panel.
    if (ui_.current_ == this)
        if (PanelObj* fp = ui_.focusPanel()) return fp->handleNative(in, msg, a, result);
    return false;
}

bool UiSystem::PanelObj::handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& r) {
    auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
    if (native_ && native_->handle(msg, a, r)) return true;
    if (msg == "slider_selected") {  // SliderPanel: the slider being edited, or 0
        ComponentObj* sl = editingSlider();
        return r = sl ? Value::obj(sl) : Value::integer(0), true;
    }
    if (msg == "set_paused") {
        paused_ = arg(0).truthy();
        if (native_) native_->setPaused(paused_);
        return true;
    }
    if (msg == "set_display") {
        override_ = arg(0);
        hasOverride_ = true;
        return true;
    }
    if (msg == "is_up") return r = Value::integer(up_ ? 1 : 0), true;
    if (msg == "set_focus") {
        // main.dta focuses Tutorial on the first visit with a guitar and no
        // tutorial done; the port always opens on Career (user request).
        if (arg(0).str() == "main_tutorial.btn") return true;
        return setFocus(arg(0).str()), true;
    }
    if (msg == "focus_name") return r = sym(focus_), true;
    if (msg == "enable_component" || msg == "disable_component") {
        std::string c = arg(0).str();
        if (!scene_) return true;
        if (msg == "enable_component") {
            if (scene_->state(c) == UiResources::Disabled) scene_->setState(c, UiResources::Normal);
        } else if (c == "main_tutorial.btn") {
            // main.dta greys Tutorials out on a joypad without tutorial_access; on PC the
            // keyboard and pads strum, so tutorials stay playable (docs/tutorials_plan.md §2.5).
        } else if (scene_->state(c) != UiResources::Disabled) {
            if (c == focus_) navigate(Down);
            scene_->setState(c, UiResources::Disabled);
        }
        return true;
    }
    if (msg == "set_showing") return showing_ = arg(0).truthy(), true;
    if (msg == "load") return load(), true;
    if (msg == "unload") return true;
    if (msg == "delay_task" && a.size() >= 1) {
        // {$this delay_task ms [(preserve ...)] {...}}: the body is the
        // unevaluated command arguments; they arrive evaluated here, so the
        // panel keeps the code values instead.
        Task t{arg(0).number(), {}, {}};
        for (size_t k = 1; k < a.size(); ++k) {
            if (a[k].type == Value::Type::Code) t.body.push_back(a[k].code);
            else if (a[k].type == Value::Type::Array && !a[k].array->empty() && (*a[k].array)[0].str() == "preserve")
                for (size_t n = 1; n < a[k].array->size(); ++n) {  // (preserve $v ...): values captured now
                    std::string v = preserveName((*a[k].array)[n]);
                    t.preserved.push_back({v, ui_.interp_.var(v)});
                }
        }
        tasks_.push_back(std::move(t));
        return true;
    }
    if (msg == "backwards_anim") return ui_.forward_ = false, true;
    if (msg == "anim_task") return ui_.animTask(a), true;
    // TaskMgr thread_task (ThreadTask 0x241958): a script thread run by the panel's poll.
    if (msg == "thread_task") {
        auto t = std::make_unique<ThreadTask>(ui_);
        for (const Value& v : a) {
            if (v.type == Value::Type::Code) t->body.push_back(v.code);
            else if (v.type == Value::Type::Array && !v.array->empty() && (*v.array)[0].str() == "preserve")
                for (size_t n = 1; n < v.array->size(); ++n) {
                    std::string name = preserveName((*v.array)[n]);
                    t->preserved.push_back({name, ui_.interp_.var(name)});
                }
        }
        t->wake = clock_;
        threads_.push_back(std::move(t));
        return true;
    }
    if (msg == "clear_tasks") {
        for (auto& t : threads_) t->index = t->body.size();  // finished; removed by the next poll
        tasks_.clear();
        return true;
    }
    // meta (MetaPanel_v1 0x14cfb8): button meanings, guitar vs joypad.
    // MetaPanel play_movie (FUN_0024edf8): videos/<file>, blocking; the app plays it with the UI frozen.
    if (msg == "play_movie") {
        if (ui_.hooks_.playMovie && !a.empty()) ui_.hooks_.playMovie(a[0].str());
        return true;
    }
    if (msg == "set_tool_movie" || msg == "print_current_screen") return true;
    if (msg == "is_select_button") {
        int b = arg(0).integerValue();
        return r = Value::integer(ui_.guitar_ ? b == R2 : (b == Cross || b == Select)), true;
    }
    if (msg == "is_back_button") {
        int b = arg(0).integerValue();
        return r = Value::integer(ui_.guitar_ ? b == Circle : b == Triangle), true;
    }
    if (msg == "is_any_button") return r = Value::integer(ui_.guitar_ ? arg(0).integerValue() != L2 : 1), true;
    (void)in;
    return false;
}

bool UiSystem::ComponentObj::handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) {
    Panel* p = panel_.scene_.get();
    auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
    if (kind_ == UiObj::Kind::TextEntry) {
        if (msg == "get_text") return r = Value::string(done_ ? entered_ : entryText()), true;
        if (msg == "set_text") return entered_ = arg(0).str(), letter_ = 0, done_ = false, showEntry(), true;
        if (msg == "is_done") return r = Value::integer(done_), true;
        if (msg == "no_text_entered") return r = Value::integer(entered_.empty()), true;
        if (msg == "user_can_scroll") return r = Value::integer(!done_), true;
        if (msg == "resume_input") return done_ = false, showEntry(), true;
    }
    if (msg == "set_text") return p && (p->setText(name(), ui_.res_->localize(arg(0).str())), true);
    if (msg == "set_localized_text") return p && (p->setText(name(), arg(0).str()), true);
    if (msg == "set_local_pos" || msg == "set_local_scale") {
        std::array<float, 3> v{arg(0).number(), arg(1).number(), arg(2).number()};
        if (p) msg == "set_local_pos" ? p->setLocalPos(name(), v) : p->setLocalScale(name(), v);
        return true;
    }
    if (msg == "set_wrap_width") return true;  // [I] the label templates already wrap
    if (msg == "get_text") return r = Value::string(p ? p->text(name()) : ""), true;
    if (kind_ == UiObj::Kind::Slider) {  // RndSlider Handle 0x128828
        if (msg == "set_num_steps") return steps_ = arg(0).integerValue(), true;
        if (msg == "num_steps") return r = Value::integer(steps_), true;
        if (msg == "set_selected_pos") return pos_ = arg(0).integerValue(), posSet_ = true, true;
        if (msg == "selected_pos") return r = Value::integer(pos_), true;
    }
    if (kind_ == UiObj::Kind::PictureToggle) {  // RndPictureToggle: set_state / get_state / toggle are on / off
        if (msg == "set_state") return on_ = arg(0).truthy(), showToggle(), true;
        if (msg == "get_state") return r = Value::integer(on_), true;
        if (msg == "toggle") return on_ = !on_, showToggle(), true;
    }
    if (msg == "set_state") {
        if (p) p->setState(name(), UiResources::State(std::clamp(arg(0).integerValue(), 0, 2) == 2 ? UiResources::Disabled
                                                      : arg(0).integerValue() == 1 ? UiResources::Focused : UiResources::Normal));
        return true;
    }
    if (msg == "set_showing") return p && (p->setShowing(name(), arg(0).truthy()), true);
    if (msg == "set_tex_file") return p && (p->setTexFile(name(), arg(0).str()), true);
    if (msg == "showing") {
        const SceneObject* o = p ? p->scene().find(name()) : nullptr;
        return r = Value::integer(o && o->drawable() && o->drawable()->showing), true;
    }
    if (msg == "set_provider" || msg == "refresh") {
        if (msg == "set_provider") provider_ = arg(0).object ? arg(0).object : ui_.interp_.findObject(arg(0).str());
        if (auto* sp = dynamic_cast<ProviderObj*>(provider_)) sp->snapshot();
        return true;
    }
    if (msg == "selected_pos") return r = Value::integer(selected_), true;
    if (msg == "spaced_selected_pos") {
        auto* sp = dynamic_cast<ProviderObj*>(provider_);
        // FUN_00124998: sel + 2 slots per header up to the selected row.
        return r = Value::integer(sp ? selected_ + 2 * (sp->rowOf(selected_) - selected_) : selected_), true;
    }
    if (msg == "set_selected_pos") return selected_ = arg(0).integerValue(), true;
    if (msg == "scrolling") return r = Value::integer(0), true;
    if (msg == "scroll_percent") return r = Value::real(1), true;
    if (msg == "show_element" || msg == "hide_element") return true;
    return false;
}

// TextEntry: the text so far plus the letter being chosen.
std::string UiSystem::ComponentObj::entryText() const {
    const UiObj* u = panel_.scene_ ? panel_.scene_->scene().find(name())->ui.get() : nullptr;
    const UiResources::Style& st = ui_.res_->style("textentry", u ? u->style : "");
    std::string t = entered_;
    if (!done_ && int(t.size()) < std::max(1, st.length) && !st.characters.empty())
        t += st.characters[size_t(letter_) % st.characters.size()];
    return t;
}

void UiSystem::ComponentObj::showEntry() {
    if (panel_.scene_) panel_.scene_->setText(name(), entryText());
}

bool UiSystem::ComponentObj::button(int b) {
    bool select = ui_.interp_.send("meta", "is_select_button", {Value::integer(b), Value::integer(0)}).truthy();
    if (kind_ == UiObj::Kind::Slider) {  // FUN_00126c10
        if (panel_.scene_ && panel_.scene_->state(name()) == UiResources::Disabled) return false;
        auto msgVars = [&] {
            return std::map<std::string, Value>{{"component", Value::obj(this)}, {"panel", Value::obj(&panel_)},
                                                {"screen", Value::obj(ui_.current_)}, {"button", Value::integer(6)}};
        };
        if (select) {  // toggles editing; SELECT_MSG either way, then falls through
            editing_ = !editing_;
            ui_.route("component_select", msgVars());
        }
        bool back = ui_.interp_.send("meta", "is_back_button", {Value::integer(b), Value::integer(0)}).truthy();
        if (back && editing_) {
            editing_ = false;
            ui_.route("component_cancel_selection", msgVars());
            return true;
        }
        if (b == Up || b == Down) {
            int p = pos_ + (b == Down ? -1 : 1);
            if (p >= 0 && p < steps_) {
                pos_ = p, posSet_ = true;
                ui_.route("component_scroll_start", {{"component", Value::obj(this)}, {"panel", Value::obj(&panel_)},
                                                     {"screen", Value::obj(ui_.current_)}});
            }
            return true;
        }
        return select;
    }
    if (kind_ == UiObj::Kind::TextEntry) {
        // splash.dta's help text: strum cycles the letter, fret1 takes it and
        // moves on, fret2 deletes, START accepts (TEXT_ENTRY_MSG).
        if (done_) return false;
        const UiObj* u = panel_.scene_ ? panel_.scene_->scene().find(name())->ui.get() : nullptr;
        const UiResources::Style& st = ui_.res_->style("textentry", u ? u->style : "");
        int n = std::max(1, int(st.characters.size()));
        bool back = ui_.interp_.send("meta", "is_back_button", {Value::integer(b), Value::integer(0)}).truthy();
        if (b == Up || b == Down) letter_ = (letter_ + (b == Down ? 1 : n - 1)) % n;
        else if (select) {
            if (int(entered_.size()) < st.length - 1 && !st.characters.empty()) entered_ += st.characters[size_t(letter_)];
        } else if (back) {
            if (entered_.empty()) return false;  // nothing to delete: the screen's back
            letter_ = int(st.characters.find(entered_.back()));
            if (letter_ < 0) letter_ = 0;
            entered_.pop_back();
        } else if (b == Start) {
            entered_ = entryText();
            while (!entered_.empty() && entered_.back() == ' ') entered_.pop_back();
            done_ = true;
            ui_.route("text_entry", {{"component", Value::obj(this)}, {"text", Value::string(entered_)}});
        } else {
            return false;
        }
        showEntry();
        return true;
    }
    if (kind_ == UiObj::Kind::List) {
        int count = provider_ ? ui_.interp_.send(provider_, "list_length", {}).integerValue() : 0;
        if ((b == Up || b == Down) && count > 0) {
            int next = std::clamp(selected_ + (b == Down ? 1 : -1), 0, count - 1);
            if (next != selected_) {
                selected_ = next;
                ui_.route("component_scroll_start", {{"component", Value::obj(this)}, {"panel", Value::obj(&panel_)},
                                                     {"screen", Value::obj(ui_.current_)}});
                ui_.route("component_scroll", {{"component", Value::obj(this)}, {"player_num", Value::integer(0)},
                                               {"screen", Value::obj(ui_.current_)}, {"direction", Value::integer(b == Down ? 1 : -1)},
                                               {"done", Value::integer(1)}});
            }
            return true;
        }
    }
    if (!select) return false;
    if (panel_.scene_ && panel_.scene_->state(name()) == UiResources::Disabled) return false;
    if (kind_ == UiObj::Kind::Button) {
        selectFrames_ = kSelectFrames;
        if (panel_.scene_) panel_.scene_->setState(name(), UiResources::Selected);
    }
    ui_.route("component_select", {{"component", Value::obj(this)}, {"panel", Value::obj(&panel_)},
                                   {"screen", Value::obj(ui_.current_)}, {"button", Value::integer(b)}});
    return true;
}

void UiSystem::ComponentObj::drawList(const UiObj& list, const Mat4& vp, const Mat4& world) {
    // RndScrollList draw (0x1240c0) over song_element.view clones
    // (docs/song_list.md): one element per song; a tier header is column 3 of
    // its first song, which sits 2 slots lower (FUN_00125518).
    Panel* p = panel_.scene_.get();
    auto* sp = dynamic_cast<ProviderObj*>(provider_);
    if (!p || !sp) return;
    const auto& rows = sp->shown();
    float spacing = list.spacing > 0 ? list.spacing : 40;
    // Column colours (FUN_001314c0): the selected row's name / score / stars
    // take the highlight colour; headers are red; the blurb is .1 grey.
    const std::array<float, 3> text{0.1f, 0.1f, 0.1f}, header{0.66f, 0, 0}, highlight{0, 0, 1};
    // The element's items at their song_element.view offsets (position and scale).
    auto local = [&](const char* tmpl) -> Mat4 {
        const TextObj* t = ui_.res_->textTemplate(tmpl);
        return t ? Mat4::fromRowXfm(t->trans.local.rot, t->trans.local.pos) : Mat4{};
    };
    const SceneObject* check = ui_.res_->scene().find("song_checkmark.mesh");
    int k = 0, headers = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r.header) continue;
        bool hasHeader = i > 0 && rows[i - 1].header;
        if (hasHeader) ++headers;
        Xfm off;
        off.pos = {0, 0, -spacing * float(1 + k + 2 * headers)};
        Mat4 elem = world * Mat4::fromRowXfm(off.rot, off.pos);
        std::string key = name() + "#" + std::to_string(k);
        bool hl = k == selected_;
        const auto& c = hl ? highlight : text;
        p->drawText(key, r.text, "song_name.txt", c, vp, elem * local("song_name.txt"));
        if (!r.score.empty()) p->drawText(key + "#s", r.score, "song_score.txt", c, vp, elem * local("song_score.txt"));
        if (!r.stars.empty()) p->drawText(key + "#t", r.stars, "song_stars.txt", c, vp, elem * local("song_stars.txt"));
        if (hasHeader) p->drawText(key + "#h", rows[i - 1].text, "song_header.txt", header, vp, elem * local("song_header.txt"));
        if (!r.footer.empty()) p->drawText(key + "#f", r.footer, "song_blurb.txt", text, vp, elem * local("song_blurb.txt"));
        if (r.beaten && check && check->mesh)
            ui_.res_->drawMeshAt("song_checkmark.mesh", vp, elem * Mat4::fromRowXfm(check->mesh->trans.local.rot, check->mesh->trans.local.pos));
        ++k;
    }
}

void UiSystem::ComponentObj::drawSlider(const UiObj& slider, const Mat4& vp, const Mat4& world) {
    Panel* p = panel_.scene_.get();
    if (!p) return;
    const UiResources::Style& st = ui_.res_->style("slider", slider.style);
    const Scene& rs = ui_.res_->scene();
    // Style state: selected while editing (FUN_001284f0), else the component's state.
    UiResources::State state = p->state(name());
    if (editing_ && state != UiResources::Disabled) state = UiResources::Selected;
    // Parts: part.world = slider.world * part.local (0x127ed0).
    auto local = [&](const std::string& n) -> Xfm {
        const SceneObject* o = rs.find(n);
        return o && o->mesh ? o->mesh->trans.local : Xfm{};
    };
    if (!st.sliderMesh.empty()) {
        Xfm x = local(st.sliderMesh);
        ui_.res_->drawMeshAt(st.sliderMesh, vp, world * Mat4::fromRowXfm(x.rot, x.pos), st.meshColors[size_t(state)], p->ambient());
    }
    if (!st.podMesh.empty()) {
        // FUN_00128408: pod_anim at pos / (steps - 1) moves the pod to x 31 + 102 * frame
        // (y -1, z -5, its template scale); before any set_selected_pos it stays put.
        Xfm x = local(st.podMesh);
        if (posSet_) {
            float frame = steps_ < 2 ? 0.0f : float(pos_) / float(steps_ - 1);
            x.pos = {31 + 102 * frame, -1, -5};
        }
        ui_.res_->drawMeshAt(st.podMesh, vp, world * Mat4::fromRowXfm(x.rot, x.pos), {1, 1, 1}, p->ambient());
    }
    // The label: the localized token in char_slider.txt (scale .66 / .5), right-aligned at the origin.
    if (const TextObj* t = ui_.res_->textTemplate(st.textTemplate)) {
        std::string text = p->text(name());
        if (text.empty()) text = ui_.res_->localize(slider.symbol);
        p->drawText(name() + "#label", text, st.textTemplate, st.colors[size_t(state)], vp,
                    world * Mat4::fromRowXfm(t->trans.local.rot, t->trans.local.pos));
    }
}

void UiSystem::ComponentObj::showToggle() {
    Panel* p = panel_.scene_.get();
    const SceneObject* o = p ? p->scene().find(name()) : nullptr;
    if (!o || !o->ui) return;
    const UiResources::Style& st = ui_.res_->style("picturetoggle", o->ui->style);
    const std::string& tex = on_ ? st.texOn : st.texOff;
    if (!tex.empty()) p->setTexFile(name(), tex);
    toggleShown_ = true;
}

void UiSystem::ComponentObj::poll() {
    if (kind_ == UiObj::Kind::PictureToggle && !toggleShown_) showToggle();
    if (kind_ == UiObj::Kind::TextEntry && panel_.scene_ && panel_.scene_->text(name()).empty() && !done_) showEntry();
    if (selectFrames_ > 0 && --selectFrames_ == 0 && panel_.scene_ &&
        panel_.scene_->state(name()) == UiResources::Selected)
        panel_.scene_->setState(name(), panel_.focus_ == name() ? UiResources::Focused : UiResources::Normal);
}

// ---------------------------------------------------------------- system

UiSystem::UiSystem(Renderer& renderer, const GameData& data, Hooks hooks)
    : renderer_(renderer), data_(data), hooks_(std::move(hooks)), loader_(data) {
    // Macros the UI scripts rely on (config/macros.dta and the system ones).
    loader_.load("config/macros.dta");
    res_ = std::make_unique<UiResources>(renderer, data, loader_);
    interp_.locale = res_->strings();

    auto add = [this](ScriptObject* o) {
        natives_.emplace_back(o);
        interp_.registerObject(o);
    };
    try {  // arena/venues.dta: venues with (multi_only TRUE)
        DataNode venues = loader_.load("arena/venues.dta");
        for (const DataNode& v : venues.children)
            if (v.isList() && !v.children.empty() && v.children[0].isText())
                if (const DataNode* m = v.find("multi_only");
                    m && m->children.size() > 1 && (m->children[1].s == "TRUE" || (m->children[1].type == DataNode::Type::Int && m->children[1].i)))
                    multiOnlyVenues_.push_back(v.children[0].s);
    } catch (const std::exception& ex) {
        SDL_Log("ui: arena/venues.dta: %s", ex.what());
    }
    add(new ManagerObj(*this));
    add(new GameObj(*this, loader_.load("config/guitars.dta")));
    add(new ProviderObj(*this));
    add(new PlayerStatsObj(0, hooks_.playerStat, hooks_.playerCommand));
    add(new PlayerStatsObj(1, hooks_.playerStat, hooks_.playerCommand));
    add(new PlayerMatcherObj(hooks_.guitarVolume, hooks_.autoPlay));
    if (hooks_.campaign) {
        CareerContext ctx;
        ctx.campaign = hooks_.campaign;
        ctx.difficulty = [this] { return static_cast<GameObj*>(interp_.findObject("game"))->difficulty; };
        ctx.song = [this] { return interp_.send("game", "get_song", {}).str(); };
        ctx.starsOf = [this](int score) { return hooks_.starsOf ? hooks_.starsOf(score) : 3; };
        ctx.localize = [this](const std::string& t) { return res_->localize(t); };
        ctx.getPlayer = [this](std::string& c, std::string& g, std::string& sk) {
            auto* p = static_cast<GameObj*>(interp_.findObject("game"))->players[0].get();
            c = p->character, g = p->guitar, sk = p->skin;
        };
        ctx.setPlayer = [this](const std::string& c, const std::string& g, const std::string& sk) {
            auto* p = static_cast<GameObj*>(interp_.findObject("game"))->players[0].get();
            if (!c.empty()) p->character = c;
            if (!g.empty()) p->guitar = g;
            if (!sk.empty()) p->skin = sk;
        };
        ctx.changed = [this] { static_cast<ProviderObj*>(interp_.findObject("song_provider"))->invalidate(); };
        for (auto& o : makeCareerObjects(ctx)) add(o.release());
    }
    add(new SynthObj(hooks_.synth));
    add(new ArenaObj([this] { return hooks_.msPerTick ? hooks_.msPerTick() : 1.0f; },
                     [this](bool on) {
                         if (hooks_.depthOfField) hooks_.depthOfField(on);
                     }));
    add(new CrowdAudioObj(hooks_.crowd));
    add(new PlayerMgrObj([this] { return multiplayer(); }));
    // `player` (the current player in the star power handlers): {player has guitarist},
    // {{player get guitarist} start_starpower} (GuitarCharacter start_starpower).
    add(new PlayerObj([this] {
        if (hooks_.guitaristStarPower) hooks_.guitaristStarPower();
    }));
    add(new BeatmatchObj(*this));
    add(new MemcardObj(*this));
    try {
        // [I] The GD01 tips join the pool, on every difficulty.
        DataNode tips = loader_.load("config/tips.dta");
        for (int k = 0; k < kPcLoadingTipCount; ++k)
            tips.children.push_back(parseDtaText(std::string("(") + kPcLoadingTips[k] + ")").children.at(0));
        add(new TipsObj(std::move(tips), [this] { return gameDifficulty(); }, [this](const std::string& t) { return res_->localize(t); }));
    } catch (const std::exception& ex) {
        SDL_Log("ui: tips: %s", ex.what());
    }
    // Stubs only for names nothing real provides (a stub registered later would replace it).
    // `tutorial` is the tut panel (new_tutorial_panel, object_name tutorial), defined by tutorials.dta.
    for (const char* n : {"app", "tips", "memcard", "highscores", "rnd", "char_sys",
                          "bank_loader"})
        if (!interp_.findObject(n)) add(new StubObj(n));
    installVerbs();
    // config/gh.dta (ui (shared_rnd_files ghui/common.gh)): the shared camera,
    // environment and transition anims.
    try {
        common_ = std::make_unique<Panel>(renderer, data, *res_, "common");
        applyTransitionAnims(1.0f);
    } catch (const std::exception& ex) {
        SDL_Log("ui: no common.gh: %s", ex.what());
    }
    interp_.resolver = [this](const std::string& name) -> ScriptObject* {
        if (common_ && common_->scene().find(name)) {  // ui::ui.enm and friends
            auto& p = commonObjects_[name];
            if (!p) p = makeSceneProxy(*common_, name);
            return p.get();
        }
        // Bare component names resolve in the panels that are up, focus panel first.
        if (PanelObj* f = focusPanel())
            if (ComponentObj* c = f->component(name)) return c;
        for (auto& [n, p] : panels_)
            if (p->up_)
                if (ComponentObj* c = p->component(name)) return c;
        if (PanelObj* f = focusPanel())
            if (ScriptObject* o = f->sceneObject(name)) return o;
        for (auto& [n, p] : panels_)
            if (p->loaded_)
                if (ScriptObject* o = p->sceneObject(name)) return o;
        // The venue's objects (arena::lamp.view in the tutorials).
        if (hooks_.venueObject) return hooks_.venueObject(name);
        return nullptr;
    };

    // config/gh.dta: (ui (create #include ../ghui/ui.dta {meta set_defaults} {ui my_init} ...))
    // The game's gameplay handlers: config/beatmatch.dta (player_matcher_handlers),
    // config/player.dta (star_power_handlers, handlers), arena/arena_game.dta
    // ({arena add_handlers ...}), with config/sfx_macros.dta.
    loader_.load("config/sfx_macros.dta");
    auto handlerSet = [&](const char* file, const char* block, const char* name) {
        try {
            scripts_.push_back(loader_.load(file));
            if (const DataNode* b = scripts_.back().find(block)) {
                // PlayerMatcher's refresh_track_buttons (FUN_001084f0): the smashers glow by the held frets.
                auto* o = new HandlerSetObj(name, [this](const std::string& msg) {
                    if (msg == "refresh_track_buttons" && hooks_.trackMessage) hooks_.trackMessage("set_smasher_glowing", {Value::integer(-1), Value::integer(0)});
                });
                o->addHandlers(*b, 1);
                add(o);
            }
        } catch (const std::exception& ex) {
            SDL_Log("ui: %s: %s", file, ex.what());
        }
    };
    scripts_.reserve(8);
    handlerSet("config/beatmatch.dta", "player_matcher_handlers", "player_matcher_handlers");
    handlerSet("config/player.dta", "star_power_handlers", "star_power_handlers");
    handlerSet("config/player.dta", "handlers", "player_handlers");  // held_note_released, whammy_start / _end
    try {
        scripts_.push_back(loader_.load("arena/arena_game.dta"));
        for (const DataNode& c : scripts_.back().children)
            if (c.type == DataNode::Type::Command && c.children.size() > 2 && c.children[0].isText() &&
                c.children[0].s == "arena" && c.children[1].isText() && c.children[1].s == "add_handlers")
                interp_.findObject("arena")->addHandlers(c, 2);
    } catch (const std::exception& ex) {
        SDL_Log("ui: arena/arena_game.dta: %s", ex.what());
    }
    // config/system_script.dta (play_game_sfx, play_game_sequence, player_pan, ...) runs first.
    try {
        scripts_.push_back(loader_.load("config/system_script.dta"));
        interp_.run(scripts_.back());
    } catch (const std::exception& ex) {
        SDL_Log("ui: config/system_script.dta: %s", ex.what());
    }
    size_t first = scripts_.size();
    for (const char* f : {"ghui/sfx.dta", "ghui/ui.dta"}) {
        try {
            scripts_.push_back(loader_.load(f));
        } catch (const std::exception& ex) {
            SDL_Log("ui: %s: %s", f, ex.what());
        }
    }
    if (scripts_.size() > first && scripts_[first].children.size()) natives_.front()->addHandlers(scripts_[first]);  // sfx handlers on ui
    if (scripts_.size() > first + 1) interp_.run(scripts_[first + 1]);
    // The port's own screens (options > Enhanced Settings; EnhancedPanel).
    try {
        scripts_.push_back(parseDtaText(R"dta(
            {new_enhanced_panel enhanced_settings (rnd_file "")}
            {new_gh_screen enhanced_settings
                (panels meta enhanced_settings helpbar)
                (focus enhanced_settings)
                (helpbar (default ((fret1 help_select) (fret2 help_back) (strum help_updown))))}
            {new_gamepad_panel gamepads (rnd_file "")}
            {new_gh_screen gamepads
                (panels meta gamepads helpbar)
                (focus gamepads)
                (helpbar (default ((fret1 help_select) (fret2 help_back) (strum help_updown))))}
        )dta"));
        interp_.run(scripts_.back());
    } catch (const std::exception& ex) {
        SDL_Log("ui: enhanced settings: %s", ex.what());
    }
    interp_.send("meta", "set_defaults", {});
}

UiSystem::~UiSystem() = default;

void UiSystem::installVerbs() {
    auto definePanel = [this](Interp&, const DataNode& c) {
        if (c.children.size() < 2) return Value::integer(0);
        std::string name = c.children[1].s;
        auto p = std::make_unique<PanelObj>(*this, name, c);
        p->native_ = c.children[0].s == "new_gamepad_panel" ? makeGamepadPanel(*this, hooks_.input)
                                                            : makeNativePanel(c.children[0].s, name, *this, p->props);
        if (!interp_.findObject(name)) interp_.registerObject(p.get());  // natives (game) keep their name
        if (auto it = p->props.find("object_name"); it != p->props.end() && it->second.str() != name)
            if (!interp_.findObject(it->second.str())) interp_.registerAs(it->second.str(), p.get());  // also by its object name
        panels_[name] = std::move(p);
        return Value::integer(0);
    };
    for (const char* v : {"new_gh_panel", "new_panel", "new_metagame_panel", "new_helpbar_panel", "new_charsys_panel",
                          "new_guitar_display_panel", "new_guitar_select_panel", "new_store_panel", "new_status_panel",
                          "new_slider_panel", "new_credits_panel", "new_game_panel", "new_arena_panel",
                          "new_fade_panel", "new_hud_panel", "new_track_panel", "new_multi_char_sel_panel",
                          "new_tutorial_panel", "new_enhanced_panel", "new_gamepad_panel"})
        interp_.addBuiltin(v, definePanel);
    auto defineScreen = [this](Interp&, const DataNode& c) {
        if (c.children.size() < 2) return Value::integer(0);
        std::string name = c.children[1].s;
        auto s = std::make_unique<ScreenObj>(*this, name, c);
        if (!interp_.findObject(name)) interp_.registerObject(s.get());
        screens_[name] = std::move(s);
        return Value::integer(0);
    };
    interp_.addBuiltin("new_gh_screen", defineScreen);

    // Script functions the game registers natively.
    interp_.addBuiltin("song_preview", [this](Interp& in, const DataNode& c) {
        std::string id = c.children.size() > 1 ? in.eval(c.children[1]).str() : "";
        if (hooks_.songPreview) hooks_.songPreview(id);
        return Value::integer(0);
    });
    interp_.addBuiltin("meta_music_start", [this](Interp&, const DataNode&) {
        if (hooks_.menuMusic) hooks_.menuMusic(true);
        return Value::integer(0);
    });
    interp_.addBuiltin("meta_music_stop", [this](Interp&, const DataNode&) {
        if (hooks_.menuMusic) hooks_.menuMusic(false);
        return Value::integer(0);
    });
    // autosave_goto (system_script.dta) runs as written: the `autosave` memcard
    // screen saves through the `memcard` object.
    // animate_forever_30fps panel obj: loop obj's animation at 30 fps.
    interp_.addBuiltin("animate_forever_30fps", [this](Interp& in, const DataNode& c) {
        if (c.children.size() < 3) return Value::integer(0);
        Value p = in.eval(c.children[1]);
        std::string obj = in.eval(c.children[2]).str();
        if (auto* po = dynamic_cast<PanelObj*>(p.object)) po->loops_.push_back(obj);
        return Value::integer(0);
    });
    // {play_sfx name (volume v) (transpose t) (pan p) (stop B)} (FUN_00213038), {stop_sfx name}.
    interp_.addBuiltin("play_sfx", [this](Interp& in, const DataNode& c) {
        if (c.children.size() < 2 || !hooks_.synth) return Value::integer(0);
        std::string name = in.eval(c.children[1]).str();
        float vol = 1, tr = 0, pan = 0;
        bool stop = false;
        for (size_t k = 2; k < c.children.size(); ++k) {
            const DataNode& o = c.children[k];
            if (!o.isList() || o.children.size() < 2 || !o.children[0].isText()) continue;
            Value v = in.eval(o.children[1]);
            if (o.children[0].s == "volume") vol = v.number();
            else if (o.children[0].s == "transpose") tr = v.number();
            else if (o.children[0].s == "pan") pan = v.number();
            else if (o.children[0].s == "stop") stop = v.truthy();
        }
        hooks_.synth->playSfx(name, vol, tr, pan, stop);
        return Value::integer(0);
    });
    interp_.addBuiltin("stop_sfx", [this](Interp& in, const DataNode& c) {
        if (c.children.size() > 1 && hooks_.synth) hooks_.synth->stopSfx(in.eval(c.children[1]).str());
        return Value::integer(0);
    });
    interp_.addBuiltin("new_screen", defineScreen);
}

UiSystem::PanelObj* UiSystem::panel(const std::string& name) const {
    auto it = panels_.find(name);
    return it == panels_.end() ? nullptr : it->second.get();
}

UiSystem::ScreenObj* UiSystem::screen(const std::string& name) const {
    auto it = screens_.find(name);
    return it == screens_.end() ? nullptr : it->second.get();
}

UiSystem::PanelObj* UiSystem::focusPanel() const {
    if (!current_) return nullptr;
    auto it = current_->props.find("focus");
    return it == current_->props.end() ? nullptr : panel(it->second.str());
}

const std::string& UiSystem::currentScreen() const { return current_ ? current_->name() : none_; }

Value UiSystem::send(ScriptObject* o, const std::string& message, const std::map<std::string, Value>& vars,
                     bool& handled) {
    handled = false;
    const DataNode* h = o ? o->handler(message) : nullptr;
    if (!h) return unhandled();
    // Message macros carry the parameter list, e.g. SELECT_MSG =
    // component_select ($component $panel $screen $button): pass the
    // message's data in that order.
    std::vector<Value> args;
    if (h->children.size() > 1 && h->children[1].type == DataNode::Type::Array)
        for (const DataNode& p : h->children[1].children) {
            std::string name = !p.s.empty() && p.s[0] == '$' ? p.s.substr(1) : p.s;
            auto it = vars.find(name);
            args.push_back(it == vars.end() ? Value::integer(0) : it->second);
        }
    Value r = interp_.send(o, message, std::move(args));
    handled = !Interp::unhandled(r);
    return r;
}

void UiSystem::route(const std::string& message, const std::map<std::string, Value>& vars) {
    bool handled = false;
    send(natives_.front().get(), message, vars, handled);  // ui: sfx handlers
    if (handled || !current_) return;
    PanelObj* fp = focusPanel();
    if (message == "button_down") {
        // GHScreen auto-back (FUN_001435e0).
        int b = vars.at("button").integerValue();
        bool back = interp_.send("meta", "is_back_button", {Value::integer(b), Value::integer(0)}).truthy();
        if (back && current_->flag("allow_back", true) && current_->back_) {
            bool h1 = false, h2 = false;
            send(current_, message, vars, h1);
            if (!h1 && fp) h2 = fp->handle(message, vars);
            if (!h1 && !h2) {
                ScreenObj* old = current_;
                gotoScreen(current_->back_->name());
                route("screen_back", {{"old_screen", Value::obj(old)}, {"prev_screen", Value::obj(current_)}});
            }
            return;
        }
    }
    send(current_, message, vars, handled);
    if (handled) return;
    if (fp) fp->handle(message, vars);
}

void UiSystem::exitScreen(ScreenObj* cur, ScreenObj* next) {
    bool h;
    send(cur, "exit", {}, h);
    std::vector<std::string> keep = next ? next->panelNames() : std::vector<std::string>{};
    for (const std::string& pn : cur->panelNames()) {
        PanelObj* p = panel(pn);
        if (!p || !p->up_) continue;
        bool force = p->props.count("force_exit") && p->props["force_exit"].truthy();
        if (force || !next || std::find(keep.begin(), keep.end(), pn) == keep.end()) p->exit();
    }
}

void UiSystem::enterScreen(ScreenObj* next) {
    ScreenObj* prev = current_;
    current_ = next;
    // The PS2 showed the loading poster while the song loaded from disc; a
    // PC loads at once, so it stays up at least 2 s (the memory-card rule).
    if (next->name() == "loading") holdUntilMs_ = frameMs_ + 2000;
    // A {helpbar set_display ...} belongs to the screen that sent it; the new
    // screen shows its own (helpbar ...) property unless its enter sets one.
    if (PanelObj* hb = panel("helpbar")) hb->hasOverride_ = false;
    bool going_back = prev && prev->back_ == next;
    for (const std::string& pn : next->panelNames())
        if (PanelObj* p = panel(pn); p && !p->up_) p->enter();
    bool h;
    send(next, "enter", {}, h);
    if (!going_back && prev && prev->flag("allow_back", true)) next->back_ = prev;
    // The game screen starts the song when it comes up from outside the game
    // (loading), or after `game restart`; resuming from pause continues it.
    auto* g = static_cast<GameObj*>(interp_.findObject("game"));
    auto showsGame = [](ScreenObj* sc) {
        if (!sc) return false;
        auto names = sc->panelNames();
        return std::find(names.begin(), names.end(), "game") != names.end();
    };
    bool fromGame = showsGame(prev);
    // Tutorials: tut_script is the game screen (TutorialPanel; docs/tutorials_plan.md §2.4).
    if (hooks_.tutorialLoad && next->name() == "tut_script" && !fromGame && g->engineSongId >= 0) {
        hooks_.tutorialLoad(g->engineSongId);
        if (hooks_.tutorialStart) hooks_.tutorialStart(g->skipIntro, g->startPaused);
    }
    if (hooks_.startGame && next->name() == "game" && (!fromGame || g->restartPending)) {
        g->restartPending = false;
        auto* provider = static_cast<ProviderObj*>(interp_.findObject("song_provider"));
        // Career: the selected venue, the player's character and guitar skin;
        // quickplay: the song's own (songs.dta quickplay), "" here.
        // The game config: quickplay copied the song's setup into it, career the player's choices.
        int songId = g->engineSongId >= 0 ? g->engineSongId : provider->engineSong(g->songIndex);
        if (SDL_getenv("GD01_UILOG"))
            SDL_Log("ui: start game: song %d, difficulty %d, venue %s, character %s, guitar %s", songId, g->difficulty,
                    g->venue.c_str(), g->players[0]->character.c_str(),
                    (g->players[0]->skin.empty() ? g->players[0]->guitar : g->players[0]->skin).c_str());
        hooks_.startGame(songId, g->difficulty, g->venue, g->players[0]->character,
                         g->players[0]->skin.empty() ? g->players[0]->guitar : g->players[0]->skin);
    }
    if (fromGame && !showsGame(next) && hooks_.leaveGame) hooks_.leaveGame();
}

bool UiSystem::arenaShowing() {
    auto* arena = dynamic_cast<ArenaObj*>(interp_.findObject("arena"));
    return !arena || arena->showing();
}

void UiSystem::setArenaShowing(bool on) {
    if (auto* arena = dynamic_cast<ArenaObj*>(interp_.findObject("arena"))) arena->setShowing(on);
}

bool UiSystem::inGame() const {
    if (!current_) return false;
    auto names = current_->panelNames();
    return std::find(names.begin(), names.end(), "game") != names.end();
}

void UiSystem::gotoScreen(const std::string& name) {
    if (SDL_getenv("GD01_UILOG")) SDL_Log("ui: goto_screen %s", name.c_str());  // development aid
    ScreenObj* next = screen(name);
    if (!next) {
        SDL_Log("ui: no screen %s", name.c_str());
        return;
    }
    if (next == current_ || next == target_) return;
    if (phase_ != Phase::Idle || entering_ || frameMs_ < holdUntilMs_) {
        // Requested during a transition (or from an enter handler): it runs
        // once the current one completes.
        pending_ = name;
        return;
    }
    ScreenObj* cur = current_;
    target_ = next;
    // GHScreen_v5: going back when the target is in this screen's back chain.
    goingBack_ = !forward_;
    for (ScreenObj* s = cur ? cur->back_ : nullptr; s && !goingBack_; s = s->back_) goingBack_ = s == next;
    route("screen_change", {{"new_screen", Value::obj(next)}, {"old_screen", Value::obj(cur)}});
    if (cur) exitScreen(cur, next);
    if (cur && cur->flag("animate_transition", true)) {
        // Out-anim (FUN_00143290): forward exits play meta_cam_pause_right_out,
        // backward ones meta_cam_right_in reversed; the ambient fades out.
        phase_ = Phase::Out;
        phaseMs_ = 0;
        drawn_ = cur;
    } else {
        startIn();
    }
}

void UiSystem::startIn() {
    ScreenObj* prev = current_;
    transitionFrom_ = prev;
    phase_ = Phase::In;
    phaseMs_ = 0;
    entering_ = true;
    enterScreen(target_);
    entering_ = false;
    drawn_ = current_;
    if (!current_->flag("animate_transition", true)) finishTransition();
}

void UiSystem::finishTransition() {
    if (phase_ == Phase::Out) startIn();
    if (phase_ == Phase::Idle && !target_) return;
    phase_ = Phase::Idle;
    target_ = nullptr;
    forward_ = true;
    applyTransitionAnims(1.0f);
    route("transition_complete", {{"new_screen", Value::obj(current_)}, {"old_screen", Value::obj(transitionFrom_)}});
    if (!pending_.empty() && frameMs_ >= holdUntilMs_) {
        std::string next = std::move(pending_);
        pending_.clear();
        gotoScreen(next);
    }
}

// Drives the shared camera and ambient for the current phase at progress t (0..1).
void UiSystem::applyTransitionAnims(float t) {
    if (!common_) return;
    float f = std::clamp(t, 0.0f, 1.0f) * kTransitionMs;
    Panel& c = *common_;
    if (phase_ == Phase::Out) {
        c.setFrame("ui.enm", kTransitionMs - f);
        if (goingBack_) c.setFrame("meta_cam_right_in.tnm", kTransitionMs - f);
        else c.setFrame("meta_cam_pause_right_out.tnm", f);
    } else if (phase_ == Phase::In) {
        // Forward: right_in (turned -> straight); back: right_out reversed.
        c.setFrame("ui.enm", f);
        if (goingBack_) c.setFrame("meta_cam_right_out.tnm", kTransitionMs - f);
        else c.setFrame("meta_cam_right_in.tnm", f);
    } else {
        c.setFrame("ui.enm", kTransitionMs);
        c.setFrame("meta_cam_right_in.tnm", kTransitionMs);
    }
}

void UiSystem::later(float ms, std::function<void()> fn) { timed_.push_back({ms, std::move(fn)}); }

void UiSystem::update(float dtMs) {
    frameMs_ += dtMs;
    if (phase_ == Phase::Idle && !pending_.empty() && frameMs_ >= holdUntilMs_) {
        std::string next = std::move(pending_);
        pending_.clear();
        gotoScreen(next);
    }
    if (updateBoot(dtMs)) return;
    repeatHeld(dtMs);
    for (size_t k = 0; k < timed_.size();) {
        timed_[k].first -= dtMs;
        if (timed_[k].first > 0) {
            ++k;
            continue;
        }
        auto fn = std::move(timed_[k].second);
        timed_.erase(timed_.begin() + long(k));
        fn();
    }
    for (size_t k = 0; k < interpTasks_.size();) {
        InterpTask& t = interpTasks_[k];
        t.elapsed += dtMs;
        float u = std::min(1.0f, t.elapsed / t.ms);
        interp_.var(t.var) = Value::real(t.from + (t.to - t.from) * u);
        for (const DataNode* n : t.body) interp_.eval(*n);
        if (u >= 1) interpTasks_.erase(interpTasks_.begin() + long(k));
        else ++k;
    }
    if (auto* arena = dynamic_cast<ArenaObj*>(interp_.findObject("arena"))) arena->poll(interp_, dtMs);
    if (current_) current_->poll(dtMs);
    for (size_t k = 0; k < animTasks_.size();) {
        AnimTask& t = animTasks_[k];
        t.elapsed += dtMs;
        float u = t.ms > 0 ? std::min(1.0f, t.elapsed / t.ms) : 1.0f;
        Value f = Value::real(t.from + (t.to - t.from) * u);
        if (t.target.isText()) interp_.send(t.target.str(), "set_frame", {f});
        else if (t.target.object) interp_.send(t.target.object, "set_frame", {f});
        if (u >= 1) animTasks_.erase(animTasks_.begin() + long(k));
        else ++k;
    }
    if (phase_ != Phase::Idle) {
        phaseMs_ += dtMs;
        applyTransitionAnims(phaseMs_ / kTransitionMs);
        if (phaseMs_ >= kTransitionMs) {
            if (phase_ == Phase::Out) startIn();
            else finishTransition();
        }
    }
    if (!current_) return;
    bool h;
    if (current_->handler("poll")) send(current_, "poll", {}, h);
    for (const std::string& pn : current_->panelNames())
        if (PanelObj* p = panel(pn); p && p->up_) p->poll(dtMs);
}

void UiSystem::draw() {
    if (!logos_.empty()) {
        // The logos' own backgrounds are black: so is the screen around them.
        renderer_.beginWide(100.0f);
        renderer_.fillRect(0, 0, kScreenW, kScreenH, {0, 0, 0, 255});
        renderer_.endWide();
        logos_.front()->draw("splash.view");
        return;
    }
    ScreenObj* s = drawn_ ? drawn_ : current_;
    if (!s) return;
    // GHScreen_v4: the help bar draws last, on top, and only while the UI
    // manager is idle; it vanishes when goto_screen is called and returns
    // once the new screen's transition completes.
    PanelObj* help = nullptr;
    for (const std::string& pn : s->panelNames())
        // During the out-anim the exiting screen's panels are still drawn.
        if (PanelObj* p = panel(pn); p && (p->up_ || s != current_ || phase_ == Phase::Out)) {
            if (pn == "helpbar") {
                help = p;
                continue;
            }
            renderer_.clearDepth();
            // The in-game HUD was laid out for 4:3: its slide-in start poses
            // sit just outside that area, so it is clipped to it.
            bool clip = pn == "hud";
            if (clip) renderer_.clip43();
            p->draw();
            if (clip) renderer_.clipEnd();
        }
    if (help && phase_ == Phase::Idle && pending_.empty()) {
        renderer_.clearDepth();
        help->draw();
    }
}

bool UiSystem::isSelectButton(int b) {
    return interp_.send("meta", "is_select_button", {Value::integer(b), Value::integer(0)}).truthy();
}

bool UiSystem::isBackButton(int b) {
    return interp_.send("meta", "is_back_button", {Value::integer(b), Value::integer(0)}).truthy();
}

namespace {
PlayerConfigObj* playerConfig(Interp& in, int p) {
    Value v = in.send("game", "get_player_config", {Value::integer(std::clamp(p, 0, 1))});
    return dynamic_cast<PlayerConfigObj*>(v.object);
}
}  // namespace

std::string UiSystem::playerCharacter(int p) {
    PlayerConfigObj* c = playerConfig(interp_, p);
    return c ? c->character : "";
}

void UiSystem::setPlayerCharacter(int p, const std::string& character) {
    if (PlayerConfigObj* c = playerConfig(interp_, p)) c->character = character;
}

std::string UiSystem::playerGuitar(int p) {
    PlayerConfigObj* c = playerConfig(interp_, p);
    return c ? c->guitar : "";
}

std::string UiSystem::playerSkin(int p) {
    PlayerConfigObj* c = playerConfig(interp_, p);
    return c ? c->skin : "";
}

bool UiSystem::unlocked(const std::string& item) {
    return interp_.send("campaign", "is_unlocked", {Value::symbol(item)}).truthy();
}

Panel* UiSystem::panelScene(const std::string& name) {
    PanelObj* p = panel(name);
    return p ? p->scene_.get() : nullptr;
}

bool UiSystem::unlockedAny(const std::string& item) {
    return !hooks_.campaign || hooks_.campaign->isUnlockedAny(item);
}

Value UiSystem::focusComponent() {
    PanelObj* f = focusPanel();
    return Value::symbol(f ? f->focus_ : "");
}

Value UiSystem::screenValue() { return Value::obj(current_); }

// App::App (FUN_00100258 / FUN_00152b38): the RedOctane, MTV Games and
// Harmonix logos (ghui/gen/<name>_splash.rnd_ps2, splash.view), 3000 ms each,
// not skippable; then the scripts' $first_screen (bootup_load).
void UiSystem::boot(bool skipLogos) {
    if (!skipLogos)
        for (const char* n : {"pub", "mtv", "harmonix"}) {
            std::string path = std::string("ghui/gen/") + n + "_splash.rnd_ps2";
            if (!data_.exists(path)) continue;
            try {
                logos_.push_back(std::make_unique<Panel>(renderer_, data_, *res_, path));
            } catch (const std::exception& ex) {
                SDL_Log("ui: logo %s: %s", path.c_str(), ex.what());
            }
        }
    logoMs_ = 0;
    if (logos_.empty()) gotoScreen(interp_.var("first_screen").str().empty() ? "main" : interp_.var("first_screen").str());
}

bool UiSystem::updateBoot(float dtMs) {
    if (logos_.empty()) return false;
    logoMs_ += dtMs;
    if (logoMs_ >= 3000) {
        logos_.erase(logos_.begin());
        logoMs_ = 0;
        if (logos_.empty()) {
            gotoScreen(interp_.var("first_screen").str().empty() ? "main" : interp_.var("first_screen").str());
            return false;
        }
    }
    return true;
}

int UiSystem::storePrice(const std::string& category, const std::string& item) {
    return hooks_.campaign ? hooks_.campaign->price(category, item) : 0;
}

// StorePanel low_cost / high_cost: over every item of the category (starts 10,000,000 / 0).
int UiSystem::storePriceRange(const std::string& category, bool highest) {
    Campaign* c = hooks_.campaign;
    if (!c) return 0;
    static const std::map<std::string, Campaign::Category> kCats = {{"guitar", Campaign::kGuitar}, {"skin", Campaign::kSkin},
                                                                    {"song", Campaign::kSong}, {"character", Campaign::kCharacter},
                                                                    {"video", Campaign::kVideo}};
    auto cat = kCats.find(category);
    int best = highest ? 0 : 10000000;
    if (cat == kCats.end()) return best;
    for (const Campaign::Item& it : c->career(gameDifficulty()).items) {
        if (it.cat != cat->second || !it.isStore) continue;
        int p = c->price(category, it.sym);
        best = highest ? std::max(best, p) : std::min(best, p);
    }
    return best;
}

int UiSystem::gameDifficulty() const {
    auto* g = dynamic_cast<GameObj*>(interp_.findObject("game"));
    return g ? g->difficulty : 0;
}

std::string UiSystem::gameVenue() const {
    auto* g = dynamic_cast<GameObj*>(interp_.findObject("game"));
    return g ? g->venue : "";
}

void UiSystem::gameEvent(const std::string& object, const std::string& msg, std::vector<Value> args) {
    if (ScriptObject* o = interp_.findObject(object)) interp_.send(o, msg, std::move(args));
}

void UiSystem::resetGameTasks() {
    interpTasks_.clear();
    if (auto* arena = dynamic_cast<ArenaObj*>(interp_.findObject("arena"))) arena->clearTasks();
}

void UiSystem::sendPanel(const std::string& name, const std::string& msg, std::vector<Value> args) {
    PanelObj* p = panel(name);
    if (!p) return;
    Value saved = interp_.var("this");
    interp_.send(p, msg, std::move(args));
    interp_.var("this") = saved;
}

void UiSystem::animTask(const std::vector<Value>& a) {
    if (a.size() < 4) return;
    animTasks_.push_back({a[0], std::max(a[1].number(), 0.0f), 0, a[2].number(), a[3].number()});
    Value t = a[0];
    if (t.isText()) interp_.send(t.str(), "set_frame", {Value::real(a[2].number())});
    else if (t.object) interp_.send(t.object, "set_frame", {Value::real(a[2].number())});
}

void UiSystem::repeatTiming(float& holdMs, float& repeatMs) {
    holdMs = 1000, repeatMs = 80;  // config/gh.dta (joypad (hold_ms 1000) (repeat_ms 80))
    if (!current_) return;
    for (const std::string& n : current_->panelNames()) {
        PanelObj* p = panel(n);
        const DataNode* j = p && p->up_ && p->def_ ? p->def_->find("joypad") : nullptr;
        if (!j) continue;
        if (const DataNode* h = j->find("hold_ms")) holdMs = h->floatAt(1);
        if (const DataNode* r = j->find("repeat_ms")) repeatMs = r->floatAt(1);
    }
}

void UiSystem::repeatHeld(float dtMs) {
    for (size_t p = 0; p < held_.size(); ++p) {
        HeldButton& h = held_[p];
        if (h.button < 0) continue;
        if (!logos_.empty() || phase_ != Phase::Idle) {
            h.ms = 0, h.repeating = false;  // restarts after a transition
            continue;
        }
        float holdMs, repeatMs;
        repeatTiming(holdMs, repeatMs);
        h.ms += dtMs;
        float wait = h.repeating ? repeatMs : holdMs;
        if (h.ms < wait) continue;
        h.ms = h.repeating ? std::fmod(h.ms - wait, std::max(repeatMs, 1.0f)) : 0;
        h.repeating = true;
        route("button_down", {{"button", Value::integer(h.button)}, {"player_num", Value::integer(int(p))}});
    }
}

void UiSystem::buttonDown(int button, bool guitar, int player) {
    if (player == 0) guitar_ = guitar;
    if (button == Up || button == Down || button == Left || button == Right) held_[size_t(player & 1)] = {button, 0, false};
    if (!logos_.empty()) {
        // The original's logos are not skippable; the port skips them all on
        // select / back (Enter, Esc) or Start.
        if (button == Cross || button == R2 || button == Circle || button == Triangle || button == Start) {
            logos_.clear();
            logoMs_ = 0;
            gotoScreen(interp_.var("first_screen").str().empty() ? "main" : interp_.var("first_screen").str());
        }
        return;
    }
    if (phase_ != Phase::Idle) return;  // input is gated during transitions
    route("button_down", {{"button", Value::integer(button)}, {"player_num", Value::integer(player)}});
}

void UiSystem::buttonUp(int button, int player) {
    if (held_[size_t(player & 1)].button == button) held_[size_t(player & 1)] = {};
    if (phase_ != Phase::Idle) return;
    route("button_up", {{"button", Value::integer(button)}, {"player_num", Value::integer(player)}});
}

void UiSystem::playerSetup(int p, std::string& character, std::string& guitar) const {
    auto* g = static_cast<GameObj*>(interp_.findObject("game"));
    if (!g || p < 0 || p > 1 || !g->players[p]) return;
    character = g->players[p]->character;
    guitar = g->players[p]->skin.empty() ? g->players[p]->guitar : g->players[p]->skin;
}

bool UiSystem::mouse(float x, float y, bool click) {
    if (!logos_.empty() || phase_ != Phase::Idle || !current_) return false;
    auto inside = [&](const Panel::Hit& h, float pad) { return x >= h.x0 - pad && x <= h.x1 + pad && y >= h.y0 - pad && y <= h.y1 + pad; };
    const int select = guitar_ ? R2 : Cross;
    auto press = [&](int b) {
        buttonDown(b, guitar_, 0);
        buttonUp(b, 0);
    };
    // The help bar: CONTINUE / BACK (and Start) act as their buttons.
    if (PanelObj* hb = panel("helpbar"); hb && hb->up_)
        for (const auto& [elem, h] : hb->helpElems_)
            if (inside(h, 4)) {
                int b = elem == "fret1" ? select : elem == "fret2" ? (guitar_ ? Circle : Triangle) : elem == "start" ? Start
                        : elem == "fret3" ? L1 : -1;
                if (click && b >= 0) press(b);
                if (click && elem == "fret4") gotoScreen("gamepads");  // the main menu's GAMEPADS
                return true;
            }
    PanelObj* fp = focusPanel();
    if (!fp) return false;
    if (fp->native_ && fp->native_->mouse(x, y, click)) return true;
    if (!fp->scene_) return false;
    for (const Panel::Hit& h : fp->scene_->hits()) {
        if (!inside(h, 6)) continue;
        size_t hash = h.key.find('#');
        std::string comp = h.key.substr(0, hash);
        ComponentObj* c = fp->component(comp);
        if (!c || fp->scene_->state(comp) == UiResources::Disabled) continue;
        bool navigable = std::find(fp->vertical_.begin(), fp->vertical_.end(), comp) != fp->vertical_.end() ||
                         std::find(fp->horizontal_.begin(), fp->horizontal_.end(), comp) != fp->horizontal_.end();
        if (c->kind() == UiObj::Kind::List && hash != std::string::npos) {
            // The list keeps its selection in one slot: a hover would scroll
            // it, so lists only take clicks (and the wheel).
            if (!click) return true;
            int row = std::atoi(h.key.c_str() + hash + 1);
            if (fp->focus_ != comp) fp->setFocus(comp);
            if (row != c->selected_) {
                int dir = row > c->selected_ ? 1 : -1;
                c->selected_ = row;
                route("component_scroll_start", {{"component", Value::obj(c)}, {"panel", Value::obj(fp)}, {"screen", Value::obj(current_)}});
                route("component_scroll", {{"component", Value::obj(c)}, {"player_num", Value::integer(0)}, {"screen", Value::obj(current_)},
                                           {"direction", Value::integer(dir)}, {"done", Value::integer(1)}});
            }
            if (click) press(select);
            return true;
        }
        if (!navigable && fp->focus_ != comp) continue;
        if (fp->focus_ != comp) fp->setFocus(comp);
        if (click) press(select);
        return true;
    }
    return false;
}

void UiSystem::wheel(int dir) {
    if (!logos_.empty() || phase_ != Phase::Idle || !current_) return;
    const int b = dir < 0 ? Up : Down;
    buttonDown(b, guitar_, 0);
    buttonUp(b, 0);
}

bool UiSystem::trackShowing() const {
    PanelObj* p = panel("track");
    return !p || p->showing_;
}

void UiSystem::setTrackShowing(bool on) {
    if (PanelObj* p = panel("track")) p->showing_ = on;
}

bool UiSystem::multiplayer() const {
    auto* g = static_cast<GameObj*>(interp_.findObject("game"));
    return g && g->multiplayer;
}

// FUN_00104fd0 (GamePanel_v16): the newspaper photo is
// ghui/image/photo_<player's character><0..3>.bmp, put into winner_photo0.tex.
void UiSystem::PanelObj::fillStatus() {
    auto* g = static_cast<GameObj*>(ui_.interp_.findObject("game"));
    std::string character = g && g->players[0] ? g->players[0]->character : "";
    if (!character.empty()) {
        std::string path = "ghui/image/gen/status_" + character + "0.png_ps2";
        if (ui_.data_.exists(path)) try {
                Bytes b = ui_.data_.read(path);
                scene_->renderer().replaceTexture("status_char1.tex", decodeBitmap(b.data(), b.size(), nullptr));
            } catch (const std::exception& ex) {
                SDL_Log("ui: status picture %s: %s", path.c_str(), ex.what());
            }
    }
    const DataNode* text = def_->find("text");
    if (!text) return;
    int n = ui_.interp_.send("campaign", "status", {}).integerValue();
    const DataNode* entry = text->find("status" + std::to_string(n));
    if (!entry) return;
    for (size_t k = 1; k < entry->children.size(); ++k) {
        const DataNode& e = entry->children[k];
        if (!e.isList() || e.children.empty() || !e.children[0].isText()) continue;
        const DataNode* label = e.find("label");
        if (!label || label->children.size() < 2) continue;
        std::string str;
        if (e.children[0].s == "band") {
            Value slot = ui_.interp_.send("campaign", "profile_slot", {});
            str = ui_.interp_.send("campaign", "profile_name", {slot}).str();
        } else {
            str = ui_.res_->localize(character);
        }
        if (const DataNode* t = e.find("token"); t && t->children.size() > 1) {
            std::string fmt = ui_.res_->localize(t->textAt(1));
            if (size_t at = fmt.find("%s"); at != std::string::npos) str = fmt.replace(at, 2, str);
            else str = fmt;
        }
        scene_->setText(label->textAt(1), str);
    }
}

void UiSystem::loadWinnerPhoto(Panel& panel) {
    auto* g = static_cast<GameObj*>(interp_.findObject("game"));
    if (!g) return;
    // GamePanel_v15/16: each player's photo (winner_photo0 / 1 in multi_compete).
    for (int p = 0; p < (g->multiplayer ? 2 : 1); ++p) {
        if (!g->players[p]) continue;
        std::string character = g->players[p]->character;
        if (character.empty()) continue;
        static std::mt19937 rng{std::random_device{}()};
        std::string path = "ghui/image/gen/photo_" + character + std::to_string(std::uniform_int_distribution<int>(0, 3)(rng)) + ".bmp_ps2";
        if (!data_.exists(path)) {
            SDL_Log("ui: no winner photo %s", path.c_str());
            continue;
        }
        try {
            Bytes b = data_.read(path);
            panel.renderer().replaceTexture("winner_photo" + std::to_string(p) + ".tex", decodeBitmap(b.data(), b.size(), nullptr));
        } catch (const std::exception& ex) {
            SDL_Log("ui: winner photo %s: %s", path.c_str(), ex.what());
        }
    }
}

float UiSystem::textWidth(const std::string& font, const std::string& text, float height) {
    const FontMetrics* fm = res_ ? res_->font(font) : nullptr;
    if (!fm || text.empty() || height <= 0) return 0;
    TextStyle ts;
    ts.align = 0x11;
    TextMesh tm = layoutText(*fm, text, ts);
    return tm.lineHeight > 0 ? tm.width * height / tm.lineHeight : 0;
}

void UiSystem::transition(int& phase, float& t, bool& back) {
    phase = phase_ == Phase::Out ? 1 : phase_ == Phase::In ? 2 : 0;
    t = phase ? std::clamp(phaseMs_ / kTransitionMs, 0.0f, 1.0f) : 1.0f;
    back = goingBack_;
}

void UiSystem::drawText(const std::string& font, const std::string& text, float x, float y, float height,
                        const std::array<float, 4>& rgba, uint32_t align) {
    const FontMetrics* fm = res_ ? res_->font(font) : nullptr;
    if (!fm || text.empty() || height <= 0) return;
    // Laid out once at size 1 (one cell wide) and scaled: the line height is cellH / cellW.
    TextCache& c = textCache_[font + "|" + text];
    if (!c.mesh) {
        TextStyle ts;
        ts.align = 0x11;
        TextMesh tm = layoutText(*fm, text, ts);
        if (tm.indices.empty()) return;
        c.mesh = renderer_.createMesh(tm.vertices, tm.indices);
        c.width = tm.width, c.height = tm.lineHeight;
    }
    c.used = ++textFrame_;
    float s = c.height > 0 ? height / c.height : 1;
    float w = c.width * s;
    if (align & 0x2) x -= w * 0.5f;
    else if (align & 0x4) x -= w;
    if (align & 0x20) y -= height * 0.5f;
    else if (align & 0x40) y -= height;
    // Text quads lie in the XZ plane (z up); the menu screen is 1280 x 720, y down.
    Mat4 m;
    m.at(0, 0) = 2.0f * s / kScreenW, m.at(0, 3) = 2.0f * x / kScreenW - 1;
    m.at(1, 1) = 0, m.at(1, 2) = 2.0f * s / kScreenH, m.at(1, 3) = 1 - 2.0f * y / kScreenH;
    m.at(2, 2) = 0;
    Renderer::RenderState st;
    st.texture = res_->fontTexture(font);
    st.blend = Blend::Alpha;
    st.depthTest = false;
    st.depthWrite = false;
    std::copy_n(rgba.data(), 4, st.tint);
    renderer_.drawMesh(c.mesh, m.m.data(), st);
}
