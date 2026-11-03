// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/spotlight_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "char/character.h"
#include "game/band.h"
#include "game/world.h"
#include "render/band_view.h"
#include "render/venue_view.h"
#include "scene/anim.h"
#include "scene/world_objects.h"

namespace gfx {
namespace {

constexpr int kMaxFlares = 128;
constexpr float kPi = 3.14159265f;

// The flares' visibility test: one texel per flare. The new value steps
// 1/steps from the previous frame's towards 1 when the scene's depth at
// the flare is behind it (most of five samples), else towards 0.
const char* kVisVertex = R"(
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";
const char* kVisFragment = R"(
layout(std140) uniform GdFlareVis {
    vec4 uFlares[128];  // xy: screen uv, z: window depth, w: tested
    vec4 uSteps[128];   // x: 1 / steps, y: in front of the camera
    vec4 uTexel;        // xy: 1 / the depth buffer's size
};
uniform sampler2D sPrev;
uniform sampler2D sDepth;
out vec4 fragColor;
void main() {
    int i = int(gl_FragCoord.x);
    float prev = texelFetch(sPrev, ivec2(i, 0), 0).r;
    vec4 f = uFlares[i];
    float visible = 1.0;
    if (f.w > 0.5) {
        float n = 0.0;
        vec2 o = uTexel.xy * 2.0;
        n += step(f.z, texture(sDepth, f.xy).r + 0.0005);
        n += step(f.z, texture(sDepth, f.xy + vec2(o.x, 0.0)).r + 0.0005);
        n += step(f.z, texture(sDepth, f.xy - vec2(o.x, 0.0)).r + 0.0005);
        n += step(f.z, texture(sDepth, f.xy + vec2(0.0, o.y)).r + 0.0005);
        n += step(f.z, texture(sDepth, f.xy - vec2(0.0, o.y)).r + 0.0005);
        visible = step(2.5, n);
    }
    visible *= uSteps[i].y;
    float v = clamp(prev + (visible > 0.5 ? uSteps[i].x : -uSteps[i].x), 0.0, 1.0);
    fragColor = vec4(v, v, v, 1.0);
}
)";

// A flare: a screen-space square at its point's projection.
const char* kFlareVertex = R"(
layout(location = 0) in vec3 aCenter;
layout(location = 1) in vec2 aCorner;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
layout(location = 4) in float aSlot;
layout(std140) uniform GdFlare {
    mat4 uViewProj;
    vec4 uParams;  // x: output scale (HDR 0.25), y: use the visibility texture
};
uniform sampler2D sTex;
uniform sampler2D sVis;
out vec2 vUv;
out vec4 vColor;
void main() {
    vec4 c = uViewProj * vec4(aCenter, 1.0);
    c.xy += aCorner * c.w;
    c.z = 0.0;
    gl_Position = c;
    float vis = uParams.y > 0.5 ? texelFetch(sVis, ivec2(int(aSlot + 0.5), 0), 0).r : 1.0;
    vColor = aColor * vis;
    vUv = aUv;
}
)";
const char* kFlareFragment = R"(
layout(std140) uniform GdFlare {
    mat4 uViewProj;
    vec4 uParams;
};
uniform sampler2D sTex;
uniform sampler2D sVis;
in vec2 vUv;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec4 c = texture(sTex, vUv) * vColor;
    c.rgb *= uParams.x;
    fragColor = c;
}
)";

struct VisBlock {
    float flares[kMaxFlares][4];
    float steps[kMaxFlares][4];
    float texel[4];
};
struct FlareBlock {
    float viewProj[16];
    float params[4];
};

// The beams per pixel (GD02-151): GH2's depthvolume.fx "render cone
// depths" branch, from the disc's own HLSL (system/run/shaders): the screen
// ramp illum = sat(1 - |(ramp.xy - ndc) * (aspect, 1)| / reach)^2, the depth
// min(scene's view depth, the sheet's) as sat((w - nearest) * scale), out
// colour x depth x illum, into a beam buffer with no depth test (add, ONE /
// ONE), as 0x82307078 [C]. Cross-section, cut-outs and the density map are
// identities on the disc [C data].
const char* kBeamVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec4 aRamp;   // light ndc xy, aspect, 1 / reach
layout(location = 3) in vec2 aRange;  // nearest view depth, scale
layout(std140) uniform GdBeam {
    mat4 uViewProj;
    vec4 uDepth;  // near, far, depth 0..1 (1) or -1..1 (0), output scale
};
uniform sampler2D sDepth;
out vec4 vClip;
out vec3 vColor;
out vec4 vRamp;
out vec2 vRange;
void main() {
    vec4 c = uViewProj * vec4(aPos, 1.0);
    gl_Position = c;
    vClip = c;
    vColor = aColor;
    vRamp = aRamp;
    vRange = aRange;
}
)";
const char* kBeamFragment = R"(
layout(std140) uniform GdBeam {
    mat4 uViewProj;
    vec4 uDepth;
};
uniform sampler2D sDepth;
in vec4 vClip;
in vec3 vColor;
in vec4 vRamp;
in vec2 vRange;
out vec4 fragColor;
void main() {
    vec2 ndc = vClip.xy / vClip.w;
    float d = length(vec2((ndc.x - vRamp.x) * vRamp.z, ndc.y - vRamp.y)) * vRamp.w;
    float illum = clamp(1.0 - d, 0.0, 1.0);
    illum *= illum;
    float z = texture(sDepth, ndc * 0.5 + 0.5).r;
    float n = uDepth.x, f = uDepth.y;
    float sceneW = uDepth.z > 0.5 ? n * f / (f - z * (f - n)) : 2.0 * n * f / ((f + n) - (z * 2.0 - 1.0) * (f - n));
    float w = min(sceneW, vClip.w);
    float depth = clamp((w - vRange.x) * vRange.y, 0.0, 1.0);
    fragColor = vec4(vColor * (depth * illum * uDepth.w), 0.0);
}
)";
// FogPixel (postprocess.fx): the beam buffer added onto the world's pixels.
const char* kBeamCompVertex = R"(
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); vUv = aPos * 0.5 + 0.5; }
)";
const char* kBeamCompFragment = R"(
layout(std140) uniform GdBeamComp {
    vec4 uGain;  // x: the buffer's gain (x the HDR output scale)
};
uniform sampler2D sBeams;
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = vec4(texture(sBeams, vUv).rgb * uGain.x, 0.0); }
)";

