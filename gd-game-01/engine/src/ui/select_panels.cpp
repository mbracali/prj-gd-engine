#include "ui/select_panels.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <random>

#include "data/dta.h"
#include "data/game_data.h"
#include "render/renderer.h"
#include "render/scene_renderer.h"
#include "scene/character.h"
#include "ui/panel.h"

namespace {

// AI Upscaling: FSR 3.1 and DLSS 4 run in Vulkan beside desktop OpenGL
// (render/vk_upscaler.h); the Android build has neither.
#ifdef __ANDROID__
constexpr bool kVkUpscalers = false;
#else
constexpr bool kVkUpscalers = true;
#endif

// PS2 pad buttons (UiSystem::Button).
constexpr int kUp = 12, kDown = 14;

std::vector<std::string> listOf(const Value& v) {
    std::vector<std::string> out;
    if (v.type == Value::Type::Array)
        for (const Value& e : *v.array) out.push_back(e.str());
    else if (v.isText())
        out.push_back(v.s);
    return out;
}

Value prop(const std::map<std::string, Value>& props, const char* key) {
    auto it = props.find(key);
    return it == props.end() ? Value::null() : it->second;
}

const DataNode* entry(const DataNode& list, const std::string& key) {
    for (const DataNode& c : list.children)
        if (c.isList() && !c.children.empty() && c.children[0].isText() && c.children[0].s == key) return &c;
    return nullptr;
}

const Transformable* worldOf(Panel* scene, const std::string& name) {
    const SceneObject* o = scene ? scene->scene().find(name) : nullptr;
    return o ? o->transformable() : nullptr;
}

// config/guitars.dta: guitar -> skins, in file order.
std::vector<std::pair<std::string, std::vector<std::string>>> loadGuitars(const GameData& data) {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    DataNode guitars = parseDtb(data.read(dtbPath("config/guitars.dta")));
    for (const DataNode& g : guitars.children) {
        if (!g.isList() || g.children.empty() || !g.children[0].isText()) continue;
        std::vector<std::string> skins;
        if (const DataNode* sk = g.find("skins"))
            for (size_t k = 1; k < sk->children.size(); ++k)
                if (sk->children[k].isList() && !sk->children[k].children.empty()) skins.push_back(sk->children[k].children[0].s);
        out.push_back({g.children[0].s, skins});
    }
    return out;
}

// ------------------------------------------------------------ CharsysPanel

// CharsysPanel (handler 0x141488): up to two menu characters on the placers
// of a `char_states` entry (ghui/config.dta), drawn through that state's view
// camera and environment, from a cache of at most 6 loaded models.
class CharsysPanel : public NativePanel {
public:
    CharsysPanel(SelectHost& host, const std::string& name, const std::map<std::string, Value>& props)
        : host_(host), name_(name) {
        config_ = prop(props, "char_config").str();
        for (const std::string& c : listOf(prop(props, "load_order"))) {
            cache_.emplace_back();
            cache_.back().character = c;
        }
        // (components (bg (char_states (singleplayer (chars (char (index 0)(view v)(placer p)) ...)) ...)))
        try {
            DataNode cfg = parseDtb(host.data().read("ghui/gen/config.dtb"));
            const DataNode* comps = cfg.find("components");
            const DataNode* bg = comps ? comps->find("bg") : nullptr;
            const DataNode* states = bg ? bg->find("char_states") : nullptr;
            const DataNode* st = states ? entry(*states, config_) : nullptr;
            const DataNode* chars = st ? st->find("chars") : nullptr;
            if (chars)
                for (const DataNode& c : chars->children) {
                    if (!c.isList() || c.children.empty() || c.children[0].s != "char") continue;
                    State s;
                    if (const DataNode* i = c.find("index")) s.index = i->children.size() > 1 ? i->children[1].i : 0;
                    if (const DataNode* v = c.find("view")) s.view = v->textAt(1);
                    if (const DataNode* p = c.find("placer")) s.placer = p->textAt(1);
                    states_.push_back(s);
                }
        } catch (const std::exception& ex) {
            SDL_Log("%s: no char_states: %s", name.c_str(), ex.what());
        }
    }

    bool handle(const std::string& msg, std::vector<Value>& a, Value& r) override {
        auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
        if (msg == "show_char") {
            size_t i = size_t(std::clamp(arg(0).integerValue(), 0, 1));
            std::string c = arg(1).str();
            if (c != slots_[i].character) {
                slots_[i].dir = direction(slots_[i].character, c);
                slots_[i].character = c;
                prioritise();
            }
            return r = Value::integer(0), true;
        }
        if (msg == "hide") return shown_ = !arg(0).truthy(), r = Value::integer(0), true;
        if (msg == "are_chars_loaded") return r = Value::integer(loaded(0)), true;  // slot 0 only (FUN_00140f28)
        if (msg == "is_char_loaded") return r = Value::integer(arg(0).integerValue() >= 2 || loaded(size_t(arg(0).integerValue()))), true;
        if (msg == "char_store_lights_on") return lightsOn_ = arg(0).truthy(), r = Value::integer(0), true;
        return false;
    }

    void entered() override {
        polled_ = false;
        prioritise();
    }

    void exit() override {
        for (Slot& s : slots_) s.character.clear();
        for (CacheModel& m : cache_) m.model.reset();
    }

    void poll(float dtMs) override {
        schedule();
        clockMs_ += dtMs;
        if (!shown_) return;
        polled_ = true;
        for (const State& st : states_) {
            Character* c = model(size_t(st.index));
            if (!c) continue;
            if (const Transformable* p = worldOf(scene_, st.placer)) c->setPlacement(p->world);
            // The menu clip (<c>_idle_ui, kGuitarUI, real time) loops.
            c->update(0, clockMs_, Character::kGuitarUI);
        }
    }

    // Each character through its state's view camera and environment (FUN_0014baf0).
    void drawBefore() override {
        if (!shown_ || !polled_ || !scene_) return;
        bool cleared = false;
        for (const State& st : states_) {
            Character* c = model(size_t(st.index));
            const SceneObject* view = scene_->scene().find(st.view);
            if (!c || !view || !view->view) continue;
            std::string cam;
            const EnvironObj* env = nullptr;
            for (const std::string& d : view->view->draw.draws)
                if (const SceneObject* o = scene_->scene().find(d)) {
                    if (o->cam) cam = d;
                    if (o->environment) env = o->environment.get();
                }
            Mat4 vp;
            if (cam.empty() || !scene_->cameraViewProj(cam, vp)) continue;
            if (!cleared) host_.renderer().clearDepth(), cleared = true;
            // char_store_lights_on FALSE: no lights and a black ambient (not yet bought).
            EnvironObj dark;
            if (env && !lightsOn_ && config_ == "store") {
                dark = *env;
                dark.lights.clear();
                dark.ambient = {0, 0, 0, dark.ambient[3]};
                env = &dark;
            }
            c->draw(vp, env);
        }
    }

private:
    struct Slot {
        std::string character;
        int dir = 1;
    };
    struct State {
        int index = 0;
        std::string view, placer;
    };
    struct CacheModel {
        std::string character;
        std::unique_ptr<Character> model;
        int priority = 0;
        float lastUse = 0;
        bool failed = false;
    };

    int indexOf(const std::string& c) const {
        for (size_t i = 0; i < cache_.size(); ++i)
            if (cache_[i].character == c) return int(i);
        return -1;
    }
    // FUN_00140c78: +1 when the new character is less than half the list ahead.
    int direction(const std::string& from, const std::string& to) const {
        int a = indexOf(from), b = indexOf(to), n = int(cache_.size());
        if (a < 0 || b < 0 || n == 0) return 1;
        int d = ((b - a) % n + n) % n;
        return 2 * d < n ? 1 : -1;
    }
    // FUN_00140d50: wrap, skipping characters not unlocked (except in the store).
    int step(int i, int dir) const {
        int n = int(cache_.size());
        for (int k = 0; k < n; ++k) {
            i = ((i + dir) % n + n) % n;
            if (config_ == "store" || host_.unlocked(cache_[size_t(i)].character)) return i;
        }
        return i;
    }
    // FUN_00140ab0: the shown characters first, one behind, four ahead.
    void prioritise() {
        int n = int(cache_.size());
        for (CacheModel& m : cache_) m.priority = n;
        for (const State& st : states_) {
            const Slot& s = slots_[size_t(std::clamp(st.index, 0, 1))];
            int i = indexOf(s.character);
            if (i < 0) continue;
            cache_[size_t(i)].priority = 0;
            cache_[size_t(i)].lastUse = clockMs_;
            int back = step(i, -s.dir);
            cache_[size_t(back)].priority = std::min(cache_[size_t(back)].priority, 2);
            int j = i;
            for (int k = 1; k <= 4; ++k) {
                j = step(j, s.dir);
                cache_[size_t(j)].priority = std::min(cache_[size_t(j)].priority, k);
            }
        }
    }
    static bool better(const CacheModel& a, const CacheModel& b) {
        return a.priority < b.priority || (a.priority == b.priority && a.lastUse > b.lastUse);
    }
    // FUN_00140fb0: one load per poll, at most 6 models.
    void schedule() {
        int n = int(cache_.size()), loadedCount = 0;
        CacheModel *best = nullptr, *worst = nullptr;
        for (CacheModel& m : cache_) {
            if (m.model) {
                ++loadedCount;
                if (!worst || better(*worst, m)) worst = &m;
            } else if (!m.failed && (!best || better(m, *best))) {
                best = &m;
            }
        }
        if (!best || best->priority >= n) return;
        if (loadedCount >= 6) {
            if (!worst || !better(*best, *worst)) return;
            worst->model.reset();
        }
        load(*best);
    }
    void load(CacheModel& m) {
        try {
            // A Character with `meta` set: the menu clip set, no hand IK (charbase.dta).
            // The guitar it holds is the GUITARS default, inferred to be the SG.
            m.model = std::make_unique<Character>(host_.renderer(), host_.data(),
                                                  Character::guitaristSpec(host_.data(), m.character, "gibson_sg", 1, true));
            Panel* scene = scene_;
            m.model->setWorldLookup([scene](const std::string& o) -> const SceneObject* {
                return scene ? scene->scene().find(o) : nullptr;
            });
        } catch (const std::exception& ex) {
            SDL_Log("%s: character %s: %s", name_.c_str(), m.character.c_str(), ex.what());
            m.failed = true;
        }
    }
    Character* model(size_t slot) {
        if (slot >= 2) return nullptr;
        int i = indexOf(slots_[slot].character);
        return i >= 0 ? cache_[size_t(i)].model.get() : nullptr;
    }
    bool loaded(size_t slot) {
        if (states_.empty() || slot >= 2) return true;
        return model(slot) != nullptr;
    }

