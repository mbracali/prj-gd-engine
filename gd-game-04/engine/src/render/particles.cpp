// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/particles.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "data/texture.h"
#include "data/venue.h"
#include "render/camera.h"
#include "render/rmath.h"
#include "script/natives_core.h"
#include "script/vm.h"

namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }
constexpr double kStepMs = 1000.0 / 60.0;
constexpr size_t kMaxParticles = 20000;

const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr; }
uint32_t keyOf(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0; }
float num(const qb::Value* v, float f) { return v && (v->type == qb::Type::Float || v->type == qb::Type::Int) ? v->number() : f; }
venue::Vec3 vec(const qb::Value* v, venue::Vec3 f) {
    if (!v || (v->type != qb::Type::Vector && v->type != qb::Type::Pair)) return f;
    return {v->f[0], v->f[1], v->type == qb::Type::Vector ? v->f[2] : 0.0f};
}

// A particle's random numbers: a hash of the system's seed and the index.
float rnd(uint64_t seed, uint64_t i, uint32_t k) {
    uint64_t x = seed ^ (i * 0x9E3779B97F4A7C15ull) ^ (uint64_t(k) << 48);
    x ^= x >> 33, x *= 0xFF51AFD7ED558CCDull, x ^= x >> 33, x *= 0xC4CEB9FE1A85EC53ull, x ^= x >> 33;
    return float(x >> 40) / float(1 << 24);
}

const char* kVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aUv;
layout(std140) uniform GdParticleFrame { mat4 uViewProj; vec4 uParams; };
out vec4 vColor;
out vec2 vUv;
void main() {
    vColor = aColor;
    vUv = aUv;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

// The quick material's look [I: GH3's particlequickmaterial shaders draw the
// texture x the particle colour; lighting and burn are not reproduced].
const char* kFragment = R"(
in vec4 vColor;
in vec2 vUv;
layout(std140) uniform GdParticleFrame { mat4 uViewProj; vec4 uParams; };
uniform sampler2D sTex;
out vec4 fragColor;
void main() {
    vec4 c = texture(sTex, vUv) * vColor;
    if (c.a < uParams.x) discard;
    // The burn (ParticleQuickMaterialNoLighting, m_Burn): a' = (1 - burn) a,
    // rgb x a x a', drawn premultiplied [C].
    if (uParams.y > 0.0) {
        float a2 = (1.0 - uParams.y) * c.a;
        c.rgb *= c.a * a2;
        c.a = a2;
    }
    fragColor = c;
}
)";

struct PVtx {
    float pos[3];
    uint8_t col[4];
    float uv[2];
};

}  // namespace

Particles::Particles() = default;
Particles::~Particles() { shutdown(); }

gfx::ProgramDesc Particles::programDesc() {
    gfx::ProgramDesc d;
    d.name = "particles";
    d.vertex = kVertex;
    d.fragment = kFragment;
    d.uniformBlocks = {"GdParticleFrame"};
    d.samplers = {"sTex"};
    return d;
}

double Particles::System::end() const { return start + (p.delay + p.lifetime + p.lifeMax) * 1000.0; }

