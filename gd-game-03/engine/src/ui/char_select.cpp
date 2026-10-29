// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The band's look: characters, outfits, guitars and skins (ui/char_select.h).
#include "ui/char_select.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <cstdio>
#include <functional>

#include "char/char_driver.h"
#include "char/char_objects.h"
#include "char/character.h"
#include "char/skeleton.h"
#include "render/band_view.h"
#include "render/canvas.h"
#include "render/scene_gpu.h"
#include "scene/milo.h"
#include "scene/ui_styles.h"
#include "ui/campaign.h"
#include "ui/ui_manager.h"
#include "ui/ui_objects.h"

namespace ui::chars {

namespace {

std::string nameOf(const Value& v) { return v.type == Value::Type::Object && v.object ? v.object->name() : v.str(); }
int intArg(const std::vector<Value>& a, size_t i, int def = 0) { return i < a.size() && a[i].isNumber() ? a[i].integerValue() : def; }
std::string strArg(const std::vector<Value>& a, size_t i) { return i < a.size() ? nameOf(a[i]) : std::string(); }

constexpr int kTrackBass = 0;  // config/macros.dta kTrackBass (0), kTrackGuitar (2)

DataNode loadDta(UiManager& ui, const char* path) {
    try {
        return scene::loadPlainDta(ui.data(), path);
    } catch (const std::exception& e) {
        SDL_Log("chars: %s: %s", path, e.what());
        return {};
    }
}

int indexIn(const std::vector<std::string>& v, const std::string& s) {
    auto it = std::find(v.begin(), v.end(), s);
    return it == v.end() ? -1 : int(it - v.begin());
}

}  // namespace

const Data& data(UiManager& ui) {
    static Data d;
    static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    const DataNode gh2 = loadDta(ui, "config/gen/gh2.dtb");
    if (const DataNode* cs = gh2.find("characters"))
        for (size_t k = 1; k < cs->children.size(); ++k) {
            const DataNode& c = cs->children[k];
            if (!c.isList() || c.children.empty() || !c.children[0].isText()) continue;
            std::vector<std::string> outfits;
            for (size_t i = 1; i < c.children.size(); ++i)
                if (c.children[i].isList() && !c.children[i].children.empty() && c.children[i].children[0].isText())
                    outfits.push_back(c.children[i].children[0].s);
            d.characters.push_back({c.children[0].s, outfits});
        }
    const DataNode gs = loadDta(ui, "config/gen/guitars.dtb");
    for (const DataNode& g : gs.children) {
        if (!g.isList() || g.children.empty() || !g.children[0].isText()) continue;
        Guitar out;
        out.name = g.children[0].s;
        if (const DataNode* t = g.find("type")) out.type = t->textAt(1);
        if (const DataNode* skins = g.find("skins"))
            for (size_t k = 1; k < skins->children.size(); ++k) {
                const DataNode& s = skins->children[k];
                if (!s.isList() || s.children.empty() || !s.children[0].isText()) continue;
                Skin sk;
                sk.name = s.children[0].s;
                if (const DataNode* o = s.find("outfit")) sk.outfit = o->textAt(1);
                if (const DataNode* m = s.find("mat")) sk.mat = m->textAt(1);
                out.skins.push_back(sk);
            }
        d.guitars.push_back(out);
    }
    const DataNode store = loadDta(ui, "config/gen/store.dtb");
    for (const char* cat : {"guitar", "skin", "character", "outfit"})
        if (const DataNode* c = store.find(cat))
            for (size_t k = 1; k < c->children.size(); ++k)
                if (c->children[k].isList() && !c->children[k].children.empty() && c->children[k].children[0].isText())
                    d.store.push_back(c->children[k].children[0].s);
    return d;
}

bool unlocked(UiManager& ui, const std::string& sym, int* known) {
    const Data& d = data(ui);
    bool isItem = false;
    for (const auto& [c, outfits] : d.characters)
        if (c == sym || indexIn(outfits, sym) >= 0) isItem = true;
    for (const Guitar& g : d.guitars) {
        if (g.name == sym) isItem = true;
        for (const Skin& s : g.skins)
            if (s.name == sym) isItem = true;
    }
    if (known) *known = isItem ? 1 : 0;
    return campaign::isUnlocked(ui, sym);
}

std::string characterOf(UiManager& ui, const std::string& outfit, int* index) {
    for (const auto& [c, outfits] : data(ui).characters)
        if (int i = indexIn(outfits, outfit); i >= 0) {
            if (index) *index = i;
            return c;
        }
    if (index) *index = 0;
    return {};
}

std::string outfitAt(UiManager& ui, const std::string& character, int i) {
    for (const auto& [c, outfits] : data(ui).characters)
        if (c == character && !outfits.empty()) return outfits[size_t(std::clamp(i, 0, int(outfits.size()) - 1))];
    return {};
}

int numOutfits(UiManager& ui, const std::string& character) {
    int n = 0;
    for (const auto& [c, outfits] : data(ui).characters)
        if (c == character)
            for (const std::string& o : outfits) n += unlocked(ui, o) ? 1 : 0;
    return n;
}

const Guitar* guitar(UiManager& ui, const std::string& name) {
    for (const Guitar& g : data(ui).guitars)
        if (g.name == name) return &g;
    return nullptr;
}

std::vector<std::string> guitars(UiManager& ui, const Session::PlayerCfg& p) {
    std::vector<std::string> out;
    if (p.character == "grim") return {"scythe"};
    const std::string type = p.trackType == kTrackBass ? "bass" : "guitar";
    for (const Guitar& g : data(ui).guitars)
        if (g.type == type && unlocked(ui, g.name)) out.push_back(g.name);
    return out;
}

std::vector<std::string> skins(UiManager& ui, const std::string& name) {
    std::vector<std::string> out;
    if (const Guitar* g = guitar(ui, name))
        for (const Skin& s : g->skins)
            if (unlocked(ui, s.name)) out.push_back(s.name);
    return out;
}

void validate(UiManager& ui, Session::PlayerCfg& p) {
    const std::vector<std::string> gs = guitars(ui, p);
    if (indexIn(gs, p.guitar) < 0) {
        p.guitar = gs.empty() ? std::string() : gs[0];
        const std::vector<std::string> ss = skins(ui, p.guitar);
        p.skin = ss.empty() ? std::string() : ss[0];
    }
    const std::vector<std::string> ss = skins(ui, p.guitar);
    if (indexIn(ss, p.skin) < 0) p.skin = ss.empty() ? std::string() : ss[0];
}

void setCharacter(UiManager& ui, Session::PlayerCfg& p, const std::string& outfit, bool reset) {
    p.character = characterOf(ui, outfit);
    p.outfit = outfit;
    if (reset) validate(ui, p);
}

void setGuitar(UiManager& ui, Session::PlayerCfg& p, const std::string& name, const std::string& skin, bool) {
    if (!guitar(ui, name)) {
        p.guitar.clear();
        validate(ui, p);
        return;
    }
    p.guitar = name;
    if (skin.empty() || skin == "default") {
        const std::vector<std::string> ss = skins(ui, name);
        p.skin = ss.empty() ? std::string() : ss[0];
    } else {
        p.skin = skin;
    }
}

bool playerConfigNative(UiManager& ui, Session::PlayerCfg& p, const std::string& msg, std::vector<Value>& args, Value& r) {
    r = Value::integer(0);
    if (msg == "get_character") return r = Value::symbol(p.character), true;
    if (msg == "get_character_outfit") return r = Value::symbol(p.outfit), true;
    if (msg == "set_character") return setCharacter(ui, p, strArg(args, 0), args.size() > 1 && args[1].truthy()), true;
    if (msg == "get_outfit_index") {
        int i = 0;
        characterOf(ui, p.outfit, &i);
        return r = Value::integer(i), true;
    }
    if (msg == "set_outfit_index") {
        setCharacter(ui, p, outfitAt(ui, p.character, intArg(args, 0)), false);
        return r = Value::symbol(p.outfit), true;
    }
    if (msg == "get_guitar") return validate(ui, p), r = Value::symbol(p.guitar), true;
    if (msg == "get_guitar_skin") return validate(ui, p), r = Value::symbol(p.skin), true;
    if (msg == "set_guitar" || msg == "force_guitar") return setGuitar(ui, p, strArg(args, 0), strArg(args, 1), msg == "force_guitar"), true;
    if (msg == "set_guitar_index") {
        const std::vector<std::string> gs = guitars(ui, p);
        const int i = intArg(args, 0);
        setGuitar(ui, p, i >= 0 && i < int(gs.size()) ? gs[size_t(i)] : std::string(), "default", false);
        return true;
    }
    if (msg == "get_guitar_desc" || msg == "get_guitar_skin_desc") return r = Value::symbol(strArg(args, 0) + "_desc"), true;
    if (msg == "get_num_skins") return validate(ui, p), r = Value::integer(int(skins(ui, p.guitar).size())), true;
    if (msg == "get_total_num_skins") {
        validate(ui, p);
        const Guitar* g = guitar(ui, p.guitar);
        return r = Value::integer(g ? int(g->skins.size()) : 0), true;
    }
    return false;
}

std::string pickPhoto(UiManager& ui, const Session::PlayerCfg& p) {
    char path[128];
    std::snprintf(path, sizeof path, "image/og/photo_%s%d_keep.bmp", p.outfit.c_str(), ui.interp().randomInt(0, 4));
    return path;
}

namespace {

// character_provider (0x822C1530): gh2.dta's characters in their order.
class CharacterProviderObj : public ListProvider {
public:
    explicit CharacterProviderObj(UiManager& ui) : ListProvider("character_provider", "CharacterProvider"), ui_(ui) {}
    int numData() override { return int(data(ui_).characters.size()); }
    std::string symbol(int i) override { return i >= 0 && i < numData() ? data(ui_).characters[size_t(i)].first : std::string(); }
    // The list itself shows nothing: the screen draws the portraits (char_<c>.mesh).
    void rows(std::vector<gfx::UiView::ListRow>& out, int selected, int& selectedRow) override {
        out.assign(size_t(numData()), {});
        selectedRow = selected;
    }
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "get_symbol") return r = Value::symbol(symbol(intArg(args, 0))), true;
        if (msg == "get_index") return r = Value::integer(indexOf(strArg(args, 0))), true;
        if (msg == "num_data") return r = Value::integer(numData()), true;
        // IsActive [I: the character is unlocked, as the screen's locked portraits].
        if (msg == "is_active") return r = Value::integer(unlocked(ui_, symbol(intArg(args, 0))) ? 1 : 0), true;
        if (msg == "num_outfits") return r = Value::integer(numOutfits(ui_, strArg(args, 0))), true;
        if (msg == "get_outfit") return r = Value::symbol(outfitAt(ui_, strArg(args, 0), intArg(args, 1))), true;
        if (msg == "set_store_mode" || msg == "init_data") return true;
        return false;
    }

private:
    UiManager& ui_;
};

