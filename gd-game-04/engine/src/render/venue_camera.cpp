// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_camera.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "data/qbkey.h"
#include "script/globals.h"

namespace venue {
namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }
constexpr double kNever = std::numeric_limits<double>::infinity();

const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct ? v->st.get() : nullptr; }
uint32_t keyOf(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0; }
bool hasFlag(const qb::Struct* st, uint32_t flag) {
    if (!st) return false;
    for (const qb::Item& it : st->items)
        if (it.name == 0 && it.value.type == qb::Type::Key && it.value.key == flag) return true;
    return false;
}
// An entry's field, looked up on the entry and then in its params
// (StartCallback reads Type and name that way) [C script CameraCuts_StartCallback].
const qb::Value* entryField(const qb::Struct* e, uint32_t name) {
    if (!e) return nullptr;
    if (const qb::Value* v = e->find(name)) return v;
    return structOf(e->find(K("params"))) ? structOf(e->find(K("params")))->find(name) : nullptr;
}

float smooth(float s) { return s * s * (3 - 2 * s); }  // ccam_domorph's default motion [I: smooth]

Quat slerp(Quat a, Quat b, float t) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0) {
        for (float& x : b) x = -x;
        d = -d;
    }
    Quat r;
    if (d > 0.9995f) {
        for (size_t i = 0; i < 4; ++i) r[i] = a[i] + (b[i] - a[i]) * t;
    } else {
        const float th = std::acos(d), s = std::sin(th);
        const float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
        for (size_t i = 0; i < 4; ++i) r[i] = a[i] * wa + b[i] * wb;
    }
    const float l = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    for (float& x : r) x /= l;
    return r;
}

Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t}; }

CamPose blend(const CamPose& a, const CamPose& b, float t) {
    return {lerp(a.pos, b.pos, t), slerp(a.rot, b.rot, t), a.fov + (b.fov - a.fov) * t};
}

Quat yRot(float a) { return {0, std::sin(a * 0.5f), 0, std::cos(a * 0.5f)}; }

}  // namespace

const qb::Value* CameraDirector::global(uint32_t name) const {
    if (const qb::Value* v = v_->find(name)) return v;
    const qb::Value* g = g_->find(name);
    return g && g->type == qb::Type::KeyPtr ? g_->resolve(*g) : g;
}

void CameraDirector::init(const Venue& v, const Globals& g, const Chart& chart, uint32_t seed, double leadInMs) {
    v_ = &v, g_ = &g;
    notes_ = chart.cameras;
    std::stable_sort(notes_.begin(), notes_.end(), [](const ChartEvent& a, const ChartEvent& b) { return a.time < b.time; });
    for (const ChartEvent& e : chart.lightshow)
        if (e.note == 58) downbeats_.push_back(e.time);
    std::sort(downbeats_.begin(), downbeats_.end());
    rng_ = 0x9E3779B97F4A7C15ull ^ seed;
    // The guitarist's target node in a one-player song: <zone>_TRG_Waypoint_Guitarist_Start
    // [C script get_start_node_id: '%s_TRG_Waypoint_%a_start', character "guitarist"].
    targetNode_ = v.key("_TRG_Waypoint_Guitarist_Start");
    // create_cameracuts (start_gem_scroller), then the transition's
    // Transition_CameraCut at its time 0: prefix cameras_fastintro (the
    // 3 s fast intro) or cameras_intro (8 s), length = the transition's time
    // [C scripts start_gem_scroller, Common_FastIntro_Transition].
    now_ = -leadInMs;
    prefix_ = "cameras";
    setDefaultCuts();
    qb::Struct p;
    qb::Value s;
    s.type = qb::Type::String;
    s.str = leadInMs > 3000.5 ? "cameras_intro" : "cameras_fastintro";
    p.items.push_back({K("prefix"), s});
    qb::Value cn;
    cn.type = qb::Type::Key;
    cn.key = K("changenow");
    p.items.push_back({0, cn});
    setArrayPrefix(&p, int(leadInMs));
    transitionEnd_ = 0;
    startShot(now_);
}