struct BeamBlock {
    float viewProj[16];
    float depth[4];
};
struct BeamCompBlock {
    float gain[4];
};

const VertexLayout& beamLayout() {
    static VertexLayout l = [] {
        VertexLayout v;
        v.stride = 48;
        v.attribs[0] = {0, 3, AttribType::Float, 0};
        v.attribs[1] = {1, 3, AttribType::Float, 12};
        v.attribs[2] = {2, 4, AttribType::Float, 24};
        v.attribs[3] = {3, 2, AttribType::Float, 40};
        return v;
    }();
    return l;
}

const VertexLayout& triangleLayout() {
    static VertexLayout l = [] {
        VertexLayout v;
        v.stride = 8;
        v.attribs[0] = {0, 2, AttribType::Float, 0};
        return v;
    }();
    return l;
}

const VertexLayout& flareLayout() {
    static VertexLayout l = [] {
        VertexLayout v;
        v.stride = 48;
        v.attribs[0] = {0, 3, AttribType::Float, 0};
        v.attribs[1] = {1, 2, AttribType::Float, 12};
        v.attribs[2] = {2, 2, AttribType::Float, 20};
        v.attribs[3] = {3, 4, AttribType::Float, 28};
        v.attribs[4] = {4, 1, AttribType::Float, 44};
        return v;
    }();
    return l;
}

ProgramDesc visDesc() {
    ProgramDesc d;
    d.name = "spotlight_flare_vis";
    d.vertex = kVisVertex;
    d.fragment = kVisFragment;
    d.uniformBlocks = {"GdFlareVis"};
    d.samplers = {"sPrev", "sDepth"};
    return d;
}

ProgramDesc beamDesc() {
    ProgramDesc d;
    d.name = "spotlight_beam";
    d.vertex = kBeamVertex;
    d.fragment = kBeamFragment;
    d.uniformBlocks = {"GdBeam"};
    d.samplers = {"sDepth"};
    return d;
}

ProgramDesc beamCompDesc() {
    ProgramDesc d;
    d.name = "spotlight_beam_composite";
    d.vertex = kBeamCompVertex;
    d.fragment = kBeamCompFragment;
    d.uniformBlocks = {"GdBeamComp"};
    d.samplers = {"sBeams"};
    return d;
}

ProgramDesc flareDesc() {
    ProgramDesc d;
    d.name = "spotlight_flare";
    d.vertex = kFlareVertex;
    d.fragment = kFlareFragment;
    d.uniformBlocks = {"GdFlare"};
    d.samplers = {"sTex", "sVis"};
    return d;
}

const scene::Environ* dirEnv(const scene::Directory* d) {
    if (!d) return nullptr;
    const auto* own = d->self ? d->self->as<scene::DirObj>() : nullptr;
    if (!own || !own->hasRnd || own->rnd.environment.empty()) return nullptr;
    return d->resolve<scene::Environ>(own->rnd.environment);
}

Vec3 v3(const std::array<float, 3>& a) { return {a[0], a[1], a[2]}; }

Vec4 mul(const Mat4& m, Vec3 p) {
    return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3), m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
            m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3), m.at(3, 0) * p.x + m.at(3, 1) * p.y + m.at(3, 2) * p.z + m.at(3, 3)};
}

// The material of a beam / floor spot / lens: GH2's Mat with the colour
// from the vertices (prelit, no environment), no depth writes, added.
Material fxMaterial(SceneCache& cache, const scene::Mat& mat, const ScenePose& pose) {
    Material m = cache.material(mat, &pose);
    m.prelit = true;
    m.useEnviron = false;
    m.cull = false;
    if (m.zMode == ZMode::Normal || m.zMode == ZMode::Decal) m.zMode = ZMode::Transparent;
    if (m.blend == MatBlend::Src || m.blend == MatBlend::Dest) m.blend = MatBlend::Add;
    return m;
}

// One vertex in GH2's 16-float layout (position, normal, colour, uv, tangent).
void vertex(std::vector<float>& v, Vec3 p, const float rgba[4], float u, float t) {
    v.insert(v.end(), {p.x, p.y, p.z, 0, 0, 1, rgba[0], rgba[1], rgba[2], rgba[3], u, t, 1, 0, 0, 1});
}

struct Batch {
    Material mat;
    std::vector<float> verts;
    std::vector<uint16_t> idx;
};

void quadIdx(Batch& b, uint16_t a, uint16_t c, uint16_t d, uint16_t e) { b.idx.insert(b.idx.end(), {a, c, d, a, d, e}); }

