// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/vk_upscaler.h"

#include "volk.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>

#if GD01_DLSS
#include "nvsdk_ngx_helpers_vk.h"
#include "nvsdk_ngx_vk.h"
#endif

#include "render/gl.h"
#include "platform/paths.h"

namespace {

// A zeroed Vulkan structure of type `t`.
template <typename T>
T vkStruct(VkStructureType t) {
    T v{};
    v.sType = t;
    return v;
}

// ------------------------------------------------------------------ GL interop entry points
struct GlInterop {
    PFNGLGETUNSIGNEDBYTEI_VEXTPROC getUnsignedBytei = nullptr;
    PFNGLCREATEMEMORYOBJECTSEXTPROC createMemoryObjects = nullptr;
    PFNGLDELETEMEMORYOBJECTSEXTPROC deleteMemoryObjects = nullptr;
    PFNGLMEMORYOBJECTPARAMETERIVEXTPROC memoryObjectParameteriv = nullptr;
    PFNGLTEXSTORAGEMEM2DEXTPROC texStorageMem2D = nullptr;
    PFNGLGENSEMAPHORESEXTPROC genSemaphores = nullptr;
    PFNGLDELETESEMAPHORESEXTPROC deleteSemaphores = nullptr;
    PFNGLSIGNALSEMAPHOREEXTPROC signalSemaphore = nullptr;
    PFNGLWAITSEMAPHOREEXTPROC waitSemaphore = nullptr;
#ifdef _WIN32
    PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC importMemory = nullptr;
    PFNGLIMPORTSEMAPHOREWIN32HANDLEEXTPROC importSemaphore = nullptr;
#else
    PFNGLIMPORTMEMORYFDEXTPROC importMemory = nullptr;
    PFNGLIMPORTSEMAPHOREFDEXTPROC importSemaphore = nullptr;
#endif
    PFNGLDRAWBUFFERSPROC drawBuffers = nullptr;
    void(APIENTRY* flush)() = nullptr;

    bool load() {
        auto get = [](const char* n) { return SDL_GL_GetProcAddress(n); };
#ifdef _WIN32
        const char* memExt = "GL_EXT_memory_object_win32";
        const char* semExt = "GL_EXT_semaphore_win32";
#else
        const char* memExt = "GL_EXT_memory_object_fd";
        const char* semExt = "GL_EXT_semaphore_fd";
#endif
        if (!SDL_GL_ExtensionSupported("GL_EXT_memory_object") || !SDL_GL_ExtensionSupported("GL_EXT_semaphore") ||
            !SDL_GL_ExtensionSupported(memExt) || !SDL_GL_ExtensionSupported(semExt))
            return false;
        getUnsignedBytei = reinterpret_cast<PFNGLGETUNSIGNEDBYTEI_VEXTPROC>(get("glGetUnsignedBytei_vEXT"));
        createMemoryObjects = reinterpret_cast<PFNGLCREATEMEMORYOBJECTSEXTPROC>(get("glCreateMemoryObjectsEXT"));
        deleteMemoryObjects = reinterpret_cast<PFNGLDELETEMEMORYOBJECTSEXTPROC>(get("glDeleteMemoryObjectsEXT"));
        memoryObjectParameteriv = reinterpret_cast<PFNGLMEMORYOBJECTPARAMETERIVEXTPROC>(get("glMemoryObjectParameterivEXT"));
        texStorageMem2D = reinterpret_cast<PFNGLTEXSTORAGEMEM2DEXTPROC>(get("glTexStorageMem2DEXT"));
        genSemaphores = reinterpret_cast<PFNGLGENSEMAPHORESEXTPROC>(get("glGenSemaphoresEXT"));
        deleteSemaphores = reinterpret_cast<PFNGLDELETESEMAPHORESEXTPROC>(get("glDeleteSemaphoresEXT"));
        signalSemaphore = reinterpret_cast<PFNGLSIGNALSEMAPHOREEXTPROC>(get("glSignalSemaphoreEXT"));
        waitSemaphore = reinterpret_cast<PFNGLWAITSEMAPHOREEXTPROC>(get("glWaitSemaphoreEXT"));
#ifdef _WIN32
        importMemory = reinterpret_cast<PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC>(get("glImportMemoryWin32HandleEXT"));
        importSemaphore = reinterpret_cast<PFNGLIMPORTSEMAPHOREWIN32HANDLEEXTPROC>(get("glImportSemaphoreWin32HandleEXT"));
#else
        importMemory = reinterpret_cast<PFNGLIMPORTMEMORYFDEXTPROC>(get("glImportMemoryFdEXT"));
        importSemaphore = reinterpret_cast<PFNGLIMPORTSEMAPHOREFDEXTPROC>(get("glImportSemaphoreFdEXT"));
#endif
        drawBuffers = reinterpret_cast<PFNGLDRAWBUFFERSPROC>(get("glDrawBuffers"));
        flush = reinterpret_cast<void(APIENTRY*)()>(get("glFlush"));
        return flush && getUnsignedBytei && createMemoryObjects && deleteMemoryObjects && memoryObjectParameteriv && texStorageMem2D &&
               genSemaphores && deleteSemaphores && signalSemaphore && waitSemaphore && importMemory && importSemaphore && drawBuffers;
    }
};

#ifdef _WIN32
constexpr VkExternalMemoryHandleTypeFlagBits kMemHandle = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
constexpr VkExternalSemaphoreHandleTypeFlagBits kSemHandle = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
const char* const kInteropDeviceExts[] = {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
#else
constexpr VkExternalMemoryHandleTypeFlagBits kMemHandle = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
constexpr VkExternalSemaphoreHandleTypeFlagBits kSemHandle = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
const char* const kInteropDeviceExts[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
#endif

// The scene's colour, depth and motion vectors into the shared images, in one
// pass: motion vectors point from this frame's unjittered position to the
// previous frame's (UV), from the depth buffer and the two cameras.
const char* kInputVertex = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char* kInputFragment = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform mat4 uPrevViewProj;
uniform vec2 uJitter;   // NDC
uniform int uHasCamera;
layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oDepth;
layout(location = 2) out vec4 oMotion;
void main() {
    oColor = vec4(texture(uColor, vUv).rgb, 1.0);
    float d = texture(uDepth, vUv).r;
    oDepth = vec4(d, 0.0, 0.0, 0.0);
    vec2 mv = vec2(0.0);
    if (uHasCamera == 1) {
        vec2 ndc = vUv * 2.0 - 1.0 - uJitter;
        vec4 w = uInvViewProj * vec4(ndc, d * 2.0 - 1.0, 1.0);
        w /= w.w;
        vec4 p = uPrevViewProj * w;
        if (p.w > 1e-5) mv = (p.xy / p.w * 0.5 + 0.5) - (ndc * 0.5 + 0.5);
    }
    oMotion = vec4(mv, 0.0, 0.0);
}
)";

// Column-major 4x4 inverse.
std::array<float, 16> inverse(const std::array<float, 16>& m) {
    std::array<float, 16> inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f) return {};
    for (float& v : inv) v /= det;
    return inv;
}

// Near, far and vertical field of view of P * V (V rigid, P a GL perspective):
// row 1 of VP is f * V's up row; rows 2 and 3 are A * fwd + B and -fwd.
void cameraOf(const std::array<float, 16>& m, float& nearZ, float& farZ, float& fovY) {
    auto row = [&](int r) { return std::array<float, 4>{m[size_t(r)], m[size_t(4 + r)], m[size_t(8 + r)], m[size_t(12 + r)]}; };
    std::array<float, 4> r1 = row(1), r2 = row(2), r3 = row(3);
    float f = std::sqrt(r1[0] * r1[0] + r1[1] * r1[1] + r1[2] * r1[2]);
    float A = -(r2[0] * r3[0] + r2[1] * r3[1] + r2[2] * r3[2]);
    float B = r2[3] + A * r3[3];
    nearZ = 5, farZ = 5000, fovY = 0.8f;
    if (f > 1e-4f) fovY = 2.0f * std::atan(1.0f / f);
    if (std::fabs(A - 1) > 1e-6f && std::fabs(A + 1) > 1e-6f) {
        float n = B / (A - 1), fr = B / (A + 1);
        if (n > 0 && fr > n) nearZ = n, farZ = fr;
    }
}

}  // namespace

