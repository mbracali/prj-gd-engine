// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/menu_scene.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

#include "data/disc.h"
#include "data/qbkey.h"
#include "data/venue.h"
#include "game/chart.h"
#include "render/canvas.h"
#include "render/gh3_shader.h"
#include "render/hitch.h"
#include "render/renderer.h"
#include "render/venue_stage.h"
#include "render/venue_view.h"
#include "script/globals.h"
#include "script/vm.h"
#include "ui/menu_objects.h"
#include "ui/ui_system.h"

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

// The screen elements' morph curves [C 0x822A6B28].
float ease(int motion, float t) {
    switch (motion) {
    case 1: return t * t * (3 - 2 * t);
    case 2: return t * t;
    case 3: return t * (2 - t);
    default: return t;
    }
}

venue::Quat slerp(venue::Quat a, venue::Quat b, float t) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0) {
        for (float& x : b) x = -x;
        d = -d;
    }
    venue::Quat r;
    if (d > 0.9995f) {
        for (int i = 0; i < 4; ++i) r[size_t(i)] = a[size_t(i)] + (b[size_t(i)] - a[size_t(i)]) * t;
    } else {
        const float th = std::acos(d), s = std::sin(th), wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
        for (int i = 0; i < 4; ++i) r[size_t(i)] = a[size_t(i)] * wa + b[size_t(i)] * wb;
    }
    const float n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    if (n > 0)
        for (float& x : r) x /= n;
    return r;
}

const qb::Value* field(const qb::Struct& p, uint32_t name) {
    if (const qb::Value* v = qs::Vm::param(p, name)) return v;
    // `ccam_domorph { ( $ store_camera_props . main_store_menu ) time = .. }`:
    // the pose comes as an unnamed struct.
    for (const qb::Item& it : p.items)
        if (it.name == 0 && it.value.type == qb::Type::Struct && it.value.st)
            if (const qb::Value* v = qs::Vm::param(*it.value.st, name)) return v;
    return nullptr;
}

}  // namespace

MenuScene::MenuScene(const Disc& disc, Globals& globals, qs::Vm& vm, ui::UiSystem& ui) : disc_(disc), globals_(globals), vm_(vm), ui_(ui) {
    objects_ = std::make_unique<MenuObjects>(disc_, globals_, vm_, [this](uint32_t name, venue::Vec3& pos, venue::Quat& rot) {
        const venue::Node* n = ensureVenue() ? venue_->node(name) : nullptr;
        if (!n) return false;
        pos = n->pos, rot = n->rot;
        return true;
    });
}

MenuScene::~MenuScene() {
    dropPending();
    for (auto& [f, v] : discard_) f.wait();
    releaseGpu();
}

void MenuScene::dropPending() {
    if (prepareWork_.valid()) discard_.emplace_back(std::move(prepareWork_), std::move(pendingView_));
    pendingView_.reset();
}

void MenuScene::releaseGpu() {
    fx_.shutdown();
    if (dev_ && shadow_) dev_->destroyTarget(shadow_);
    shadow_ = {}, fxTried_ = fxReady_ = false, dev_ = nullptr;
}