// A beam as GH2 draws it (0x82260B40 -> 0x822601D0 with depthvolume.fx) [C
// unless marked]. The sheet (0x82235F68, is_cone 0 on all 341): top_radius to
// bottom_radius wide over `length`, starting `offset` along the light,
// BillboardZ (constraint 5). The shader ignores the sheet's vertex alpha and
// the material: the colour is
//   spot colour x intensity x 8 x brighten x depth x illum
// depth = saturate((w - nearest) x 0.125 / thickness): w the view depth,
// nearest the closest of the volume's four corners (no nearer than the near
// plane, 0x8225FF98), thickness min(length x 0.33, bottom radius / sin(angle to
// the view)); illum = (1 - |d|)^2: d the screen distance from the light over
// the distance to the end corners (aspect corrected, 0x8225E3C0). The
// cross-section map defaults to white and the density map to black, so
// neither changes it. Evaluated per vertex on a strip of rows [I: GH2 per
// pixel]. GH2 adds the result to the scene in postprocess.fx FogPixel; the
// buffer's scale (0x8225F2E8: total x 1024 x density) is not understood, so
// the gain here keeps 1 [I]. Depth test on instead of the shader's min()
// with the scene's depth [I: the same where a beam is fully hidden].
bool beamStrip(const scene::Spotlight& s, Vec3 pos, Vec3 y, Vec3 dir, const Camera& camera, const float lit[3], Batch& out,
               std::vector<float>* px) {
    const float len = s.beam.length, top = s.beam.topRadius, bottom = s.beam.bottomRadius;
    const Vec3 cam = camera.position();
    const Vec3 fwd = normalize(Vec3{camera.world.m[4], camera.world.m[5], camera.world.m[6]});
    const Vec3 a = pos + y * s.beam.offset;  // the mesh, offset along the light
    Vec3 side = cross(a - cam, dir);
    if (dot(side, side) < 1e-6f) return false;
    side = normalize(side);
    // The volume's corners (0x8225E590): towards the camera and away, at the
    // light and at the end, the sheet's radii narrowed by its borders (0x8222E9E0).
    const float rTop = top * s.beam.expand * (1 - s.beam.topSideBorder * 0.7f);
    const float rEnd = bottom * s.beam.expand * (1 - s.beam.bottomSideBorder * 0.7f);
    Vec3 toCam = cam - pos;
    toCam = toCam - y * dot(toCam, y);
    toCam = dot(toCam, toCam) > 1e-8f ? normalize(toCam) : Vec3{camera.world.m[8], camera.world.m[9], camera.world.m[10]};
    const Vec3 corners[4] = {pos + toCam * rTop, pos - toCam * rTop, pos + y * len + toCam * rEnd, pos + y * len - toCam * rEnd};
    float nearest = 1e30f;
    for (const Vec3& c : corners) nearest = std::min(nearest, dot(c - cam, fwd));
    nearest = std::max(nearest, camera.nearPlane);
    const float cosA = std::fabs(dot(fwd, y)), sinA = std::sqrt(std::max(0.0f, 1 - cosA * cosA));
    float thick = len * 0.33f;
    if (rEnd < thick * sinA) thick = rEnd / sinA;
    const float scale = 0.125f / std::max(thick, 1e-3f);
    // The screen ramp (0x8225E3C0): NDC, x in the picture's aspect.
    const Mat4 vp = camera.viewProj(false);
    const auto ndc = [&](Vec3 p, float& nx, float& ny) {
        const float cw = vp.at(3, 0) * p.x + vp.at(3, 1) * p.y + vp.at(3, 2) * p.z + vp.at(3, 3);
        nx = (vp.at(0, 0) * p.x + vp.at(0, 1) * p.y + vp.at(0, 2) * p.z + vp.at(0, 3)) / cw;
        ny = (vp.at(1, 0) * p.x + vp.at(1, 1) * p.y + vp.at(1, 2) * p.z + vp.at(1, 3)) / cw;
        return cw > 0;
    };
    float lx = 0, ly = 0, ex = 0, ey = 0;
    float reach = 1e-3f;
    if (!ndc(pos, lx, ly)) return false;
    for (int i = 2; i < 4; ++i)
        if (ndc(corners[i], ex, ey)) reach = std::max(reach, std::hypot((ex - lx) * camera.aspect, ey - ly));
    const float k = 8.0f * s.beam.brighten;
    if (px) {  // per pixel (GD02-151): the sheet's two triangles, the beam's constants on every vertex
        const Vec3 q[4] = {a - side * top, a + side * top, a + dir * len + side * bottom, a + dir * len - side * bottom};
        for (int i : {0, 1, 2, 0, 2, 3})
            px->insert(px->end(), {q[i].x, q[i].y, q[i].z, lit[0] * k, lit[1] * k, lit[2] * k, lx, ly, camera.aspect, 1.0f / reach,
                                   nearest, scale});
        return true;
    }
    const int rows = 16;
    const uint16_t base = uint16_t(out.verts.size() / 16);
    if (base > 60000) return false;
    for (int r = 0; r <= rows; ++r) {
        const float v = float(r) / rows;
        const Vec3 c = a + dir * (len * v);
        const float radius = top + (bottom - top) * v;
        for (int u = -1; u <= 1; ++u) {
            const Vec3 p = c + side * (radius * float(u));
            float nx = 0, ny = 0;
            const bool front = ndc(p, nx, ny);
            const float d = front ? std::hypot((nx - lx) * camera.aspect, ny - ly) / reach : 1.0f;
            float illum = std::clamp(1 - d, 0.0f, 1.0f);
            illum *= illum;
            const float depth = std::clamp((dot(p - cam, fwd) - nearest) * scale, 0.0f, 1.0f);
            const float m = k * depth * illum;
            const float col[4] = {lit[0] * m, lit[1] * m, lit[2] * m, 1};
            vertex(out.verts, p, col, (float(u) + 1) * 0.5f, v);
        }
    }
    for (uint16_t r = 0; r < rows; ++r)
        for (uint16_t c = 0; c < 2; ++c) {
            const uint16_t i0 = uint16_t(base + r * 3 + c);
            quadIdx(out, i0, uint16_t(i0 + 1), uint16_t(i0 + 4), uint16_t(i0 + 3));
        }
    return true;
}

}  // namespace

