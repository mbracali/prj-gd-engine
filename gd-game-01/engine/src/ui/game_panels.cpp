// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "render/renderer.h"
#include "render/scene_renderer.h"
#include "ui/panel.h"
#include "ui/select_panels.h"

namespace {

// Frames of a looping anim at `t` ms and 30 fps (frame = t * 0.03), wrapped
// at the anim's last key.
float loopFrame(Panel& p, const std::string& anim, float t) {
    float f = t * 0.03f, end = p.endFrame(anim);
    return end > 0 ? std::fmod(f, end) : f;
}

// An AnimTask entry of type 1 (AnimateTaskFPS, FUN_00136ce8..): frames
// from -> to at 0.03 frames per ms, holding the last frame.
struct AnimTask {
    std::string anim;
    float start = -1, from = 0, to = 0;
    bool running() const { return start >= 0; }
    void play(float t, float a, float b) { start = t, from = a, to = b; }
    void stop(Panel& p) {
        start = -1;
        p.setFrame(anim, 0);
    }
    // Advances; false once finished.
    bool poll(Panel& p, float t) {
        if (start < 0) return false;
        float dur = std::fabs(to - from) / 0.03f;
        float u = dur > 0 ? std::clamp((t - start) / dur, 0.0f, 1.0f) : 1.0f;
        p.setFrame(anim, from + (to - from) * u);
        if (u >= 1) start = -1;
        return start >= 0;
    }
};

// HudPanel (ctor FUN_00131d30, Poll HudPanel_v10 @0x132858; docs/endgame_and_hud.md B1):
// hud/gen/hud.rnd_ps2 driven by the player state. Single player: hud_single
// (score meter 0, star meter 0, the rock meter). Face-off: hud_multi (the
// score shells and star meters 1 / 2, messages 1 / 2, the lead meter); each
// ScoreMeter draws the score digits, bulbs and multiplier into its shell.
class HudPanel : public NativePanel {
public:
    explicit HudPanel(SelectHost& host) : host_(host) {}

    bool handle(const std::string& msg, std::vector<Value>& a, Value& r) override {
        auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
        if (!scene_) return false;
        if (msg == "reset") return reset(), r = Value::integer(0), true;
        if (msg == "set_win_showing") return scene_->setShowing("youwin.mesh", !multi_ && arg(0).truthy()), r = Value::integer(0), true;
        if (msg == "set_in_game") {  // FUN_00132c68
            bool b = arg(0).truthy();
            for (int p = 0; p < players(); ++p) {
                scene_->setShowing(name("score_meter_%d.view", p), b);
                scene_->setShowing(name(multi_ ? "star_meter_group_%d.view" : "star_meter_%d.view", p), b);
                if (!b) scene_->setShowing(name("star_meter_ready_%d.view", p), false);
            }
            scene_->setShowing(multi_ ? "multi_hud_group.view" : "rock_meter.view", b);
            inGame_ = b;
            return r = Value::integer(0), true;
        }
        if (msg == "set_score_showing") {
            for (int p = 0; p < players(); ++p) scene_->setShowing(name("score_meter_%d.view", p), arg(0).truthy());
            return r = Value::integer(0), true;
        }
        if (msg == "show_message") {  // FUN_001333b8: localized text, message<k>.tnm 0 -> 100 at 30 fps
            int p = std::clamp(arg(0).integerValue(), 0, players() - 1);
            Player& pl = p_[size_t(p)];
            scene_->setText(name("message%d.txt", p), host_.localize(arg(1).str()));
            scene_->setShowing(name("message%d.txt", p), true);
            pl.message.anim = name("message%d.tnm", p);
            pl.message.play(t_, 0, 100);
            pl.messageShown = true;
            return r = Value::integer(0), true;
        }
        return false;
    }

