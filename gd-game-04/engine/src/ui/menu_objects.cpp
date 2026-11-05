// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/menu_objects.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "data/venue.h"
#include "render/band_view.h"
#include "render/gh3_shader.h"
#include "script/globals.h"

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

uint32_t keyOf(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0; }

const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr; }

bool isVec(const qb::Value* v) { return v && (v->type == qb::Type::Vector || v->type == qb::Type::Pair); }

venue::Vec3 vecOf(const qb::Value* v) { return {v->f[0], v->f[1], v->type == qb::Type::Vector ? v->f[2] : 0.0f}; }

qb::Struct* locals(qs::ScriptRun& run) { return run.frames.empty() || !run.frames.back().locals ? nullptr : run.frames.back().locals.get(); }

// A struct with its unnamed struct items (`<arg> profile` inside `{ }`)
// merged in, a later name replacing an earlier one [I: GH3's struct
// building, as the scripts rely on: `profile = { <arg> profile_struct
// musician_body = {...} }` overrides the profile's body].
void flatten(const qb::Struct& in, qb::Struct& out) {
    for (const qb::Item& it : in.items) {
        if (!it.name && it.value.type == qb::Type::Struct && it.value.st) {
            flatten(*it.value.st, out);
            continue;
        }
        if (!it.name) {
            out.items.push_back(it);
            continue;
        }
        qs::Vm::set(out, it.name, it.value);
    }
}

venue::Quat axis(int a, float deg) {
    venue::Quat q{0, 0, 0, std::cos(deg * 3.14159265f / 360.0f)};
    q[size_t(a)] = std::sin(deg * 3.14159265f / 360.0f);
    return q;
}

// obj_setorientation x y z / RotateVector rx ry rz: degrees, the nodes'
// order (qy * qz * qx, docs/venues.md "Angles") [I: the same convention].
venue::Quat euler(float x, float y, float z) { return venue::mul(venue::mul(axis(1, y), axis(2, z)), axis(0, x)); }

// The rotation that turns +z to `dir` (y up) [I: obj_setorientation dir].
venue::Quat facing(venue::Vec3 d) {
    const float n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (n < 1e-6f) return {0, 0, 0, 1};
    for (float& c : d) c /= n;
    const float yaw = std::atan2(d[0], d[2]) * 180.0f / 3.14159265f;
    const float pitch = -std::asin(std::clamp(d[1], -1.0f, 1.0f)) * 180.0f / 3.14159265f;
    return venue::mul(axis(1, yaw), axis(0, pitch));
}

venue::Quat quatOfMat(const bandanim::Mat34& m) {
    // Columns normalised (a scaled root), then the usual trace method.
    float r[3][3];
    for (int c = 0; c < 3; ++c) {
        const float n = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
        for (int rr = 0; rr < 3; ++rr) r[rr][c] = n > 0 ? m.m[rr * 4 + c] / n : 0;
    }
    venue::Quat q;
    const float tr = r[0][0] + r[1][1] + r[2][2];
    if (tr > 0) {
        const float s = std::sqrt(tr + 1) * 2;
        q = {(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, s / 4};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1 + r[0][0] - r[1][1] - r[2][2]) * 2;
        q = {s / 4, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s};
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1 + r[1][1] - r[0][0] - r[2][2]) * 2;
        q = {(r[0][1] + r[1][0]) / s, s / 4, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s};
    } else {
        const float s = std::sqrt(1 + r[2][2] - r[0][0] - r[1][1]) * 2;
        q = {(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, s / 4, (r[1][0] - r[0][1]) / s};
    }
    return q;
}

// The scripts whose work the animator does (docs/band.md "Body loop",
// "Face"): spawned on a band member, they are not run.
bool nativeLoop(uint32_t s) {
    return s == K("guitarist_idle") || s == K("bandmember_idle") || s == K("hero_play_adjusting_random_anims") || s == K("facial_anim_loop") ||
           s == K("guitarist_idle_animpreview");
}