// ------------------------------------------------------------------ the Vulkan side
struct VkUpscaler::Impl {
    GlInterop gl;
    bool tried = false, ready = false, nvidia = false;
    bool onIntegrated = false;  // a hybrid laptop running the game on its integrated GPU
    std::string why;  // why Vulkan is not ready
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    // Frames in flight: the CPU only waits for the upscale of two frames ago,
    // never for the one just submitted (the GL side orders the shared images).
    static constexpr int kFrames = 2;
    VkCommandBuffer cmds[kFrames] = {};
    VkFence fences[kFrames] = {};
    int slot = 0;
    // The upscaler's GPU time: two timestamps per frame slot, read when the slot comes back.
    VkQueryPool timestamps = VK_NULL_HANDLE;
    bool timestampsWritten[kFrames] = {};
    float timestampPeriod = 0, gpuMs = 0;
    VkSemaphore glReady = VK_NULL_HANDLE, vkDone = VK_NULL_HANDLE;
    unsigned glReadySem = 0, vkDoneSem = 0;

    struct Shared {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImageCreateInfo info{};
        unsigned glMemory = 0, glTexture = 0;
        int w = 0, h = 0;
    };
    Shared color, depth, motion, output;
    unsigned fbo = 0, program = 0, vao = 0;
    int inW = 0, inH = 0, outW = 0, outH = 0;

    // FSR 3.1
    std::vector<unsigned char> ffxScratch;
    FfxInterface ffxInterface{};
    FfxFsr3UpscalerContext fsr{};
    bool fsrReady = false;
    // The upscaler's shared resources (dilated depth / motion, previous depth),
    // which the app owns when FSR 3.1 runs without frame generation.
    FfxUInt32 sharedContext = 0;
    bool sharedContextReady = false;
    FfxResourceInternal sharedRes[3]{};
    int fsrW = 0, fsrH = 0, fsrOW = 0, fsrOH = 0;

    // DLSS
    bool ngxReady = false, dlssAvailable = false;
    std::string dlssWhy;
#if GD01_DLSS
    NVSDK_NGX_Parameter* ngxParams = nullptr;
    NVSDK_NGX_Handle* dlss = nullptr;
    int dlssW = 0, dlssH = 0, dlssOW = 0, dlssOH = 0, dlssQuality = -1;
    std::wstring ngxDataPath, ngxModulePath;
#endif
    bool firstFrame = true;

    ~Impl() { shutdown(); }

