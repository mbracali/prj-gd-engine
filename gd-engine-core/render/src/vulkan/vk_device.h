// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>

#include "gd/core/render/device.h"

namespace gd::core::render {
// The Vulkan backend (vk_device.cpp); used by createDevice.
std::unique_ptr<Device> createVulkanDevice(const DeviceCreateInfo& info, std::string& error);
}  // namespace gd::core::render