bool Particles::start(const Disc& disc, const Globals& g, const venue::Venue& v, const Chart& chart, const venue::PerfTimeline* perf,
                      double startMs, std::string& error) {
    (void)disc;
    v_ = &v;
    perf_ = perf;
    systems_.clear();
    const qb::Struct* map = structOf(v.find(v.key("_lightshow_mapping")));
    if (!map) {
        error = v.zone + ": no light show mapping";
        return false;
    }
    pyroScript_ = keyOf(map->find(K("pyro_script")));
    perfScripts_.clear();
    if (const qb::Struct* tr = structOf(map->find(K("transitions"))))
        if (const qb::Value* a = tr->find(K("performance")); a && a->type == qb::Type::Array && a->arr)
            for (const qb::Value& s : a->arr->values) perfScripts_.push_back(keyOf(&s));
    // LightShow_NoteMapping: note 56 runs LightShow_PyroEvent, lightshow_offset_ms early [C; I: the sign].
    offsetMs_ = qbNumber(g, K("lightshow_offset_ms"), 100);
    fretbars_ = chart.fretbars;
    pyroTimes_.clear();
    for (const ChartEvent& e : chart.lightshow)
        if (e.note == 56) pyroTimes_.push_back(double(e.time) - offsetMs_);
    std::sort(pyroTimes_.begin(), pyroTimes_.end());
    pyroAt_ = perfAt_ = 0;
    lastPerf_ = venue::Performance::Medium;
    vmTime_ = startMs;
    // A VM of its own over the globals plus the zone's scripts.
    g_ = std::make_unique<Globals>(g);
    for (const qb::File& f : v.files) g_->add(f);
    vm_ = std::make_unique<qs::Vm>(*g_);
    qs::addCoreNatives(*vm_);
    vm_->addNative("GetUniqueCompositeObjectID", [](qs::Vm&, qs::ScriptRun& run, qb::Struct& p) {
        if (!run.frames.empty() && run.frames.back().locals)
            qs::Vm::set(*run.frames.back().locals, K("uniqueid"), qs::Vm::makeKey(keyOf(qs::Vm::param(p, K("preferredid")))));
        return true;
    });
    vm_->addNative("CreateParticleSystem_fast", [this](qs::Vm&, qs::ScriptRun&, qb::Struct& p) {
        create(vec(qs::Vm::param(p, K("pos")), {}), qs::Vm::param(p, K("params_script")), vmTime_);
        return true;
    });
    vm_->addNative("IsCreated", [this](qs::Vm&, qs::ScriptRun&, qb::Struct& p) {
        const qb::Value* n = qs::Vm::positional(p);
        const venue::Node* node = n ? v_->node(keyOf(n)) : nullptr;
        return node && node->createdAtStart;
    });
    addObjectNatives();
    // The nodes' trigger scripts: a created node runs its triggerscript on
    // itself [C data: e.g. z_party's globes (Z_Party_GlobeRot01Script),
    // z_video's fans (Z_Video_G_RMM_fantop02Script); I: at the level's start].
    size_t triggers = 0;
    for (const venue::Node& n : v.nodes) {
        if (!n.st) continue;
        if (!n.createdAtStart) {
            // crowd_create_lighters creates the lighter geometry at the song's start [C script].
            std::string nm = qbkey::names().get(n.name);
            std::transform(nm.begin(), nm.end(), nm.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (nm.find("_lighter_geo") == std::string::npos) continue;
        }
        const uint32_t ts = keyOf(n.st->find(K("triggerscript")));
        if (!ts || !vm_->code(ts)) continue;
        vm_->spawnOn(ts, qb::Struct{}, n.name);
        ++triggers;
    }
    for (const char* noop : {"SpawnSound", "PlaySound", "Obj_PlaySound", "SoundEvent", "SetSpawnInstanceLimits", "RequireParams"})
        vm_->addNative(noop, [](qs::Vm&, qs::ScriptRun&, qb::Struct&) { return true; });
    using ull = unsigned long long;
    SDL_Log("particles %s: %llu pyro notes (%s), %llu performance scripts", v.zone.c_str(), ull(pyroTimes_.size()),
            pyroScript_ ? qbkey::names().get(pyroScript_).c_str() : "no pyro script", ull(perfScripts_.size()));
    if (std::getenv("GD04_PARTICLES_LOG"))
        for (double t : pyroTimes_) SDL_Log("particles: pyro at %.0f ms", t);
    if (triggers) SDL_Log("particles %s: %llu node trigger scripts", v.zone.c_str(), ull(triggers));
    return pyroScript_ || !perfScripts_.empty() || triggers;
}

float Particles::Axis::at(double t) const {
    if (rate == 0) return a0;
    const float a = a0 + rate * float((t - t0) / 1000.0);
    if (!target) return a;
    return rate > 0 ? std::min(a, to) : std::max(a, to);
}

float Particles::Motion::scale(double t) const {
    if (sDur <= 0 || t >= sT0 + sDur) return sTo;
    const float u = float(std::max(0.0, (t - sT0) / sDur));
    return s0 + (sTo - s0) * u;
}

void Particles::playerHit(double ms, uint8_t frets) {
    if (frets) hits_.push_back({ms, frets});
}

float Particles::pulse(const Motion& m, double ms) const {
    // AddPulseEvent: on the event the scale goes to amount for a game frame,
    // then back over `time` [I: as Venue_PulseOnEvents' own delay branch:
    // obj_applyscaling, Wait 1 gameframe, obj_morphscaling].
    if (!m.pulseFrets) return 1;
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (ms - it->first > 1000.0) break;
        if (it->first > ms || !(it->second & m.pulseFrets)) continue;
        const double dt = ms - it->first;
        if (dt < kStepMs) return m.pulseAmount;
        const double back = m.pulseTime * 1000.0;
        if (dt >= kStepMs + back) return 1;
        return m.pulseAmount + (1 - m.pulseAmount) * float((dt - kStepMs) / back);
    }
    return 1;
}

bool Particles::Axis::done(double t) const { return !target || rate == 0 || at(t) == to; }

venue::Vec3 Particles::Motion::pos(double t) const {
    if (mDur <= 0 || t >= m0 + mDur) return to;
    const float u = float(std::max(0.0, (t - m0) / mDur));
    return {from[0] + (to[0] - from[0]) * u, from[1] + (to[1] - from[1]) * u, from[2] + (to[2] - from[2]) * u};
}

