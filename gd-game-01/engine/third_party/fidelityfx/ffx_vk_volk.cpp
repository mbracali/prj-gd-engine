// The FidelityFX Vulkan backend with Vulkan loaded through volk (the port
// links no Vulkan loader; libvulkan / vulkan-1.dll is loaded at run time).
#include "volk.h"
#include "src/backends/vk/ffx_vk.cpp"

// The frame-interpolation swap chain is not built (FSR 3.1 upscaling only);
// ffxGetInterfaceVK still references this entry point.
FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*) { return FFX_ERROR_INVALID_ARGUMENT; }