// Members accepted with nothing to do: the hands' clips (the animator plays
// them at its start), the arms (its stance), pauses around pak loads, the
// ragdoll's set-up (Band::accessorySim) [C scripts create_guitarist,
// create_band_member].
bool noOp(uint32_t m) {
    static const uint32_t keys[] = {K("hero_toggle_arms"),        K("hero_play_strum_anim"), K("hero_play_fret_anim"), K("hero_play_finger_anim"),
                                    K("hero_pause_anim"),         K("hero_unpause_anim"),    K("ragdoll_setaccessorybones"),
                                    K("ragdoll_markforreset"),    K("Obj_ForceUpdate"),      K("Obj_WaitRotate"),      K("Obj_WaitMove"),
                                    K("obj_stoprotating"),        K("obj_stopmoving"),       K("Obj_PlaySound"),       K("SetTags"),
                                    K("removetags"),              K("obj_enablescaling"),    K("seteventhandler"),     K("ClearEventHandlerGroup"),
                                    K("ResetEventHandlersFromTable"),
                                    // LightShow_CreatePermModels parks the zone's light models
                                    // (Suspend: no updates); the port's venue lights do not update them [C script].
                                    K("Suspend")};
    return std::find(std::begin(keys), std::end(keys), m) != std::end(keys);
}

}  // namespace

// A model loading on a worker: band::loadModel with the musician tables
// copied (the VM's globals stay on the main thread).
// GPU work a frame for a character being added (BandView::prepare) [port].
constexpr double kUploadMsPerFrame = 6.0;