    SelectHost& host_;
    std::string name_, config_;
    std::vector<State> states_;
    Slot slots_[2];
    std::vector<CacheModel> cache_;
    bool shown_ = true, polled_ = false, lightsOn_ = true;
    float clockMs_ = 0;
};

// ------------------------------------------------------- GuitarDisplayPanel

// GuitarDisplayPanel (ctor 0x143e50, handler 0x145718): the selected guitar
// spinning on a placer of ghui/guitar_display.gh.
class GuitarDisplayPanel : public NativePanel {
public:
    GuitarDisplayPanel(SelectHost& host, const std::map<std::string, Value>& props) : host_(host) {
        std::string config = prop(props, "config").str();
        if (config == "multiplayer") {
            slots_.resize(2);
            slots_[0].placer = "guitar_multi0.placer", slots_[0].spin = "guitar_p1.tnm";
            slots_[1].placer = "guitar_multi1.placer", slots_[1].spin = "guitar_p2.tnm";
        } else {
            slots_.resize(1);
            if (config == "store") slots_[0].placer = "guitar_store.placer", slots_[0].spin = "guitar_store.tnm";
            else if (config == "endgame") {
                slots_[0].placer = "guitar_axe.placer", slots_[0].spin = "guitar_axe.tnm";
                endgame_ = true;
            } else {
                slots_[0].placer = "guitar_single.placer", slots_[0].spin = "guitar_single.tnm";
            }
        }
        guitars_ = loadGuitars(host.data());
    }

    bool handle(const std::string& msg, std::vector<Value>& a, Value& r) override {
        auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
        if (msg == "show_guitar") {
            showGuitar(size_t(std::max(0, arg(0).integerValue())), arg(1).str(), arg(2).str());
            return r = Value::integer(0), true;
        }
        if (msg == "hide") return shown_ = !arg(0).truthy(), r = Value::integer(0), true;
        return false;
    }

    void exit() override {  // unloads every slot (force_exit reloads on entry)
        for (Slot& s : slots_) s.scene.reset(), s.renderer.reset(), s.skin.clear();
    }

    // +0.5 frame per 60 Hz poll through the spin anim's filters; the limit is
    // 240 (100 for the endgame axe).
    void poll(float dtMs) override {
        if (!shown_ || !scene_) return;
        float limit = endgame_ ? 100.0f : 240.0f;
        for (Slot& s : slots_) {
            if (!s.scene) continue;
            s.frame += dtMs * 0.03f;
            while (s.frame > limit) s.frame -= limit;
            scene_->setFrame(s.spin, s.frame);
        }
    }

    // The guitar's attach bone (bone_pos_guitar.mesh, guitar.mesh for the
    // axe) lands on the placer's world transform; lit by guitar_setup.env.
    void drawAfter() override {
        if (!shown_ || !scene_) return;
        Mat4 vp;
        if (!scene_->cameraViewProj(endgame_ ? "guitar_setup_endgame.cam" : "guitar_setup.cam", vp) && !scene_->viewProj(vp))
            return;
        const SceneObject* envObj = scene_->scene().find("guitar_setup.env");
        const EnvironObj* env = envObj ? envObj->environment.get() : nullptr;
        bool cleared = false;
        for (Slot& s : slots_) {
            if (!s.scene || !s.renderer) continue;
            const Transformable* placer = worldOf(scene_, s.placer);
            const SceneObject* attach = s.scene->find(endgame_ ? "guitar.mesh" : "bone_pos_guitar.mesh");
            if (!placer || !attach || !attach->transformable()) continue;
            Xfm move = compose(inverse(attach->transformable()->world), placer->world);
            if (!cleared) host_.renderer().clearDepth(), cleared = true;
            s.renderer->draw(s.scene->find("top.view") ? "top.view" : "guitar.mesh", vp, Mat4::fromRowXfm(move.rot, move.pos), env);
        }
    }

private:
    struct Slot {
        std::string placer, spin, skin;
        std::unique_ptr<Scene> scene;
        std::unique_ptr<SceneRenderer> renderer;
        float frame = 0;
    };

    // FUN_001447b8.
    void showGuitar(size_t i, const std::string& guitar, std::string skin) {
        if (guitar.empty() || i >= slots_.size()) return;
        if (skin.empty())
            for (const auto& [g, skins] : guitars_)
                if (g == guitar && !skins.empty()) skin = skins[0];
        Slot& s = slots_[i];
        if (skin == s.skin && s.scene) return;
        s.frame = 0;
        if (scene_) scene_->setFrame(s.spin, 0);
        s.scene.reset(), s.renderer.reset();
        s.skin = skin;
        auto [file, mat] = Character::guitarModel(host_.data(), skin.empty() ? guitar : skin);
        std::string path = "charsys/guitars/gen/" + file + ".rnd_ps2";
        if (file.empty() || !host_.data().exists(path)) return;
        try {
            s.scene = std::make_unique<Scene>(host_.data().read(path));
            if (SceneObject* g = s.scene->find("guitar.mesh"); g && g->mesh && !mat.empty() && s.scene->find(mat)) g->mesh->mat = mat;
            s.renderer = std::make_unique<SceneRenderer>(host_.renderer(), *s.scene);
            Panel* scene = scene_;
            Scene* own = s.scene.get();
            s.renderer->setLookup([scene, own](const std::string& o) -> const SceneObject* {
                if (const SceneObject* x = own->find(o)) return x;
                return scene ? scene->scene().find(o) : nullptr;
            });
        } catch (const std::exception& ex) {
            SDL_Log("guitar display %s: %s", path.c_str(), ex.what());
            s.scene.reset(), s.renderer.reset();
        }
    }

    SelectHost& host_;
    std::vector<Slot> slots_;
    std::vector<std::pair<std::string, std::vector<std::string>>> guitars_;
    bool shown_ = true, endgame_ = false;
};

// -------------------------------------------------------- GuitarSelectPanel

// GuitarSelectPanel (handler 0x147ff0, input FUN_00147bc0): each player's
// guitar / skin choice; no art of its own.
class GuitarSelectPanel : public NativePanel {
public:
    GuitarSelectPanel(SelectHost& host, const std::map<std::string, Value>& props) : host_(host) {
        players_ = prop(props, "multiplayer").truthy() ? 2 : 1;
        guitars_ = loadGuitars(host.data());
    }

    void enter() override {  // 0x147678
        for (int p = 0; p < 2; ++p) {
            st_[p].guitar = host_.playerGuitar(p);
            st_[p].skin = host_.playerSkin(p);
            st_[p].skinSelect = st_[p].animating = false;
        }
    }

    bool handle(const std::string& msg, std::vector<Value>& a, Value& r) override {
        auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
        auto p = [&] { return size_t(std::clamp(arg(0).integerValue(), 0, 1)); };
        if (msg == "set_skin_select") return st_[p()].skinSelect = arg(1).truthy(), r = Value::integer(0), true;
        if (msg == "is_skin_select") return r = Value::integer(st_[p()].skinSelect), true;
        if (msg == "set_animating") return st_[p()].animating = arg(1).truthy(), r = Value::integer(0), true;
        if (msg == "animating") return r = Value::integer(st_[p()].animating), true;
        if (msg == "set_quickplay") return st_[p()].quickplay = arg(1).truthy(), r = Value::integer(0), true;
        if (msg == "get_selected_guitar") return r = Value::symbol(st_[p()].guitar), true;
        if (msg == "get_selected_skin") return r = Value::symbol(st_[p()].skin), true;
        if (msg == "get_num_skins") return r = Value::integer(int(skins(int(p()), arg(1).str()).size())), true;
        return false;
    }

    bool buttonDown(int button, int player) override {
        int slot = players_ == 1 ? 0 : std::clamp(player, 0, 1);
        St& s = st_[slot];
        if (s.animating) return true;  // swallowed while the skin zoom plays
        if (host_.isSelectButton(button)) {
            host_.route("gh_component_select", {{"component", host_.focusComponent()}, {"player_num", Value::integer(slot)},
                                                {"screen", host_.screenValue()}, {"button", Value::integer(button)}});
            return true;
        }
        if (button != kUp && button != kDown) return false;  // back reaches the auto-back
        int delta = button == kDown ? 1 : -1;
        if (stepSelection(slot, delta))
            host_.route("component_scroll", {{"component", host_.focusComponent()}, {"player_num", Value::integer(slot)},
                                             {"screen", host_.screenValue()}, {"direction", Value::integer(delta)},
                                             {"done", Value::integer(1)}});
        return true;
    }

private:
    struct St {
        std::string guitar, skin;
        bool skinSelect = false, animating = false, quickplay = false;
    };

