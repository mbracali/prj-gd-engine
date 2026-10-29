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
    for (const char* noop : {"CallScriptOnNode", "SpawnSound", "PlaySound", "Obj_PlaySound", "SoundEvent"})
        vm_->addNative(noop, [](qs::Vm&, qs::ScriptRun&, qb::Struct&) { return true; });
    using ull = unsigned long long;
    SDL_Log("particles %s: %llu pyro notes (%s), %llu performance scripts", v.zone.c_str(), ull(pyroTimes_.size()),
            pyroScript_ ? qbkey::names().get(pyroScript_).c_str() : "no pyro script", ull(perfScripts_.size()));
    if (std::getenv("GD04_PARTICLES_LOG"))
        for (double t : pyroTimes_) SDL_Log("particles: pyro at %.0f ms", t);
    return pyroScript_ || !perfScripts_.empty();
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
    if (const qb::Value* c = f("color"); c && c->type == qb::Type::Int) p.color = uint32_t(c->i);
    if (const qb::Value* pc = f("particlecolor"); pc && pc->type == qb::Type::Array && pc->arr)
        for (const qb::Value& c : pc->arr->values) p.gradient.push_back(uint32_t(c.i));
    if (const qb::Struct* qm = structOf(f("quickmaterial"))) {
        p.texture = keyOf(g_->field(*qm, K("diffusetexture")));
        p.additive = keyOf(g_->field(*qm, K("blendmode"))) != K("blend");
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
        float alphaCut = 0;
        size_t first = 0, count = 0;
    };
    std::vector<Batch> batches;
    std::vector<PVtx> verts;
    for (const System& s : systems_) {
        const Params& p = s.p;
        const double age = (ms - s.start) / 1000.0 - p.delay;
        if (age < 0) continue;
        Batch b{p.texture, p.additive, p.alphaCut, verts.size(), 0};
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
            PVtx q[4];
            for (int k = 0; k < 4; ++k) {
                for (size_t j = 0; j < 3; ++j) q[k].pos[j] = centre[j] + (sx[j] * cx[k] + sy[j] * cy[k]) * h;
                std::memcpy(q[k].col, c8, 4);
                q[k].uv[0] = cx[k] * 0.5f + 0.5f, q[k].uv[1] = 0.5f - cy[k] * 0.5f;
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
        u.params[0] = b.alphaCut;
        c.uniforms[0] = {&u, sizeof u, 0};
        auto it = textures_.find(b.texture);
        c.textures[0] = it != textures_.end() ? it->second : white_;
        c.state.src = gfx::BlendFactor::SrcAlpha;
        c.state.dst = b.additive ? gfx::BlendFactor::One : gfx::BlendFactor::InvSrcAlpha;
        c.first = int(b.first), c.count = int(b.count);
        d.draw(c);
    }
}