// The view of the screen panel holding a milo object (a placer, a proxy, an Environ).
gfx::UiView* holderView(UiManager& ui, const Value& v, std::string& name) {
    name = nameOf(v);
    if (auto* c = v.type == Value::Type::Object ? dynamic_cast<ComponentObj*>(v.object) : nullptr; c && c->panel && c->panel->view)
        return c->panel->view.get();
    if (name.empty() || !ui.current()) return nullptr;
    for (PanelObj* p : ui.current()->panels())
        if (p->view && p->view->find(name)) return p->view.get();
    return nullptr;
}

// A script clip selection ({$this play_clip ui_loop {| kPlayLast kPlayGraphLoop}}).
chr::ClipSel clipSel(Interp& in, const Value& raw) {
    const Value v = in.evalValue(raw);
    chr::ClipSel s;
    if (v.isNumber()) s.kind = chr::ClipSel::Flags, s.flags = uint32_t(v.integerValue());
    else s.kind = chr::ClipSel::Name, s.name = nameOf(v);
    return s;
}

// A CharDriver as scripts see it ({$char find main.drv} transfer ...).
class DriverObj : public ScriptObject {
public:
    DriverObj(const std::string& name, chr::ClipDriver* d) : ScriptObject(name, "CharDriver"), drv(d) {}
    chr::ClipDriver* drv;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "transfer") {
            // CharDriver::Transfer [I]: the other driver's clips (by name) at their frames and weights.
            auto* src = args.empty() ? nullptr : dynamic_cast<DriverObj*>(in.evalValue(args[0]).object);
            if (!drv || !src || !src->drv || !drv->clipSet()) return true;
            std::vector<chr::ClipDriver::Node> nodes;
            for (chr::ClipDriver::Node n : src->drv->chain()) {
                if (!n.clip) continue;
                n.clip = drv->clipSet()->clip(n.clip->name);
                if (n.clip) nodes.push_back(n);
            }
            if (!nodes.empty()) drv->chainNodes() = nodes;
            return true;
        }
        if (msg == "play") {
            if (drv && !args.empty()) drv->play(clipSel(in, args[0]), args.size() > 1 ? uint32_t(in.evalValue(args[1]).integerValue()) : chr::kPlayLast);
            return true;
        }
        return false;
    }
};

