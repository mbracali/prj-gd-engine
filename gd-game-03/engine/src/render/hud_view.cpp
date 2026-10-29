// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/hud_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

#include "data/game_data.h"
#include "render/canvas.h"
#include "render/renderer.h"
#include "scene/milo.h"

// How the game drives the HUD (default.xex; [C] read in the code, [I] inferred):
// - HudPanel poll 0x8268E670 [C] feeds every display each frame: the crowd
//   rating to the rock meter, the Star Power energy and "using" to the star
//   meter, the score, and (multiplier, partial streak, using) to the streak
//   display, partial = 10 from a streak of 30 on, else streak % 10 with a
//   full row (10) on every exact multiple of ten.
// - The HUD has no camera of its own: the UI camera (UIManager init
//   0x8236C4C0 [C]; ui.cam: y_fov 34.516 degrees, at (0, -768, 0) looking
//   along +y, near 250, far 1000 [C data, merge order I]), so one unit is
//   about a pixel of a 640x480 frame.
// - Load 0x82690308 [C] hides youwin.mesh and every "mask*" object.
// - hud_panel.dta [C data]: intro_start_msg sets meters_slide_in.view to
//   frame 0 and hides the meters; slide_meter_in (game.dta: 1.8 s into the
//   intro) shows them and animates the view over 0.5 s; intro_skip holds
//   frame 480 (METER_END_FRAME).
// - The displays' own records name the objects they drive (HudDirData refs,
//   in load order), so the same code drives the single-player, face-off and
//   co-op scenes.
// - Loop frames are seconds * 30.
namespace {

using gfx::Mat4;
using gfx::Vec3;

constexpr float kLoopFps = 30;
constexpr float kMeterEndFrame = 480;     // hud_panel.dta METER_END_FRAME
constexpr float kSlideDelayMs = 1800;     // game.dta extend_track -> slide_meter_in
constexpr float kSlideMs = 500;           // hud_panel.dta slide_meter_in (period 0.5)
constexpr float kMessageMs = 3000;        // gh2.dta (hud (message_show_time 3))
constexpr float kStarRatePerMs = 0.0005f; // BandStarMeterDir 0x82131298 [C]
constexpr float kRockRatePerMs = 0.0009f; // BandCrowdMeterDir 0x82139010 [C]
constexpr float kReadyLevel = 0.5f;       // star meter +0x20c, scoring.dtb ready level [C]
constexpr float kPeakRating = 0.66f;      // 0x822DCE80 [C]

// Moves `shown` towards `target` by at most `step`.
float approach(float shown, float target, float step) {
    if (shown < target) return std::min(target, shown + step);
    return std::max(target, shown - step);
}

const std::vector<std::string>* refsOf(const scene::Directory* dir) {
    auto* self = dir && dir->self ? dir->self->as<scene::DirObj>() : nullptr;
    return self && self->hasHud ? &self->hud.refs : nullptr;
}
const std::string& ref(const scene::Directory* dir, size_t i) {
    static const std::string none;
    const auto* r = refsOf(dir);
    return r && i < r->size() ? (*r)[i] : none;
}
uint32_t value(const scene::Directory* dir, size_t i, uint32_t fallback) {
    auto* self = dir && dir->self ? dir->self->as<scene::DirObj>() : nullptr;
    return self && self->hasHud && i < self->hud.values.size() ? self->hud.values[i] : fallback;
}

}  // namespace

HudView::~HudView() = default;

