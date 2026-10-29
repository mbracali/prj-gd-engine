// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/enhanced_lights.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

#include "render/standard_shader.h"

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

void EnhancedLights::flush(std::vector<Vertex>& v, std::vector<uint16_t>& idx, bool additive, const Camera& camera, bool hdr) {
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
    const Mat4 vp = camera.viewProj(device_.info().clipDepthZeroToOne);
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

void EnhancedLights::draw(const Settings& s, const std::vector<Vec3>& members, const Camera& camera, bool hdr, float timeSec) {
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
    const Vec3 camPos = camera.position();
    const Vec3 camRight{camera.world.at(0, 0), camera.world.at(1, 0), camera.world.at(2, 0)};
    const Vec3 camUp{camera.world.at(0, 2), camera.world.at(1, 2), camera.world.at(2, 2)};
    const float beamA = s.moreLights == 2 ? 0.32f : 0.16f;
    const int motes = s.lightDust && s.moreLights > 0 ? (s.moreLights == 2 ? 70 : 35) : 0;
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
        disc(va, idxa, floor + away * 6, 52, away, 1.25f, colour, s.shadowsAll ? 0.32f : s.moreLights == 2 ? 0.26f : 0.16f);
        if (s.moreLights == 0) continue;
        // The beam: from 25 % of the way down (the lamp is out of shot) to the floor.
        const Vec3 A = light + (t - light) * 0.25f, B{t.x, t.y, t.z + 1};
        const Vec3 axis = B - A, mid = (A + B) * 0.5f;
        const Vec3 side = normalize(cross(axis, camPos - mid));
        const float rTop = 10, rBot = 46;
        const uint16_t base = uint16_t(va.size());
        for (int row = 0; row < 2; ++row) {
            const Vec3 P = row == 0 ? A : B;
            const float r = row == 0 ? rTop : rBot, a = row == 0 ? beamA : beamA * 0.35f;
            push(va, P - side * r, colour, 0);
            push(va, P, colour, a);
            push(va, P + side * r, colour, 0);
        }
        for (uint16_t c = 0; c < 2; ++c)
            idxa.insert(idxa.end(), {uint16_t(base + c), uint16_t(base + c + 1), uint16_t(base + 3 + c), uint16_t(base + c + 1), uint16_t(base + 4 + c),
                                     uint16_t(base + 3 + c)});
        ++beams_;
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
            const float a = (s.moreLights == 2 ? 0.6f : 0.4f) * twinkle * (1 - u * 0.6f) * std::clamp((dist - 40) / 80, 0.0f, 1.0f);
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