uint32_t CameraDirector::random(uint32_t n) {
    // xorshift64*: the scripts' GetRandomValue, made deterministic per song.
    rng_ ^= rng_ >> 12, rng_ ^= rng_ << 25, rng_ ^= rng_ >> 27;
    const uint64_t r = rng_ * 0x2545F4914F6CDD1Dull;
    return n ? uint32_t((r >> 32) % n) : 0;
}

uint32_t CameraDirector::findArray(const std::string& prefix, const char* perf) const {
    // set_defaultcameracut_perf: '%s_%p_%c', '%s_%p', 'default_%p_%c', 'default_%p' [C].
    const std::string names[4] = {v_->zone + "_" + prefix + "_" + perf, v_->zone + "_" + prefix, "default_" + prefix + "_" + perf,
                                  "default_" + prefix};
    for (const std::string& n : names) {
        const qb::Value* a = global(qbkey::key(n));
        if (a && a->type == qb::Type::Array) return qbkey::key(n);
    }
    return 0;
}

void CameraDirector::setDefaultCuts() {
    if (uint32_t a = findArray(prefix_, "good")) goodArray_ = a;
    if (uint32_t a = findArray(prefix_, "normal")) normalArray_ = a;
    if (uint32_t a = findArray(prefix_, "poor")) poorArray_ = a;
}

void CameraDirector::setArrayPrefix(const qb::Struct* params, int length) {
    // CameraCuts_SetArrayPrefix {prefix='cameras' Type=none name=none length=0} [C].
    std::string prefix = "cameras";
    uint32_t type = 0, name = 0;
    if (params) {
        if (const qb::Value* p = params->find(K("prefix")); p && p->type == qb::Type::String) prefix = p->str;
        type = keyOf(params->find(K("type")));
        name = keyOf(params->find(K("name")));
        if (hasFlag(params, K("selectguitartype"))) type = K("normal");  // no Demonik guitar here
    }
    if (type == K("none")) type = 0;
    if (name == K("none")) name = 0;
    prefix_ = prefix;
    forceType_ = type;
    nextName_ = name;
    setDefaultCuts();
    if (const qb::Value* ct = params ? params->find(K("changetime")) : nullptr) forceChange_ = ct->number();
    if (hasFlag(params, K("changenow"))) {
        if (length > 200) nextTimeMs_ = length;
        enabled_ = true, changeNow_ = true;
    }
}

void CameraDirector::applyNote(const ChartEvent& e) {
    // cameracuts_iterator: GetNoteMapping section=cameras (CameraCuts_NoteMapping),
    // then the mapped script with its params and the note's length [C].
    const qb::Value* map = global(K("CameraCuts_NoteMapping"));
    if (!map || map->type != qb::Type::Array || !map->arr) return;
    for (const qb::Value& m : map->arr->values) {
        const qb::Struct* st = structOf(&m);
        if (!st) continue;
        const qb::Value* note = st->find(K("midinote"));
        if (!note || int(note->number()) != e.note) continue;
        const uint32_t scr = keyOf(st->find(K("scr")));
        const qb::Struct* params = structOf(st->find(K("params")));
        if (!allowNotes_) return;  // cameracuts_iterator skips the scripts [C]
        if (scr == K("CameraCuts_EnableChangeCam")) {
            enabled_ = params && keyOf(params->find(K("enable"))) == K("true");
            changeNow_ = true;
        } else if (scr == K("CameraCuts_SetArrayPrefix")) {
            setArrayPrefix(params, e.length);
        }
        return;
    }
}

double CameraDirector::calcTime(double t) {
    // CameraCuts_CalcTime: NextTime (a note's length) or 3..8 s rounded up to
    // the next downbeat (lightshow note 58) [C].
    if (nextTimeMs_ > 0) {
        const double s = nextTimeMs_ / 1000.0;
        nextTimeMs_ = 0;
        return s;
    }
    const double r = 3.0 + 5.0 * double(random(1u << 24)) / double(1u << 24);
    double end = t + r * 1000.0;
    for (size_t i = downbeatAt_; i < downbeats_.size(); ++i)
        if (double(downbeats_[i]) > end) {
            downbeatAt_ = i;
            end = downbeats_[i];
            break;
        }
    return (end - t) / 1000.0;
}