    bool init();
    void shutdown();
    bool createShared(Shared& s, int w, int h, VkFormat fmt, GLenum glFormat, VkImageUsageFlags usage);
    void destroyShared(Shared& s);
    bool ensureTargets(int sw, int sh, int ow, int oh);
    bool glInputs(const VkUpscaleFrame& f);
    void barrier(VkCommandBuffer c, const Shared& s, bool acquire);
    bool runFsr(VkCommandBuffer c, const VkUpscaleFrame& f);
    void destroyFsr();
    bool runDlss(VkCommandBuffer c, const VkUpscaleFrame& f);
    void initNgx();
};

bool VkUpscaler::Impl::init() {
    if (tried) return ready;
    tried = true;
    if (!gl.load()) return why = "the OpenGL driver has no Vulkan interop (GL_EXT_memory_object / GL_EXT_semaphore)", false;
    if (volkInitialize() != VK_SUCCESS) return why = "Vulkan is not installed", false;
    // The GL context's GPU.
    uint8_t glUuid[GL_UUID_SIZE_EXT] = {};
    gl.getUnsignedBytei(GL_DEVICE_UUID_EXT, 0, glUuid);

    std::vector<const char*> instExts;
    std::vector<const char*> ngxDevExts;
#if GD01_DLSS
    {
        unsigned ic = 0, dc = 0;
        const char** ie = nullptr;
        const char** de = nullptr;
        if (NVSDK_NGX_VULKAN_RequiredExtensions(&ic, &ie, &dc, &de) == NVSDK_NGX_Result_Success) {
            uint32_t n = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> have(n);
            vkEnumerateInstanceExtensionProperties(nullptr, &n, have.data());
            for (unsigned k = 0; k < ic; ++k)
                for (const auto& h : have)
                    if (std::strcmp(h.extensionName, ie[k]) == 0) instExts.push_back(ie[k]);
            for (unsigned k = 0; k < dc; ++k) ngxDevExts.push_back(de[k]);
        }
    }
#endif
    auto app = vkStruct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = "gd01";
    app.pEngineName = "gd01";
    app.apiVersion = VK_API_VERSION_1_2;
    auto ici = vkStruct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = uint32_t(instExts.size());
    ici.ppEnabledExtensionNames = instExts.data();
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) return why = "no Vulkan 1.2 driver", false;
    volkLoadInstanceOnly(instance);

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    VkPhysicalDeviceProperties props{};
    bool nvidiaPresent = false;
    for (VkPhysicalDevice d : devices) {
        auto id = vkStruct<VkPhysicalDeviceIDProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
        auto p2 = vkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        p2.pNext = &id;
        vkGetPhysicalDeviceProperties2(d, &p2);
        nvidiaPresent = nvidiaPresent || p2.properties.vendorID == 0x10DE;
        if (std::memcmp(id.deviceUUID, glUuid, VK_UUID_SIZE) == 0 && p2.properties.apiVersion >= VK_API_VERSION_1_2) {
            phys = d;
            props = p2.properties;
        }
    }
    if (!phys) return why = "no Vulkan 1.2 device matches the OpenGL GPU", false;
    nvidia = props.vendorID == 0x10DE;
    onIntegrated = !nvidia && nvidiaPresent && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

    // A queue with compute (the graphics family when it has it).
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf.data());
    bool found = false;
    for (uint32_t i = 0; i < qn && !found; ++i)
        if ((qf[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) family = i, found = true;
    if (!found) return why = "the GPU has no graphics + compute queue", false;

    // Device extensions: the interop ones (required), NGX's (when available).
    uint32_t en = 0;
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> have(en);
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &en, have.data());
    auto has = [&](const char* e) {
        for (const auto& h : have)
            if (std::strcmp(h.extensionName, e) == 0) return true;
        return false;
    };
    std::vector<const char*> devExts;
    for (const char* e : kInteropDeviceExts) {
        if (!has(e)) return why = std::string("the Vulkan driver lacks ") + e, false;
        devExts.push_back(e);
    }
    bool ngxExtsOk = nvidia;
    for (const char* e : ngxDevExts) {
        if (!has(e)) {
            ngxExtsOk = false;
            continue;
        }
        if (std::find_if(devExts.begin(), devExts.end(), [&](const char* x) { return std::strcmp(x, e) == 0; }) == devExts.end()) devExts.push_back(e);
    }
    // FidelityFX picks optional features from the extension list it is given.
    for (const char* e : {VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
                          VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME})
        if (has(e) && std::find_if(devExts.begin(), devExts.end(), [&](const char* x) { return std::strcmp(x, e) == 0; }) == devExts.end())
            devExts.push_back(e);

    // Every feature the device has (the upscalers' shaders need some of the 1.1 / 1.2 ones).
    auto f12 = vkStruct<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    auto f11 = vkStruct<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    f11.pNext = &f12;
    auto features = vkStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    features.pNext = &f11;
    vkGetPhysicalDeviceFeatures2(phys, &features);

    float priority = 1;
    auto qci = vkStruct<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    auto dci = vkStruct<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    dci.pNext = &features;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = uint32_t(devExts.size());
    dci.ppEnabledExtensionNames = devExts.data();
    if (vkCreateDevice(phys, &dci, nullptr, &device) != VK_SUCCESS) return why = "could not create the Vulkan device", false;
    volkLoadDevice(device);
    vkGetDeviceQueue(device, family, 0, &queue);

    auto pci = vkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family;
    vkCreateCommandPool(device, &pci, nullptr, &pool);
    auto cai = vkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = kFrames;
    vkAllocateCommandBuffers(device, &cai, cmds);
    auto fci = vkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (VkFence& f : fences) vkCreateFence(device, &fci, nullptr, &f);
    if (qf[family].timestampValidBits && props.limits.timestampPeriod > 0) {
        auto qpi = vkStruct<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
        qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpi.queryCount = 2 * kFrames;
        if (vkCreateQueryPool(device, &qpi, nullptr, &timestamps) == VK_SUCCESS) timestampPeriod = props.limits.timestampPeriod;
    }

    // The two semaphores: GL -> Vulkan (inputs written), Vulkan -> GL (output written).
    for (int k = 0; k < 2; ++k) {
        auto esi = vkStruct<VkExportSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO);
        esi.handleTypes = kSemHandle;
        auto sci = vkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        sci.pNext = &esi;
        VkSemaphore& s = k == 0 ? glReady : vkDone;
        unsigned& g = k == 0 ? glReadySem : vkDoneSem;
        if (vkCreateSemaphore(device, &sci, nullptr, &s) != VK_SUCCESS) return why = "could not create a shared semaphore", false;
        gl.genSemaphores(1, &g);
#ifdef _WIN32
        auto hi = vkStruct<VkSemaphoreGetWin32HandleInfoKHR>(VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR);
        hi.semaphore = s;
        hi.handleType = kSemHandle;
        HANDLE h = nullptr;
        if (vkGetSemaphoreWin32HandleKHR(device, &hi, &h) != VK_SUCCESS) return why = "could not share a semaphore", false;
        gl.importSemaphore(g, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, h);
#else
        auto fi = vkStruct<VkSemaphoreGetFdInfoKHR>(VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR);
        fi.semaphore = s;
        fi.handleType = kSemHandle;
        int fd = -1;
        if (vkGetSemaphoreFdKHR(device, &fi, &fd) != VK_SUCCESS) return why = "could not share a semaphore", false;
        gl.importSemaphore(g, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);
#endif
    }

    // FidelityFX backend (one FSR 3.1 context).
    VkDeviceContext ctx{device, phys, vkGetDeviceProcAddr};
    const size_t contexts = FFX_FSR3UPSCALER_CONTEXT_COUNT + 1;  // + the shared resources'
    size_t scratch = ffxGetScratchMemorySizeVK(phys, contexts);
    ffxScratch.assign(scratch, 0);
    if (ffxGetInterfaceVK(&ffxInterface, ffxGetDeviceVK(&ctx), ffxScratch.data(), scratch, contexts) != FFX_OK)
        return why = "the FidelityFX Vulkan backend did not start", false;

    if (ngxExtsOk) initNgx();
    else if (nvidia) dlssWhy = "the Vulkan driver lacks the extensions DLSS needs";
    else if (nvidiaPresent) dlssWhy = "restart the game to run it on the NVIDIA GPU (DLSS)";  // a hybrid laptop on its integrated GPU
    else dlssWhy = "DLSS needs an NVIDIA RTX GPU (the game runs on " + std::string(props.deviceName) + ")";
    SDL_Log("ai upscaling: Vulkan on %s", props.deviceName);
    ready = true;
    return true;
}

