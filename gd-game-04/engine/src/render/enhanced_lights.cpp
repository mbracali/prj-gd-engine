// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/enhanced_lights.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>


namespace gfx {
namespace {

struct LightsBlock {
    float viewProj[16];
    float scale[4];  // x: the colour's scale (1 / 4 into GH2's HDR buffer)
};

const char* kVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec4 aColor;
layout(std140) uniform GdLights {
    mat4 uViewProj;
    vec4 uScale;
};
out vec4 vColor;
void main() {
    vColor = vec4(aColor.rgb * uScale.x, aColor.a);
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* kFragment = R"(
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

}  // namespace

ProgramDesc EnhancedLights::programDesc() {
    ProgramDesc d;
    d.name = "enh_lights";
    d.vertex = kVertex;
    d.fragment = kFragment;
    d.uniformBlocks = {"GdLights"};
    return d;
}

EnhancedLights::EnhancedLights(RenderDevice& device) : device_(device) {}

EnhancedLights::~EnhancedLights() {
    for (int i = 0; i < 2; ++i) {
        if (vbo_[i]) device_.destroyBuffer(vbo_[i]);
        if (ibo_[i]) device_.destroyBuffer(ibo_[i]);
    }
    if (program_) device_.destroyProgram(program_);
}

void EnhancedLights::flush(std::vector<Vertex>& v, std::vector<uint16_t>& idx, bool additive, const LightsView& camera, bool hdr) {
    if (idx.empty()) return;
    const int i = additive ? 1 : 0;
    const size_t vbytes = v.size() * sizeof(Vertex), ibytes = idx.size() * sizeof(uint16_t);
    if (vbytes > vboSize_[i]) {
        if (vbo_[i]) device_.destroyBuffer(vbo_[i]);
        vboSize_[i] = std::max<size_t>(vbytes * 2, 32 * 1024);
        vbo_[i] = device_.createBuffer(BufferKind::Vertex, vboSize_[i], nullptr, true);
    }
    if (ibytes > iboSize_[i]) {
        if (ibo_[i]) device_.destroyBuffer(ibo_[i]);
        iboSize_[i] = std::max<size_t>(ibytes * 2, 8 * 1024);
        ibo_[i] = device_.createBuffer(BufferKind::Index, iboSize_[i], nullptr, true);
    }
    if (!vbo_[i] || !ibo_[i]) return;
    device_.updateBuffer(vbo_[i], 0, v.data(), vbytes);
    device_.updateBuffer(ibo_[i], 0, idx.data(), ibytes);
    static const VertexLayout layout = [] {
        VertexLayout l;
        l.stride = sizeof(Vertex);
        l.attribs[0] = {kAttribPos, 3, AttribType::Float, uint16_t(offsetof(Vertex, x))};
        l.attribs[1] = {kAttribColor, 4, AttribType::UNorm8, uint16_t(offsetof(Vertex, r))};
        return l;
    }();
    LightsBlock b{};
    const Mat4 vp = camera.viewProj;
    std::memcpy(b.viewProj, vp.m, sizeof b.viewProj);
    b.scale[0] = hdr ? 0.25f : 1.0f;
    DrawCall c;
    c.program = program_;
    c.state.blend = true;
    c.state.src = BlendFactor::SrcAlpha;
    c.state.dst = additive ? BlendFactor::One : BlendFactor::InvSrcAlpha;
    c.state.depthTest = true, c.state.depthWrite = false;
    c.layout = &layout;
    c.vertices = vbo_[i];
    c.indices = ibo_[i];
    c.count = int(idx.size());
    c.uniforms[0] = UniformData{&b, sizeof b, 0};
    device_.draw(c);
}

void EnhancedLights::drawPools(const std::vector<Pool>& pools, const LightsView& camera) {
    if (pools.empty() || failed_) return;
    if (!program_) {
        std::string error;
        program_ = device_.createProgram(programDesc(), error);
        if (!program_) {
            SDL_Log("enhanced lights: %s (the projectors' pools are off)", error.c_str());
            failed_ = true;
            return;
        }
    }
    std::vector<Vertex> va;
    std::vector<uint16_t> idx;
    for (const Pool& p : pools) {
        auto put = [&](Vec3 q, float a) {
            va.push_back({q.x, q.y, q.z, uint8_t(std::clamp(p.colour.x, 0.0f, 1.0f) * 255), uint8_t(std::clamp(p.colour.y, 0.0f, 1.0f) * 255),
                          uint8_t(std::clamp(p.colour.z, 0.0f, 1.0f) * 255), uint8_t(std::clamp(a, 0.0f, 1.0f) * 255)});
            return uint16_t(va.size() - 1);
        };
        constexpr int kSeg = 28;
        const uint16_t c = put(p.centre, p.alpha);
        for (int i = 0; i < kSeg; ++i) {
            const float t = float(i) / kSeg * 6.2831853f;
            put(Vec3{p.centre.x + std::cos(t) * p.radius, p.centre.y + std::sin(t) * p.radius, p.centre.z}, 0);
        }
        for (int i = 0; i < kSeg; ++i) idx.insert(idx.end(), {c, uint16_t(c + 1 + i), uint16_t(c + 1 + (i + 1) % kSeg)});
        if (va.size() > 60000) break;
    }
    flush(va, idx, true, camera, false);
}

void EnhancedLights::drawBolts(const std::vector<Bolt>& bolts, const LightsView& camera, float timeSec, float coreWidth, float glowWidth) {
    if (bolts.empty() || failed_) return;
    if (!program_) {
        std::string error;
        program_ = device_.createProgram(programDesc(), error);
        if (!program_) {
            failed_ = true;
            return;
        }
    }
    std::vector<Vertex> va;
    std::vector<uint16_t> idx;
    const uint32_t strike = uint32_t(timeSec * 30.0f);
    auto hash = [](uint32_t x) {
        x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
        return float(x & 0xFFFFFF) / float(0xFFFFFF) * 2.0f - 1.0f;
    };
    auto ribbon = [&](const std::vector<Vec3>& pts, float w, Vec3 col, float a) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec3 d = pts[i + 1] - pts[i], mid = (pts[i] + pts[i + 1]) * 0.5f;
            const Vec3 side = normalize(cross(d, camera.pos - mid)) * w;
            const uint16_t b = uint16_t(va.size());
            for (const Vec3& q : {pts[i] - side, pts[i], pts[i] + side, pts[i + 1] - side, pts[i + 1], pts[i + 1] + side}) {
                va.push_back({q.x, q.y, q.z, uint8_t(col.x * 255), uint8_t(col.y * 255), uint8_t(col.z * 255), 0});
            }
            va[b + 1].a = va[b + 4].a = uint8_t(std::clamp(a, 0.0f, 1.0f) * 255);
            for (uint16_t c = 0; c < 2; ++c)
                idx.insert(idx.end(), {uint16_t(b + c), uint16_t(b + c + 1), uint16_t(b + 3 + c), uint16_t(b + c + 1), uint16_t(b + 4 + c), uint16_t(b + 3 + c)});
        }
    };
    for (const Bolt& bo : bolts) {
        const Vec3 d = bo.b - bo.a;
        const Vec3 mid = (bo.a + bo.b) * 0.5f;
        const Vec3 s1 = normalize(cross(d, camera.pos - mid)), s2 = normalize(cross(d, s1));
        constexpr int kSeg = 12;
        std::vector<Vec3> pts;
        for (int i = 0; i <= kSeg; ++i) {
            const float u = float(i) / kSeg, env = std::sin(u * 3.14159265f);
            const uint32_t h = bo.seed * 7919u + strike * 104729u + uint32_t(i) * 31u;
            pts.push_back(bo.a + d * u + s1 * (hash(h) * bo.amp * env) + s2 * (hash(h ^ 0x5bd1e995u) * bo.amp * 0.5f * env));
        }
        ribbon(pts, glowWidth, {0.35f, 0.55f, 1.0f}, 0.45f);
        ribbon(pts, coreWidth, {0.85f, 0.92f, 1.0f}, 1.0f);
        if (va.size() > 60000) break;
    }
    flush(va, idx, true, camera, false);
}