    // FinishLoad (HudPanel_v16): the single or multiplayer view, "You Rock!" hidden.
    void entered() override {
        if (!scene_) return;
        multi_ = host_.multiplayer();
        scene_->setShowing("hud_single.view", !multi_);
        scene_->setShowing("hud_multi.view", multi_);
        if (!loaded_) {
            loaded_ = true;
            scene_->setShowing("youwin.mesh", false);
            shake_.anim = "meter_shake.tnm";
            scoreShake_.anim = "score_shake.tnm";
            // B47 [I]: the view draws score_mult_glow (SrcAlphaAdd, x2 colour)
            // over the frame, which saturates the x2..x4 box to white; drawn
            // first, the opaque frame covers it and it shows as the coloured
            // halo around the box.
            if (SceneObject* v = scene_->scene().find("score_meter_0.view"); v && v->drawable()) {
                auto& d = v->drawable()->draws;
                auto g = std::find(d.begin(), d.end(), "score_mult_glow.mesh");
                auto f = std::find(d.begin(), d.end(), "score_mult_frame.mesh");
                if (g != d.end() && f != d.end() && g > f) std::rotate(f, g, g + 1);
            }
            if (const SceneObject* v = scene_->scene().find("score_meter_0.view"); v && v->transformable())
                meter0_ = v->transformable()->local;
        }
        reset();  // HudPanel_v3: Enter, then reset
    }

    void poll(float dtMs) override {
        if (!scene_) return;
        t_ += dtMs;
        for (int p = 0; p < players(); ++p) {
            SelectHost::PlayerState ps;
            if (!host_.playerState(p, ps)) return;
            state_[size_t(p)] = ps;
            if (!multi_) crowd(dtMs, ps.rating);
            star(p, dtMs, ps.energy, ps.starPowerActive);
            if (!multi_) score(ps, p_[0]);
            // HudMsg poll: the text hides when its anim task ends.
            Player& pl = p_[size_t(p)];
            if (pl.messageShown && !pl.message.poll(*scene_, t_)) {
                scene_->setShowing(name("message%d.txt", p), false);
                pl.messageShown = false;
            }
        }
        if (multi_) lead(state_[0].score, state_[1].score);
    }

    // Face-off: each player's ScoreMeter contents at its shell (score_meter_1 / 2).
    void drawAfter() override {
        if (!scene_ || !multi_ || !inGame_) return;
        SceneObject* m0 = scene_->scene().find("score_meter_0.view");
        if (!m0 || !m0->transformable()) return;
        for (int p = 0; p < 2; ++p) {
            const SceneObject* shell = scene_->scene().find(name("score_meter_%d.view", p));
            if (!shell || !shell->transformable() || !shell->drawable() || !shell->drawable()->showing) continue;
            Player& pl = p_[size_t(p)];
            pl.lastScore = pl.lastMult = pl.lastStreak = pl.lastSp = -1;  // the meshes are shared: refresh them all
            score(state_[size_t(p)], pl);
            scene_->setLocalXfm("score_meter_0.view", shell->transformable()->world);
            for (const char* own : {"score_shell.mesh", "score_shell_outline.mesh"}) scene_->setShowing(own, false);
            scene_->draw("score_meter_0.view");
            for (const char* own : {"score_shell.mesh", "score_shell_outline.mesh"}) scene_->setShowing(own, true);
        }
        scene_->setLocalXfm("score_meter_0.view", meter0_);
    }

private:
    struct Player {
        float starTarget = 0, starShown = 0;
        int lastScore = -1, lastMult = -1, lastStreak = -1, lastSp = -1;
        bool messageShown = false;
        AnimTask message;
    };
    int players() const { return multi_ ? 2 : 1; }
    // A scene name for player p: index 0 in single player, p + 1 in a face-off.
    std::string name(const char* fmt, int p) const {
        char buf[48];
        std::snprintf(buf, sizeof buf, fmt, multi_ ? p + 1 : 0);
        return buf;
    }