void VkUpscaler::Impl::initNgx() {
#if GD01_DLSS
    const char* base = SDL_GetBasePath();
    std::string dir = base ? base : "./";
    ngxModulePath.assign(dir.begin(), dir.end());
    std::string data = prefDir().empty() ? dir : prefDir();
    ngxDataPath.assign(data.begin(), data.end());
    const wchar_t* paths[] = {ngxModulePath.c_str()};
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;
    info.LoggingInfo.MinimumLoggingLevel = SDL_getenv("GD01_NGXLOG") ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE : NVSDK_NGX_LOGGING_LEVEL_OFF;
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID("8c4f3b1e-6a2d-4e57-9b0a-1f7d2c5e9a41", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0",
                                                               ngxDataPath.c_str(), instance, phys, device, vkGetInstanceProcAddr,
                                                               vkGetDeviceProcAddr, &info, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(r)) {
        dlssWhy = "the NVIDIA driver could not start NGX (DLSS)";
        return;
    }
    ngxReady = true;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetCapabilityParameters(&ngxParams)) || !ngxParams) {
        dlssWhy = "the NVIDIA driver has no DLSS parameters";
        return;
    }
    int available = 0, needsDriver = 0;
    NVSDK_NGX_Parameter_GetI(ngxParams, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    NVSDK_NGX_Parameter_GetI(ngxParams, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
    dlssAvailable = available != 0;
    if (!dlssAvailable)
        dlssWhy = needsDriver ? "DLSS needs a newer NVIDIA driver" : "DLSS is not available on this GPU (it needs an RTX card)";
#else
    dlssWhy = "DLSS is not in this build (on Windows it needs the MSVC build)";
#endif
}

void VkUpscaler::Impl::destroyShared(Shared& s) {
    if (s.glTexture) gl::glDeleteTextures(1, &s.glTexture);
    if (s.glMemory) gl.deleteMemoryObjects(1, &s.glMemory);
    if (s.view) vkDestroyImageView(device, s.view, nullptr);
    if (s.image) vkDestroyImage(device, s.image, nullptr);
    if (s.memory) vkFreeMemory(device, s.memory, nullptr);
    s = Shared{};
}

void VkUpscaler::Impl::shutdown() {
    if (!device) {
        if (instance) vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
        return;
    }
    vkDeviceWaitIdle(device);
    destroyFsr();
    if (sharedContextReady) ffxInterface.fpDestroyBackendContext(&ffxInterface, sharedContext);
    sharedContextReady = false;
#if GD01_DLSS
    if (dlss) NVSDK_NGX_VULKAN_ReleaseFeature(dlss);
    dlss = nullptr;
    if (ngxParams) NVSDK_NGX_VULKAN_DestroyParameters(ngxParams);
    ngxParams = nullptr;
    if (ngxReady) NVSDK_NGX_VULKAN_Shutdown1(device);
    ngxReady = false;
#endif
    // The GL objects go with the GL context at exit; Vulkan's are freed here.
    for (Shared* s : {&color, &depth, &motion, &output}) {
        if (s->view) vkDestroyImageView(device, s->view, nullptr);
        if (s->image) vkDestroyImage(device, s->image, nullptr);
        if (s->memory) vkFreeMemory(device, s->memory, nullptr);
    }
    if (glReady) vkDestroySemaphore(device, glReady, nullptr);
    if (vkDone) vkDestroySemaphore(device, vkDone, nullptr);
    for (VkFence f : fences)
        if (f) vkDestroyFence(device, f, nullptr);
    if (timestamps) vkDestroyQueryPool(device, timestamps, nullptr);
    if (pool) vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE, instance = VK_NULL_HANDLE;
}

bool VkUpscaler::Impl::createShared(Shared& s, int w, int h, VkFormat fmt, GLenum glFormat, VkImageUsageFlags usage) {
    auto ext = vkStruct<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    ext.handleTypes = kMemHandle;
    VkImageCreateInfo& ci = s.info;
    ci = vkStruct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ci.pNext = &ext;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {uint32_t(w), uint32_t(h), 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ci, nullptr, &s.image) != VK_SUCCESS) return false;
    ci.pNext = nullptr;  // the stack struct is gone after this call
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, s.image, &req);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount && type == UINT32_MAX; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) type = i;
    if (type == UINT32_MAX) return false;
    auto dedicated = vkStruct<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    dedicated.image = s.image;
    auto exportInfo = vkStruct<VkExportMemoryAllocateInfo>(VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO);
    exportInfo.pNext = &dedicated;
    exportInfo.handleTypes = kMemHandle;
    auto ai = vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    ai.pNext = &exportInfo;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(device, &ai, nullptr, &s.memory) != VK_SUCCESS) return false;
    vkBindImageMemory(device, s.image, s.memory, 0);
    auto vi = vkStruct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = s.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &vi, nullptr, &s.view) != VK_SUCCESS) return false;

    gl.createMemoryObjects(1, &s.glMemory);
    const int dedicatedFlag = GL_TRUE;
    gl.memoryObjectParameteriv(s.glMemory, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicatedFlag);
