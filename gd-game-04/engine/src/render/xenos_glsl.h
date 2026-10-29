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
// - uniform blocks: binding 0 "GdXG" vec4 cG[128] = float constants
//   c128..c255 (GH3's engine constants, the same in both stages: matrices,
//   lights, time), binding 1 "GdXVM" vec4 cV[64] = the vertex shader's
//   c0..c63 (a material's constsA), binding 2 "GdXPM" vec4 cP[64] = the
//   pixel shader's c0..c63 (constsB); cP[63].x is the alpha test
//   reference (the render state; < 0: none), cP[63].y the bool constants
//   b0..b23 as a bit mask; binding 3 "GdXO" vec4 cO[28] = the per-object
//   registers c176..c191 (lights) and c216..c227 (world matrices), which
//   cG does not hold.
// - samplers "sT0".."sT15" in slots 0..15 (texture fetch constant = slot).
// - vertex attributes at attribLocation(usage, index).
// - the clip depth is converted from Direct3D's 0..w unless GD_CLIP_ZO is
//   defined.
namespace xenos {

int attribLocation(uint8_t usage, uint8_t index);  // -1: not bound

struct Program {
    bool ok = false;
    std::string error;
    std::string vertex, fragment;
    uint16_t samplers = 0;  // texture fetch constants the pixel shader reads (bit per slot)
    uint16_t cubes = 0;     // of those, the cube maps
    uint16_t attribs = 0;   // attribute locations the vertex shader reads
};

Program translate(const matlib::Shader& vs, const matlib::Shader& ps);

// A readable disassembly (development aid, GD04_XENOS_DUMP).
std::string disassemble(const matlib::Shader& s);

}  // namespace xenos
