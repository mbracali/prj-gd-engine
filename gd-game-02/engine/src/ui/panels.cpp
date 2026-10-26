// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The native panel classes of GH2's front end (MetaPanel, HelpBarPanel,
// SelSongPanel, SliderPanel, EndGamePanel, FadePanel, LagPanel,
// StatsProvider...) and the `new` class registry.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "render/canvas.h"
#include "scene/milo.h"
#include "scene/ui_objects.h"
#include "scene/ui_styles.h"
#include "script/tasks.h"
#include "ui/char_select.h"
#include "ui/gamepad_panel.h"
#include "ui/natives.h"
#include "ui/ui_host.h"
#include "ui/ui_objects.h"

namespace ui {

namespace {

// MetaPanel `meta`: the menus' frame (no milo of its own), play_movie and
// is_any_button.
class MetaPanelObj : public PanelObj {
public:
    MetaPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "MetaPanel") {}
    void enter() override {
        PanelObj::enter();
        // MetaPanel Load / Enter with autostart: the menus' music loop [I: gd01's FUN_0020dcd8].
        ui.host().menuMusic(true);
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "play_movie") {
            // MetaPanel::Handle 0x822A1850 -> 0x822A1630 (videos/%s) [C]: modal.
            ui.host().playMovie(args.empty() ? std::string() : args[0].str());
            return true;
        }
        if (msg == "set_tool_movie") return true;
        if (msg == "is_any_button") {
            // Any button but the sticks' directions (16..23) [C].
            const int b = args.empty() ? -1 : args[0].integerValue();
            return r = Value::integer(b >= 0 && b < 16 ? 1 : 0), true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }
};

// HelpBarPanel `helpbar` (helpbar.milo, 0x8229A8C0): the buttons and their
// labels along the bottom; fret icons from help_bar_starting.mesh going
// right (button_spacing, then the label and text_spacing), the strum icon
// from help_bar_strumbar.mesh (strumbar_spacing).
class HelpBarPanelObj : public PanelObj {
public:
    HelpBarPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "HelpBarPanel") {}

    void load() override {
        PanelObj::load();
        if (!view) return;
        for (const char* t : {"help_bar.mesh", "help_bar_strum.mesh", "help_bar_starting.mesh", "help_bar_strumbar.mesh", "help_bar.txt"})
            view->setShowing(t, false);
        view->overlay = [this](gfx::MaterialRenderer& r) { drawItems(r); };
    }

    void setDisplay(const Value& v) {
        items_.clear();
        if (v.type != Value::Type::Array || !v.array) return;
        for (const Value& e : *v.array)
            if (e.type == Value::Type::Array && e.array && e.array->size() >= 2) items_.push_back({(*e.array)[0].str(), (*e.array)[1].str()});
    }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "set_display") {
            setDisplay(args.empty() ? Value::list() : args[0]);
            r = Value::integer(0);
            return true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }

private:
    float prop(const char* key, float def) {
        const Value v = getProp(ui.interp(), key);
        return v.isNumber() ? v.number() : def;
    }

    void drawItems(gfx::MaterialRenderer& r) {
        if (!view || items_.empty()) return;
        const auto* start = view->find("help_bar_starting.mesh");
        const auto* strumStart = view->find("help_bar_strumbar.mesh");
        const auto* icon = view->find("help_bar.mesh") ? view->find("help_bar.mesh")->as<scene::Mesh>() : nullptr;
        const auto* strumIcon = view->find("help_bar_strum.mesh") ? view->find("help_bar_strum.mesh")->as<scene::Mesh>() : nullptr;
        const auto* text = view->find("help_bar.txt") ? view->find("help_bar.txt")->as<scene::Text>() : nullptr;
        if (!start || !icon || !text) return;
        const float buttonSpacing = prop("button_spacing", 35), textSpacing = prop("text_spacing", 30), strumSpacing = prop("strumbar_spacing", 70);
        scene::Xfm pen = view->pose().world(start);
        scene::Xfm strumPen = strumStart ? view->pose().world(strumStart) : pen;
        const scene::Xfm textWorld = view->pose().world(text);
        int k = 0;
        for (const auto& [kind, token] : items_) {
            scene::Xfm& p = kind == "strum" ? strumPen : pen;
            const bool button = kind == "fret1" || kind == "fret2" || kind == "fret3" || kind == "start";
            if (button || kind == "strum") {
                const scene::Mesh* m = kind == "strum" && strumIcon ? strumIcon : icon;
                const scene::Mat* mat = nullptr;
                if (m->dir) mat = m->dir->resolve<scene::Mat>("hb_" + kind + ".mat");
                scene::Xfm at = p;
                view->drawMeshAt(r, *m, mat, at);
                p.pos[0] += kind == "strum" ? strumSpacing : buttonSpacing;
            }
            // The label: the template's orientation at the pen.
            scene::Xfm at = textWorld;
            at.pos = p.pos;
            const float w = view->drawTemplate(r, "hb" + std::to_string(k++), *text, ui.localize(token), at);
            p.pos[0] += w + textSpacing;
        }
    }

    std::vector<std::pair<std::string, std::string>> items_;
};