// A menu character (char/<outfit>/og/<outfit>_ui.milo, Character type ui
// of char_objects.dta: animate / select / store play ui_enter / ui_loop).
class UiCharObj : public ScriptObject {
public:
    UiCharObj(const std::string& outfit, std::unique_ptr<chr::Character> c) : ScriptObject(outfit, "Character"), ch(std::move(c)) {
        main = std::make_unique<DriverObj>("main.drv", ch->mainDriver());
    }
    std::unique_ptr<chr::Character> ch;
    std::unique_ptr<DriverObj> main;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "play_clip") {
            if (chr::ClipDriver* d = ch->mainDriver(); d && !args.empty())
                d->play(clipSel(in, args[0]), args.size() > 1 ? uint32_t(in.evalValue(args[1]).integerValue()) : chr::kPlayLast);
            return true;
        }
        if (msg == "find" || msg == "exists") {
            const bool drv = strArg(args, 0) == "main.drv";
            if (msg == "exists") return r = Value::integer(drv || ch->dir().find(strArg(args, 0)) ? 1 : 0), true;
            return r = Value::obj(drv ? main.get() : nullptr), true;
        }
        if (msg == "get_type") return r = Value::symbol("ui"), true;
        // grim_fire_on / johnny_snot_rocket_on...: their particle systems are not drawn in the menus [I].
        if (msg == "grim_fire_on" || msg == "grim_fire_off" || msg == "johnny_snot_rocket_on" || msg == "johnny_snot_rocket_off") return true;
        return false;
    }
};