SpotlightView::SpotlightView(World& world, SceneCache& cache) : world_(world), cache_(cache), device_(cache.device()), mesh_(cache.device()) {
    std::string error;
    visProgram_ = device_.createProgram(visDesc(), error);
    if (!visProgram_) SDL_Log("spotlights: %s does not compile: %s", visDesc().name.c_str(), error.c_str());
    error.clear();
    flareProgram_ = device_.createProgram(flareDesc(), error);
    if (!flareProgram_) SDL_Log("spotlights: %s does not compile: %s", flareDesc().name.c_str(), error.c_str());
    error.clear();
    beamProgram_ = device_.createProgram(beamDesc(), error);
    if (!beamProgram_) SDL_Log("spotlights: %s does not compile: %s", beamDesc().name.c_str(), error.c_str());
    error.clear();
    beamCompProgram_ = device_.createProgram(beamCompDesc(), error);
    if (!beamCompProgram_) SDL_Log("spotlights: %s does not compile: %s", beamCompDesc().name.c_str(), error.c_str());
    const float tri[6] = {-1, -1, 3, -1, -1, 3};
    triangle_ = device_.createBuffer(BufferKind::Vertex, sizeof tri, tri, false);
    for (TargetId& t : vis_) {
        error.clear();
        t = device_.createTarget(kMaxFlares, 1, false, error);
    }
}

SpotlightView::~SpotlightView() {
    for (TargetId t : vis_)
        if (t) device_.destroyTarget(t);
    if (triangle_) device_.destroyBuffer(triangle_);
    if (flareVb_) device_.destroyBuffer(flareVb_);
    if (visProgram_) device_.destroyProgram(visProgram_);
    if (flareProgram_) device_.destroyProgram(flareProgram_);
    if (beamProgram_) device_.destroyProgram(beamProgram_);
    if (beamCompProgram_) device_.destroyProgram(beamCompProgram_);
    if (beamTarget_) device_.destroyTarget(beamTarget_);
    if (beamVb_) device_.destroyBuffer(beamVb_);
}

std::vector<ProgramDesc> SpotlightView::allPrograms() { return {visDesc(), flareDesc(), beamDesc(), beamCompDesc()}; }

int SpotlightView::slotOf(const void* key) {
    auto it = slots_.find(key);
    if (it != slots_.end()) return it->second;
    const int s = int(slots_.size()) % kMaxFlares;
    slots_[key] = s;
    return s;
}