    // SelectGuitarProvider_v9 (@0x119ce0): grim only plays grim_guitar; for
    // everyone else grim_guitar and guitar_red_octane are removed. Career lists
    // what is unlocked at the current difficulty, quickplay (and multiplayer)
    // what is unlocked at any; config/guitars.dta order.
    bool ok(int p, const std::string& item) const {
        return st_[p].quickplay ? host_.unlockedAny(item) : host_.unlocked(item);
    }
    std::vector<std::string> guitarList(int p) const {
        if (host_.playerCharacter(p) == "grim") return {"grim_guitar"};
        std::vector<std::string> out;
        for (const auto& [g, skins] : guitars_)
            if (g != "grim_guitar" && g != "guitar_red_octane" && ok(p, g)) out.push_back(g);
        return out;
    }
    std::vector<std::string> skins(int p, const std::string& guitar) const {
        std::vector<std::string> out;
        for (const auto& [g, list] : guitars_)
            if (g == guitar)
                for (const std::string& s : list)
                    if (ok(p, s)) out.push_back(s);
        return out;
    }
    // FUN_001478e8: step the guitar (or the skin in skin select), wrapping.
    bool stepSelection(int p, int delta) {
        St& s = st_[p];
        if (s.skinSelect) {
            std::vector<std::string> list = skins(p, s.guitar);
            if (list.empty()) return false;
            int n = int(list.size());
            int idx = int(std::find(list.begin(), list.end(), s.skin) - list.begin());
            if (idx >= n) idx = -1;
            int next = ((idx + delta) % n + n) % n;
            if (next == idx) return false;
            s.skin = list[size_t(next)];
            return true;
        }
        std::vector<std::string> list = guitarList(p);
        if (list.empty()) return false;
        int n = int(list.size());
        int idx = int(std::find(list.begin(), list.end(), s.guitar) - list.begin());
        if (idx >= n) idx = -1;
        int next = idx + delta;
        if (next < 0) next = n - 1;
        if (next >= n) next = 0;
        if (next == idx) return false;
        s.guitar = list[size_t(next)];
        std::vector<std::string> sk = skins(p, s.guitar);
        auto it = std::find(sk.begin(), sk.end(), s.skin);
        s.skin = sk.empty() ? "" : it != sk.end() ? *it : sk[0];
        return true;
    }

    SelectHost& host_;
    int players_ = 1;
    St st_[2];
    std::vector<std::pair<std::string, std::vector<std::string>>> guitars_;
};

// -------------------------------------------------------- MultiCharSelPanel

// MultiCharSelPanel (handler 0x14f240, input 0x14e850): both players pick
// different characters.
class MultiCharSelPanel : public NativePanel {
public:
    MultiCharSelPanel(SelectHost& host, const std::map<std::string, Value>& props) : host_(host) {
        // (char_navigator (vertical c ...) [(wrap B)])
        Value nav = prop(props, "char_navigator");
        auto take = [&](const Value& v) {
            if (v.type != Value::Type::Array || v.array->empty()) return;
            std::string key = (*v.array)[0].str();
            if (key == "vertical")
                for (size_t k = 1; k < v.array->size(); ++k) chars_.push_back((*v.array)[k].str());
            else if (key == "wrap" && v.array->size() > 1)
                wrap_ = (*v.array)[1].truthy();
        };
        if (nav.type == Value::Type::Array && !nav.array->empty() && (*nav.array)[0].type == Value::Type::Array)
            for (const Value& v : *nav.array) take(v);
        else
            take(nav);
    }

    void enter() override { done_[0] = done_[1] = false; }

    // Locked characters' pictures are disabled (0x14e190).
    void entered() override {
        if (!scene_) return;
        for (const std::string& c : chars_)
            if (!host_.unlockedAny(c)) scene_->setState("msc_char_" + c + ".pic", UiResources::Disabled);
    }

    bool handle(const std::string& msg, std::vector<Value>& a, Value& r) override {
        if (msg == "is_player_done")
            return r = Value::integer(done_[std::clamp(a.empty() ? 0 : a[0].integerValue(), 0, 1)]), true;
        return false;
    }

    bool buttonDown(int button, int player) override {
        int pad = std::clamp(player, 0, 1);
        if (host_.isSelectButton(button)) {
            if (done_[pad]) return true;
            host_.route("gh_component_select", {{"component", host_.focusComponent()}, {"player_num", Value::integer(pad)},
                                                {"screen", host_.screenValue()}, {"button", Value::integer(button)}});
            done_[pad] = true;
            if (done_[0] && done_[1]) host_.route("chars_selected", {});
            return true;
        }
        if (button == kUp || button == kDown) {
            if (done_[pad]) return true;
            if (step(pad, button == kDown ? 1 : -1))
                host_.route("component_scroll", {{"component", host_.focusComponent()}, {"player_num", Value::integer(pad)},
                                                 {"screen", host_.screenValue()},
                                                 {"direction", Value::integer(button == kUp ? 1 : -1)},
                                                 {"done", Value::integer(1)}});
            return true;
        }
        if (host_.isBackButton(button)) {
            if (!done_[pad]) return false;  // auto-back
            done_[pad] = false;
            host_.route("deselect_char", {});
            return true;
        }
        return false;
    }

private:
    // FUN_0014e4a0: the next character, skipping the other player's and locked ones.
    bool step(int pad, int dir) {
        int n = int(chars_.size());
        if (n == 0) return false;
        std::string mine = host_.playerCharacter(pad), other = host_.playerCharacter(1 - pad);
        int i = int(std::find(chars_.begin(), chars_.end(), mine) - chars_.begin());
        if (i >= n) i = 0;
        int start = i;
        for (int k = 0; k < n; ++k) {
            i += dir;
            if (wrap_) i = (i % n + n) % n;
            else if (i < 0 || i >= n) return false;
            if (i == start) return false;
            const std::string& c = chars_[size_t(i)];
            if (c == other) continue;
            host_.setPlayerCharacter(pad, c);
            if (host_.unlockedAny(c)) return true;
        }
        return false;
    }

    SelectHost& host_;
    std::vector<std::string> chars_;
    bool wrap_ = true;
    bool done_[2] = {false, false};
};

// ------------------------------------------------------------- CreditsPanel

// CreditsPanel (ctor FUN_00141700) + RndCreditList (FUN_0012dc00; docs/credits_plan.md):
// config/credits.dta rolls up through credits.lst, 15 rows 25 apart, 0.018 of
// a row a poll (60 Hz), the top and bottom two rows fading; once the last
// page is reached the panel sends go_back. Rows: () blank, ("X") heading,
// ("role" "name"), ("" "text" "") centred.
// [I] The GD01 credits roll first, then a spacer and the original's.
constexpr const char* kGh1reCredits = R"dta(
("GD01 PROJECT")
()
("MAIN DEVELOPERS")
("" "BRACA DEVELOPER" "")
("" "Anthropic Claude (yeah... What can I say)" "")
()
("THANKS, OPEN SOURCE COMMUNITY")
("" "Folks from REA, GHIDRA and JAVA, YOU ROCK!" "")
()
("BETA TESTERS")
("" "Shuubs" "")
("" "Lopes@vista.aero" "")
("" "ColinClarkPuxa" "")
("" "Nevessilva91" "")
("" "Bracalister" "")
("" "M@D_C@W" "")
()
("ORIGINAL HEROES")
("" "RedOctane, Harmonix, MTV Games and Activision..." "")
("" "Thanks for everything! Lovyouguys S2!" "")
()
()
("" "---" "")
()
()
)dta";

class CreditsPanel : public NativePanel {
public:
    explicit CreditsPanel(SelectHost& host) : host_(host) {
        try {
            DataNode d = parseDtb(host.data().read(dtbPath("config/credits.dta")));
            // After the original's opening blank rows (the list starts empty and rolls up).
            DataNode ours = parseDtaText(kGh1reCredits);
            size_t first = 0;
            while (first < d.children.size() && d.children[first].isList() && d.children[first].children.empty()) ++first;
            d.children.insert(d.children.begin() + long(first), ours.children.begin(), ours.children.end());
            for (const DataNode& r : d.children) {
                if (!r.isList()) continue;
                std::array<std::string, 4> col;
                auto text = [](const DataNode& n) {
                    std::string t = n.isText() ? n.s : "";
                    for (size_t at; (at = t.find("\\q")) != std::string::npos;) t.replace(at, 2, "\"");
                    return t;
                };
                size_t n = r.children.size();
                if (n == 2 && !text(r.children[0]).empty()) col[0] = text(r.children[0]);
                if (n == 1) col[1] = text(r.children[0]);
                if (n == 2) col[2] = text(r.children[1]);
                if (n == 3) col[3] = text(r.children[1]);
                rows_.push_back(std::move(col));
            }
        } catch (const std::exception& ex) {
            SDL_Log("credits: %s", ex.what());
        }
    }

    // Enter (0x141bd0): reset, scroll to the last page, start the music.
    void entered() override {
        pos_ = 0, frac_ = 0, dir_ = 0, ticks_ = 0;
        moving_ = done_ = false;
        target_ = std::max(0, int(rows_.size()) - kDisplay);
        host_.creditsMusic(true);
    }
    void exit() override { host_.creditsMusic(false); }

    // FUN_0012f9e8 at 60 Hz: a row takes 56 polls.
    void poll(float dtMs) override {
        ticks_ += dtMs;
        for (int n = 0; ticks_ >= 1000.0f / 60.0f && n < 4; ++n) {
            ticks_ -= 1000.0f / 60.0f;
            step();
        }
        if (ticks_ > 1000.0f / 60.0f) ticks_ = 0;
    }