// CharsysPanel (0x8228E8A8; slots of 0x30 bytes at +0x38): per placer the
// outfit shown (show_char loads char/<outfit>/og/<outfit>_ui.milo through
// a cache of loaded characters, 0x8228E0A0 / 0x8228E428), the event the
// character gets once loaded (char_event, 0x8228D348), its BandPlacer, door
// and Environ. The characters stand on their placers and are drawn with
// the placer's panel camera and the slot's Environ.
class CharsysPanelObj : public PanelObj {
public:
    CharsysPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "CharsysPanel") {}

    std::string fileName() override { return ""; }
    bool drawsSomething() const override { return true; }

    void load() override {
        PanelObj::load();
        const Value n = getProp(ui.interp(), "num_placers");
        slots_.resize(size_t(std::clamp(n.isNumber() ? n.integerValue() : 1, 1, 4)));
    }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        const int n = intArg(args, 0);
        Slot* s = n >= 0 && n < int(slots_.size()) ? &slots_[size_t(n)] : nullptr;
        if (msg == "show_char") {
            const std::string outfit = strArg(args, 1);
            if (!s || outfit == s->outfit) return true;
            s->outfit = outfit;
            s->ch = load(outfit);
            if (s->ch) s->ch->ch->setXfm(placerXfm(*s));
            return r = Value::integer(1), true;
        }
        if (msg == "char_event") {
            if (s) s->event = strArg(args, 1);
            return true;
        }
        if (msg == "get_char") return r = Value::obj(s ? s->ch : nullptr), true;
        if (msg == "is_char_loaded") return r = Value::integer(s && (s->outfit.empty() || s->ch) ? 1 : 0), true;
        if (msg == "are_chars_loaded") {
            bool all = true;
            for (const Slot& x : slots_) all = all && (x.outfit.empty() || x.ch);
            return r = Value::integer(all ? 1 : 0), true;
        }
        if (msg == "set_placer" || msg == "set_door" || msg == "set_env") {
            if (!s) return true;
            std::string obj;
            gfx::UiView* v = args.size() > 1 ? holderView(ui, args[1], obj) : nullptr;
            if (msg == "set_placer") {
                // The store's (store TRUE): drawn in the placer's panel at the
                // placer's place (char_store.placer in store.milo), as the
                // guitar's proxy [I].
                if (s->placerView) s->placerView->objectDraws.erase(s->placer);
                s->placer = obj, s->placerView = v, s->hooked = false;
                if (v && !obj.empty() && getProp(ui.interp(), "store").truthy()) {
                    const size_t slot = size_t(n);
                    v->objectDraws[obj] = [this, slot](gfx::MaterialRenderer&) { drawSlot(slots_[slot], true); };
                    s->hooked = true;
                }
            }
            else if (msg == "set_door") s->door = obj, s->doorView = v;
            else s->env = obj, s->envView = v;
            return true;
        }
        if (msg == "set_exclusions") return true;  // which outfits the cache keeps loaded [I: no memory limit here]
        return PanelObj::handleNative(in, msg, args, r);
    }

    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        if (paused) return;
        const float dt = float(std::clamp(dtMs, 0.0, 100.0) / 1000.0);
        clock_ += dt;
        for (Slot& s : slots_) {
            // Poll 0x8228F250 places, wakes (the pending event, 0x8228D348)
            // and polls a loaded character only while its slot has a placer
            // (slot +0xc, set_placer 0x8228E6F8) [C]: the character select
            // sets it at TRANSITION_COMPLETE, so ui_enter's kick starts once
            // the screen is up, behind the closed door (GD03-078).
            if (!s.ch || s.placer.empty()) continue;
            if (!s.event.empty()) {
                // 0x8228D348: the pending event goes to the loaded character.
                const std::string e = s.event;
                s.event.clear();
                ui.interp().send(s.ch, e, {}, false);
            }
            chr::RigContext ctx;
            // The menus' clock at 120 beats a minute [I].
            ctx.beat = clock_ * 2.0, ctx.dBeats = dt * 2.0f, ctx.dSeconds = dt, ctx.seconds = clock_;
            s.ch->ch->setXfm(placerXfm(s));
            s.ch->ch->poll(ctx);
        }
        for (Slot& s : slots_) {
            // Poll 0x8228F250: the door stands at (pi / 2, 0, 0); a loaded,
            // placed character swings it with its bone_door (its local
            // rotation's z) [C].
            if (s.doorView && !s.door.empty()) {
                float z = 0;
                if (s.ch && !s.placer.empty()) {
                    chr::Skeleton& sk = s.ch->ch->skeleton();
                    int b = sk.find("bone_door.trans");
                    if (b < 0) b = sk.find("bone_door.mesh");
                    if (b >= 0) {
                        const auto& m = sk.local(b).rot;
                        z = std::abs(m[5]) <= 0.9999999f ? std::atan2(-m[3], m[4]) : std::atan2(m[1], m[0]);  // 0x82269700
                    }
                }
                s.doorView->setLocalEuler(s.door, 1.5707964f, 0, z);
            }
        }
    }

    void draw() override {
        if (!showing || ui.headless() || !ui.sceneCache()) return;
        for (Slot& s : slots_)
            if (!s.hooked) drawSlot(s, false);
    }

