// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/menu_scene.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
    ui_.pointerToViewport = [this](uint32_t vp, float x, float y, float& vx, float& vy) { return pointerToViewport(vp, x, y, vx, vy); };
    objects_ = std::make_unique<MenuObjects>(disc_, globals_, vm_, [this](uint32_t name, venue::Vec3& pos, venue::Quat& rot) {
        const venue::Node* n = ensureVenue() ? venue_->node(name) : nullptr;
        if (!n) return false;
        pos = n->pos, rot = n->rot;
        return true;
    });
}

MenuScene::~MenuScene() {
    ui_.pointerToViewport = nullptr;
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
    static const bool log = SDL_getenv("GD07_SHADOW_LOG") != nullptr;  // development aid
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
    const qb::Struct* pp = static_cast<const qb::Struct*>(params);
    // `ccam_domorph { ($jam_camera_props.<prop>) }`: the pose as an unnamed
    // struct in the call (the music studio's jam_camera_script) [C script];
    // read from it when the call has no pos itself (GD07 bug pass).
    if (!field(*pp, K("pos")) && !SDL_getenv("GD07_POSE_STRUCT_LEGACY"))
        for (const qb::Item& it : pp->items)
            if (!it.name && it.value.st && field(*it.value.st, K("pos"))) {
                pp = it.value.st.get();
                break;
            }
    const qb::Struct& p = *pp;
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
    // screenoffset: an asymmetric-frustum lens shift (x right, y down, in
    // half-screens) that pushes the look-at off-centre so the band sits
    // low-left and the menu list upper-right (ref 01_main_menu.png). The port
    // had silently dropped it, aiming the camera at the venue's back wall
    // (GW-002 wrong framing / GW-003 looks backdrop-less). Fed into the
    // frustum's lens shift at draw(); GD07_MENU_FRAMING_LEGACY=1 drops it.
    out.screenOffset[0] = out.screenOffset[1] = 0;
    if (const qb::Value* so = field(p, K("screenoffset")); so && (so->type == qb::Type::Pair || so->type == qb::Type::Vector))
        out.screenOffset[0] = so->f[0], out.screenOffset[1] = so->f[1];
    if (SDL_getenv("GD07_SCENELOG"))
        SDL_Log("menu readPose: pos (%.3f %.3f %.3f) quat (%.4f %.4f %.4f) fov %.3f screenoffset (%.4f %.4f) lockto %s bone %s",
                out.pos[0], out.pos[1], out.pos[2], out.rot[0], out.rot[1], out.rot[2], out.fov, out.screenOffset[0], out.screenOffset[1],
                out.lockTo ? qbkey::name(out.lockTo).c_str() : "world", out.bone ? qbkey::name(out.bone).c_str() : "-");
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
        // Z_soundcheck_Init spawns Z_Soundcheck_LightShow, whose first
        // CycleNextSnapshot fires at once (then every 2 s) [C]; arm it now.
        nextSnapMs_ = clockMs_;
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
    for (int i = 0; i < 2; ++i) p.screenOffset[i] = a.screenOffset[i] + (b.screenOffset[i] - a.screenOffset[i]) * k;
    return p;
}

