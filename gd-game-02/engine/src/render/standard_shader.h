// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "render/device.h"

// GH2's "standard" material shader, as our own GLSL.
//
// The game draws every mesh with one shader, compiled per material into
// permutations chosen by a 32-bit key; the disc's shader cache names each
// one `standard_<key in hex>_vs|ps`. The key's layout below is the one the
// executable builds (its shader-define builder turns each field into a
// preprocessor switch). Keeping the same numbers means a key here names the
// same variant as the game's cache, which helps when comparing the two.
namespace gfx::stdkey {

constexpr uint32_t PerPixel = 1u << 0;       // lighting per pixel instead of per vertex
constexpr uint32_t SpecularMap = 1u << 1;    // specular colour/power from a texture (per-pixel only)
constexpr uint32_t Specular = 1u << 2;       // specular highlights
constexpr uint32_t EnvironMap = 1u << 3;     // cube-map reflection (replaces the light specular)
constexpr uint32_t DiffuseMap = 1u << 4;
constexpr uint32_t NormalMap = 1u << 5;      // tangent-space normal map (per-pixel only)
constexpr uint32_t Normalize = 1u << 6;      // renormalise the interpolated tangent frame
constexpr uint32_t GlowMap = 1u << 7;        // emissive texture
constexpr uint32_t Prelit = 1u << 8;         // vertex colours instead of the material colour for ambient
constexpr uint32_t CustomDiffuse = 1u << 9;  // a 1D ramp texture replaces N.L (per-pixel only)
constexpr int TexGenShift = 10;              // 2 bits: 0 mesh uv, 1 normal, 2 world position, 3 reflection
constexpr int WeightsShift = 12;             // 2 bits: bone weights per vertex (0: rigid)
constexpr int LightsShift = 14;              // 3 bits: lights in the environment (incl. projected)
constexpr uint32_t DirLights = 1u << 17;     // lights are directional (else point lights)
constexpr uint32_t Fog = 1u << 18;
constexpr uint32_t ShadowBuffer = 1u << 19;
constexpr uint32_t CostEstimate = 1u << 21;  // GH2 debug view; ignored here
constexpr uint32_t Hdr = 1u << 22;           // colour out / 4 (the 10-bit HDR buffer)
constexpr uint32_t Extrude = 1u << 23;       // shadow-volume extrusion; not used by the standard shader
constexpr uint32_t Billboard = 1u << 25;     // no effect in the standard shader
constexpr int FadeShift = 26;                // 2 bits: 1 alpha, 2 colour, 3 colour + alpha + depth fade
constexpr int ProjLightsShift = 28;          // 2 bits: projected (spot) lights among the lights
constexpr uint32_t UseEnviron = 1u << 30;    // the environment's ambient (else the default white)

inline int texGen(uint32_t k) { return int(k >> TexGenShift) & 3; }
inline int weights(uint32_t k) { return int(k >> WeightsShift) & 3; }
inline int lights(uint32_t k) { return int(k >> LightsShift) & 7; }
inline int fade(uint32_t k) { return int(k >> FadeShift) & 3; }
inline int projLights(uint32_t k) { return int(k >> ProjLightsShift) & 3; }

}  // namespace gfx::stdkey

namespace gfx {

// Texture slots of the standard shader (the game's sampler registers).
enum StdSlot : int {
    kSlotDiffuse = 0, kSlotNormal = 1, kSlotSpecular = 2, kSlotGlow = 3, kSlotEnviron = 4,
    kSlotShadow0 = 5, kSlotShadow1 = 6, kSlotShadow2 = 7, kSlotSpot0 = 8, kSlotSpot1 = 9, kSlotSpot2 = 10,
    kSlotCustomDiffuse = 11, kSlotSceneDepth = 12,
};
// Uniform block bindings.
enum StdBlock : int { kBlockFrame = 0, kBlockMaterial = 1, kBlockObject = 2 };

// The uniform blocks, std140 (vec4 and mat4 members only, so the C++ layout
// matches). Matrices are column-major (render/rmath.h).
struct StdFrameBlock {
    float viewProj[16];
    float cameraPos[4];
    float ambient[4];       // default ambient (white in GH2)
    float envAmbient[4];    // the Environ's ambient (materials with use_environ)
    float lightVec[7][4];   // directional: unit vector towards the light; point: position
    float lightColor[7][4]; // rgb, w = 1 / range (point lights)
    float fogColor[4];
    float fogRange[4];      // x end, y 1 / (end - start)
    float fadeRange[4];     // x end, y 1 / (end - start)
    float worldToShadow[16];// world -> shadow-map texture space (xy 0..1, z depth)
    float spotXfm[3][16];   // world -> (u z, v z, z) of each projected light
    float depthRange[4];    // near, far, depth-fade scale (for fade mode 3)
    float screen[4];        // w, h, 1/w, 1/h
};
struct StdMaterialBlock {
    float diffuse[4];       // Mat colour and alpha
    float specular[4];      // rgb colour, w power
    float texXfm[16];
    float multipliers[4];   // x emissive multiplier, y intensify (1 or 2)
    float fadeColor[4];
    float alphaTest[4];     // x 1 when alpha cut, y threshold
};
struct StdObjectBlock {
    float model[4][16];     // bone (or object) transforms
};

// The vertex inputs (attribute locations).
enum StdAttrib : uint8_t { kAttribPos = 0, kAttribNormal = 1, kAttribColor = 2, kAttribUv = 3, kAttribWeights = 4, kAttribTangent = 5 };

class StandardShaders {
public:
    explicit StandardShaders(RenderDevice& device) : device_(device) {}
    ~StandardShaders();

    // The program for `key`, compiled on first use. If it fails to compile,
    // a simpler variant stands in (logged once): an effect never blacks out
    // the game. Empty only when even the plainest variant fails.
    ProgramId program(uint32_t key);
    // Compiles `key` with no fallback (for --shadertest).
    bool compile(uint32_t key, std::string& error);
    // The program description for a key (GLSL + defines).
    static ProgramDesc describe(uint32_t key);
    // Readable list of a key's switches, e.g. "diffuse_map per_pixel lights=3(dir)".
    static std::string name(uint32_t key);
    // Every permutation --shadertest compiles: each material feature
    // combination GH2 can produce, under a lit environment, then the
    // environment, skinning, fog, fade, shadow and HDR switches.
    static std::vector<uint32_t> testKeys();

private:
    RenderDevice& device_;
    std::unordered_map<uint32_t, ProgramId> programs_;
    std::vector<ProgramId> owned_;
    std::unordered_set<uint32_t> failedLogged_;
};

}  // namespace gfx