CameraDirector::Keyed CameraDirector::readCam(const qb::Struct* st) const {
    Keyed k;
    if (!st) return k;
    const qb::Value* pos = st->find(K("pos"));
    const qb::Value* q = st->find(K("quat"));
    if (!pos || pos->type != qb::Type::Vector || !q || q->type != qb::Type::Vector) return k;
    k.pos = {pos->f[0], pos->f[1], pos->f[2]};
    // Quat: x, y, z; w = sqrt(1 - x^2 - y^2 - z^2) [I: w >= 0].
    const float w2 = 1 - q->f[0] * q->f[0] - q->f[1] * q->f[1] - q->f[2] * q->f[2];
    k.rot = {q->f[0], q->f[1], q->f[2], std::sqrt(std::max(0.0f, w2))};
    if (const qb::Value* f = st->find(K("fov"))) k.fov = f->number();
    k.lockTo = keyOf(st->find(K("lockto")));
    if (k.lockTo == K("world")) k.lockTo = 0;
    k.lookat = keyOf(st->find(K("lookat")));
    k.lookatBone = keyOf(st->find(K("lookatbone")));
    if (const qb::Value* so = st->find(K("screenoffset")); so && (so->type == qb::Type::Pair || so->type == qb::Type::Vector))
        k.screenOffset[0] = so->f[0], k.screenOffset[1] = so->f[1];
    k.valid = true;
    return k;
}

CamPose CameraDirector::world(const Keyed& k) const {
    CamPose p{k.pos, k.rot, k.fov};
    if (!k.lockTo) return p;
    // LockTo a band member: the member's frame, its start waypoint [I: no
    // characters yet, so no animation or bone (locktobone) on top].
    const char* wp = k.lockTo == K("Bassist")    ? "_TRG_Waypoint_Bassist_Start"
                     : k.lockTo == K("Drummer")  ? "_TRG_Waypoint_Drummer_Start"
                     : k.lockTo == K("Vocalist") ? "_TRG_Waypoint_Vocalist_Start"
                                                 : nullptr;
    const Node* n = v_->node(wp ? v_->key(wp) : targetNode_);
    if (!n) return p;
    const Vec3 r = rotate(n->rot, k.pos);
    p.pos = {n->pos[0] + r[0], n->pos[1] + r[1], n->pos[2] + r[2]};
    p.rot = mul(n->rot, k.rot);
    return p;
}

