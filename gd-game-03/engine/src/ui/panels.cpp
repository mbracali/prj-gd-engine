// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The native panel classes of GH2's front end (MetaPanel, HelpBarPanel,
// SelSongPanel, SliderPanel, EndGamePanel, FadePanel, LagPanel,
// StatsProvider...) and the `new` class registry.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

#include "render/canvas.h"
#include "scene/milo.h"
#include "scene/ui_objects.h"
#include "scene/ui_styles.h"
#include "script/tasks.h"
#include "ui/char_select.h"
#include "ui/enhanced_panel.h"
#include "ui/gamepad_panel.h"
#include "ui/multi_select.h"
#include "ui/natives.h"
#include "ui/store.h"
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
        // GH2 draws the icon and label templates only as copies at each
        // item (0x8229A630), and the background box (help_box_left / mid /
        // right) only around the items (0x822998D0, 0x82299698) [C]. The
        // port does not draw the boxes yet; left showing, they stood at
        // their rest place in the middle of every screen.
        for (const char* t : {"help_bar.mesh", "help_bar_strum.mesh", "help_bar_starting.mesh", "help_bar_strumbar.mesh", "help_bar.txt",
                              "help_box_left.mesh", "help_box_mid.mesh", "help_box_right.mesh"})
            view->setShowing(t, false);
        view->overlay = [this](gfx::MaterialRenderer& r) { drawItems(r); };
        yellowIcon();
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

    // The items act as their buttons (gd01's help bar): select, back, Start,
    // up / down (the strum bar's upper half up, its lower half down) and the
    // main menu's GAMEPADS; each label's rectangle grown left over its icon.
    bool helpMouse(float x, float y, bool click) override {
        if (!view || !showing) return false;
        for (const gfx::UiView::Hit& h : view->hits()) {
            if (h.key.rfind("hb", 0) != 0) continue;
            const size_t k = size_t(std::atoi(h.key.c_str() + 2));
            if (k >= shown_.size()) continue;
            const std::string& kind = shown_[k].first;
            const float icon = kind == "strum" ? (h.y1 - h.y0) * 3.2f : (h.y1 - h.y0) * 1.8f;
            if (x < h.x0 - icon || x > h.x1 + 6 || y < h.y0 - 8 || y > h.y1 + 8) continue;
            if (!click) return true;
            const int b = kind == "fret1" ? kPad_X : kind == "fret2" ? kPad_Tri : kind == "start" ? kPad_Start
                          : kind == "strum" ? (y < (h.y0 + h.y1) * 0.5f ? kPad_DUp : kPad_DDown) : -1;
            if (kind == "fret3" && ui.currentScreen() == "main_screen") ui.openGamepads();
            else if (b >= 0) ui.buttonDown(b, 0), ui.buttonUp(b, 0);
            return true;
        }
        return false;
    }