    void drawAfter() override {
        if (!scene_ || rows_.empty()) return;
        Mat4 vp;
        if (!scene_->viewProj(vp)) return;
        const SceneObject* lst = scene_->scene().find("credits.lst");
        if (!lst || !lst->ui) return;
        const float spacing = lst->ui->spacing > 0 ? lst->ui->spacing : 25.0f;
        Mat4 listW = Mat4::fromRowXfm(lst->ui->trans.world.rot, lst->ui->trans.world.pos);
        static const char* const kTemplates[4] = {"credit_title.txt", "credit_center.txt", "credit_name.txt", "credit_center_name.txt"};
        float off = spacing * std::min(frac_ * float(dir_), 1.0f);
        if (off < 0) off += spacing;
        const float fadeTop = spacing * kFade, fbs = float(kDisplay + 1 - kFade) * spacing, end = float(kDisplay + 1) * spacing;
        int slots = kDisplay + (moving_ ? 1 : 0);
        for (int k = 1; k <= slots; ++k) {
            int row = pos_ + k - 1 - (moving_ && dir_ > 0 ? 1 : 0);
            if (row < 0 || row >= int(rows_.size())) continue;
            float y = spacing * float(k) - off;
            float a = y < fadeTop ? y / fadeTop : y > fbs ? 1 - (y - fbs) / (end - fbs) : 1.0f;
            a = std::clamp(a, 0.0f, 1.0f);
            if (a <= 0) continue;
            Xfm slot;
            slot.pos = {0, 0, -y};
            Mat4 slotW = listW * Mat4::fromRowXfm(slot.rot, slot.pos);
            for (int j = 0; j < 4; ++j) {
                const std::string& str = rows_[size_t(row)][size_t(j)];
                if (str.empty()) continue;
                const TextObj* t = host_.textTemplate(kTemplates[j]);
                if (!t) continue;
                auto c = host_.fontColor(t->font);  // the font mat's colour shows, not the template's
                scene_->drawText("cr" + std::to_string(row % 32) + "_" + std::to_string(j), str, kTemplates[j], {c[0], c[1], c[2]}, vp,
                                 slotW * Mat4::fromRowXfm(t->trans.local.rot, t->trans.local.pos), a);
            }
        }
    }

private:
    static constexpr int kDisplay = 15, kFade = 2;
    static constexpr float kMaxStep = 0.018f;

    // One poll of the scroll (FUN_0012f9e8), then go_back at the last page.
    void step() {
        if (!moving_ && pos_ == target_) return;
        if (frac_ == 0) {
            dir_ = pos_ < target_ ? 1 : -1;
            pos_ += dir_;
            moving_ = true;
        }
        frac_ += kMaxStep;
        if (frac_ >= 1) {
            frac_ = 0;
            moving_ = target_ != pos_;
            dir_ = 0;
            if (target_ == pos_ && !done_) {
                done_ = true;
                host_.route("go_back", {});
            }
        }
    }

    SelectHost& host_;
    std::vector<std::array<std::string, 4>> rows_;
    int pos_ = 0, target_ = 0, dir_ = 0;
    float frac_ = 0, ticks_ = 0;
    bool moving_ = false, done_ = false;
};

// ------------------------------------------------------------ EnhancedPanel

// The port's Enhanced Settings (not in the original game; options >
// Enhanced Settings): the settings in collapsible sections on a see-through
// black box, over a still close-up of the first venue's microphone (the
// host's backdrop; a black page without it). The focused (or hovered)
// option's explanation shows in a box on the right. Every change applies at
// once. Options not built yet show a warning sign and cannot be switched on.
class EnhancedPanel : public NativePanel {
public:
    explicit EnhancedPanel(SelectHost& host) : host_(host) {}

    void enter() override {
        rebuild();
        focus_ = 0;
        scroll_ = scrollTarget_ = 0;
        dropdown_ = -1;
        status_.clear();
        statusMs_ = 0;
        cal_ = Calib{};
        host_.menuBackdrop(true);
    }
    void exit() override { host_.menuBackdrop(false); }

    bool buttonDown(int button, int player) override {
        (void)player;
        if (rows_.empty()) return false;
        if (cal_.phase != 0) return calibButton(button), true;
        const Item& it = item(focus_);
        if (dropdown_ >= 0) {  // the open dropdown takes the input
            int n = int(it.options.size());
            if (button == kUp || button == kDown) {
                ddFocus_ = (ddFocus_ + (button == kDown ? 1 : n - 1)) % n;
                sound("button_toggle");
            } else if (host_.isSelectButton(button)) {
                setValue(it, it.options[size_t(ddFocus_)].value);
                dropdown_ = -1;
            } else if (host_.isBackButton(button)) {
                dropdown_ = -1;
                sound("button_back");
            }
            return true;
        }
        if (button == kUp || button == kDown) {
            moveFocus(button == kDown ? 1 : -1);
            return true;
        }
        if ((button == kLeft || button == kRight) && it.kind == Kind::Choice) {
            if (!editable(it)) return sound("button_error"), true;
            const std::string cur = host_.enhancedValue(it.key);
            int n = int(it.options.size()), i = optionIndex(it, cur);
            for (int k = 0; k < n; ++k) {  // the next available option
                i = (i + (button == kRight ? 1 : n - 1)) % n;
                if (it.options[size_t(i)].ok) break;
            }
            setValue(it, it.options[size_t(i)].value);
            return true;
        }
        if (host_.isSelectButton(button)) return activate(), true;
        return false;  // back: the screen's auto-back
    }

    bool mouse(float x, float y, bool click) override {
        if (rows_.empty()) return false;
        if (cal_.phase != 0) {  // a click is a press
            if (click) calibButton(kCross);
            return true;
        }
        x -= offset_;
        // The open dropdown: its options, or a click anywhere else closes it.
        if (dropdown_ >= 0) {
            const Item& it = item(focus_);
            for (size_t k = 0; k < it.options.size(); ++k) {
                float oy = ddTop_ + float(k) * kDdRow;
                if (x < kDdPopX0 || x > kCtrlX1 || y < oy || y > oy + kDdRow) continue;
                if (int(k) != ddFocus_) ddFocus_ = int(k), sound("button_toggle");
                if (click) setValue(it, it.options[k].value), dropdown_ = -1;
                return true;
            }
            if (click) dropdown_ = -1, sound("button_back");
            return true;
        }
        for (size_t r = 0; r < rows_.size(); ++r) {
            float top = rowY(int(r)), h = rowHeight(item(int(r)));
            if (item(int(r)).kind == Kind::Separator || top < kListTop - 1 || top + h > kListBottom + 1) continue;
            if (x < kListX0 || x > kCtrlX1 + 16 || y < top || y > top + h) continue;
            // Hovering focuses (without scrolling the list) and shows the explanation.
            if (int(r) != focus_) focus_ = int(r), sound("button_toggle");
            if (click) activate();
            return true;
        }
        return false;
    }

    void poll(float dtMs) override {
        // The list eases toward its scroll target.
        float k = 1 - std::exp(-std::max(dtMs, 0.0f) / 70.0f);
        scroll_ += (scrollTarget_ - scroll_) * k;
        if (std::fabs(scrollTarget_ - scroll_) < 0.3f) scroll_ = scrollTarget_;
        // The user-files result shows for a few seconds.
        std::string st = host_.userFilesStatus();
        if (st != status_) status_ = st, statusMs_ = st.empty() ? 0 : 5000;
        statusMs_ = std::max(0.0f, statusMs_ - dtMs);
        calibPoll();
    }

    // The backdrop with the spotlight's beam on the microphone (or a black
    // page), faded in with the screen transition.
    void drawBefore() override {
        updateTransition();
        Renderer& r = host_.renderer();
        float fx = 0, fy = 0;
        if (!host_.drawMenuBackdrop(fx, fy)) {
            r.fillRect(offset_, 0, kScreenW, kScreenH, {0, 0, 0, uint8_t(255 * fade_)});
            return;
        }
        // The beam from above-left of the frame, and a warm glow on the subject.
        std::vector<Vertex> v;
        auto vert = [&](float x, float y, uint8_t a) { v.push_back({x, y, 0, 0, 0, 255, 238, 205, a}); };
        const float apexX = fx - 260, apexY = -60;
        constexpr int kSeg = 24;
        for (int i = 0; i < kSeg; ++i) {
            float a0 = float(i) / kSeg, a1 = float(i + 1) / kSeg;
            float x0 = fx - 150 + 300 * a0, x1 = fx - 150 + 300 * a1;
            auto edge = [](float t) { float d = std::fabs(t - 0.5f) * 2; return uint8_t(40 * (1 - d * d)); };
            vert(apexX, apexY, 0), vert(x0, fy + 120, edge(a0)), vert(x1, fy + 120, edge(a1));
        }
        for (int i = 0; i < 48; ++i) {
            float t0 = float(i) / 48 * 6.2831853f, t1 = float(i + 1) / 48 * 6.2831853f;
            vert(fx, fy, 70);
            vert(fx + 170 * std::cos(t0), fy + 150 * std::sin(t0), 0);
            vert(fx + 170 * std::cos(t1), fy + 150 * std::sin(t1), 0);
        }
        r.draw(v.data(), v.size(), 0, Blend::AddAlpha);
        r.fillRect(0, 0, kScreenW, kScreenH, {0, 0, 0, uint8_t(255 * (1 - fade_))});
    }