struct MenuObjects::ModelLoad {
    std::future<std::unique_ptr<band::Model>> work;
    band::Member member;
    std::string error;  // written by the worker before the future is ready
    bool ready() const { return work.valid() && work.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
};

MenuObjects::MenuObjects(const Disc& disc, Globals& globals, qs::Vm& vm, WaypointFn waypoint)
    : disc_(disc), globals_(globals), vm_(vm), waypoint_(std::move(waypoint)) {}

MenuObjects::~MenuObjects() {
    if (next_) vm_.objectCall = next_;
    objects_.clear();
    ghosts_.clear();
    inflight_.clear();  // waits for the workers
    view_.reset();
}

bandanim::Mat34 MenuObjects::root(const Object& o) const {
    bandanim::Xf x;
    x.t = {o.pos[0], o.pos[1], o.pos[2]};
    x.q = {o.rot[0], o.rot[1], o.rot[2], o.rot[3]};
    bandanim::Mat34 m = bandanim::matOf(x);
    const float s = o.scale * (o.entry ? o.entry->model->member.scale : 1.0f);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m.m[i * 4 + j] *= s;
    return m;
}

void MenuObjects::unload(Object& o) {
    if (o.entry && o.entry->view >= 0 && view_) view_->remove(o.entry->view);
    if (dev_)
        for (auto& [k, t] : o.made)
            if (t) dev_->destroyTexture(t);
    o.made.clear();
    o.entry.reset();
    o.animated = false;
    o.pending.reset();  // a load under way ends on its own (inflight_), its model cached
}

void MenuObjects::retire(Object& o) {
    if (!o.entry || o.entry->view < 0 || o.hidden) {
        unload(o);
        return;
    }
    auto g = std::make_unique<Object>();
    g->name = o.name, g->type = o.type, g->pos = o.pos, g->rot = o.rot, g->scale = o.scale;
    g->skeleton = o.skeleton, g->lightGroup = o.lightGroup, g->bodySpeed = o.bodySpeed, g->animMs = o.animMs;
    g->animated = o.animated;
    g->profile = o.profile;  // the model's member.profile points into it (the animator's anim_set)
    g->entry = std::move(o.entry);
    o.animated = false;
    o.pending.reset();
    if (Object* old = ghostOf(o.name)) unload(*old);
    ghosts_[o.name] = std::move(g);
}

// obj_initmodelfromprofile: the profile's body and instrument (band::loadModel,
// as the song band's) on the object's skeleton [C scripts]: a model loaded
// before at once, else on a worker (placed by a later load()).
void MenuObjects::load(Object& o) {
    if (o.entry || o.loadFailed || !o.profile || suspended_) return;
    if (o.pending) {
        if (!o.pending->ready()) return;
        const std::shared_ptr<ModelLoad> job = std::move(o.pending);
        std::unique_ptr<band::Model> model;
        try {
            model = job->work.get();
        } catch (const std::exception& e) {
            job->error = e.what();
        }
        if (!model) {
            SDL_Log("menu objects: %s: %s", qbkey::name(o.name).c_str(), job->error.c_str());
            o.loadFailed = true;
            return;
        }
        place(o, std::move(model), job->member);
        return;
    }
    if (!libLoaded_) {
        if (libFailed_) return;
        try {
            lib_ = band::Library::shared(disc_);
            libLoaded_ = true;
        } catch (const std::exception& e) {
            SDL_Log("menu objects: %s", e.what());
            libFailed_ = true;
            return;
        }
    }
    band::Member m = band::memberOf(globals_, o.name, *o.profile);
    if (o.skeleton) m.skeleton = o.skeleton;
    if (auto model = band::cachedModel(disc_, m, "")) {
        place(o, std::move(model), m);
        return;
    }
    // The worker reads only the musician tables (bodyDesc / instrumentDesc):
    // those are copied, the arrays shared (the scripts never change them).
    auto snap = std::make_shared<Globals>();
    for (uint32_t k : {K("musician_body"), K("musician_instrument")})
        if (const qb::Value* v = globals_.find(k)) *snap->ensure(k) = *v;
    auto job = std::make_shared<ModelLoad>();
    job->member = m;
    ModelLoad* raw = job.get();
    job->work = std::async(std::launch::async, [raw, snap, lib = lib_, d = &disc_, m] {
        std::string error;
        auto model = band::loadModel(*d, *snap, *lib, m, "", error);
        raw->error = error;
        return model;
    });
    o.pending = job;
    inflight_.push_back(std::move(job));
}

void MenuObjects::place(Object& o, std::unique_ptr<band::Model> model, const band::Member& m) {
    auto e = std::make_unique<Band::Entry>();
    e->model = std::move(model);
    // The model's lights: its lightgroup (alt_band with band, as the song's guitarist) [C script create_band_member].
    const uint32_t lg = o.lightGroup ? o.lightGroup : K("band");
    e->lightGroups = {lg};
    if (lg == K("alt_band")) e->lightGroups.insert(e->lightGroups.begin(), K("band"));
    e->placed = true;
    o.entry = std::move(e);
    SDL_Log("menu objects: %s = %s%s%s", qbkey::name(o.name).c_str(), m.body ? qbkey::name(m.body).c_str() : "",
            m.body && m.instrument ? " + " : "", m.instrument ? qbkey::name(m.instrument).c_str() : "");
    if (o.treeWanted) startAnim(o);
}

// anim_inittree: the member's tree and its scripts (bandanim::Animator) from
// the object's clock; the stance its info struct holds (create_guitarist:
// <name>_Info.stance) [C scripts].
void MenuObjects::startAnim(Object& o) {
    if (!o.entry || o.animated) return;
    Band::Entry& e = *o.entry;
    bandanim::Animator::Setup s;
    s.model = e.model.get();
    s.g = &globals_;
    s.chart = &chart_;
    s.seed = o.name ^ e.model->member.body ^ uint32_t(clockMs_);
    s.startMs = o.animMs;
    if (const qb::Struct* info = structOf(vm_.global(qbkey::key(qbkey::name(o.name) + "_info")))) s.stance = keyOf(info->find(K("stance")));
    std::string error;
    if (!e.anim.init(s, error)) {
        SDL_Log("menu objects: %s: %s", qbkey::name(o.name).c_str(), error.c_str());
        return;
    }
    const float sc = std::max(1e-6f, o.scale * e.model->member.scale);
    const bandanim::Mat34 r = root(o);
    // The world's (0, -9.8, 0) in model space, the rotation only (render/band.cpp).
    e.gravity = {r.m[4] * -9.8f / sc, r.m[5] * -9.8f / sc, r.m[6] * -9.8f / sc};
    e.acc = Band::accessorySim(disc_, globals_, e.model->member, e.anim.rig(), globalPak_);
    e.evaluatedAt = -1e18;
    o.animated = true;
}

void MenuObjects::evaluate(Object& o) {
    if (!o.entry) return;
    Band::Entry& e = *o.entry;
    if (o.animated) {
        Band::evaluateEntry(e, o.animMs);
        return;
    }
    // No tree: the skeleton's bind pose (the store's guitar: its mesh as modelled).
    if (!e.world.empty() || !e.model->skeleton) return;
    bandanim::Rig rig;
    rig.init(*e.model->skeleton);
    bandanim::worldMatrices(rig, rig.bind, e.world);
    e.skin.resize(e.world.size());
    for (size_t b = 0; b < e.world.size(); ++b) e.skin[b] = bandanim::mmul(e.world[b], rig.invBind[b]);
}

bool MenuObjects::frame(uint32_t object, uint32_t bone, venue::Vec3& pos, venue::Quat& rot) {
    auto it = objects_.find(object);
    if (it == objects_.end()) return false;
    Object& o = *it->second;
    if (!bone) {
        pos = o.pos, rot = o.rot;
        return true;
    }
    load(o);
    // Loading: the ghost of that name (the model it replaces) stands in.
    Object* src = o.entry ? &o : ghostOf(o.name);
    if (!src || !src->entry) return false;
    evaluate(*src);
    const Band::Entry& e = *src->entry;
    int b = -1;
    if (src->animated) {
        b = e.anim.rig().find(bone);
    } else if (e.model->skeleton) {
        bandanim::Rig rig;
        rig.init(*e.model->skeleton);
        b = rig.find(bone);
    }
    if (b < 0 || size_t(b) >= e.world.size()) return false;
    const bandanim::Mat34 w = bandanim::mmul(root(*src), e.world[size_t(b)]);
    pos = {w.m[3], w.m[7], w.m[11]};
    rot = quatOfMat(w);
    return true;
}

void MenuObjects::update(double dtMs) {
    clockMs_ += dtMs;
    for (auto& [k, o] : objects_) {
        o->animMs += dtMs * double(o->bodySpeed);
        if (o->animated && o->entry) o->entry->anim.advance(o->animMs);
    }
    for (auto& [k, g] : ghosts_) {
        g->animMs += dtMs * double(g->bodySpeed);
        g->ghostMs += dtMs;
        if (g->animated && g->entry) g->entry->anim.advance(g->animMs);
    }
    // Loads no object waits for any more, once done (their models stay in band::loadModel's cache).
    inflight_.erase(std::remove_if(inflight_.begin(), inflight_.end(),
                                   [](const std::shared_ptr<ModelLoad>& j) { return j.use_count() == 1 && j->ready(); }),
                    inflight_.end());
}

void MenuObjects::draw(gfx::RenderDevice& dev, const venue::CamPose& cam, const venue::Venue& v, const std::vector<venue::LightState>& lights,
                       const venue::LightShow& show) {
    if (objects_.empty() && ghosts_.empty()) return;
    if (dev_ && dev_ != &dev) view_.reset(), ghosts_.clear();
    dev_ = &dev;
    std::vector<BandView::Instance> inst;
    for (auto& [k, op] : objects_) {
        Object& o = *op;
        if (o.hidden || !o.profile) continue;
        load(o);
        if (!o.entry) continue;
        Band::Entry& e = *o.entry;
        if (e.view < 0) {
            if (!view_) {
                view_ = std::make_unique<BandView>();
                view_->setLibrary(gh3shader::library(disc_));
            }
            // The textures and programs a few milliseconds a frame (a
            // character's 5-8 MB and its new templates took 40-230 ms at once).
            if (!view_->prepare(dev, *e.model, o.made, kUploadMsPerFrame)) continue;
            std::string error;
            e.view = view_->add(dev, *e.model, error, &o.made);
            o.made.clear();  // the member's now
            if (e.view < 0) {
                SDL_Log("menu objects: %s: %s", qbkey::name(o.name).c_str(), error.c_str());
                o.loadFailed = true;
                o.entry.reset();
                continue;
            }
        }
        evaluate(o);
        if (e.skin.empty()) continue;
        BandView::Instance i;
        i.member = e.view;
        i.skin = &e.skin;
        i.root = root(o);
        i.lightGroups = e.lightGroups;
        inst.push_back(std::move(i));
    }
    // A ghost stands in while the object of its name loads, up to 2 s.
    for (auto it = ghosts_.begin(); it != ghosts_.end();) {
        Object& g = *it->second;
        auto live = objects_.find(it->first);
        const Object* o = live == objects_.end() ? nullptr : live->second.get();
        const bool waiting = o && !o->hidden && !o->loadFailed && !(o->entry && o->entry->view >= 0) && g.ghostMs < 2000;
        if (!waiting || !g.entry || g.entry->view < 0) {
            unload(g);
            it = ghosts_.erase(it);
            continue;
        }
        evaluate(g);
        if (!g.entry->skin.empty()) {
            BandView::Instance i;
            i.member = g.entry->view;
            i.skin = &g.entry->skin;
            i.root = root(g);
            i.lightGroups = g.entry->lightGroups;
            inst.push_back(std::move(i));
        }
        ++it;
    }
    if (inst.empty() || !view_) return;
    view_->setTime(clockMs_ / 1000.0);
    view_->draw(cam, inst, v, lights, show);
}

bool MenuObjects::member(qs::ScriptRun& run, Object& o, uint32_t method, qb::Struct& p, bool& handled) {
    using qs::Vm;
    handled = true;
    const uint32_t id = o.name;
    static const bool log = SDL_getenv("GD04_MENUOBJ_LOG") != nullptr;  // development aid
    if (log) {
        std::string items;
        for (const qb::Item& it : p.items) {
            items += " " + qbkey::name(it.name) + "=";
            if (isVec(&it.value)) items += std::to_string(it.value.f[0]) + "," + std::to_string(it.value.f[1]) + "," + std::to_string(it.value.f[2]);
            else if (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) items += qbkey::name(it.value.key);
            else if (it.value.type == qb::Type::Float || it.value.type == qb::Type::Int) items += std::to_string(it.value.number());
        }
        SDL_Log("menu object %s: %s%s", qbkey::name(id).c_str(), qbkey::name(method).c_str(), items.c_str());
    }
    if (method == K("Die")) {
        // The object and the scripts running on it [I: Die ends them].
        for (auto& r : vm_.runs())
            if (r && r->object == id && r.get() != &run) vm_.kill(r->id);
        retire(o);
        objects_.erase(id);  // `o` is gone
        return true;
    }
    if (method == K("hide") || method == K("Unhide")) {
        o.hidden = method == K("hide");
        return true;
    }
    if (method == K("obj_setposition") || method == K("obj_movetopos")) {
        const qb::Value* v = Vm::param(p, K("position"));
        if (!v) v = Vm::positional(p);
        if (isVec(v)) o.pos = vecOf(v);
        return true;
    }
    if (method == K("obj_getposition")) {
        if (qb::Struct* l = locals(run)) {
            qb::Value v;
            v.type = qb::Type::Vector;
            v.f[0] = o.pos[0], v.f[1] = o.pos[1], v.f[2] = o.pos[2];
            Vm::set(*l, K("pos"), v);
        }
        return true;
    }
    if (method == K("obj_setorientation") || method == K("obj_rotate")) {
        // dir = <vector> (+z to it) or x / y / z angles in degrees; obj_rotate absolute = (x, y, z) [I: at once].
        if (const qb::Value* d = Vm::param(p, K("dir")); isVec(d)) {
            o.rot = facing(vecOf(d));
        } else if (const qb::Value* a = Vm::param(p, K("absolute")); isVec(a)) {
            const venue::Vec3 v = vecOf(a);
            o.rot = euler(v[0], v[1], v[2]);
        } else {
            o.rot = euler(Vm::number(Vm::param(p, K("x")), 0), Vm::number(Vm::param(p, K("y")), 0), Vm::number(Vm::param(p, K("z")), 0));
        }
        return true;
    }
    if (method == K("obj_getorientation")) {
        // x, y, z: the facing direction (create_band_member builds dir from them) [C script].
        const venue::Vec3 f = venue::rotate(o.rot, {0, 0, 1});
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("x"), Vm::makeFloat(f[0])), Vm::set(*l, K("y"), Vm::makeFloat(f[1])), Vm::set(*l, K("z"), Vm::makeFloat(f[2]));
        return true;
    }
    if (method == K("obj_getid")) {
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("ObjID"), Vm::makeKey(id));
        return true;
    }
    if (method == K("obj_initmodelfromprofile")) {
        auto prof = std::make_shared<qb::Struct>();
        if (const qb::Struct* st = structOf(Vm::param(p, K("struct")))) flatten(*st, *prof);
        if (const qb::Struct* ps = structOf(Vm::param(p, K("params"))))
            if (const uint32_t lg = keyOf(ps->find(K("lightgroup")))) o.lightGroup = lg;
        retire(o);
        o.profile = std::move(prof);
        o.loadFailed = false;
        load(o);
        return true;
    }
    if (method == K("anim_inittree")) {
        o.treeWanted = true;
        startAnim(o);
        return true;
    }
    if (method == K("anim_uninittree")) {
        o.treeWanted = false;
        if (o.entry) o.entry->world.clear(), o.entry->skin.clear();
        o.animated = false;
        return true;
    }
    if (method == K("anim_animnodeexists")) return o.treeWanted;
    if (method == K("anim_command")) {
        // bodytimer timer_setspeed {Speed}: the body clock's rate (0 holds the pose) [I: the whole tree's clock].
        if (keyOf(Vm::param(p, K("command"))) == K("timer_setspeed"))
            if (const qb::Struct* ps = structOf(Vm::param(p, K("params")))) o.bodySpeed = Vm::number(ps->find(K("Speed")), 1);
        return true;
    }
    if (method == K("handle_change_stance") || method == K("Band_ChangeStance")) {
        const uint32_t st = keyOf(Vm::param(p, K("stance")));
        if (!st) return true;
        if (o.animated && o.entry) o.entry->anim.changeStance(st, Vm::flag(p, K("no_wait")), o.animMs);
        // The info struct's stance as the scripts test it (`if not ($guitarist_info.stance = stance_frontend)`) [I: at once].
        const uint32_t infoKey = qbkey::key(qbkey::name(id) + "_info");
        if (const qb::Value* info = vm_.global(infoKey); info && info->st) {
            qb::Value v = *info;
            v.st = std::make_shared<qb::Struct>(*info->st);
            Vm::set(*v.st, K("stance"), Vm::makeKey(st));
            Vm::set(*v.st, K("next_stance"), Vm::makeKey(st));
            vm_.setGlobal(infoKey, v);
        }
        return true;
    }
    if (method == K("handle_play_anim") || method == K("Band_PlayAnim")) {
        if (o.animated && o.entry) o.entry->anim.playAnim(p, o.animMs);
        return true;
    }
    if (method == K("obj_spawnscriptnow") || method == K("obj_spawnscriptlater") || method == K("Obj_SwitchScript")) {
        const qb::Struct* in = &p;
        if (const qb::Struct* inner = structOf(Vm::positional(p))) in = inner;
        uint32_t scr = 0;
        for (const qb::Item& it : in->items)
            if (!it.name && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && !scr) scr = it.value.key;
        if (method == K("Obj_SwitchScript"))
            for (auto& r : vm_.runs())
                if (r && r->object == id && r.get() != &run) vm_.kill(r->id);
        if (!scr || scr == K("EmptyScript") || (o.type == K("bandmember") && nativeLoop(scr))) return true;
        qb::Struct args;
        if (const qb::Struct* ps = structOf(Vm::param(*in, K("params")))) args = *ps;
        const uint32_t spawned = vm_.spawnOn(scr, args, id);
        if (spawned && method == K("obj_spawnscriptnow")) vm_.runNow(spawned);
        return true;
    }
    if (method == K("obj_killspawnedscript")) {
        const uint32_t nm = keyOf(Vm::param(p, K("name")));
        for (auto& r : vm_.runs())
            if (r && r->object == id && r->name == nm && r.get() != &run) vm_.kill(r->id);
        return true;
    }
    if (noOp(method)) return true;
    // A script called on the object runs with the object as its own.
    if (const qb::Value* g = globals_.find(method); g && g->type == qb::Type::Script) {
        bool result = true;
        vm_.run(method, p, &result, id);
        return result;
    }
    handled = false;
    return false;
}