// The characters' dynamic shadow: as VenueStage::shadowPass (the same
// light-space box, 1024 x 1024 depth map, bias and intensity [I there]),
// from SetShadowDirFromLight's light, else the strongest band-group
// directional light pointing down [I].
void MenuScene::shadowPass(gfx::RenderDevice& dev) {
    view_->setShadow({}, gfx::Mat4{}, 1024, 0.35f);
    objects_->setShadow({}, gfx::Mat4{}, 1024, 0.35f);
    venue::Vec3 centre;
    if (!shadow_ || !objects_->centre(centre)) return;
    const venue::Venue& v = *venue_;
    int best = -1;
    float bestPower = 0;
    for (size_t i = 0; i < v.lights.size() && i < lightState_.size(); ++i) {
        const venue::Light& l = v.lights[i];
        if (objects_->shadowLight) {
            if (l.name == objects_->shadowLight) best = int(i);
            continue;
        }
        if (l.type != venue::LightType::Directional || l.group != K("band") || l.dir[1] > -0.2f || !l.createdAtStart) continue;
        const venue::LightState& st = lightState_[i];
        const float pw = st.intensity * (st.color[0] + st.color[1] + st.color[2]);
        if (pw > bestPower) bestPower = pw, best = int(i);
    }
    static const bool log = SDL_getenv("GD10_SHADOW_LOG") != nullptr;  // development aid
    if (log)
        SDL_Log("menu shadow: light %s, centre %.2f %.2f %.2f", best >= 0 ? qbkey::name(v.lights[size_t(best)].name).c_str() : "none",
                centre[0], centre[1], centre[2]);
    if (best < 0) return;
    const venue::Vec3 d = v.lights[size_t(best)].dir;
    const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dl < 1e-6f) return;
    const venue::Vec3 f{d[0] / dl, d[1] / dl, d[2] / dl};
    venue::Vec3 r{f[2], 0, -f[0]};
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (rl < 1e-4f) r = {1, 0, 0};
    else r[0] /= rl, r[2] /= rl;
    const venue::Vec3 u{f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    const float back = 30.0f, halfSize = 7.0f, depth = 60.0f;
    const venue::Vec3 eye{centre[0] - f[0] * back, centre[1] + 1.0f - f[1] * back, centre[2] - f[2] * back};
    gfx::Mat4 view, proj;
    const venue::Vec3 rows[3] = {r, u, f};
    for (int a = 0; a < 3; ++a) {
        for (int c = 0; c < 3; ++c) view.at(a, c) = rows[a][size_t(c)];
        view.at(a, 3) = -(rows[a][0] * eye[0] + rows[a][1] * eye[1] + rows[a][2] * eye[2]);
    }
    proj.at(0, 0) = 1.0f / halfSize, proj.at(1, 1) = 1.0f / halfSize, proj.at(2, 2) = 1.0f / depth;
    const gfx::Mat4 vp = proj * view;
    gfx::Mat4 casterProj = proj;
    casterProj.at(1, 1) = -casterProj.at(1, 1);
    const gfx::Mat4 casterVp = casterProj * view;
    gh3shader::Globals g;
    g.setMatrix(gh3shader::kRegView, view, 4);
    g.setMatrix(gh3shader::kRegProjection, casterProj, 4);
    g.setMatrix(gh3shader::kRegViewProj, casterVp, 4);
    g.setMatrix(gh3shader::kRegInvView, view.inverseAffine(), 4);
    g.set(gh3shader::kRegDistanceLodScaler, 1, 0, 0, 0);
    const gfx::TargetId oldTarget = dev.currentTarget();
    int vx, vy, vw, vh;
    dev.currentViewport(vx, vy, vw, vh);
    dev.setTarget(shadow_);
    dev.setViewport(0, 0, 1024, 1024);
    const float white[4] = {1, 1, 1, 1};
    dev.clear(white, true);
    objects_->drawShadow(g, v, lightState_, lights_);
    dev.setTarget(oldTarget);
    dev.setViewport(vx, vy, vw, vh);
    gfx::Mat4 bias;
    bias.at(2, 3) = -0.04f / depth;
    const gfx::Mat4 recv = bias * vp;
    view_->setShadow(dev.targetDepth(shadow_), recv, 1024, 0.35f);
    objects_->setShadow(dev.targetDepth(shadow_), recv, 1024, 0.35f);
}

// A camera pose: pos, Quat (x, y, z; w >= 0) and FOV, LockTo world
// (the menus' cameras) [C data: store_camera_props, ch_view_cam].
bool MenuScene::readPose(const void* params, Pose& out) {
    const qb::Struct& p = *static_cast<const qb::Struct*>(params);
    const qb::Value* pos = field(p, K("pos"));
    const qb::Value* q = field(p, K("quat"));
    if (!pos || pos->type != qb::Type::Vector || !q || q->type != qb::Type::Vector) return false;
    out.pos = {pos->f[0], pos->f[1], pos->f[2]};
    const float w2 = 1 - q->f[0] * q->f[0] - q->f[1] * q->f[1] - q->f[2] * q->f[2];
    out.rot = {q->f[0], q->f[1], q->f[2], std::sqrt(std::max(0.0f, w2))};
    if (const qb::Value* f = field(p, K("fov"))) out.fov = f->number();
    // LockTo <object> [locktobone <bone>]: pos and Quat in its frame
    // (guitar_select_camera_morph: the guitarist's BONE_GUITAR_NECK) [C scripts].
    out.lockTo = out.bone = 0;
    if (const qb::Value* l = field(p, K("lockto")); l && (l->type == qb::Type::Key || l->type == qb::Type::KeyPtr) && l->key != K("world"))
        out.lockTo = l->key;
    if (const qb::Value* b = field(p, K("locktobone")); b && (b->type == qb::Type::Key || b->type == qb::Type::KeyPtr)) out.bone = b->key;
    return true;
}