    void drawAfter() override {
        const float o = offset_;
        quad(kBoxX0 + o, kBoxY0, kBoxX1 + o, kBoxY1, {0, 0, 0, 0.62f});
        frame(kBoxX0 + o, kBoxY0, kBoxX1 + o, kBoxY1, 1.5f, {1, 1, 1, 0.12f});
        text("rokk.font", "ENHANCED SETTINGS", 90 + o, 40, 62, kTitle, 0x11);
        text("impactor.font", "Features of this PC version, beyond the original game", 92 + o, 110, 20, kDim, 0x11);
        // The user-files result for a few seconds, else the Steam Deck note.
        if (statusMs_ > 0) {
            std::array<float, 4> c = kOn;
            c[3] = std::min(1.0f, statusMs_ / 500);
            text("impactor.font", status_, 92 + o, 134, 18, c, 0x11);
        } else if (deckOn()) {
            text("impactor.font", "Steam Deck mode is on: the other options are locked", 92 + o, 134, 18, kAccent, 0x11);
        }
        for (size_t r = 0; r < rows_.size(); ++r) drawRow(int(r));
        // More above / below.
        if (scroll_ > 1) arrow(kListX0 + 270 + o, kListTop - 10, true);
        if (contentHeight() - scroll_ > kListBottom - kListTop + 1) arrow(kListX0 + 270 + o, kListBottom + 8, false);
        drawHelp();
        if (dropdown_ >= 0) drawDropdown();
        if (cal_.phase != 0) drawCalib();
    }

private:
    enum class Kind { Section, Separator, Check, Choice, Button };
    struct Option {
        const char* value;
        const char* label;
        bool ok = true;  // false: not available yet (shown, cannot be chosen)
    };
    struct Item {
        Kind kind;
        const char* key;    // the setting (enhanced.json), the section's id, or the button's action
        const char* name;
        bool ready;         // false: not built yet (warning sign, cannot be switched on)
        const char* help;
        std::vector<Option> options = {};  // a Choice's values; a Button's label
        const char* needs = nullptr;  // shown only while this setting is on (a check) or not none / off (a choice)
    };
    static const std::vector<Item>& items() {
        static const std::vector<Item> k = {
            {Kind::Section, "deck", "STEAM DECK MODE", true,
             "Settings for handheld PCs such as the Steam Deck."},
            {Kind::Check, "steam_deck", "Steam Deck Mode", true,
             "Tunes the game down to run well on a handheld: 60 FPS, a 1280 x 800 internal resolution, borderless full "
             "screen and the original sprites. While it is on, every other option is locked at its default."},
            {Kind::Separator, "", "", true, ""},
            {Kind::Section, "controls", "CONTROLS & GAMEPAD", true, "Mouse, touch, guitars and gamepads."},
            {Kind::Check, "mouse", "Enable Mouse Selection", true,
             "Point at a menu entry to highlight it and click to choose it; the mouse wheel scrolls lists. CONTINUE and "
             "BACK in the help bar are clickable too. Hovering works only while the game window has focus."},
            {Kind::Check, "touch", "Enable Touch Controls", true,
             "Play on a touch screen. In the menus a tap is a click. In a song, touch a lane at the bottom of the "
             "highway when its note arrives: it plays the fret and the strum at once (hold the finger for long notes, "
             "several fingers for chords). Two fingers above the highway deploy Star Power; the top corner pauses."},
            {Kind::Check, "rockband", "Enable Rock Band Guitars", true,
             "Plays with the USB Rock Band guitars for Xbox 360 and PlayStation 3 / 4: the five frets (the solo frets "
             "too), the strum bar, the whammy bar, and tilt or Select for Star Power."},
            {Kind::Check, "xinput", "Enable XInput Controllers", true,
             "Any XInput gamepad plays like the original game's gamepad mode: the shoulder buttons and X / A are the "
             "frets, the analog sticks bend the whammy, and pressing a button plays the note and the strum at once."},
            {Kind::Separator, "", "", true, ""},
            {Kind::Section, "graphics", "PERFORMANCE & GRAPHICS", true, "Timing, window, frame rate and image quality."},
            {Kind::Button, "calibrate", "Audio / Video Calibration", true,
             "Measures your setup's delay so notes are judged and drawn in time. First strum (or press green) on each "
             "tick you HEAR, then on each flash you SEE, 8 times each. Back cancels.",
             {{"", "Calibrate..."}}},
            {Kind::Choice, "screen_mode", "Screen Mode", true,
             "Windowed; full screen at the desktop's resolution; or full screen without a border (a window covering "
             "the screen, the quickest to switch away from).",
             {{"windowed", "Windowed"}, {"fullscreen", "Full Screen"}, {"borderless", "Full Screen (No Border)"}}},
            {Kind::Choice, "max_fps", "In-Game Max FPS", true,
             "The highest frame rate the game draws at; the original ran at 60. The game never draws faster than this, "
             "whatever the display's refresh rate.",
             {{"60", "60"}, {"120", "120"}, {"240", "240"}}},
            {Kind::Check, "fps_counter", "Show FPS Counter", true,
             "Shows the frames drawn per second in the bottom-left corner of the screen."},
            {Kind::Choice, "internal_res", "Internal Resolution", true,
             "The resolution the game draws at before it is scaled to the window. Higher is sharper and costs more; "
             "above the window's own size it also smooths the edges (supersampling). The PS2 drew 640 x 448. "
             "With AI Upscaling on, the screen's resolution is used instead (or this one, when higher).",
             {{"448", "PS2 (448p)"}, {"480", "480p"}, {"720", "720p"}, {"800", "800p (Steam Deck)"}, {"900", "900p"},
              {"1080", "1080p"}, {"1440", "1440p"}, {"2160", "4K (2160p)"}}},
            {Kind::Check, "sharp_ui", "Sharper Text & Sprites", true,
             "Upscales the menu art and the game's fonts with a sharper filter, so text and sprites stay crisp on "
             "large screens. Screens opened after the change use it."},
            {Kind::Choice, "antialiasing", "Anti-Aliasing", true,
             "Smooths jagged edges. FXAA: a fast filter over the image. SSAA: draws the game at twice the resolution "
             "and scales it down, the sharpest and the most costly. TAA: blends each frame with the last ones for "
             "very smooth edges, with a little softness in motion.",
             {{"none", "None"}, {"fxaa", "FXAA"}, {"ssaa", "SSAA"}, {"taa", "TAA"}}},
            {Kind::Choice, "ai_upscaling", "AI Upscaling", true,
             "Draws the 3D scene below your screen's resolution and upscales it to the screen (menus and the "
             "highway stay sharp), for higher frame rates. FSR 1: spatial, any GPU. FSR 3.1: temporal, any Vulkan "
             "GPU. DLSS 4: NVIDIA RTX GPUs (Linux build). The game always starts on the dedicated GPU when there "
             "are two. If one cannot run, FSR 1 is used.",
             {{"none", "None"}, {"fsr1", "FSR 1 (Spatial)"}, {"fsr31", "FSR 3.1", kVkUpscalers},
              {"dlss4", "DLSS 4", kVkUpscalers}, {"dlss5", "DLSS 5", false}}},
            {Kind::Choice, "ai_upscaling_mode", "Upscaling Quality", true,
             "How far below the screen's resolution the scene is drawn: Quality (1.5x upscale, the sharpest), "
             "Balanced (1.7x) or Performance (2x, the fastest). On a 1440p screen: 960p, 847p, 720p.",
             {{"performance", "Performance"}, {"balanced", "Balanced"}, {"quality", "Quality"}}, "ai_upscaling"},
            {Kind::Choice, "bokeh", "Bokeh Effect", true,
             "The depth-of-field blur of the close shots. None: always sharp. Regular: as the original game. "
             "Stronger: the focus starts nearer the guitarist and the blur is wider.",
             {{"none", "None"}, {"regular", "Regular"}, {"stronger", "Stronger"}}},
            {Kind::Check, "particles", "Enable Particles FX", true,
             "Sparks, smoke, dust and the highway's hit effects. Turn them off to save power on low-end hardware; "
             "the gameplay is the same."},
            {Kind::Choice, "bloom", "Bloom", true,
             "A soft glow around the brightest lights of the venue, like a camera lens.",
             {{"off", "Off"}, {"low", "Low"}, {"high", "High"}}},
            {Kind::Check, "cinematic_color", "Cinematic Color", true,
             "A filmic colour curve with a gentle grade and vignette on the venue, for a richer, more cinematic image."},
            {Kind::Choice, "color_grade", "Color Grading", true,
             "A colour look for the venue, like a film's grade (with or without Cinematic Color). Warm: golden stage "
             "light. Cold: steel blue. Vintage: faded 70s film. Noir: high-contrast black and white. Neon: punchy "
             "purples and cyans.",
             {{"off", "Off"}, {"warm", "Warm"}, {"cold", "Cold"}, {"vintage", "Vintage"}, {"noir", "Noir"}, {"neon", "Neon"}}},
            {Kind::Choice, "film_grain", "Film Grain", true,
             "Fine animated grain over the venue, like a concert shot on film. Light is barely there; Strong is "
             "clearly visible.",
             {{"off", "Off"}, {"light", "Light"}, {"strong", "Strong"}}},
            {Kind::Check, "chromatic", "Chromatic Aberration", true,
             "A camera lens's colour fringes: red and blue edges pull apart towards the corners of the venue."},
            {Kind::Check, "light_streaks", "Anamorphic Light Streaks", true,
             "The brightest stage lights flare into long blue-white horizontal streaks, like a cinema lens."},
            {Kind::Check, "ssao", "Ambient Occlusion", true,
             "Soft contact shadows where surfaces meet: under the band's feet, in the folds of clothes, in the "
             "corners of the stage, for more depth. Costs some performance."},
            {Kind::Choice, "sharpening", "Sharpening", true,
             "Contrast-adaptive sharpening of the venue (AMD CAS): crisper detail without halos, good with TAA or "
             "an upscaler.",
             {{"off", "Off"}, {"low", "Low"}, {"high", "High"}}},
            {Kind::Choice, "star_blur", "Blur on Star Power", true,
             "While Star Power is on, the venue and the band blur with the camera's motion and leave light trails; "
             "it fades out when Star Power ends. The highway always stays sharp.",
             {{"none", "None"}, {"light", "Light"}, {"medium", "Medium"}, {"high", "High"}}},
            {Kind::Separator, "", "", true, ""},
            {Kind::Section, "enhancers", "ENHANCERS", true, "Visual extras beyond the original game, and your user files."},
            {Kind::Check, "shadows_all", "All Band Mates Cast Shadows", true,
             "Every band mate gets a spotlight from above and casts a shadow on the stage floor, like the guitarist."},
            {Kind::Choice, "more_lights", "More Lights on Venues", true,
             "Richer stage lighting: brighter light cones and floor pools, dust in the beams and a rim light on the "
             "band, following the venue's own light cues.",
             {{"off", "Off"}, {"subtle", "Subtle"}, {"strong", "Strong"}}},
            {Kind::Check, "light_dust", "Dust in Light Beams", true,
             "With More Lights on Venues: fine dust drifting and twinkling in the light beams.", {}, "more_lights"},
            {Kind::Choice, "ground_fog", "Add Fog to the Ground (Experimental)", true,
             "A low fog over the stage floor where the band plays, for more depth. Low is a thin haze; Full a thick "
             "stage-fog layer. Super Fog: a volumetric fog with light shafts from the stage lights and a haze in "
             "the air, glowing as you look into the lights (costs more on slower GPUs).",
             {{"none", "None"}, {"low", "Low"}, {"full", "Full"}, {"super", "Super Fog"}}},
            {Kind::Check, "no_shaky_cam", "Disable Shaky Camera", true,
             "The original game shakes the camera on its close-up shots of the band, like a handheld camera. Turn this "
             "on for steady close-ups."},
            {Kind::Button, "replace_textures", "Replace Textures", false,
             "Choose a folder on your computer with replacement textures, loaded over the game's own.", {{"", "Browse..."}}},
            {Kind::Button, "export", "Export User Files", true,
             "Choose a folder to save two files in: GD01-user-settings.json with every Enhanced Setting, and "
             "GD01-user-progress.json with all your bands and their progress. Use them as a backup or to move to "
             "another computer.",
             {{"", "Export..."}}},
            {Kind::Button, "load_settings", "Load User Settings", true,
             "Choose a GD01-user-settings.json file to load. The settings take effect at once; any setting in the "
             "file that is missing or not valid keeps its default.",
             {{"", "Browse..."}}},
            {Kind::Button, "load_progress", "Load User Progress", true,
             "Choose a GD01-user-progress.json file to load. It replaces your bands and their progress and is saved "
             "at once; any value in the file that is missing or not valid keeps its default.",
             {{"", "Browse..."}}},
            {Kind::Separator, "", "", true, ""},
            {Kind::Section, "cheats", "CHEATS", true, "The original game's cheats and more ways to bend the rules."},
            {Kind::Check, "cut_content", "Enable Cut Content", false,
             "Loads content left unused on the original disc into the game."},
            {Kind::Check, "no_fail", "No Fail Mode", true, "The rock meter cannot fail you: every song plays to the end."},
            {Kind::Check, "autoplay", "Autoplay", true, "The game plays every note of a song by itself, without a miss."},
            {Kind::Choice, "hopo_mode", "Hammer-ons & Pull-offs", true,
             "Notes with a coloured top can be played without strumming. Original: as on the PS2, after a strum "
             "while a fret stays held, from 40 ms before the note, and a wrong fret locks the note out. Forgiving: no "
             "strum or held fret needed after the previous note was hit, as early as a strum may be, and a wrong "
             "fret can be corrected.",
             {{"original", "Original"}, {"forgiving", "Forgiving"}}},
            {Kind::Check, "cheat_air_guitar", "Air Guitar", true,
             "The original cheat: your rocker plays without a guitar. Cannot be on with Guitar Hero Controller Guitar."},
            {Kind::Check, "cheat_gh_guitar", "Guitar Hero Controller Guitar", true,
             "The original cheat: your rocker plays the Guitar Hero controller itself as the guitar. Cannot be on with "
             "Air Guitar."},
            {Kind::Check, "cheat_unlock_all", "Unlock Everything", true,
             "The original cheat: every song, venue, character and guitar is open on every difficulty. Nothing is "
             "bought or saved by it: turn it off and your own unlocks are back."},
            {Kind::Check, "cheat_full_meter", "Locked Full Rock Meter", true,
             "The original cheat: the rock meter stays full whatever you play, so the crowd always loves you and you "
             "cannot fail. Songs start with it."},
            {Kind::Check, "cheat_no_venue", "No Venue", true,
             "The original cheat: the venue, the band and the crowd are hidden, leaving only the highway on a black "
             "background."},
            {Kind::Check, "cheat_monkey_crowd", "Monkey Head Crowd", true,
             "The original cheat: everyone in the crowd wears a monkey head. Cannot be on with Skull Head Crowd."},
            {Kind::Check, "cheat_skull_crowd", "Skull Head Crowd", true,
             "The original cheat: everyone in the crowd has a skull for a head. Cannot be on with Monkey Head Crowd."},
        };
        return k;
    }