void SpotlightView::draw(MaterialRenderer& mr, const Camera& camera, int viewW, int viewH, bool hdr, TargetId worldTarget,
                         TextureId worldDepth) {
    beams_ = flares_ = spots_ = 0;
    const uint64_t nowNs = SDL_GetTicksNS();
    const float dt = lastDrawNs_ ? float(double(nowNs - lastDrawNs_) * 1e-9) : 0.0f;
    lastDrawNs_ = nowNs;
    flareList_.clear();
    // Development aid: GD02_SPOTS=<letters> draws only those parts (c cans,
    // f floor spots, b beams, l lenses, F flares); unset: all.
    const char* only = SDL_getenv("GD02_SPOTS");
    auto on = [&](char c) { return !only || std::strchr(only, c) != nullptr; };
    const ScenePose& pose = world_.pose();
    Venue& venue = world_.venue();
    const bool mp = venue.variant() != VenueVariant::Single;
    const Vec3 camPos = camera.position();
    const Vec3 camFwd = normalize(Vec3{camera.world.m[4], camera.world.m[5], camera.world.m[6]});
    std::map<const scene::Mat*, Batch> batches;
    Batch beamBatch;
    beamBatch.mat.prelit = true, beamBatch.mat.useEnviron = false, beamBatch.mat.cull = false;
    beamBatch.mat.blend = MatBlend::Add, beamBatch.mat.zMode = ZMode::Transparent;
    // Per pixel into the beam buffer when the world's depth is there (post-
    // processing on); else the old per-vertex sheet in the world (GD02-151).
    const bool perPixel = worldTarget && worldDepth && beamProgram_ && beamCompProgram_;
    beamPx_.clear();
    auto batch = [&](const scene::Mat* m) -> Batch& {
        auto [it, added] = batches.try_emplace(m);
        if (added) it->second.mat = fxMaterial(cache_, *m, pose);
        return it->second;
    };
    auto findMat = [&](const scene::Object& o, const std::string& name) -> const scene::Mat* {
        if (name.empty()) return nullptr;
        if (o.dir)
            if (auto* m = o.dir->resolve<scene::Mat>(name)) return m;
        return venue.find<scene::Mat>(name);
    };
    struct Can {
        const scene::Mesh* mesh;
        Mat4 world;
        const scene::Environ* env;
    };
    // additional_objects (Spotlight's draw children, 0x822348F8): drawn in the
    // queue under an Environ whose ambient is the spot's colour x intensity
    // (0x8222F738 under 0x8222EC40) [C]: the circles the spots throw.
    struct Extra {
        const scene::Mesh* mesh;
        Mat4 world;
        Vec3 color;
    };
    std::vector<Extra> extras;
    std::vector<Can> cans;
    struct Shadow {
        std::string target;
        Vec3 dir;
    };
    std::vector<Shadow> shadows;
    const auto& states = world_.spots();
    for (const scene::Spotlight* s : venue.all<scene::Spotlight>()) {
        if (!s->decoded || !pose.showing(s)) continue;
        // The preset's state over the object's own.
        float intensity = s->intensity;
        std::array<float, 4> color = s->color;
        std::string target = s->target, subpart;
        bool flareOn = s->flareEnabled;
        scene::Xfm x = pose.world(s);
        if (auto it = states.find(s); it != states.end()) {
            intensity = it->second.intensity, color = it->second.color;
            target = it->second.target, subpart = it->second.subpart;
            flareOn = flareOn && it->second.flare;
            // The poll (0x82233250): the preset's quaternion is the LOCAL rotation.
            if (it->second.rotation != std::array<float, 4>{}) {
                scene::Xfm lx = pose.local(s);
                scene::quatToRot(it->second.rotation, lx.rot);
                x = pose.world(s, lx);
            }
        }
        // 0x822328E8: queued when intensity x green or x blue reads as a
        // byte (the code tests green twice, not red) [C].
        if ((int(intensity * color[1] * 255.0f) & 0xff) == 0 && (int(intensity * color[2] * 255.0f) & 0xff) == 0) continue;
        const float lit[3] = {color[0] * intensity, color[1] * intensity, color[2] * intensity};
        ++spots_;
        const Vec3 pos = v3(x.pos);
        Vec3 right{x.rot[0], x.rot[1], x.rot[2]}, fwd{x.rot[3], x.rot[4], x.rot[5]}, up{x.rot[6], x.rot[7], x.rot[8]};
        // The light shines along its y row [C: the can, beam, lens and flare
        // offsets, 0x82231AF0, and the aim]. A target turns it there (0x82231610:
        // y = target - position, z from y x (1 0 0)); a target its entry
        // names that is not in the venue leaves the spotlight off (0x8223ADE8
        // clears the flag, 0x82232698 clears +0x1ec, 0x822328E8 skips it).
        scene::Xfm rootXfm;
        bool targeted = false;
        if (!target.empty()) {
            scene::Xfm tx;
            if (!venueObjectXfm(world_, target, mp, tx, subpart)) continue;
            venueObjectXfm(world_, target, mp, rootXfm);
            targeted = true;
            const Vec3 d = v3(tx.pos) - pos;
            if (dot(d, d) > 1e-4f) {
                fwd = normalize(d);
                // damping_constant != 1 (15 spotlights): the aim eases there,
                // dt x damping of the way each poll (0x82233250) [C; a
                // normalised lerp in place of 0x8226A980's slerp, I].
                if (s->dampingConstant != 1) {
                    auto [it, fresh] = damped_.try_emplace(s, fwd);
                    if (!fresh) {
                        const float k = std::clamp(dt * s->dampingConstant, 0.0f, 1.0f);
                        it->second = normalize(it->second + (fwd - it->second) * k);
                        fwd = it->second;
                    }
                }
                Vec3 n = cross(d, Vec3{1, 0, 0});
                if (dot(n, n) < 1e-6f) n = Vec3{0, 0, 1};
                right = normalize(cross(fwd, normalize(n)));
                up = normalize(cross(right, fwd));
                x.rot = {right.x, right.y, right.z, fwd.x, fwd.y, fwd.z, up.x, up.y, up.z};
            }
        }
        Vec3 dir = fwd;
        // target_shadow (0x822312C8, drawn first by 0x8222EE58): the target
        // character's shadow group flattened along the light (Character 0x82151B98).
        if (targeted && s->targetShadow) shadows.push_back({target, dir});
        // angle_offset (degrees) tilts the beam about the light's x and z [I].
        if (s->beam.angleOffset[0] != 0 || s->beam.angleOffset[1] != 0) {
            const float ax = s->beam.angleOffset[0] * kPi / 180, ay = s->beam.angleOffset[1] * kPi / 180;
            dir = normalize(dir * std::cos(ax) + up * std::sin(ax));
            dir = normalize(dir * std::cos(ay) + right * std::sin(ay));
        }
        // The light can.
        if (!s->lightCan.empty() && on('c'))
            if (auto* can = s->dir ? s->dir->resolve<scene::Mesh>(s->lightCan) : nullptr; can || (can = venue.find<scene::Mesh>(s->lightCan))) {
                scene::Xfm cx = x;
                cx.rot = {right.x, right.y, right.z, fwd.x, fwd.y, fwd.z, up.x, up.y, up.z};
                const Vec3 cp = pos + dir * s->lightCanOffset;
                cx.pos = {cp.x, cp.y, cp.z};
                cans.push_back({can, toMat4(cx), dirEnv(can->dir)});
            }
        if (on('f'))
            for (const std::string& n : s->additionalObjects)
                if (auto* m = s->dir ? s->dir->resolve<scene::Mesh>(n) : nullptr; m && pose.showing(m))
                    extras.push_back({m, toMat4(pose.world(m)), Vec3{lit[0], lit[1], lit[2]}});
        // The beam: depthvolume.fx on the sheet of 0x82235F68 (0x822601D0, 0x82260B40).
        if (s->beam.length > 0 && on('b') && beamStrip(*s, pos, fwd, dir, camera, lit, beamBatch, perPixel ? &beamPx_ : nullptr)) ++beams_;
        // The floor spot (0x82231AF0, its mesh 0x82235488: a unit quad, uv 0..1):
        // only with a target and spot_material, where the light's y axis
        // meets z = the target's z + spot_height, spot_scale wide across and
        // spot_scale x |d| / |d.z| long along the light's heading.
        if (const scene::Mat* sm = on('f') && targeted ? findMat(*s, s->spotMaterial) : nullptr; sm) {
            const Vec3 d = v3(rootXfm.pos) - pos;
            if (d.z != 0 && fwd.z != 0) {
                const float t = (rootXfm.pos[2] + s->spotHeight - pos.z) / fwd.z;
                const Vec3 c = pos + fwd * t;
                Vec3 along{fwd.x, fwd.y, 0};
                if (dot(along, along) < 1e-8f) along = Vec3{0, 1, 0};
                along = normalize(along);
                const Vec3 across = normalize(cross(along, Vec3{0, 0, 1}));
                const float hw = s->spotScale * 0.5f, hl = hw * std::sqrt(dot(d, d)) / std::fabs(d.z);
                Batch& bt = batch(sm);
                const uint16_t base = uint16_t(bt.verts.size() / 16);
                const float col[4] = {lit[0], lit[1], lit[2], 1};
                vertex(bt.verts, c + across * -hw + along * -hl, col, 0, 0);
                vertex(bt.verts, c + across * hw + along * -hl, col, 1, 0);
                vertex(bt.verts, c + across * -hw + along * hl, col, 0, 1);
                vertex(bt.verts, c + across * hw + along * hl, col, 1, 1);
                quadIdx(bt, base, uint16_t(base + 1), uint16_t(base + 3), uint16_t(base + 2));
            }
        }
        // The lens (lens_material, 0x82231AF0): the unit quad lens_size wide
        // at lens_offset along the light, square to it.
        if (const scene::Mat* lm = on('l') ? findMat(*s, s->lensMaterial) : nullptr; lm && s->lensSize > 0) {
            const Vec3 c = pos + fwd * s->lensOffset;
            Batch& bt = batch(lm);
            const uint16_t base = uint16_t(bt.verts.size() / 16);
            const float r = s->lensSize * 0.5f;
            const float col[4] = {lit[0], lit[1], lit[2], 1};
            vertex(bt.verts, c + (right * -r) + (up * -r), col, 0, 0);
            vertex(bt.verts, c + (right * r) + (up * -r), col, 1, 0);
            vertex(bt.verts, c + (right * r) + (up * r), col, 1, 1);
            vertex(bt.verts, c + (right * -r) + (up * r), col, 0, 1);
            quadIdx(bt, base, uint16_t(base + 1), uint16_t(base + 2), uint16_t(base + 3));
        }
        // The flare.
        if (flareOn && on('F'))
            if (const scene::Mat* fm = findMat(*s, s->flareMaterial)) {
                Flare f;
                f.pos = pos + dir * s->flareOffset;
                const Vec3 toCam = normalize(camPos - f.pos);
                const float face = std::clamp(dot(dir, toCam), 0.0f, 1.0f);
                f.size = s->flareSize[0] + (s->flareSize[1] - s->flareSize[0]) * face;
                const float depth = dot(f.pos - camPos, camFwd);
                float fade = 1;
                if (s->flareRange[0] != s->flareRange[1])
                    fade = std::clamp((depth - s->flareRange[1]) / (s->flareRange[0] - s->flareRange[1]), 0.0f, 1.0f);
                const float in = std::min(1.0f, intensity);
                for (int q = 0; q < 3; ++q) f.rgba[q] = std::min(1.0f, color[size_t(q)]) * in * fade;
                f.rgba[3] = fade;
                f.tex = cache_.material(*fm, &pose).diffuseTex;
                f.test = !s->flareVisibilityTest;  // stored inverted (0x822328E8 passes byte == 0)
                f.steps = int(std::max<uint32_t>(1, s->flareSteps));
                f.slot = slotOf(s);
                if (fade > 0 && depth > 0) flareList_.push_back(f);
            }
    }
    // RndFlare objects (always tested).
    for (const scene::Flare* fl : venue.all<scene::Flare>()) {
        if (!fl->decoded || !pose.showing(fl) || !on('F')) continue;
        const scene::Mat* fm = findMat(*fl, fl->mat);
        if (!fm) continue;
        const scene::Xfm x = pose.world(fl);
        Flare f;
        f.pos = v3(x.pos);
        const float face = std::clamp(-dot(Vec3{x.rot[3], x.rot[4], x.rot[5]}, camFwd), 0.0f, 1.0f);
        f.size = fl->size[0] + (fl->size[1] - fl->size[0]) * face;
        const float depth = dot(f.pos - camPos, camFwd);
        float fade = 1;
        if (fl->range[0] != fl->range[1]) fade = std::clamp((depth - fl->range[1]) / (fl->range[0] - fl->range[1]), 0.0f, 1.0f);
        f.rgba[0] = f.rgba[1] = f.rgba[2] = fade;
        f.rgba[3] = fade;
        f.tex = cache_.material(*fm, &pose).diffuseTex;
        f.test = true;
        f.steps = int(std::max<uint32_t>(1, fl->steps));
        f.slot = slotOf(fl);
        if (fade > 0 && depth > 0) flareList_.push_back(f);
    }

    if (SDL_getenv("GD02_SPOTS_LOG")) {
        static int frames = 0;
        if (frames++ % 60 == 0) {
            SDL_Log("spots: %d lit, %d beams, %d flares", spots_, beams_, int(flareList_.size()));
            for (const Flare& f : flareList_)
                SDL_Log("  flare at %.0f %.0f %.0f size %.2f rgba %.2f %.2f %.2f %.2f test %d steps %d", f.pos.x, f.pos.y, f.pos.z, f.size,
                        f.rgba[0], f.rgba[1], f.rgba[2], f.rgba[3], f.test, f.steps);
        }
    }
    DrawContext ctx;
    ctx.hdr = hdr;
    // Cans, lit by their directory's environment.
    const scene::Environ* lastEnv = nullptr;
    bool envSet = false;
    for (const Can& c : cans) {
        const GpuMesh* g = cache_.mesh(c.mesh);
        if (!g || g->skinned || !c.mesh->dir) continue;
        if (!envSet || c.env != lastEnv) {
            LightEnv le;
            if (c.env) le = cache_.lightEnv(*c.env, &pose);
            mr.setFrame(camera, le, viewW, viewH);
            lastEnv = c.env, envSet = true;
        }
        const Mat4 bones[1] = {c.world};
        const scene::Mat* mat = c.mesh->dir->resolve<scene::Mat>(c.mesh->mat);
        if (!mat) {
            mr.draw(*g, bones, Material{}, ctx);
            continue;
        }
        for (int pass = 0; mat && pass < 4; ++pass) {
            mr.draw(*g, bones, cache_.material(*mat, &pose), ctx);
            if (mat->nextPass.empty() || !mat->dir) break;
            const scene::Mat* next = mat->dir->resolve<scene::Mat>(mat->nextPass);
            mat = next != mat ? next : nullptr;
        }
    }
    // Floor spots, beams and lenses: unlit, added.
    LightEnv plain;
    plain.ambient = {1, 1, 1};
    mr.setFrame(camera, plain, viewW, viewH);
    // SpotlightDrawer's draw (0x8222EC40) starts with the shadows (0x8222EE58).
    // shadow.mat adds black and writes depth, so the flattened character,
    // just above the floor, shuts the floor spots out of its shape: the
    // shadow is a hole in the pool of light [C for the order, I for the reading].
    shadowsDrawn_ = 0;
    if (bandView_ && world_.band)
        for (const Shadow& sh : shadows)
            if (chr::Character* c = world_.band->character(sh.target); c && c->visible && sh.dir.z != 0) {
                bandView_->drawShadow(mr, *c, {sh.dir.x, sh.dir.y, sh.dir.z}, hdr);
                ++shadowsDrawn_, ++shadowsTotal_;
            }
    for (const Extra& e : extras) {
        const GpuMesh* g = cache_.mesh(e.mesh);
        const scene::Mat* mat = e.mesh->dir ? e.mesh->dir->resolve<scene::Mat>(e.mesh->mat) : nullptr;
        if (!g || g->skinned || !mat) continue;
        LightEnv le;
        le.ambient = e.color, le.envAmbient = e.color;
        mr.setFrame(camera, le, viewW, viewH);
        const Mat4 bones[1] = {e.world};
        mr.draw(*g, bones, cache_.material(*mat, &pose), ctx);
    }
    if (!extras.empty()) mr.setFrame(camera, plain, viewW, viewH);
    const Mat4 identity[1] = {Mat4::identity()};
    for (auto& [m, b] : batches) {
        if (b.idx.empty()) continue;
        const GpuMesh g = mesh_.upload(b.verts, b.idx);
        mr.draw(g, identity, b.mat, ctx);
    }
    if (!beamBatch.idx.empty()) {
        const GpuMesh g = mesh_.upload(beamBatch.verts, beamBatch.idx);
        mr.draw(g, identity, beamBatch.mat, ctx);
    }
    if (perPixel && !beamPx_.empty()) drawBeams(camera, viewW, viewH, hdr, worldTarget, worldDepth);
    drawFlares(camera, viewW, viewH, hdr, worldTarget, worldDepth);
}