void HudView::load(const GameData& data, scene::MiloLibrary& library, const std::string& hudFile) {
    (void)data;
    std::string file = hudFile;
    if (file.size() > 5 && file.compare(file.size() - 5, 5, ".milo") == 0) file.resize(file.size() - 5);
    hud_ = &library.load("hud/gen/" + file + ".milo_ps2");
    const bool coop = file == "hud_coop";
    parts_.clear();
    for (const auto& o : hud_->root().objects) {
        const std::string& c = o->cls;
        Kind kind;
        if (c == "BandScoreDisplay") kind = Kind::Score;
        else if (c == "BandStreakDisplay") kind = Kind::Streak;
        else if (c == "BandStarMeterDir") kind = Kind::Star;
        else if (c == "BandCrowdMeterDir") kind = Kind::Crowd;
        else if (c == "BandLeadMeter") kind = Kind::Lead;
        else continue;
        scene::Milo* m = library.proxy(*hud_, *o);
        if (!m) throw std::runtime_error(file + ": " + o->name + " is not a proxy");
        auto p = std::make_unique<Part>();
        p->kind = kind;
        p->dir = &m->root();
        p->proxy = o.get();
        // Whose display: the name's last digit (BandScoreDisplay2,
        // star_meter_2p_2: the second player); co-op shows the band [I].
        p->player = 0;
        if (!coop && !o->name.empty() && o->name.back() == '2') p->player = 1;
        if (coop) p->player = -1;
        if (kind == Kind::Lead) p->leadRange = int(std::max<uint32_t>(1, value(p->dir, 0, 500)));
        // The scene's particle systems, each a running copy for this display.
        for (scene::ParticleSys* ps : p->dir->all<scene::ParticleSys>())
            p->particles.emplace(ps, gfx::ParticleSet(ps, uint32_t(parts_.size() * 16 + p->particles.size() + 1)));
        parts_.push_back(std::move(p));
    }
    if (parts_.empty()) throw std::runtime_error(file + ": no displays");
    sharedStar_ = coop;
    // The UI camera (see above), or the panel's own camera: hud_vs and
    // hud_coop name ui.cam in their PanelDir record (hud_coop's sits higher
    // and further back, framing its score meter) [C data; I: PanelDir draws
    // through its cam when it has one, as GH1's did].
    camera_.world = Mat4::translation(0, -768, 0);
    camera_.yFov = 34.516f * 0.0174533f;
    camera_.nearPlane = 250, camera_.farPlane = 1000;
    if (auto* self = hud_->root().self ? hud_->root().self->as<scene::DirObj>() : nullptr; self && self->hasPanel)
        if (const scene::Cam* cam = hud_->find<scene::Cam>(self->panel.cam)) {
            camera_.world = gfx::toMat4(cam->trans.local);
            camera_.yFov = cam->yFov, camera_.nearPlane = cam->nearPlane, camera_.farPlane = cam->farPlane;
        }
    reset();
}

void HudView::reset(float introStartMs) {
    pose_.clear();
    for (auto& p : parts_) {
        p->pose.clear();
        p->matOverride.clear();
        for (auto& [ps, set] : p->particles) set.reset();
        p->shownStar = 0, p->shownRock = 0.5f;
        p->peak = false, p->peakKnown = false;
    }
    for (Message& m : messages_) m = Message{};
    lastMs_ = 0;
    introStartMs_ = introStartMs;
    wonMs_ = -1e9f;
    meterShakeMs_ = scoreShakeMs_ = -1e9f;
}

const scene::Object* HudView::in(const Part& p, const std::string& name) {
    return p.dir && !name.empty() ? p.dir->find(name) : nullptr;
}

void HudView::setMat(Part& p, const std::string& mesh, const std::string& mat) {
    const scene::Mesh* m = in<scene::Mesh>(p, mesh);
    if (!m) return;
    p.matOverride[m] = in<scene::Mat>(p, mat);
}

void HudView::showMessage(int player, const std::string& text, float nowMs) {
    Message& m = messages_[std::clamp(player, -1, 1) < 0 ? 2 : size_t(player)];
    m.text = text;
    m.startMs = nowMs;
}

void HudView::onEvents(const std::vector<PlayerEvent>& events, int player, float nowMs) {
    for (const PlayerEvent& e : events) {
        switch (e.kind) {
        case PlayerEvent::Kind::StarPowerReady:
            // hud_panel.dta star_power_ready: show_message (the shared
            // message with a shared star meter, from player 0 only) and the
            // unhandled return lets meter_shake.trig play too [C].
            if (player < 0) break;
            if (!sharedStar_) showMessage(player, "STAR POWER READY!", nowMs);
            else if (player == 0) showMessage(-1, "STAR POWER READY!", nowMs);
            meterShakeMs_ = nowMs;
            break;
        case PlayerEvent::Kind::Failed: showMessage(-1, "SONG FAILED!", nowMs); break;  // game_lost: kPlayerShared
        case PlayerEvent::Kind::BuildStreak:  // score_shake.trig [I: which value]; co-op: the band's
            if (!sharedStar_ || player < 0) scoreShakeMs_ = nowMs;
            break;
        default: break;
        }
    }
}