void MenuScene::setSuspended(bool on) { objects_->setSuspended(on); }

// A pose in the world: one locked to an object follows its (bone's) frame
// [I: as the songs' LockTo, render/venue_camera.cpp].
MenuScene::Pose MenuScene::world(const Pose& p) {
    if (!p.lockTo) return p;
    venue::Vec3 fp;
    venue::Quat fr;
    if (!objects_->frame(p.lockTo, p.bone, fp, fr)) return p;
    Pose w = p;
    w.lockTo = w.bone = 0;
    const venue::Vec3 r = venue::rotate(fr, p.pos);
    w.pos = {fp[0] + r[0], fp[1] + r[1], fp[2] + r[2]};
    w.rot = venue::mul(fr, p.rot);
    return w;
}

bool MenuScene::ensureVenue(bool wait) {
    if (!zone_ || zone_ == K("none")) return false;
    if (zone_ != loadedZone_) {
        // The zone read on a worker (no-stutter rule); the draw goes on
        // without it meanwhile, the scripts' node lookups wait for it.
        if (loadingZone_ != zone_) {
            if (venueWork_.valid()) venueDiscard_.push_back(std::move(venueWork_));
            loadingZone_ = zone_;
            const Disc* d = &disc_;
            const std::string zone = qbkey::name(zone_);
            venueWork_ = std::async(std::launch::async, [d, zone]() -> std::shared_ptr<venue::Venue> {
                try {
                    return std::shared_ptr<venue::Venue>(venue::load(*d, zone));
                } catch (const std::exception& ex) {
                    SDL_Log("menu scene %s: %s", zone.c_str(), ex.what());
                }
                return nullptr;
            });
        }
        if (!wait && venueWork_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        dropPending();
        view_.reset();
        hitch::Scope hs("menu scene load");
        venue_ = venueWork_.get();
        loadedZone_ = zone_, loadingZone_ = 0, failed_ = false;
        if (!venue_) {
            failed_ = true;
            return false;
        }
        lights_ = {};
        lights_.init(*venue_, globals_, Chart{});
    }
    return venue_ != nullptr;
}

MenuScene::Pose MenuScene::pose() {
    const float k = ease(motion_, duration_ > 0 ? float(std::min(1.0, t_ / duration_)) : 1.0f);
    const Pose a = world(from_), b = world(to_);
    Pose p;
    for (int i = 0; i < 3; ++i) p.pos[size_t(i)] = a.pos[size_t(i)] + (b.pos[size_t(i)] - a.pos[size_t(i)]) * k;
    p.rot = slerp(a.rot, b.rot, k);
    p.fov = a.fov + (b.fov - a.fov) * k;
    return p;
}

void MenuScene::addNatives() {
    using qs::ScriptRun;
    using qs::Vm;
    objects_->addNatives();
    // Create / Kill {name = node}: a node of the zone's array made or removed
    // (misclightutils.qb SafeCreate; the store's Kill of
    // Z_SoundCheck_G_RMM_Kill_me) [C scripts; I: drawn as shown / hidden].
    vm_.addNative("Create", [this](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* n = Vm::param(p, K("name"))) nodeVis_[n->key] = 1;
        return true;
    });
    vm_.addNative("Kill", [this](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* n = Vm::param(p, K("name"))) nodeVis_[n->key] = 0;
        return true;
    });
    // EnableLightGroup / DisableLightGroup {name = group}: the store's
    // Guitar_Center_Band lights on the shadow character (monitor_store_shadow_character) [C scripts].
    vm_.addNative("EnableLightGroup", [this](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* n = Vm::param(p, K("name"))) lightsOff_.erase(n->key);
        return true;
    });
    vm_.addNative("DisableLightGroup", [this](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* n = Vm::param(p, K("name"))) lightsOff_.insert(n->key);
        return true;
    });
    // PlayIGCCam {name viewport [controlscript] [pos Quat FOV] [time]}: a
    // camera on the background viewport, held; its control script (spawned)
    // moves it with ccam_domorph [C scripts]. Other viewports (the song's
    // in-game cameras) are not drawn here.
    vm_.addNative("PlayIGCCam", [this](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* vp = Vm::param(p, K("viewport"));
        if (!vp || vp->key != K("bg_viewport")) return true;
        // guitar_startup's dummy_cam_bg only gives the boot's empty viewport
        // a camera: nothing of the zone shows before a menu asks [I].
        if (const qb::Value* nm = Vm::param(p, K("name")); nm && nm->key == K("dummy_cam_bg")) return true;
        if (controlRun_) v.kill(controlRun_), controlRun_ = 0;
        Pose target;
        if (readPose(&p, target)) {
            from_ = playing_ ? pose() : target, to_ = target;
            duration_ = Vm::number(Vm::param(p, K("time")), 0) * 1000.0, t_ = 0, motion_ = 0;
            if (!playing_) from_ = target;
        }
        playing_ = true;
        if (const qb::Value* cs = Vm::param(p, K("controlscript")); cs && (cs->type == qb::Type::Key || cs->type == qb::Type::KeyPtr))
            controlRun_ = v.spawn(cs->key, qb::Struct{});
        return true;
    });
    // ccam_domorph {<pose> [time] [motion]}: the camera moves to the pose
    // (at once without a time).
    vm_.addNative("ccam_domorph", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Pose target;
        if (!readPose(&p, target)) return false;
        from_ = playing_ ? pose() : target, to_ = target, t_ = 0;
        duration_ = Vm::number(field(p, K("time")), 0) * 1000.0;
        motion_ = 0;
        if (const qb::Value* mo = field(p, K("motion"))) {
            if (mo->key == K("smooth")) motion_ = 1;
            else if (mo->key == K("ease_in")) motion_ = 2;
            else if (mo->key == K("ease_out")) motion_ = 3;
        }
        playing_ = true;
        return true;
    });
    // ccam_waitmorph: the script waits for the move's end.
    vm_.addNative("ccam_waitmorph", [this](Vm&, ScriptRun& run, qb::Struct&) {
        run.waitFor = [this] { return t_ >= duration_; };
        return true;
    });
    // ccam_freeze: the camera stays where it is (unlocked) [I].
    vm_.addNative("ccam_freeze", [this](Vm&, ScriptRun&, qb::Struct&) {
        if (playing_) from_ = to_ = pose(), t_ = duration_ = 0;
        return true;
    });
    // KillCamAnim {name}: the camera stops (its control script ends).
    vm_.addNative("KillCamAnim", [this](Vm& v, ScriptRun&, qb::Struct&) {
        if (controlRun_) v.kill(controlRun_), controlRun_ = 0;
        return true;
    });
}

