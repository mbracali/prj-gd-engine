// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Builds without the Vulkan upscalers (Android): FSR 3.1 and DLSS 4 report
// themselves unavailable and the renderer keeps its own (FSR 1) path.
#include "render/vk_upscaler.h"

struct VkUpscaler::Impl {};

VkUpscaler::VkUpscaler() : impl_(std::make_unique<Impl>()) {}
VkUpscaler::~VkUpscaler() = default;

float VkUpscaler::gpuMs() const { return 0; }

bool VkUpscaler::available(AiUpscaler) {
    status_ = "not available on this platform";
    return false;
}

unsigned VkUpscaler::upscale(AiUpscaler which, const VkUpscaleFrame&) {
    available(which);
    return 0;
}
