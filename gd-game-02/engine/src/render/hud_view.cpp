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
// - meters_slide_in.view rests at frame 480 (METER_END_FRAME, hud_panel.dta).
// - Loop frames are seconds * 30.
namespace {

using gfx::Mat4;
using gfx::Vec3;

constexpr float kLoopFps = 30;
constexpr float kMeterEndFrame = 480;     // hud_panel.dta METER_END_FRAME
constexpr float kMessageMs = 3000;        // gh2.dta (hud (message_show_time 3))
constexpr float kStarRatePerMs = 0.0005f; // BandStarMeterDir 0x82131298 [C]
constexpr float kRockRatePerMs = 0.0009f; // BandCrowdMeterDir 0x82139010 [C]
constexpr float kReadyLevel = 0.5f;       // star meter +0x20c, scoring.dtb ready level [C]

// Moves `shown` towards `target` by at most `step`.
float approach(float shown, float target, float step) {
    if (shown < target) return std::min(target, shown + step);
    return std::max(target, shown - step);
}

}  // namespace

HudView::~HudView() = default;

void HudView::load(const GameData& data, scene::MiloLibrary& library) {
    (void)data;
    hud_ = &library.load("hud/gen/hud_sp.milo_xbox");
    auto part = [&](const char* name) {
        Part p;
        p.proxy = hud_->find(name);
        if (!p.proxy) throw std::runtime_error(std::string("hud_sp: no ") + name);
        scene::Milo* m = library.proxy(*hud_, *p.proxy);
        if (!m) throw std::runtime_error(std::string("hud_sp: ") + name + " is not a proxy");
        p.dir = &m->root();
        return p;
    };
    score_ = part("BandScoreDisplay1");
    streak_ = part("BandStreakDisplay1");
    star_ = part("BandStarMeterDir1");
    crowd_ = part("BandCrowdMeterDir1");
    // The UI camera (see above).
    camera_.world = Mat4::translation(0, -768, 0);
    camera_.yFov = 34.516f * 0.0174533f;
    camera_.nearPlane = 250, camera_.farPlane = 1000;
    reset();
}

void HudView::reset() {
    pose_.clear();
    matOverride_.clear();
    messageMs_ = -1e9f;
    message_.clear();
    shownRock_ = 0.5f, shownStar_ = 0;
    lastMs_ = 0;
    meterShakeMs_ = scoreShakeMs_ = -1e9f;
    lastMultiplier_ = 1;
}

const scene::Object* HudView::in(const Part& p, const std::string& name) const { return p.dir ? p.dir->find(name) : nullptr; }

void HudView::setMat(const Part& p, const std::string& mesh, const std::string& mat) {
    const scene::Mesh* m = in<scene::Mesh>(p, mesh);
    if (!m) return;
    matOverride_[m] = in<scene::Mat>(p, mat);
}

void HudView::showMessage(const std::string& text, float nowMs) {
    message_ = text;
    messageMs_ = nowMs;
}

void HudView::onEvents(const std::vector<PlayerEvent>& events, const Player& player, float nowMs) {
    (void)player;
    for (const PlayerEvent& e : events) {
        switch (e.kind) {
        case PlayerEvent::Kind::StarPowerReady:
            // hud_panel.dta star_power_ready: show_message star_ready, and the
            // unhandled return lets meter_shake.trig play too [C].
            showMessage("STAR POWER READY!", nowMs);
            meterShakeMs_ = nowMs;
            break;
        case PlayerEvent::Kind::Failed: showMessage("SONG FAILED!", nowMs); break;  // game_lost
        case PlayerEvent::Kind::BuildStreak: scoreShakeMs_ = nowMs; break;          // score_shake.trig [I: which value]
        default: break;
        }
    }
}

