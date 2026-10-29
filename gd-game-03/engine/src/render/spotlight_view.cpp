// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/spotlight_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "game/world.h"
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

bool alphaBlend(const Material& m) { return m.blend == MatBlend::SrcAlpha || m.blend == MatBlend::SrcAlphaAdd || m.blend == MatBlend::PreMultAlpha; }

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

}  // namespace

SpotlightView::SpotlightView(World& world, SceneCache& cache) : world_(world), cache_(cache), device_(cache.device()), mesh_(cache.device()) {
    std::string error;
    visProgram_ = device_.createProgram(visDesc(), error);
    if (!visProgram_) SDL_Log("spotlights: %s does not compile: %s", visDesc().name.c_str(), error.c_str());
    error.clear();
    flareProgram_ = device_.createProgram(flareDesc(), error);
    if (!flareProgram_) SDL_Log("spotlights: %s does not compile: %s", flareDesc().name.c_str(), error.c_str());
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
}

std::vector<ProgramDesc> SpotlightView::allPrograms() { return {visDesc(), flareDesc()}; }

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
    flareList_.clear();
    // Development aid: GD03_SPOTS=<letters> draws only those parts (c cans,
    // f floor spots, b beams, l lenses, F flares); unset: all.
    const char* only = SDL_getenv("GD03_SPOTS");
    auto on = [&](char c) { return !only || std::strchr(only, c) != nullptr; };
    const ScenePose& pose = world_.pose();
    Venue& venue = world_.venue();
    const bool mp = venue.variant() != VenueVariant::Single;
    const Vec3 camPos = camera.position();
    const Vec3 camFwd = normalize(Vec3{camera.world.m[4], camera.world.m[5], camera.world.m[6]});
    std::map<const scene::Mat*, Batch> batches;
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
    std::vector<Can> cans;
    const auto& states = world_.spots();
    for (const scene::Spotlight* s : venue.all<scene::Spotlight>()) {
        if (!s->decoded || !pose.showing(s)) continue;
        // The preset's state over the object's own.
        float intensity = s->intensity;
        std::array<float, 4> color = s->color;
        std::string target = s->target;
        bool flareOn = s->flareEnabled;
        scene::Xfm x = pose.world(s);
        if (auto it = states.find(s); it != states.end()) {
            intensity = it->second.intensity, color = it->second.color;
            target = it->second.target;
            flareOn = flareOn && it->second.flare;
            if (it->second.rotation != std::array<float, 4>{}) scene::quatToRot(it->second.rotation, x.rot);
        }
        const float k = intensity * s->beam.brighten;
        const float rgb[3] = {color[0] * k, color[1] * k, color[2] * k};
        if (rgb[0] <= 0.002f && rgb[1] <= 0.002f && rgb[2] <= 0.002f) continue;  // 0x822328E8: unlit, not drawn
        ++spots_;
        const Vec3 pos = v3(x.pos);
        Vec3 right{x.rot[0], x.rot[1], x.rot[2]}, fwd{x.rot[3], x.rot[4], x.rot[5]}, up{x.rot[6], x.rot[7], x.rot[8]};
        // A target turns the light's -z onto it (Spotlight target).
        if (!target.empty()) {
            scene::Xfm tx;
            if (venueObjectXfm(world_, target, mp, tx)) {
                Vec3 tp = v3(tx.pos);
                if (const scene::Object* o = venue.find(target); !o || o->cls == "Character" || o->cls == "BandCharacter") tp.z += 35;
                const Vec3 d = tp - pos;
                if (dot(d, d) > 1) {
                    up = normalize(d) * -1.0f;
                    right = cross(Vec3{0, 1, 0}, up);
                    if (dot(right, right) < 1e-6f) right = Vec3{1, 0, 0};
                    right = normalize(right);
                    fwd = cross(up, right);
                }
            }
        }
        Vec3 dir = up * -1.0f;
        // angle_offset (degrees) tilts the beam about the light's x and y [I].
        if (s->beam.angleOffset[0] != 0 || s->beam.angleOffset[1] != 0) {
            const float ax = s->beam.angleOffset[0] * kPi / 180, ay = s->beam.angleOffset[1] * kPi / 180;
            dir = normalize(dir * std::cos(ax) + fwd * std::sin(ax));
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
        // The beam: a camera-facing strip.
        if (s->beam.length > 0 && on('b'))
            if (const scene::Mat* bm = findMat(*s, s->beam.material)) {
                const Vec3 a = pos + dir * s->beam.offset, b = a + dir * s->beam.length;
                const Vec3 mid = (a + b) * 0.5f;
                Vec3 view = mid - camPos;
                view = normalize(view);
                Vec3 side = cross(dir, view);
                const float edgeOn = std::sqrt(dot(side, side));
                if (edgeOn > 1e-3f) {
                    side = side * (1.0f / edgeOn);
                    const float facing = std::clamp(edgeOn / 0.35f, 0.0f, 1.0f);
                    Batch& bt = batch(bm);
                    const bool alpha = alphaBlend(bt.mat);
                    const uint16_t base = uint16_t(bt.verts.size() / 16);
                    if (base < 60000) {
                        const float rows[3] = {0, std::clamp(1 - s->beam.bottomBorder, 0.0f, 1.0f), 1};
                        for (float v : rows) {
                            const Vec3 c = a + (b - a) * v;
                            const float radius = s->beam.topRadius + (s->beam.bottomRadius - s->beam.topRadius) * v;
                            const float border = std::clamp(s->beam.topSideBorder + (s->beam.bottomSideBorder - s->beam.topSideBorder) * v, 0.0f, 0.99f);
                            const float lenA = v >= 1 ? 0.0f : 1.0f;
                            const float cols[4] = {-1, -1 + border, 1 - border, 1};
                            for (float u : cols) {
                                const float sideA = std::fabs(u) >= 1 ? 0.0f : 1.0f;
                                const float aa = lenA * sideA * facing;
                                float col[4] = {rgb[0], rgb[1], rgb[2], aa};
                                if (!alpha)
                                    for (int q = 0; q < 3; ++q) col[q] *= aa;
                                vertex(bt.verts, c + side * (u * radius), col, (u + 1) * 0.5f, v);
                            }
                        }
                        for (uint16_t r = 0; r < 2; ++r)
                            for (uint16_t c = 0; c < 3; ++c) {
                                const uint16_t i0 = uint16_t(base + r * 4 + c);
                                quadIdx(bt, i0, uint16_t(i0 + 1), uint16_t(i0 + 5), uint16_t(i0 + 4));
                            }
                        ++beams_;
                    }
                }
            }
        // The floor spot where the beam meets z = spot_height.
        if (const scene::Mat* sm = on('f') ? findMat(*s, s->spotMaterial) : nullptr; sm && dir.z < -0.05f && s->spotScale > 0) {
            const float t = (s->spotHeight - pos.z) / dir.z;
            if (t > 0 && t < 5000) {
                const Vec3 c = pos + dir * t;
                Batch& bt = batch(sm);
                const uint16_t base = uint16_t(bt.verts.size() / 16);
                const float r = s->spotScale;
                const float col[4] = {rgb[0], rgb[1], rgb[2], 1};
                vertex(bt.verts, c + Vec3{-r, -r, 0}, col, 0, 0);
                vertex(bt.verts, c + Vec3{r, -r, 0}, col, 1, 0);
                vertex(bt.verts, c + Vec3{r, r, 0}, col, 1, 1);
                vertex(bt.verts, c + Vec3{-r, r, 0}, col, 0, 1);
                quadIdx(bt, base, uint16_t(base + 1), uint16_t(base + 2), uint16_t(base + 3));
            }
        }
        // The lens: a disc across the beam at lens_offset.
        if (const scene::Mat* lm = on('l') ? findMat(*s, s->lensMaterial) : nullptr; lm && s->lensSize > 0) {
            const Vec3 c = pos + dir * s->lensOffset;
            const Vec3 toCam = normalize(camPos - c);
            const float face = std::clamp(dot(dir, toCam), 0.0f, 1.0f);
            if (face > 0.01f) {
                Batch& bt = batch(lm);
                const uint16_t base = uint16_t(bt.verts.size() / 16);
                const float r = s->lensSize;
                const float col[4] = {rgb[0] * face, rgb[1] * face, rgb[2] * face, face};
                vertex(bt.verts, c + (right * -r) + (fwd * -r), col, 0, 0);
                vertex(bt.verts, c + (right * r) + (fwd * -r), col, 1, 0);
                vertex(bt.verts, c + (right * r) + (fwd * r), col, 1, 1);
                vertex(bt.verts, c + (right * -r) + (fwd * r), col, 0, 1);
                quadIdx(bt, base, uint16_t(base + 1), uint16_t(base + 2), uint16_t(base + 3));
            }
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

    if (SDL_getenv("GD03_SPOTS_LOG")) {
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
    const Mat4 identity[1] = {Mat4::identity()};
    for (auto& [m, b] : batches) {
        if (b.idx.empty()) continue;
        const GpuMesh g = mesh_.upload(b.verts, b.idx);
        mr.draw(g, identity, b.mat, ctx);
    }
    drawFlares(camera, viewW, viewH, hdr, worldTarget, worldDepth);
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