void CameraDirector::startShot(double t) {
    changeNow_ = false;
    // CameraCuts_Performance [I: CameraCuts_Performance starts good; the crowd's
    // performance is set by C code (cameracuts_setparams), here the rock meter's band].
    const Performance perf = perf_ ? perf_->at(t) : Performance::Good;
    const uint32_t arrKey = perf == Performance::Poor ? poorArray_ : perf == Performance::Good ? goodArray_ : normalArray_;
    const qb::Value* arr = arrKey ? global(arrKey) : nullptr;
    shot_ = Shot{};
    shot_.start = t;
    shot_.array = arrKey;
    ++cuts_;
    keys_.clear();
    ctl_ = Ctl::Static;
    zoomFov_ = 0, orbitRad_ = 0, handCam_ = false, orbit1_ = orbit2_ = true;
    calcAt_ = t;
    if (!arr || arr->type != qb::Type::Array || !arr->arr || arr->arr->values.empty()) {
        camTime_ = calcTime(t);
        shot_.end = changeTime_ = t + camTime_ * 1000.0;
        history_.push_back(shot_);
        return;
    }
    const auto& vals = arr->arr->values;
    // CameraCut_GetArraySize / CameraCut_GetCameraIndex: the entries named
    // NextName, or of type ForceType, or not of the last type; entries with
    // includenodes only when the guitarist's target node is listed [I: the
    // C functions' filter, from their parameters and the data].
    std::vector<int> pick;
    for (size_t i = 0; i < vals.size(); ++i) {
        const qb::Struct* e = structOf(&vals[i]);
        if (!e) continue;
        const uint32_t type = keyOf(entryField(e, K("type"))), name = keyOf(entryField(e, K("name")));
        if (nextName_) {
            if (name != nextName_) continue;
        } else if (forceType_) {
            if (type != forceType_) continue;
        } else if (lastType_ && type == lastType_) {
            continue;
        }
        if (const qb::Value* inc = entryField(e, K("includenodes")); inc && inc->type == qb::Type::Array && inc->arr) {
            bool found = false;
            for (const qb::Value& n : inc->arr->values) found = found || n.key == targetNode_;
            if (!found) continue;
        }
        pick.push_back(int(i));
    }
    const int index = pick.empty() ? 0 : pick[random(uint32_t(pick.size()))];
    const qb::Struct* e = structOf(&vals[size_t(index)]);
    shot_.index = index;
    lastType_ = keyOf(entryField(e, K("type")));
    shot_.type = lastType_;
    shot_.name = keyOf(entryField(e, K("name")));
    const uint32_t script = keyOf(e ? e->find(K("controlscript")) : nullptr);
    const qb::Struct* params = e && structOf(e->find(K("params"))) ? structOf(e->find(K("params"))) : e;
    shot_.script = script;
    // CameraCuts_StartCallback: the dof of the entry's params, else of the
    // entry; none: DOF_Off_TOD_Manager [C].
    {
        const qb::Value* d = params ? params->find(K("dof")) : nullptr;
        if (!d && e) d = e->find(K("dof"));
        shot_.dof = keyOf(d);
    }
    auto cam = [&](const char* n) { return readCam(params ? structOf(params->find(K(n))) : nullptr); };
    if (!script || script == K("CameraCuts_StaticCamControl")) {
        keys_.push_back(readCam(e));
    } else if (script == K("CameraCuts_TwoCam") || script == K("CameraCuts_SpecialStepMove")) {
        ctl_ = Ctl::TwoCam;
        keys_ = {cam("cam1"), cam("cam2")};
        handCam_ = hasFlag(params, K("UseHandCam"));
    } else if (script == K("CameraCuts_HandCam")) {
        ctl_ = Ctl::HandCam;
        keys_.push_back(readCam(params));
        handCam_ = true;
    } else if (script == K("CameraCuts_HandCamZoom2")) {
        ctl_ = Ctl::HandCamZoom2;
        keys_.push_back(readCam(params));
        handCam_ = true;
        calcAt_ = t + (2.0 + 0.6 + 0.1 + 0.25 + 0.03 + 0.3) * 1000.0;  // its morphs and waits before CalcTime [C]
    } else if (script == K("CameraCuts_QuickZoom")) {
        ctl_ = Ctl::QuickZoom;
        keys_ = {cam("cam1"), cam("cam2")};
        handCam_ = true;
        calcAt_ = t + 2750.0;
    } else if (script == K("CameraCuts_Pan")) {
        ctl_ = Ctl::Pan;
        keys_ = {cam("cam1"), cam("cam2")};
        handCam_ = true;
        calcAt_ = t + 3000.0;
    } else if (script == K("CameraCuts_Orbit")) {
        ctl_ = Ctl::Orbit;
        keys_.push_back(readCam(params));
    } else if (script == K("CameraCuts_OrbitAndMorphTwoCam")) {
        ctl_ = Ctl::OrbitMorph;
        keys_ = {cam("cam1"), cam("cam2")};
        orbit1_ = !hasFlag(params, K("DisableOrbit1"));
        orbit2_ = !hasFlag(params, K("DisableOrbit2"));
    } else if (script == K("CameraCuts_ThreePos") || script == K("CameraCuts_FourPos") || script == K("CameraCuts_SplineMorph")) {
        ctl_ = Ctl::Spline;
        if (const qb::Value* kf = params ? params->find(K("keyframes")) : nullptr; kf && kf->type == qb::Type::Array && kf->arr)
            for (const qb::Value& k : kf->arr->values) keys_.push_back(readCam(structOf(&k)));
        else
            keys_ = {cam("cam1"), cam("cam2"), cam("Cam3"), cam("Cam4")};
    } else {
        // A venue's own control script (encore, boss): its first camera [I].
        Keyed k = cam("cam1");
        keys_.push_back(k.valid ? k : readCam(params));
    }
    keys_.erase(std::remove_if(keys_.begin(), keys_.end(), [](const Keyed& k) { return !k.valid; }), keys_.end());
    camTime_ = calcTime(calcAt_);
    if (ctl_ == Ctl::HandCam && params) {
        // CameraCuts_HandCam: GetRandomValue 0..100 picks a slow zoom for
        // LongShot (< 25: FOV 62, > 95: 78) and mid shots (< 5: 64, > 85: 82) [C].
        const uint32_t type = keyOf(params->find(K("type")));
        if (type == K("LongShot") || type == K("mid")) {
            const uint32_t r = random(101);
            if (type == K("LongShot")) zoomFov_ = r < 25 ? 62.0f : r > 95 ? 78.0f : 0.0f;
            else zoomFov_ = r < 5 ? 64.0f : r > 85 ? 82.0f : 0.0f;
        }
    }
    if (ctl_ == Ctl::Orbit || ctl_ == Ctl::OrbitMorph) {
        const qb::Value* a = params ? params->find(K("orbitangle")) : nullptr;
        // orbitangle = 3.141 * orbitangle / 180 (the script's own pi) [C].
        orbitRad_ = a ? 3.1409998f * a->number() / 180.0f : 0.0f;
    }
    shot_.end = changeTime_ = calcAt_ + camTime_ * 1000.0;
    history_.push_back(shot_);
}