// BandScoreDisplay 0x8212AE68 / 0x8212B630 [C]: the digit meshes (its own
// record's mesh list) fill right to left, the meshes left of the number
// show score_none.mat (no leading zeros), a score of 0 shows one 0; higher
// digits are dropped.
void HudView::driveScore(Part& p, int score) {
    const auto* refs = refsOf(p.dir);
    const uint32_t digits = value(p.dir, 0, 6);
    std::vector<std::string> meshes;
    for (uint32_t i = 0; refs && i < digits && i < refs->size(); ++i) meshes.push_back((*refs)[i]);
    if (meshes.empty())
        for (int i = 1; i <= 6; ++i) meshes.push_back("score_num_" + std::to_string(i) + ".mesh");
    int v = std::max(0, score);
    for (size_t k = 0; k < meshes.size(); ++k) {
        const std::string& mesh = meshes[meshes.size() - 1 - k];
        if (v > 0 || (k == 0 && score <= 0)) setMat(p, mesh, "score_" + std::to_string(v % 10) + ".mat");
        else setMat(p, mesh, "score_none.mat");
        v /= 10;
    }
}

// BandStreakDisplay 0x8212EBE0 / 0x8212EEE8 [C].
void HudView::driveStreak(Part& p, int streak, int mult, bool star) {
    const int partial = streak > 29 ? 10 : (streak % 10 == 0 && streak != 0 ? 10 : streak % 10);
    int tier = star ? mult / 2 : mult;
    if (partial >= 10) --tier;
    tier = std::clamp(tier, 0, 4);
    if (tier == 4 && !star) tier = 3;
    for (int i = 0; i < 10; ++i)
        setMat(p, "score_streak_" + std::to_string(i + 1) + ".mesh",
               i < partial ? "score_streak_" + std::to_string(tier) + ".mat" : "score_streak_0.mat");
    // The multiplier: nothing at x1, else the digit and the X.
    if (mult <= 1) {
        setMat(p, "score_mult_2.mesh", "score_none.mat");
        setMat(p, "score_mult_3.mesh", "score_none.mat");
    } else {
        setMat(p, "score_mult_2.mesh", mult < 10 ? "score_" + std::to_string(mult) + ".mat" : "score_none.mat");
        setMat(p, "score_mult_3.mesh", "score_x.mat");
    }
    // Colours from score_colors.mnm's keys (rgb only): the multiplier frame
    // at frame (star ? 4 : mult - 1); the glow shows (in that colour) unless
    // that frame is 0 (0x8212EB58).
    const auto* colors = in<scene::MatAnim>(p, "score_colors.mnm");
    const float frame = star ? 4.0f : float(mult - 1);
    std::array<float, 4> c{1, 1, 1, 1};
    if (colors) scene::sampleLinear(colors->colorKeys, frame, c);
    auto tint = [&](const scene::Mat* m) {
        if (!m) return;
        gfx::ScenePose::MatState& st = p.pose.matState(m);
        st.hasColor = true;
        st.rgb[0] = c[0], st.rgb[1] = c[1], st.rgb[2] = c[2];
    };
    tint(in<scene::Mat>(p, "score_mult_frame.mat"));
    if (const scene::Mesh* glow = in<scene::Mesh>(p, "score_mult_glow.mesh")) {
        p.pose.setShowing(glow, frame != 0);
        if (glow->dir) tint(glow->dir->resolve<scene::Mat>(glow->mat));
    }
}

// BandStarMeterDir 0x82131708 [C]: the shown value eases at 0.5 / s; the
// meter view (ref 0) at value x 100 (its ParticleSysAnim sets star_glow's
// ranges); the fill view (ref 1) shows while the value moves; the ready
// view (ref 2) at 0.5 or while in use; the glow group (ref 5) loops; the
// co-op engage anims (refs 3, 4) play while Star Power is on [I].
void HudView::driveStar(Part& p, const PerformerState& player, float dt, float loop) {
    const float value = player.starMeter;
    const bool using_ = player.spActive;
    p.shownStar = approach(p.shownStar, value, kStarRatePerMs * dt);
    p.pose.setFrame(in(p, ref(p.dir, 0)), p.shownStar * 100);
    if (const scene::Object* fill = in(p, ref(p.dir, 1))) {
        p.pose.setShowing(fill, p.shownStar != value && !using_);
        p.pose.setFrame(fill, loop);
    }
    if (const scene::Object* ready = in(p, ref(p.dir, 2))) {
        p.pose.setShowing(ready, p.shownStar >= kReadyLevel || using_);
        p.pose.setFrame(ready, loop);
    }
    for (size_t i : {size_t(3), size_t(4)})
        if (const scene::Object* a = in(p, ref(p.dir, i)); a && using_) p.pose.setFrame(a, loop);
    p.pose.setFrame(in(p, ref(p.dir, 5)), loop);
}