private:
    struct Slot {
        std::string outfit, event, placer, door, env;
        UiCharObj* ch = nullptr;
        gfx::UiView* placerView = nullptr;
        gfx::UiView* envView = nullptr;
        gfx::UiView* doorView = nullptr;
        bool hooked = false;  // drawn by the placer's panel (the store)
    };

    void drawSlot(Slot& s, bool inOwner) {
        if (!showing || !up || ui.headless() || !ui.sceneCache()) return;
        if (!s.ch || !s.placerView || s.placer.empty() || !s.placerView->find(s.placer)) return;
        if (!view_) view_ = std::make_unique<BandView>(*ui.sceneCache());
        gfx::Renderer& r = ui.canvas().renderer();
        ui.canvas().flush();
        int vx, vy, vw, vh;
        r.pictureRect(vx, vy, vw, vh);
        // The placer's panel camera; the depth its room left is kept, so
        // the door frame and the room hide the character as on the 360 [I].
        gfx::UiView& v = *s.placerView;
        r.materials().setFrame(v.camera(), s.envView ? s.envView->environment(s.env) : v.environment(), vw, vh);
        view_->drawCharacter(r.materials(), *s.ch->ch, v.camera(), false);
        if (inOwner) r.materials().setFrame(v.camera(), v.environment(), vw, vh);
        else ui.keepDepthOnce();
    }

    scene::Xfm placerXfm(const Slot& s) const {
        scene::Xfm x;
        if (s.placerView) s.placerView->worldXfm(s.placer, x);
        return x;
    }

    UiCharObj* load(const std::string& outfit) {
        if (outfit.empty()) return nullptr;
        if (auto it = cache_.find(outfit); it != cache_.end()) return it->second.get();
        UiCharObj* out = nullptr;
        try {
            if (!clips_) clips_ = std::make_unique<chr::ClipLibrary>(ui.data());
            const std::string path = scene::findMiloRelative(ui.data(), "char/x/x.milo_ps2", "../" + outfit + "/og/" + outfit + "_ui.milo");
            auto obj = std::make_unique<UiCharObj>(outfit, std::make_unique<chr::Character>(ui.data(), path, *clips_));
            // Its script: char_objects.dta's (Character (types (ui ...))).
            if (const DataNode* objects = ui.bootTree() ? ui.bootTree()->find("objects") : nullptr)
                if (const DataNode* c = objects->find("Character"))
                    if (const DataNode* types = c->find("types"))
                        if (const DataNode* t = types->find("ui")) obj->addHandlers(*t, 1);
            out = obj.get();
            cache_[outfit] = std::move(obj);
        } catch (const std::exception& e) {
            SDL_Log("%s: %s: %s", name().c_str(), outfit.c_str(), e.what());
            cache_[outfit] = nullptr;
        }
        return out;
    }

    std::vector<Slot> slots_;
    std::map<std::string, std::unique_ptr<UiCharObj>> cache_;
    std::unique_ptr<chr::ClipLibrary> clips_;
    std::unique_ptr<BandView> view_;
    double clock_ = 0;
};

