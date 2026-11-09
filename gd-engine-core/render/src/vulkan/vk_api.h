// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// The Vulkan API as the "vulkan" backend sees it (docs/render.md 4.3):
// volk (the loader is found at run time, through SDL's
// SDL_Vulkan_GetVkGetInstanceProcAddr: no Vulkan library at link time) and
// the Vulkan Memory Allocator. Both pinned in cmake/gdcore_deps.cmake.
//
// volk is compiled in the C++ namespace `volk` (VOLK_NAMESPACE, defined by
// render/CMakeLists.txt for this library): gd-game-01 links its own copy of
// volk (extern "C" symbols, for its upscalers), and the two never clash.
// Device functions are volk's global pointers, loaded for the one device
// (volkLoadDevice): the core makes one Vulkan device at a time.

#ifndef VOLK_NAMESPACE
#error "the vulkan backend is built with VOLK_NAMESPACE (render/CMakeLists.txt)"
#endif
#include <volk.h>

// VMA takes every Vulkan function from the two ProcAddr functions we give it.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>
