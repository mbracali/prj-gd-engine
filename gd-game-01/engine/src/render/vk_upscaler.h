// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <memory>
#include <string>

// [I] The temporal AI upscalers of Enhanced Settings > AI Upscaling, run in
// Vulkan next to the OpenGL renderer: AMD FidelityFX FSR 3.1 (any Vulkan GPU)
// and NVIDIA DLSS 4 super resolution (an RTX GPU, Linux builds). The scene's
// colour, depth and motion vectors are written into images shared through
// GL_EXT_memory_object / GL_EXT_semaphore (the Vulkan device is the GL
// context's own GPU, matched by device UUID); the upscaled frame comes back
// as a GL texture. Everything is optional: when Vulkan, the interop, the GPU
// or the SDK is missing, upscale() returns 0 and status() says why.
enum class AiUpscaler { Fsr31, Dlss4 };

struct VkUpscaleFrame {
    unsigned color = 0, depth = 0;      // the GL scene textures, sw x sh
    int sw = 0, sh = 0, ow = 0, oh = 0;  // render and output sizes
    std::array<float, 2> jitterNdc{};    // this frame's projection jitter (GL NDC)
    std::array<float, 16> viewProj{};    // column-major, without the jitter
    std::array<float, 16> prevViewProj{};
    bool hasCamera = false;
    bool reset = false;                  // a camera cut: drop the history
    float dtMs = 16.7f;
    int quality = 2;                     // 0 performance, 1 balanced, 2 quality
};

class VkUpscaler {
public:
    VkUpscaler();
    ~VkUpscaler();
    // The upscaled frame (ow x oh) as a GL texture, or 0 (see status()).
    unsigned upscale(AiUpscaler which, const VkUpscaleFrame& frame);
    // Whether `which` can run here (initialises Vulkan on first use).
    bool available(AiUpscaler which);
    // Why the last request fell back, or what is running ("" before any).
    const std::string& status() const { return status_; }
    // The upscaler's own GPU time per frame (a running average, 0 before it is known).
    float gpuMs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string status_;
};
