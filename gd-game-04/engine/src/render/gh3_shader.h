// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "data/matlib.h"
#include "data/scene.h"
#include "data/venue.h"
#include "render/venue_lights.h"
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

// A light as the light manager sees it (the light record 822BE020 fills,
// with the light show's live values) [C: docs/venues.md "Lights"].
struct LightIn {
    enum Type : uint8_t { Ambient, Directional, Point, Spot } type = Directional;
    gfx::Vec3 pos, dir{0, -1, 0};  // dir: where the light shines
    gfx::Vec3 color{1, 1, 1};      // lightcolor (0..1), not scaled
    float intensity = 1, specular = 1, shadow = 1;
    float attA = 0, attB = -2000;  // point / spot: attenend^2, 1 / (attenend^2 - attenstart^2 + 0.0001)
    bool dynamic = false;          // a candidate everywhere (no box)
    bool shadowLight = false;      // ranked x 50000
    gfx::Vec3 boxCenter, boxHalf{1e9f, 1e9f, 1e9f};
    float groupScale = 1;          // the group's intensity x the fade weight x the modulation (1)
};
LightIn lightIn(const venue::Light& l, const venue::LightState& s);
// The light group rule of the candidate query (822BB680): the object's
// group, `band` lights for an `alt_band` object, and `defaultgroup` (a
// light without a lightgroup) everywhere [C].
bool groupLights(uint32_t lightGroup, uint32_t objectGroup);

// The light registers (c176..c191) of an object at `p` (a mesh: its
// bounding sphere's centre; a model: its origin): 823DA990's light set and
// 823DB168's upload [C]. The candidates are the lights whose box holds `p`
// (and the dynamic ones), at most 32 in order, ranked by luminance x
// intensity x attenuation (x 50000 for shadow lights); the first
// directional / point light is the key light (c177..c179, not in the SH),
// the next three take the specular slots (c180..c185) and go into the L1
// spherical harmonics (4/17, 8/17 l) with every other directional, point
// (attenuated) and spot light (by the unnormalised axis distance); ambient
// lights sum into c176.
void lightObject(const LightIn* lights, size_t count, const gfx::Vec3& p, ObjectConstants& out);

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
    // A named technique of a template (the filter templates' passes, e.g.
    // ImmediateMode's "_2D_Cutoff"), its Default variant; null when missing
    // or not translated.
    const Program* get(uint32_t templateKey, const std::string& technique);
    const matlib::Library* library() const { return lib_.get(); }
    gfx::TextureId white() const { return white_; }
    gfx::TextureId whiteCube() const { return whiteCube_; }

private:
    gfx::RenderDevice* dev_ = nullptr;
    std::shared_ptr<const matlib::Library> lib_;
    std::unordered_map<uint64_t, std::unique_ptr<Program>> programs_;  // null entry: failed
    std::unordered_map<std::string, std::unique_ptr<Program>> named_;
    std::unique_ptr<Program> make(int vs, int ps, const std::string& name);
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