// The beam buffer (0x8225F948): its own target, cleared, every beam added
// with no depth test; half size when the picture is wider than 960 [C; the
// upsampling I]. Then FogPixel adds it to the world (before the track and
// HUD are drawn): drawer total (1) x 1024 x density 0.2 on the sampled
// value of the 360's fixed 16-bit EDRAM buffer [C]. What that read returns
// is not settled: 1/64 of the value (a gain of 3.2) turns every beam
// white, so the port keeps gain 1, the brightness its per-vertex beams had
// [I].
void SpotlightView::drawBeams(const Camera& camera, int viewW, int viewH, bool hdr, TargetId worldTarget, TextureId worldDepth) {
    const int bw = viewW > 960 ? viewW / 2 : viewW, bh = viewW > 960 ? viewH / 2 : viewH;
    if (!beamTarget_ || beamW_ != bw || beamH_ != bh) {
        if (beamTarget_) device_.destroyTarget(beamTarget_);
        TargetDesc d;
        d.width = bw, d.height = bh, d.format = TargetFormat::RGBA16F;
        std::string error;
        beamTarget_ = device_.createTarget(d, error);
        beamW_ = bw, beamH_ = bh;
        if (!beamTarget_) {
            SDL_Log("spotlights: no beam buffer: %s", error.c_str());
            return;
        }
    }
    const size_t bytes = beamPx_.size() * sizeof(float);
    if (bytes > beamVbSize_) {
        if (beamVb_) device_.destroyBuffer(beamVb_);
        beamVbSize_ = std::max<size_t>(bytes * 2, 16 * 1024);
        beamVb_ = device_.createBuffer(BufferKind::Vertex, beamVbSize_, nullptr, true);
    }
    device_.updateBuffer(beamVb_, 0, beamPx_.data(), bytes);
    device_.setTarget(beamTarget_);
    device_.setViewport(0, 0, bw, bh);
    const float zero[4] = {0, 0, 0, 0};
    device_.clear(zero, false);
    const bool zeroToOne = device_.info().clipDepthZeroToOne;
    const Mat4 vp = camera.viewProj(zeroToOne);
    BeamBlock bb;
    std::copy(vp.m, vp.m + 16, bb.viewProj);
    bb.depth[0] = camera.nearPlane, bb.depth[1] = camera.farPlane, bb.depth[2] = zeroToOne ? 1.0f : 0.0f, bb.depth[3] = 1.0f;
    DrawCall c;
    c.program = beamProgram_;
    c.state.blend = true;
    c.state.src = BlendFactor::One, c.state.dst = BlendFactor::One;
    c.layout = &beamLayout();
    c.vertices = beamVb_;
    c.count = int(beamPx_.size() / 12);
    c.textures[0] = worldDepth;
    c.samplers[0] = {Filter::Nearest, Wrap::Clamp, 0};
    c.uniforms[0] = UniformData{&bb, sizeof bb, 0};
    device_.draw(c);
    device_.setTarget(worldTarget);
    device_.setViewport(0, 0, viewW, viewH);
    BeamCompBlock cb{};
    cb.gain[0] = kBeamGain * (hdr ? 0.25f : 1.0f);
    DrawCall k;
    k.program = beamCompProgram_;
    k.state.blend = true;
    k.state.src = BlendFactor::One, k.state.dst = BlendFactor::One;
    k.layout = &triangleLayout();
    k.vertices = triangle_;
    k.count = 3;
    k.textures[0] = device_.targetTexture(beamTarget_);
    k.samplers[0] = {Filter::Linear, Wrap::Clamp, 0};
    k.uniforms[0] = UniformData{&cb, sizeof cb, 0};
    device_.draw(k);
}