// GuitarSelectPanel (0x82293B10): per player the guitar and skin being
// picked (Enter 0x822934B8: the player's own), skin select and done flags.
// Up / down step through the guitars (or the skins), wrapping, and send
// update_display when the pick changes; X sends guitar_selected (0x82293830).
class GuitarSelectPanelObj : public PanelObj {
public:
    GuitarSelectPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "GuitarSelectPanel") {}

    void enter() override {
        // (quickplay FALSE) / (quickplay TRUE TRUE): one entry per player.
        const Value q = getProp(ui.interp(), "quickplay");
        const int n = q.type == Value::Type::Array && q.array ? int(q.array->size()) : 1;
        picks_.assign(size_t(std::clamp(n, 1, 2)), {});
        for (size_t p = 0; p < picks_.size(); ++p) {
            Session::PlayerCfg& cfg = ui.session().players[p];
            validate(ui, cfg);
            picks_[p].guitar = cfg.guitar, picks_[p].skin = cfg.skin;
        }
        PanelObj::enter();
    }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        const int p = std::clamp(intArg(args, 0), 0, 1);
        Pick* k = p < int(picks_.size()) ? &picks_[size_t(p)] : nullptr;
        if (msg == "get_selected_guitar") return r = Value::symbol(k ? k->guitar : std::string()), true;
        if (msg == "get_selected_skin") return r = Value::symbol(k ? k->skin : std::string()), true;
        if (msg == "get_num_guitars") return r = Value::integer(int(guitars(ui, ui.session().players[size_t(p)]).size())), true;
        if (msg == "get_num_skins") return r = Value::integer(int(skins(ui, strArg(args, 1)).size())), true;
        if (msg == "is_skin_select") return r = Value::integer(k && k->skinSelect ? 1 : 0), true;
        if (msg == "set_skin_select") return k && (k->skinSelect = args.size() > 1 && args[1].truthy()), true;
        if (msg == "is_select_done") return r = Value::integer(k && k->done ? 1 : 0), true;
        if (msg == "set_select_done") return k && (k->done = args.size() > 1 && args[1].truthy()), true;
        if (msg == "get_instrument_type") return r = Value::symbol(ui.session().players[size_t(p)].trackType == kTrackBass ? "bass" : "guitar"), true;
        return PanelObj::handleNative(in, msg, args, r);
    }

    bool buttonDown(int button, int player) override {
        const size_t p = picks_.size() == 1 ? 0 : size_t(std::clamp(player, 0, 1));
        if (p >= picks_.size() || picks_[p].done) return false;
        if (button == kPad_X) {
            ui.route("guitar_selected", {Value::integer(int(p))});
            return true;
        }
        if (button != kPad_DUp && button != kPad_DDown) return false;
        Pick& k = picks_[p];
        const Session::PlayerCfg& cfg = ui.session().players[p];
        const std::vector<std::string> list = k.skinSelect ? skins(ui, k.guitar) : guitars(ui, cfg);
        const int n = int(list.size());
        const int old = indexIn(list, k.skinSelect ? k.skin : k.guitar);
        int i = old + (button == kPad_DDown ? 1 : -1);
        if (i < 0) i = n - 1;
        else if (i >= n) i = 0;
        if (n == 0 || i == old) return true;
        if (k.skinSelect) {
            k.skin = list[size_t(i)];
        } else {
            // The new guitar keeps the skin's place when it has it, else its first.
            const int si = std::max(0, indexIn(skins(ui, list[size_t(i)]), k.skin));
            k.guitar = list[size_t(i)];
            const std::vector<std::string> ss = skins(ui, k.guitar);
            k.skin = ss.empty() ? std::string() : ss[size_t(std::min(si, int(ss.size()) - 1))];
        }
        ui.route("update_display", {Value::integer(1)});
        return true;
    }

private:
    struct Pick {
        bool skinSelect = false, done = false;
        std::string guitar, skin;
    };
    std::vector<Pick> picks_;
};