// SelSongPanel (sel_song_panel): the song list's panel.
class SelSongPanelObj : public PanelObj {
public:
    SelSongPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "SelSongPanel") {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "cache_song" || msg == "clear_cache") return r = Value::integer(0), true;
        return PanelObj::handleNative(in, msg, args, r);
    }
};

// SliderPanel: slider_selected (a slider is being changed).
class SliderPanelObj : public PanelObj {
public:
    SliderPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "SliderPanel") {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "slider_selected") {
            bool any = false;
            for (const auto& [n, c] : components_) any = any || (c->isSlider() && c->sliderSelected);
            r = Value::integer(any ? 1 : 0);
            return true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }
};

// EndGamePanel: the newspaper's headline (generate_headline 0x82291428).
class EndGamePanelObj : public PanelObj {
public:
    EndGamePanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "EndGamePanel") {}
    void enter() override {
        PanelObj::enter();
        // The newspaper photo: the picture GamePanel picked as the song
        // loaded (0x822CC730) becomes winner_photo<i>.tex (0x822CC9B0).
        const auto& photos = ui.session().winnerPhotos;
        if (view)
            for (size_t i = 0; i < photos.size(); ++i) view->setTexFile("winner_photo" + std::to_string(i) + ".tex", photos[i]);
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "generate_headline") {
            const std::string token = args.empty() ? "" : args[0].str();
            r = Value::string(generateHeadline(ui, token, std::vector<Value>(args.begin() + (args.empty() ? 0 : 1), args.end())));
            return true;
        }
        if (msg == "get_versus_spread") {
            // 0 close, 1 clear, 2 crushing [I: thresholds].
            const float a = args.size() > 0 ? args[0].number() : 0, b = args.size() > 1 ? args[1].number() : 0;
            const float hi = std::max(a, b), lo = std::min(a, b);
            const float f = hi > 0 ? (hi - lo) / hi : 0;
            r = Value::integer(f < 0.1f ? 0 : f < 0.3f ? 1 : 2);
            return true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }
};

// FadePanel (fade_panel, fade.milo): start_fade fades the picture to black.
class FadePanelObj : public PanelObj {
public:
    FadePanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "FadePanel") {}
    void enter() override {
        fadeStart_ = -1;
        PanelObj::enter();
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "start_fade") {
            fadeStart_ = ui.nowMs();
            fadeLen_ = std::max(0.01f, args.empty() ? 1.0f : args[0].number()) * 1000.0f;
            r = Value::integer(0);
            return true;
        }
        if (msg == "fade_synth") return r = Value::integer(0), true;
        return PanelObj::handleNative(in, msg, args, r);
    }
    void draw() override {
        if (fadeStart_ >= 0) {
            const float a = float(std::clamp((ui.nowMs() - fadeStart_) / fadeLen_, 0.0, 1.0));
            ui.canvas().fillRect(0, 0, kScreenW, kScreenH, {0, 0, 0, Uint8(a * 255)});
            ui.canvas().flush();
        }
        PanelObj::draw();
    }

private:
    double fadeStart_ = -1, fadeLen_ = 1000;
};

