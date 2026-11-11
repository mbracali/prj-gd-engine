// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/gh3_shader.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

#include "data/disc.h"
#include "data/qbkey.h"
#include "render/hitch.h"

namespace gh3shader {

namespace {
std::atomic<const Disc*> lastDisc{nullptr};
}  // namespace

const Disc* libraryDisc() { return lastDisc.load(); }

std::shared_ptr<const matlib::Library> library(const Disc& disc) {
    lastDisc.store(&disc);
    static std::mutex mutex;
    static std::string cachedPath;
    static std::shared_ptr<const matlib::Library> cached;
    static bool off = false;  // gh4's library read and switched off (below)
    std::lock_guard<std::mutex> lock(mutex);
    if ((cached || off) && cachedPath == disc.path()) return cached;
    cached.reset(), off = false;
    cachedPath = disc.path();
    // fxfiles\MaterialLibrary.bin.xen (Function_82384668) [C]; the disc
    // also holds a compressed copy under DATA/COMPRESSED/FXFILES.
    for (const char* path : {"DATA/FXFILES/MaterialLibrary.bin.xen", "DATA/COMPRESSED/FXFILES/MaterialLibrary.bin.xen"}) {
        if (!disc.exists(path)) continue;
        try {
            const Bytes b = disc.read(path);
            auto lib = std::make_shared<matlib::Library>();
            lib->parse(b.data(), b.size());
            // gh4 (MATL v3): its engine constants sit at other registers than
            // GH3's (xenos::objectSlot, Globals, docs/render.md). GD10_GH4_SHADERS=0
            // (a development aid) switches them off: the approximations draw.
            if (const char* e = SDL_getenv("GD10_GH4_SHADERS"); e && e[0] == '0') {
                SDL_Log("material library: %llu templates; its shaders are off (GD10_GH4_SHADERS=0)",
                        static_cast<unsigned long long>(lib->templates().size()));
                off = true;
                return nullptr;
            }
            using ull = unsigned long long;
            SDL_Log("material library: %llu templates, %llu shaders", ull(lib->templates().size()), ull(lib->shaderCount()));
            cached = lib;
            return cached;
        } catch (const std::exception& ex) {
            SDL_Log("material library %s: %s", path, ex.what());
        }
    }
    return nullptr;
}

void Globals::setMatrix(int r, const gfx::Mat4& m, int rows) {
    for (int i = 0; i < rows; ++i) set(r + i, m.at(0, i), m.at(1, i), m.at(2, i), m.at(3, i));
}

void setShadowGlobals(Globals& g, const DynamicShadow& s, const gfx::Mat4& view) {
    if (!s) return;
    const gfx::Mat4 inv = view.inverseAffine();
    g.setMatrix(kRegViewToLight0, s.vp[0] * inv, 4);
    g.setMatrix(kRegViewToLight1, s.vp[1] * inv, 4);
    g.set(kRegShadowMapConstants, s.blend[0], s.blend[1], s.fadeStart, s.fadeEnd);
    g.set(kRegShadowLevel, 0, 0, 1.0f - s.intensity, s.intensity);
    g.set(kRegShadowMapSize, s.size[0], s.size[1], 1.0f / s.size[0], 1.0f / s.size[1]);
}

gfx::ProgramDesc programDesc(const xenos::Program& p, const std::string& name, bool clipZeroToOne) {
    gfx::ProgramDesc d;
    d.name = "gh3:" + name;
    d.vertex = p.vertex;
    d.fragment = p.fragment;
    if (clipZeroToOne) d.defines = "#define GD_CLIP_ZO 1\n";
    d.uniformBlocks = {"GdXG", "GdXVM", "GdXPM", "GdXO"};
    for (int k = 0; k < 16; ++k) d.samplers.push_back("sT" + std::to_string(k));
    return d;
}

void Programs::init(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib) {
    shutdown();
    dev_ = &dev;
    lib_ = std::move(lib);
    const uint8_t px[4] = {255, 255, 255, 255};
    std::string err;
    gfx::TexLevel l{px, 4};
    white_ = dev.createTexture({1, 1, 1, gfx::TexFormat::RGBA8, gfx::TexKind::Tex2D}, &l, err);
    gfx::TexLevel faces[6] = {l, l, l, l, l, l};
    whiteCube_ = dev.createTexture({1, 1, 1, gfx::TexFormat::RGBA8, gfx::TexKind::Cube}, faces, err);
}

namespace {

// The programs of every Programs, by device serial, library and template /
// technique: compiled once a run (they stay until the device goes).
struct SharedPrograms {
    std::mutex mutex;
    std::map<std::tuple<uint64_t, const matlib::Library*, std::string>, std::shared_ptr<Program>> programs;
    std::vector<std::shared_ptr<const matlib::Library>> libraries;  // kept: their addresses stay theirs
};

SharedPrograms& sharedPrograms() {
    static SharedPrograms s;
    return s;
}

}  // namespace

void Programs::shutdown() {
    if (!dev_) return;
    programs_.clear();  // (shared: they stay with the device)
    requested_.clear();
    deferred_.clear();
    named_.clear();
    if (white_) dev_->destroyTexture(white_);
    if (whiteCube_) dev_->destroyTexture(whiteCube_);
    white_ = {}, whiteCube_ = {};
    dev_ = nullptr;
    lib_.reset();
}

std::unique_ptr<Program> Programs::make(int vs, int ps, const std::string& name, bool async) {
    std::unique_ptr<Program> out;
    hitch::Scope hs("shader");
    const xenos::Program x = [&] {
        hitch::Scope ht("shader translate");
        return xenos::translate(lib_->shader(vs), lib_->shader(ps));
    }();
    if (!x.ok) {
        SDL_Log("gh3 shader %s: not translated: %s", name.c_str(), x.error.c_str());
        return out;
    }
    if (SDL_getenv("GD10_XENOS_DUMP")) SDL_Log("gh3 shader %s\n--- vertex\n%s\n--- pixel\n%s", name.c_str(), x.vertex.c_str(), x.fragment.c_str());
    std::string err;
    const gfx::ProgramDesc desc = programDesc(x, name, dev_->info().clipDepthZeroToOne);
    const gfx::ProgramId pid = async ? dev_->beginProgram(desc, err) : dev_->createProgram(desc, err);
    if (!pid) {
        SDL_Log("gh3 shader %s: %s", name.c_str(), err.c_str());
        return out;
    }
    out = std::make_unique<Program>();
    out->id = pid;
    out->samplers = x.samplers, out->cubes = x.cubes, out->attribs = x.attribs;
    out->name = name;
    out->compiling = async;
    return out;
}

const Program* Programs::ready(std::shared_ptr<Program>& p) {
    if (p && p->compiling) {
        std::string err;
        int r;
        while ((r = dev_->programStatus(p->id, err)) == 0) {}
        p->compiling = false;
        if (r < 0) {
            SDL_Log("gh3 shader %s: %s", p->name.c_str(), err.c_str());
            p->id = {};
        }
    }
    return p && p->id ? p.get() : nullptr;
}

void Programs::request(uint32_t key, matlib::Technique t) {
    if (!dev_ || !lib_) return;
    if (!dev_->backgroundCompile()) {
        deferred_.emplace_back(key, "#" + std::to_string(int(t)));
        return;
    }
    const uint64_t id = uint64_t(key) << 8 | t;
    if (programs_.count(id)) return;
    const matlib::Template* tp = lib_->find(key);
    const bool ok = tp && tp->vs[t] >= 0 && tp->ps[t] >= 0;
    std::shared_ptr<Program>& local = programs_[id];
    shared(std::to_string(key) + "#" + std::to_string(int(t)), ok ? tp->vs[t] : -1, ok ? tp->ps[t] : -1, tp ? tp->name : std::string(), local, true);
    if (local && local->compiling) requested_.push_back(local);
}

void Programs::request(uint32_t key, const std::string& technique) {
    if (!dev_ || !lib_) return;
    if (!dev_->backgroundCompile()) {
        deferred_.emplace_back(key, technique);
        return;
    }
    const std::string id = std::to_string(key) + ":" + technique;
    if (named_.count(id)) return;
    const matlib::Template* tp = lib_->find(key);
    const matlib::TechniqueRef* tr = tp ? tp->technique(technique) : nullptr;
    const bool ok = tr && tr->vs[0] >= 0 && tr->ps[0] >= 0;
    std::shared_ptr<Program>& local = named_[id];
    shared(id, ok ? tr->vs[0] : -1, ok ? tr->ps[0] : -1, ok ? tp->name + "." + technique : std::string(), local, true);
    if (local && local->compiling) requested_.push_back(local);
}

bool Programs::settle() {
    // Finished a slice a frame: a driver may do the last of its work as the
    // program is first asked about (NVIDIA: 20..45 ms each on a cold cache).
    size_t done = 0;
    for (; done < deferred_.size(); ++done) {
        const auto& [key, t] = deferred_[done];
        const bool cached = t[0] == '#' ? has(key, matlib::Technique(std::atoi(t.c_str() + 1))) : named_.count(std::to_string(key) + ":" + t) != 0;
        if (!cached && !hitch::slice()) break;
        if (t[0] == '#') (void)get(key, matlib::Technique(std::atoi(t.c_str() + 1)));
        else (void)get(key, t);
    }
    deferred_.erase(deferred_.begin(), deferred_.begin() + std::ptrdiff_t(done));
    if (!deferred_.empty()) return false;
    for (auto it = requested_.begin(); it != requested_.end();) {
        Program& p = **it;
        if (p.compiling) {
            if (!hitch::slice()) return false;
            std::string err;
            const int r = dev_->programStatus(p.id, err);
            if (r == 0) {
                ++it;
                continue;
            }
            p.compiling = false;
            if (r < 0) {
                SDL_Log("gh3 shader %s: %s", p.name.c_str(), err.c_str());
                p.id = {};
            }
        }
        it = requested_.erase(it);
    }
    return requested_.empty();
}

const Program* Programs::shared(const std::string& id, int vs, int ps, const std::string& name, std::shared_ptr<Program>& local, bool async) {
    SharedPrograms& sp = sharedPrograms();
    const auto key = std::make_tuple(dev_->serial(), lib_.get(), id);
    {
        std::lock_guard<std::mutex> lock(sp.mutex);
        if (auto it = sp.programs.find(key); it != sp.programs.end()) {
            local = it->second;
            return async ? local.get() : ready(local);
        }
    }
    if (vs >= 0 && ps >= 0) local = make(vs, ps, name, async);
    {
        std::lock_guard<std::mutex> lock(sp.mutex);
        if (std::find(sp.libraries.begin(), sp.libraries.end(), lib_) == sp.libraries.end()) sp.libraries.push_back(lib_);
        sp.programs[key] = local;
    }
    return async ? local.get() : ready(local);
}

bool Programs::has(uint32_t key, matlib::Technique t) const {
    if (programs_.count(uint64_t(key) << 8 | t)) return true;
    if (!dev_ || !lib_) return false;
    SharedPrograms& sp = sharedPrograms();
    std::lock_guard<std::mutex> lock(sp.mutex);
    return sp.programs.count(std::make_tuple(dev_->serial(), lib_.get(), std::to_string(key) + "#" + std::to_string(int(t)))) != 0;
}

const Program* Programs::get(uint32_t key, matlib::Technique t) {
    if (!dev_ || !lib_) return nullptr;
    const uint64_t id = uint64_t(key) << 8 | t;
    auto it = programs_.find(id);
    if (it != programs_.end()) return ready(it->second);
    const matlib::Template* tp = lib_->find(key);
    const bool ok = tp && tp->vs[t] >= 0 && tp->ps[t] >= 0;
    return shared(std::to_string(key) + "#" + std::to_string(int(t)), ok ? tp->vs[t] : -1, ok ? tp->ps[t] : -1, tp ? tp->name : std::string(),
                  programs_[id]);
}

const Program* Programs::get(uint32_t key, const std::string& technique) {
    if (!dev_ || !lib_) return nullptr;
    const std::string id = std::to_string(key) + ":" + technique;
    auto it = named_.find(id);
    if (it != named_.end()) return ready(it->second);
    const matlib::Template* tp = lib_->find(key);
    const matlib::TechniqueRef* tr = tp ? tp->technique(technique) : nullptr;
    const bool ok = tr && tr->vs[0] >= 0 && tr->ps[0] >= 0;
    return shared(id, ok ? tr->vs[0] : -1, ok ? tr->ps[0] : -1, ok ? tp->name + "." + technique : std::string(), named_[id]);
}

void fillMaterialConstants(const scene::Material& m, float alphaRef, MaterialConstants& out) {
    out = MaterialConstants{};
    for (size_t i = 0; i < m.constsA.size() && i < 63; ++i)
        for (size_t k = 0; k < 4; ++k) out.vs[i][k] = m.constsA[i][k];
    for (size_t i = 0; i < m.constsB.size() && i < 63; ++i)
        for (size_t k = 0; k < 4; ++k) out.ps[i][k] = m.constsB[i][k];
    out.ps[63][0] = alphaRef;
    out.vs[63][0] = -1.0f;
}

LightIn lightIn(const venue::Light& l, const venue::LightState& s) {
    LightIn li;
    li.type = l.type == venue::LightType::Ambient ? LightIn::Ambient : l.type == venue::LightType::Directional ? LightIn::Directional
              : l.type == venue::LightType::Point ? LightIn::Point : l.type == venue::LightType::Prt ? LightIn::Prt : LightIn::Spot;
    for (int k = 0; k < 4; ++k) li.prt[k] = {l.prt[k][0], l.prt[k][1], l.prt[k][2]};
    li.pos = {l.pos[0], l.pos[1], l.pos[2]};
    li.dir = {l.dir[0], l.dir[1], l.dir[2]};
    li.color = {s.color[0], s.color[1], s.color[2]};
    li.intensity = s.intensity, li.specular = s.specular, li.shadow = s.shadow;
    if (li.type == LightIn::Point || li.type == LightIn::Spot) {
        // Linear in the squared distance between attenstart and attenend [C 822BE020].
        li.attA = l.attenEnd * l.attenEnd;
        li.attB = 1.0f / (l.attenEnd * l.attenEnd - l.attenStart * l.attenStart + 0.0001f);
    }
    if (li.type == LightIn::Spot) {
        // hotspot / field: the full cone angles in degrees [I: half of each
        // against the cosine the shaders compute].
        const float kDeg = 3.14159265f / 360.0f;
        const float field = std::max(l.field, l.hotspot), hot = std::min(l.hotspot, field);
        li.coneX = std::cos(field * kDeg);
        li.coneY = 1.0f / std::max(std::cos(hot * kDeg) - li.coneX, 0.001f);
    }
    li.dynamic = l.dynamic, li.shadowLight = l.shadowLight;
    li.boxCenter = {l.boxCenter[0], l.boxCenter[1], l.boxCenter[2]};
    li.boxHalf = {l.boxHalf[0], l.boxHalf[1], l.boxHalf[2]};
    return li;
}

bool groupLights(uint32_t lightGroup, uint32_t objectGroup) {
    static const uint32_t kDefault = qbkey::key("defaultgroup"), kBand = qbkey::key("band"), kAlt = qbkey::key("alt_band");
    if (!lightGroup) lightGroup = kDefault;
    if (!objectGroup) objectGroup = kDefault;
    return lightGroup == objectGroup || lightGroup == kDefault || (lightGroup == kBand && objectGroup == kAlt);
}

void lightObject(const LightIn* lights, size_t count, const gfx::Vec3& p, ObjectConstants& out) {
    for (int r = kRegAmbient; r < kRegSpotLight + 20; ++r) out.set(r, 0, 0, 0, 0);
    for (int r = kRegPointAtten; r < kRegSpotAtten + 4; ++r) out.set(r, 0, 0, 0, 0);
    struct Cand {
        const LightIn* l;
        float scale, inten, att, score;
    };
    Cand cand[32];
    int n = 0;
    for (size_t i = 0; i < count && n < 32; ++i) {
        const LightIn& l = lights[i];
        if (!l.dynamic && (std::fabs(p.x - l.boxCenter.x) > l.boxHalf.x || std::fabs(p.y - l.boxCenter.y) > l.boxHalf.y ||
                           std::fabs(p.z - l.boxCenter.z) > l.boxHalf.z))
            continue;
        const float a = l.groupScale;
        const float inten = l.intensity * a;
        const gfx::Vec3 d = l.pos - p;
        const float att = std::clamp((l.attA - (gfx::dot(d, d) + 0.0001f)) * l.attB, 0.0f, 1.0f);
        const float lum = l.color.x * 0.299f + l.color.y * 0.587f + l.color.z * 0.114f;
        cand[n++] = {&l, a, inten, att, lum * inten * (l.shadowLight ? 50000.0f : 1.0f) * att};
    }
    std::stable_sort(cand, cand + n, [](const Cand& x, const Cand& y) { return x.score > y.score; });
    gfx::Vec3 amb{}, sh[4]{};
    const Cand* slot[4] = {};
    int ndir = 0, npoint = 0, nspot = 0;
    auto addSh = [&](const gfx::Vec3& c, const gfx::Vec3& travel) {
        // 823DA668: SH0 += C x 4/17, SH1..3 += C x 8/17 x l (l = towards the light) [C].
        const gfx::Vec3 l = travel * -1.0f;
        sh[0] = sh[0] + c * (4.0f / 17.0f);
        sh[1] = sh[1] + c * (8.0f / 17.0f * l.x);
        sh[2] = sh[2] + c * (8.0f / 17.0f * l.y);
        sh[3] = sh[3] + c * (8.0f / 17.0f * l.z);
    };
    auto norm = [](const gfx::Vec3& v) {
        const float len = std::sqrt(gfx::dot(v, v));
        return len > 0 ? v * (1.0f / len) : gfx::Vec3{0, 0, 0};
    };
    for (int i = 0; i < n; ++i) {
        const Cand& c = cand[i];
        const LightIn& l = *c.l;
        switch (l.type) {
        case LightIn::Directional:
            if (ndir > 0) addSh(l.color * c.inten, norm(l.dir));
            if (ndir < 4) slot[ndir++] = &c;
            break;
        case LightIn::Point:
            if (npoint < 4) {
                // The vertex shaders light by distance and N.L themselves [C microcode].
                const gfx::Vec3 col = l.color * c.inten;
                out.set(kRegPointLight + 4 * npoint, l.pos.x, l.pos.y, l.pos.z, 1);
                out.set(kRegPointLight + 4 * npoint + 1, col.x, col.y, col.z, 1);
                out.reg(kRegPointAtten)[npoint] = l.attA;
                out.reg(kRegPointAtten + 1)[npoint] = l.attB;
                ++npoint;
            } else {
                addSh(l.color * (c.inten * c.att), norm(p - l.pos));
            }
            break;
        case LightIn::Spot:
            if (nspot < 5) {
                const gfx::Vec3 col = l.color * c.inten, dir = norm(l.dir);
                const int r = kRegSpotLight + 4 * nspot;
                out.set(r, dir.x, dir.y, dir.z, 0);
                out.set(r + 1, l.pos.x, l.pos.y, l.pos.z, 1);
                out.set(r + 2, col.x, col.y, col.z, 1);
                if (nspot < 4) {
                    out.reg(kRegSpotAtten)[nspot] = l.attA;
                    out.reg(kRegSpotAtten + 1)[nspot] = l.attB;
                    out.reg(kRegSpotAtten + 2)[nspot] = l.coneX;
                    out.reg(kRegSpotAtten + 3)[nspot] = l.coneY;
                } else {
                    out.set(r + 3, l.attA, l.attB, l.coneX, l.coneY);  // c211 [C microcode]
                }
                ++nspot;
            } else {
                const float along = std::clamp(gfx::dot(p - l.pos, l.dir), 0.0f, 1.0f);
                addSh(l.color * (c.inten * along), norm(p - l.pos));
            }
            break;
        case LightIn::Ambient: amb = amb + l.color * c.inten; break;
        case LightIn::Prt:
            // 823DAFA8: SH0..3 += coeff0..3 x the candidate's intensity (no
            // colour, no slot) [C].
            for (int k = 0; k < 4; ++k) sh[k] = sh[k] + l.prt[k] * c.inten;
            break;
        }
    }
    out.set(kRegAmbient, amb.x, amb.y, amb.z, 1);
    sh[0] = sh[0] + amb;
    for (int k = 0; k < ndir; ++k) {
        const LightIn& l = *slot[k]->l;
        const float a = slot[k]->scale;
        const gfx::Vec3 dir = l.dir;
        const gfx::Vec3 spec = l.color * (l.specular * a);
        if (k == 0) {
            const gfx::Vec3 col = l.color * (l.intensity * a);
            out.set(kRegDirLight0, dir.x, dir.y, dir.z, 0);
            out.set(kRegDirLight0 + 1, col.x, col.y, col.z, l.shadow);
            out.set(kRegDirLight0 + 2, spec.x, spec.y, spec.z, 0);
        } else {
            out.set(kRegDirLight0 + 1 + 2 * k, dir.x, dir.y, dir.z, 0);
            out.set(kRegDirLight0 + 2 + 2 * k, spec.x, spec.y, spec.z, 0);
        }
    }
    for (int k = 0; k < 4; ++k) out.set(kRegShLight0 + k, sh[k].x, sh[k].y, sh[k].z, 0);
}

gfx::Vec3 spriteLight(const ObjectConstants& ocIn, const gfx::Vec3& p) {
    (void)p;
    ObjectConstants& oc = const_cast<ObjectConstants&>(ocIn);
    auto v3 = [&](int r) { const float* f = oc.reg(r); return gfx::Vec3{f[0], f[1], f[2]}; };
    // The SH's constant term (the ambient and 4/17 of every light in it) and
    // the key light as the SH would hold it.
    return v3(kRegShLight0) + v3(kRegDirLight0 + 1) * (4.0f / 17.0f);
}

const gfx::VertexLayout& vertexLayout() {
    static const gfx::VertexLayout l = [] {
        gfx::VertexLayout r;
        r.stride = sizeof(Vertex);
        const auto F = gfx::AttribType::Float;
        r.attribs[0] = {0, 3, F, uint16_t(offsetof(Vertex, pos))};
        r.attribs[1] = {1, 3, F, uint16_t(offsetof(Vertex, nrm))};
        r.attribs[2] = {2, 4, gfx::AttribType::UNorm8, uint16_t(offsetof(Vertex, col))};
        r.attribs[3] = {3, 2, F, uint16_t(offsetof(Vertex, uv))};
        r.attribs[4] = {5, 3, F, uint16_t(offsetof(Vertex, tan))};
        r.attribs[5] = {6, 2, F, uint16_t(offsetof(Vertex, uv) + 8)};
        r.attribs[6] = {7, 3, F, uint16_t(offsetof(Vertex, bin))};
        r.attribs[7] = {8, 2, F, uint16_t(offsetof(Vertex, uv) + 16)};
        r.attribs[8] = {9, 2, F, uint16_t(offsetof(Vertex, uv) + 24)};
        r.attribs[9] = {12, 3, F, uint16_t(offsetof(Vertex, pos2))};
        r.attribs[10] = {13, 2, F, uint16_t(offsetof(Vertex, uv) + 32)};
        r.attribs[11] = {14, 2, F, uint16_t(offsetof(Vertex, uv) + 40)};
        r.attribs[12] = {15, 2, F, uint16_t(offsetof(Vertex, uv) + 48)};
        return r;
    }();
    return l;
}

void materialState(uint32_t blendMode, uint32_t flags, gfx::PipelineState& out) {
    using F = gfx::BlendFactor;
    using O = gfx::BlendOp;
    // The Xenos blend control word per mode (colour: src bits 0-4, op 5-7, dst 8-12).
    static const uint32_t kControl[13] = {0x10001,   0x1060106, 0x1860186, 0x7060706, 0x6000600, 0x1080108, 0x10008,
                                          0x1010101, 0x10706,   0x10701,   0x10108,   0x10189,   0x1810181};
    const uint32_t w = blendMode < 13 ? kControl[blendMode] : kControl[0];
    auto factor = [](uint32_t f) {
        switch (f) {
        case 0: return F::Zero;
        case 1: return F::One;
        case 4: return F::SrcColor;
        case 5: return F::InvSrcColor;
        case 6: return F::SrcAlpha;
        case 7: return F::InvSrcAlpha;
        case 8: return F::DstColor;
        case 9: return F::InvDstColor;
        case 10: return F::DstAlpha;
        case 11: return F::InvDstAlpha;
        default: return F::One;
        }
    };
    const uint32_t src = w & 31, op = (w >> 5) & 7, dst = (w >> 8) & 31;
    out.blend = !(src == 1 && dst == 0 && op == 0);
    out.src = factor(src), out.dst = factor(dst);
    out.op = op == 1 ? O::Subtract : op == 4 ? O::ReverseSubtract : O::Add;
    out.depthWrite = (flags & 0x20) == 0;
    out.depthTest = true;
    out.depthFunc = (flags & 0x10) ? gfx::CompareFunc::Always : gfx::CompareFunc::LessEqual;
}

}  // namespace gh3shader