    // FUN_00132680 + the script's on_reset.
    void reset() {
        for (int p = 0; p < 2; ++p) {
            Player& pl = p_[size_t(p)];
            pl.message.start = -1;
            pl.messageShown = false;
            pl.starTarget = pl.starShown = 0;
            pl.lastScore = pl.lastMult = pl.lastStreak = pl.lastSp = -1;
        }
        for (int p = 0; p < players(); ++p) {
            scene_->setShowing(name("message%d.txt", p), false);
            // StarMeter reset.
            scene_->setShowing(name("star_meter_%d.view", p), true);
            scene_->setFrame(name("star_meter_%d.view", p), 0);
            scene_->setShowing(name("star_meter_fill_%d.view", p), false);
            scene_->setShowing(name("star_meter_ready_%d.view", p), false);
        }
        shake_.stop(*scene_);
        // ScoreMeter reset: {0, x1, 0, no SP}.
        SelectHost::PlayerState zero;
        state_ = {zero, zero};
        score(zero, p_[0]);
        // CrowdMeter reset: the target only. LeadMeter reset: no leader, arrow at 50.
        crowdTarget_ = 0;
        leader_ = -1;
        if (multi_) scene_->setFrame("multi_hud_arrow.tnm", 50);
        host_.sendTo("hud", "on_reset", {});
    }

    // CrowdMeter (FUN_00137338): the needle follows the rating at 0.9 per second.
    void crowd(float dt, float rating) {
        crowdTarget_ = rating;
        if (crowdShown_ != crowdTarget_) {
            float step = 0.0009f * std::fabs(dt);
            crowdShown_ = crowdShown_ < crowdTarget_ ? std::min(crowdShown_ + step, crowdTarget_) : std::max(crowdShown_ - step, crowdTarget_);
        }
        // Every frame: the view starts on frame 0 (red) until it is posed.
        scene_->setFrame("rock_meter.view", crowdShown_ * 100);
        // The face flashes while the rating is under warning_level (0.15).
        const char* mnm = crowdTarget_ >= 0.15f ? "rock_meter_normal.mnm" : "rock_meter_warning.mnm";
        scene_->setFrame(mnm, loopFrame(*scene_, mnm, t_));
    }

    // LeadMeter (FUN_00133de0; gh.dta hud lead_meter max_lead 10000).
    void lead(int s0, int s1) {
        int leader = s0 == s1 ? 0 : s0 > s1 ? 1 : 2;
        if (leader != leader_) {
            leader_ = leader;
            scene_->setFrame("multi_hud_logo.mnm", float(leader));
            scene_->setFrame("multi_hud_middle_glow.mnm", float(leader));
            scene_->setShowing("multi_hud_glow_left.mesh", leader == 1);
            scene_->setShowing("multi_hud_glow_right.mesh", leader == 2);
            static const char* const kLens[] = {"multi_hud_lens.mat", "multi_hud_lens_left.mat", "multi_hud_lens_right.mat"};
            if (SceneObject* o = scene_->scene().find("multi_hud_lens.mesh"); o && o->mesh) o->mesh->mat = kLens[leader];
        }
        float d = std::min(1.0f, std::fabs(float(s1 - s0)) / 10000.0f);
        float f = s0 > s1 ? 50 - 50 * d : 50 + 50 * d;
        // Pinned at an end, the arrow wobbles (multi_hud_arrow_left / right.tnm at t * 0.03).
        if (f <= 0) scene_->setFrame("multi_hud_arrow_left.tnm", loopFrame(*scene_, "multi_hud_arrow_left.tnm", t_));
        else if (f >= 100) scene_->setFrame("multi_hud_arrow_right.tnm", loopFrame(*scene_, "multi_hud_arrow_right.tnm", t_));
        else scene_->setFrame("multi_hud_arrow.tnm", f);
    }