// BandScoreDisplay 0x8212AE68 / 0x8212B630 [C]: the digits fill the six
// meshes right to left, the meshes left of the number show score_none.mat
// (no leading zeros), a score of 0 shows one 0; higher digits are dropped.
void HudView::driveScore(int score) {
    static const char* const kMeshes[6] = {"score_num_1.mesh", "score_num_2.mesh", "score_num_3.mesh",
                                           "score_num_4.mesh", "score_num_5.mesh", "score_num_6.mesh"};
    int v = std::max(0, score);
    for (int k = 0; k < 6; ++k) {
        const char* mesh = kMeshes[5 - k];
        if (v > 0 || (k == 0 && score <= 0)) setMat(score_, mesh, "score_" + std::to_string(v % 10) + ".mat");
        else setMat(score_, mesh, "score_none.mat");
        v /= 10;
    }
}

// BandStreakDisplay 0x8212EBE0 / 0x8212EEE8 [C].
void HudView::driveStreak(int streak, int mult, bool star) {
    const int partial = streak > 29 ? 10 : (streak % 10 == 0 && streak != 0 ? 10 : streak % 10);
    int tier = star ? mult / 2 : mult;
    if (partial >= 10) --tier;
    tier = std::clamp(tier, 0, 4);
    if (tier == 4 && !star) tier = 3;
    for (int i = 0; i < 10; ++i)
        setMat(streak_, "score_streak_" + std::to_string(i + 1) + ".mesh",
               i < partial ? "score_streak_" + std::to_string(tier) + ".mat" : "score_streak_0.mat");
    // The multiplier: nothing at x1, else the digit and the X.
    if (mult <= 1) {
        setMat(streak_, "score_mult_2.mesh", "score_none.mat");
        setMat(streak_, "score_mult_3.mesh", "score_none.mat");
    } else {
        setMat(streak_, "score_mult_2.mesh", mult < 10 ? "score_" + std::to_string(mult) + ".mat" : "score_none.mat");
        setMat(streak_, "score_mult_3.mesh", "score_x.mat");
    }
    // Colours from score_colors.mnm's keys (rgb only): the multiplier frame
    // at frame (star ? 4 : mult - 1); the glow shows (in that colour) unless
    // that frame is 0 (0x8212EB58).
    const auto* colors = in<scene::MatAnim>(streak_, "score_colors.mnm");
    const float frame = star ? 4.0f : float(mult - 1);
    std::array<float, 4> c{1, 1, 1, 1};
    if (colors) scene::sampleLinear(colors->colorKeys, frame, c);
    auto tint = [&](const scene::Mat* m) {
        if (!m) return;
        gfx::ScenePose::MatState& st = pose_.matState(m);
        st.hasColor = true;
        st.rgb[0] = c[0], st.rgb[1] = c[1], st.rgb[2] = c[2];
    };
    tint(in<scene::Mat>(streak_, "score_mult_frame.mat"));
    if (const scene::Mesh* glow = in<scene::Mesh>(streak_, "score_mult_glow.mesh")) {
        pose_.setShowing(glow, frame != 0);
        if (glow->dir) tint(glow->dir->resolve<scene::Mat>(glow->mat));
    }
}

