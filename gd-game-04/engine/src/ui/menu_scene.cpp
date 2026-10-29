// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/menu_scene.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <exception>

#include "data/disc.h"
#include "data/qbkey.h"
#include "data/venue.h"
#include "game/chart.h"
#include "render/canvas.h"
#include "render/renderer.h"
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

MenuScene::~MenuScene() = default;

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

bool MenuScene::ensureVenue() {
    if (!zone_ || zone_ == K("none")) return false;
    if (zone_ != loadedZone_) {
        view_.reset(), venue_.reset();
        loadedZone_ = zone_, failed_ = false;
        const std::string zone = qbkey::name(zone_);
        try {
            venue_ = venue::load(disc_, zone);
        } catch (const std::exception& ex) {
            SDL_Log("menu scene %s: %s", zone.c_str(), ex.what());
        }
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
    if (!ensureVenue()) return;
    if (!view_ && !failed_) {
        const std::string zone = qbkey::name(zone_);
        view_ = std::make_unique<VenueView>();
        std::string error;
        if (!view_->init(c.renderer().device(), disc_, *venue_, {}, error)) {
            SDL_Log("menu scene %s: %s", zone.c_str(), error.c_str());
            view_.reset(), failed_ = true;
            return;
        }
        SDL_Log("menu scene: %s", zone.c_str());
    }
    if (failed_ || !view_) return;
    const Pose p = pose();
    const venue::CamPose cam{p.pos, p.rot, p.fov};
    lights_.evaluate(clockMs_, lightState_);
    c.flush();
    view_->setTime(clockMs_ / 1000.0);
    view_->draw(cam, lightState_, lights_);
    objects_->draw(c.renderer().device(), cam, *venue_, lightState_, lights_);
    view_->drawVolumes(cam, lights_);
}