    // StarMeter (FUN_00136928).
    void star(int p, float dt, float energy, bool deployed) {
        Player& pl = p_[size_t(p)];
        const std::string meter = name("star_meter_%d.view", p), fill = name("star_meter_fill_%d.view", p),
                          readyView = name("star_meter_ready_%d.view", p);
        const SceneObject* view = scene_->scene().find(meter);
        if (!view || !view->drawable() || !view->drawable()->showing) return;
        float old = pl.starShown;
        pl.starTarget = energy;
        bool moving = false;
        if (pl.starShown != pl.starTarget) {
            float step = 0.0009f * std::fabs(dt);
            pl.starShown = pl.starShown < pl.starTarget ? std::min(pl.starShown + step, pl.starTarget) : std::max(pl.starShown - step, pl.starTarget);
            scene_->setFrame(meter, pl.starShown * 100);
            moving = !deployed;
        }
        scene_->setShowing(fill, moving);
        scene_->setFrame(fill, moving ? loopFrame(*scene_, fill, t_) : 0);
        constexpr float kReady = 0.5f;  // scoring.dta star_power ready_level
        bool ready = pl.starShown >= kReady || deployed;
        const SceneObject* rv = scene_->scene().find(readyView);
        bool readyShown = rv && rv->drawable() && rv->drawable()->showing;
        if (!readyShown && ready) {
            scene_->setShowing(readyView, true);
            if (!deployed && !multi_) shake_.play(t_, 0, 100000);
        } else if (readyShown && !ready) {
            scene_->setShowing(readyView, false);
            if (!multi_) shake_.stop(*scene_);
        }
        if (ready) {
            scene_->setFrame(readyView, loopFrame(*scene_, readyView, t_));
            if (!multi_) shake_.poll(*scene_, t_);
        }
        host_.starMeterShown(p, pl.starShown);
        // HudPanel_v10: crossing ready_level sends star_power_ready to the script.
        if (old < kReady && kReady <= pl.starShown) host_.sendTo("hud", "star_power_ready", {Value::integer(p)});
    }

    void setDigit(const char* mesh, const char* tex) {
        if (MatObj* m = scene_->renderer().ownMaterial(mesh); m && !m->stages.empty()) m->stages[0].tex = tex;
    }
    void setColor(const char* mesh, const std::array<float, 3>& c) {
        if (MatObj* m = scene_->renderer().ownMaterial(mesh)) m->color = {c[0], c[1], c[2], m->color[3]};
    }