void HudView::drive(const Player& player, float nowMs) {
    const float dt = std::clamp(nowMs - lastMs_, 0.0f, 100.0f);
    lastMs_ = nowMs;
    const float loop = std::max(0.0f, nowMs) * 0.001f * kLoopFps;

    // Load (0x82690308): youwin and the masks stay hidden.
    for (const char* n : {"youwin.mesh", "mask1.mesh", "mask2.mesh"})
        if (const scene::Object* o = hud_->find(n)) pose_.setShowing(o, false);
    pose_.setFrame(hud_->find("meters_slide_in.view"), kMeterEndFrame);
    // Shakes: meter_shake.trig on star_power_ready, score_shake.trig on
    // build_streak; 25-frame ranges at 30 fps (rate 0) [C data].
    if (const float f = (nowMs - meterShakeMs_) * 0.001f * kLoopFps; f >= 0 && f <= 25)
        pose_.setFrame(hud_->find("meter_shake_star_power_ready.filt"), f);
    if (const float f = (nowMs - scoreShakeMs_) * 0.001f * kLoopFps; f >= 0 && f <= 25)
        pose_.setFrame(hud_->find("score_shake_build_streak.filt"), f);

    driveScore(player.score());
    const bool using_ = player.starPowerActive();
    driveStreak(player.streak(), player.multiplier(), using_);

    // BandStarMeterDir 0x82131708 [C].
    const float value = player.starMeter();
    shownStar_ = approach(shownStar_, value, kStarRatePerMs * dt);
    pose_.setFrame(in(star_, "star_meter.view"), shownStar_ * 100);
    if (const scene::Object* fill = in(star_, "star_meter_fill.view")) {
        pose_.setShowing(fill, shownStar_ != value && !using_);
        pose_.setFrame(fill, loop);
    }
    if (const scene::Object* ready = in(star_, "star_meter_ready.view")) {
        pose_.setShowing(ready, shownStar_ >= kReadyLevel || using_);
        pose_.setFrame(ready, loop);
    }
    pose_.setFrame(in(star_, "star_glow.grp"), loop);

    // BandCrowdMeterDir 0x82139010 [C]: the needle (and the red / yellow /
    // green lights stepping at frames 33 and 66) follow the displayed value;
    // under the warning level the face pulses red.
    const float rock = player.rockMeter();
    shownRock_ = approach(shownRock_, rock, kRockRatePerMs * dt);
    pose_.setFrame(in(crowd_, "hud1_rock_needle.view"), shownRock_ * 100);
    pose_.setFrame(in(crowd_, rock < player.warningLevel() ? "rock_meter_warning.filt" : "rock_meter_normal.mnm"), loop);
    // begin.trig hides the peak flames; nothing seen sends peak_on yet [I].
    if (const scene::Object* peak = in(crowd_, "top_level_peak.grp")) pose_.setShowing(peak, false);
}

void HudView::setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env) {
    if (env == currentEnv_ && frameSet_) return;
    currentEnv_ = env;
    frameSet_ = true;
    gfx::LightEnv le;
    if (env) le = cache.lightEnv(*env, &pose_);
    mr.setFrame(camera_, le, viewW_, viewH_);
}

void HudView::drawObject(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object* o, const scene::Xfm& base,
                         const scene::Environ* env, int depth) {
    if (!o || depth > 16 || !pose_.showing(o)) return;
    if (auto* m = o->as<scene::Mesh>()) {
        setEnv(mr, cache, env);
        const Mat4 world = gfx::toMat4(scene::compose(pose_.world(m), base));
        const scene::Mat* mat = nullptr;
        if (auto it = matOverride_.find(m); it != matOverride_.end()) {
            if (!it->second) return;
            mat = it->second;
        }
        if (const gfx::ScenePose::MeshAnimState* ma = pose_.meshAnim(m)) {
            const scene::Mesh* geom = m;
            if (m->verts.empty() && !m->geomOwner.empty() && m->dir)
                if (auto* g = m->dir->resolve<scene::Mesh>(m->geomOwner)) geom = g;
            if (!geom->faces.empty()) {
                if (!meshAnimBuffer_) meshAnimBuffer_ = std::make_unique<gfx::DynamicMesh>(*device_);
                std::vector<uint16_t> idx;
                for (const auto& f : geom->faces) idx.insert(idx.end(), {f[0], f[1], f[2]});
                const gfx::GpuMesh g = meshAnimBuffer_->upload(gfx::meshAnimVertices(*geom, *ma->anim, ma->frame), idx);
                gfx::drawMesh(mr, cache, *m, world, &pose_, mat, nullptr, &g);
                return;
            }
        }
        gfx::drawMesh(mr, cache, *m, world, &pose_, mat);
    } else if (auto* g = o->as<scene::Group>()) {
        if (!g->environment.empty() && g->dir)
            if (auto* e = g->dir->resolve<scene::Environ>(g->environment)) env = e;
        for (const std::string& n : g->objects)
            if (const scene::Object* c = g->dir ? g->dir->resolve(n) : nullptr; c && c != g) drawObject(mr, cache, c, base, env, depth + 1);
    } else if (auto* d = o->as<scene::DirObj>()) {
        for (const Part* p : {&score_, &streak_, &star_, &crowd_})
            if (p->proxy == d) drawDir(mr, cache, *p->dir, scene::compose(pose_.world(d), base), env);
    }
}