// BandCrowdMeterDir 0x82139010 [C]: the needle view (ref 4) follows the
// value eased at 0.9 / s; under the warning level the warning anim (ref 0),
// else the zone anim (refs 1..3: red / yellow / green, every one the normal
// anim in the single-player meter; zones by thirds I). Peak (0x822DCE80,
// 0x822DCEF0) [C]: multiplier 4 (without Star Power) and rating > 0.66
// sends peak_on, and its end peak_off; crowd_meter's triggers then show
// top_level_peak.grp and loop peak_effects_on.filt (peak_effects.trig), or
// play peak_effects_off.filt and hide it; begin.trig hides it at the start;
// SP_on / SP_off.trig swap peak_flames.grp and peak_flames_SP.grp [C data].
void HudView::driveCrowd(Part& p, const PerformerState& player, float dt, float loop, float nowMs) {
    (void)nowMs;
    const float rock = player.rock;
    p.shownRock = approach(p.shownRock, rock, kRockRatePerMs * dt);
    p.pose.setFrame(in(p, ref(p.dir, 4).empty() ? "hud1_rock_needle.view" : ref(p.dir, 4)), p.shownRock * 100);
    size_t zone = rock < player.warning ? 0 : rock < 0.33f ? 1 : rock < 0.66f ? 2 : 3;
    std::string anim = ref(p.dir, zone);
    if (anim.empty()) anim = zone == 0 ? "rock_meter_warning.filt" : "rock_meter_normal.mnm";
    p.pose.setFrame(in(p, anim), loop);

    const scene::Object* peakGrp = in(p, "top_level_peak.grp");
    if (!peakGrp) return;
    const bool peak = player.baseMultiplier == 4 && rock > kPeakRating;
    if (!p.peakKnown || peak != p.peak) {
        p.peak = peak;
        p.peakKnown = true;
        p.pose.setShowing(peakGrp, peak);
    }
    const bool sp = player.spActive;
    if (const scene::Object* a = in(p, "peak_flames.grp")) p.pose.setShowing(a, !sp);
    if (const scene::Object* b = in(p, "peak_flames_SP.grp")) p.pose.setShowing(b, sp);
    if (peak) p.pose.setFrame(in(p, "peak_effects_on.filt"), loop);
}

// BandLeadMeter 0x82126E88 [C]: lead > 0 (the left player ahead) or < 0;
// the glow anims (ref 1) at frame 0 / 1 / 2 (even / left / right), the
// left (ref 2) or right (ref 3) glow shown, the lens (ref 6) gets ref 7 /
// 9 / 8, the needle (ref 0) at 50 - 50 x min(|lead| / range, 1) x sign.
// The lead is the score difference [I], the range the record's first value
// (500).
void HudView::driveLead(Part& p, int lead) {
    const int state = lead == 0 ? 0 : lead > 0 ? 1 : 2;
    p.pose.setFrame(in(p, ref(p.dir, 1)), float(state));
    if (const scene::Object* l = in(p, ref(p.dir, 2))) p.pose.setShowing(l, state == 1);
    if (const scene::Object* r = in(p, ref(p.dir, 3))) p.pose.setShowing(r, state == 2);
    if (const scene::Mesh* lens = in<scene::Mesh>(p, ref(p.dir, 6)))
        p.matOverride[lens] = in<scene::Mat>(p, ref(p.dir, lead < 0 ? 8 : lead == 0 ? 7 : 9));
    const float frac = std::min(1.0f, float(std::abs(lead)) / float(p.leadRange));
    const float sign = lead > 0 ? -1.0f : 1.0f;
    p.pose.setFrame(in(p, ref(p.dir, 0)), frac * 50 * sign + 50);
}