// LagPanel (lag.milo): calculate_lag from the strums against the clicks.
class LagPanelObj : public PanelObj {
public:
    LagPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "LagPanel") {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "calculate_lag") {
            // ($window $min_hits $hits $seconds): per click the nearest strum;
            // the mean offset (ms) of those within the window of the median [I].
            const float window = args.size() > 0 ? args[0].number() : 50;
            const int minHits = args.size() > 1 ? args[1].integerValue() : 7;
            std::vector<double> hits, clicks, d;
            if (args.size() > 2 && args[2].array)
                for (const Value& v : *args[2].array) hits.push_back(v.number());
            if (args.size() > 3 && args[3].array)
                for (const Value& v : *args[3].array) clicks.push_back(v.number());
            for (double c : clicks) {
                double best = 1e9;
                for (double h : hits)
                    if (std::fabs(h - c) < std::fabs(best)) best = h - c;
                if (std::fabs(best) < 0.4) d.push_back(best * 1000);
            }
            float lag = 1e6f;
            if (!d.empty()) {
                std::vector<double> s = d;
                std::sort(s.begin(), s.end());
                const double med = s[s.size() / 2];
                double sum = 0;
                int n = 0;
                for (double x : d)
                    if (std::fabs(x - med) <= window) sum += x, ++n;
                if (n >= minHits) lag = float(sum / n);
            }
            r = Value::real(lag);
            return true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }
};

// StatsProvider: the endgame stats' practice sections list (none in
// quickplay).
class StatsProviderObj : public ListProvider {
public:
    using ListProvider::ListProvider;
    int numData() override { return 0; }
    std::string symbol(int) override { return {}; }
    void rows(std::vector<gfx::UiView::ListRow>& out, int, int& sel) override { out.clear(), sel = 0; }
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>&, Value& r) override {
        if (msg == "num_data") return r = Value::integer(0), true;
        return false;
    }
};

// CreditsPanel (credits.milo, 0x8229BDF8 / 0x8229BE88): config/credits.dta
// scrolls up through credits.lst, a row every scroll_time (1 s), over the
// credits music. Rows: ("A") center.txt, ("A" "B") title.txt + name.txt,
// ("A" "B" "C") centername.txt (0x8229B590) [C].
class CreditsPanelObj : public PanelObj {
public:
    CreditsPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "CreditsPanel") {}
    void load() override {
        PanelObj::load();
        lines_.clear();
        try {
            const DataNode root = scene::loadPlainDta(ui.data(), "config/gen/credits.dtb");
            for (const DataNode& l : root.children) {
                std::vector<std::string> v;
                for (const DataNode& c : l.children) v.push_back(c.isText() ? c.s : std::string());
                lines_.push_back(v);
            }
        } catch (const std::exception& e) {
            ui.logOnce(std::string("credits: ") + e.what());
        }
    }
    void enter() override {
        PanelObj::enter();
        pos_ = 0;
        ui.host().creditsMusic(true);
        if (ComponentObj* l = component("credits.lst")) l->selected = 0;
        update();
    }
    void exit() override {
        ui.host().creditsMusic(false);
        PanelObj::exit();
    }
    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        if (!up) return;
        const int before = int(pos_);
        pos_ = std::min(pos_ + float(dtMs / 1000.0), float(std::max<size_t>(1, lines_.size()) - 1));
        if (ComponentObj* l = component("credits.lst"); l && int(pos_) != before) {
            l->selected = int(pos_);
            ui.route("component_scroll", {Value::obj(l)});
        }
        update();
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "num_lines") return r = Value::integer(int(lines_.size())), true;
        return PanelObj::handleNative(in, msg, args, r);
    }

private:
    void update() {
        if (!view) return;
        std::vector<gfx::UiView::ListRow> rows;
        const int first = int(pos_);
        for (int i = first; i < int(lines_.size()) && i < first + 17; ++i) {
            gfx::UiView::ListRow r;
            r.slot = float(i) - pos_;
            const auto& l = lines_[size_t(i)];
            if (l.size() == 1) r.texts["center.txt"] = l[0];
            if (l.size() == 2) r.texts["title.txt"] = l[0], r.texts["name.txt"] = l[1];
            if (l.size() == 3) r.texts["centername.txt"] = l[1];
            rows.push_back(std::move(r));
        }
        view->setList("credits.lst", std::move(rows), -1);
    }
    std::vector<std::vector<std::string>> lines_;
    float pos_ = 0;
};

// Screens: GHScreen and its relatives.
class GenericScreen : public ScreenObj {
public:
    using ScreenObj::ScreenObj;
};

}  // namespace

void setHelpbarDisplay(UiManager& ui, const Value& display) {
    if (auto* h = dynamic_cast<HelpBarPanelObj*>(ui.panel("helpbar"))) h->setDisplay(display);
}