void CameraDirector::update(double ms, const PerfTimeline& perf) {
    perf_ = &perf;
    double changeNowAt = now_;
    for (int guard = 0; guard < 100000; ++guard) {
        const double tNote = noteAt_ < notes_.size() ? double(notes_[noteAt_].time) : kNever;
        const double tPend = pending_.empty() ? kNever : pending_.front().t;
        // CameraCuts_WaitScript: ForceChangeTime ends the shot once it has
        // lasted that long, whether changes are enabled or not [C].
        const double tForce = forceChange_ > 0 ? shot_.start + forceChange_ + 1e-3 : kNever;
        const double tTrans = transitionDone_ ? kNever : transitionEnd_;
        double tCut = !enabled_ ? kNever : changeNow_ ? std::max(changeNowAt, now_) : changeTime_;
        // A timed cut within a game frame of a camera note is the note's own
        // cut (the original runs both in the same or the next frame) [I].
        if (!changeNow_ && tNote >= tCut && tNote - tCut < 1000.0 / 60.0) tCut = kNever;
        const double t = std::min({tNote, tTrans, tCut, tPend, tForce});
        if (t > ms || t == kNever) break;
        now_ = std::max(now_, t);
        if (t == tPend) {
            const Pending pd = pending_.front();
            pending_.erase(pending_.begin());
            qb::Struct p;
            qb::Value v;
            v.type = qb::Type::String;
            if (pd.what == 0) {
                // CameraCuts_SetArrayPrefix prefix='cameras_starpower' changetime=2500 length=7000 [C].
                allowNotes_ = false;
                v.str = "cameras_starpower";
                p.items.push_back({K("prefix"), v});
                qb::Value ct;
                ct.type = qb::Type::Float;
                ct.f[0] = 2500;
                p.items.push_back({K("changetime"), ct});
            } else {
                v.str = "cameras";
                p.items.push_back({K("prefix"), v});
                allowNotes_ = true;
                spCamera_ = false;
            }
            setArrayPrefix(&p, 0);
            continue;
        }
        if (t == tForce && t <= tNote && t <= tTrans) {
            forceChange_ = 0;
            startShot(now_);
            continue;
        }
        // At the same time: the transition's end, then the note, then the cut.
        if (t == tCut && t < tNote && t < tTrans) {
            startShot(now_);
        } else if (t == tTrans) {
            // Transition_Play_Spawned at the transition's end: EndWithDefaultCamera
            // + SyncWithNoteCameras: a note camera within 2 s waits for it, else
            // CameraCuts_SetArrayPrefix prefix='cameras' changenow [C].
            transitionDone_ = true;
            const double next = noteAt_ < notes_.size() ? double(notes_[noteAt_].time) : -1;
            if (next >= 0 && next - now_ < 2000) {
                enabled_ = false, changeNow_ = true;
            } else {
                qb::Struct p;
                qb::Value cn;
                cn.type = qb::Type::Key;
                cn.key = K("changenow");
                p.items.push_back({0, cn});
                setArrayPrefix(&p, 0);
            }
            changeNowAt = now_;
        } else {
            applyNote(notes_[noteAt_++]);
            changeNowAt = now_;
        }
    }
    now_ = std::max(now_, ms);
}