void HudView::drive(const std::vector<PerformerState>& players, const PerformerState* band, float nowMs) {
    const float dt = std::clamp(nowMs - lastMs_, 0.0f, 100.0f);
    lastMs_ = nowMs;
    const float loop = std::max(0.0f, nowMs) * 0.001f * kLoopFps;
    clockFrames_ = nowMs * 0.001f * kLoopFps;

    // Load (0x82690308): youwin and the masks stay hidden.
    for (const char* n : {"mask1.mesh", "mask2.mesh", "mask3.mesh"})
        if (const scene::Object* o = hud_->find(n)) pose_.setShowing(o, false);
    if (const scene::Object* o = hud_->find("youwin.mesh")) pose_.setShowing(o, nowMs >= wonMs_ && nowMs < wonMs_ + 3000);
    float slide = kMeterEndFrame;
    if (const scene::Object* view = hud_->find("meters_slide_in.view"); view && introStartMs_ > -1e8f) {
        const float t = (nowMs - introStartMs_ - kSlideDelayMs) / kSlideMs;
        slide = t >= 1 ? kMeterEndFrame : std::clamp(t, 0.0f, 1.0f) * gfx::ScenePose::endFrame(view);
    }
    pose_.setFrame(hud_->find("meters_slide_in.view"), slide);
    // Shakes: meter_shake.trig on star_power_ready, score_shake.trig on
    // build_streak; 25-frame ranges at 30 fps (rate 0) [C data].
    if (const float f = (nowMs - meterShakeMs_) * 0.001f * kLoopFps; f >= 0 && f <= 25)
        pose_.setFrame(hud_->find("meter_shake_star_power_ready.filt"), f);
    if (const float f = (nowMs - scoreShakeMs_) * 0.001f * kLoopFps; f >= 0 && f <= 25)
        pose_.setFrame(hud_->find("score_shake_build_streak.filt"), f);

    const PerformerState& p0 = players[0];
    const PerformerState& p1 = players.size() > 1 ? players[1] : p0;
    for (auto& part : parts_) {
        Part& p = *part;
        const PerformerState& pl = p.player < 0 && band ? *band : p.player == 1 ? p1 : p0;
        switch (p.kind) {
        case Kind::Score: driveScore(p, pl.score); break;
        case Kind::Streak: driveStreak(p, pl.streak, pl.multiplier, pl.spActive); break;
        case Kind::Star: driveStar(p, pl, dt, loop); break;
        case Kind::Crowd: driveCrowd(p, pl, dt, loop, nowMs); break;
        case Kind::Lead: driveLead(p, p0.score - p1.score); break;
        }
    }
}

void HudView::setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env, const gfx::ScenePose& pose) {
    if (env == currentEnv_ && frameSet_) return;
    currentEnv_ = env;
    frameSet_ = true;
    gfx::LightEnv le;
    if (env) le = cache.lightEnv(*env, &pose);
    mr.setFrame(camera_, le, viewW_, viewH_);
}

void HudView::drawObject(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object* o, const scene::Xfm& base,
                         const scene::Environ* env, Part* part, int depth) {
    const gfx::ScenePose& pose = part ? part->pose : pose_;
    if (!o || depth > 16 || !pose.showing(o)) return;
    if (auto* m = o->as<scene::Mesh>()) {
        setEnv(mr, cache, env, pose);
        const Mat4 world = gfx::toMat4(scene::compose(pose.world(m), base));
        const scene::Mat* mat = nullptr;
        if (part)
            if (auto it = part->matOverride.find(m); it != part->matOverride.end()) {
                if (!it->second) return;
                mat = it->second;
            }
        if (const gfx::ScenePose::MeshAnimState* ma = pose.meshAnim(m)) {
            const scene::Mesh* geom = m;
            if (m->verts.empty() && !m->geomOwner.empty() && m->dir)
                if (auto* g = m->dir->resolve<scene::Mesh>(m->geomOwner)) geom = g;
            if (!geom->faces.empty()) {
                if (!meshAnimBuffer_) meshAnimBuffer_ = std::make_unique<gfx::DynamicMesh>(*device_);
                std::vector<uint16_t> idx;
                for (const auto& f : geom->faces) idx.insert(idx.end(), {f[0], f[1], f[2]});
                const gfx::GpuMesh g = meshAnimBuffer_->upload(gfx::meshAnimVertices(*geom, *ma->anim, ma->frame), idx);
                gfx::drawMesh(mr, cache, *m, world, &pose, mat, nullptr, &g);
                return;
            }
        }
        gfx::drawMesh(mr, cache, *m, world, &pose, mat);
    } else if (auto* g = o->as<scene::Group>()) {
        if (!g->environment.empty() && g->dir)
            if (auto* e = g->dir->resolve<scene::Environ>(g->environment)) env = e;
        for (const std::string& n : g->objects)
            if (const scene::Object* c = g->dir ? g->dir->resolve(n) : nullptr; c && c != g) drawObject(mr, cache, c, base, env, part, depth + 1);
    } else if (auto* ps = o->as<scene::ParticleSys>()) {
        if (!part) return;
        auto it = part->particles.find(ps);
        if (it == part->particles.end()) return;
        it->second.sync(pose);
        it->second.update(pose, base, clockFrames_);
        setEnv(mr, cache, env, pose);
        if (!particles_) particles_ = std::make_unique<gfx::ParticleRenderer>(*device_);
        it->second.draw(*particles_, mr, cache, camera_, Mat4::identity(), &pose);
    } else if (auto* d = o->as<scene::DirObj>()) {
        for (auto& p : parts_)
            if (p->proxy == d) drawDir(mr, cache, *p->dir, scene::compose(pose.world(d), base), env, p.get());
    }
}