// A directory draws its drawables that no group of its own holds (groups
// draw their members), in table order.
void HudView::drawDir(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Directory& dir, const scene::Xfm& base,
                      const scene::Environ* env) {
    if (auto* self = dir.self ? dir.self->as<scene::DirObj>() : nullptr; self && self->hasRnd && !self->rnd.environment.empty())
        if (auto* e = dir.find<scene::Environ>(self->rnd.environment)) env = e;
    std::set<std::string> grouped;
    for (const auto& o : dir.objects)
        if (auto* g = o->as<scene::Group>()) grouped.insert(g->objects.begin(), g->objects.end());
    std::vector<const scene::Object*> top;
    for (const auto& o : dir.objects) {
        if (grouped.count(o->name)) continue;
        if (o->as<scene::Mesh>() || o->as<scene::Group>() || o->as<scene::DirObj>()) top.push_back(o.get());
    }
    // [I] The drop shadow behind the rock meter (meter_extras.grp, z mode
    // force) has to come first; the game's draw order is not traced.
    std::stable_sort(top.begin(), top.end(), [](const scene::Object* a, const scene::Object* b) {
        return (a->name == "meter_extras.grp") > (b->name == "meter_extras.grp");
    });
    for (const scene::Object* o : top) drawObject(mr, cache, o, base, env, 0);
}

// The HUD message (0x826908D0 [C]): message0.txt with the localized token,
// animated by message0.tnm over frames 0..100 in message_show_time (3 s);
// drawn here in the HUD font (rokk.font) through the canvas, its glyph
// quads projected with the UI camera. Glyph layout [I]: cells of the font's
// cell size, `size` units tall, centred (align 0x22).
void HudView::drawMessage(Canvas& canvas, float nowMs) {
    const float age = nowMs - messageMs_;
    if (message_.empty() || age < 0 || age > kMessageMs) return;
    const scene::Text* text = hud_->find<scene::Text>("message0.txt");
    if (!text) return;
    pose_.setFrame(hud_->find("message0.tnm"), age / kMessageMs * 100);
    const scene::Font* font = text->dir ? text->dir->resolve<scene::Font>(text->font) : nullptr;
    const scene::Mat* mat = font && font->dir ? font->dir->resolve<scene::Mat>(font->mat) : nullptr;
    const scene::Tex* tex = mat && mat->dir ? mat->dir->resolve<scene::Tex>(mat->diffuseTex) : nullptr;
    if (!font || !tex || font->cellSize[1] <= 0 || font->texWidth == 0 || font->texHeight == 0) return;
    const float aspect = font->cellSize[0] / font->cellSize[1];
    const float h = text->size;
    float width = 0;
    for (unsigned char ch : message_) width += font->glyphs[ch].advance * h * aspect;
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
    for (unsigned char ch : message_) {
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

void HudView::draw(gfx::Renderer& r, gfx::SceneCache& cache, Canvas& canvas, const Player& player, float nowMs) {
    if (!hud_) return;
    device_ = &r.device();
    cache_ = &cache;
    int vx, vy;
    r.pictureRect(vx, vy, viewW_, viewH_);
    drive(player, nowMs);
    // The HUD draws over everything (the UI camera's depth range 0..0.02).
    r.device().clear(nullptr, true);
    frameSet_ = false;
    currentEnv_ = nullptr;
    gfx::MaterialRenderer& mr = r.materials();
    drawDir(mr, cache, hud_->root(), scene::Xfm{}, nullptr);
    drawMessage(canvas, nowMs);
}