private:
    // [I] Port: the Universal Access Buttons (universal_mechanisms.md 1.1.3,
    // gd01's addAccessButtons). Where the screen's own help bar leaves one
    // out, the pills a mouse or touch user needs are added in the game's style and
    // order (fret1, fret2, Start, then the strum bar): SELECT (green) when
    // something can be chosen, BACK (red) when the screen goes back, UP / DOWN
    // when the focus can move, START on a text entry (it accepts the name). A
    // screen whose help bar is empty (progress messages) stays empty.
    std::vector<std::pair<std::string, std::string>> shown() {
        std::vector<std::pair<std::string, std::string>> out = items_;
        ScreenObj* screen = ui.screen(ui.currentScreen());
        if (out.empty() || !screen) return out;
        auto has = [&](const char* e) { return std::any_of(out.begin(), out.end(), [&](const auto& i) { return i.first == e; }); };
        auto strumAt = [&] { return std::find_if(out.begin(), out.end(), [](const auto& e) { return e.first == "strum"; }); };
        PanelObj* fp = ui.focusPanel();
        ComponentObj* focus = fp ? fp->focused() : nullptr;
        const bool entry = focus && focus->isTextEntry();
        const bool navigable = fp && fp->navigable();
        if (!has("fret1") && fp && (focus || !fp->view) && fp->drawsSomething()) out.insert(out.begin(), {"fret1", "help_select"});
        if (!has("fret2") && screen->flag("allow_back", true) && screen->back) {
            auto at = std::find_if(out.begin(), out.end(), [](const auto& e) { return e.first != "fret1"; });
            out.insert(at, {"fret2", "help_back"});
        }
        if (!has("start") && entry) out.insert(strumAt(), {"start", "gd_uab_start"});
        if (!has("strum") && navigable) out.push_back({"strum", "help_updown"});
        return out;
    }

    // GH2's help items use only fret1 (green), fret2 (red) and strum; the
    // disc's hb_fret3.tex is an unused placeholder without alpha (a yellow
    // shape on black with white bars). A fret3 item (none of GH2's screens
    // has one now) gets the green icon recoloured yellow instead, as gd01 does.
    void yellowIcon() {
        scene::Object* green = view->find("hb_fret1.tex");
        scene::Object* yellow = view->find("hb_fret3.tex");
        auto* src = green ? green->as<scene::Tex>() : nullptr;
        auto* dst = yellow ? yellow->as<scene::Tex>() : nullptr;
        std::vector<uint8_t> px;
        if (!src || !dst || !scene::decodeBitmapRgba(src->bitmap, 0, px)) return;
        for (size_t i = 0; i + 3 < px.size(); i += 4) {
            const uint8_t g = px[i + 1], b = px[i + 2];  // green -> yellow
            px[i] = uint8_t(std::min(255.0f, g * 1.4f)), px[i + 1] = uint8_t(std::min(255.0f, g * 1.25f)), px[i + 2] = uint8_t(b * 0.25f);
        }
        scene::Bitmap bmp;
        bmp.format = scene::Bitmap::Format::RGBA8;
        bmp.bpp = 32;
        bmp.width = src->bitmap.levels[0].width, bmp.height = src->bitmap.levels[0].height;
        bmp.levels.push_back({bmp.width, bmp.height, 0, px.size()});
        bmp.data.assign(px.begin(), px.end());
        dst->bitmap = std::move(bmp);
    }

    float prop(const char* key, float def) {
        const Value v = getProp(ui.interp(), key);
        return v.isNumber() ? v.number() : def;
    }

    void drawItems(gfx::MaterialRenderer& r) {
        shown_ = shown();
        if (!view || shown_.empty()) return;
        const auto* start = view->find("help_bar_starting.mesh");
        const auto* strumStart = view->find("help_bar_strumbar.mesh");
        const auto* icon = view->find("help_bar.mesh") ? view->find("help_bar.mesh")->as<scene::Mesh>() : nullptr;
        const auto* strumIcon = view->find("help_bar_strum.mesh") ? view->find("help_bar_strum.mesh")->as<scene::Mesh>() : nullptr;
        const auto* text = view->find("help_bar.txt") ? view->find("help_bar.txt")->as<scene::Text>() : nullptr;
        if (!start || !icon || !text) return;
        const float buttonSpacing = prop("button_spacing", 35), textSpacing = prop("text_spacing", 30), strumSpacing = prop("strumbar_spacing", 70);
        // The items are laid out in the help bar's own space (helpbar.view,
        // parented to the metacam's camera), so they keep together and keep
        // their place on screen while a transition moves the camera. Before,
        // the pen moved along the world's x while the camera turned, and the
        // labels drifted against their icons by a few pixels on every screen
        // change (gd-backlog GD03-075) [I: GH2 positions the items from the
        // start meshes' places, 0x82299CD8 / 0x8229A8C0].
        const gfx::ScenePose& pose = view->pose();
        const auto parentWorld = [&](const scene::Object* o) {
            const scene::Trans* t = gfx::transOf(o);
            const scene::Object* parent = t && !t->parent.empty() && o->dir ? o->dir->resolve(t->parent) : nullptr;
            return parent && parent != o ? pose.world(parent) : scene::Xfm{};
        };
        const scene::Xfm space = parentWorld(start);
        scene::Xfm pen = pose.local(start);
        scene::Xfm strumPen = strumStart ? pose.local(strumStart) : pen;
        const scene::Xfm textLocal = pose.local(text);
        int k = 0;
        for (const auto& [kind, token] : shown_) {
            scene::Xfm& p = kind == "strum" ? strumPen : pen;
            const bool button = kind == "fret1" || kind == "fret2" || kind == "fret3" || kind == "start";
            if (button || kind == "strum") {
                const scene::Mesh* m = kind == "strum" && strumIcon ? strumIcon : icon;
                const scene::Mat* mat = nullptr;
                if (m->dir) mat = m->dir->resolve<scene::Mat>("hb_" + kind + ".mat");
                // GH2 places a Copy of the icon mesh (its own rotation and
                // scale: help_bar_strum.mesh is 2 wide, 64 units) at the pen
                // (0x82299950 / 0x8229A818) [C]; drawn at the pen's own
                // transform, the strum bar was squeezed to half its width.
                scene::Xfm at = pose.local(m);
                at.pos = p.pos;
                view->drawMeshAt(r, *m, mat, scene::compose(at, space));
                p.pos[0] += kind == "strum" ? strumSpacing : buttonSpacing;
            }
            // The label: the template's orientation at the pen.
            scene::Xfm at = textLocal;
            at.pos = p.pos;
            const float w = view->drawTemplate(r, "hb" + std::to_string(k++), *text, ui.localize(token), scene::compose(at, space));
            p.pos[0] += w + textSpacing;
        }
    }

    std::vector<std::pair<std::string, std::string>> items_;
    std::vector<std::pair<std::string, std::string>> shown_;  // with the access buttons, as last drawn
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