// A directory draws its drawables that no group of its own holds (groups
// draw their members), in table order.
void HudView::drawDir(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Directory& dir, const scene::Xfm& base,
                      const scene::Environ* env, Part* part) {
    if (auto* self = dir.self ? dir.self->as<scene::DirObj>() : nullptr; self && self->hasRnd && !self->rnd.environment.empty())
        if (auto* e = dir.find<scene::Environ>(self->rnd.environment)) env = e;
    std::set<std::string> grouped;
    for (const auto& o : dir.objects)
        if (auto* g = o->as<scene::Group>()) grouped.insert(g->objects.begin(), g->objects.end());
    std::vector<const scene::Object*> top;
    for (const auto& o : dir.objects) {
        if (grouped.count(o->name)) continue;
        if (o->as<scene::Mesh>() || o->as<scene::Group>() || o->as<scene::DirObj>() || o->as<scene::ParticleSys>()) top.push_back(o.get());
    }
    // [I] The drop shadow behind the rock meter (meter_extras.grp, z mode
    // force) has to come first; the game's draw order is not traced.
    std::stable_sort(top.begin(), top.end(), [](const scene::Object* a, const scene::Object* b) {
        return (a->name == "meter_extras.grp") > (b->name == "meter_extras.grp");
    });
    for (const scene::Object* o : top) drawObject(mr, cache, o, base, env, part, 0);
}

// The HUD messages (0x826908D0 [C]): messageN.txt (message_shared.txt for
// the shared one, else message0.txt) with the localized token, animated by
// its .tnm over frames 0..100 in message_show_time (3 s); drawn here in the
// HUD font (rokk.font) through the canvas, its glyph quads projected with
// the UI camera. Glyph layout [I]: cells of the font's cell size, `size`
// units tall, centred (align 0x22).
void HudView::drawMessages(Canvas& canvas, float nowMs) {
    for (int i = 0; i < 3; ++i) {
        const Message& m = messages_[i];
        if (m.text.empty()) continue;
        std::string name = i == 2 ? "message_shared" : "message" + std::to_string(i);
        const scene::Text* text = hud_->find<scene::Text>(name + ".txt");
        if (!text && i == 2) name = "message0", text = hud_->find<scene::Text>("message0.txt");
        if (!text) continue;
        drawMessage(canvas, text, hud_->find(name + ".tnm"), m, nowMs);
    }
}