void EnhancedLights::draw(const Settings& s, const std::vector<Vec3>& members, const LightsView& camera, bool hdr, float timeSec) {
    shadows_ = beams_ = motes_ = 0;
    if (!s.any() || members.empty() || failed_) return;
    if (!program_) {
        std::string error;
        program_ = device_.createProgram(programDesc(), error);
        if (!program_) {
            SDL_Log("enhanced lights: %s (the venue extras are off)", error.c_str());
            failed_ = true;
            return;
        }
    }
    std::vector<Vertex> v, va;
    std::vector<uint16_t> idx, idxa;
    const Vec3 colour{1, 0.93f, 0.8f};  // gd01's warm white for the band's lights
    auto push = [](std::vector<Vertex>& out, Vec3 p, Vec3 c, float a) {
        out.push_back({p.x, p.y, p.z, uint8_t(std::clamp(c.x, 0.0f, 1.0f) * 255), uint8_t(std::clamp(c.y, 0.0f, 1.0f) * 255),
                       uint8_t(std::clamp(c.z, 0.0f, 1.0f) * 255), uint8_t(std::clamp(a, 0.0f, 1.0f) * 255)});
        return uint16_t(out.size() - 1);
    };
    // A disc on the floor: centre alpha `a`, transparent rim, stretched by `stretch` along `dir`.
    auto disc = [&](std::vector<Vertex>& out, std::vector<uint16_t>& ix, Vec3 c, float r, Vec3 dir, float stretch, Vec3 col, float a) {
        const uint16_t centre = push(out, c, col, a);
        const Vec3 side{-dir.y, dir.x, 0};
        constexpr int kSeg = 24;
        for (int i = 0; i < kSeg; ++i) {
            const float t = float(i) / kSeg * 6.2831853f;
            const Vec3 p = c + dir * (std::cos(t) * r * stretch) + side * (std::sin(t) * r);
            push(out, p, col, 0);
        }
        for (int i = 0; i < kSeg; ++i) ix.insert(ix.end(), {centre, uint16_t(centre + 1 + i), uint16_t(centre + 1 + (i + 1) % kSeg)});
    };
    const Vec3 camPos = camera.pos, camRight = camera.right, camUp = camera.up;
    // More Lights none / 1.5x / 2x / 2.5x: the beam's strength; Dust none / 1.5x / 2x / 2.5x: the motes per beam (gd01's).
    static constexpr float kBeam[4] = {0, 0.16f, 0.32f, 0.42f}, kPool[4] = {0.16f, 0.16f, 0.26f, 0.32f};
    static constexpr int kMotes[4] = {0, 35, 50, 70};
    const int ml = std::clamp(s.moreLights, 0, 3), dl = std::clamp(s.lightDust, 0, 3);
    const float beamA = kBeam[ml];
    const int motes = kMotes[dl];
    // Add Fog to the Ground (gd04, the port's own): soft grey layers drifting
    // over the stage floor around the band, a little above it; low / full /
    // super: more and thicker layers, reaching higher [I].
    if (s.groundFog > 0) {
        Vec3 c{0, 0, 0};
        for (const Vec3& m : members) c = c + m;
        c = c * (1.0f / float(members.size()));
        const int fl = std::clamp(s.groundFog, 1, 3);
        const int layers = 2 + 2 * fl;
        const float top = 10.0f + 10.0f * float(fl), alpha = 0.05f + 0.035f * float(fl);
        for (int l = 0; l < layers; ++l) {
            const float u = (float(l) + 0.5f) / float(layers);
            for (int b = 0; b < 7; ++b) {
                const float ph = float(b) * 0.9f + float(l) * 2.1f;
                const Vec3 drift{std::cos(timeSec * 0.05f + ph) * 60.0f + std::cos(ph * 1.7f) * 140.0f,
                                 std::sin(timeSec * 0.04f + ph) * 40.0f + std::sin(ph * 2.3f) * 90.0f, top * u};
                disc(v, idx, Vec3{c.x, c.y, c.z + 0.5f} + drift, 150.0f + 40.0f * std::sin(ph), Vec3{1, 0, 0}, 1.6f, {0.72f, 0.74f, 0.8f},
                     alpha * (1.0f - u * 0.6f));
            }
        }
    }
    for (size_t li = 0; li < members.size(); ++li) {
        const Vec3 t = members[li];
        // gd01's light per member: straight above and a little toward the audience.
        const Vec3 light{t.x + 10, t.y - 70, t.z + 210};
        Vec3 away{t.x - light.x, t.y - light.y, 0};
        const float h = std::sqrt(dot(away, away));
        away = h > 1e-4f ? away * (1 / h) : Vec3{0, 1, 0};
        const Vec3 floor{t.x, t.y, t.z + 0.6f};
        if (s.shadowsAll) {
            // The shadow, pushed away from the light, and the light's pool around the member.
            disc(v, idx, floor + away * 12, 26, away, 1.5f, {0, 0, 0}, 0.55f);
            ++shadows_;
        }
        if (s.shadowsAll || ml > 0) disc(va, idxa, floor + away * 6, 52, away, 1.25f, colour, s.shadowsAll ? 0.32f : kPool[ml]);  // dust only: no pools
        if (ml == 0 && dl == 0) continue;
        // The beam: from 25 % of the way down (the lamp is out of shot) to the floor.
        const Vec3 A = light + (t - light) * 0.25f, B{t.x, t.y, t.z + 1};
        const Vec3 axis = B - A, mid = (A + B) * 0.5f;
        const Vec3 side = normalize(cross(axis, camPos - mid));
        const float rTop = 10, rBot = 46;
        const uint16_t base = uint16_t(va.size());
        for (int row = 0; row < 2 && ml > 0; ++row) {
            const Vec3 P = row == 0 ? A : B;
            const float r = row == 0 ? rTop : rBot, a = row == 0 ? beamA : beamA * 0.35f;
            push(va, P - side * r, colour, 0);
            push(va, P, colour, a);
            push(va, P + side * r, colour, 0);
        }
        if (ml > 0) {
            for (uint16_t c = 0; c < 2; ++c)
                idxa.insert(idxa.end(), {uint16_t(base + c), uint16_t(base + c + 1), uint16_t(base + 3 + c), uint16_t(base + c + 1),
                                         uint16_t(base + 4 + c), uint16_t(base + 3 + c)});
            ++beams_;
        }
        // Dust: each mote drifts on its own slow orbit inside the beam (gd01's).
        for (int k = 0; k < motes; ++k) {
            float h1 = std::fabs(std::fmod(std::sin(float(k * 37 + int(li) * 101) * 12.9898f) * 43758.5453f, 1.0f));
            float h2 = std::fabs(std::fmod(std::sin(float(k * 53 + int(li) * 71) * 78.233f) * 12345.678f, 1.0f));
            const float u = std::fmod(h1 + timeSec * (0.01f + 0.02f * h2), 1.0f);
            const float ang = h2 * 6.2831853f + timeSec * 0.3f * (h1 - 0.5f);
            const float r = (rTop + (rBot - rTop) * u) * 0.8f * std::sqrt(std::fabs(std::sin(h1 * 91.7f)));
            const Vec3 P = A + axis * u + side * (r * std::cos(ang)) + Vec3{0, 0, r * 0.5f * std::sin(ang)};
            const float twinkle = 0.5f + 0.5f * std::sin(timeSec * (1.5f + 3 * h2) + h1 * 20);
            const Vec3 toCam = P - camPos;
            const float dist = std::sqrt(dot(toCam, toCam));
            const float a = (dl >= 3 ? 0.6f : dl == 2 ? 0.5f : 0.4f) * twinkle * (1 - u * 0.6f) * std::clamp((dist - 40) / 80, 0.0f, 1.0f);
            if (a <= 0.01f || va.size() > 60000) continue;
            const float sz = 0.5f;
            const uint16_t b = push(va, P, colour, a);
            push(va, P + camRight * sz, colour, 0), push(va, P + camUp * sz, colour, 0);
            push(va, P - camRight * sz, colour, 0), push(va, P - camUp * sz, colour, 0);
            idxa.insert(idxa.end(), {b, uint16_t(b + 1), uint16_t(b + 2), b, uint16_t(b + 2), uint16_t(b + 3), b, uint16_t(b + 3), uint16_t(b + 4), b,
                                     uint16_t(b + 4), uint16_t(b + 1)});
            ++motes_;
        }
    }
    flush(v, idx, false, camera, hdr);
    flush(va, idxa, true, camera, hdr);
}

}  // namespace gfx