void Particles::addObjectNatives() {
    auto setLocal = [](qs::ScriptRun& run, uint32_t name, qb::Value v) {
        if (!run.frames.empty() && run.frames.back().locals) qs::Vm::set(*run.frames.back().locals, name, std::move(v));
    };
    // GameObj_PlayAnim {Anim blendduration} / GameObj_WaitAnimFinished on a
    // stage ped (the scripts' anim_command degenerateblend_addbranch and
    // timer_waitanimcomplete) [C scripts gameobj; I: the C anim tree].
    vm_->addNative("GameObj_PlayAnim", [this](qs::Vm&, qs::ScriptRun& run, qb::Struct& p) {
        if (!animHook_ || !run.object) return true;
        const uint32_t anim = keyOf(qs::Vm::param(p, K("Anim")));
        const double len = animHook_(run.object, anim, qs::Vm::number(qs::Vm::param(p, K("blendduration")), -1), vmTime_);
        animEnd_[run.object] = vmTime_ + std::max(kStepMs, len);
        return true;
    });
    vm_->addNative("GameObj_WaitAnimFinished", [this](qs::Vm&, qs::ScriptRun& run, qb::Struct&) {
        const uint32_t obj = run.object;
        if (!animEnd_.count(obj)) return true;
        run.waitFor = [this, obj] { return vmTime_ >= animEnd_[obj]; };
        return true;
    });
    vm_->addNative("obj_getid", [setLocal](qs::Vm&, qs::ScriptRun& run, qb::Struct&) {
        setLocal(run, K("ObjID"), qs::Vm::makeKey(run.object));
        return true;
    });
    // hero_get_skill_level_cfunc: skill bad / normal / good by the rock
    // meter's bands [C: the crowd's script use; I: the bands, as the crowd's].
    vm_->addNative("hero_get_skill_level_cfunc", [this, setLocal](qs::Vm&, qs::ScriptRun& run, qb::Struct&) {
        const venue::Performance pf = perf_ ? perf_->at(vmTime_) : venue::Performance::Medium;
        setLocal(run, K("skill"), qs::Vm::makeKey(K(pf == venue::Performance::Poor ? "bad" : pf == venue::Performance::Good ? "good" : "normal")));
        return true;
    });
    // get_anim_speed_for_tempo_cfunc: slow / med / fast by the beat at the
    // time against slow_tempo / med_tempo / fast_tempo [I: as render/crowd.cpp].
    vm_->addNative("get_anim_speed_for_tempo_cfunc", [this, setLocal](qs::Vm& vm, qs::ScriptRun& run, qb::Struct&) {
        double beat = 500;
        if (fretbars_.size() >= 2) {
            size_t i = size_t(std::upper_bound(fretbars_.begin(), fretbars_.end(), int32_t(vmTime_)) - fretbars_.begin());
            i = std::clamp<size_t>(i, 1, fretbars_.size() - 1);
            beat = std::max(1, fretbars_[i] - fretbars_[i - 1]);
        }
        const double bpm = 60000.0 / beat;
        const float slow = qs::Vm::number(vm.global(K("slow_tempo")), 75), med = qs::Vm::number(vm.global(K("med_tempo")), 115),
                    fast = qs::Vm::number(vm.global(K("fast_tempo")), 145);
        setLocal(run, K("anim_speed"),
                 qs::Vm::makeKey(K(bpm < (slow + med) * 0.5 ? "slow" : bpm < (med + fast) * 0.5 ? "med" : "fast")));
        return true;
    });
    // CallScriptOnNode prefix=<name prefix> callBack_Script=<script> params={...}:
    // the script once for every node whose name starts so, with nodeName [C
    // script use, z_dive_performance_changed; I: the C side's order].
    vm_->addNative("CallScriptOnNode", [this](qs::Vm& vm, qs::ScriptRun&, qb::Struct& p) {
        const uint32_t cb = keyOf(qs::Vm::param(p, K("callBack_Script")));
        const qb::Value* pre = qs::Vm::param(p, K("prefix"));
        std::string prefix = pre ? (pre->type == qb::Type::String ? pre->str : qbkey::names().get(keyOf(pre))) : std::string();
        if (!cb || prefix.empty()) return true;
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        qb::Struct base;
        if (const qb::Struct* ps = structOf(qs::Vm::param(p, K("params")))) base = *ps;
        for (const venue::Node& n : v_->nodes) {
            std::string name = qbkey::names().get(n.name);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (name.compare(0, prefix.size(), prefix) != 0) continue;
            qb::Struct args = base;
            qs::Vm::set(args, K("nodeName"), qs::Vm::makeKey(n.name));
            vm.run(cb, args);
        }
        return true;
    });
    // Object members on a node (`<node> : obj_rotz Speed = 300`, or in a
    // node's trigger script): obj_rotx / roty / rotz {Speed (degrees a
    // second), angle (a turn by, else for ever)}, obj_rotate {absolute (x, y,
    // z) time}, obj_stoprotating, obj_movetopos (pos) {time | Speed},
    // obj_setposition position, Obj_WaitRotate / Obj_WaitMove [C: the
    // scripts' use; I: the units, the Euler order, the pivot = the node].
    vm_->objectCall = [this](qs::Vm&, qs::ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled) -> bool {
        const venue::Node* node = v_->node(obj);
        handled = node != nullptr;
        if (!node) return false;
        const double t = vmTime_;
        Motion& m = motion_[obj];
        if (!m.moved) m.from = m.to = node->pos;
        auto rot = [&](int axis) {
            Axis& a = m.rot[axis];
            const float cur = a.at(t);
            const float speed = qs::Vm::number(qs::Vm::param(p, K("Speed")), 0);
            const qb::Value* ang = qs::Vm::param(p, K("angle"));
            a = Axis{};
            a.a0 = cur, a.t0 = t;
            if (ang) {
                const float by = qs::Vm::number(ang, 0);
                a.target = true, a.to = cur + by;
                a.rate = by == 0 ? 0 : (by > 0 ? std::fabs(speed) : -std::fabs(speed));
            } else {
                a.rate = speed;
            }
        };
        if (method == K("obj_rotx")) rot(0);
        else if (method == K("obj_roty")) rot(1);
        else if (method == K("obj_rotz")) rot(2);
        else if (method == K("obj_rotate")) {
            const venue::Vec3 abs = vec(qs::Vm::param(p, K("absolute")), {});
            const float time = qs::Vm::number(qs::Vm::param(p, K("time")), 0);
            for (int k = 0; k < 3; ++k) {
                Axis& a = m.rot[k];
                const float cur = a.at(t);
                a = Axis{};
                a.t0 = t;
                if (time <= 0) {
                    a.a0 = abs[size_t(k)];
                } else {
                    a.a0 = cur, a.target = true, a.to = abs[size_t(k)];
                    a.rate = (abs[size_t(k)] - cur) / time;
                }
            }
        } else if (method == K("obj_stoprotating")) {
            for (Axis& a : m.rot) a = Axis{a.at(t), t};
        } else if (method == K("obj_movetorelpos")) {
            // A move by an offset in the node's frame [I: as obj_movetopos, Speed m/s].
            const qb::Value* pv = qs::Vm::positional(p);
            if (!pv || (pv->type != qb::Type::Vector && pv->type != qb::Type::Pair)) return true;
            const venue::Vec3 off = venue::rotate(node->rot, vec(pv, {}));
            const venue::Vec3 cur = m.pos(t);
            m.moved = true;
            m.from = cur, m.to = {cur[0] + off[0], cur[1] + off[1], cur[2] + off[2]}, m.m0 = t;
            const float speed = qs::Vm::number(qs::Vm::param(p, K("Speed")), 0), time = qs::Vm::number(qs::Vm::param(p, K("time")), 0);
            const float dist = std::sqrt(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
            m.mDur = time > 0 ? time * 1000.0 : speed > 0 ? dist / speed * 1000.0 : 0.0;
        } else if (method == K("obj_movetopos") || method == K("obj_setposition")) {
            const qb::Value* pv = qs::Vm::param(p, K("position"));
            if (!pv) pv = qs::Vm::positional(p);
            if (!pv || (pv->type != qb::Type::Vector && pv->type != qb::Type::Pair)) return true;
            const venue::Vec3 dst = vec(pv, node->pos);
            const venue::Vec3 cur = m.pos(t);
            m.moved = true;
            m.from = cur, m.to = dst, m.m0 = t, m.mDur = 0;
            if (method == K("obj_movetopos")) {
                const float time = qs::Vm::number(qs::Vm::param(p, K("time")), 0);
                const float speed = qs::Vm::number(qs::Vm::param(p, K("Speed")), 0);
                const float dx = dst[0] - cur[0], dy = dst[1] - cur[1], dz = dst[2] - cur[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                m.mDur = time > 0 ? time * 1000.0 : speed > 0 ? dist / speed * 1000.0 : 0.0;
            }
        } else if (method == K("hide") || method == K("Unhide")) {
            visibility_[obj] = method == K("hide") ? 0 : 1;
        } else if (method == K("AddPulseEvent")) {
            static const char* const kEv[5] = {"hitnote_green", "hitnote_red", "hitnote_yellow", "hitnote_blue", "hitnote_orange"};
            m.pulseFrets = 0;
            if (const qb::Value* ev = qs::Vm::param(p, K("events")); ev && ev->type == qb::Type::Array && ev->arr)
                for (const qb::Value& e : ev->arr->values)
                    for (int k = 0; k < 5; ++k)
                        if (keyOf(&e) == K(kEv[k])) m.pulseFrets |= uint8_t(1 << k);
            m.pulseAmount = qs::Vm::number(qs::Vm::param(p, K("amount")), 1.12f);
            m.pulseTime = qs::Vm::number(qs::Vm::param(p, K("time")), 0.1f);
        } else if (method == K("obj_enablescaling")) {
        } else if (method == K("obj_getscaling")) {
            if (!run.frames.empty() && run.frames.back().locals)
                qs::Vm::set(*run.frames.back().locals, K("scaling"), qs::Vm::makeFloat(m.scale(t)));
        } else if (method == K("obj_applyscaling")) {
            m.s0 = m.sTo = qs::Vm::number(qs::Vm::param(p, K("scale")), 1), m.sDur = 0;
        } else if (method == K("obj_morphscaling")) {
            m.s0 = m.scale(t), m.sT0 = t;
            m.sTo = qs::Vm::number(qs::Vm::param(p, K("target_scale")), 1);
            m.sDur = qs::Vm::number(qs::Vm::param(p, K("blend_duration")), 0) * 1000.0;
        } else if (method == K("obj_spawnscriptnow") || method == K("obj_spawnscriptlater") || method == K("Obj_SwitchScript")) {
            // `{ $script params = {...} }`: the arguments inside one struct.
            const qb::Struct* in = &p;
            if (const qb::Struct* inner = structOf(qs::Vm::positional(p))) in = inner;
            uint32_t scr = 0;
            for (const qb::Item& it : in->items)
                if (!it.name && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && !scr) scr = it.value.key;
            if (SDL_getenv("GD04_OBJLOG")) {
                std::string items;
                for (const qb::Item& it : p.items) items += qbkey::names().get(it.name) + ":" + std::to_string(int(it.value.type)) + " ";
                SDL_Log("objcall %s on %s: script %s; items %s", qbkey::names().get(method).c_str(), qbkey::names().get(obj).c_str(),
                        qbkey::names().get(scr).c_str(), items.c_str());
            }
            if (!scr) return true;
            qb::Struct args;
            if (const qb::Struct* ps = structOf(qs::Vm::param(*in, K("params")))) args = *ps;
            if (method == K("Obj_SwitchScript"))
                for (auto& r : vm_->runs())
                    if (r && r->object == obj && r.get() != &run) vm_->kill(r->id);
            vm_->spawnOn(scr, args, obj);
        } else if (method == K("obj_killspawnedscript")) {
            const uint32_t nm = keyOf(qs::Vm::param(p, K("name")));
            for (auto& r : vm_->runs())
                if (r && r->object == obj && r->name == nm) vm_->kill(r->id);
        } else if (method == K("obj_stopmoving")) {
            m.to = m.pos(t), m.from = m.to, m.mDur = 0;
        } else if (method == K("Obj_WaitRotate")) {
            run.waitFor = [this, obj] {
                const Motion& mm = motion_[obj];
                for (const Axis& a : mm.rot)
                    if (!a.done(vmTime_)) return false;
                return true;
            };
        } else if (method == K("Obj_WaitMove")) {
            run.waitFor = [this, obj] {
                const Motion& mm = motion_[obj];
                return mm.mDur <= 0 || vmTime_ >= mm.m0 + mm.mDur;
            };
        } else {
            handled = false;
            return false;
        }
        return true;
    };
}

bool Particles::nodeMotion(uint32_t node, double ms, gfx::Mat4& delta) const {
    auto it = motion_.find(node);
    if (it == motion_.end() || !v_) return false;
    const venue::Node* n = v_->node(node);
    if (!n) return false;
    const Motion& m = it->second;
    constexpr float kDeg = 3.14159265f / 180.0f;
    const venue::Vec3 extra{m.rot[0].at(ms) * kDeg, m.rot[1].at(ms) * kDeg, m.rot[2].at(ms) * kDeg};
    const venue::Vec3 p = m.pos(ms);
    // world' = T(p) R(node) R(extra) R(node)^-1 T(-node.pos) world [I].
    auto rotM = [](const venue::Quat& q) {
        gfx::Mat4 r;
        const venue::Vec3 ax = venue::rotate(q, {1, 0, 0}), ay = venue::rotate(q, {0, 1, 0}), az = venue::rotate(q, {0, 0, 1});
        for (int k = 0; k < 3; ++k) r.at(k, 0) = ax[size_t(k)], r.at(k, 1) = ay[size_t(k)], r.at(k, 2) = az[size_t(k)];
        return r;
    };
    const gfx::Mat4 rn = rotM(n->rot);
    gfx::Mat4 rnInv = rn.inverseAffine();
    const float sc = m.scale(ms) * pulse(m, ms);
    gfx::Mat4 sm;
    sm.at(0, 0) = sm.at(1, 1) = sm.at(2, 2) = sc;
    delta = gfx::Mat4::translation(p[0], p[1], p[2]) * rn * rotM(venue::quatFromAngles(extra)) * sm * rnInv *
            gfx::Mat4::translation(-n->pos[0], -n->pos[1], -n->pos[2]);
    return true;
}

bool Particles::attach(gfx::RenderDevice& dev, const Disc& disc, std::string& error) {
    dev_ = &dev;
    program_ = dev.createProgram(programDesc(), error);
    if (!program_) return false;
    const uint8_t px[4] = {255, 255, 255, 255};
    gfx::TexLevel l{px, 4};
    white_ = dev.createTexture({1, 1, 1, gfx::TexFormat::RGBA8, gfx::TexKind::Tex2D}, &l, error);
    // The quick materials' textures: .img entries of ZONES/global.pak by name [C data: JOW_Spark01].
    try {
        const Pak pak = Pak::load(disc, "DATA/COMPRESSED/ZONES/global.pak.xen");
        for (const PakEntry& e : pak.entries()) {
            if (e.type != K(".img")) continue;
            auto [p, size] = pak.data(e);
            gh3tex::Texture t = gh3tex::decodeImg(p, size);
            if (!t.supported || t.levels.empty() || t.kind != gfx::TexKind::Tex2D) continue;
            std::string err;
            auto levels = t.texLevels();
            if (const gfx::TextureId id = dev.createTexture(t.desc(), levels.data(), err)) textures_[e.name ? e.name : e.fullName] = id;
        }
    } catch (const std::exception& ex) {
        SDL_Log("particles: global.pak: %s", ex.what());
    }
    return true;
}

void Particles::shutdown() {
    if (dev_) {
        if (program_) dev_->destroyProgram(program_);
        if (vb_) dev_->destroyBuffer(vb_);
        if (white_) dev_->destroyTexture(white_);
        for (auto& [k, t] : textures_) dev_->destroyTexture(t);
    }
    program_ = {}, vb_ = {}, white_ = {};
    vbSize_ = 0;
    textures_.clear();
    dev_ = nullptr;
}

void Particles::create(const venue::Vec3& pos, const qb::Value* params, double t) {
    const qb::Value* pv = params;
    if (pv && (pv->type == qb::Type::Key || pv->type == qb::Type::KeyPtr)) pv = g_->find(pv->key);
    const qb::Struct* src = structOf(pv);
    if (!src) return;
    // The parameters with their included structs ({$GP_Dive_CobraSparks01
    // color = ...}: an unnamed reference to a global struct, then overrides).
    qb::Struct merged;
    std::function<void(const qb::Struct&, int)> merge = [&](const qb::Struct& st, int depth) {
        for (const qb::Item& it : st.items) {
            if (it.name == 0 && depth < 4) {
                const qb::Value* inc = nullptr;
                if (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) inc = g_->find(it.value.key);
                else if (it.value.type == qb::Type::Struct) inc = &it.value;
                if (const qb::Struct* is = structOf(inc)) {
                    merge(*is, depth + 1);
                    continue;
                }
            }
            merged.items.push_back(it);
        }
    };
    merge(*src, 0);
    const qb::Struct* st = &merged;
    auto f = [&](const char* name) -> const qb::Value* {
        const qb::Value* r = nullptr;
        for (const qb::Item& it : merged.items)
            if (it.name == K(name)) r = &it.value;
        return r;
    };
    Params p;
    p.box = vec(f("emitrangedims"), {});
    p.target = vec(f("emit_target"), {0, 1, 0});
    p.force = vec(f("force"), {});
    p.spread = num(f("anglespread"), 0);
    const venue::Vec3 life = vec(f("liferange"), {1, 1, 0});
    p.lifeMin = life[0], p.lifeMax = std::max(life[0], life[1]);
    p.rate = num(f("emit_rate"), 0);
    p.lifetime = num(f("lifetime"), 0);
    p.burst = int(num(f("emitnum"), 0));
    p.delay = num(f("emitdelaystart"), 0);
    if (const qb::Value* sr = f("sizerange"); sr && sr->type == qb::Type::Array && sr->arr && sr->arr->values.size() >= 2) {
        const venue::Vec3 a = vec(&sr->arr->values[0], {0.1f, 0, 0}), b = vec(&sr->arr->values[1], {0.1f, 0, 0});
        p.sizeStart = a[0], p.sizeStartVar = a[1], p.sizeEnd = b[0], p.sizeEndVar = b[1];
    }
    const venue::Vec3 speed = vec(f("speedrange"), {1, 1, 0});
    p.speedMin = speed[0], p.speedMax = std::max(speed[0], speed[1]);
    p.alignWithPath = false;
    for (const qb::Item& it : st->items)
        if (it.name == 0 && it.value.type == qb::Type::Key && it.value.key == K("alignwithpath")) p.alignWithPath = true;
    p.pathTime = num(f("pathfollowtime"), 0.1f);
    for (const qb::Item& it : st->items)
        if (it.name == 0 && it.value.type == qb::Type::Key && it.value.key == K("burn")) p.burn = std::max(0.0f, num(f("burnvalue"), 0.0f));
    {
        const uint32_t tl = keyOf(f("texturelayout"));
        p.layout = tl == K("layout2x2") ? 1 : tl == K("layout1x2") ? 2 : tl == K("layout2x1") ? 3 : 0;
        p.animFps = num(f("quadanimationfps"), 0);
    }
    if (const qb::Value* c = f("color"); c && c->type == qb::Type::Int) p.color = uint32_t(c->i);
    if (const qb::Value* pc = f("particlecolor"); pc && pc->type == qb::Type::Array && pc->arr)
        for (const qb::Value& c : pc->arr->values) p.gradient.push_back(uint32_t(c.i));
    if (const qb::Struct* qm = structOf(f("quickmaterial"))) {
        p.texture = keyOf(g_->field(*qm, K("diffusetexture")));
        // blendmode (FUN_823a5f18): add 1, subtract / sub 2, blend 3, modulate 4, brighten 5, else 0 [C].
        const uint32_t bm = keyOf(g_->field(*qm, K("blendmode")));
        p.blend = bm == K("add") ? 1 : (bm == K("subtract") || bm == K("sub")) ? 2 : bm == K("blend") ? 3 : bm == K("modulate") ? 4
                  : bm == K("brighten") ? 5 : 0;
        p.additive = p.blend == 1;
        p.alphaCut = num(g_->field(*qm, K("alphacutoff")), 0) / 255.0f;
    }
    System s;
    s.p = std::move(p);
    s.pos = pos;
    s.start = t;
    s.seed = nextSeed_ = nextSeed_ * 6364136223846793005ull + 1442695040888963407ull;
    static const bool log = std::getenv("GD04_PARTICLES_LOG") != nullptr;
    if (log)
        SDL_Log("particles: %.0f ms system at (%.2f %.2f %.2f): burst %d, rate %.0f for %.2f s, texture %s", t, pos[0], pos[1], pos[2],
                s.p.burst, double(s.p.rate), double(s.p.lifetime), qbkey::names().get(s.p.texture).c_str());
    systems_.push_back(std::move(s));
    ++created_;
}

void Particles::step(double t) {
    // The performance changes (LightShow_SetParams performance=...: the
    // mapping's transitions.performance scripts) and pyro notes at their times.
    while (perf_ && perfAt_ < perf_->changes.size() && perf_->changes[perfAt_].first <= t) {
        const venue::Performance p = perf_->changes[perfAt_++].second;
        if (p == lastPerf_) continue;
        lastPerf_ = p;
        qb::Struct params;
        qs::Vm::set(params, K("performance"),
                    qs::Vm::makeKey(K(p == venue::Performance::Poor ? "poor" : p == venue::Performance::Good ? "good" : "medium")));
        for (uint32_t s : perfScripts_) vm_->run(s, params);
    }
    while (pyroAt_ < pyroTimes_.size() && pyroTimes_[pyroAt_] <= t) {
        ++pyroAt_;
        if (!pyroScript_) continue;
        qb::Struct params;
        qs::Vm::set(params, K("performance"),
                    qs::Vm::makeKey(K(lastPerf_ == venue::Performance::Poor ? "poor" : lastPerf_ == venue::Performance::Good ? "good" : "medium")));
        vm_->run(pyroScript_, params);
    }
    vm_->update(kStepMs);
}

void Particles::advance(double ms) {
    if (!vm_) return;
    while (vmTime_ + kStepMs <= ms) {
        vmTime_ += kStepMs;
        step(vmTime_);
    }
    systems_.erase(std::remove_if(systems_.begin(), systems_.end(), [&](const System& s) { return s.end() < ms; }), systems_.end());
}

void Particles::draw(const venue::CamPose& cam, double ms) {
    if (!dev_ || !program_ || systems_.empty()) return;
    gfx::RenderDevice& d = *dev_;
    const venue::Vec3 fwd = venue::rotate(cam.rot, {0, 0, 1});
    const venue::Vec3 up = venue::rotate(cam.rot, {0, 1, 0});
    const venue::Vec3 right = venue::rotate(cam.rot, {-1, 0, 0});
    gfx::Mat4 view;
    const venue::Vec3 rows[3] = {right, up, {-fwd[0], -fwd[1], -fwd[2]}};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) view.at(r, c) = rows[r][size_t(c)];
        view.at(r, 3) = -(rows[r][0] * cam.pos[0] + rows[r][1] * cam.pos[1] + rows[r][2] * cam.pos[2]);
    }
    const float aspect = gfx::kLogicalW / gfx::kLogicalH;
    const float hfov = cam.fov * 3.14159265f / 180.0f;
    const float yfov = 2.0f * std::atan(std::tan(hfov * 0.5f) / aspect);
    const gfx::Mat4 vp = gfx::Mat4::perspective(yfov, aspect, 0.05f, 4000.0f, d.info().clipDepthZeroToOne) * view;

    struct Batch {
        uint32_t texture = 0;
        bool additive = true;
        int blend = 1;
        float burn = 0;
        float alphaCut = 0;
        size_t first = 0, count = 0;
    };
    std::vector<Batch> batches;
    std::vector<PVtx> verts;
    for (const System& s : systems_) {
        const Params& p = s.p;
        const double age = (ms - s.start) / 1000.0 - p.delay;
        if (age < 0) continue;
        Batch b{p.texture, p.additive, p.blend, p.burn, p.alphaCut, verts.size(), 0};
        // The burst at the start, then emit_rate a second for `lifetime` [I].
        const int continuous = int(std::floor(double(p.rate) * double(p.lifetime)));
        const int total = p.burst + continuous;
        venue::Vec3 dir = p.target;
        const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (dl > 1e-6f) for (float& c : dir) c /= dl;
        else dir = {0, 1, 0};
        // Two axes across the emit direction for the spread cone.
        venue::Vec3 ax = std::fabs(dir[1]) < 0.9f ? venue::Vec3{dir[2], 0, -dir[0]} : venue::Vec3{1, 0, 0};
        float al = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        for (float& c : ax) c /= al;
        const venue::Vec3 ay{dir[1] * ax[2] - dir[2] * ax[1], dir[2] * ax[0] - dir[0] * ax[2], dir[0] * ax[1] - dir[1] * ax[0]};
        for (int i = 0; i < total && verts.size() / 6 < kMaxParticles; ++i) {
            const double born = i < p.burst ? 0.0 : double(i - p.burst) / std::max(1.0f, p.rate);
            const float life = p.lifeMin + (p.lifeMax - p.lifeMin) * rnd(s.seed, uint64_t(i), 0);
            const double a = age - born;
            if (a < 0 || a > life) continue;
            const float tl = float(a / life);
            // Start: the emitter box (half extents) [I]; direction within the spread cone.
            venue::Vec3 p0{s.pos[0] + p.box[0] * (rnd(s.seed, uint64_t(i), 1) - 0.5f), s.pos[1] + p.box[1] * (rnd(s.seed, uint64_t(i), 2) - 0.5f),
                           s.pos[2] + p.box[2] * (rnd(s.seed, uint64_t(i), 3) - 0.5f)};
            const float th = p.spread * 0.5f * 3.14159265f / 180.0f * std::sqrt(rnd(s.seed, uint64_t(i), 4));
            const float ph = 6.2831853f * rnd(s.seed, uint64_t(i), 5);
            const float st = std::sin(th), ct = std::cos(th);
            venue::Vec3 vdir;
            for (size_t k = 0; k < 3; ++k) vdir[k] = dir[k] * ct + (ax[k] * std::cos(ph) + ay[k] * std::sin(ph)) * st;
            const float speed = p.speedMin + (p.speedMax - p.speedMin) * rnd(s.seed, uint64_t(i), 6);
            const float at = float(a);
            venue::Vec3 pos, vel;
            for (size_t k = 0; k < 3; ++k) {
                vel[k] = vdir[k] * speed + p.force[k] * at;
                pos[k] = p0[k] + vdir[k] * speed * at + 0.5f * p.force[k] * at * at;
            }
            const float s0 = p.sizeStart + p.sizeStartVar * rnd(s.seed, uint64_t(i), 7);
            const float s1 = p.sizeEnd + p.sizeEndVar * rnd(s.seed, uint64_t(i), 8);
            const float size = std::max(0.0f, s0 + (s1 - s0) * tl);
            // Colour: the system's colour x the particlecolor gradient over the life (RGBA words) [I].
            float col[4];
            for (int k = 0; k < 4; ++k) col[k] = float((p.color >> (24 - 8 * k)) & 0xFF) / 255.0f;
            if (!p.gradient.empty()) {
                const float gpos = tl * float(p.gradient.size() - 1);
                const size_t g0 = std::min(p.gradient.size() - 1, size_t(gpos));
                const size_t g1 = std::min(p.gradient.size() - 1, g0 + 1);
                const float w = gpos - float(g0);
                for (int k = 0; k < 4; ++k) {
                    const float c0 = float((p.gradient[g0] >> (24 - 8 * k)) & 0xFF) / 255.0f;
                    const float c1 = float((p.gradient[g1] >> (24 - 8 * k)) & 0xFF) / 255.0f;
                    col[k] *= c0 + (c1 - c0) * w;
                }
            }
            uint8_t c8[4];
            for (int k = 0; k < 4; ++k) c8[k] = uint8_t(std::clamp(col[k], 0.0f, 1.0f) * 255.0f + 0.5f);
            // A camera-facing quad; alignwithpath stretches it back along the
            // velocity over pathfollowtime (the sparks' trails) [I].
            venue::Vec3 sx = right, sy = up;
            venue::Vec3 centre = pos;
            if (p.alignWithPath) {
                venue::Vec3 trail;
                for (size_t k = 0; k < 3; ++k) trail[k] = vel[k] * p.pathTime;
                const float tlen = std::sqrt(trail[0] * trail[0] + trail[1] * trail[1] + trail[2] * trail[2]);
                if (tlen > 1e-5f) {
                    venue::Vec3 td{trail[0] / tlen, trail[1] / tlen, trail[2] / tlen};
                    venue::Vec3 side{td[1] * fwd[2] - td[2] * fwd[1], td[2] * fwd[0] - td[0] * fwd[2], td[0] * fwd[1] - td[1] * fwd[0]};
                    const float sl = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
                    if (sl > 1e-5f) {
                        for (size_t k = 0; k < 3; ++k) sx[k] = side[k] / sl, sy[k] = td[k] * (tlen * 0.5f + size) / std::max(size, 1e-4f);
                        for (size_t k = 0; k < 3; ++k) centre[k] = pos[k] - trail[k] * 0.5f;
                    }
                }
            }
            const float h = size * 0.5f;
            const float cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
            // The flipbook cell: (int(age x quadanimationfps) + the particle's
            // random) & mask, or the random alone; 2x2 / 1x2 / 2x1 cells [C 82427E08].
            float cu = 0, cv = 0, cw = 1, chh = 1;
            if (p.layout) {
                const int mask = p.layout == 1 ? 3 : 1;
                const int r = int(rnd(s.seed, uint64_t(i), 9) * 65536.0f);
                const int cell = (p.animFps > 0 ? int(float(a) * p.animFps) + r : r) & mask;
                if (p.layout == 1) cw = chh = 0.5f, cu = 0.5f * float(cell & 1), cv = 0.5f * float(cell >> 1);
                else if (p.layout == 2) cw = 0.5f, cu = 0.5f * float(cell);
                else chh = 0.5f, cv = 0.5f * float(cell);
            }
            PVtx q[4];
            for (int k = 0; k < 4; ++k) {
                for (size_t j = 0; j < 3; ++j) q[k].pos[j] = centre[j] + (sx[j] * cx[k] + sy[j] * cy[k]) * h;
                std::memcpy(q[k].col, c8, 4);
                q[k].uv[0] = (cx[k] * 0.5f + 0.5f) * cw + cu, q[k].uv[1] = (0.5f - cy[k] * 0.5f) * chh + cv;
            }
            for (int k : {0, 1, 2, 0, 2, 3}) verts.push_back(q[k]);
        }
        b.count = verts.size() - b.first;
        if (b.count) batches.push_back(b);
    }
    if (verts.empty()) return;
    const size_t bytes = verts.size() * sizeof(PVtx);
    if (bytes > vbSize_) {
        if (vb_) d.destroyBuffer(vb_);
        vbSize_ = std::max(bytes, size_t(1) << 16);
        vb_ = d.createBuffer(gfx::BufferKind::Vertex, vbSize_, nullptr, true);
    }
    d.updateBuffer(vb_, 0, verts.data(), bytes);
    static const gfx::VertexLayout layout = [] {
        gfx::VertexLayout l;
        l.stride = sizeof(PVtx);
        l.attribs[0] = {gfx::kAttribPos, 3, gfx::AttribType::Float, uint16_t(offsetof(PVtx, pos))};
        l.attribs[1] = {gfx::kAttribColor, 4, gfx::AttribType::UNorm8, uint16_t(offsetof(PVtx, col))};
        l.attribs[2] = {gfx::kAttribUv, 2, gfx::AttribType::Float, uint16_t(offsetof(PVtx, uv))};
        return l;
    }();
    gfx::DrawCall c;
    c.program = program_;
    c.layout = &layout;
    c.vertices = vb_;
    c.state.blend = true;
    c.state.depthTest = true, c.state.depthWrite = false;
    c.state.depthFunc = gfx::CompareFunc::LessEqual;
    c.samplers[0] = {gfx::Filter::Trilinear, gfx::Wrap::Clamp, 0};
    for (const Batch& b : batches) {
        struct {
            float vp[16];
            float params[4];
        } u{};
        std::memcpy(u.vp, vp.m, sizeof u.vp);
        u.params[0] = b.alphaCut, u.params[1] = b.burn;
        c.uniforms[0] = {&u, sizeof u, 0};
        auto it = textures_.find(b.texture);
        c.textures[0] = it != textures_.end() ? it->second : white_;
        // The material blend table (823DC180) by the system's blendmode; burn:
        // mode 9, ONE / ONE_MINUS_SRC_ALPHA [C].
        c.state.blend = true, c.state.op = gfx::BlendOp::Add;
        using F = gfx::BlendFactor;
        if (b.burn > 0) c.state.src = F::One, c.state.dst = F::InvSrcAlpha;
        else switch (b.blend) {
            case 0: c.state.blend = false; break;
            case 1: c.state.src = F::SrcAlpha, c.state.dst = F::One; break;
            case 2: c.state.src = F::SrcAlpha, c.state.dst = F::One, c.state.op = gfx::BlendOp::ReverseSubtract; break;
            case 4: c.state.src = F::Zero, c.state.dst = F::SrcAlpha; break;
            case 5: c.state.src = F::DstColor, c.state.dst = F::One; break;
            default: c.state.src = F::SrcAlpha, c.state.dst = F::InvSrcAlpha; break;
            }
        c.first = int(b.first), c.count = int(b.count);
        d.draw(c);
    }
}