void HudView::drawMessage(Canvas& canvas, const scene::Text* text, const scene::Object* anim, const Message& msg, float nowMs) {
    const float age = nowMs - msg.startMs;
    if (age < 0 || age > kMessageMs) return;
    pose_.setFrame(anim, age / kMessageMs * 100);
    const scene::Font* font = text->dir ? text->dir->resolve<scene::Font>(text->font) : nullptr;
    const scene::Mat* mat = font && font->dir ? font->dir->resolve<scene::Mat>(font->mat) : nullptr;
    const scene::Tex* tex = mat && mat->dir ? mat->dir->resolve<scene::Tex>(mat->diffuseTex) : nullptr;
    if (!font || !tex || font->cellSize[1] <= 0 || font->texWidth == 0 || font->texHeight == 0) return;
    const float aspect = font->cellSize[0] / font->cellSize[1];
    const float h = text->size;
    float width = 0;
    for (unsigned char ch : msg.text) width += font->glyphs[ch].advance * h * aspect;
    const Mat4 world = gfx::toMat4(pose_.world(text));
    const Mat4 vp = camera_.viewProj(false);
    auto project = [&](float x, float z, Vertex& v) {
        const Vec3 p = world.transformPoint({x, 0, z});
        const float cx = vp.at(0, 0) * p.x + vp.at(0, 1) * p.y + vp.at(0, 2) * p.z + vp.at(0, 3);
        const float cy = vp.at(1, 0) * p.x + vp.at(1, 1) * p.y + vp.at(1, 2) * p.z + vp.at(1, 3);
        const float cw = vp.at(3, 0) * p.x + vp.at(3, 1) * p.y + vp.at(3, 2) * p.z + vp.at(3, 3);
        if (cw <= 0.01f) return false;
        v.x = (cx / cw * 0.5f + 0.5f) * kScreenW;
        v.y = (0.5f - cy / cw * 0.5f) * kScreenH;
        v.z = 0;
        return true;
    };
    const float gw = font->cellSize[0] / float(font->texWidth), gh = font->cellSize[1] / float(font->texHeight);
    const Uint8 r = Uint8(std::clamp(mat->color[0] * text->color[0], 0.0f, 1.0f) * 255);
    const Uint8 g = Uint8(std::clamp(mat->color[1] * text->color[1], 0.0f, 1.0f) * 255);
    const Uint8 b = Uint8(std::clamp(mat->color[2] * text->color[2], 0.0f, 1.0f) * 255);
    const Uint8 a = Uint8(std::clamp(mat->color[3] * text->color[3], 0.0f, 1.0f) * 255);
    std::vector<Vertex> quads;
    float x = -width * 0.5f;
    for (unsigned char ch : msg.text) {
        const scene::Font::Glyph& gl = font->glyphs[ch];
        const float w = gl.width * h * aspect;
        if (ch != ' ' && gl.width > 0) {
            Vertex q[4];
            const float u0 = gl.x, v0 = gl.y, u1 = gl.x + gl.width * gw, v1 = gl.y + gh;
            bool ok = project(x, h * 0.5f, q[0]) && project(x + w, h * 0.5f, q[1]) && project(x + w, -h * 0.5f, q[2]) &&
                      project(x, -h * 0.5f, q[3]);
            if (ok) {
                q[0].u = u0, q[0].v = v0, q[1].u = u1, q[1].v = v0, q[2].u = u1, q[2].v = v1, q[3].u = u0, q[3].v = v1;
                for (Vertex& v : q) v.r = r, v.g = g, v.b = b, v.a = a;
                quads.insert(quads.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
            }
        }
        x += gl.advance * h * aspect;
    }
    const gfx::TextureId id = cache_ ? cache_->texture(tex) : gfx::TextureId{};
    if (id) canvas.draw(quads.data(), quads.size(), int(id.id), Blend::Alpha);
}

void HudView::draw(gfx::Renderer& r, gfx::SceneCache& cache, Canvas& canvas, const std::vector<PerformerState>& players,
                   const PerformerState* band, float nowMs) {
    if (!hud_ || players.empty()) return;
    device_ = &r.device();
    cache_ = &cache;
    int vx, vy;
    r.pictureRect(vx, vy, viewW_, viewH_);
    drive(players, band, nowMs);
    // The HUD draws over everything (the UI camera's depth range 0..0.02).
    r.device().clear(nullptr, true);
    frameSet_ = false;
    currentEnv_ = nullptr;
    gfx::MaterialRenderer& mr = r.materials();
    // intro_start_msg hides the meters until slide_meter_in.
    const bool metersShown = introStartMs_ < -1e8f || nowMs >= introStartMs_ + kSlideDelayMs;
    if (metersShown) drawDir(mr, cache, hud_->root(), scene::Xfm{}, nullptr, nullptr);
    drawMessages(canvas, nowMs);
}