#ifdef _WIN32
    auto hi = vkStruct<VkMemoryGetWin32HandleInfoKHR>(VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR);
    hi.memory = s.memory;
    hi.handleType = kMemHandle;
    HANDLE handle = nullptr;
    if (vkGetMemoryWin32HandleKHR(device, &hi, &handle) != VK_SUCCESS) return false;
    gl.importMemory(s.glMemory, req.size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle);
#else
    auto fi = vkStruct<VkMemoryGetFdInfoKHR>(VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);
    fi.memory = s.memory;
    fi.handleType = kMemHandle;
    int fd = -1;
    if (vkGetMemoryFdKHR(device, &fi, &fd) != VK_SUCCESS) return false;
    gl.importMemory(s.glMemory, req.size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);  // GL owns the fd now
#endif
    gl::glGenTextures(1, &s.glTexture);
    gl::glBindTexture(GL_TEXTURE_2D, s.glTexture);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
    gl.texStorageMem2D(GL_TEXTURE_2D, 1, glFormat, w, h, s.glMemory, 0);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    s.w = w, s.h = h;
    return gl::glGetError() == GL_NO_ERROR;
}

void VkUpscaler::Impl::barrier(VkCommandBuffer c, const Shared& s, bool acquire) {
    auto b = vkStruct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    b.srcAccessMask = acquire ? 0 : VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = acquire ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0;
    b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = acquire ? VK_QUEUE_FAMILY_EXTERNAL : family;
    b.dstQueueFamilyIndex = acquire ? family : VK_QUEUE_FAMILY_EXTERNAL;
    b.image = s.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(c, acquire ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         acquire ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// The shared images at these sizes (new ones start in GENERAL, owned by GL).
bool VkUpscaler::Impl::ensureTargets(int sw, int sh, int ow, int oh) {
    if (sw == inW && sh == inH && ow == outW && oh == outH && color.image) return true;
    vkWaitForFences(device, kFrames, fences, VK_TRUE, UINT64_MAX);
    VkCommandBuffer cmd = cmds[slot];
    VkFence fence = fences[slot];
    for (Shared* s : {&color, &depth, &motion, &output}) destroyShared(*s);
    const VkImageUsageFlags in = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const VkImageUsageFlags out = in | VK_IMAGE_USAGE_STORAGE_BIT;
    if (!createShared(color, sw, sh, VK_FORMAT_R8G8B8A8_UNORM, GL_RGBA8, in) ||
        !createShared(depth, sw, sh, VK_FORMAT_R32_SFLOAT, GL_R32F, in) ||
        !createShared(motion, sw, sh, VK_FORMAT_R16G16_SFLOAT, GL_RG16F, in) ||
        !createShared(output, ow, oh, VK_FORMAT_R16G16B16A16_SFLOAT, GL_RGBA16F, out)) {
        for (Shared* s : {&color, &depth, &motion, &output}) destroyShared(*s);
        inW = inH = outW = outH = 0;
        return false;
    }
    // UNDEFINED -> GENERAL, then handed to GL (the external queue).
    vkResetFences(device, 1, &fence);
    vkResetCommandBuffer(cmd, 0);
    auto bi = vkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    for (Shared* s : {&color, &depth, &motion, &output}) {
        auto b = vkStruct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = family;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        b.image = s->image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    vkEndCommandBuffer(cmd);
    auto si = vkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    // The GL framebuffer writing the three inputs.
    if (!fbo) gl::glGenFramebuffers(1, &fbo);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.glTexture, 0);
    gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, depth.glTexture, 0);
    gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, motion.glTexture, 0);
    const GLenum bufs[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    gl.drawBuffers(3, bufs);
    const bool complete = gl::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!complete) return false;
    inW = sw, inH = sh, outW = ow, outH = oh;
    return true;
}

