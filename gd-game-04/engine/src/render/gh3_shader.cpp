// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/gh3_shader.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <mutex>

#include "data/disc.h"
#include "data/qbkey.h"

namespace gh3shader {

std::shared_ptr<const matlib::Library> library(const Disc& disc) {
    static std::mutex mutex;
    static std::string cachedPath;
    static std::shared_ptr<const matlib::Library> cached;
    std::lock_guard<std::mutex> lock(mutex);
    if (cached && cachedPath == disc.path()) return cached;
    cached.reset();
    cachedPath = disc.path();
    // fxfiles\MaterialLibrary.bin.xen (Function_82384668) [C]; the disc
    // also holds a compressed copy under DATA/COMPRESSED/FXFILES.
    for (const char* path : {"DATA/FXFILES/MaterialLibrary.bin.xen", "DATA/COMPRESSED/FXFILES/MaterialLibrary.bin.xen"}) {
        if (!disc.exists(path)) continue;
        try {
            const Bytes b = disc.read(path);
            auto lib = std::make_shared<matlib::Library>();
            lib->parse(b.data(), b.size());
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

void Programs::shutdown() {
    if (!dev_) return;
    for (auto& [k, p] : programs_)
        if (p && p->id) dev_->destroyProgram(p->id);
    programs_.clear();
    for (auto& [k, p] : named_)
        if (p && p->id) dev_->destroyProgram(p->id);
    named_.clear();
    if (white_) dev_->destroyTexture(white_);
    if (whiteCube_) dev_->destroyTexture(whiteCube_);
    white_ = {}, whiteCube_ = {};
    dev_ = nullptr;
    lib_.reset();
}

std::unique_ptr<Program> Programs::make(int vs, int ps, const std::string& name) {
    std::unique_ptr<Program> out;
    const xenos::Program x = xenos::translate(lib_->shader(vs), lib_->shader(ps));
    if (!x.ok) {
        SDL_Log("gh3 shader %s: not translated: %s", name.c_str(), x.error.c_str());
        return out;
    }
    if (SDL_getenv("GD04_XENOS_DUMP")) SDL_Log("gh3 shader %s\n--- vertex\n%s\n--- pixel\n%s", name.c_str(), x.vertex.c_str(), x.fragment.c_str());
    std::string err;
    const gfx::ProgramId pid = dev_->createProgram(programDesc(x, name, dev_->info().clipDepthZeroToOne), err);
    if (!pid) {
        SDL_Log("gh3 shader %s: %s", name.c_str(), err.c_str());
        return out;
    }
    out = std::make_unique<Program>();
    out->id = pid;
    out->samplers = x.samplers, out->cubes = x.cubes, out->attribs = x.attribs;
    out->name = name;
    return out;
}

const Program* Programs::get(uint32_t key, matlib::Technique t) {
    if (!dev_ || !lib_) return nullptr;
    const uint64_t id = uint64_t(key) << 8 | t;
    auto it = programs_.find(id);
    if (it != programs_.end()) return it->second.get();
    std::unique_ptr<Program> out;
    const matlib::Template* tp = lib_->find(key);
    if (tp && tp->vs[t] >= 0 && tp->ps[t] >= 0) out = make(tp->vs[t], tp->ps[t], tp->name);
    const Program* r = out.get();
    programs_[id] = std::move(out);
    return r;
}

const Program* Programs::get(uint32_t key, const std::string& technique) {
    if (!dev_ || !lib_) return nullptr;
    const std::string id = std::to_string(key) + ":" + technique;
    auto it = named_.find(id);
    if (it != named_.end()) return it->second.get();
    std::unique_ptr<Program> out;
    const matlib::Template* tp = lib_->find(key);
    const matlib::TechniqueRef* tr = tp ? tp->technique(technique) : nullptr;
    if (tr && tr->vs[0] >= 0 && tr->ps[0] >= 0) out = make(tr->vs[0], tr->ps[0], tp->name + "." + technique);
    const Program* r = out.get();
    named_[id] = std::move(out);
    return r;
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
              : l.type == venue::LightType::Point ? LightIn::Point : LightIn::Spot;
    li.pos = {l.pos[0], l.pos[1], l.pos[2]};
    li.dir = {l.dir[0], l.dir[1], l.dir[2]};
    li.color = {s.color[0], s.color[1], s.color[2]};
    li.intensity = s.intensity, li.specular = s.specular, li.shadow = s.shadow;
    if (li.type == LightIn::Point || li.type == LightIn::Spot) {
        // Linear in the squared distance between attenstart and attenend [C 822BE020].
        li.attA = l.attenEnd * l.attenEnd;
        li.attB = 1.0f / (l.attenEnd * l.attenEnd - l.attenStart * l.attenStart + 0.0001f);
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
    for (int r = kRegAmbient; r < kRegAmbient + 16; ++r) out.set(r, 0, 0, 0, 0);
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
    int ndir = 0;
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
            if (ndir > 0) addSh(l.color * (c.inten * c.att), norm(p - l.pos));
            if (ndir < 4) slot[ndir++] = &c;
            break;
        case LightIn::Spot: {
            const float along = std::clamp(gfx::dot(p - l.pos, l.dir), 0.0f, 1.0f);
            addSh(l.color * (c.inten * along), norm(p - l.pos));
            break;
        }
        case LightIn::Ambient: amb = amb + l.color * c.inten; break;
        }
    }
    out.set(kRegAmbient, amb.x, amb.y, amb.z, 1);
    for (int k = 0; k < ndir; ++k) {
        const LightIn& l = *slot[k]->l;
        const float a = slot[k]->scale;
        // Slots 0 and 1: a point light's direction from it to the object;
        // slots 2 and 3 always the light's own direction (as coded) [C 823DB168].
        const gfx::Vec3 dir = (k < 2 && l.type != LightIn::Directional) ? norm(p - l.pos) : l.dir;
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

}  // namespace gh3shader