void CameraDirector::starPower(double ms, bool on) {
    if (on) {
        // do_starpower_camera: not while a walk / Star Power camera runs; one
        // game frame later the cameras_starpower prefix (notes off), 7 s later
        // back to cameras (notes on) [C].
        if (spCamera_) return;
        spCamera_ = true;
        const double t0 = std::max(ms, now_) + 1000.0 / 60.0;
        pending_.push_back({t0, 0});
        pending_.push_back({t0 + 7000.0, 1});
        return;
    }
    // Kill_StarPower_Camera (changecamera = 1): prefix cameras, notes on [C].
    if (!spCamera_) return;
    if (perf_) update(ms, *perf_);
    pending_.clear();
    spCamera_ = false;
    allowNotes_ = true;
    qb::Struct p;
    qb::Value v;
    v.type = qb::Type::String;
    v.str = "cameras";
    p.items.push_back({K("prefix"), v});
    setArrayPrefix(&p, 0);
}

CamPose CameraDirector::pose(double ms) const {
    if (keys_.empty()) return {};
    const double since = (ms - shot_.start) / 1000.0;  // s since the cut
    const double span = std::max(1e-3, camTime_);
    const float s = float(std::clamp((ms - calcAt_) / 1000.0 / span, 0.0, 1.0));
    const Keyed& k0 = keys_.front();
    const Keyed& k1 = keys_.size() > 1 ? keys_[1] : k0;
    CamPose p;
    float mix = 0;  // how much of k1's lookat framing the pose has
    switch (ctl_) {
    case Ctl::Static: p = world(k0); break;
    case Ctl::TwoCam: p = blend(world(k0), world(k1), mix = smooth(s)); break;
    case Ctl::HandCam:
        p = world(k0);
        if (zoomFov_ > 0) p.fov = k0.fov + (zoomFov_ - k0.fov) * smooth(s);
        break;
    case Ctl::HandCamZoom2: {
        // FOV 50 over 0.6 s after 2 s, wait 0.1, 53 over 0.25, wait 0.03, 51 over 0.3 [C].
        p = world(k0);
        auto seg = [&](double a, double len, float from, float to) {
            return from + (to - from) * smooth(float(std::clamp((since - a) / len, 0.0, 1.0)));
        };
        if (since < 2.7) p.fov = seg(2.0, 0.6, k0.fov, 50);
        else if (since < 2.98) p.fov = seg(2.7, 0.25, 50, 53);
        else p.fov = seg(2.98, 0.3, 53, 51);
        break;
    }
    case Ctl::QuickZoom: p = blend(world(k0), world(k1), mix = smooth(float(std::clamp((since - 2.0) / 0.75, 0.0, 1.0)))); break;
    case Ctl::Pan: p = blend(world(k0), world(k1), mix = smooth(float(std::clamp((since - 2.0) / 1.0, 0.0, 1.0)))); break;
    case Ctl::Orbit:
    case Ctl::OrbitMorph: {
        // ccam_orbit axis=(0,1,0) speed=angle/camera_time about the LockTo
        // object's origin [I: the orbit's centre], from the cut on.
        const float ang = float(orbitRad_ / span * std::max(0.0, (ms - calcAt_) / 1000.0));
        auto orbit = [&](Keyed k, bool on) {
            if (!on) return k;
            const Quat q = yRot(ang);
            k.pos = rotate(q, k.pos);
            k.rot = mul(q, k.rot);
            return k;
        };
        if (ctl_ == Ctl::Orbit) p = world(orbit(k0, true));
        else p = blend(world(orbit(k0, orbit1_)), world(orbit(k1, orbit2_)), mix = smooth(s));
        break;
    }
    case Ctl::Spline: {
        // ccam_splinemorph over camera_time: Catmull-Rom through the keys [I].
        const size_t n = keys_.size();
        if (n == 1) {
            p = world(k0);
            break;
        }
        const float f = s * float(n - 1);
        const size_t i = std::min(n - 2, size_t(f));
        const float u = f - float(i);
        const CamPose a = world(keys_[i > 0 ? i - 1 : 0]), b = world(keys_[i]), c = world(keys_[i + 1]),
                      d = world(keys_[std::min(n - 1, i + 2)]);
        for (size_t j = 0; j < 3; ++j)
            p.pos[j] = 0.5f * ((2 * b.pos[j]) + (-a.pos[j] + c.pos[j]) * u + (2 * a.pos[j] - 5 * b.pos[j] + 4 * c.pos[j] - d.pos[j]) * u * u +
                               (-a.pos[j] + 3 * b.pos[j] - 3 * c.pos[j] + d.pos[j]) * u * u * u);
        p.rot = slerp(b.rot, c.rot, u);
        p.fov = b.fov + (c.fov - b.fov) * u;
        p = aim(p, keys_[i], keys_[i + 1], u, ms);
        break;
    }
    }
    if (ctl_ != Ctl::Spline) p = aim(p, k0, k1, mix, ms);
    if (handCam_) {
        // CameraCuts_SetHandCamParams: motionx / motiony, amplitude 0.034 /
        // 0.04, period base 0.013 / 0.017 per frame with multiples 1, 3, 4 [C
        // data]; applied as small yaw / pitch sways [I: the units and the sum].
        const double fr = since * 60.0;
        auto sway = [&](double base) {
            double v = 0;
            for (double m : {1.0, 3.0, 4.0}) v += std::sin(2 * 3.14159265358979 * base * m * fr) / m;
            return float(v / 1.583);
        };
        const float yaw = 0.034f * 0.25f * sway(0.013), pitch = 0.04f * 0.25f * sway(0.017);
        p.rot = mul(p.rot, mul(yRot(yaw), Quat{std::sin(pitch * 0.5f), 0, 0, std::cos(pitch * 0.5f)}));
    }
    return p;
}

