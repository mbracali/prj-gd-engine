// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>

#include "data/matlib.h"

// Translates a GH3 vertex + pixel shader pair (Xenos microcode from the
// material library, data/matlib.h) into GLSL bodies for render/device.h's
// ProgramDesc. The microcode is run as written: control flow (exec, the
// predicate and bool-constant conditional execs), every ALU vector and
// scalar opcode the library uses, vertex and texture fetches, exports.
// Format reference: the public Xenos microcode layout (as documented by
// the Xenia emulator's ucode.h); decoding checked on the library itself
// (docs/formats.md "Material library").
//
// Interface of the generated programs:
// - uniform blocks: binding 0 "GdXG" vec4 cG[192] = float constants
//   c64..c255 (the engine's constants, the same in both stages: matrices,
//   time, the filter passes' c64..c99), binding 1 "GdXVM" vec4 cV[64] = the vertex shader's
//   c0..c63 (a material's constsA), binding 2 "GdXPM" vec4 cP[64] = the
//   pixel shader's c0..c63 (constsB); cP[63].x is the alpha test
//   reference (the render state; < 0: none), cP[63].y the bool constants
//   b0..b23 as a bit mask; binding 3 "GdXO" vec4 cO[kObjectRegs] = the
//   per-object registers (objectSlot: gh4's lights c160..c211, the world
//   matrices c216..c231, the light attenuations c248..c253), which cG
//   does not hold.
// - samplers "sT0".."sT15" in slots 0..15 (texture fetch constant = slot).
// - vertex attributes at attribLocation(usage, index).
// - the clip depth is converted from Direct3D's 0..w unless GD_CLIP_ZO is
//   defined.
namespace xenos {

int attribLocation(uint8_t usage, uint8_t index);  // -1: not bound

// gh4's per-object registers [C data: the constant tables of all 4,981
// shaders of Metallica's library]: g_ambientLightColor c160,
// g_dirLight0Dir / Color / SpecColor c161..c163, g_dirLight1..3Dir /
// SpecColor c164..c169, g_SHLight0..3 c172..c175, g_PointLight c176
// (4 x 4), g_SpotLight c192 (5 x 4), g_World / WorldView / WorldViewProj /
// WorldIT c216..c231, g_point / spotLightGroup0Attenuation* c248..c253.
// Their slot in the object block, -1 for every other register.
constexpr int kObjectRegs = 74;
constexpr int objectSlot(int reg) {
    return reg >= 160 && reg < 212 ? reg - 160 : reg >= 216 && reg < 232 ? reg - 164 : reg >= 248 && reg < 254 ? reg - 180 : -1;
}

struct Program {
    bool ok = false;
    std::string error;
    std::string vertex, fragment;
    uint16_t samplers = 0;  // texture fetch constants the pixel shader reads (bit per slot)
    uint16_t cubes = 0;     // of those, the cube maps
    uint16_t attribs = 0;   // attribute locations the vertex shader reads
};

Program translate(const matlib::Shader& vs, const matlib::Shader& ps);

// A readable disassembly (development aid, GD10_XENOS_DUMP).
std::string disassemble(const matlib::Shader& s);

}  // namespace xenos