    // ScoreMeter (FUN_00135b10).
    void score(const SelectHost::PlayerState& ps, Player& pl) {
        // score_colors.mnm key colours, read as a palette.
        static const std::array<float, 3> kPalette[5] = {{1, 1, 1}, {1, 0.86f, 0}, {0.56f, 1, 0}, {0.72f, 0.5f, 1}, {0, 0.75f, 1}};
        int sp = ps.starPowerActive ? 1 : 0;
        // Digits (FUN_00135b90): ones on score_num_6, no leading zeros.
        if (ps.score != pl.lastScore) {
            pl.lastScore = ps.score;
            int v = ps.score % 1000000;
            for (int i = 0; i < 6; ++i) {
                char mesh[32], tex[32];
                std::snprintf(mesh, sizeof mesh, "score_num_%d.mesh", 6 - i);
                if (i == 0 || v > 0) std::snprintf(tex, sizeof tex, "score_%d.tex", v % 10);
                else std::snprintf(tex, sizeof tex, "score_none.tex");
                setDigit(mesh, tex);
                v /= 10;
            }
        }
        // Streak bulbs (FUN_00135c98).
        int streak = ps.streak;
        if (streak != pl.lastStreak || ps.multiplier != pl.lastMult || sp != pl.lastSp) {
            bool full = streak > 0 && (streak >= 30 || streak % 10 == 0);
            int m = sp ? ps.multiplier / 2 : ps.multiplier;
            int lit = streak <= 0 ? -1 : full ? 9 : streak % 10 - 1;
            int ci = m - (full ? 1 : 0);
            if (ci == 4) ci = sp ? 4 : 3;
            ci = std::clamp(ci, 0, 4);
            for (int j = 0; j < 10; ++j) {
                char mesh[32], mat[32];
                std::snprintf(mesh, sizeof mesh, "score_streak_%d.mesh", j + 1);
                std::snprintf(mat, sizeof mat, "score_streak_%d.mat", j <= lit ? ci : 0);
                if (SceneObject* o = scene_->scene().find(mesh); o && o->mesh) o->mesh->mat = mat;
            }
            setColor("score_num_frame.mesh", kPalette[sp ? 4 : 0]);
            pl.lastStreak = streak;
        }
        // Multiplier (FUN_00135df8): "xN", blank at x1; frame and glow colours.
        if (ps.multiplier != pl.lastMult || sp != pl.lastSp) {
            if (!multi_ && pl.lastMult >= 0 && ps.multiplier > pl.lastMult) scoreShake_.play(t_, 0, 100000);
            if (ps.multiplier <= 1) {
                setDigit("score_mult_3.mesh", "score_none.tex");
                setDigit("score_mult_2.mesh", "score_none.tex");
            } else {
                char tex[32];
                std::snprintf(tex, sizeof tex, "score_%d.tex", ps.multiplier % 10);
                setDigit("score_mult_3.mesh", "score_x.tex");
                setDigit("score_mult_2.mesh", tex);
            }
            int c = std::clamp(sp ? 4 : ps.multiplier - 1, 0, 4);
            setColor("score_mult_frame.mesh", kPalette[c]);
            scene_->setShowing("score_mult_glow.mesh", c != 0);
            if (c != 0) setColor("score_mult_glow.mesh", kPalette[c]);
            pl.lastMult = ps.multiplier;
        }
        pl.lastSp = sp;
        if (!multi_) scoreShake_.poll(*scene_, t_);
    }

