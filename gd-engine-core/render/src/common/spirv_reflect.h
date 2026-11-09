// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// What a SPIR-V module of the gd shader dialect binds (docs/render.md section
// 3.2): the resource counts SDL_CreateGPUShader asks for, and the descriptor
// set layout of the Vulkan backend. Read from the module's decorations, so it
// works for any SPIR-V, not only ours. Used by the build-time compiler
// (render/tools/shaderc: it refuses a shader that breaks the layout) and by
// the SDL3 GPU and Vulkan backends at createShader.

#include <cstddef>
#include <cstdint>
#include <string>

namespace gd::core::render {

enum class SpirvStage : uint8_t { Unknown, Vertex, Fragment };

struct SpirvStageInfo {
    SpirvStage stage = SpirvStage::Unknown;
    // Bit b: a combined image sampler (sampler2D, samplerCube, sampler2DShadow)
    // at binding b of the stage's texture set (vertex 0, fragment 2).
    uint32_t samplerMask = 0;
    // Bit b: a std140 uniform block at binding b of the stage's uniform set
    // (vertex 1, fragment 3).
    uint32_t uniformMask = 0;
    // Bit l: an input / output variable at location l (vertex inputs are the
    // vertex attributes; built-ins are not counted).
    uint32_t inputMask = 0, outputMask = 0;

    // SDL_GPUShaderCreateInfo's num_samplers / num_uniform_buffers: the
    // highest binding + 1 (SDL_GPU numbers a stage's bindings from 0; a slot
    // below the highest that the shader does not use still counts, and the
    // backend binds something to it).
    uint32_t numSamplers() const { return highest(samplerMask); }
    uint32_t numUniformBuffers() const { return highest(uniformMask); }

    static uint32_t highest(uint32_t m) {
        uint32_t n = 0;
        while (m >> n) ++n;
        return n;
    }
};

// Reads `bytes` of SPIR-V (whole 32-bit words, host byte order). False with
// `error` set when it is not SPIR-V, has no single vertex or fragment entry
// point, or binds something the dialect does not allow (another descriptor
// set than the stage's, storage buffers or images, push constants, separate
// samplers, a binding beyond kMaxTextureSlots / kMaxUniformSlots).
bool reflectSpirv(const uint32_t* words, size_t bytes, SpirvStageInfo& out, std::string& error);

}  // namespace gd::core::render