std::string generateHeadline(UiManager& ui, const std::string& token, const std::vector<Value>& slots) {
    std::string s = ui.localize(token);
    const auto slot = [&](const std::string& key) -> std::string {
        for (const Value& v : slots)
            if (v.type == Value::Type::Array && v.array && v.array->size() >= 2 && (*v.array)[0].str() == key)
                return ui.interp().evalValue((*v.array)[1]).str();  // ({localize {random_elem ...}}) evaluated here
        return {};
    };
    const std::pair<const char*, std::string> keys[] = {
        {"{GH_ADJ}", slot("adj")}, {"{GH_NOUN}", slot("noun")}, {"{GH_VERB}", slot("verb")}, {"{GH_VERB_PAST}", slot("verb_past")},
        {"{GH_WIN_PHRASE}", slot("win_phrase")}, {"{GH_NUM}", slot("num")}, {"{GH_VS_VERB}", slot("vs_verb")},
        {"{GH_VS_NOUN}", slot("vs_noun")}, {"{GH_VENUE}", ui.localize(ui.session().venue)},
        {"{GH_BAND}", ui.career().profiles[0].inUse ? ui.career().profiles[0].name : std::string()}};
    for (const auto& [k, v] : keys)
        for (size_t p = s.find(k); p != std::string::npos; p = s.find(k, p + v.size())) s.replace(p, std::strlen(k), v);
    return s;
}

const std::vector<std::string>& knownClasses() {
    static const std::vector<std::string> k = {
        "GHScreen", "UIScreen", "MultiSelectScreen", "TrackBudgetScreen", "GHPanel", "UIPanel", "MetaPanel", "HelpBarPanel",
        "SelSongPanel", "SliderPanel", "EndGamePanel", "FadePanel", "GamePanel", "HudPanel", "TrackPanel", "TrackMaskPanel",
        "MidiLoaderPanel", "LagPanel", "CreditsPanel", "StatsProvider", "MultiSelectPanel", "GuitarDisplayPanel", "CharsysPanel",
        "GuitarSelectPanel", "StorePanel", "MultiCharSelPanel", "MultiSelectListPanel", "TutorialPanel", "GamepadPanel"};
    return k;
}

std::unique_ptr<ScriptObject> makeClassObject(UiManager& ui, const std::string& cls, const std::string& name) {
    if (cls == "GHScreen" || cls == "UIScreen" || cls == "MultiSelectScreen" || cls == "TrackBudgetScreen")
        return std::make_unique<GenericScreen>(ui, name, cls);
    if (auto g = makeGameClass(ui, cls, name)) return g;
    if (auto c = chars::makeClass(ui, cls, name)) return c;
    if (cls == "MetaPanel") return std::make_unique<MetaPanelObj>(ui, name);
    if (cls == "HelpBarPanel") return std::make_unique<HelpBarPanelObj>(ui, name);
    if (cls == "SelSongPanel") return std::make_unique<SelSongPanelObj>(ui, name);
    if (cls == "SliderPanel") return std::make_unique<SliderPanelObj>(ui, name);
    if (cls == "EndGamePanel") return std::make_unique<EndGamePanelObj>(ui, name);
    if (cls == "FadePanel") return std::make_unique<FadePanelObj>(ui, name);
    if (cls == "LagPanel") return std::make_unique<LagPanelObj>(ui, name);
    if (cls == "CreditsPanel") return std::make_unique<CreditsPanelObj>(ui, name);
    if (cls == "StatsProvider") return std::make_unique<StatsProviderObj>(name, cls);
    if (cls == "GamepadPanel") return makeGamepadPanel(ui, name);
    // GHPanel / UIPanel, and the classes of screens outside quickplay
    // (career, multiplayer, store, tutorials: later milestones) as plain panels.
    if (std::find(knownClasses().begin(), knownClasses().end(), cls) != knownClasses().end())
        return std::make_unique<PanelObj>(ui, name, cls);
    return nullptr;
}

void registerUiClasses(UiManager& ui, Interp& interp) {
    for (const std::string& cls : knownClasses())
        interp.registerClass(cls, [&ui, cls](Interp&, const std::string& name, const DataNode&) {
            auto o = makeClassObject(ui, cls, name);
            if (o) ui.madeObjects().push_back(o.get());
            return o;
        });
}

}  // namespace ui