void MenuScene::addNatives() {
    using qs::ScriptRun;
    using qs::Vm;
    objects_->addNatives();
    // Level objects of the zone (`<node> : obj_movetorelpos ...`): after the
    // composite objects (MenuObjects), before the screen elements.
    {
        auto next = vm_.objectCall;
        vm_.objectCall = [this, next](Vm& v, ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled) -> bool {
            const venue::Node* node = venue_ ? venue_->node(obj) : nullptr;
            if (!node) return next ? next(v, run, obj, method, p, handled) : (handled = false);
            handled = true;
            const double t = clockMs_;
            auto vecOf = [](const qb::Value* pv, venue::Vec3& out) {
                if (!pv || (pv->type != qb::Type::Vector && pv->type != qb::Type::Pair)) return false;
                out = {pv->f[0], pv->f[1], pv->type == qb::Type::Vector ? pv->f[2] : 0.0f};
                return true;
            };
            auto cur = [&]() {
                auto it = moves_.find(obj);
                return it == moves_.end() ? node->pos : it->second.pos(t);
            };
            auto timed = [&](NodeMove& m, const venue::Vec3& dst) {
                const venue::Vec3 c = cur();
                const float time = Vm::number(Vm::param(p, K("time")), 0), speed = Vm::number(Vm::param(p, K("Speed")), 0);
                const float dx = dst[0] - c[0], dy = dst[1] - c[1], dz = dst[2] - c[2];
                m.from = c, m.to = dst, m.m0 = t;
                m.dur = time > 0 ? time * 1000.0 : speed > 0 ? std::sqrt(dx * dx + dy * dy + dz * dz) / speed * 1000.0 : 0.0;
            };
            if (method == K("obj_movetorelpos")) {
                // An offset in the node's frame [I: as the songs' (render/particles.cpp)].
                venue::Vec3 off;
                if (!vecOf(Vm::positional(p), off)) return true;
                off = venue::rotate(node->rot, off);
                const venue::Vec3 c = cur();
                NodeMove m;
                timed(m, {c[0] + off[0], c[1] + off[1], c[2] + off[2]});
                moves_[obj] = m;
            } else if (method == K("obj_movetopos") || method == K("obj_setposition")) {
                const qb::Value* pv = Vm::param(p, K("position"));
                if (!pv) pv = Vm::positional(p);
                venue::Vec3 dst;
                if (!vecOf(pv, dst)) return true;
                NodeMove m;
                timed(m, dst);
                if (method == K("obj_setposition")) m.dur = 0;
                moves_[obj] = m;
            } else if (method == K("obj_stopmoving")) {
                const venue::Vec3 c = cur();
                moves_[obj] = NodeMove{c, c, t, 0};
            } else if (method == K("Obj_WaitMove")) {
                run.waitFor = [this, obj] {
                    auto it = moves_.find(obj);
                    return it == moves_.end() || it->second.dur <= 0 || clockMs_ >= it->second.m0 + it->second.dur;
                };
            } else if (method == K("obj_locktoobject")) {
                if (const qb::Value* o = Vm::param(p, K("objectname")); o && (o->type == qb::Type::Key || o->type == qb::Type::KeyPtr))
                    locks_[obj] = o->key;
            } else {
                return next ? next(v, run, obj, method, p, handled) : (handled = false);
            }
            return true;
        };
    }
    // Create / Kill {name = node}: a node of the zone's array made or removed
    // (misclightutils.qb SafeCreate; the store's Kill of
    // Z_SoundCheck_G_RMM_Kill_me) [C scripts; I: drawn as shown / hidden].
    // A killed and re-created object is back where the level placed it
    // (z_soundcheck_UIResetPos kills and creates the sound stage's props before
    // each Move_restart lowers them again) [C scripts; I].
    // {prefix = <name start>}: every node so named (z_soundcheck_FrontDragons'
    // Kill / Create, CallScriptOnNode's speaker cones) [C scripts].
    auto byPrefix = [this](const qb::Value* pre) {
        std::vector<uint32_t> out;
        if (!pre || !ensureVenue()) return out;
        std::string prefix = pre->type == qb::Type::String ? pre->str : qbkey::name(pre->key);
        auto lower = [](std::string x) {
            for (char& ch : x) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            return x;
        };
        prefix = lower(prefix);
        if (prefix.empty()) return out;
        for (const venue::Node& n : venue_->nodes)
            if (lower(qbkey::name(n.name)).compare(0, prefix.size(), prefix) == 0) out.push_back(n.name);
        return out;
    };
    static const bool nodesLegacy = SDL_getenv("GD07_MENU_NODES_LEGACY") != nullptr;
    vm_.addNative("Create", [this, byPrefix](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* n = Vm::param(p, K("name"))) nodeVis_[n->key] = 1, moves_.erase(n->key), locks_.erase(n->key);
        if (const qb::Value* n = Vm::param(p, K("name")); n && SDL_getenv("GD07_SCENELOG")) SDL_Log("menu scene: %.0f ms Create %s", clockMs_, qbkey::name(n->key).c_str());
        if (!nodesLegacy)
            for (uint32_t k : byPrefix(Vm::param(p, K("prefix")))) nodeVis_[k] = 1, moves_.erase(k), locks_.erase(k);
        return true;
    });
    vm_.addNative("Kill", [this, byPrefix](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* n = Vm::param(p, K("name"))) nodeVis_[n->key] = 0, moves_.erase(n->key), locks_.erase(n->key);
        if (const qb::Value* n = Vm::param(p, K("name")); n && SDL_getenv("GD07_SCENELOG")) SDL_Log("menu scene: %.0f ms Kill %s", clockMs_, qbkey::name(n->key).c_str());
        if (!nodesLegacy)
            for (uint32_t k : byPrefix(Vm::param(p, K("prefix")))) nodeVis_[k] = 0, moves_.erase(k), locks_.erase(k);
        return true;
    });
    if (!nodesLegacy) {
        // IsInNodeArray <name> / IsCreated <name>: the zone has the node / it
        // is shown (made at start, or by Create since). SafeCreate / SafeKill
        // test both [C script misclightutils.qb]: always false before, the
        // main menu's plane and projector never came, the props never went
        // (GD07 bug pass). GD07_MENU_NODES_LEGACY=1: always false.
        auto nameOf = [](qb::Struct& p) -> uint32_t {
            if (const qb::Value* n = Vm::param(p, K("name"))) return n->key;
            for (const qb::Item& it : p.items)
                if (!it.name && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr)) return it.value.key;
            return 0;
        };
        vm_.addNative("IsInNodeArray", [this, nameOf](Vm&, ScriptRun&, qb::Struct& p) {
            const uint32_t n = nameOf(p);
            return n && ensureVenue() && venue_->node(n) != nullptr;
        });
        vm_.addNative("IsCreated", [this, nameOf](Vm&, ScriptRun&, qb::Struct& p) {
            const uint32_t n = nameOf(p);
            if (SDL_getenv("GD07_SCENELOG")) {
                std::string it;
                for (const qb::Item& i2 : p.items) it += " " + (i2.name ? qbkey::name(i2.name) : std::string("_")) + ":" + std::to_string(int(i2.value.type)) + "=" + qbkey::name(i2.value.key);
                auto f = nodeVis_.find(n);
                SDL_Log("menu scene: IsCreated %s ->%s vis %d", qbkey::name(n).c_str(), it.c_str(), f == nodeVis_.end() ? -1 : f->second);
            }
            if (!n || !ensureVenue()) return false;
            if (auto it = nodeVis_.find(n); it != nodeVis_.end()) return it->second != 0;
            const venue::Node* nd = venue_->node(n);
            return nd && nd->createdAtStart;
        });
        // CallScriptOnNode {prefix callback_script params}: the script for each
        // node so named, with nodeName, each in its own run (the speaker
        // cones' moves wait) [C scripts z_soundcheck_speakers_*; I: spawned].
        vm_.addNative("CallScriptOnNode", [this, byPrefix](Vm& v, ScriptRun&, qb::Struct& p) {
            uint32_t cb = 0;
            for (const char* k : {"callback_script", "callBack_Script"})
                if (const qb::Value* c = Vm::param(p, K(k)); c && !cb) cb = c->key;
            if (!cb) return true;
            qb::Struct base;
            if (const qb::Value* ps = Vm::param(p, K("params")); ps && ps->st) base = *ps->st;
            if (SDL_getenv("GD07_SCENELOG")) {
                const qb::Value* pre = Vm::param(p, K("prefix"));
                SDL_Log("menu scene: %.0f ms CallScriptOnNode %s -> %s (%zu nodes)", clockMs_, pre ? qbkey::name(pre->key).c_str() : "-", qbkey::name(cb).c_str(),
                        byPrefix(pre).size());
            }
            for (uint32_t k : byPrefix(Vm::param(p, K("prefix")))) {
                qb::Struct args = base;
                Vm::set(args, K("nodeName"), Vm::makeKey(k));
                if (const uint32_t id = v.spawn(cb, args)) v.runNow(id);
            }
            return true;
        });
    }
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
        if (SDL_getenv("GD07_SCENELOG")) {
            const qb::Value* nm = Vm::param(p, K("name"));
            const qb::Value* cs = Vm::param(p, K("controlscript"));
            SDL_Log("menu scene: PlayIGCCam %s control %s", nm ? qbkey::name(nm->key).c_str() : "-", cs ? qbkey::name(cs->key).c_str() : "-");
        }
        if (controlRun_) v.kill(controlRun_), controlRun_ = 0;
        Pose target;
        if (readPose(&p, target)) {
            from_ = playing_ ? pose() : target, to_ = target;
            duration_ = Vm::number(Vm::param(p, K("time")), 0) * 1000.0, t_ = 0, motion_ = 0;
            if (!playing_) from_ = target;
        }
        playing_ = true;
        static const bool camLegacy = SDL_getenv("GD07_IGCCAM_LEGACY") != nullptr;
        camName_ = Vm::param(p, K("name")) ? Vm::param(p, K("name"))->key : 0;
        if (const qb::Value* cs = Vm::param(p, K("controlscript")); cs && (cs->type == qb::Type::Key || cs->type == qb::Type::KeyPtr)) {
            controlRun_ = v.spawn(cs->key, qb::Struct{});
            // The control script's first pose at once (the music studio's
            // jam_camera_script: a pose-less PlayIGCCam) [I] (GD07 bug pass).
            if (controlRun_ && !camLegacy) v.runNow(controlRun_);
        }
        return true;
    });
    // ccam_domorph {<pose> [time] [motion]}: the camera moves to the pose
    // (at once without a time).
    vm_.addNative("ccam_domorph", [this](Vm&, ScriptRun&, qb::Struct& p) {
        Pose target;
        const bool ok = readPose(&p, target);
        if (SDL_getenv("GD07_SCENELOG")) {
            std::string names;
            for (const qb::Item& it : p.items) names += " " + (it.name ? qbkey::name(it.name) : std::string("<unnamed ") + std::to_string(int(it.value.type)) + ">");
            SDL_Log("menu scene: ccam_domorph %s:%s pos (%.2f %.2f %.2f)", ok ? "ok" : "no pose", names.c_str(), target.pos[0], target.pos[1], target.pos[2]);
        }
        if (!ok) return false;
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
    vm_.addNative("KillCamAnim", [this](Vm& v, ScriptRun&, qb::Struct& p) {
        // {name = <cam>}: only that camera's (another name leaves the current
        // one running) [I] (GD07 bug pass). GD07_IGCCAM_LEGACY=1: any.
        static const bool camLegacy = SDL_getenv("GD07_IGCCAM_LEGACY") != nullptr;
        if (const qb::Value* nm = Vm::param(p, K("name")); !camLegacy && nm && camName_ && nm->key != camName_) return true;
        if (SDL_getenv("GD07_SCENELOG")) SDL_Log("menu scene: KillCamAnim");
        if (controlRun_) v.kill(controlRun_), controlRun_ = 0;
        return true;
    });
}

venue::Vec3 MenuScene::NodeMove::pos(double t) const {
    if (dur <= 0 || t >= m0 + dur) return to;
    const float u = float(std::max(0.0, (t - m0) / dur));
    return {from[0] + (to[0] - from[0]) * u, from[1] + (to[1] - from[1]) * u, from[2] + (to[2] - from[2]) * u};
}

// A moved node's translation now; a locked one moves with its target [I: as the songs'].
bool MenuScene::nodeDelta(uint32_t node, venue::Vec3& d, int depth) const {
    if (depth > 8 || !venue_) return false;
    if (auto it = moves_.find(node); it != moves_.end()) {
        const venue::Node* n = venue_->node(node);
        if (!n) return false;
        const venue::Vec3 p = it->second.pos(clockMs_);
        d = {p[0] - n->pos[0], p[1] - n->pos[1], p[2] - n->pos[2]};
        return true;
    }
    if (auto lk = locks_.find(node); lk != locks_.end()) return nodeDelta(lk->second, d, depth + 1);
    return false;
}

bool MenuScene::pointerToViewport(uint32_t viewport, float x, float y, float& vx, float& vy) {
    if (!view_ || !playing_) return false;
    const Pose p = pose();
    const venue::CamPose cam{p.pos, p.rot, p.fov};
    for (const auto& [id, o] : ui_.textureOverrides()) {
        (void)id;
        if (o.viewport != viewport) continue;
        float u = 0, v = 0;
        if (!view_->pickTexture(o.texture, cam, x, y, u, v)) continue;
        // The target's 1280 x 720, its rows as the UI draws them (Canvas::flipTarget) [I].
        static const bool flipV = SDL_getenv("GD07_POINTER_BOARD_FLIPV") != nullptr;  // development aid
        vx = u * 1280.0f, vy = (flipV ? 1.0f - v : v) * 720.0f;
        return true;
    }
    return false;
}

void MenuScene::drawUiViewports(Canvas& c, gfx::RenderDevice& dev) {
    // Each texture override: its viewport's windows drawn into a 1280 x 720
    // target (transparent where nothing is drawn [I: the viewport's clear]),
    // the zone's texture of that key replaced by it [C script
    // create_viewport_ui; I: the clear colour, the size from window_dims].
    std::unordered_map<uint32_t, bool> live;
    for (const auto& [id, o] : ui_.textureOverrides()) {
        (void)id;
        if (!ui_.textureViewport(o.viewport)) continue;
        gfx::TargetId& t = uiTargets_[o.texture];
        if (!t) {
            std::string err;
            gfx::TargetDesc td;
            td.width = 1280, td.height = 720;
            t = dev.createTarget(td, err);
            if (!t) {
                SDL_Log("menu scene: no UI viewport target: %s", err.c_str());
                continue;
            }
        }
        const gfx::TargetId outer = dev.currentTarget();
        int vx, vy, vw, vh;
        dev.currentViewport(vx, vy, vw, vh);
        c.flush();
        dev.setTarget(t);
        dev.setViewport(0, 0, 1280, 720);
        float clear[4] = {0, 0, 0, 0};
        dev.clear(clear, false);
        Canvas::flipTarget = true;
        const bool drawn = ui_.drawViewport(c, o.viewport);
        c.flush();
        Canvas::flipTarget = false;
        dev.setTarget(outer);
        dev.setViewport(vx, vy, vw, vh);
        view_->setOverride(o.texture, drawn ? dev.targetTexture(t) : gfx::TextureId{});
        live[o.texture] = drawn;
    }
    for (auto& [tex, t] : uiTargets_)
        if (!live.count(tex)) view_->setOverride(tex, gfx::TextureId{});
}

void MenuScene::update(double dtMs) {
    clockMs_ += dtMs;
    if (t_ < duration_) t_ = std::min(duration_, t_ + dtMs);
    // Z_Soundcheck_LightShow: step the mood's snapshots every 2 s (blend 0.1 s)
    // [C zones\z_soundcheck]. The lightshow has no notes on the frontend, so
    // the zone script drives the cycling itself; without this the menu stays on
    // the first snapshot and its one static mood reads fire-dominant instead of
    // the 360's animated purple / pink / red show (GW-00x menu mood).
    static const bool noCycle = SDL_getenv("GD07_MENU_NO_LIGHTCYCLE") != nullptr;
    if (!noCycle && nextSnapMs_ >= 0 && venue_ && loadedZone_ == zone_)
        while (clockMs_ >= nextSnapMs_) {
            lights_.cycleSnapshot(clockMs_, 0.1f);
            nextSnapMs_ += 2000.0;
        }
    objects_->update(dtMs);
}

void MenuScene::draw(Canvas& c) {
    if (SDL_getenv("GD07_SCENELOG")) {  // development aid
        static int n = 0;
        if (++n % 60 == 0) {
            const ui::Element* e = ui_.find(K("bg_viewport"));
            bool hid = false;
            for (const ui::Element* p = e; p; p = p->parent) hid = hid || p->hidden;
            SDL_Log("menu scene draw: playing %d zone %s bg_viewport %d hidden %d loading %d", int(playing_), qbkey::name(zone_).c_str(), e != nullptr,
                    int(hid), int(ui_.loadingScreenUp()));
        }
    }
    if (!playing_ || !zone_ || zone_ == K("none")) return;
    // Only while the background viewport element is up.
    const ui::Element* e = ui_.find(K("bg_viewport"));
    for (const ui::Element* p = e; p; p = p->parent)
        if (p->hidden) return;
    if (!e || ui_.loadingScreenUp()) return;
    // Under a menu backdrop (create_menu_backdrop's full-screen picture: the
    // boot, main and options menus) the port used to skip the 3D venue,
    // assuming the picture covers it. But the 360 draws the venue behind it
    // anyway (ref 01_main_menu.png: the main menu shows the full sound-stage
    // venue -- band from behind, crowd, speaker stacks, purple light, fire --
    // not a flat picture), and the port's picture is not an opaque cover here,
    // so that shortcut left the main menu black/grain (GW-003). Draw the venue
    // regardless now; GD07_MENU_BACKDROP_LEGACY=1 restores the old suppression.
    static const bool menuBackdropLegacy = SDL_getenv("GD07_MENU_BACKDROP_LEGACY") && SDL_getenv("GD07_MENU_BACKDROP_LEGACY")[0] == '1';
    if (menuBackdropLegacy)
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
            fxReady_ = !SDL_getenv("GD07_NO_SCREENFX") && fx_.init(dev, gh3shader::library(disc_));
            screenfx::readViewport(globals_, K("Default_TOD_Manager"), fxParams_);
            if (!SDL_getenv("GD07_NO_SHADOWS")) {
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
        view_->setNodeMotion([this](uint32_t node, gfx::Mat4& delta) {
            venue::Vec3 d;
            if (!nodeDelta(node, d)) return false;
            delta = gfx::Mat4::translation(d[0], d[1], d[2]);
            return true;
        });
        view_->setNodeVisibility([vis](uint32_t node) {
            auto it = vis->find(node);
            return it == vis->end() ? -1 : it->second;
        });
    }
    c.flush();
    gfx::RenderDevice& dev = c.renderer().device();
    objects_->prepareFrame(dev);
    drawUiViewports(c, dev);
    shadowPass(dev);
    const bool fx = fxReady_ && fx_.begin();
    VenueView::jitterX = VenueView::jitterY = 0;  // a song's Enhanced TAA jitter (statics)
    // screenoffset -> frustum lens shift: screenoffset x is in half-screens
    // (NDC units), the same units the lens shift adds to proj.at(0,2)
    // (venue_view.cpp:907, NDC x = sx*(x/z) + lensShiftX), so an on-axis point
    // lands at NDC x = screenoffset. This reframes the menu venue off-centre
    // (band low-left, menu list upper-right) instead of aiming at the back wall
    // (GW-002 / GW-003). GD07_MENU_FRAMING_LEGACY=1 keeps the old (dropped) path.
    static const bool framingLegacy = SDL_getenv("GD07_MENU_FRAMING_LEGACY") && SDL_getenv("GD07_MENU_FRAMING_LEGACY")[0] == '1';
    VenueView::lensShiftX = framingLegacy ? 0.0f : p.screenOffset[0];
    view_->setTime(clockMs_ / 1000.0);
    view_->draw(cam, lightState_, lights_);
    objects_->draw(dev, cam, *venue_, lightState_, lights_);
    // The venue's volumetric lighting, as the song stage draws it
    // (render/venue_stage.cpp): the projector wash, the 3D cones, the soft 2D
    // light volumes and the raytraced beams. The menu used to draw only the 3D
    // cones, so z_soundcheck's big purple stage-light haze behind the band and
    // logo (ref 01_main_menu.png: a bright magenta wash washing out the fire)
    // never came up and the main menu read dark / backdrop-less (GW-003). The
    // 2D volumes and the raytraced beams need the scene's depth once (they, and
    // the projectors, write none); depthSnapshot is valid only inside the
    // screen-fx pass (its scene target bound), so this all sits before fx_.end.
    static const bool noProjectors = SDL_getenv("GD07_NO_PROJECTORS") != nullptr;  // development aid: compare without them
    if (!noProjectors) view_->drawProjectors(cam, lights_);
    view_->drawVolumes(cam, lights_);
    const gfx::TextureId sceneDepth = fx ? fx_.depthSnapshot() : gfx::TextureId{};
    // z_soundcheck's 2D light volumes run at full density, as on the song stage
    // and the 360 frontend (ref: a bright, coloured haze). The logo banner
    // (venue node Z_SoundCheck_Plane02), its projection light and the viewport
    // planes carry `rendermethod = $pushback`: the original draws them in a
    // late pass after the scene and the volumes [C 82431B38], so the haze,
    // whose beams cross in front of the banner, never washes over them (see
    // drawPushback below). GD07_MENU_VOL2D_SCALE overrides the density.
    static const float vol2dScale = SDL_getenv("GD07_MENU_VOL2D_SCALE") ? float(std::atof(SDL_getenv("GD07_MENU_VOL2D_SCALE"))) : 1.0f;
    view_->drawVolumes2D(cam, lights_, sceneDepth, vol2dScale);
    static const bool noRays = SDL_getenv("GD07_NO_RAYTRACE") != nullptr;  // development aid: compare without the `volume` beams
    if (fx && !noRays && view_->hasRaytraced()) view_->drawRaytraced(cam, lights_, sceneDepth);
    view_->drawPushback(cam, lightState_, lights_);  // the pushback pass: after the volumes [C 82431B38]
    if (fx) {
        // Enhanced: Remove All Effects drops GH3's own; Bloom None its bloom (as the songs').
        screenfx::Params p = fxParams_;
        if (!VenueStage::post.original) p.bloom.on = false, p.brightSat[0].on = p.brightSat[1].on = false;
        if (!VenueStage::post.gameBloom) p.bloom.on = false;
        p.nearZ = 0.05f, p.farZ = 4000.0f;  // VenueView's projection
        // The active menu camera's depth of field: a submenu camera
        // (ui_<state>_camera) carries a `dof` (DOF_UIblur_tod_manager:
        // dof3 strength 0.9, everything past 4 m blurred) that softens the
        // band and stage behind the focused jacket panel, while the main
        // menu's camera has none and Default_TOD_Manager's dof3 strength 0
        // leaves it sharp. The original applies it on bg_viewport in
        // ApplyMenuTransitionDOF (the camera's `dof` once settled, or the one
        // being left when going back) [C script menu_transitions.qb]; our
        // ScreenFX_AddFXInstance only drives the brightness flash, so without
        // this the submenu zoom lacked its focus blur. GD07_NO_MENU_DOF=1 off.
        static const bool noMenuDof = SDL_getenv("GD07_NO_MENU_DOF") != nullptr;
        if (!noMenuDof) {
            const qb::Value* back = vm_.global(K("target_menu_camera_back"));
            const qb::Value* state = (back && qs::Vm::number(back, 0) == 1) ? vm_.global(K("current_menu_camera")) : vm_.global(K("target_menu_camera"));
            // The state name reaches us as a checksum (Key) or, from a script
            // argument (task_menu_default_anim_in: base_name), a String.
            std::string base;
            if (state) {
                if ((state->type == qb::Type::Key || state->type == qb::Type::KeyPtr) && state->key) base = qbkey::name(state->key);
                else if (state->type == qb::Type::String) base = state->str;
            }
            if (!base.empty() && base != "none" && base != "null") {
                const std::string cam = "ui_" + base + "_camera";
                if (const qb::Value* cs = globals_.find(qbkey::key(cam.c_str())); cs && cs->st)
                    if (const qb::Value* dof = globals_.field(*cs->st, K("dof")); dof && (dof->type == qb::Type::Key || dof->type == qb::Type::KeyPtr) && dof->key) {
                        screenfx::readDof(globals_, dof->key, p);
                        if (SDL_getenv("GD07_SCENELOG")) SDL_Log("menu scene: dof %s from %s (strength %.2f)", qbkey::name(dof->key).c_str(), cam.c_str(), p.dof.strength);
                    }
            }
        }
        // bloom2's material filter: the scene's pass 19 [C 8250F158, 8242AB78].
        fx_.end(p, {}, [&] { view_->drawGlow(cam, lightState_, lights_); });
    }
    VenueView::lensShiftX = 0;  // leave the static clear for other viewports
}
