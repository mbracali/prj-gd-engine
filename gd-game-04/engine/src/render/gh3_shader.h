// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "data/matlib.h"
#include "data/scene.h"
#include "render/device.h"
#include "render/rmath.h"
#include "render/xenos_glsl.h"

class Disc;

// GH3's own material shaders on the render device: the material library
// (data/matlib.h) read once per disc, each template's technique translated
// (render/xenos_glsl.h) and compiled on first use, and the engine constants
// c128..c255 the shaders read (gh3shader::Globals), laid out as GH3's
// shaders declare them [C: the constant tables, docs/venues.md "Shading"].
namespace gh3shader {

// The disc's material library (cached; nullptr when the disc has none or
// it does not parse, with the reason logged once).
std::shared_ptr<const matlib::Library> library(const Disc& disc);

// The engine's float constants c128..c255 (one block, both stages) [C:
// register numbers from the constant tables of all 4,981 shaders].
struct Globals {
    float c[128][4] = {};
    float* reg(int r) { return c[r - 128]; }
    void set(int r, float x, float y, float z, float w) {
        float* p = reg(r);
        p[0] = x, p[1] = y, p[2] = z, p[3] = w;
    }
    // A matrix as the shaders read it: row-vector convention (v * M), its
    // rows in `rows` consecutive registers; `m` is a column-vector matrix
    // (render/rmath.h), so row i of the shaders' matrix is column i of m.
    void setMatrix(int r, const gfx::Mat4& m, int rows = 4);
};

// The per-object registers: c176..c191 (the lights) in c[0..15] and
// c216..c227 (g_World, g_WorldView, g_WorldViewProj) in c[16..27].
struct ObjectConstants {
    float c[28][4] = {};
    float* reg(int r) { return c[r < 216 ? r - 176 : r - 200]; }
    void set(int r, float x, float y, float z, float w) {
        float* p = reg(r);
        p[0] = x, p[1] = y, p[2] = z, p[3] = w;
    }
    void setMatrix(int r, const gfx::Mat4& m, int rows = 4) {
        for (int i = 0; i < rows; ++i) set(r + i, m.at(0, i), m.at(1, i), m.at(2, i), m.at(3, i));
    }
};

enum : int {
    kRegShadowLightIntensity = 137,  // g_shLightShadowIntensity
    kRegDistanceLodScaler = 138,     // g_distanceLODScaler
    kRegShadowDir = 140,             // g_shadowDirWorldSpace
    kRegShadowMapConstants = 141, kRegShadowLevel = 142, kRegShadowMapSize = 143,
    kRegViewToLight0 = 144, kRegViewToLight1 = 148,
    kRegInvProj = 152, kRegViewport = 154, kRegTime = 155, kRegTexProj0 = 156, kRegWibbleDir = 164,
    kRegAmbient = 176,               // g_ambientLightColor
    kRegDirLight0 = 177,             // g_dirLight<n>Dir 177 + 2n, Color 178, SpecColor 179 (+ 2n for 1..3: 181, 183, 185)
    kRegShLight0 = 188,              // g_shLight0..3Color: L1 spherical harmonics (constant, x, y, z)
    kRegWorld = 216, kRegWorldView = 220, kRegWorldViewProj = 224, kRegView = 232, kRegProjection = 236,
    kRegInvView = 240, kRegViewProj = 244,
};

// A light as the light manager sees it for one object.
struct LightIn {
    enum Type : uint8_t { Ambient, Directional, Point, Spot } type = Point;
    gfx::Vec3 pos, dir{0, 0, 1};  // dir: where the light shines (its +z)
    gfx::Vec3 color{1, 1, 1};     // colour x intensity
    float specular = 1;           // specular intensity
    float attenStart = 0, attenEnd = 1;
    float cosHotspot = 1, cosField = 0;
};
// The light registers (c176..c191) of an object at `center`: GH3's model
// is one ambient colour, one key directional light (direction, colour,
// specular colour) and the remaining lights as L1 spherical harmonics
// (constant, x, y, z) [C: the pixel shaders, e.g. D_3ChkBox_Pass0:
// colour = diffuse x vertex colour x (ambient + SH(n) x shadow +
// N.L x dir colour x shadow)]. How GH3 picks the key light and folds the
// others is the light manager's (C code, not reimplemented yet) [I].
void lightObject(const LightIn* lights, size_t count, const gfx::Vec3& center, ObjectConstants& out);

// One translated template.
struct Program {
    gfx::ProgramId id;
    uint16_t samplers = 0, cubes = 0, attribs = 0;
    std::string name;
};

// Programs of one device, by template key. Not thread-safe.
class Programs {
public:
    ~Programs() { shutdown(); }
    void init(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib);
    void shutdown();
    bool ready() const { return lib_ != nullptr; }
    // The template's program for a technique (compiled on first use); null
    // when the template is unknown or its shaders cannot be translated.
    const Program* get(uint32_t templateKey, matlib::Technique t = matlib::Default);
    const matlib::Library* library() const { return lib_.get(); }
    gfx::TextureId white() const { return white_; }
    gfx::TextureId whiteCube() const { return whiteCube_; }

private:
    gfx::RenderDevice* dev_ = nullptr;
    std::shared_ptr<const matlib::Library> lib_;
    std::unordered_map<uint64_t, std::unique_ptr<Program>> programs_;  // null entry: failed
    gfx::TextureId white_, whiteCube_;
};

// The program description of a translated pair (shader test, Programs).
gfx::ProgramDesc programDesc(const xenos::Program& p, const std::string& name, bool clipZeroToOne);

// The per-material constant blocks: c0.. of each stage, the pixel block's
// c63.x the alpha test reference (< 0: none).
struct MaterialConstants {
    float vs[64][4] = {};
    float ps[64][4] = {};
};
void fillMaterialConstants(const scene::Material& m, float alphaRef, MaterialConstants& out);

// The vertex layout every GH3-shader mesh uses (attribute locations of
// xenos::attribLocation).
struct Vertex {
    float pos[3];
    float nrm[3];
    float tan[3];
    float bin[3];
    uint8_t col[4];
    float uv[7][2];  // TEXCOORD0..6
    float pos2[3];   // POSITION1
};
const gfx::VertexLayout& vertexLayout();

}  // namespace gh3shader
