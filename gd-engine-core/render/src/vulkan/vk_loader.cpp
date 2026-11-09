// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The implementations of volk and VMA for the "vulkan" backend (unmodified
// upstream code, compiled here once; no warnings for this file:
// render/CMakeLists.txt).
#define VOLK_IMPLEMENTATION
#define VMA_IMPLEMENTATION
#include "vulkan/vk_api.h"
