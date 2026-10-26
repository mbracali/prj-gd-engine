// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/material.h"

#include <algorithm>
#include <cstring>

namespace gfx {

uint32_t standardKey(const Material& m, const LightEnv* env, const DrawContext& ctx) {
    using namespace stdkey;
    uint32_t k = 0;
    if (m.diffuseTex) k |= DiffuseMap;
    if (m.prelit || ctx.particles) k |= Prelit;
    if (m.useEnviron && ctx.lit) k |= UseEnviron;
    if (ctx.lit) {
        const bool ppl = m.perPixelLit;
        if (ppl) k |= PerPixel;
        if (ppl && m.normalMap) k |= NormalMap;
        if (ppl && m.customDiffuseMap) k |= CustomDiffuse;
        if (ppl && m.specularMap) k |= SpecularMap;
        if ((ppl && m.specularMap) || m.specularRgb[0] != 0 || m.specularRgb[1] != 0 || m.specularRgb[2] != 0) k |= Specular;
        if (m.emissiveMap) k |= GlowMap;
        if (m.environMap) k |= EnvironMap;
        if ((k & NormalMap) && m.normalize) k |= Normalize;
        switch (m.texGen) {
        case TexGen::Sphere: k |= 1u << TexGenShift; break;     // from the normal
        case TexGen::Projected: k |= 2u << TexGenShift; break;  // from the world position
        case TexGen::Environ: k |= 3u << TexGenShift; break;    // from the reflection
        default: break;
        }
    }
    k |= uint32_t(std::clamp(ctx.bones, 1, 4) - 1) << WeightsShift;
    if (ctx.lit && !ctx.particles && m.perPixelLit && ctx.shadowBuffer) k |= ShadowBuffer;
    if (env && m.useEnviron && ctx.lit && !ctx.particles) {
        k |= uint32_t(std::clamp(env->lightCount, 0, 7)) << LightsShift;
        k |= uint32_t(std::clamp(env->projectedCount, 0, 3)) << ProjLightsShift;
        if (env->directional) k |= DirLights;
    }
    // Fog only for blends that replace or darken the frame.
    const bool fogBlend = m.blend == MatBlend::Src || m.blend == MatBlend::SrcAlpha || int(m.blend) >= int(MatBlend::Multiply);
    if (env && env->fog && fogBlend && ctx.lit && !ctx.particles) k |= Fog;
    if (env && env->fadeActive()) {
        if (m.blend == MatBlend::Src || m.blend == MatBlend::SrcAlpha) k |= 1u << FadeShift;  // fade the alpha
        else if (m.blend != MatBlend::Dest && m.blend != MatBlend::PreMultAlpha) k |= 2u << FadeShift;  // fade the colour
    }
    if (ctx.depthFade) k |= 3u << FadeShift;
    if (ctx.hdr && m.blend != MatBlend::Multiply) k |= Hdr;
    return k;
}

PipelineState materialPipeline(const Material& m, const LightEnv* env) {
    PipelineState s;
    MatBlend blend = m.blend;
    if (blend == MatBlend::Src && env && env->fadeActive()) blend = MatBlend::SrcAlpha;  // fading needs the alpha
    s.blend = blend != MatBlend::Src;
    switch (blend) {
    case MatBlend::Dest: s.src = BlendFactor::Zero, s.dst = BlendFactor::One; break;
    case MatBlend::Src: s.src = BlendFactor::One, s.dst = BlendFactor::Zero; break;
    case MatBlend::Add: s.src = BlendFactor::One, s.dst = BlendFactor::One; break;
    case MatBlend::SrcAlpha: s.src = BlendFactor::SrcAlpha, s.dst = BlendFactor::InvSrcAlpha; break;
    case MatBlend::SrcAlphaAdd: s.src = BlendFactor::SrcAlpha, s.dst = BlendFactor::One; break;
    case MatBlend::Subtract: s.src = BlendFactor::One, s.dst = BlendFactor::One, s.op = BlendOp::ReverseSubtract; break;
    case MatBlend::Multiply: s.src = m.intensify ? BlendFactor::DstColor : BlendFactor::Zero, s.dst = BlendFactor::SrcColor; break;
    case MatBlend::PreMultAlpha: s.src = BlendFactor::One, s.dst = BlendFactor::InvSrcAlpha; break;  // [I]: the game sets no factors
    }
    switch (m.zMode) {
    case ZMode::Disable: s.depthTest = false, s.depthWrite = false; break;
    case ZMode::Normal: s.depthTest = true, s.depthWrite = true, s.depthFunc = CompareFunc::Less; break;
    case ZMode::Transparent: s.depthTest = true, s.depthWrite = false, s.depthFunc = CompareFunc::LessEqual; break;
    case ZMode::Force: s.depthTest = true, s.depthWrite = true, s.depthFunc = CompareFunc::Always; break;
    case ZMode::Decal: s.depthTest = true, s.depthWrite = true, s.depthFunc = CompareFunc::LessEqual; break;
    }
    // GH2's meshes wind their visible faces counter-clockwise about the
    // normal (every face of the track and gem meshes); through our
    // right-handed projection those are clockwise on a y-down screen, so
    // the culled side is the counter-clockwise one. The game's D3D "CW"
    // applies to its mirrored (left-handed) projection [I].
    s.cull = m.cull ? Cull::CounterClockwise : Cull::None;
    s.alphaWrite = m.alphaWrite;
    return s;
}

const VertexLayout& meshLayout(bool skinned, bool tangents) {
    auto make = [](bool skin, bool tan) {
        VertexLayout l;
        l.stride = tan ? 64 : 48;
        l.attribs[0] = {kAttribPos, 3, AttribType::Float, 0};
        l.attribs[1] = {kAttribNormal, 3, AttribType::Float, 12};
        l.attribs[2] = {uint8_t(skin ? kAttribWeights : kAttribColor), 4, AttribType::Float, 24};
        l.attribs[3] = {kAttribUv, 2, AttribType::Float, 40};
        if (tan) l.attribs[4] = {kAttribTangent, 4, AttribType::Float, 48};
        return l;
    };
    static const VertexLayout layouts[4] = {make(false, false), make(false, true), make(true, false), make(true, true)};
    return layouts[(skinned ? 2 : 0) + (tangents ? 1 : 0)];
}

namespace {
void put4(float* d, float a, float b, float c, float e) { d[0] = a, d[1] = b, d[2] = c, d[3] = e; }
void put16(float* d, const Mat4& m) { std::memcpy(d, m.m, sizeof m.m); }
float inverseSpan(float start, float end) { return end != start ? 1.0f / (end - start) : 0.0f; }
}  // namespace

void MaterialRenderer::setFrame(const Camera& camera, const LightEnv& env, int viewportW, int viewportH, TextureId sceneDepth) {
    env_ = env;
    sceneDepth_ = sceneDepth;
    StdFrameBlock& f = frame_;
    f = {};
    put16(f.viewProj, camera.viewProj(device_.info().clipDepthZeroToOne));
    const Vec3 eye = camera.position();
    put4(f.cameraPos, eye.x, eye.y, eye.z, 1);
    put4(f.ambient, env.ambient.x, env.ambient.y, env.ambient.z, 1);
    put4(f.envAmbient, env.envAmbient.x, env.envAmbient.y, env.envAmbient.z, 1);
    for (int i = 0; i < 7; ++i) {
        const Light& l = env.lights[i];
        put4(f.lightVec[i], l.vec.x, l.vec.y, l.vec.z, 0);
        put4(f.lightColor[i], l.color.x, l.color.y, l.color.z, l.range > 0 ? 1.0f / l.range : 0.0f);
    }
    std::memcpy(f.fogColor, env.fogColor, sizeof f.fogColor);
    put4(f.fogRange, env.fogEnd, inverseSpan(env.fogStart, env.fogEnd), 0, 0);
    put4(f.fadeRange, env.fadeEnd, inverseSpan(env.fadeStart, env.fadeEnd), 0, 0);
    put16(f.worldToShadow, env.worldToShadow);
    for (int i = 0; i < 3; ++i) put16(f.spotXfm[i], env.spotXfm[i]);
    put4(f.depthRange, camera.nearPlane, camera.farPlane, 0.05f, 0);
    put4(f.screen, float(viewportW), float(viewportH), viewportW ? 1.0f / float(viewportW) : 0.0f, viewportH ? 1.0f / float(viewportH) : 0.0f);
    frameStaged_ = device_.stageUniforms(&frame_, sizeof frame_);
}

void MaterialRenderer::draw(const GpuMesh& mesh, const Mat4* bones, const Material& m, const DrawContext& ctx) {
    const uint32_t key = standardKey(m, &env_, ctx);
    DrawCall c;
    c.program = shaders_.program(key);
    if (!c.program) return;
    c.state = materialPipeline(m, &env_);
    c.layout = &meshLayout(mesh.skinned, mesh.tangents);
    c.vertices = mesh.vertices;
    c.vertexOffset = mesh.vertexOffset;
    c.indices = mesh.indices;
    c.indices32 = mesh.indices32;
    c.first = mesh.firstIndex;
    c.count = mesh.indexCount;

    Wrap wrap = Wrap::Repeat;
    switch (m.texWrap) {
    case TexWrap::Clamp: wrap = Wrap::Clamp; break;
    case TexWrap::Repeat: wrap = Wrap::Repeat; break;
    case TexWrap::BorderBlack: wrap = Wrap::BorderBlack; break;
    case TexWrap::BorderWhite: wrap = Wrap::BorderWhite; break;
    case TexWrap::Mirror: wrap = Wrap::Mirror; break;
    }
    auto bind = [&](int slot, TextureId t, Wrap w) {
        c.textures[slot] = t;
        c.samplers[slot] = {Filter::Trilinear, w, 0};
    };
    bind(kSlotDiffuse, m.diffuseTex, wrap);
    bind(kSlotNormal, m.normalMap, wrap);
    bind(kSlotSpecular, m.specularMap, wrap);
    bind(kSlotGlow, m.emissiveMap, wrap);
    bind(kSlotEnviron, m.environMap, Wrap::Clamp);
    bind(kSlotCustomDiffuse, m.customDiffuseMap, Wrap::Clamp);
    bind(kSlotShadow0, env_.shadowMap ? env_.shadowMap : env_.spotShadow[0], Wrap::Clamp);
    bind(kSlotShadow1, env_.spotShadow[1], Wrap::Clamp);
    bind(kSlotShadow2, env_.spotShadow[2], Wrap::Clamp);
    for (int i = 0; i < 3; ++i) bind(kSlotSpot0 + i, env_.spot[i], Wrap::BorderBlack);
    bind(kSlotSceneDepth, sceneDepth_, Wrap::Clamp);

    StdMaterialBlock mb{};
    put4(mb.diffuse, m.color[0], m.color[1], m.color[2], m.alpha);
    put4(mb.specular, m.specularRgb[0], m.specularRgb[1], m.specularRgb[2], m.specularPower);
    put16(mb.texXfm, m.texXfm);
    put4(mb.multipliers, m.emissiveMultiplier, m.intensify ? 2.0f : 1.0f, 0, 0);
    // The fade-out's target colour: white for multiply (no darkening), else transparent black.
    const float fadeTo = m.blend == MatBlend::Multiply ? 1.0f : 0.0f;
    put4(mb.fadeColor, fadeTo, fadeTo, fadeTo, fadeTo);
    // [I] The threshold of alpha_cut is not traced yet.
    put4(mb.alphaTest, m.alphaCut ? 1.0f : 0.0f, 0.5f, 0, 0);

    StdObjectBlock ob{};
    const int n = std::clamp(ctx.bones, 1, 4);
    for (int i = 0; i < 4; ++i) put16(ob.model[i], bones[std::min(i, n - 1)]);

    c.uniforms[kBlockFrame] = frameStaged_;
    c.uniforms[kBlockMaterial] = {&mb, sizeof mb};
    c.uniforms[kBlockObject] = {&ob, sizeof ob};
    device_.draw(c);
}

}  // namespace gfx