bool VkUpscaler::Impl::glInputs(const VkUpscaleFrame& f) {
    if (!program) {
        auto compile = [](GLenum type, const char* src) {
            unsigned s = gl::glCreateShader(type);
            gl::glShaderSource(s, 1, &src, nullptr);
            gl::glCompileShader(s);
            int ok = 0;
            gl::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024];
                gl::glGetShaderInfoLog(s, sizeof log, nullptr, log);
                SDL_Log("ai upscaling inputs: %s", log);
            }
            return s;
        };
        program = gl::glCreateProgram();
        gl::glAttachShader(program, compile(GL_VERTEX_SHADER, kInputVertex));
        gl::glAttachShader(program, compile(GL_FRAGMENT_SHADER, kInputFragment));
        gl::glLinkProgram(program);
        int ok = 0;
        gl::glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) return false;
        gl::glGenVertexArrays(1, &vao);
    }
    gl::glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::glViewport(0, 0, inW, inH);
    gl::glDisable(GL_BLEND);
    gl::glDisable(GL_DEPTH_TEST);
    gl::glDisable(GL_SCISSOR_TEST);
    gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::glUseProgram(program);
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glBindTexture(GL_TEXTURE_2D, f.color);
    gl::glActiveTexture(GL_TEXTURE1);
    gl::glBindTexture(GL_TEXTURE_2D, f.depth);
    gl::glUniform1i(gl::glGetUniformLocation(program, "uColor"), 0);
    gl::glUniform1i(gl::glGetUniformLocation(program, "uDepth"), 1);
    std::array<float, 16> inv = f.hasCamera ? inverse(f.viewProj) : std::array<float, 16>{};
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(program, "uInvViewProj"), 1, GL_FALSE, inv.data());
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(program, "uPrevViewProj"), 1, GL_FALSE, f.prevViewProj.data());
    gl::glUniform2f(gl::glGetUniformLocation(program, "uJitter"), f.jitterNdc[0], f.jitterNdc[1]);
    gl::glUniform1i(gl::glGetUniformLocation(program, "uHasCamera"), f.hasCamera && !f.reset ? 1 : 0);
    gl::glBindVertexArray(vao);
    gl::glDrawArrays(GL_TRIANGLES, 0, 3);
    gl::glActiveTexture(GL_TEXTURE0);
    return true;
}

// FSR 3.1. Coordinates: the shared images keep GL's row order, so Vulkan
// sees them upside down; as all of them (and the output) are, UV motion and
// pixel jitter keep GL's signs.
void VkUpscaler::Impl::destroyFsr() {
    if (!fsrReady) return;
    vkDeviceWaitIdle(device);
    for (FfxResourceInternal& r : sharedRes) ffxInterface.fpDestroyResource(&ffxInterface, r, sharedContext);
    ffxFsr3UpscalerContextDestroy(&fsr);
    fsrReady = false;
}

