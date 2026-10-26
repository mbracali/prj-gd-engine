// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>

#include "render/camera.h"
#include "render/device.h"
#include "render/rmath.h"
#include "render/standard_shader.h"

// GH2's materials (Mat v28) and lighting environment (Environ + Light) as
// the renderer needs them, and how a material picks its shader permutation
// and render state. The scene loader fills these from the .milo objects.
//
// The rules follow the executable's material setup [C: decompiled]:
// - shader key: StandardKey below, one bit per Mat field (see stdkey);
// - blend: dest (0) keeps the frame (zero, one); src (1) is opaque, but turns
//   into src-alpha while the environment fades out; add (2) one, one;
//   src_alpha (3) the usual alpha blend; src_alpha_add (4) src-alpha, one;
//   subtract (5) one, one, reverse-subtract; multiply (6) zero, src-colour,
//   or dest-colour, src-colour (2x) with intensify;
// - z_mode: disable (0) no test or write; normal (1) less, write;
//   transparent (2) less-equal, no write; force (3) always, write;
//   decal (4) less-equal, write;
// - cull: the game culls clockwise faces (Direct3D's "CW") when set;
// - tex_wrap applies to the diffuse, normal, specular and glow maps; the
//   environment and custom-diffuse maps always clamp;
// - shader constants: emissive multiplier, intensify + 1 as the diffuse
//   texture's multiplier, colour/alpha, specular rgb/power, tex_xfm.
namespace gfx {

enum class MatBlend : int { Dest = 0, Src = 1, Add = 2, SrcAlpha = 3, SrcAlphaAdd = 4, Subtract = 5, Multiply = 6, PreMultAlpha = 7 };
enum class ZMode : int { Disable = 0, Normal = 1, Transparent = 2, Force = 3, Decal = 4 };
enum class TexGen : int { None = 0, Xfm = 1, Sphere = 2, Projected = 3, XfmOrigin = 4, Environ = 5 };
enum class TexWrap : int { Clamp = 0, Repeat = 1, BorderBlack = 2, BorderWhite = 3, Mirror = 4 };

struct Material {
    MatBlend blend = MatBlend::Src;
    float color[3] = {1, 1, 1};
    float alpha = 1;
    bool useEnviron = true;
    bool prelit = false;
    bool alphaCut = false;
    bool alphaWrite = false;
    bool intensify = false;
    bool cull = true;
    bool perPixelLit = false;
    bool normalize = false;
    ZMode zMode = ZMode::Normal;
    TexGen texGen = TexGen::None;
    TexWrap texWrap = TexWrap::Repeat;
    Mat4 texXfm;
    float emissiveMultiplier = 1;
    float specularRgb[3] = {0, 0, 0};
    float specularPower = 10;
    TextureId diffuseTex, normalMap, emissiveMap, specularMap, customDiffuseMap, environMap;
};

struct Light {
    Vec3 vec;            // directional: unit vector towards the light; point: position
    Vec3 color{1, 1, 1};
    float range = 0;     // point lights: distance where the light reaches zero (0: no falloff)
};

// The lighting environment of a frame: GH2's Environ with its lights.
struct LightEnv {
    Vec3 ambient{1, 1, 1};       // materials without use_environ (GH2 uploads white)
    Vec3 envAmbient{0.3f, 0.3f, 0.3f};
    Light lights[7];
    int lightCount = 0;          // including the projected ones, which come last
    int projectedCount = 0;      // 0..3
    bool directional = true;
    bool fog = false;
    float fogColor[4] = {0, 0, 0, 1};
    float fogStart = 0, fogEnd = 1;
    bool fade = false;           // fade-out with distance
    float fadeStart = 0, fadeEnd = 1;
    Mat4 spotXfm[3];             // projected lights: world -> (u z, v z, z)
    TextureId spot[3], spotShadow[3];
    TextureId shadowMap;         // the shadow buffer (with a ShadowBuffer key)
    Mat4 worldToShadow;
    bool fadeActive() const { return fade && fadeEnd != fadeStart; }
};

// What kind of draw this is (GH2's draw modes, simplified).
struct DrawContext {
    int bones = 1;               // transforms blended per vertex (1..4)
    bool lit = true;             // the normal lit pass (false: plain passes)
    bool particles = false;
    bool shadowBuffer = false;
    bool hdr = false;
    bool depthFade = false;      // soft particles (fade mode 3)
};

uint32_t standardKey(const Material& m, const LightEnv* env, const DrawContext& ctx);
PipelineState materialPipeline(const Material& m, const LightEnv* env);

// A mesh on the GPU, in GH2's vertex layout (meshLayout).
struct GpuMesh {
    BufferId vertices, indices;
    int indexCount = 0;
    bool indices32 = false;
    bool skinned = false;        // the four floats after the normal are weights, not colour
    bool tangents = false;       // 64-byte vertices with a tangent (xyz, handedness) after the uv
};
// GH2's Mesh v28 vertex: position, normal, four floats (RGBA colour, or bone
// weights when skinned), uv: 48 bytes; with tangents 64.
const VertexLayout& meshLayout(bool skinned, bool tangents);

// Draws meshes with GH2 materials through the standard shader.
class MaterialRenderer {
public:
    MaterialRenderer(RenderDevice& device, StandardShaders& shaders) : device_(device), shaders_(shaders) {}
    // Per-frame constants: camera and lighting (uploaded once; call again
    // each frame, after the frame has begun).
    void setFrame(const Camera& camera, const LightEnv& env, int viewportW, int viewportH, TextureId sceneDepth = {});
    // `bones`: ctx.bones transforms (one for a rigid mesh).
    void draw(const GpuMesh& mesh, const Mat4* bones, const Material& m, const DrawContext& ctx);

private:
    RenderDevice& device_;
    StandardShaders& shaders_;
    StdFrameBlock frame_{};
    UniformData frameStaged_;
    LightEnv env_;
    TextureId sceneDepth_;
};

}  // namespace gfx