    static constexpr int kUp = 12, kDown = 14, kLeft = 15, kRight = 13, kCross = 6;
    static constexpr float kListTop = 168, kListBottom = 612, kListX0 = 80, kNameX = 118, kCtrlX1 = 612;
    static constexpr float kBoxX0 = 56, kBoxY0 = 22, kBoxX1 = 640, kBoxY1 = 640;
    static constexpr float kDdX0 = 360, kDdPopX0 = 300, kDdRow = 30, kPopX0 = 700, kPopX1 = 1224;  // kDdPopX0: the open list
    static constexpr std::array<float, 4> kTitle{0.95f, 0.95f, 0.95f, 1}, kFocused{1, 0.86f, 0.2f, 1},
        kNormal{0.85f, 0.85f, 0.88f, 1}, kDim{0.5f, 0.5f, 0.55f, 1}, kAccent{1, 0.55f, 0.15f, 1}, kOn{0.3f, 1, 0.35f, 1},
        kWarn{1, 0.78f, 0.1f, 1};

    // -- the rows shown (sections collapse; separators stay)
    void rebuild() {
        int keep = rows_.empty() ? -1 : rows_[size_t(std::clamp(focus_, 0, int(rows_.size()) - 1))];
        rows_.clear();
        bool hidden = false;
        for (size_t i = 0; i < items().size(); ++i) {
            const Item& it = items()[i];
            if (it.kind == Kind::Section) hidden = collapsed(it.key);
            if (it.kind == Kind::Separator) hidden = false;
            if (it.kind != Kind::Section && it.kind != Kind::Separator && hidden) continue;
            if (it.needs && !settingOn(it.needs)) continue;
            rows_.push_back(int(i));
        }
        focus_ = 0;
        for (size_t r = 0; r < rows_.size(); ++r)
            if (rows_[r] == keep) focus_ = int(r);
        scrollTarget_ = std::clamp(scrollTarget_, 0.0f, maxScroll());
    }
    const Item& item(int row) const { return items()[size_t(rows_[size_t(row)])]; }
    bool collapsed(const std::string& section) const {
        auto it = collapsed_.find(section);
        return it == collapsed_.end() ? section == "deck" : it->second;  // Steam Deck starts collapsed
    }
    static float rowHeight(const Item& it) { return it.kind == Kind::Section ? 44 : it.kind == Kind::Separator ? 18 : 34; }
    float rowY(int row) const {
        float y = kListTop - scroll_;
        for (int r = 0; r < row; ++r) y += rowHeight(item(r));
        return y;
    }
    float contentHeight() const {
        float h = 0;
        for (size_t r = 0; r < rows_.size(); ++r) h += rowHeight(item(int(r)));
        return h;
    }
    float maxScroll() const { return std::max(0.0f, contentHeight() - (kListBottom - kListTop)); }
    void moveFocus(int dir) {
        int n = int(rows_.size());
        for (int k = 0; k < n; ++k) {
            focus_ = (focus_ + dir + n) % n;
            if (item(focus_).kind != Kind::Separator) break;
        }
        // Keep the focus in view, with the section title above a first row.
        float top = rowY(focus_) + scroll_ - kListTop, h = rowHeight(item(focus_));
        if (top < scrollTarget_ + 4) scrollTarget_ = top - (focus_ > 0 ? 8 : 0);
        if (top + h > scrollTarget_ + (kListBottom - kListTop) - 4) scrollTarget_ = top + h - (kListBottom - kListTop) + 8;
        scrollTarget_ = std::clamp(scrollTarget_, 0.0f, maxScroll());
        sound("button_toggle");
    }

    // -- values
    bool deckOn() { return host_.enhancedSetting("steam_deck"); }
    bool settingOn(const char* key) {
        std::string v = host_.enhancedValue(key);
        return v.empty() ? host_.enhancedSetting(key) : v != "none" && v != "off";
    }
    bool editable(const Item& it) { return it.ready && (std::string(it.key) == "steam_deck" || !deckOn()); }
    static int optionIndex(const Item& it, const std::string& v) {
        for (size_t k = 0; k < it.options.size(); ++k)
            if (v == it.options[k].value) return int(k);
        return 0;
    }
    void setValue(const Item& it, const std::string& v) {
        for (const Option& o : it.options)
            if (v == o.value && !o.ok) return sound("button_error");
        host_.setEnhancedValue(it.key, v);
        rebuild();  // a choice may show or hide its dependent row
        sound("button_select");
    }
    void sound(const char* seq) { host_.sendTo("synth", "play_sequence", {Value::symbol(seq)}); }
    void activate() {
        const Item& it = item(focus_);
        switch (it.kind) {
        case Kind::Section:
            collapsed_[it.key] = !collapsed(it.key);
            rebuild();
            sound("button_select");
            return;
        case Kind::Separator: return;
        case Kind::Check:
            if (!editable(it)) return sound("button_error");
            host_.setEnhancedSetting(it.key, !host_.enhancedSetting(it.key));
            rebuild();  // a check may show or hide its dependent row
            sound("button_select");
            return;
        case Kind::Choice: {
            if (!editable(it)) return sound("button_error");
            dropdown_ = focus_;
            ddFocus_ = optionIndex(it, host_.enhancedValue(it.key));
            sound("button_select");
            return;
        }
        case Kind::Button:
            // The user files and the calibration work in Steam Deck mode too.
            if (!it.ready) return sound("button_error");
            sound("button_select");
            if (std::string(it.key) == "calibrate") return calibStart();
            host_.userFiles(it.key);
            return;
        }
    }

    // -- the screen transition: the page slides like the other screens' posters
    void updateTransition() {
        int phase = 0;
        float t = 1;
        bool back = false;
        host_.transition(phase, t, back);
        float e = t * t * (3 - 2 * t);
        offset_ = 0, fade_ = 1;
        if (phase == 2) offset_ = (back ? -1.0f : 1.0f) * (1 - e) * kScreenW * 0.6f, fade_ = e;
        if (phase == 1) offset_ = (back ? 1.0f : -1.0f) * e * kScreenW * 0.6f, fade_ = 1 - e;
    }
    std::array<float, 4> faded(std::array<float, 4> c) const { return c[3] *= fade_, c; }
    void text(const char* font, const std::string& s, float x, float y, float h, const std::array<float, 4>& c, uint32_t align) {
        host_.drawText(font, s, x, y, h, faded(c), align);
    }