// TutorialPanel (`tutorial`, handler 0x0013ff68, enter 0x0013f9f8, poll
// 0x0013fb00) [C]:
// - play_vo <event>: config/sound.dta (tutorial (streaming_vo (volume dB)
//   (dir d) (events (<event> <file>...)))) -> d/<file>.vgs, the event's
//   files in turn (0x00140888); dropped while another plays (0x001407f0);
//   is_vo_playing, reset_vo (stops it).
// - start_tuning_mode <callback> / end_tuning_mode / is_tuning: the tuner
//   (0x0014cf88 .. 0x0014d6d8) reads player 0's guitar each poll: each fret
//   held lights t1_fret<n>.view (frame 1, else 0); a strum with one fret
//   held counts it (0x0014d468): sound s101<fret x 3 + min(count, 3)>
//   (the fret's other two stopped), fret_played <n> to the callback, and
//   at the third the fret is tuned (t1_fretcheck<n>.view shown, play_vo
//   tuning_hit_vo); a strum with two or more is miss_gtr. Every fret
//   counted three times ends the tuning (the frets' views back to 0).
// - is_missing_guitar: the port's controllers always play as a guitar, so
//   never [I: as game is_missing_controller].
// The hopo training (tutorial_33) is not done yet.
class TutorialPanelObj : public PanelObj {
public:
    TutorialPanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "TutorialPanel") {}
    void enter() override {
        PanelObj::enter();
        tuning_ = false, callback_ = nullptr;
        counts_ = {};
        loadVo();
    }
    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        if (paused || !tuning_) return;
        const unsigned frets = ui.host().playerFrets(0);
        const int strums = ui.host().playerStrums(0);
        int held = 0, fret = -1;
        for (int f = 0; f < 5; ++f) {
            const bool on = (frets >> f) & 1u;
            setFrame("t1_fret" + std::to_string(f + 1) + ".view", on ? 1.0f : 0.0f);
            if (on) ++held, fret = f;
        }
        if (strums != lastStrums_) {
            if (held == 1) hit(fret);
            else if (held >= 2) ui.playSfx("miss_gtr");
        }
        lastStrums_ = strums;
        if (std::all_of(counts_.begin(), counts_.end(), [](int c) { return c >= 3; })) {
            for (int f = 0; f < 5; ++f) setFrame("t1_fret" + std::to_string(f + 1) + ".view", 0);
            tuning_ = false;
        }
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "play_vo") return playVo(args.empty() ? std::string() : args[0].str()), true;
        if (msg == "is_vo_playing") return r = Value::integer(ui.host().voicePlaying() ? 1 : 0), true;
        if (msg == "reset_vo") return ui.host().stopVoice(), true;
        if (msg == "start_tuning_mode") {
            if (!tuning_) lastStrums_ = ui.host().playerStrums(0);
            callback_ = !args.empty() && args[0].type == Value::Type::Object ? args[0].object : args.empty() ? nullptr : in.findObject(args[0].str());
            return tuning_ = true, true;
        }
        if (msg == "end_tuning_mode") return tuning_ = false, callback_ = nullptr, true;
        if (msg == "is_tuning") return r = Value::integer(tuning_ ? 1 : 0), true;
        if (msg == "is_missing_guitar") return true;
        return PanelObj::handleNative(in, msg, args, r);
    }