void MenuObjects::addNatives() {
    using qs::ScriptRun;
    using qs::Vm;
    next_ = vm_.objectCall;
    vm_.objectCall = [this](Vm& v, ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled) -> bool {
        auto it = objects_.find(obj);
        if (it != objects_.end()) {
            const bool r = member(run, *it->second, method, p, handled);
            if (handled) return r;
            v.logOnce("UNHANDLED composite object member " + qbkey::name(method));
            handled = true;
            return false;
        }
        return next_ ? next_(v, run, obj, method, p, handled) : (handled = false);
    };
    // CreateCompositeObject {Components = [...] params = {name pos ...}}: the
    // object (an existing one of that name is replaced) [C scripts
    // create_band_member, store_do_guitar_load].
    vm_.addNative("CreateCompositeObject", [this](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Struct* ps = structOf(Vm::param(p, K("params")));
        if (!ps) return false;
        uint32_t name = 0;
        for (const qb::Item& it : ps->items)
            if (it.name == K("name") && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr)) name = it.value.key;
        if (!name) return false;  // the scripts' sound and particle objects have their ids elsewhere (not drawn here)
        qb::Struct flat;
        flatten(*ps, flat);
        auto o = std::make_unique<Object>();
        o->name = name;
        o->type = keyOf(flat.find(K("object_type")));
        if (const qb::Value* pos = flat.find(K("pos")); isVec(pos)) o->pos = vecOf(pos);
        if (const qb::Value* s = flat.find(K("scale")); s && (s->type == qb::Type::Float || s->type == qb::Type::Int)) o->scale = s->number();
        if (const qb::Value* c = Vm::param(p, K("Components")); c && c->type == qb::Type::Array && c->arr)
            for (const qb::Value& cv : c->arr->values) {
                const qb::Struct* cs = structOf(&cv);
                if (!cs) continue;
                const uint32_t comp = keyOf(cs->find(K("Component")));
                if (comp == K("Skeleton")) o->skeleton = keyOf(cs->find(K("SkeletonName")));
                else if (comp == K("AnimTree")) o->animTree = true;
                else if (comp == K("Model"))
                    if (const uint32_t lg = keyOf(cs->find(K("lightgroup")))) o->lightGroup = lg;
            }
        o->animMs = clockMs_;
        if (auto old = objects_.find(name); old != objects_.end()) retire(*old->second);
        objects_[name] = std::move(o);
        return true;
    });
    auto nameOf = [](const qb::Struct& p) {
        uint32_t n = keyOf(Vm::param(p, K("name")));
        if (!n) n = keyOf(Vm::param(p, K("id")));
        if (!n) n = keyOf(Vm::positional(p));
        // `CompositeObjectExists <arg> name`: the name as a bare key
        // (create_band_member's useoldpos check) [C data].
        for (const qb::Item& it : p.items)
            if (!n && !it.name) n = keyOf(&it.value);
        return n;
    };
    vm_.addNative("CompositeObjectExists", [this, nameOf](Vm&, ScriptRun&, qb::Struct& p) { return exists(nameOf(p)); });
    vm_.addNative("ObjectExists", [this, nameOf](Vm&, ScriptRun&, qb::Struct& p) { return exists(nameOf(p)); });
    // GetWaypointPos / GetWaypointDir {name}: a node of the current zone ->
    // pos / dir (its +z); DoesWayPointExist [C scripts create_band_member,
    // store_do_guitar_load; I: dir = the node's +z, docs/band.md "Placement"].
    vm_.addNative("GetWaypointPos", [this, nameOf](Vm&, ScriptRun& run, qb::Struct& p) {
        venue::Vec3 pos;
        venue::Quat rot;
        if (suspended_ || !waypoint_ || !waypoint_(nameOf(p), pos, rot)) return false;
        qb::Value v;
        v.type = qb::Type::Vector;
        v.f[0] = pos[0], v.f[1] = pos[1], v.f[2] = pos[2];
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("pos"), v);
        return true;
    });
    vm_.addNative("GetWaypointDir", [this, nameOf](Vm&, ScriptRun& run, qb::Struct& p) {
        venue::Vec3 pos;
        venue::Quat rot;
        if (suspended_ || !waypoint_ || !waypoint_(nameOf(p), pos, rot)) return false;
        const venue::Vec3 d = venue::rotate(rot, {0, 0, 1});
        qb::Value v;
        v.type = qb::Type::Vector;
        v.f[0] = d[0], v.f[1] = d[1], v.f[2] = d[2];
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("dir"), v);
        return true;
    });
    vm_.addNative("DoesWayPointExist", [this, nameOf](Vm&, ScriptRun&, qb::Struct& p) {
        venue::Vec3 pos;
        venue::Quat rot;
        return !suspended_ && waypoint_ && waypoint_(nameOf(p), pos, rot);
    });
    // RotateVector {vector rx ry rz} -> result_vector: degrees, the
    // orientation's convention [I].
    vm_.addNative("RotateVector", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* v = Vm::param(p, K("vector"));
        if (!isVec(v)) return false;
        const venue::Quat q = euler(Vm::number(Vm::param(p, K("rx")), 0), Vm::number(Vm::param(p, K("ry")), 0), Vm::number(Vm::param(p, K("rz")), 0));
        const venue::Vec3 r = venue::rotate(q, vecOf(v));
        qb::Value out;
        out.type = qb::Type::Vector;
        out.f[0] = r[0], out.f[1] = r[1], out.f[2] = r[2];
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("result_vector"), out);
        return true;
    });
    // SetShadowDirFromLight {name}: the characters' shadow direction (no
    // dynamic shadows in the menus' scene yet).
    vm_.addNative("SetShadowDirFromLight", [](Vm&, ScriptRun&, qb::Struct&) { return true; });
}