// lookat / lookatbone / screenoffset: the camera turns (the least rotation
// from its authored orientation) so the member's bone lands at the screen
// offset, every frame [C data: the authored Quat frames the member's start
// position this way (docs/venues.md "Conventions"); I: GH3's own tracking,
// its smoothing and the roll it keeps]. x right, y down, in half-screens.
CamPose CameraDirector::aim(const CamPose& p, const Keyed& k0, const Keyed& k1, float mix, double ms) const {
    if (!lookup_) return p;
    const Keyed& k = mix < 0.5f || !k1.lookat ? k0 : k1;
    if (!k.lookat || !k.lookatBone) return p;
    Vec3 target;
    if (!lookup_(k.lookat, k.lookatBone, ms, target)) return p;
    float so[2] = {k.screenOffset[0], k.screenOffset[1]};
    if (k0.lookat && k1.lookat && &k0 != &k1)
        for (int i = 0; i < 2; ++i) so[i] = k0.screenOffset[i] + (k1.screenOffset[i] - k0.screenOffset[i]) * mix;
    const float th = std::tan(p.fov * 0.5f * 3.14159265f / 180.0f), tv = th / (16.0f / 9.0f);
    // The wanted direction in the camera's local axes (screen right is local -x).
    Vec3 local{-so[0] * th, -so[1] * tv, 1.0f};
    const Vec3 have = rotate(p.rot, local);
    const Vec3 want{target[0] - p.pos[0], target[1] - p.pos[1], target[2] - p.pos[2]};
    auto norm = [](Vec3 v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        return l > 1e-9f ? Vec3{v[0] / l, v[1] / l, v[2] / l} : v;
    };
    const Vec3 a = norm(have), b = norm(want);
    const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    if (d < 0.2f) return p;  // behind or far off: keep the authored framing
    const Vec3 c{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    Quat r{c[0], c[1], c[2], 1 + d};
    const float n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    for (float& x : r) x /= n;
    CamPose out = p;
    out.rot = mul(r, p.rot);
    return out;
}

}  // namespace venue
