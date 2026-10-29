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
    if (white_) dev_->destroyTexture(white_);
    if (whiteCube_) dev_->destroyTexture(whiteCube_);
    white_ = {}, whiteCube_ = {};
    dev_ = nullptr;
    lib_.reset();
}

const Program* Programs::get(uint32_t key, matlib::Technique t) {
    if (!dev_ || !lib_) return nullptr;
    const uint64_t id = uint64_t(key) << 8 | t;
    auto it = programs_.find(id);
    if (it != programs_.end()) return it->second.get();
    std::unique_ptr<Program> out;
    const matlib::Template* tp = lib_->find(key);
    if (tp && tp->vs[t] >= 0 && tp->ps[t] >= 0) {
        const xenos::Program x = xenos::translate(lib_->shader(tp->vs[t]), lib_->shader(tp->ps[t]));
        if (!x.ok) {
            SDL_Log("gh3 shader %s: not translated: %s", tp->name.c_str(), x.error.c_str());
        } else {
            if (SDL_getenv("GD04_XENOS_DUMP"))
                SDL_Log("gh3 shader %s\n--- vertex\n%s\n--- pixel\n%s", tp->name.c_str(), x.vertex.c_str(), x.fragment.c_str());
            std::string err;
            const gfx::ProgramId pid = dev_->createProgram(programDesc(x, tp->name, dev_->info().clipDepthZeroToOne), err);
            if (!pid) {
                SDL_Log("gh3 shader %s: %s", tp->name.c_str(), err.c_str());
            } else {
                out = std::make_unique<Program>();
                out->id = pid;
                out->samplers = x.samplers, out->cubes = x.cubes, out->attribs = x.attribs;
                out->name = tp->name;
            }
        }
    }
    const Program* r = out.get();
    programs_[id] = std::move(out);
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

void lightObject(const LightIn* lights, size_t count, const gfx::Vec3& center, ObjectConstants& out) {
    for (int r = kRegAmbient; r < kRegAmbient + 16; ++r) out.set(r, 0, 0, 0, 0);
    gfx::Vec3 amb{}, sh0{}, shx{}, shy{}, shz{};
    // How much of a light reaches the object, and the direction to it.
    auto reach = [&](const LightIn& l, gfx::Vec3& toLight) {
        if (l.type == LightIn::Directional) {
            toLight = l.dir * -1.0f;
            return 1.0f;
        }
        gfx::Vec3 d = l.pos - center;
        const float dist = std::sqrt(gfx::dot(d, d));
        toLight = dist > 1e-4f ? d * (1.0f / dist) : gfx::Vec3{0, 1, 0};
        float att = 1;
        if (l.attenEnd > l.attenStart) att = std::clamp((l.attenEnd - dist) / (l.attenEnd - l.attenStart), 0.0f, 1.0f);
        else if (dist > l.attenEnd) att = 0;
        if (l.type == LightIn::Spot) {
            const float c = -gfx::dot(toLight, l.dir);
            const float lo = std::min(l.cosField, l.cosHotspot - 1e-4f);
            att *= std::clamp((c - lo) / (l.cosHotspot - lo), 0.0f, 1.0f);
        }
        return att;
    };
    // The key light and the three specular-only lights: the strongest
    // directional (or spot) lights reaching the object, in order [I].
    struct Ranked {
        int index;
        float power, reach;
        gfx::Vec3 toLight;
    };
    Ranked ranked[4];
    int nRanked = 0;
    for (size_t i = 0; i < count; ++i) {
        const LightIn& l = lights[i];
        if (l.type == LightIn::Ambient || l.type == LightIn::Point) continue;
        gfx::Vec3 t;
        const float a = reach(l, t);
        const float p = a * (l.color.x + l.color.y + l.color.z);
        if (p <= 0) continue;
        int at = nRanked < 4 ? nRanked++ : 4;
        while (at > 0 && ranked[at - 1].power < p) {
            if (at < 4) ranked[at] = ranked[at - 1];
            --at;
        }
        if (at < 4) ranked[at] = {int(i), p, a, t};
    }
    for (size_t i = 0; i < count; ++i) {
        const LightIn& l = lights[i];
        if (l.type == LightIn::Ambient) {
            amb = amb + l.color;
            continue;
        }
        int rank = -1;
        for (int k = 0; k < nRanked; ++k)
            if (ranked[k].index == int(i)) rank = k;
        gfx::Vec3 t;
        const float a = rank >= 0 ? ranked[rank].reach : reach(l, t);
        if (rank >= 0) t = ranked[rank].toLight;
        if (a <= 0) continue;
        const gfx::Vec3 c = l.color * a;
        const gfx::Vec3 sc = c * l.specular;
        if (rank == 0) {
            // g_dirLight0Dir is where the light travels (the shaders use N.-dir).
            out.set(kRegDirLight0, -t.x, -t.y, -t.z, 0);
            out.set(kRegDirLight0 + 1, c.x, c.y, c.z, 1.0f);
            out.set(kRegDirLight0 + 2, sc.x, sc.y, sc.z, 0);
            continue;
        }
        if (rank > 0) {
            out.set(kRegDirLight0 + 1 + 2 * rank, -t.x, -t.y, -t.z, 0);
            out.set(kRegDirLight0 + 2 + 2 * rank, sc.x, sc.y, sc.z, 0);
        }
        // L1 projection of a clamped cosine lobe: 1/4 + n.l / 2.
        sh0 = sh0 + c * 0.25f;
        shx = shx + c * (0.5f * t.x);
        shy = shy + c * (0.5f * t.y);
        shz = shz + c * (0.5f * t.z);
    }
    out.set(kRegAmbient, amb.x, amb.y, amb.z, 1);
    out.set(kRegShLight0, sh0.x, sh0.y, sh0.z, 0);
    out.set(kRegShLight0 + 1, shx.x, shx.y, shx.z, 0);
    out.set(kRegShLight0 + 2, shy.x, shy.y, shy.z, 0);
    out.set(kRegShLight0 + 3, shz.x, shz.y, shz.z, 0);
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