std::vector<const scene::Mesh*> displayMeshesOf(const scene::Directory& d) {
    std::unique_ptr<scene::CharacterObj> own;
    try {
        if (d.self) own = scene::decodeCharacterDir(*d.self);
    } catch (const std::exception&) {
        own.reset();
    }
    const scene::Group* lod0 = nullptr;
    const scene::Group* shadow = nullptr;
    if (own && !own->lods.empty())
        if (const scene::Object* g = d.get(own->lods[0].group)) lod0 = g->as<scene::Group>();
    if (own && !own->shadow.empty())
        if (const scene::Object* g = d.get(own->shadow)) shadow = g->as<scene::Group>();
    // The other LODs' groups hide (the Character shows only its LOD's).
    std::set<const scene::Object*> skip;
    if (own)
        for (size_t k = 1; k < own->lods.size(); ++k)
            if (const scene::Object* g = d.get(own->lods[k].group)) skip.insert(g);
    std::set<const scene::Object*> children;
    for (const auto& o : d.objects)
        if (auto* g = o->as<scene::Group>())
            for (const std::string& n : g->objects)
                if (const scene::Object* x = d.get(n); x && x != g) children.insert(x);
    std::vector<const scene::Object*> roots;
    for (const auto& o : d.objects) {
        if (!o->decoded || children.count(o.get()) || skip.count(o.get()) || o.get() == shadow) continue;
        if (o->as<scene::Mesh>() || o->as<scene::Group>()) roots.push_back(o.get());
    }
    if (lod0 && std::find(roots.begin(), roots.end(), lod0) == roots.end()) roots.push_back(lod0);
    std::stable_sort(roots.begin(), roots.end(), [](const scene::Object* a, const scene::Object* b) {
        const scene::Draw* da = gfx::drawOf(a);
        const scene::Draw* db = gfx::drawOf(b);
        return (da ? da->order : 0) < (db ? db->order : 0);
    });
    std::vector<const scene::Mesh*> out;
    std::set<const scene::Object*> seen;
    std::function<void(const scene::Object*, int)> add = [&](const scene::Object* o, int depth) {
        if (!o || depth > 16 || o == shadow || skip.count(o) || !seen.insert(o).second) return;
        if (auto* m = o->as<scene::Mesh>()) {
            // A mesh's own showing; a LOD group's showing is the Character's to set.
            if (m->draw.showing) out.push_back(m);
        } else if (auto* g = o->as<scene::Group>()) {
            if (g != lod0 && !g->draw.showing) return;
            for (const std::string& n : g->objects) add(d.get(n), depth + 1);
        }
    };
    for (const scene::Object* o : roots) add(o, 0);
    return out;
}