    // -- drawing helpers (menu units, 1280 x 720)
    void quad(float x0, float y0, float x1, float y1, const std::array<float, 4>& c) {
        auto f = faded(c);
        SDL_Color k{uint8_t(f[0] * 255), uint8_t(f[1] * 255), uint8_t(f[2] * 255), uint8_t(f[3] * 255)};
        const Vertex v[6] = {{x0, y0, 0, 0, 0, k.r, k.g, k.b, k.a}, {x1, y0, 0, 0, 0, k.r, k.g, k.b, k.a},
                             {x1, y1, 0, 0, 0, k.r, k.g, k.b, k.a}, {x0, y0, 0, 0, 0, k.r, k.g, k.b, k.a},
                             {x1, y1, 0, 0, 0, k.r, k.g, k.b, k.a}, {x0, y1, 0, 0, 0, k.r, k.g, k.b, k.a}};
        host_.renderer().draw(v, 6, 0, Blend::Alpha);
    }
    void frame(float x0, float y0, float x1, float y1, float t, const std::array<float, 4>& c) {
        quad(x0, y0, x1, y0 + t, c), quad(x0, y1 - t, x1, y1, c), quad(x0, y0 + t, x0 + t, y1 - t, c), quad(x1 - t, y0 + t, x1, y1 - t, c);
    }
    void line(float x0, float y0, float x1, float y1, float w, const std::array<float, 4>& c) {
        float dx = x1 - x0, dy = y1 - y0, l = std::hypot(dx, dy);
        if (l <= 0) return;
        float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
        auto f = faded(c);
        uint8_t r = uint8_t(f[0] * 255), g = uint8_t(f[1] * 255), b = uint8_t(f[2] * 255), a = uint8_t(f[3] * 255);
        const Vertex v[6] = {{x0 + nx, y0 + ny, 0, 0, 0, r, g, b, a}, {x1 + nx, y1 + ny, 0, 0, 0, r, g, b, a},
                             {x1 - nx, y1 - ny, 0, 0, 0, r, g, b, a}, {x0 + nx, y0 + ny, 0, 0, 0, r, g, b, a},
                             {x1 - nx, y1 - ny, 0, 0, 0, r, g, b, a}, {x0 - nx, y0 - ny, 0, 0, 0, r, g, b, a}};
        host_.renderer().draw(v, 6, 0, Blend::Alpha);
    }
    void triangle(float ax, float ay, float bx, float by, float cx, float cy, const std::array<float, 4>& c) {
        auto f = faded(c);
        uint8_t r = uint8_t(f[0] * 255), g = uint8_t(f[1] * 255), b = uint8_t(f[2] * 255), a = uint8_t(f[3] * 255);
        const Vertex v[3] = {{ax, ay, 0, 0, 0, r, g, b, a}, {bx, by, 0, 0, 0, r, g, b, a}, {cx, cy, 0, 0, 0, r, g, b, a}};
        host_.renderer().draw(v, 3, 0, Blend::Alpha);
    }
    void arrow(float x, float y, bool up) {
        if (up) triangle(x - 9, y + 5, x + 9, y + 5, x, y - 5, kDim);
        else triangle(x - 9, y - 5, x + 9, y - 5, x, y + 5, kDim);
    }
    void warning(float x, float cy) {  // a yellow warning sign: not built yet
        triangle(x, cy + 9, x + 20, cy + 9, x + 10, cy - 9, kWarn);
        text("impactor.font", "!", x + 10, cy + 1, 15, {0, 0, 0, 1}, 0x22);
    }

    void drawRow(int r) {
        const Item& it = item(r);
        const float h = rowHeight(it), top = rowY(r), cy = top + h * 0.5f, o = offset_;
        if (top < kListTop - 1 || top + h > kListBottom + 1) return;
        const bool f = r == focus_;
        if (it.kind == Kind::Separator) {
            quad(kListX0 + o, cy - 1, kCtrlX1 + 8 + o, cy + 1, {0.3f, 0.3f, 0.34f, 1});
            return;
        }
        if (f) quad(kListX0 - 6 + o, top + 2, kCtrlX1 + 12 + o, top + h - 2, {1, 0.86f, 0.2f, 0.12f});
        if (it.kind == Kind::Section) {
            bool c = collapsed(it.key);
            float ax = kListX0 + 8 + o;
            if (c) triangle(ax - 4, cy - 8, ax - 4, cy + 8, ax + 8, cy, f ? kFocused : kAccent);
            else triangle(ax - 7, cy - 5, ax + 7, cy - 5, ax, cy + 7, f ? kFocused : kAccent);
            text("impactor.font", it.name, kListX0 + 26 + o, cy + 1, 27, f ? kFocused : kAccent, 0x21);
            return;
        }
        const bool locked = it.kind == Kind::Button ? !it.ready : !editable(it);
        const std::array<float, 4> nameC = f ? kFocused : locked ? kDim : kNormal;
        // A long name next to a dropdown is drawn smaller to fit before it.
        float nameH = 23;
        if (it.kind == Kind::Choice)
            if (float w = host_.textWidth("impactor.font", it.name, 23), room = kDdX0 - kNameX - 14; w > room) nameH = 23 * room / w;
        text("impactor.font", it.name, kNameX + o, cy + 1, nameH, nameC, 0x21);
        if (!it.ready) warning(kNameX + o + host_.textWidth("impactor.font", it.name, 23) + 10, cy);
        const std::array<float, 4> ctrl = locked ? kDim : f ? kFocused : kNormal;
        switch (it.kind) {
        case Kind::Check: {
            const float x1 = kCtrlX1 + o, x0 = x1 - 24;
            frame(x0, cy - 12, x1, cy + 12, 2.5f, ctrl);
            if (host_.enhancedSetting(it.key)) {
                auto c = locked ? kDim : kOn;
                line(x0 + 5, cy + 1, x0 + 10, cy + 7, 4, c);
                line(x0 + 10, cy + 7, x0 + 20, cy - 7, 4, c);
            }
            break;
        }
        case Kind::Choice: {
            const float x1 = kCtrlX1 + o, x0 = kDdX0 + o;
            const std::string v = host_.enhancedValue(it.key);
            frame(x0, cy - 13, x1, cy + 13, 2, locked ? kDim : std::array<float, 4>{0.5f, 0.5f, 0.55f, 1});
            text("impactor.font", it.options[size_t(optionIndex(it, v))].label, x0 + 10, cy + 1, 20, ctrl, 0x21);
            triangle(x1 - 22, cy - 4, x1 - 8, cy - 4, x1 - 15, cy + 5, ctrl);
            break;
        }
        case Kind::Button: {
            const float x1 = kCtrlX1 + o, x0 = x1 - 120;
            frame(x0, cy - 13, x1, cy + 13, 2, ctrl);
            text("impactor.font", it.options.empty() ? "" : it.options[0].label, (x0 + x1) * 0.5f, cy + 1, 20, ctrl, 0x22);
            break;
        }
        default: break;
        }
    }

    void drawDropdown() {
        const Item& it = item(dropdown_);
        const float rowTop = rowY(dropdown_), h = rowHeight(it), n = float(it.options.size());
        ddTop_ = rowTop + h - 2;
        if (ddTop_ + n * kDdRow > kScreenH - 70) ddTop_ = rowTop + 2 - n * kDdRow;  // no room below: open upward
        const float o = offset_, x0 = kDdPopX0 + o, x1 = kCtrlX1 + o;
        quad(x0, ddTop_, x1, ddTop_ + n * kDdRow, {0.08f, 0.08f, 0.1f, 0.97f});
        frame(x0, ddTop_, x1, ddTop_ + n * kDdRow, 2, kAccent);
        const std::string v = host_.enhancedValue(it.key);
        for (size_t k = 0; k < it.options.size(); ++k) {
            float oy = ddTop_ + float(k) * kDdRow;
            bool hot = int(k) == ddFocus_;
            if (hot) quad(x0 + 2, oy + 2, x1 - 2, oy + kDdRow - 2, {1, 0.86f, 0.2f, 0.18f});
            const bool ok = it.options[k].ok;
            text("impactor.font", ok ? std::string(it.options[k].label) : std::string(it.options[k].label) + " (not available yet)", x0 + 12,
                 oy + kDdRow * 0.5f + 1, 20, !ok ? kDim : hot ? kFocused : kNormal, 0x21);
            if (v == it.options[k].value) line(x1 - 26, oy + 15, x1 - 21, oy + 20, 3, kOn), line(x1 - 21, oy + 20, x1 - 12, oy + 9, 3, kOn);
        }
    }