void MenuScene::update(double dtMs) {
    clockMs_ += dtMs;
    if (t_ < duration_) t_ = std::min(duration_, t_ + dtMs);
    objects_->update(dtMs);
}

void MenuScene::draw(Canvas& c) {
    if (!playing_ || !zone_ || zone_ == K("none")) return;
    // Only while the background viewport element is up.
    const ui::Element* e = ui_.find(K("bg_viewport"));
    for (const ui::Element* p = e; p; p = p->parent)
        if (p->hidden) return;
    if (!e || ui_.loadingScreenUp()) return;
    // Under a menu backdrop (create_menu_backdrop's full-screen picture: the
    // boot, main and options menus) nothing of it shows: not drawn (nor
    // loaded) then [I: a shortcut, the 360 draws it anyway].
    if (const ui::Element* b = ui_.find(K("menu_backdrop"))) {
        float a = 1;
        bool shown = true;
        for (const ui::Element* p = b; p; p = p->parent) a *= p->alpha, shown = shown && !p->hidden;
        if (shown && a >= 0.999f && b->rgba[3] == 255) return;
    }
    if (!ensureVenue(false)) return;  // (loading: nothing of it yet)
    discard_.erase(std::remove_if(discard_.begin(), discard_.end(),
                                  [](auto& d) { return d.first.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }),
                   discard_.end());
    venueDiscard_.erase(std::remove_if(venueDiscard_.begin(), venueDiscard_.end(),
                                       [](auto& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }),
                        venueDiscard_.end());
    if (!view_ && !failed_) {
        // The zone's view: read and decoded on a worker, then uploaded and
        // compiled a slice a frame; nothing of it draws until it is in (a
        // zone's view had held the menus ~1 s).
        if (!pendingView_) {
            pendingView_ = std::make_shared<VenueView>();
            std::shared_ptr<VenueView> pv = pendingView_;
            std::shared_ptr<const venue::Venue> v = venue_;
            const Disc* d = &disc_;
            prepareWork_ = std::async(std::launch::async, [pv, v, d] { pv->prepare(*d, *v); });
        }
        if (prepareWork_.valid()) {
            if (prepareWork_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            prepareWork_.get();
        }
        const std::string zone = qbkey::name(zone_);
        std::string error;
        hitch::Scope hs("menu scene view");
        gfx::RenderDevice& dev = c.renderer().device();
        if (!fxTried_) {
            // bg_viewport's screen effects and the shadow map, with the first zone.
            fxTried_ = true, dev_ = &dev;
            fxReady_ = !SDL_getenv("GD10_NO_SCREENFX") && fx_.init(dev, gh3shader::library(disc_));
            screenfx::readViewport(globals_, K("Default_TOD_Manager"), fxParams_);
            if (!SDL_getenv("GD10_NO_SHADOWS")) {
                std::string serr;
                gfx::TargetDesc td;
                td.width = td.height = 1024;
                td.depth = true;
                shadow_ = dev.createTarget(td, serr);
                if (!shadow_) SDL_Log("menu scene: no shadow map: %s", serr.c_str());
            }
            return;
        }
        if (fxReady_ && !fx_.settle()) return;  // its programs a slice a frame
        const VenueView::InitStep r = pendingView_->initStep(dev, disc_, *venue_, {}, error);
        if (r == VenueView::InitStep::More) return;
        if (r == VenueView::InitStep::Failed) {
            SDL_Log("menu scene %s: %s", zone.c_str(), error.c_str());
            pendingView_.reset(), failed_ = true;
            return;
        }
        view_ = std::move(pendingView_);
        SDL_Log("menu scene: %s", zone.c_str());
    }
    if (failed_ || !view_) return;
    const Pose p = pose();
    const venue::CamPose cam{p.pos, p.rot, p.fov};
    lights_.evaluate(clockMs_, lightState_);
    for (size_t i = 0; i < venue_->lights.size() && i < lightState_.size(); ++i)
        if (lightsOff_.count(venue_->lights[i].group)) lightState_[i].intensity = 0;
    {
        const auto* vis = &nodeVis_;
        view_->setNodeVisibility([vis](uint32_t node) {
            auto it = vis->find(node);
            return it == vis->end() ? -1 : it->second;
        });
    }
    c.flush();
    gfx::RenderDevice& dev = c.renderer().device();
    objects_->prepareFrame(dev);
    shadowPass(dev);
    const bool fx = fxReady_ && fx_.begin();
    VenueView::jitterX = VenueView::jitterY = 0;  // a song's Enhanced TAA jitter (statics)
    view_->setTime(clockMs_ / 1000.0);
    view_->draw(cam, lightState_, lights_);
    objects_->draw(dev, cam, *venue_, lightState_, lights_);
    view_->drawVolumes(cam, lights_);
    if (fx) {
        // Enhanced: Remove All Effects drops GH3's own; Bloom None its bloom (as the songs').
        screenfx::Params p = fxParams_;
        if (!VenueStage::post.original) p.bloom.on = false, p.brightSat[0].on = p.brightSat[1].on = false;
        if (!VenueStage::post.gameBloom) p.bloom.on = false;
        p.nearZ = 0.05f, p.farZ = 4000.0f;  // VenueView's projection
        fx_.end(p);
    }
}