    SelectHost& host_;
    bool loaded_ = false, multi_ = false, inGame_ = true;
    float t_ = 0;
    float crowdTarget_ = 0, crowdShown_ = 0.5f;
    std::array<Player, 2> p_;
    std::array<SelectHost::PlayerState, 2> state_;
    int leader_ = -1;
    Xfm meter0_;
    AnimTask shake_, scoreShake_;
};

// TrackPanel (TrackPanel_v1 @0x15c0e0): the highway is drawn by the engine's
// track renderer; the panel answers the script's messages.
class TrackPanel : public NativePanel {
public:
    explicit TrackPanel(SelectHost& host) : host_(host) {}
    bool handle(const std::string& msg, std::vector<Value>& a, Value& r) override {
        if (msg == "set_frame") {
            host_.trackFrame(a.empty() ? 0.0f : a[0].number());
            return r = Value::integer(0), true;
        }
        if (msg == "pop_smasher" || msg == "set_smasher_glowing" || msg == "reset" || msg == "explode") {
            host_.trackMessage(msg, a);
            return r = Value::integer(0), true;
        }
        // on_miss_phrase / set_bonus_gems: the star gems' looks, drawn by our track renderer.
        for (const char* m : {"on_miss_phrase", "set_bonus_gems", "toggle_nowbar",
                              "toggle_surface", "toggle_gems", "set_dual_perspective"})
            if (msg == m) return r = Value::integer(0), true;
        if (msg == "dual_perspective") return r = Value::integer(0), true;
        return false;
    }
    bool drawsScene() const override { return false; }

private:
    SelectHost& host_;
};

// ArenaPanel: the venue is the engine's (start, set_paused, set_load_banks are handled there).
class ArenaPanel : public NativePanel {
public:
    bool handle(const std::string& msg, std::vector<Value>&, Value& r) override {
        if (msg == "start" || msg == "set_load_banks") return r = Value::integer(0), true;
        return false;
    }
};

// StorePanel (StorePanel_v1 @0x1535d0): store.dta prices; the rest is the script.
class StorePanel : public NativePanel {
public:
    explicit StorePanel(SelectHost& host) : host_(host) {}
    bool handle(const std::string& msg, std::vector<Value>& a, Value& r) override {
        auto arg = [&](size_t k) { return k < a.size() ? a[k].str() : std::string(); };
        if (msg == "price") return r = Value::integer(host_.storePrice(arg(0), arg(1))), true;
        if (msg == "low_cost") return r = Value::integer(host_.storePriceRange(arg(0), false)), true;
        if (msg == "high_cost") return r = Value::integer(host_.storePriceRange(arg(0), true)), true;
        return false;
    }

private:
    SelectHost& host_;
};

// FadePanel (FadePanel_v1 @0x112cb8): start_fade ms [(color r g b)] [(fade_synth B)]
// [(fade_out B)]; a full-screen rect whose alpha ramps over ms, and the synth
// master volume follows (1 - alpha). It stays active after finishing.
class FadePanel : public NativePanel {
public:
    explicit FadePanel(SelectHost& host) : host_(host) {}
    bool handle(const std::string& msg, std::vector<Value>& a, Value& r) override {
        if (msg != "start_fade") return false;
        dur_ = a.empty() ? 0.0f : std::max(1.0f, a[0].number());
        color_ = {0, 0, 0};
        synth_ = out_ = true;
        for (size_t k = 1; k < a.size(); ++k) {
            if (a[k].type != Value::Type::Array || a[k].array->size() < 2) continue;
            const auto& v = *a[k].array;
            std::string key = v[0].str();
            if (key == "color" && v.size() >= 4) color_ = {v[1].number(), v[2].number(), v[3].number()};
            else if (key == "fade_synth") synth_ = v[1].truthy();
            else if (key == "fade_out") out_ = v[1].truthy();
        }
        active_ = true;
        t_ = 0;
        alpha_ = 0;
        return r = Value::integer(0), true;
    }
    void entered() override {  // Load resets it
        active_ = false;
        host_.setMasterVolume(1);
    }
    // The attract mode leaves through finish_fade, which keeps this panel up
    // with the volume faded to 0; the menus after it (splash, meta_loading)
    // have no fade panel, so its exit gives the sound effects back.
    void exit() override {
        active_ = false;
        host_.setMasterVolume(1);
    }
    void poll(float dtMs) override {
        if (!active_) return;
        t_ += dtMs;
        float u = std::clamp(t_ / dur_, 0.0f, 1.0f);
        alpha_ = out_ ? u : 1 - u;
        if (synth_) host_.setMasterVolume(1 - alpha_);
    }
    void drawAfter() override {
        if (!active_ || alpha_ <= 0) return;
        host_.renderer().fillRect(0, 0, kScreenW, kScreenH,
                                  {Uint8(color_[0] * 255), Uint8(color_[1] * 255), Uint8(color_[2] * 255), Uint8(alpha_ * 255)});
    }

private:
    SelectHost& host_;
    bool active_ = false, synth_ = true, out_ = true;
    float dur_ = 1, t_ = 0, alpha_ = 0;
    std::array<float, 3> color_{0, 0, 0};
};

}  // namespace

std::unique_ptr<NativePanel> makeGamePanel(const std::string& verb, SelectHost& host, const std::map<std::string, Value>&) {
    if (verb == "new_hud_panel") return std::make_unique<HudPanel>(host);
    if (verb == "new_track_panel") return std::make_unique<TrackPanel>(host);
    if (verb == "new_arena_panel") return std::make_unique<ArenaPanel>();
    if (verb == "new_store_panel") return std::make_unique<StorePanel>(host);
    if (verb == "new_fade_panel") return std::make_unique<FadePanel>(host);
    return nullptr;
}