private:
    void loadVo() {
        events_.clear();
        const DataNode* snd = ui.bootTree() ? ui.bootTree()->find("sound") : nullptr;
        const DataNode* t = snd ? snd->find("tutorial") : nullptr;
        const DataNode* vo = t ? t->find("streaming_vo") : nullptr;
        if (!vo) return;
        if (const DataNode* v = vo->find("volume"); v && v->children.size() > 1) volumeDb_ = v->floatAt(1);
        if (const DataNode* d = vo->find("dir"); d && d->children.size() > 1) dir_ = d->textAt(1);
        if (const DataNode* ev = vo->find("events"))
            for (size_t k = 1; k < ev->children.size(); ++k) {
                const DataNode& e = ev->children[k];
                if (!e.isList() || e.children.size() < 2 || !e.children[0].isText()) continue;
                Event& out = events_[e.children[0].s];
                for (size_t i = 1; i < e.children.size(); ++i)
                    if (e.children[i].isText()) out.files.push_back(e.children[i].s);
            }
    }
    void playVo(const std::string& name) {
        if (ui.host().voicePlaying()) return ui.logOnce("tutorial: dropping voice over " + name);
        auto it = events_.find(name);
        if (it == events_.end() || it->second.files.empty()) return ui.logOnce("tutorial: no voice-over " + name);
        Event& e = it->second;
        e.next = (e.next + 1) % int(e.files.size());
        std::string file = e.files[size_t(e.next)];
        std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        ui.host().playVoice(dir_ + "/" + file + ".vgs", volumeDb_);
    }
    void hit(int fret) {
        int& c = counts_[size_t(fret)];
        ++c;
        for (int l = 1; l <= 3; ++l) ui.host().stopSfx(sound(fret, l));
        ui.playSfx(sound(fret, std::min(c, 3)));
        if (callback_) ui.interp().send(callback_, "fret_played", {Value::integer(fret + 1)});
        if (c == 3) {
            if (PanelObj* p = ui.panel("tut_script"); p && p->view) p->view->setShowing("t1_fretcheck" + std::to_string(fret + 1) + ".view", true);
            playVo("tuning_hit_vo");
        }
        c = std::min(c, 3);
    }
    static std::string sound(int fret, int level) {
        char b[16];
        std::snprintf(b, sizeof b, "s101%02d", fret * 3 + level);
        return b;
    }
    void setFrame(const std::string& view, float frame) {
        if (PanelObj* p = ui.panel("tut_script"); p && p->view) p->view->setFrame(view, frame);
    }
    struct Event {
        std::vector<std::string> files;
        int next = -1;
    };
    std::map<std::string, Event> events_;
    std::string dir_;
    float volumeDb_ = 0;
    bool tuning_ = false;
    ScriptObject* callback_ = nullptr;
    std::array<int, 5> counts_{};
    int lastStrums_ = 0;
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
    // [I] Port (universal_mechanisms.md 1.1.1): the original credits only
    // leave with back (red); here green, select, Start (Enter), a click or a
    // tap leave them too, as back does (the next poll, outside the routing).
    bool buttonDown(int button, int player) override {
        if (button == kPad_X || button == kPad_Start || button == kPad_Circle) return leave_ = true;
        return PanelObj::buttonDown(button, player);
    }
    bool mouse(float x, float y, bool click) override {
        (void)x, (void)y;
        if (click) leave_ = true;
        return click;
    }
    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        if (!up) return;
        if (leave_) {
            leave_ = false;
            ui.buttonDown(kPad_Tri, 0);
            ui.buttonUp(kPad_Tri, 0);
            return;
        }
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
    bool leave_ = false;
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
        "GuitarSelectPanel", "StorePanel", "MultiCharSelPanel", "MultiSelectListPanel", "TutorialPanel", "GamepadPanel", "EnhancedPanel"};
    return k;
}

std::unique_ptr<ScriptObject> makeClassObject(UiManager& ui, const std::string& cls, const std::string& name) {
    if (auto m = makeMultiClass(ui, cls, name)) return m;
    if (cls == "GHScreen" || cls == "UIScreen" || cls == "TrackBudgetScreen")
        return std::make_unique<GenericScreen>(ui, name, cls);
    if (auto g = makeGameClass(ui, cls, name)) return g;
    if (auto c = chars::makeClass(ui, cls, name)) return c;
    if (auto c = store::makeClass(ui, cls, name)) return c;
    if (cls == "MetaPanel") return std::make_unique<MetaPanelObj>(ui, name);
    if (cls == "HelpBarPanel") return std::make_unique<HelpBarPanelObj>(ui, name);
    if (cls == "SelSongPanel") return std::make_unique<SelSongPanelObj>(ui, name);
    if (cls == "SliderPanel") return std::make_unique<SliderPanelObj>(ui, name);
    if (cls == "EndGamePanel") return std::make_unique<EndGamePanelObj>(ui, name);
    if (cls == "FadePanel") return std::make_unique<FadePanelObj>(ui, name);
    if (cls == "LagPanel") return std::make_unique<LagPanelObj>(ui, name);
    if (cls == "TutorialPanel") return std::make_unique<TutorialPanelObj>(ui, name);
    if (cls == "CreditsPanel") return std::make_unique<CreditsPanelObj>(ui, name);
    if (cls == "StatsProvider") return std::make_unique<StatsProviderObj>(name, cls);
    if (cls == "GamepadPanel") return makeGamepadPanel(ui, name);
    if (cls == "EnhancedPanel") return makeEnhancedPanel(ui, name);
    // GHPanel / UIPanel, and the classes of screens outside quickplay
    // (multiplayer, tutorials: later milestones) as plain panels.
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