    // The focused option's explanation, in its box on the right.
    void drawHelp() {
        const Item& it = item(focus_);
        if (it.kind == Kind::Separator) return;
        const float o = offset_, x0 = kPopX0 + o, x1 = kPopX1 + o, pad = 24, bodyH = 22, lineH = 30;
        std::vector<std::string> lines = wrap(it.help, x1 - x0 - 2 * pad, bodyH);
        const bool locked = it.kind != Kind::Section && it.kind != Kind::Button && it.ready && !editable(it);
        const bool calib = std::string(it.key) == "calibrate" || std::string(it.key) == "ai_upscaling";  // a status line
        // The upscaler's status (what runs, its resolutions and cost) can take two lines.
        std::vector<std::string> status;
        if (std::string(it.key) == "ai_upscaling") {
            const std::string st = host_.enhancedValue("upscaler_status");
            status = wrap("Now: " + (st.empty() ? std::string("off") : st), x1 - x0 - 2 * pad, 19);
        }
        float height = pad + 46 + float(lines.size()) * lineH + (!it.ready || locked || calib ? 42 : 0) +
                       float(status.size() > 1 ? status.size() - 1 : 0) * 26 + pad;
        // Bottom-aligned with the options box, leaving the backdrop's microphone in view above.
        const float top = kBoxY1 - height;
        float y = top + pad;
        quad(x0, top, x1, kBoxY1, {0, 0, 0, 0.7f});
        frame(x0, top, x1, kBoxY1, 1.5f, {1, 1, 1, 0.12f});
        quad(x0, top, x1, top + 5, kAccent);
        text("impactor.font", it.name, x0 + pad, y + 2, 30, kFocused, 0x11);
        y += 46;
        for (const std::string& l : lines) text("impactor.font", l, x0 + pad, y, bodyH, kNormal, 0x11), y += lineH;
        if (std::string(it.key) == "ai_upscaling") {
            y += 6;
            for (const std::string& l : status) text("impactor.font", l, x0 + pad, y + 13, 19, kOn, 0x21), y += 26;
        } else if (std::string(it.key) == "calibrate") {
            y += 6;
            text("impactor.font", "Now: audio " + signedMs(host_.enhancedValue("audio_offset")) + ", video " +
                 signedMs(host_.enhancedValue("video_offset")), x0 + pad, y + 13, 21, kOn, 0x21);
        } else if (!it.ready) {
            y += 6;
            warning(x0 + pad, y + 12);
            text("impactor.font", "Not available yet", x0 + pad + 30, y + 13, 21, kWarn, 0x21);
        } else if (locked) {
            y += 6;
            text("impactor.font", "Locked by Steam Deck mode", x0 + pad, y + 13, 21, kAccent, 0x21);
        }
    }
    // -- Audio / Video Calibration (the port's; the original had none). A
    // beat every 750 ms: the audio step plays a tick, the video step flashes
    // a ring silently. After 3 lead-in beats, each press is matched to the
    // nearest beat; the median of 8 is the step's offset. audio_offset
    // (heard + input delay) moves the judging, video_offset (seen + input)
    // draws the highway ahead (app.cpp).
    static constexpr uint64_t kBeatNs = 750000000ull;
    static constexpr int kLeadIn = 3, kSamples = 8;
    struct Calib {
        int phase = 0;  // 0 off, 1 audio, 2 video, 3 done
        uint64_t nextTick = 0, flashNs = 0;
        std::vector<uint64_t> ticks;
        std::vector<float> samples;
        float audio = 0, video = 0;
    };
    static std::string signedMs(const std::string& v) {
        int ms = std::atoi(v.c_str());
        return (ms > 0 ? "+" : "") + std::to_string(ms) + " ms";
    }
    void calibStart() {
        cal_ = Calib{};
        cal_.phase = 1;
        cal_.nextTick = SDL_GetTicksNS() + 1000000000ull;
    }
    void calibPoll() {
        if (cal_.phase != 1 && cal_.phase != 2) return;
        uint64_t now = SDL_GetTicksNS();
        if (now < cal_.nextTick) return;
        if (cal_.phase == 1) sound("button_toggle");
        else cal_.flashNs = now;
        cal_.ticks.push_back(now);
        cal_.nextTick += kBeatNs;
        if (cal_.nextTick < now) cal_.nextTick = now + kBeatNs;  // a stall: no burst of ticks
    }
    void calibButton(int button) {
        if (host_.isBackButton(button)) {  // cancel (or close the result)
            cal_.phase = 0;
            sound("button_back");
            return;
        }
        if (cal_.phase == 3) {
            if (host_.isSelectButton(button)) cal_.phase = 0, sound("button_select");
            return;
        }
        if (!host_.isSelectButton(button) && button != kUp && button != kDown) return;  // green, Enter or the strum bar
        if (int(cal_.ticks.size()) < kLeadIn) return;
        uint64_t now = SDL_GetTicksNS();
        float best = 1e9f;
        for (uint64_t t : cal_.ticks)
            if (std::fabs(float(int64_t(now - t)) / 1e6f) < std::fabs(best)) best = float(int64_t(now - t)) / 1e6f;
        if (float d = float(int64_t(now - cal_.nextTick)) / 1e6f; std::fabs(d) < std::fabs(best)) best = d;  // early for the next
        if (std::fabs(best) > kBeatNs / 1e6f * 0.45f) return;
        cal_.samples.push_back(best);
        if (int(cal_.samples.size()) < kSamples) return;
        std::vector<float> s = cal_.samples;
        std::nth_element(s.begin(), s.begin() + long(s.size() / 2), s.end());
        float median = std::clamp(s[s.size() / 2], -300.0f, 500.0f);
        if (cal_.phase == 1) {
            cal_.audio = median;
            cal_.phase = 2;
            cal_.ticks.clear(), cal_.samples.clear();
            cal_.nextTick = now + 1500000000ull;
        } else {
            cal_.video = median;
            cal_.phase = 3;
            host_.setEnhancedValue("audio_offset", std::to_string(std::lround(cal_.audio)));
            host_.setEnhancedValue("video_offset", std::to_string(std::lround(cal_.video)));
        }
    }
    void drawCalib() {
        Renderer& r = host_.renderer();
        r.fillRect(0, 0, kScreenW, kScreenH, {0, 0, 0, uint8_t(225 * fade_)});
        const float cx = kScreenW * 0.5f, cy = 360;
        text("rokk.font", "CALIBRATION", cx, 110, 56, kTitle, 0x22);
        if (cal_.phase == 3) {
            text("impactor.font", "Audio " + signedMs(std::to_string(std::lround(cal_.audio))) + "   Video " +
                 signedMs(std::to_string(std::lround(cal_.video))), cx, cy, 34, kOn, 0x22);
            text("impactor.font", "Saved. Press green to close.", cx, cy + 60, 22, kNormal, 0x22);
            return;
        }
        const bool audio = cal_.phase == 1;
        text("impactor.font", audio ? "Step 1 of 2: AUDIO" : "Step 2 of 2: VIDEO", cx, 200, 30, kAccent, 0x22);
        text("impactor.font", audio ? "Strum or press green on each tick you HEAR" : "Strum or press green on each flash you SEE", cx, 245,
             24, kNormal, 0x22);
        if (!audio) {  // the ring, lit for 120 ms on each beat
            float lit = cal_.flashNs ? std::max(0.0f, 1 - float(SDL_GetTicksNS() - cal_.flashNs) / 120e6f) : 0;
            for (int i = 0; i < 48; ++i) {
                float a0 = float(i) / 48 * 6.2831853f, a1 = float(i + 1) / 48 * 6.2831853f;
                line(cx + 70 * std::cos(a0), cy + 70 * std::sin(a0), cx + 70 * std::cos(a1), cy + 70 * std::sin(a1), 6,
                     {0.4f + 0.6f * lit, 0.4f + 0.46f * lit, 0.45f - 0.25f * lit, 1});
            }
            if (lit > 0) {
                std::vector<Vertex> v;
                uint8_t a = uint8_t(255 * lit * fade_);
                for (int i = 0; i < 48; ++i) {
                    float a0 = float(i) / 48 * 6.2831853f, a1 = float(i + 1) / 48 * 6.2831853f;
                    v.push_back({cx, cy, 0, 0, 0, 255, 220, 50, a});
                    v.push_back({cx + 62 * std::cos(a0), cy + 62 * std::sin(a0), 0, 0, 0, 255, 220, 50, a});
                    v.push_back({cx + 62 * std::cos(a1), cy + 62 * std::sin(a1), 0, 0, 0, 255, 220, 50, a});
                }
                r.draw(v.data(), v.size(), 0, Blend::Alpha);
            }
        }
        // Progress: one dot per press taken.
        for (int k = 0; k < kSamples; ++k) {
            float x = cx + (float(k) - (kSamples - 1) * 0.5f) * 34;
            quad(x - 9, cy + 130, x + 9, cy + 148, k < int(cal_.samples.size()) ? kOn : std::array<float, 4>{0.3f, 0.3f, 0.34f, 1});
        }
        if (int(cal_.ticks.size()) < kLeadIn) text("impactor.font", "Get ready...", cx, cy + 190, 22, kDim, 0x22);
        text("impactor.font", "Back cancels", cx, cy + 230, 18, kDim, 0x22);
    }

    std::vector<std::string> wrap(const std::string& s, float width, float h) {
        std::vector<std::string> out;
        std::string line, word;
        auto flush = [&] {
            if (word.empty()) return;
            std::string next = line.empty() ? word : line + " " + word;
            if (!line.empty() && host_.textWidth("impactor.font", next, h) > width) out.push_back(line), line = word;
            else line = next;
            word.clear();
        };
        for (char c : s) {
            if (c == ' ') flush();
            else word += c;
        }
        flush();
        if (!line.empty()) out.push_back(line);
        return out;
    }

    SelectHost& host_;
    std::vector<int> rows_;  // items() shown
    std::map<std::string, bool> collapsed_;
    int focus_ = 0, dropdown_ = -1, ddFocus_ = 0;
    float scroll_ = 0, scrollTarget_ = 0, ddTop_ = 0, offset_ = 0, fade_ = 1, statusMs_ = 0;
    std::string status_;
    Calib cal_;
};

}  // namespace

std::unique_ptr<NativePanel> makeNativePanel(const std::string& verb, const std::string& name, SelectHost& host,
                                             const std::map<std::string, Value>& props) {
    if (verb == "new_charsys_panel") return std::make_unique<CharsysPanel>(host, name, props);
    if (verb == "new_guitar_display_panel") return std::make_unique<GuitarDisplayPanel>(host, props);
    if (verb == "new_guitar_select_panel") return std::make_unique<GuitarSelectPanel>(host, props);
    if (verb == "new_multi_char_sel_panel") return std::make_unique<MultiCharSelPanel>(host, props);
    if (verb == "new_enhanced_panel") return std::make_unique<EnhancedPanel>(host);
    if (verb == "new_tutorial_panel") return makeTutorialPanel(host);
    if (verb == "new_credits_panel") return std::make_unique<CreditsPanel>(host);
    return makeGamePanel(verb, host, props);
}