// GuitarDisplayPanel (0x822922B8): show_guitar loads the skin's outfit
// (char/og/guitars/<outfit>.milo, 0x82291EE0) into the screen's proxy; its
// poll (0x82291FF8) puts the skin's mat on guitar.mesh (0x822B2148) and
// turns the filter (guitar_single.filt) on at 30 frames a second, wrapping
// after 240 frames.
class GuitarDisplayPanelObj : public PanelObj {
public:
    GuitarDisplayPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "GuitarDisplayPanel") {}

    struct Show {
        std::string outfit, mat, proxy, filter, env;
        gfx::UiView* view = nullptr;
        gfx::UiView* envView = nullptr;
        scene::Milo* milo = nullptr;
        float frame = 0;
        bool hooked = false;  // drawn by the proxy's panel (the store)
    };

    std::string fileName() override { return ""; }
    bool drawsSomething() const override { return true; }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        const size_t p = size_t(std::clamp(intArg(args, 0), 0, 1));
        if (msg == "show_guitar") {
            Show& s = shows_[p];
            const Guitar* g = guitar(ui, strArg(args, 1));
            const std::string skin = strArg(args, 2);
            // 0x82292148: the named skin, else the guitar's first.
            const Skin* sk = g && !g->skins.empty() ? &g->skins[0] : nullptr;
            if (g)
                for (const Skin& x : g->skins)
                    if (x.name == skin) sk = &x;
            std::string filt;
            if (s.hooked && s.view) s.view->objectDraws.erase(s.proxy), s.hooked = false;
            s.view = args.size() > 3 ? holderView(ui, args[3], s.proxy) : nullptr;
            if (s.view && !s.proxy.empty() && getProp(ui.interp(), "config").str() == "store") {
                s.view->objectDraws[s.proxy] = [this, p](gfx::MaterialRenderer&) { drawShow(shows_[p], true); };
                s.hooked = true;
            }
            if (args.size() > 4) holderView(ui, args[4], filt);
            if (!sk) return true;
            if (sk->outfit != s.outfit || filt != s.filter) s.frame = 0;  // a new guitar starts the turn again
            s.filter = filt;
            s.mat = sk->mat;
            s.outfit = sk->outfit;
            s.milo = nullptr;
            try {
                s.milo = &ui.library().load(scene::findMiloRelative(ui.data(), "char/x.milo_ps2", "og/guitars/" + sk->outfit + ".milo"));
            } catch (const std::exception& e) {
                SDL_Log("%s: %s: %s", name().c_str(), sk->outfit.c_str(), e.what());
            }
            return true;
        }
        if (msg == "set_env") {
            shows_[p].envView = args.size() > 1 ? holderView(ui, args[1], shows_[p].env) : nullptr;
            return true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }

    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        if (paused) return;
        for (Show& s : shows_) {
            if (!s.milo || !s.view || s.filter.empty()) continue;
            s.frame += float(std::clamp(dtMs, 0.0, 100.0) / 1000.0) * 30.0f;
            if (s.frame > 240.0f) s.frame -= 240.0f;
            s.view->setFrame(s.filter, s.frame);
        }
    }

    void draw() override {
        if (!showing || ui.headless() || !ui.sceneCache()) return;
        for (Show& s : shows_)
            if (!s.hooked) drawShow(s, false);
    }

    // One guitar on its proxy: in this panel's own slot of the screen, or
    // (the store's, `(config store)`) in the proxy's owner panel at the
    // proxy's place in its draw order, as GH2's UIProxy draws its directory
    // [I: the port hooks the store's only, whose panel comes before store_panel].
    void drawShow(Show& s, bool inOwner) {
        if (!showing || !up || ui.headless() || !ui.sceneCache()) return;
        {
            scene::Xfm at;
            if (!s.milo || !s.view || !s.view->worldXfm(s.proxy, at)) return;
            gfx::Renderer& r = ui.canvas().renderer();
            ui.canvas().flush();
            int vx, vy, vw, vh;
            r.pictureRect(vx, vy, vw, vh);
            r.materials().setFrame(s.view->camera(), s.envView ? s.envView->environment(s.env) : s.view->environment(), vw, vh);
            const scene::Directory& d = s.milo->root();
            const scene::Mat* skinMat = s.mat.empty() || !d.get(s.mat) ? nullptr : d.get(s.mat)->as<scene::Mat>();
            gfx::ScenePose pose;
            // UIProxy (main_trans "guitar.mesh", ui_objects.dta): the
            // guitar's main transform stands on the proxy, the rest keeps
            // its place relative to it [I: the main trans' own local dropped].
            const scene::Object* mainTrans = d.get("guitar.mesh");
            const scene::Xfm toMain = mainTrans ? chr::inverse(pose.world(mainTrans)) : scene::Xfm{};
            // The outfit as a Character at its first LOD, in draw order (displayMeshes).
            for (const scene::Mesh* m : displayMeshesOf(d)) {
                const scene::Object* mo = m->mat.empty() ? nullptr : d.get(m->mat);
                const scene::Mat* mat = m->name == "guitar.mesh" && skinMat ? skinMat : mo ? mo->as<scene::Mat>() : nullptr;
                gfx::drawMesh(r.materials(), *ui.sceneCache(), *m, gfx::toMat4(scene::compose(scene::compose(pose.world(m), toMain), at)), &pose, mat);
            }
            if (inOwner) r.materials().setFrame(s.view->camera(), s.view->environment(), vw, vh);  // the owner's frame again
            else ui.keepDepthOnce();
        }
    }

private:
    std::array<Show, 2> shows_;
};

}  // namespace

std::vector<const scene::Mesh*> displayMeshes(const scene::Directory& outfit) { return displayMeshesOf(outfit); }

std::unique_ptr<ScriptObject> makeClass(UiManager& ui, const std::string& cls, const std::string& name) {
    if (cls == "CharsysPanel") return std::make_unique<CharsysPanelObj>(ui, name);
    if (cls == "GuitarSelectPanel") return std::make_unique<GuitarSelectPanelObj>(ui, name);
    if (cls == "GuitarDisplayPanel") return std::make_unique<GuitarDisplayPanelObj>(ui, name);
    return nullptr;
}

void installNatives(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned) {
    auto cp = std::make_unique<CharacterProviderObj>(ui);
    in.registerObject(cp.get());
    owned.push_back(std::move(cp));
    // {reset_hair $char}: the character's hair back to rest.
    in.addBuiltin("reset_hair", [](Interp& i, const DataNode& cmd) {
        const std::vector<Value> a = i.evalArgs(cmd);
        if (auto* c = a.empty() ? nullptr : dynamic_cast<UiCharObj*>(a[0].object)) c->ch->rig().resetHair();
        return Value::integer(0);
    });
}

}  // namespace ui::chars