bool VkUpscaler::Impl::runFsr(VkCommandBuffer c, const VkUpscaleFrame& f) {
    if (!fsrReady || fsrW != f.sw || fsrH != f.sh || fsrOW != f.ow || fsrOH != f.oh) {
        destroyFsr();
        if (!sharedContextReady) {
            if (ffxInterface.fpCreateBackendContext(&ffxInterface, FFX_EFFECT_SHAREDRESOURCES, nullptr, &sharedContext) != FFX_OK) return false;
            sharedContextReady = true;
        }
        FfxFsr3UpscalerContextDescription d{};
        d.flags = 0;  // LDR colour, render-resolution motion vectors, standard depth (tools/ffx_gen_shaders.py)
        d.maxRenderSize = {uint32_t(f.sw), uint32_t(f.sh)};
        d.maxUpscaleSize = {uint32_t(f.ow), uint32_t(f.oh)};
        d.backendInterface = ffxInterface;
        if (ffxFsr3UpscalerContextCreate(&fsr, &d) != FFX_OK) return false;
        fsrReady = true;
        FfxFsr3UpscalerSharedResourceDescriptions shared{};
        ffxFsr3UpscalerGetSharedResourceDescriptions(&fsr, &shared);
        const FfxCreateResourceDescription* descs[3] = {&shared.dilatedDepth, &shared.dilatedMotionVectors, &shared.reconstructedPrevNearestDepth};
        for (int k = 0; k < 3; ++k)
            if (ffxInterface.fpCreateResource(&ffxInterface, descs[k], sharedContext, &sharedRes[k]) != FFX_OK) {
                ffxFsr3UpscalerContextDestroy(&fsr);
                fsrReady = false;
                return false;
            }
        fsrW = f.sw, fsrH = f.sh, fsrOW = f.ow, fsrOH = f.oh;
    }
    auto res = [&](const Shared& s, const wchar_t* name, bool uav) {
        return ffxGetResourceVK(reinterpret_cast<void*>(s.image),
                                ffxGetImageResourceDescriptionVK(s.image, s.info, uav ? FFX_RESOURCE_USAGE_UAV : FFX_RESOURCE_USAGE_READ_ONLY),
                                name, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    };
    FfxFsr3UpscalerDispatchDescription d{};
    d.commandList = ffxGetCommandListVK(c);
    d.color = res(color, L"FSR3_InputColor", false);
    d.depth = res(depth, L"FSR3_InputDepth", false);
    d.motionVectors = res(motion, L"FSR3_InputMotionVectors", false);
    d.output = res(output, L"FSR3_Output", true);
    d.dilatedDepth = ffxInterface.fpGetResource(&ffxInterface, sharedRes[0]);
    d.dilatedMotionVectors = ffxInterface.fpGetResource(&ffxInterface, sharedRes[1]);
    d.reconstructedPrevNearestDepth = ffxInterface.fpGetResource(&ffxInterface, sharedRes[2]);
    d.jitterOffset = {f.jitterNdc[0] * 0.5f * float(f.sw), f.jitterNdc[1] * 0.5f * float(f.sh)};
    d.motionVectorScale = {float(f.sw), float(f.sh)};
    d.renderSize = {uint32_t(f.sw), uint32_t(f.sh)};
    d.upscaleSize = {uint32_t(f.ow), uint32_t(f.oh)};
    d.enableSharpening = true;
    d.sharpness = 0.25f;
    d.frameTimeDelta = std::clamp(f.dtMs, 1.0f, 100.0f);
    d.preExposure = 1;
    d.reset = f.reset;
    cameraOf(f.viewProj, d.cameraNear, d.cameraFar, d.cameraFovAngleVertical);
    d.viewSpaceToMetersFactor = 0.025f;  // venue units are about an inch
    return ffxFsr3UpscalerContextDispatch(&fsr, &d) == FFX_OK;
}

bool VkUpscaler::Impl::runDlss(VkCommandBuffer c, const VkUpscaleFrame& f) {
#if GD01_DLSS
    if (!dlssAvailable) return false;
    if (!dlss || dlssW != f.sw || dlssH != f.sh || dlssOW != f.ow || dlssOH != f.oh || dlssQuality != f.quality) {
        if (dlss) {
            vkDeviceWaitIdle(device);
            NVSDK_NGX_VULKAN_ReleaseFeature(dlss);
            dlss = nullptr;
        }
        static const NVSDK_NGX_PerfQuality_Value kQuality[3] = {NVSDK_NGX_PerfQuality_Value_MaxPerf, NVSDK_NGX_PerfQuality_Value_Balanced,
                                                                 NVSDK_NGX_PerfQuality_Value_MaxQuality};
        // DLSS 4: the transformer model (preset K) for every mode.
        NVSDK_NGX_Parameter_SetUI(ngxParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, NVSDK_NGX_DLSS_Hint_Render_Preset_K);
        NVSDK_NGX_Parameter_SetUI(ngxParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_DLSS_Hint_Render_Preset_K);
        NVSDK_NGX_Parameter_SetUI(ngxParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, NVSDK_NGX_DLSS_Hint_Render_Preset_K);
        NVSDK_NGX_DLSS_Create_Params cp{};
        cp.Feature.InWidth = unsigned(f.sw);
        cp.Feature.InHeight = unsigned(f.sh);
        cp.Feature.InTargetWidth = unsigned(f.ow);
        cp.Feature.InTargetHeight = unsigned(f.oh);
        cp.Feature.InPerfQualityValue = kQuality[std::clamp(f.quality, 0, 2)];
        cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        if (NVSDK_NGX_FAILED(NGX_VULKAN_CREATE_DLSS_EXT1(device, c, 1, 1, &dlss, ngxParams, &cp)) || !dlss) {
            dlss = nullptr;
            dlssWhy = "DLSS could not start at this resolution";
            return false;
        }
        dlssW = f.sw, dlssH = f.sh, dlssOW = f.ow, dlssOH = f.oh, dlssQuality = f.quality;
    }
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    NVSDK_NGX_Resource_VK inColor = NVSDK_NGX_Create_ImageView_Resource_VK(color.view, color.image, range, color.info.format, unsigned(color.w), unsigned(color.h), false);
    NVSDK_NGX_Resource_VK inDepth = NVSDK_NGX_Create_ImageView_Resource_VK(depth.view, depth.image, range, depth.info.format, unsigned(depth.w), unsigned(depth.h), false);
    NVSDK_NGX_Resource_VK inMotion = NVSDK_NGX_Create_ImageView_Resource_VK(motion.view, motion.image, range, motion.info.format, unsigned(motion.w), unsigned(motion.h), false);
    NVSDK_NGX_Resource_VK out = NVSDK_NGX_Create_ImageView_Resource_VK(output.view, output.image, range, output.info.format, unsigned(output.w), unsigned(output.h), true);
    NVSDK_NGX_VK_DLSS_Eval_Params ep{};
    ep.Feature.pInColor = &inColor;
    ep.Feature.pInOutput = &out;
    ep.pInDepth = &inDepth;
    ep.pInMotionVectors = &inMotion;
    // The projection's shift in render pixels, as FSR takes it (the sign was
    // checked by sharpness on still parts of the frame: the opposite blurs).
    ep.InJitterOffsetX = f.jitterNdc[0] * 0.5f * float(f.sw);
    ep.InJitterOffsetY = f.jitterNdc[1] * 0.5f * float(f.sh);
    ep.InRenderSubrectDimensions = {unsigned(f.sw), unsigned(f.sh)};
    ep.InReset = f.reset ? 1 : 0;
    ep.InMVScaleX = float(f.sw);
    ep.InMVScaleY = float(f.sh);
    ep.InPreExposure = 1;
    ep.InExposureScale = 1;
    return NVSDK_NGX_SUCCEED(NGX_VULKAN_EVALUATE_DLSS_EXT(c, dlss, ngxParams, &ep));
#else
    (void)c, (void)f;
    return false;
#endif
}

// ------------------------------------------------------------------ public
VkUpscaler::VkUpscaler() : impl_(std::make_unique<Impl>()) {}
VkUpscaler::~VkUpscaler() = default;

float VkUpscaler::gpuMs() const { return impl_->gpuMs; }

bool VkUpscaler::available(AiUpscaler which) {
    if (!impl_->init()) return status_ = impl_->why, false;
    if (which == AiUpscaler::Dlss4 && !impl_->dlssAvailable) return status_ = impl_->dlssWhy, false;
    return true;
}

unsigned VkUpscaler::upscale(AiUpscaler which, const VkUpscaleFrame& f) {
    if (!available(which)) return 0;
    Impl& m = *impl_;
    if (f.sw <= 0 || f.sh <= 0 || f.ow <= 0 || f.oh <= 0) return 0;
    if (!m.ensureTargets(f.sw, f.sh, f.ow, f.oh)) return status_ = "could not share images between OpenGL and Vulkan", 0;
    // 1. GL writes the inputs, then hands the images to Vulkan.
    if (!m.glInputs(f)) return status_ = "the motion-vector shader did not build", 0;
    const unsigned textures[4] = {m.color.glTexture, m.depth.glTexture, m.motion.glTexture, m.output.glTexture};
    const GLenum layouts[4] = {GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT};
    m.gl.signalSemaphore(m.glReadySem, 0, nullptr, 4, textures, layouts);
    m.gl.flush();
    // 2. Vulkan upscales, in the slot of two frames ago (its time is read now).
    m.slot = (m.slot + 1) % Impl::kFrames;
    VkCommandBuffer cmd = m.cmds[m.slot];
    VkFence fence = m.fences[m.slot];
    vkWaitForFences(m.device, 1, &fence, VK_TRUE, UINT64_MAX);
    const uint32_t query = uint32_t(2 * m.slot);
    if (m.timestamps && m.timestampsWritten[m.slot]) {
        uint64_t ts[2] = {};
        if (vkGetQueryPoolResults(m.device, m.timestamps, query, 2, sizeof ts, ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ts[1] >= ts[0]) {
            const float ms = float(double(ts[1] - ts[0]) * m.timestampPeriod / 1e6);
            m.gpuMs = m.gpuMs > 0 ? m.gpuMs + (ms - m.gpuMs) * 0.05f : ms;
        }
    }
    vkResetFences(m.device, 1, &fence);
    vkResetCommandBuffer(cmd, 0);
    auto bi = vkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    if (m.timestamps) {
        vkCmdResetQueryPool(cmd, m.timestamps, query, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m.timestamps, query);
    }
    for (const auto* s : {&m.color, &m.depth, &m.motion, &m.output}) m.barrier(cmd, *s, true);
    const bool ok = which == AiUpscaler::Fsr31 ? m.runFsr(cmd, f) : m.runDlss(cmd, f);
    for (const auto* s : {&m.color, &m.depth, &m.motion, &m.output}) m.barrier(cmd, *s, false);
    if (m.timestamps) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m.timestamps, query + 1);
    m.timestampsWritten[m.slot] = m.timestamps != VK_NULL_HANDLE;
    vkEndCommandBuffer(cmd);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    auto si = vkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &m.glReady;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &m.vkDone;
    if (vkQueueSubmit(m.queue, 1, &si, fence) != VK_SUCCESS) {
        status_ = "the Vulkan queue failed";
        m.ready = false;
        return 0;
    }
    // 3. GL waits (on the GPU) for the output.
    m.gl.waitSemaphore(m.vkDoneSem, 0, nullptr, 4, textures, layouts);
    if (!ok) {
        if (status_.empty() || status_.rfind("running", 0) == 0)
            status_ = which == AiUpscaler::Fsr31 ? "FSR 3.1 failed to run" : (m.dlssWhy.empty() ? "DLSS failed to run" : m.dlssWhy);
        return 0;
    }
    status_ = which == AiUpscaler::Fsr31 ? "running FSR 3.1" : "running DLSS 4";
    status_ += " (" + std::to_string(f.sw) + "x" + std::to_string(f.sh) + " to " + std::to_string(f.ow) + "x" + std::to_string(f.oh) + ")";
    if (m.onIntegrated) status_ += " on the integrated GPU (the dedicated one did not start)";
    return m.output.glTexture;
}