void SpotlightView::drawFlares(const Camera& camera, int viewW, int viewH, bool hdr, TargetId worldTarget, TextureId worldDepth) {
    if (flareList_.empty() || !flareProgram_) return;
    const bool zeroToOne = device_.info().clipDepthZeroToOne;
    const Mat4 vp = camera.viewProj(zeroToOne);
    // The visibility pass (needs the world's depth: post-processing on).
    const bool test = worldTarget && worldDepth && visProgram_ && vis_[0] && vis_[1];
    if (test) {
        if (!visCleared_) {
            const float black[4] = {0, 0, 0, 1};
            for (TargetId t : vis_) {
                device_.setTarget(t);
                device_.clear(black, false);
            }
            visCleared_ = true;
        }
        static VisBlock vb;
        std::fill(&vb.flares[0][0], &vb.flares[0][0] + kMaxFlares * 4, 0.0f);
        std::fill(&vb.steps[0][0], &vb.steps[0][0] + kMaxFlares * 4, 0.0f);
        for (const Flare& f : flareList_) {
            const Vec4 c = mul(vp, f.pos);
            auto* e = vb.flares[f.slot];
            if (c.w > 0.01f) {
                const float nx = c.x / c.w, ny = c.y / c.w, nz = c.z / c.w;
                e[0] = nx * 0.5f + 0.5f, e[1] = ny * 0.5f + 0.5f;
                e[2] = zeroToOne ? nz : nz * 0.5f + 0.5f;
                vb.steps[f.slot][1] = std::fabs(nx) <= 1 && std::fabs(ny) <= 1 ? 1.0f : 0.0f;
            }
            e[3] = f.test ? 1.0f : 0.0f;
            vb.steps[f.slot][0] = 1.0f / float(f.steps);
        }
        vb.texel[0] = 1.0f / float(std::max(1, viewW)), vb.texel[1] = 1.0f / float(std::max(1, viewH));
        const int next = 1 - visCurrent_;
        device_.setTarget(vis_[next]);
        device_.setViewport(0, 0, kMaxFlares, 1);
        DrawCall c;
        c.program = visProgram_;
        c.state.alphaWrite = true;
        c.layout = &triangleLayout();
        c.vertices = triangle_;
        c.count = 3;
        c.textures[0] = device_.targetTexture(vis_[visCurrent_]);
        c.samplers[0] = {Filter::Nearest, Wrap::Clamp, 0};
        c.textures[1] = worldDepth;
        c.samplers[1] = {Filter::Nearest, Wrap::Clamp, 0};
        c.uniforms[0] = UniformData{&vb, sizeof vb, 0};
        device_.draw(c);
        visCurrent_ = next;
        device_.setTarget(worldTarget);
        device_.setViewport(0, 0, viewW, viewH);
    }
    // The flares, one draw per texture.
    std::vector<float> v;
    std::vector<std::pair<TextureId, std::pair<int, int>>> runs;
    std::stable_sort(flareList_.begin(), flareList_.end(), [](const Flare& a, const Flare& b) { return a.tex.id < b.tex.id; });
    const float aspect = float(std::max(1, viewW)) / float(std::max(1, viewH));
    for (const Flare& f : flareList_) {
        if (runs.empty() || runs.back().first.id != f.tex.id) runs.push_back({f.tex, {int(v.size() / 12), 0}});
        const float hw = f.size, hh = f.size * aspect;
        const float corners[6][4] = {{-1, -1, 0, 1}, {1, -1, 1, 1}, {1, 1, 1, 0}, {-1, -1, 0, 1}, {1, 1, 1, 0}, {-1, 1, 0, 0}};
        for (const auto& k : corners)
            v.insert(v.end(), {f.pos.x, f.pos.y, f.pos.z, k[0] * hw, k[1] * hh, k[2], k[3], f.rgba[0], f.rgba[1], f.rgba[2], f.rgba[3], float(f.slot)});
        runs.back().second.second += 6;
        ++flares_;
    }
    const size_t bytes = v.size() * sizeof(float);
    if (bytes > flareVbSize_) {
        if (flareVb_) device_.destroyBuffer(flareVb_);
        flareVbSize_ = std::max<size_t>(bytes * 2, 16 * 1024);
        flareVb_ = device_.createBuffer(BufferKind::Vertex, flareVbSize_, nullptr, true);
    }
    device_.updateBuffer(flareVb_, 0, v.data(), bytes);
    FlareBlock fb;
    std::copy(vp.m, vp.m + 16, fb.viewProj);
    fb.params[0] = hdr ? 0.25f : 1.0f;
    fb.params[1] = test ? 1.0f : 0.0f;
    fb.params[2] = fb.params[3] = 0;
    for (const auto& [tex, range] : runs) {
        if (!tex) continue;
        DrawCall c;
        c.program = flareProgram_;
        c.state.blend = true;
        c.state.src = BlendFactor::One, c.state.dst = BlendFactor::One;
        c.layout = &flareLayout();
        c.vertices = flareVb_;
        c.first = range.first, c.count = range.second;
        c.textures[0] = tex;
        c.samplers[0] = {Filter::Linear, Wrap::Clamp, 0};
        c.textures[1] = test ? device_.targetTexture(vis_[visCurrent_]) : tex;
        c.samplers[1] = {Filter::Nearest, Wrap::Clamp, 0};
        c.uniforms[0] = UniformData{&fb, sizeof fb, 0};
        device_.draw(c);
    }
}

}  // namespace gfx
