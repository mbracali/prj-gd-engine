// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// GH3's material templates: DATA/FXFILES/MaterialLibrary.bin.xen, loaded by
// Function_82383768 (0x82383768, path "fxfiles\\MaterialLibrary.bin.xen" in
// Function_82384668) [C]. Layout (docs/formats.md "Material library"):
//
//   0x00  "MATL", u32 2, u32 template count (418), u32 0, u32 string base, u32 0
//   0x18  u32 offsets[count] (from 0x18 + 4 * count)
//   B     template records (0x28 each): u32 0, u32 name (offset from B),
//         u32 key = qbkey(name), ... (the effect: techniques, passes,
//         parameters with their annotations, relocated by the loader)
//   ...   strings, then the compiled Xenos shaders (XDK format, magic
//         0x102A1101 vertex / 0x102A1100 pixel), one vertex + one pixel
//         shader per technique: Default, DepthOnly, AlphaOnly,
//         DynamicShadowReciever, ReducedLOD.
//
// A scene material's template key (scene::Material::shader) selects the
// template; its constsA are the vertex shader's float constants c0.., its
// constsB the pixel shader's c0.., its textures the samplers s0.. in order
// [C: the shaders' constant tables against the materials of all eight
// venues and the characters; docs/venues.md "Shading"].
namespace matlib {

enum Technique : uint8_t { Default, DepthOnly, AlphaOnly, DynamicShadowReceiver, ReducedLOD, kTechniques };

// A constant table entry (D3DXSHADER_CONSTANTINFO): set 0 bool, 1 int,
// 2 float4, 3 sampler.
struct Constant {
    std::string name;
    uint8_t set = 0;
    uint16_t reg = 0, count = 0;
};

// A vertex fetch instruction's input (usage as D3DDECLUSAGE: 0 position,
// 1 blend weight, 2 blend indices, 3 normal, 5 texcoord, 6 tangent, 7
// binormal, 10 colour).
struct Fetch {
    uint16_t address = 0;  // instruction index in the microcode
    uint8_t usage = 0, index = 0;
};

// An interpolator: a vertex shader export register (o0..o15) or a pixel
// shader input register (r0..), its component mask and its semantic
// (usage << 4 | index; 0x50 = TEXCOORD0, 0xa0 = COLOR0).
struct Link {
    uint8_t reg = 0, mask = 0, semantic = 0;
};

struct Shader {
    bool pixel = false;
    std::string name;                 // the .updb base name ("D_3ChkBox_Pass0_Default")
    std::vector<uint32_t> ucode;      // microcode dwords (control flow first)
    std::vector<Constant> constants;
    std::vector<Fetch> fetches;       // vertex shaders
    std::vector<Link> links;          // vertex: exports; pixel: inputs
    // Literal float constants (the compiler's "def"s): `literalCount` floats
    // from register `literalReg` (0..255 in the shader's own bank) on.
    uint16_t literalReg = 0;
    std::vector<float> literals;
};

struct Template {
    uint32_t key = 0;
    std::string name;
    int vs[kTechniques] = {-1, -1, -1, -1, -1};
    int ps[kTechniques] = {-1, -1, -1, -1, -1};
};

class Library {
public:
    // Throws std::runtime_error on data the disc never holds.
    void parse(const uint8_t* data, size_t size);
    bool empty() const { return templates_.empty(); }

    const Template* find(uint32_t key) const {
        auto it = byKey_.find(key);
        return it == byKey_.end() ? nullptr : &templates_[it->second];
    }
    const Shader& shader(int i) const { return shaders_[size_t(i)]; }
    size_t shaderCount() const { return shaders_.size(); }
    const std::vector<Template>& templates() const { return templates_; }

private:
    std::vector<Template> templates_;
    std::unordered_map<uint32_t, size_t> byKey_;
    std::vector<Shader> shaders_;
};

}  // namespace matlib
