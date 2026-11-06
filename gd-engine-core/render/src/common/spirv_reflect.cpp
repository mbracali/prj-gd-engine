// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// SPIR-V resource reflection (spirv_reflect.h): a walk over the module's
// instructions, keeping the decorations, types and variables it needs.
#include "common/spirv_reflect.h"

#include <unordered_map>
#include <vector>

#include "gd/core/render/device.h"

namespace gd::core::render {
namespace {

// The SPIR-V numbers used here (SPIR-V 1.0 specification, section 3).
constexpr uint32_t kMagic = 0x07230203u;
enum Op : uint32_t {
    OpEntryPoint = 15,
    OpTypeImage = 25,
    OpTypeSampler = 26,
    OpTypeSampledImage = 27,
    OpTypeArray = 28,
    OpTypeRuntimeArray = 29,
    OpTypeStruct = 30,
    OpTypePointer = 32,
    OpVariable = 59,
    OpDecorate = 71,
};
enum Decoration : uint32_t { BufferBlock = 3, BuiltIn = 11, Location = 30, Binding = 33, DescriptorSet = 34 };
enum StorageClass : uint32_t { UniformConstant = 0, Input = 1, Uniform = 2, Output = 3, PushConstant = 9, StorageBuffer = 12 };
enum ExecutionModel : uint32_t { Vertex = 0, Fragment = 4 };

struct Decorations {
    int set = -1, binding = -1, location = -1;
    bool builtIn = false, bufferBlock = false;
};

}  // namespace

bool reflectSpirv(const uint32_t* words, size_t bytes, SpirvStageInfo& out, std::string& error) {
    out = {};
    if (!words || bytes < 20 || bytes % 4 != 0 || words[0] != kMagic) {
        error = "not SPIR-V (no magic number, or not whole words)";
        return false;
    }
    const size_t count = bytes / 4;
    std::unordered_map<uint32_t, Decorations> deco;
    std::unordered_map<uint32_t, uint32_t> opOf;        // type id -> its opcode
    std::unordered_map<uint32_t, uint32_t> elementOf;   // array / pointer type -> element type
    std::unordered_map<uint32_t, uint32_t> imageDimOf;  // OpTypeImage -> Dim (1 = 2D, 3 = Cube)
    struct Var {
        uint32_t id, pointerType, storage;
    };
    std::vector<Var> vars;
    int entryPoints = 0;
    for (size_t i = 5; i < count;) {
        const uint32_t n = words[i] >> 16, op = words[i] & 0xffffu;
        if (n == 0 || i + n > count) {
            error = "truncated instruction at word " + std::to_string(i);
            return false;
        }
        const uint32_t* w = words + i;
        switch (op) {
        case OpEntryPoint:
            ++entryPoints;
            out.stage = w[1] == Vertex ? SpirvStage::Vertex : w[1] == Fragment ? SpirvStage::Fragment : SpirvStage::Unknown;
            break;
        case OpDecorate:
            if (n >= 3) {
                Decorations& d = deco[w[1]];
                if (w[2] == DescriptorSet && n >= 4) d.set = int(w[3]);
                else if (w[2] == Binding && n >= 4) d.binding = int(w[3]);
                else if (w[2] == Location && n >= 4) d.location = int(w[3]);
                else if (w[2] == BuiltIn) d.builtIn = true;
                else if (w[2] == BufferBlock) d.bufferBlock = true;
            }
            break;
        case OpTypeImage:
            if (n >= 4) opOf[w[1]] = op, imageDimOf[w[1]] = w[3];
            break;
        case OpTypeSampler:
        case OpTypeSampledImage:
        case OpTypeStruct:
            if (n >= 2) opOf[w[1]] = op;
            break;
        case OpTypeArray:
        case OpTypeRuntimeArray:
            if (n >= 3) opOf[w[1]] = op, elementOf[w[1]] = w[2];
            break;
        case OpTypePointer:
            if (n >= 4) opOf[w[1]] = op, elementOf[w[1]] = w[3];
            break;
        case OpVariable:
            if (n >= 4) vars.push_back({w[2], w[1], w[3]});
            break;
        default: break;
        }
        i += n;
    }
    if (entryPoints != 1 || out.stage == SpirvStage::Unknown) {
        error = "expected one vertex or fragment entry point, found " + std::to_string(entryPoints);
        return false;
    }
    const bool vs = out.stage == SpirvStage::Vertex;
    const int texSet = vs ? 0 : 2, uboSet = vs ? 1 : 3;
    const char* stage = vs ? "vertex" : "fragment";
    auto pointee = [&](uint32_t type) {
        auto it = elementOf.find(type);
        uint32_t t = it == elementOf.end() ? 0 : it->second;
        while (opOf.count(t) && (opOf[t] == OpTypeArray || opOf[t] == OpTypeRuntimeArray)) t = elementOf[t];
        return t;
    };
    for (const Var& v : vars) {
        const Decorations d = deco.count(v.id) ? deco[v.id] : Decorations{};
        if (v.storage == Input || v.storage == Output) {
            if (d.builtIn || d.location < 0) continue;  // built-ins (gl_Position...) and block members
            if (d.location >= 32) {
                error = std::string(stage) + ": location " + std::to_string(d.location) + " is beyond 31";
                return false;
            }
            (v.storage == Input ? out.inputMask : out.outputMask) |= 1u << d.location;
            continue;
        }
        if (v.storage == PushConstant) {
            error = std::string(stage) + ": push constants are not in the dialect (SDL_GPU has none): use GD_UNIFORMS";
            return false;
        }
        if (v.storage != UniformConstant && v.storage != Uniform && v.storage != StorageBuffer) continue;
        const uint32_t type = pointee(v.pointerType);
        const uint32_t typeOp = opOf.count(type) ? opOf[type] : 0;
        const bool ubo = v.storage == Uniform && typeOp == OpTypeStruct && !(deco.count(type) && deco[type].bufferBlock);
        const bool sampler = v.storage == UniformConstant && typeOp == OpTypeSampledImage;
        const std::string where = std::string(stage) + ": the resource at set " + std::to_string(d.set) + ", binding " +
                                  std::to_string(d.binding);
        if (!ubo && !sampler) {
            error = where + " is not a combined image sampler or a uniform block (storage buffers / images and separate "
                            "samplers are not in the dialect)";
            return false;
        }
        if (d.binding < 0 || d.set < 0) {
            error = std::string(stage) + ": a resource without a set / binding (declare it with GD_UNIFORMS / GD_SAMPLER*)";
            return false;
        }
        if (sampler && (d.set != texSet || d.binding >= kMaxTextureSlots)) {
            error = where + ": a sampler must be in set " + std::to_string(texSet) + ", binding 0.." +
                    std::to_string(kMaxTextureSlots - 1) + " (SDL_GPU's layout)";
            return false;
        }
        if (ubo && (d.set != uboSet || d.binding >= kMaxUniformSlots)) {
            error = where + ": a uniform block must be in set " + std::to_string(uboSet) + ", binding 0.." +
                    std::to_string(kMaxUniformSlots - 1) + " (SDL_GPU's layout)";
            return false;
        }
        uint32_t& mask = sampler ? out.samplerMask : out.uniformMask;
        if (mask & (1u << d.binding)) {
            error = where + " is declared twice";
            return false;
        }
        mask |= 1u << d.binding;
    }
    return true;
}

}  // namespace gd::core::render
