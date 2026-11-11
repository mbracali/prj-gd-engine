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

// The original material shaders on the render device: the material library
// (data/matlib.h, gh4's MATL v3) read once per disc, each template's
// technique translated (render/xenos_glsl.h) and compiled ahead of the
// draw, and the engine constants the shaders read (gh3shader::Globals,
// ObjectConstants), laid out as gh4's shaders declare them [C: the
// constant tables, docs/render.md].
namespace gh3shader {

// The disc's material library (cached; nullptr when the disc has none or
// it does not parse, with the reason logged once).
std::shared_ptr<const matlib::Library> library(const Disc& disc);
// The disc library() last read from (the screen effects' sys textures in
// ZONES/global.pak go with the library: screen_fx.cpp), or null.
const Disc* libraryDisc();

// The engine's float constants c64..c255 (one block, both stages) [C:
// register numbers from the constant tables of all 4,981 shaders]; c64..c99
// are the filter passes' (m_filterSampleWeights c64, m_filterSampleOffsets
// c80, ...), c124 the vertex shaders' g_TextureProjection0.
struct Globals {
    float c[192][4] = {};
    float* reg(int r) { return c[r - 64]; }
    const float* reg(int r) const { return c[r - 64]; }
    void set(int r, float x, float y, float z, float w) {
        float* p = reg(r);
        p[0] = x, p[1] = y, p[2] = z, p[3] = w;
    }
    // A matrix as the shaders read it: row-vector convention (v * M), its
    // rows in `rows` consecutive registers; `m` is a column-vector matrix
    // (render/rmath.h), so row i of the shaders' matrix is column i of m.
    void setMatrix(int r, const gfx::Mat4& m, int rows = 4);
};

// The per-object registers (xenos::objectSlot): gh4's lights c160..c211,
// the world matrices c216..c231 and the light attenuations c248..c253.
struct ObjectConstants {
    float c[xenos::kObjectRegs][4] = {};
    float* reg(int r) { return c[xenos::objectSlot(r)]; }
    void set(int r, float x, float y, float z, float w) {
        float* p = reg(r);
        p[0] = x, p[1] = y, p[2] = z, p[3] = w;
    }
    void setMatrix(int r, const gfx::Mat4& m, int rows = 4) {
        for (int i = 0; i < rows; ++i) set(r + i, m.at(0, i), m.at(1, i), m.at(2, i), m.at(3, i));
    }
};

// gh4's registers [C data: the constant tables].
enum : int {
    kRegTextureProjection0 = 124,    // g_TextureProjection0 (vertex, 4 rows)
    kRegTopviewRange = 132,          // g_TopviewRange
    kRegWetEffect = 134,             // g_wetEffectParams
    kRegNearFar = 135,               // g_NearFar_Values
    kRegWibbleDir = 137,             // g_wibbleDirection
    kRegDistanceLodScaler = 138,     // g_distanceLODScaler
    kRegShadowDir = 140,             // g_shadowDirWorldSpace
    kRegShadowMapConstants = 141, kRegShadowLevel = 142, kRegShadowMapSize = 143,
    kRegViewToLight0 = 144, kRegViewToLight1 = 148,
    kRegInvProj = 152, kRegClipPlanes = 153, kRegViewport = 154, kRegTime = 155,
    kRegAmbient = 160,               // g_ambientLightColor
    kRegDirLight0 = 161,             // g_dirLight<n>Dir 161 + 2n (n > 0: 164, 166, 168), Color 162, SpecColor 163 (165, 167, 169)
    kRegShLight0 = 172,              // g_SHLight0..3: L1 spherical harmonics (constant, x, y, z)
    kRegPointLight = 176,            // g_PointLight: 4 lights x (position, colour, -, -)
    kRegSpotLight = 192,             // g_SpotLight: 5 lights x (direction, position, colour, -); the fifth's w row: its attenuation and cone
    kRegWorld = 216, kRegWorldView = 220, kRegWorldViewProj = 224, kRegWorldIT = 228,
    kRegView = 232, kRegProjection = 236, kRegInvView = 240, kRegViewProj = 244,
    kRegPointAtten = 248,            // g_pointLightGroup0AttenuationX / Y: lights 0..3 in x..w
    kRegSpotAtten = 250,             // g_spotLightGroup0AttenuationX / Y (distance), Z / W (cone): lights 0..3
};

// A light as the light manager sees it (the light record 822BE020 fills,
// with the light show's live values) [C: docs/venues.md "Lights"].
struct LightIn {
    enum Type : uint8_t { Ambient, Directional, Point, Spot, Prt } type = Directional;
    gfx::Vec3 pos, dir{0, -1, 0};  // dir: where the light shines
    gfx::Vec3 color{1, 1, 1};      // lightcolor (0..1), not scaled
    float intensity = 1, specular = 1, shadow = 1;
    float attA = 0, attB = -2000;  // point / spot: attenend^2, 1 / (attenend^2 - attenstart^2 + 0.0001)
    float coneX = 1, coneY = 0;    // spot: cone = saturate((cos - X) x Y), X = cos(field / 2), Y = 1 / (cos(hotspot / 2) - X)
    bool dynamic = false;          // a candidate everywhere (no box)
    bool shadowLight = false;      // ranked x 50000
    gfx::Vec3 boxCenter, boxHalf{1e9f, 1e9f, 1e9f};
    float groupScale = 1;          // the group's intensity x the fade weight x the modulation (1)
    gfx::Vec3 prt[4];              // Prt: its SH terms (coeff0..3)
};
LightIn lightIn(const venue::Light& l, const venue::LightState& s);
// The light group rule of the candidate query (822BB680): the object's
// group, `band` lights for an `alt_band` object, and `defaultgroup` (a
// light without a lightgroup) everywhere [C].
bool groupLights(uint32_t lightGroup, uint32_t objectGroup);

// The light registers of an object at `p` (a mesh: its bounding sphere's
// centre; a model: its origin), gh4's layout [C data: the registers and
// what the microcode does with them; I: which light takes which slot, the
// executable's light set is VMX128 code not read yet, docs/render.md
// "Lights"]. The candidates are GH3's (823DA990: the lights whose box
// holds `p` and the dynamic ones, at most 32, ranked by luminance x
// intensity x attenuation, x 50000 for shadow lights). The first
// directional light is the key light (c161..c163), the next three take
// the specular slots (c164..c169) and the L1 spherical harmonics (4/17,
// 8/17 l, c172..c175); the first four point lights are the vertex
// shaders' g_PointLight (c176 + 4i: position, colour x intensity; c248 /
// c249: attend^2, 1 / (attend^2 - attstart^2)), the first five spot
// lights g_SpotLight (c192 + 4i: direction, position, colour x intensity;
// c250..c253 and c211: distance and cone terms); lights beyond those go
// into the SH as GH3's did. Ambient lights sum into c160 and SH0 (the
// shaders read one or the other, never both [C data]); a prt light adds
// its coeff0..3 x intensity to the SH.
void lightObject(const LightIn* lights, size_t count, const gfx::Vec3& p, ObjectConstants& out);
// The light a camera-facing sprite at `p` takes from an object's registers
// (lit particles): the SH's constant term (with the ambient) and 4/17 of the
// key light, the vertex point / spot lights left out [I: the average over
// the sprite's normals; the quick material's lit shader not translated].
gfx::Vec3 spriteLight(const ObjectConstants& oc, const gfx::Vec3& p);

// The band's dynamic shadow for one frame (VenueStage::shadowPass), as the
// DynamicShadowReciever shaders read it [C: the microcode, 822B6E08]:
// g_ViewToLightProjection0 / 1 (c144 / c148: view space -> each cascade's
// Direct3D clip space), g_sampShadowMap0 / 1 (s14 / s15), g_shadowMapSize
// (c143.x / .y: cascade 0's / 1's texels), g_shadowMapConstants (c141: zw =
// the camera distance where the shadow fades out, smoothstepped) and
// g_shadowLevel (c142.zw: light = lit x (1 - i) + i). A pixel takes cascade
// 0 where its coordinates are inside that map (0 < u, v < 1, depth < 1),
// else cascade 1.
struct DynamicShadow {
    gfx::TextureId tex[2];       // the cascades' depth maps; tex[0] empty: no shadow
    gfx::Mat4 vp[2];             // world -> the cascade's clip space (the receivers' bias in)
    float size[2] = {1024, 1024};
    float blend[2] = {0, 0};     // c141.xy (not read by the receivers seen)
    float fadeStart = 0, fadeEnd = 1e9f;
    float intensity = 0.35f;
    explicit operator bool() const { return bool(tex[0]); }
    // One map for both cascades (the menus' backdrop), no fade.
    static DynamicShadow single(gfx::TextureId depth, const gfx::Mat4& vp, int size, float intensity) {
        DynamicShadow s;
        s.tex[0] = s.tex[1] = depth;
        s.vp[0] = s.vp[1] = vp;
        s.size[0] = s.size[1] = float(size);
        s.intensity = intensity;
        return s;
    }
};
// c141..c151 for a camera's `view` (world -> view) when `s` is on.
void setShadowGlobals(Globals& g, const DynamicShadow& s, const gfx::Mat4& view);

// One translated template.
struct Program {
    gfx::ProgramId id;
    uint16_t samplers = 0, cubes = 0, attribs = 0;
    std::string name;
    bool compiling = false;  // begun (RenderDevice::beginProgram), not polled ready yet
};

// Programs of one device, by template key. Not thread-safe. Every Programs
// of a device shares its compiled programs (the menus' zone, the song's
// same zone, the next song's: compiled once a run, no-stutter rule); they
// live as long as the device.
class Programs {
public:
    ~Programs() { shutdown(); }
    void init(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib);
    void shutdown();
    bool ready() const { return lib_ != nullptr; }
    // The template's program for a technique (compiled on first use); null
    // when the template is unknown or its shaders cannot be translated.
    const Program* get(uint32_t templateKey, matlib::Technique t = matlib::Default);
    // Whether get() made (or failed) that program before: no GL work left.
    bool has(uint32_t templateKey, matlib::Technique t = matlib::Default) const;
    // A named technique of a template (the filter templates' passes, e.g.
    // ImmediateMode's "_2D_Cutoff"), its Default variant; null when missing
    // or not translated.
    const Program* get(uint32_t templateKey, const std::string& technique);
    // A program asked for ahead (no-stutter rule): compiled in the driver's
    // background where it can (OpenGL), else now; settle() polls them, true
    // once every asked one is ready (get() of one not ready waits for it).
    void request(uint32_t templateKey, matlib::Technique t = matlib::Default);
    void request(uint32_t templateKey, const std::string& technique);
    bool settle();
    const matlib::Library* library() const { return lib_.get(); }
    gfx::TextureId white() const { return white_; }
    gfx::TextureId whiteCube() const { return whiteCube_; }

private:
    gfx::RenderDevice* dev_ = nullptr;
    std::shared_ptr<const matlib::Library> lib_;
    std::unordered_map<uint64_t, std::shared_ptr<Program>> programs_;  // null entry: failed
    std::unordered_map<std::string, std::shared_ptr<Program>> named_;
    std::unique_ptr<Program> make(int vs, int ps, const std::string& name, bool async);
    const Program* shared(const std::string& id, int vs, int ps, const std::string& name, std::shared_ptr<Program>& local, bool async = false);
    const Program* ready(std::shared_ptr<Program>& p);  // waits for a compiling one; null if it failed
    std::vector<std::shared_ptr<Program>> requested_;
    // Asked for on a device that compiles at once: made by settle, a slice a frame.
    std::vector<std::pair<uint32_t, std::string>> deferred_;  // (template, "#<technique>" or a technique name)
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

// A material's render state as GH3 sets it (Function_823DC180) [C]: the
// blend control of +0xd8 (13 modes: 0 opaque, 1 SRC_ALPHA + ONE, 2 its
// reverse subtract, 3 SRC_ALPHA / INV_SRC_ALPHA, 4 ZERO / SRC_ALPHA, 5
// DST_COLOR + ONE, 6 DST_COLOR / ZERO, 7 ONE + ONE, 8 SRC_ALPHA /
// INV_SRC_ALPHA (alpha kept), 9 ONE / INV_SRC_ALPHA, 10 DST_COLOR + ONE,
// 11 ONE - DST_COLOR reverse subtract, 12 ONE reverse subtract), depth
// write unless +0xcc & 0x20, the depth test (LESSEQUAL) unless +0xcc &
// 0x10. Blended materials write depth unless they say not to (the band's
// hair and the venues' blended meshes drew without it: see-through hair,
// smoke over the band; owner 11-04 doc 1.2 / 1.3).
void materialState(uint32_t blendMode, uint32_t flags, gfx::PipelineState& out);

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
