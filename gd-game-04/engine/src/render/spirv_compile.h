// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// SPIR-V for gd-engine-core's sdl3 and vulkan backends, made at run time from
// gd04's GLSL (render/core_device.cpp). GH3's materials draw with the disc's
// own Xenos shader microcode, translated to GLSL when a material is loaded
// (render/xenos_glsl.h; the pairs a disc uses are known only then), so the
// build-time step gd01 uses (gdcore_spirv_shaders) cannot list them: those
// backends compile with glslang when a program is made, as gd02's
// (gd-engine-core/docs/render.md section 6c).
namespace gfx {

// Whether this build can (GD04_RUNTIME_SPIRV: glslang linked).
bool spirvCompilerAvailable();

// Compiles a program's two stages to SPIR-V 1.0 in SDL_GPU's resource
// layout: uniform block `blocks[i]` at binding i of set 1 (vertex) / 3
// (fragment), sampler `samplers[i]` at binding i of set 0 / 2, varyings
// matched by name. `dialect`: the source is in gd-engine-core's dialect
// (GD_UNIFORMS...) instead of gd04's plain GLSL. The vertex source calls
// gd_clip (the core's clip-space convention: z to 0..1).
bool compileSpirv(const std::string& name, const std::string& vertex, const std::string& fragment, const std::string& defines,
                  const std::vector<std::string>& blocks, const std::vector<std::string>& samplers, bool dialect,
                  std::vector<uint32_t>& vertexOut, std::vector<uint32_t>& fragmentOut, std::string& error);

}  // namespace gfx
