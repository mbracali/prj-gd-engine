// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The Vulkan backend: gd::core::render::Device on Vulkan 1.1+ (docs/render.md
// section 4.3). The essentials:
//
// - SPIR-V from the build step (ShaderDesc::spirv*) in SDL_GPU's descriptor
//   set layout (3.2): sets 0 / 2 = combined image samplers (vertex /
//   fragment), sets 1 / 3 = uniform buffers. One set layout per kind, with
//   both stages, so the same descriptor set is bound at 0 and 2 (and at 1 and
//   3): the interface's slots are shared by both stages.
// - y: a negative-height viewport (y = top + h, height = -h), the front face
//   passed through unchanged, as SDL_GPU's own Vulkan driver does: the same
//   blob draws the same picture as on SDL_GPU, row 0 at the top.
// - Passes: dynamic rendering (core 1.3 or VK_KHR_dynamic_rendering), else
//   render passes and framebuffers made (and cached) per attachment set.
// - Image layouts: every image rests in one layout (sampled: SHADER_READ_ONLY,
//   else its attachment layout); an operation moves the subresources it
//   touches out of it and back, with a full barrier. Conservative, simple and
//   valid; gd01 breaks passes a few times per frame only.
// - One command buffer records in call order. Submissions carry a serial and
//   a fence; a frame slot (kFramesInFlight) waits for its previous use before
//   its command pool, upload ring and descriptor pools are reset. Readbacks
//   and present submit at once (as GL's glFlush / swap); destroyed objects
//   wait for the serial that last could use them.
// - Transient data (vertices, indices, uniforms, staging) lives in a
//   persistently mapped ring per frame slot. Uniform slots are dynamic
//   uniform buffers whose offsets move per push.

#include "vulkan/vk_api.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "common/spirv_reflect.h"
#include "gd/core/render/device.h"
#include "vulkan/vk_device.h"

namespace gd::core::render {
namespace {

// ---------------------------------------------------------------- tables

VkCompareOp vkCompare(CompareOp op) {
    static const VkCompareOp m[] = {VK_COMPARE_OP_NEVER,     VK_COMPARE_OP_LESS,          VK_COMPARE_OP_EQUAL,
                                    VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER,   VK_COMPARE_OP_NOT_EQUAL,
                                    VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_ALWAYS};
    return m[size_t(op)];
}
VkBlendFactor vkBlendFactor(BlendFactor f) {
    static const VkBlendFactor m[] = {VK_BLEND_FACTOR_ZERO,
                                      VK_BLEND_FACTOR_ONE,
                                      VK_BLEND_FACTOR_SRC_COLOR,
                                      VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                      VK_BLEND_FACTOR_DST_COLOR,
                                      VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                      VK_BLEND_FACTOR_SRC_ALPHA,
                                      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                      VK_BLEND_FACTOR_DST_ALPHA,
                                      VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
                                      VK_BLEND_FACTOR_CONSTANT_COLOR,
                                      VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
                                      VK_BLEND_FACTOR_SRC_ALPHA_SATURATE};
    return m[size_t(f)];
}
VkBlendOp vkBlendOp(BlendOp op) {
    static const VkBlendOp m[] = {VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX};
    return m[size_t(op)];
}
VkStencilOp vkStencilOp(StencilOp op) {
    static const VkStencilOp m[] = {VK_STENCIL_OP_KEEP,
                                    VK_STENCIL_OP_ZERO,
                                    VK_STENCIL_OP_REPLACE,
                                    VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                    VK_STENCIL_OP_DECREMENT_AND_CLAMP,
                                    VK_STENCIL_OP_INVERT,
                                    VK_STENCIL_OP_INCREMENT_AND_WRAP,
                                    VK_STENCIL_OP_DECREMENT_AND_WRAP};
    return m[size_t(op)];
}
VkSamplerAddressMode vkWrap(Wrap w) {
    switch (w) {
    case Wrap::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case Wrap::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case Wrap::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case Wrap::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}
VkPrimitiveTopology vkTopology(Primitive p) {
    switch (p) {
    case Primitive::Triangles: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case Primitive::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case Primitive::Lines: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case Primitive::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case Primitive::Points: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}
VkFormat vkVertexFormat(VertexFormat f) {
    switch (f) {
    case VertexFormat::Float1: return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::Byte4Norm: return VK_FORMAT_R8G8B8A8_SNORM;
    case VertexFormat::UShort2Norm: return VK_FORMAT_R16G16_UNORM;
    case VertexFormat::Short2Norm: return VK_FORMAT_R16G16_SNORM;
    case VertexFormat::Half2: return VK_FORMAT_R16G16_SFLOAT;
    case VertexFormat::Half4: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case VertexFormat::UInt1: return VK_FORMAT_R32_UINT;
    case VertexFormat::Int1: return VK_FORMAT_R32_SINT;
    }
    return VK_FORMAT_R32G32B32A32_SFLOAT;
}
// The colour formats (depth formats depend on the device: depthFormat()).
// RGB10A2 and RG11B10F: the packed formats whose red is in the low bits, as
// GL's GL_UNSIGNED_INT_2_10_10_10_REV / GL_UNSIGNED_INT_10F_11F_11F_REV.
VkFormat vkColorFormat(Format f) {
    switch (f) {
    case Format::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::R8: return VK_FORMAT_R8_UNORM;
    case Format::RG8: return VK_FORMAT_R8G8_UNORM;
    case Format::RGB10A2: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case Format::RG11B10F: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case Format::R16F: return VK_FORMAT_R16_SFLOAT;
    case Format::RG16F: return VK_FORMAT_R16G16_SFLOAT;
    case Format::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32F: return VK_FORMAT_R32_SFLOAT;
    case Format::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC1_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case Format::BC2: return VK_FORMAT_BC2_UNORM_BLOCK;
    case Format::BC2_SRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
    case Format::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC3_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
    case Format::BC4: return VK_FORMAT_BC4_UNORM_BLOCK;
    case Format::BC5: return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
    default: return VK_FORMAT_UNDEFINED;
    }
}

const char* vkResultName(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    default: return "VkResult";
    }
}
std::string vkError(const char* what, VkResult r) { return std::string(what) + " failed (" + vkResultName(r) + " " + std::to_string(int(r)) + ")"; }

// A pool of objects addressed by 1-based ids (0 is "none"), as the GL backend's.
template <class T>
struct Pool {
    std::vector<T> items;
    std::vector<bool> live;
    std::vector<uint32_t> freeIds;
    uint32_t add(T v) {
        if (!freeIds.empty()) {
            uint32_t id = freeIds.back();
            freeIds.pop_back();
            items[id - 1] = std::move(v);
            live[id - 1] = true;
            return id;
        }
        items.push_back(std::move(v));
        live.push_back(true);
        return uint32_t(items.size());
    }
    T* get(uint32_t id) { return id && id <= items.size() && live[id - 1] ? &items[id - 1] : nullptr; }
    const T* get(uint32_t id) const { return id && id <= items.size() && live[id - 1] ? &items[id - 1] : nullptr; }
    void remove(uint32_t id) {
        if (!get(id)) return;
        live[id - 1] = false;
        items[id - 1] = T{};
        freeIds.push_back(id);
    }
};

bool envFlag(const char* name) {
    const char* v = SDL_getenv(name);
    return v && *v && std::strcmp(v, "0") != 0;
}

// Validation messages (the debug messenger), counted for the log at the end.
int g_validationErrors = 0, g_validationWarnings = 0;
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const bool error = severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if (error) ++g_validationErrors;
    else ++g_validationWarnings;
    SDL_Log("gdcore render (vulkan): %s: %s", error ? "VALIDATION ERROR" : "validation", data && data->pMessage ? data->pMessage : "");
    return VK_FALSE;
}

// ---------------------------------------------------------------- objects

struct VBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    uint8_t* mapped = nullptr;  // host-visible ones (the rings)
    BufferDesc desc;
};
struct VTexture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    TextureDesc desc;  // mips resolved (0 -> the full chain)
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = 0;        // every aspect of the format
    VkImageAspectFlags sampleAspect = 0;  // what a sampled view sees (depth only for depth + stencil)
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    int layers = 1;
    VkImageView view = VK_NULL_HANDLE;   // sampled (2D or cube); none for MSAA
    std::vector<VkImageView> rtViews;     // attachment views, [mip * layers + layer], made on first use
    VkImageLayout rest = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    uint64_t uid = 0;  // never reused: descriptor and framebuffer caches key on it
};
struct VShader {
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    SpirvStageInfo vi, fi;
    std::string name;
};
struct VPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    PipelineDesc desc;
};
struct VReadback {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    size_t size = 0;
    uint64_t serial = 0;
};

constexpr size_t kChunkSize = size_t(4) << 20;

class VulkanDevice final : public Device {
public:
    explicit VulkanDevice(const DeviceCreateInfo& info) : info_(info) {}
    ~VulkanDevice() override;
    bool init(std::string& error);

    Backend backend() const override { return Backend::Vulkan; }
    const Caps& caps() const override { return caps_; }
    const std::string& lastError() const override { return error_; }

    Buffer createBuffer(const BufferDesc& desc, const void* data) override;
    void updateBuffer(Buffer b, size_t offset, const void* data, size_t size) override;
    void destroyBuffer(Buffer b) override;
    BufferRange allocTransient(uint8_t usage, const void* data, size_t size) override;

    Texture createTexture(const TextureDesc& desc) override;
    void updateTexture(const TextureRegion& dst, int w, int h, const void* data, size_t rowBytes) override;
    void generateMipmaps(Texture t) override;
    void destroyTexture(Texture t) override;
    TextureDesc textureDesc(Texture t) const override {
        const VTexture* tx = textures_.get(t.id);
        return tx ? tx->desc : TextureDesc{};
    }

    Sampler createSampler(const SamplerDesc& desc) override;
    Shader createShader(const ShaderDesc& desc) override;
    void destroyShader(Shader s) override;
    Pipeline createPipeline(const PipelineDesc& desc) override;
    void destroyPipeline(Pipeline p) override;

    void copyTexture(const TextureRegion& src, const TextureRegion& dst, int w, int h) override;
    void blit(const TextureRegion& src, int sw, int sh, const TextureRegion& dst, int dw, int dh, Filter filter) override;
    Readback readTexture(Texture t, const Rect& r) override;
    bool readbackReady(Readback r) override;
    bool readbackData(Readback r, void* dst, size_t size) override;

    void beginFrame() override;
    void beginPass(const PassDesc& pass) override;
    void setViewport(const Rect& r, float minDepth, float maxDepth) override;
    void setScissor(const Rect& r) override;
    void setStencilRef(uint8_t ref) override;
    void setBlendConstants(const float rgba[4]) override;
    void bindPipeline(Pipeline p) override;
    void bindVertexBuffers(int firstSlot, const BufferRange* buffers, int count) override;
    void bindIndexBuffer(const BufferRange& buffer, IndexType type) override;
    void bindTextures(int firstSlot, const TextureBinding* bindings, int count) override;
    void pushUniforms(int slot, const void* data, size_t size) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;
    void endPass() override;
    bool inPass() const override { return inPass_; }

    void drawableSize(int& w, int& h) const override { SDL_GetWindowSizeInPixels(info_.window, &w, &h); }
    void present(Texture src, const Rect& srcRect, Filter filter, bool flipVertical) override;
    void setPresentMode(PresentMode mode) override;
    void endFrame() override;

    void pushDebugGroup(const char* name) override;
    void popDebugGroup() override;

private:
    struct Chunk {
        uint32_t buffer = 0;  // Buffer id
        size_t size = 0, used = 0;
    };
    struct Slot {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> cmds;
        size_t cmdsUsed = 0;
        uint64_t lastSerial = 0;
        std::vector<Chunk> chunks;
        std::vector<VkDescriptorPool> descPools;
        size_t descPool = 0;
        std::map<std::array<uint64_t, kMaxTextureSlots>, VkDescriptorSet> texSets;
        std::map<std::array<uint32_t, kMaxUniformSlots>, VkDescriptorSet> uboSets;
    };
    struct Deferred {
        uint64_t serial;
        std::function<void()> release;
    };

    bool fail(const std::string& e) {
        error_ = e;
        SDL_Log("gdcore render (vulkan): %s", e.c_str());
        return false;
    }
    bool createInstance(std::string& error);
    bool pickDevice(std::string& error);
    bool createLogicalDevice(std::string& error);
    void fillCaps();
    bool createSwapchain();
    void destroySwapchain();
    void loadPipelineCache();
    void savePipelineCache();

    // Formats.
    VkFormat depthVkFormat(Format f) const;
    VkFormat vkFormatOf(Format f) const { return isDepthFormat(f) ? depthVkFormat(f) : vkColorFormat(f); }
    VkFormatFeatureFlags features(VkFormat f) const;
    VkSampleCountFlagBits sampleBits(int samples) const;

    // Submissions and frames.
    bool ensureRecording();
    VkCommandBuffer initCommands();  // a command buffer submitted before the current one (new images' first layout)
    VkCommandBuffer nextCommandBuffer(Slot& s);
    void submit(VkSemaphore wait = VK_NULL_HANDLE, VkSemaphore signal = VK_NULL_HANDLE);
    void poll();
    bool waitSerial(uint64_t serial);
    void defer(std::function<void()> release);
    VkFence takeFence();
    VkSemaphore takeSemaphore();

    // Recording helpers.
    void transition(VkCommandBuffer cmd, const VTexture& t, VkImageLayout from, VkImageLayout to, int mip, int mips, int layer, int layers);
    void fullBarrier(VkCommandBuffer cmd);
    VkImageView rtView(VTexture& t, int mip, int layer);
    BufferRange ringAlloc(const void* data, size_t size, size_t align, size_t reserve = 0);
    void flushBindings();
    VkDescriptorSet allocSet(VkDescriptorSetLayout layout);
    VkRenderPass renderPassFor(const PassDesc* pass, const VkFormat* colors, int colorCount, VkFormat depth, VkSampleCountFlagBits samples);
    void releaseTextureNow(VTexture& t);

    DeviceCreateInfo info_;
    Caps caps_;
    std::string error_;
    bool lost_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    bool debugUtils_ = false;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    VkPhysicalDeviceFeatures supported_{};
    uint32_t apiVersion_ = VK_API_VERSION_1_1;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VmaAllocator vma_ = nullptr;
    bool dynamicRendering_ = false;
    PFN_vkCmdBeginRenderingKHR cmdBeginRendering_ = nullptr;
    PFN_vkCmdEndRenderingKHR cmdEndRendering_ = nullptr;
    bool depthClamp_ = false, anisotropy_ = false, independentBlend_ = false;
    bool sdlVulkanLoaded_ = false;
    VkFormat d24_ = VK_FORMAT_D32_SFLOAT, d24s8_ = VK_FORMAT_D32_SFLOAT_S8_UINT;

    // Swapchain (none: headless, present does nothing).
    bool canPresent_ = false;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent_{0, 0};
    std::vector<VkImage> swapImages_;
    std::vector<VkSemaphore> renderDone_;  // one per swapchain image
    bool swapDirty_ = false;
    bool presentLogged_ = false;

    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout texLayout_ = VK_NULL_HANDLE, uboLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;

    Pool<VBuffer> buffers_;
    Pool<VTexture> textures_;
    Pool<VShader> shaders_;
    Pool<VPipeline> pipelines_;
    Pool<VReadback> readbacks_;
    std::vector<std::pair<SamplerDesc, VkSampler>> samplers_;  // id = index + 1
    uint64_t nextUid_ = 1;
    Texture dummyTexture_;
    Sampler dummySampler_;
    Buffer dummyUbo_;

    // Render passes (without dynamic rendering): by attachment formats and
    // operations; framebuffers by render pass and views (and what they use).
    std::map<std::string, VkRenderPass> renderPasses_;
    struct Fb {
        VkFramebuffer fb = VK_NULL_HANDLE;
        std::vector<uint64_t> uids;
    };
    std::map<std::string, Fb> framebuffers_;

    // Submissions.
    Slot slots_[kFramesInFlight];
    int slot_ = 0;
    bool slotReady_ = false;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE, initCmd_ = VK_NULL_HANDLE;
    uint64_t serial_ = 1, completed_ = 0;
    std::deque<std::pair<uint64_t, VkFence>> inflight_;
    std::vector<VkFence> freeFences_;
    std::vector<VkSemaphore> freeSemaphores_;
    std::deque<std::pair<uint64_t, VkSemaphore>> usedSemaphores_;
    std::deque<Deferred> deferred_;

    // Pass and binding state.
    bool inPass_ = false;
    PassDesc pass_;
    int passW_ = 0, passH_ = 0;
    const VPipeline* pipeline_ = nullptr;
    uint32_t pipelineId_ = 0;
    TextureBinding textures_bound_[kMaxTextureSlots];
    bool texDirty_ = true;
    VkBuffer uboBuffer_[kMaxUniformSlots] = {};
    uint32_t uboBufferId_[kMaxUniformSlots] = {};
    uint32_t uboOffset_[kMaxUniformSlots] = {};
    bool uboDirty_ = true;
    VkDescriptorSet texSet_ = VK_NULL_HANDLE, uboSet_ = VK_NULL_HANDLE;
};

// ---------------------------------------------------------------- creation

VulkanDevice::~VulkanDevice() {
    if (device_) {
        if (inPass_) endPass();
        if (cmd_) submit();
        vkDeviceWaitIdle(device_);
        poll();
        completed_ = serial_;
        while (!deferred_.empty()) {
            deferred_.front().release();
            deferred_.pop_front();
        }
        savePipelineCache();
        for (size_t i = 0; i < readbacks_.items.size(); ++i)
            if (readbacks_.live[i]) vmaDestroyBuffer(vma_, readbacks_.items[i].buffer, readbacks_.items[i].alloc);
        for (size_t i = 0; i < pipelines_.items.size(); ++i)
            if (pipelines_.live[i]) vkDestroyPipeline(device_, pipelines_.items[i].pipeline, nullptr);
        for (size_t i = 0; i < shaders_.items.size(); ++i)
            if (shaders_.live[i]) {
                vkDestroyShaderModule(device_, shaders_.items[i].vs, nullptr);
                vkDestroyShaderModule(device_, shaders_.items[i].fs, nullptr);
            }
        for (auto& [k, f] : framebuffers_) vkDestroyFramebuffer(device_, f.fb, nullptr);
        for (auto& [k, rp] : renderPasses_) vkDestroyRenderPass(device_, rp, nullptr);
        for (size_t i = 0; i < textures_.items.size(); ++i)
            if (textures_.live[i]) releaseTextureNow(textures_.items[i]);
        for (size_t i = 0; i < buffers_.items.size(); ++i)
            if (buffers_.live[i]) vmaDestroyBuffer(vma_, buffers_.items[i].buffer, buffers_.items[i].alloc);
        for (auto& [d, s] : samplers_) vkDestroySampler(device_, s, nullptr);
        for (Slot& s : slots_) {
            for (VkDescriptorPool p : s.descPools) vkDestroyDescriptorPool(device_, p, nullptr);
            if (s.pool) vkDestroyCommandPool(device_, s.pool, nullptr);
        }
        for (VkFence f : freeFences_) vkDestroyFence(device_, f, nullptr);
        for (auto& [s, f] : inflight_) vkDestroyFence(device_, f, nullptr);
        for (VkSemaphore s : freeSemaphores_) vkDestroySemaphore(device_, s, nullptr);
        for (auto& [s, sem] : usedSemaphores_) vkDestroySemaphore(device_, sem, nullptr);
        destroySwapchain();
        if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (texLayout_) vkDestroyDescriptorSetLayout(device_, texLayout_, nullptr);
        if (uboLayout_) vkDestroyDescriptorSetLayout(device_, uboLayout_, nullptr);
        if (pipelineCache_) vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        if (vma_) vmaDestroyAllocator(vma_);
        vkDestroyDevice(device_, nullptr);
    }
    if (instance_) {
        if (surface_) SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
        if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        vkDestroyInstance(instance_, nullptr);
    }
    if (sdlVulkanLoaded_) SDL_Vulkan_UnloadLibrary();
    if (g_validationErrors || g_validationWarnings)
        SDL_Log("gdcore render (vulkan): %d validation error(s), %d other message(s)", g_validationErrors, g_validationWarnings);
}

bool VulkanDevice::createInstance(std::string& error) {
    // The loader SDL found (libvulkan.so.1 / vulkan-1.dll / the NDK's
    // libvulkan.so; MoltenVK on Apple later): no Vulkan library at link time.
    if (!SDL_Vulkan_LoadLibrary(nullptr)) return error = std::string("no Vulkan loader: ") + SDL_GetError(), false;
    sdlVulkanLoaded_ = true;
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!gipa) return error = std::string("no vkGetInstanceProcAddr: ") + SDL_GetError(), false;
    volkInitializeCustom(gipa);
    const uint32_t loaderVersion = volkGetInstanceVersion();
    if (loaderVersion < VK_API_VERSION_1_1) return error = "the Vulkan loader is older than 1.1", false;
    apiVersion_ = std::min<uint32_t>(loaderVersion, VK_API_VERSION_1_3);

    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
    auto hasExt = [&](const char* name) {
        for (auto& e : exts)
            if (std::strcmp(e.extensionName, name) == 0) return true;
        return false;
    };
    std::vector<const char*> enable;
    Uint32 sdlCount = 0;
    if (const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlCount))
        for (Uint32 i = 0; i < sdlCount; ++i) enable.push_back(sdlExts[i]);
    VkInstanceCreateFlags flags = 0;
    if (hasExt(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {  // MoltenVK / KosmicKrisp
        enable.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    // Validation: debug builds, info.debug, or GDCORE_VK_VALIDATION=1, when
    // the Khronos layer is installed.
#ifndef NDEBUG
    bool wantValidation = true;
#else
    bool wantValidation = info_.debug;
#endif
    if (const char* v = SDL_getenv("GDCORE_VK_VALIDATION")) wantValidation = *v && std::strcmp(v, "0") != 0;
    std::vector<const char*> layers;
    if (wantValidation) {
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> ls(n);
        vkEnumerateInstanceLayerProperties(&n, ls.data());
        bool found = false;
        for (auto& l : ls) found = found || std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        if (found) layers.push_back("VK_LAYER_KHRONOS_validation");
        else SDL_Log("gdcore render (vulkan): validation asked for, but VK_LAYER_KHRONOS_validation is not installed");
    }
    debugUtils_ = hasExt(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils_) enable.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Guitar Dream Engine";
    app.pEngineName = "gd-engine-core";
    app.apiVersion = apiVersion_;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.flags = flags;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = uint32_t(enable.size());
    ci.ppEnabledExtensionNames = enable.data();
    ci.enabledLayerCount = uint32_t(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS) return error = vkError("vkCreateInstance", r), false;
    volkLoadInstanceOnly(instance_);
    if (debugUtils_ && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = debugCallback;
        vkCreateDebugUtilsMessengerEXT(instance_, &mi, nullptr, &messenger_);
        SDL_Log("gdcore render (vulkan): validation layer on");
    }
    return true;
}

bool VulkanDevice::pickDevice(std::string& error) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(instance_, &n, gpus.data());
    if (gpus.empty()) return error = "no Vulkan device", false;
    // GDCORE_VK_DEVICE=<index or part of the name>: a development aid (e.g. llvmpipe).
    const char* want = SDL_getenv("GDCORE_VK_DEVICE");
    int best = -1, bestScore = -1;
    std::string why = "no Vulkan 1.1 device with graphics";
    for (uint32_t i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(gpus[i], &p);
        if (p.apiVersion < VK_API_VERSION_1_1) continue;
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, qs.data());
        int family = -1;
        for (uint32_t q = 0; q < qn && family < 0; ++q) {
            if (!(qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_TRUE;
            if (surface_) vkGetPhysicalDeviceSurfaceSupportKHR(gpus[i], q, surface_, &present);
            if (present) family = int(q);
        }
        if (family < 0) {
            why = "no Vulkan device can present to this window";
            continue;
        }
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2
                                                                           : 1;
        if (want && *want) {
            char* end = nullptr;
            long idx = std::strtol(want, &end, 10);
            const bool match = (end && *end == 0) ? idx == long(i) : std::strstr(p.deviceName, want) != nullptr;
            if (match) score += 100;
        }
        if (score > bestScore) best = int(i), bestScore = score, queueFamily_ = uint32_t(family);
    }
    if (best < 0) return error = why, false;
    gpu_ = gpus[size_t(best)];
    vkGetPhysicalDeviceProperties(gpu_, &props_);
    vkGetPhysicalDeviceFeatures(gpu_, &supported_);
    apiVersion_ = std::min(apiVersion_, props_.apiVersion);
    return true;
}

bool VulkanDevice::createLogicalDevice(std::string& error) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &n, exts.data());
    auto hasExt = [&](const char* name) {
        for (auto& e : exts)
            if (std::strcmp(e.extensionName, name) == 0) return true;
        return false;
    };
    std::vector<const char*> enable;
    if (surface_) {
        if (!hasExt(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) return error = "the Vulkan device has no VK_KHR_swapchain", false;
        enable.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    if (hasExt("VK_KHR_portability_subset")) enable.push_back("VK_KHR_portability_subset");
    // Dynamic rendering: core in 1.3; the extension (with what it needs) on 1.1 / 1.2.
    bool dynExt = false;
    if (apiVersion_ < VK_API_VERSION_1_3 && hasExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) &&
        (apiVersion_ >= VK_API_VERSION_1_2 || (hasExt(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME) && hasExt(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME)))) {
        if (apiVersion_ < VK_API_VERSION_1_2) {
            enable.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
            enable.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
        }
        enable.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        dynExt = true;
    }
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    if (apiVersion_ >= VK_API_VERSION_1_3 || dynExt) {
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &dynFeatures;
        vkGetPhysicalDeviceFeatures2(gpu_, &f2);
    }
    const char* noDyn = SDL_getenv("GDCORE_VK_RENDER_PASSES");  // development aid: test the render-pass path
    dynamicRendering_ = dynFeatures.dynamicRendering && !(noDyn && *noDyn && std::strcmp(noDyn, "0") != 0);
    if (!dynamicRendering_ && dynExt) {
        // Not used: do not enable what is not needed.
        enable.erase(std::remove_if(enable.begin(), enable.end(),
                                    [](const char* e) { return std::strcmp(e, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0; }),
                     enable.end());
    }

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = supported_.samplerAnisotropy;
    features.depthClamp = supported_.depthClamp;
    features.textureCompressionBC = supported_.textureCompressionBC;
    features.independentBlend = supported_.independentBlend;
    depthClamp_ = supported_.depthClamp, anisotropy_ = supported_.samplerAnisotropy, independentBlend_ = supported_.independentBlend;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = queueFamily_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynOn{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    dynOn.dynamicRendering = VK_TRUE;
    if (dynamicRendering_) ci.pNext = &dynOn;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = uint32_t(enable.size());
    ci.ppEnabledExtensionNames = enable.data();
    ci.pEnabledFeatures = &features;
    VkResult r = vkCreateDevice(gpu_, &ci, nullptr, &device_);
    if (r != VK_SUCCESS) return error = vkError("vkCreateDevice", r), false;
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    if (dynamicRendering_) {
        cmdBeginRendering_ = apiVersion_ >= VK_API_VERSION_1_3 ? vkCmdBeginRendering : vkCmdBeginRenderingKHR;
        cmdEndRendering_ = apiVersion_ >= VK_API_VERSION_1_3 ? vkCmdEndRendering : vkCmdEndRenderingKHR;
        if (!cmdBeginRendering_ || !cmdEndRendering_) dynamicRendering_ = false;
    }

    VmaVulkanFunctions fn{};
    fn.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    fn.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo ai{};
    ai.vulkanApiVersion = apiVersion_;
    ai.physicalDevice = gpu_;
    ai.device = device_;
    ai.instance = instance_;
    ai.pVulkanFunctions = &fn;
    r = vmaCreateAllocator(&ai, &vma_);
    if (r != VK_SUCCESS) return error = vkError("vmaCreateAllocator", r), false;
    return true;
}

VkFormatFeatureFlags VulkanDevice::features(VkFormat f) const {
    if (f == VK_FORMAT_UNDEFINED) return 0;
    VkFormatProperties p;
    vkGetPhysicalDeviceFormatProperties(gpu_, f, &p);
    return p.optimalTilingFeatures;
}

VkFormat VulkanDevice::depthVkFormat(Format f) const {
    switch (f) {
    case Format::D16: return VK_FORMAT_D16_UNORM;
    case Format::D24: return d24_;
    case Format::D24S8: return d24s8_;
    case Format::D32F: return VK_FORMAT_D32_SFLOAT;
    case Format::D32FS8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

VkSampleCountFlagBits VulkanDevice::sampleBits(int samples) const {
    int s = 1;
    while (s * 2 <= samples && s * 2 <= caps_.maxSamples) s *= 2;
    return VkSampleCountFlagBits(s);
}

void VulkanDevice::fillCaps() {
    caps_.api = "Vulkan";
    char v[96];
    std::snprintf(v, sizeof v, "%u.%u.%u (driver 0x%x)", VK_API_VERSION_MAJOR(props_.apiVersion), VK_API_VERSION_MINOR(props_.apiVersion),
                  VK_API_VERSION_PATCH(props_.apiVersion), props_.driverVersion);
    caps_.version = v;
    caps_.gpu = props_.deviceName;
    caps_.es = false;
    caps_.maxTextureSize = int(props_.limits.maxImageDimension2D);
    const VkSampleCountFlags counts = props_.limits.framebufferColorSampleCounts & props_.limits.framebufferDepthSampleCounts;
    caps_.maxSamples = 1;
    for (int s = 2; s <= 64; s *= 2)
        if (counts & VkSampleCountFlags(s)) caps_.maxSamples = s;
    caps_.maxAnisotropy = anisotropy_ ? props_.limits.maxSamplerAnisotropy : 1;
    caps_.clampToBorder = true;  // core Vulkan, with the three standard border colours

    const VkFormatFeatureFlags depthNeeds = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    // D24 / D24S8 where the device has them (not every AMD GPU: D32F behind the same Format).
    d24_ = (features(VK_FORMAT_X8_D24_UNORM_PACK32) & depthNeeds) == depthNeeds ? VK_FORMAT_X8_D24_UNORM_PACK32 : VK_FORMAT_D32_SFLOAT;
    d24s8_ = (features(VK_FORMAT_D24_UNORM_S8_UINT) & depthNeeds) == depthNeeds ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
    for (size_t i = 1; i < size_t(Format::Count); ++i) {
        const Format f = Format(i);
        const VkFormat vf = vkFormatOf(f);
        const VkFormatFeatureFlags ff = features(vf);
        uint8_t bits = 0;
        if (isCompressedFormat(f) && !supported_.textureCompressionBC) {
            caps_.formats[i] = 0;
            continue;
        }
        if (ff & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) bits |= kSampled;
        if (ff & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) bits |= kFiltered;
        if (ff & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) bits |= kRenderable;
        if (ff & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) bits |= kBlendable;
        if ((ff & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) && caps_.maxSamples > 1) bits |= kResolvable;
        caps_.formats[i] = bits;
    }
}

bool VulkanDevice::init(std::string& error) {
    if (!createInstance(error)) return fail(error);
    // The window's surface. Without one (a window not made with
    // SDL_WINDOW_VULKAN, or a video driver that cannot present) the device
    // only draws off screen: allowed under SDL_VIDEO_DRIVER=offscreen (CI,
    // the self-test) or with GDCORE_VK_HEADLESS=1 (no surface at all: any
    // device, e.g. GDCORE_VK_DEVICE=NVIDIA under offscreen, whose driver
    // cannot present to the headless surface), an error otherwise (a game
    // must never get a device that shows nothing: it falls back).
    const char* driver = SDL_GetCurrentVideoDriver();
    const bool forceHeadless = envFlag("GDCORE_VK_HEADLESS");
    const bool headlessOk = forceHeadless || (driver && std::strcmp(driver, "offscreen") == 0);
    std::string noPresent;
    if (forceHeadless) {
        noPresent = "GDCORE_VK_HEADLESS";
    } else if (!SDL_Vulkan_CreateSurface(info_.window, instance_, nullptr, &surface_)) {
        noPresent = std::string("no Vulkan surface on the window: ") + SDL_GetError();
        surface_ = VK_NULL_HANDLE;
    }
    if (!pickDevice(error)) {
        if (!surface_ || !headlessOk) return fail(error);
        // A surface no device presents to (the offscreen driver's headless one): draw off screen.
        noPresent = error;
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
        if (!pickDevice(error)) return fail(error);
    }
    if (!surface_ && !headlessOk) return error = noPresent, fail(error);
    if (!createLogicalDevice(error)) return fail(error);
    fillCaps();

    // The descriptor set layouts of SDL_GPU's SPIR-V sets (3.2), with both stages.
    {
        VkDescriptorSetLayoutBinding b[kMaxTextureSlots]{};
        for (int i = 0; i < kMaxTextureSlots; ++i)
            b[i] = {uint32_t(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = kMaxTextureSlots;
        ci.pBindings = b;
        vkCreateDescriptorSetLayout(device_, &ci, nullptr, &texLayout_);
        VkDescriptorSetLayoutBinding u[kMaxUniformSlots]{};
        for (int i = 0; i < kMaxUniformSlots; ++i)
            u[i] = {uint32_t(i), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        ci.bindingCount = kMaxUniformSlots;
        ci.pBindings = u;
        vkCreateDescriptorSetLayout(device_, &ci, nullptr, &uboLayout_);
        VkDescriptorSetLayout sets[4] = {texLayout_, uboLayout_, texLayout_, uboLayout_};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 4;
        pl.pSetLayouts = sets;
        if (VkResult r = vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_); r != VK_SUCCESS)
            return error = vkError("vkCreatePipelineLayout", r), fail(error);
    }
    for (Slot& s : slots_) {
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        ci.queueFamilyIndex = queueFamily_;
        if (VkResult r = vkCreateCommandPool(device_, &ci, nullptr, &s.pool); r != VK_SUCCESS)
            return error = vkError("vkCreateCommandPool", r), fail(error);
    }
    loadPipelineCache();

    // What unbound slots get: a 1 x 1 texture, a sampler, an empty uniform block.
    {
        TextureDesc d;
        d.width = d.height = 1;
        d.label = "gdcore dummy";
        dummyTexture_ = createTexture(d);
        const uint8_t zero[4] = {0, 0, 0, 0};
        updateTexture({dummyTexture_}, 1, 1, zero, 0);
        dummySampler_ = createSampler(SamplerDesc{});
        std::vector<uint8_t> z(kMaxUniformBytes, 0);
        BufferDesc b;
        b.size = kMaxUniformBytes;
        b.usage = kUniformBuffer | kVertexBuffer | kIndexBuffer;  // also what an empty vertex buffer slot gets
        b.label = "gdcore dummy buffer";
        dummyUbo_ = createBuffer(b, z.data());
        if (!dummyTexture_ || !dummyUbo_) return error = "could not make the default resources: " + error_, fail(error);
    }

    canPresent_ = surface_ != VK_NULL_HANDLE;
    if (canPresent_ && !createSwapchain()) {
        if (!headlessOk) return error = "no swapchain: " + error_, fail(error);
        canPresent_ = false;
    }
    if (!canPresent_) SDL_Log("gdcore render (vulkan): drawing off screen only (%s)", noPresent.empty() ? error_.c_str() : noPresent.c_str());
    SDL_Log("gdcore render: Vulkan %u.%u on %s (%s, %s)", VK_API_VERSION_MAJOR(apiVersion_), VK_API_VERSION_MINOR(apiVersion_), caps_.gpu.c_str(),
            caps_.version.c_str(), dynamicRendering_ ? "dynamic rendering" : "render passes");
    error_.clear();
    return true;
}

// ---------------------------------------------------------------- pipeline cache on disk

static std::string cachePath(const std::string& dir) {
    if (dir.empty()) return {};
    std::string p = dir;
    if (p.back() != '/' && p.back() != '\\') p += '/';
    return p + "vulkan_pipeline_cache.bin";
}

void VulkanDevice::loadPipelineCache() {
    std::vector<uint8_t> data;
    const std::string path = cachePath(info_.cacheDir);
    if (!path.empty()) {
        size_t size = 0;
        if (void* p = SDL_LoadFile(path.c_str(), &size)) {
            // Only a cache of this very device and driver (the header,
            // VkPipelineCacheHeaderVersionOne); drivers check too, not all well.
            VkPipelineCacheHeaderVersionOne h{};
            if (size >= sizeof h) {
                std::memcpy(&h, p, sizeof h);
                if (h.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE && h.vendorID == props_.vendorID && h.deviceID == props_.deviceID &&
                    std::memcmp(h.pipelineCacheUUID, props_.pipelineCacheUUID, VK_UUID_SIZE) == 0)
                    data.assign(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + size);
            }
            SDL_free(p);
        }
    }
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    ci.initialDataSize = data.size();
    ci.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(device_, &ci, nullptr, &pipelineCache_) != VK_SUCCESS && !data.empty()) {
        ci.initialDataSize = 0, ci.pInitialData = nullptr;
        vkCreatePipelineCache(device_, &ci, nullptr, &pipelineCache_);
    }
}

void VulkanDevice::savePipelineCache() {
    const std::string path = cachePath(info_.cacheDir);
    if (path.empty() || !pipelineCache_) return;
    size_t size = 0;
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) != VK_SUCCESS || !size) return;
    std::vector<uint8_t> data(size);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, data.data()) != VK_SUCCESS) return;
    // Written aside, then renamed: a crash never leaves half a file.
    const std::string tmp = path + ".tmp";
    if (SDL_SaveFile(tmp.c_str(), data.data(), size) && !SDL_RenamePath(tmp.c_str(), path.c_str())) SDL_RemovePath(tmp.c_str());
}

// ---------------------------------------------------------------- swapchain

void VulkanDevice::destroySwapchain() {
    for (VkSemaphore s : renderDone_) vkDestroySemaphore(device_, s, nullptr);
    renderDone_.clear();
    swapImages_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

bool VulkanDevice::createSwapchain() {
    VkSurfaceCapabilitiesKHR sc;
    if (VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &sc); r != VK_SUCCESS)
        return fail(vkError("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r));
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(info_.window, &w, &h);
    VkExtent2D extent = sc.currentExtent;
    if (extent.width == 0xFFFFFFFFu) extent = {uint32_t(std::max(w, 1)), uint32_t(std::max(h, 1))};
    extent.width = std::clamp(extent.width, sc.minImageExtent.width, std::max(sc.minImageExtent.width, sc.maxImageExtent.width));
    extent.height = std::clamp(extent.height, sc.minImageExtent.height, std::max(sc.minImageExtent.height, sc.maxImageExtent.height));
    if (extent.width == 0 || extent.height == 0) return false;  // minimised: try again at the next present
    if (!(sc.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return fail("the swapchain images cannot be blitted into");

    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, formats.data());
    if (formats.empty()) return fail("the surface has no formats");
    // An 8-bit UNORM surface in sRGB space, as GL's default framebuffer (no
    // sRGB encoding on write): the frame's values go to the screen as they are.
    VkSurfaceFormatKHR chosen = formats[0];
    bool found = false;
    for (VkFormat want : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_A2B10G10R10_UNORM_PACK32})
        for (auto& f : formats)
            if (!found && f.format == want && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) chosen = f, found = true;
    if (!(features(chosen.format) & VK_FORMAT_FEATURE_BLIT_DST_BIT)) return fail("the swapchain format cannot be blitted into");

    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, modes.data());
    auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // always there: vsync
    if (info_.presentMode == PresentMode::Immediate) mode = has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR
                                                            : has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
                                                                                               : mode;
    if (info_.presentMode == PresentMode::Mailbox) mode = has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
                                                          : has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR
                                                                                               : mode;
    uint32_t count = std::max(sc.minImageCount + 1, 2u);
    if (sc.maxImageCount) count = std::min(count, sc.maxImageCount);

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = count;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = (sc.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : sc.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
        if (sc.supportedCompositeAlpha & a) {
            ci.compositeAlpha = a;
            break;
        }
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = swapchain_;
    VkSwapchainKHR sw = VK_NULL_HANDLE;
    VkResult r = vkCreateSwapchainKHR(device_, &ci, nullptr, &sw);
    destroySwapchain();  // the old one (the device is idle: present waited for it)
    if (r != VK_SUCCESS) return fail(vkError("vkCreateSwapchainKHR", r));
    swapchain_ = sw;
    swapFormat_ = chosen.format;
    swapExtent_ = extent;
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    swapImages_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapImages_.data());
    renderDone_.resize(n);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore& s : renderDone_) vkCreateSemaphore(device_, &si, nullptr, &s);
    swapDirty_ = false;
    return true;
}

// ---------------------------------------------------------------- submissions

VkFence VulkanDevice::takeFence() {
    if (!freeFences_.empty()) {
        VkFence f = freeFences_.back();
        freeFences_.pop_back();
        return f;
    }
    VkFenceCreateInfo ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence f = VK_NULL_HANDLE;
    vkCreateFence(device_, &ci, nullptr, &f);
    return f;
}

VkSemaphore VulkanDevice::takeSemaphore() {
    if (!freeSemaphores_.empty()) {
        VkSemaphore s = freeSemaphores_.back();
        freeSemaphores_.pop_back();
        return s;
    }
    VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore s = VK_NULL_HANDLE;
    vkCreateSemaphore(device_, &ci, nullptr, &s);
    return s;
}

void VulkanDevice::poll() {
    while (!inflight_.empty()) {
        VkResult r = vkGetFenceStatus(device_, inflight_.front().second);
        if (r == VK_NOT_READY) break;
        if (r == VK_ERROR_DEVICE_LOST) lost_ = true;
        completed_ = inflight_.front().first;
        vkResetFences(device_, 1, &inflight_.front().second);
        freeFences_.push_back(inflight_.front().second);
        inflight_.pop_front();
    }
    while (!usedSemaphores_.empty() && usedSemaphores_.front().first <= completed_) {
        freeSemaphores_.push_back(usedSemaphores_.front().second);
        usedSemaphores_.pop_front();
    }
    while (!deferred_.empty() && deferred_.front().serial <= completed_) {
        deferred_.front().release();
        deferred_.pop_front();
    }
}

bool VulkanDevice::waitSerial(uint64_t serial) {
    if (serial >= serial_ && cmd_) submit();  // still being recorded: send it
    while (completed_ < serial && !inflight_.empty()) {
        VkResult r = vkWaitForFences(device_, 1, &inflight_.front().second, VK_TRUE, 10ull * 1000 * 1000 * 1000);
        if (r != VK_SUCCESS) {
            fail(vkError("waiting for the GPU", r));
            lost_ = true;
            completed_ = serial_;  // never wait again
            return false;
        }
        poll();
    }
    return completed_ >= serial;
}

void VulkanDevice::defer(std::function<void()> release) { deferred_.push_back({serial_, std::move(release)}); }

VkCommandBuffer VulkanDevice::nextCommandBuffer(Slot& s) {
    if (s.cmdsUsed == s.cmds.size()) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = s.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer c = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device_, &ai, &c) != VK_SUCCESS) return VK_NULL_HANDLE;
        s.cmds.push_back(c);
    }
    VkCommandBuffer c = s.cmds[s.cmdsUsed++];
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(c, &bi);
    return c;
}

bool VulkanDevice::ensureRecording() {
    if (cmd_) return true;
    if (lost_ || !device_) return false;
    Slot& s = slots_[slot_];
    if (!slotReady_) {
        // The slot's previous frame (kFramesInFlight ago) is done with its
        // command buffers, ring and descriptor sets.
        waitSerial(s.lastSerial);
        vkResetCommandPool(device_, s.pool, 0);
        s.cmdsUsed = 0;
        // Ring chunks: keep the normal ones, drop the oversize ones (one-off big uploads).
        for (size_t i = 0; i < s.chunks.size();) {
            if (s.chunks[i].size > kChunkSize && s.chunks.size() > 1) {
                destroyBuffer(Buffer{s.chunks[i].buffer});
                s.chunks.erase(s.chunks.begin() + long(i));
            } else {
                s.chunks[i++].used = 0;
            }
        }
        for (VkDescriptorPool p : s.descPools) vkResetDescriptorPool(device_, p, 0);
        s.descPool = 0;
        s.texSets.clear();
        s.uboSets.clear();
        slotReady_ = true;
    }
    cmd_ = nextCommandBuffer(s);
    texDirty_ = uboDirty_ = true;
    texSet_ = uboSet_ = VK_NULL_HANDLE;
    return cmd_ != VK_NULL_HANDLE;
}

VkCommandBuffer VulkanDevice::initCommands() {
    if (!ensureRecording()) return VK_NULL_HANDLE;
    if (!initCmd_) initCmd_ = nextCommandBuffer(slots_[slot_]);
    return initCmd_;
}

void VulkanDevice::submit(VkSemaphore wait, VkSemaphore signal) {
    if (!cmd_) return;
    VkCommandBuffer cmds[2];
    uint32_t count = 0;
    if (initCmd_) {
        vkEndCommandBuffer(initCmd_);
        cmds[count++] = initCmd_;
    }
    vkEndCommandBuffer(cmd_);
    cmds[count++] = cmd_;
    VkFence fence = takeFence();
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = wait ? 1 : 0;
    si.pWaitSemaphores = &wait;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = count;
    si.pCommandBuffers = cmds;
    si.signalSemaphoreCount = signal ? 1 : 0;
    si.pSignalSemaphores = &signal;
    VkResult r = vkQueueSubmit(queue_, 1, &si, fence);
    if (r != VK_SUCCESS) {
        fail(vkError("vkQueueSubmit", r));
        if (r == VK_ERROR_DEVICE_LOST) lost_ = true;
        freeFences_.push_back(fence);
        completed_ = serial_;  // nothing of it will run
    } else {
        inflight_.push_back({serial_, fence});
    }
    slots_[slot_].lastSerial = serial_;
    ++serial_;
    cmd_ = initCmd_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------- recording helpers

void VulkanDevice::fullBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0, nullptr, 0, nullptr);
}

void VulkanDevice::transition(VkCommandBuffer cmd, const VTexture& t, VkImageLayout from, VkImageLayout to, int mip, int mips, int layer,
                              int layers) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.image;
    b.subresourceRange = {t.aspect, uint32_t(mip), uint32_t(mips), uint32_t(layer), uint32_t(layers)};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}

VkImageView VulkanDevice::rtView(VTexture& t, int mip, int layer) {
    const size_t i = size_t(mip) * size_t(t.layers) + size_t(layer);
    if (i >= t.rtViews.size()) return VK_NULL_HANDLE;
    if (!t.rtViews[i]) {
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ci.image = t.image;
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = t.format;
        ci.subresourceRange = {t.aspect, uint32_t(mip), 1, uint32_t(layer), 1};
        vkCreateImageView(device_, &ci, nullptr, &t.rtViews[i]);
    }
    return t.rtViews[i];
}

BufferRange VulkanDevice::ringAlloc(const void* data, size_t size, size_t align, size_t reserve) {
    if (!ensureRecording()) return {};
    Slot& s = slots_[slot_];
    const size_t need = std::max(size, reserve);
    for (Chunk& c : s.chunks) {
        const size_t at = (c.used + align - 1) / align * align;
        if (at + need <= c.size) {
            c.used = at + size;
            VBuffer* b = buffers_.get(c.buffer);
            if (data) std::memcpy(b->mapped + at, data, size);
            return BufferRange{Buffer{c.buffer}, at, size};
        }
    }
    // A new chunk: host-visible, mapped for good, every usage.
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = std::max(kChunkSize, need);
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VBuffer b;
    VmaAllocationInfo info{};
    if (VkResult r = vmaCreateBuffer(vma_, &bi, &ai, &b.buffer, &b.alloc, &info); r != VK_SUCCESS) {
        fail(vkError("transient buffer", r));
        return {};
    }
    b.mapped = static_cast<uint8_t*>(info.pMappedData);
    b.desc.size = size_t(bi.size);
    b.desc.dynamic = true;
    b.desc.usage = kVertexBuffer | kIndexBuffer | kUniformBuffer;
    Chunk c;
    c.buffer = buffers_.add(b);
    c.size = size_t(bi.size);
    s.chunks.push_back(c);
    return ringAlloc(data, size, align, reserve);
}

VkDescriptorSet VulkanDevice::allocSet(VkDescriptorSetLayout layout) {
    Slot& s = slots_[slot_];
    for (;;) {
        if (s.descPool == s.descPools.size()) {
            VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 * kMaxTextureSlots},
                                             {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64 * kMaxUniformSlots}};
            VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            ci.maxSets = 320;
            ci.poolSizeCount = 2;
            ci.pPoolSizes = sizes;
            VkDescriptorPool p = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(device_, &ci, nullptr, &p) != VK_SUCCESS) return VK_NULL_HANDLE;
            s.descPools.push_back(p);
        }
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = s.descPools[s.descPool];
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult r = vkAllocateDescriptorSets(device_, &ai, &set);
        if (r == VK_SUCCESS) return set;
        if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL) return VK_NULL_HANDLE;
        ++s.descPool;  // full: the next pool
    }
}

// The descriptor sets of the bound textures (sets 0 and 2) and uniform
// buffers (sets 1 and 3), made on change and cached for the frame slot.
void VulkanDevice::flushBindings() {
    Slot& s = slots_[slot_];
    if (texDirty_) {
        std::array<uint64_t, kMaxTextureSlots> key{};
        const VTexture* tex[kMaxTextureSlots];
        VkSampler smp[kMaxTextureSlots];
        for (int i = 0; i < kMaxTextureSlots; ++i) {
            const VTexture* t = textures_.get(textures_bound_[i].texture.id);
            if (!t || !t->view) t = textures_.get(dummyTexture_.id);
            uint32_t sid = textures_bound_[i].sampler.id;
            if (!sid || sid > samplers_.size()) sid = dummySampler_.id;
            tex[i] = t;
            smp[i] = samplers_[sid - 1].second;
            key[size_t(i)] = (t->uid << 16) | sid;
        }
        auto it = s.texSets.find(key);
        if (it == s.texSets.end()) {
            VkDescriptorSet set = allocSet(texLayout_);
            if (!set) return;
            VkDescriptorImageInfo ii[kMaxTextureSlots];
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            for (int i = 0; i < kMaxTextureSlots; ++i) ii[i] = {smp[i], tex[i]->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            w.dstSet = set;
            w.dstBinding = 0;
            w.descriptorCount = kMaxTextureSlots;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = ii;
            vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
            it = s.texSets.emplace(key, set).first;
        }
        texSet_ = it->second;
        texDirty_ = false;
        uboDirty_ = true;  // the four sets are bound together below
    }
    if (uboDirty_) {
        std::array<uint32_t, kMaxUniformSlots> key{};
        for (int i = 0; i < kMaxUniformSlots; ++i) key[size_t(i)] = uboBufferId_[i] ? uboBufferId_[i] : dummyUbo_.id;
        auto it = s.uboSets.find(key);
        if (it == s.uboSets.end()) {
            VkDescriptorSet set = allocSet(uboLayout_);
            if (!set) return;
            VkDescriptorBufferInfo bi[kMaxUniformSlots];
            for (int i = 0; i < kMaxUniformSlots; ++i) bi[i] = {buffers_.get(key[size_t(i)])->buffer, 0, kMaxUniformBytes};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set;
            w.dstBinding = 0;
            w.descriptorCount = kMaxUniformSlots;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            w.pBufferInfo = bi;
            vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
            it = s.uboSets.emplace(key, set).first;
        }
        uboSet_ = it->second;
        uint32_t offsets[2 * kMaxUniformSlots];
        for (int i = 0; i < kMaxUniformSlots; ++i) offsets[i] = offsets[i + kMaxUniformSlots] = uboBufferId_[i] ? uboOffset_[i] : 0;
        VkDescriptorSet sets[4] = {texSet_, uboSet_, texSet_, uboSet_};
        vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 4, sets, 2 * kMaxUniformSlots, offsets);
        uboDirty_ = false;
    }
}

// ---------------------------------------------------------------- buffers

Buffer VulkanDevice::createBuffer(const BufferDesc& desc, const void* data) {
    if (!desc.size) return {};
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = desc.size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (desc.usage & kVertexBuffer) bi.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (desc.usage & kIndexBuffer) bi.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (desc.usage & kUniformBuffer) bi.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VBuffer b;
    b.desc = desc;
    if (VkResult r = vmaCreateBuffer(vma_, &bi, &ai, &b.buffer, &b.alloc, nullptr); r != VK_SUCCESS) {
        fail(vkError("createBuffer", r));
        return {};
    }
    if (desc.label && debugUtils_) {
        VkDebugUtilsObjectNameInfoEXT n{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        n.objectType = VK_OBJECT_TYPE_BUFFER;
        n.objectHandle = uint64_t(b.buffer);
        n.pObjectName = desc.label;
        vkSetDebugUtilsObjectNameEXT(device_, &n);
    }
    Buffer h{buffers_.add(b)};
    if (data) updateBuffer(h, 0, data, desc.size);
    return h;
}

void VulkanDevice::updateBuffer(Buffer b, size_t offset, const void* data, size_t size) {
    VBuffer* buf = buffers_.get(b.id);
    if (!buf || !size || !data || offset + size > buf->desc.size) return;
    if (inPass_) {
        fail("updateBuffer inside a pass (docs/render.md 2.2: end the pass first)");
        return;
    }
    BufferRange staging = ringAlloc(data, size, 16);
    VBuffer* src = buffers_.get(staging.buffer.id);
    buf = buffers_.get(b.id);
    if (!src || !buf) return;
    // Draws recorded before read the old contents; later ones the new.
    fullBarrier(cmd_);
    VkBufferCopy c{staging.offset, offset, size};
    vkCmdCopyBuffer(cmd_, src->buffer, buf->buffer, 1, &c);
    fullBarrier(cmd_);
}

void VulkanDevice::destroyBuffer(Buffer b) {
    VBuffer* buf = buffers_.get(b.id);
    if (!buf) return;
    VkBuffer vb = buf->buffer;
    VmaAllocation a = buf->alloc;
    VmaAllocator vma = vma_;
    defer([vma, vb, a] { vmaDestroyBuffer(vma, vb, a); });
    buffers_.remove(b.id);
}

BufferRange VulkanDevice::allocTransient(uint8_t usage, const void* data, size_t size) {
    if (!size) return {};
    const size_t align = (usage & kUniformBuffer) ? std::max<size_t>(size_t(props_.limits.minUniformBufferOffsetAlignment), 16) : 16;
    return ringAlloc(data, size, align);
}

// ---------------------------------------------------------------- textures

Texture VulkanDevice::createTexture(const TextureDesc& in) {
    TextureDesc desc = in;
    const VkFormat format = vkFormatOf(desc.format);
    if (format == VK_FORMAT_UNDEFINED || desc.width <= 0 || desc.height <= 0) {
        fail("createTexture: unsupported format or size");
        return {};
    }
    if (desc.mips <= 0) {
        int n = 1;
        for (int s = std::max(desc.width, desc.height); s > 1; s >>= 1) ++n;
        desc.mips = n;
    }
    const bool depth = isDepthFormat(desc.format);
    VTexture t;
    t.format = format;
    t.aspect = depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | (hasStencil(desc.format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)) : VK_IMAGE_ASPECT_COLOR_BIT;
    t.sampleAspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    t.layers = desc.type == TextureType::Cube ? 6 : 1;
    t.samples = desc.samples > 1 ? sampleBits(desc.samples) : VK_SAMPLE_COUNT_1_BIT;
    if (t.samples != VK_SAMPLE_COUNT_1_BIT) desc.mips = 1;
    const VkFormatFeatureFlags ff = features(format);
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.flags = desc.type == TextureType::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {uint32_t(desc.width), uint32_t(desc.height), 1};
    ci.mipLevels = uint32_t(desc.mips);
    ci.arrayLayers = uint32_t(t.layers);
    ci.samples = t.samples;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // Sampled: what the desc asks for, and every 1-sample depth target (the
    // interface's depth formats are always sampleable).
    const bool sampled = t.samples == VK_SAMPLE_COUNT_1_BIT && (desc.usage & kSampledTexture || (depth && desc.usage & kDepthTarget)) &&
                         (ff & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    if (sampled) ci.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if ((desc.usage & kColorTarget) && !depth) ci.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (depth) ci.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (!sampled && !(ci.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))) {
        fail("createTexture: the format can be neither sampled nor rendered on this device");
        return {};
    }
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (desc.usage & (kColorTarget | kDepthTarget) && desc.width * desc.height >= 512 * 512)
        ai.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;  // big render targets: their own memory
    if (VkResult r = vmaCreateImage(vma_, &ci, &ai, &t.image, &t.alloc, nullptr); r != VK_SUCCESS) {
        fail(vkError("createTexture", r));
        return {};
    }
    if (sampled) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image;
        vi.viewType = desc.type == TextureType::Cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {t.sampleAspect, 0, uint32_t(desc.mips), 0, uint32_t(t.layers)};
        vkCreateImageView(device_, &vi, nullptr, &t.view);
        t.rest = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else {
        t.rest = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    t.rtViews.assign(size_t(desc.mips) * size_t(t.layers), VK_NULL_HANDLE);
    t.desc = desc;
    t.uid = nextUid_++;
    if (desc.label && debugUtils_) {
        VkDebugUtilsObjectNameInfoEXT n{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        n.objectType = VK_OBJECT_TYPE_IMAGE;
        n.objectHandle = uint64_t(t.image);
        n.pObjectName = desc.label;
        vkSetDebugUtilsObjectNameEXT(device_, &n);
    }
    // Into its resting layout before anything can use it (submitted ahead of
    // the commands being recorded: nothing recorded so far uses it).
    if (VkCommandBuffer c = initCommands()) {
        if (sampled) {
            // Defined contents (zero), as GL's freshly specified textures read in practice.
            transition(c, t, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, desc.mips, 0, t.layers);
            if (!isCompressedFormat(desc.format)) {
                VkImageSubresourceRange range{t.aspect, 0, uint32_t(desc.mips), 0, uint32_t(t.layers)};
                if (depth) {
                    VkClearDepthStencilValue v{1.0f, 0};
                    vkCmdClearDepthStencilImage(c, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
                } else {
                    VkClearColorValue v{};
                    vkCmdClearColorImage(c, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
                }
            }
            transition(c, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, t.rest, 0, desc.mips, 0, t.layers);
        } else {
            transition(c, t, VK_IMAGE_LAYOUT_UNDEFINED, t.rest, 0, desc.mips, 0, t.layers);
        }
    }
    return Texture{textures_.add(std::move(t))};
}

void VulkanDevice::releaseTextureNow(VTexture& t) {
    for (VkImageView v : t.rtViews)
        if (v) vkDestroyImageView(device_, v, nullptr);
    if (t.view) vkDestroyImageView(device_, t.view, nullptr);
    if (t.image) vmaDestroyImage(vma_, t.image, t.alloc);
    t = VTexture{};
}

void VulkanDevice::destroyTexture(Texture tx) {
    VTexture* t = textures_.get(tx.id);
    if (!t || tx == dummyTexture_) return;
    for (auto& b : textures_bound_)
        if (b.texture == tx) b.texture = {}, texDirty_ = true;
    // Framebuffers that use it go with it.
    std::vector<VkFramebuffer> fbs;
    for (auto it = framebuffers_.begin(); it != framebuffers_.end();) {
        if (std::find(it->second.uids.begin(), it->second.uids.end(), t->uid) != it->second.uids.end()) {
            fbs.push_back(it->second.fb);
            it = framebuffers_.erase(it);
        } else {
            ++it;
        }
    }
    auto moved = std::make_shared<VTexture>(std::move(*t));
    defer([this, moved, fbs] {
        for (VkFramebuffer f : fbs) vkDestroyFramebuffer(device_, f, nullptr);
        releaseTextureNow(*moved);
    });
    textures_.remove(tx.id);
}

void VulkanDevice::updateTexture(const TextureRegion& dst, int w, int h, const void* data, size_t rowBytes) {
    VTexture* t = textures_.get(dst.texture.id);
    if (!t || w <= 0 || h <= 0 || !data || t->samples != VK_SAMPLE_COUNT_1_BIT) return;
    if (inPass_) {
        fail("updateTexture inside a pass (docs/render.md 2.2: end the pass first)");
        return;
    }
    const Format f = t->desc.format;
    const size_t px = formatBytes(f);
    uint32_t rowLength = 0;
    BufferRange staging;
    if (isCompressedFormat(f)) {
        staging = ringAlloc(data, imageBytes(f, w, h), 16);  // whole blocks, tightly packed rows of blocks
    } else {
        const size_t tight = size_t(w) * px;
        if (rowBytes && rowBytes != tight && rowBytes % px == 0) {
            rowLength = uint32_t(rowBytes / px);
            staging = ringAlloc(data, rowBytes * size_t(h - 1) + tight, 16);
        } else if (rowBytes && rowBytes != tight) {
            staging = ringAlloc(nullptr, tight * size_t(h), 16);
            if (VBuffer* sb = buffers_.get(staging.buffer.id))
                for (int y = 0; y < h; ++y)
                    std::memcpy(sb->mapped + staging.offset + tight * size_t(y), static_cast<const uint8_t*>(data) + rowBytes * size_t(y), tight);
        } else {
            staging = ringAlloc(data, tight * size_t(h), 16);
        }
    }
    VBuffer* sb = buffers_.get(staging.buffer.id);
    t = textures_.get(dst.texture.id);
    if (!sb || !t) return;
    transition(cmd_, *t, t->rest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst.mip, 1, dst.layer, 1);
    VkBufferImageCopy c{};
    c.bufferOffset = staging.offset;
    c.bufferRowLength = rowLength;
    c.imageSubresource = {t->sampleAspect, uint32_t(dst.mip), uint32_t(dst.layer), 1};
    c.imageOffset = {dst.x, dst.y, 0};
    c.imageExtent = {uint32_t(w), uint32_t(h), 1};
    vkCmdCopyBufferToImage(cmd_, sb->buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    transition(cmd_, *t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, t->rest, dst.mip, 1, dst.layer, 1);
}

void VulkanDevice::generateMipmaps(Texture tx) {
    VTexture* t = textures_.get(tx.id);
    if (!t || t->desc.mips <= 1 || inPass_ || isDepthFormat(t->desc.format) || isCompressedFormat(t->desc.format) || !ensureRecording()) return;
    const VkFormatFeatureFlags ff = features(t->format);
    if (!(ff & VK_FORMAT_FEATURE_BLIT_SRC_BIT) || !(ff & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        fail("generateMipmaps: the format cannot be blitted on this device");
        return;
    }
    const VkFilter filter = (ff & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    const int mips = t->desc.mips, layers = t->layers;
    transition(cmd_, *t, t->rest, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 1, 0, layers);
    transition(cmd_, *t, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, mips - 1, 0, layers);
    for (int m = 1; m < mips; ++m) {
        VkImageBlit b{};
        b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(m - 1), 0, uint32_t(layers)};
        b.srcOffsets[1] = {std::max(1, t->desc.width >> (m - 1)), std::max(1, t->desc.height >> (m - 1)), 1};
        b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(m), 0, uint32_t(layers)};
        b.dstOffsets[1] = {std::max(1, t->desc.width >> m), std::max(1, t->desc.height >> m), 1};
        vkCmdBlitImage(cmd_, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, filter);
        transition(cmd_, *t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m, 1, 0, layers);
    }
    transition(cmd_, *t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->rest, 0, mips, 0, layers);
}

Sampler VulkanDevice::createSampler(const SamplerDesc& d) {
    for (size_t i = 0; i < samplers_.size(); ++i)
        if (samplers_[i].first == d) return Sampler{uint32_t(i + 1)};
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = d.magFilter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.minFilter = d.minFilter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.mipmapMode = d.mipFilter == MipFilter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = vkWrap(d.wrapU);
    ci.addressModeV = vkWrap(d.wrapV);
    ci.addressModeW = vkWrap(d.wrapW);
    ci.mipLodBias = d.lodBias;
    if (d.mipFilter == MipFilter::None) {
        // GL's non-mipmapped minification: level 0 only (the usual 0..0.25
        // clamp keeps the magnification / minification choice).
        ci.minLod = 0, ci.maxLod = 0.25f;
    } else {
        ci.minLod = std::max(d.minLod, 0.0f);
        ci.maxLod = std::max(d.maxLod, ci.minLod);
    }
    if (d.maxAnisotropy > 1 && anisotropy_) {
        ci.anisotropyEnable = VK_TRUE;
        ci.maxAnisotropy = std::min(d.maxAnisotropy, props_.limits.maxSamplerAnisotropy);
    }
    if (d.compare) {
        ci.compareEnable = VK_TRUE;
        ci.compareOp = vkCompare(d.compareOp);
    }
    static const VkBorderColor borders[] = {VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
                                            VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE};
    ci.borderColor = borders[size_t(d.border)];
    VkSampler s = VK_NULL_HANDLE;
    if (VkResult r = vkCreateSampler(device_, &ci, nullptr, &s); r != VK_SUCCESS) {
        fail(vkError("createSampler", r));
        return {};
    }
    samplers_.push_back({d, s});
    return Sampler{uint32_t(samplers_.size())};
}

// ---------------------------------------------------------------- shaders, pipelines

Shader VulkanDevice::createShader(const ShaderDesc& d) {
    if (!d.spirvVertex || !d.spirvFragment || !d.spirvVertexBytes || !d.spirvFragmentBytes) {
        fail(std::string("shader ") + d.name + ": no SPIR-V (gdcore_spirv_shaders, docs/render.md 3.3; this backend takes SPIR-V)");
        return {};
    }
    VShader s;
    s.name = d.name ? d.name : "";
    std::string why;
    if (!reflectSpirv(d.spirvVertex, d.spirvVertexBytes, s.vi, why) || s.vi.stage != SpirvStage::Vertex) {
        fail("shader " + s.name + ": vertex SPIR-V: " + (why.empty() ? "not a vertex shader" : why));
        return {};
    }
    if (!reflectSpirv(d.spirvFragment, d.spirvFragmentBytes, s.fi, why) || s.fi.stage != SpirvStage::Fragment) {
        fail("shader " + s.name + ": fragment SPIR-V: " + (why.empty() ? "not a fragment shader" : why));
        return {};
    }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = d.spirvVertexBytes;
    ci.pCode = d.spirvVertex;
    if (VkResult r = vkCreateShaderModule(device_, &ci, nullptr, &s.vs); r != VK_SUCCESS) {
        fail("shader " + s.name + ": " + vkError("vkCreateShaderModule", r));
        return {};
    }
    ci.codeSize = d.spirvFragmentBytes;
    ci.pCode = d.spirvFragment;
    if (VkResult r = vkCreateShaderModule(device_, &ci, nullptr, &s.fs); r != VK_SUCCESS) {
        vkDestroyShaderModule(device_, s.vs, nullptr);
        fail("shader " + s.name + ": " + vkError("vkCreateShaderModule", r));
        return {};
    }
    return Shader{shaders_.add(std::move(s))};
}

void VulkanDevice::destroyShader(Shader sh) {
    VShader* s = shaders_.get(sh.id);
    if (!s) return;
    // Pipelines keep their own copy of the code: the modules can go now.
    vkDestroyShaderModule(device_, s->vs, nullptr);
    vkDestroyShaderModule(device_, s->fs, nullptr);
    shaders_.remove(sh.id);
}

// A render pass for the attachments (the render-pass path). Pipelines use one
// made with pass == nullptr: compatibility only looks at the formats and the
// samples (and, for a single subpass, not at the resolve attachments).
VkRenderPass VulkanDevice::renderPassFor(const PassDesc* pass, const VkFormat* colors, int colorCount, VkFormat depth,
                                         VkSampleCountFlagBits samples) {
    std::string key;
    auto put = [&key](uint32_t v) { key.append(reinterpret_cast<const char*>(&v), sizeof v); };
    put(uint32_t(colorCount)), put(uint32_t(depth)), put(uint32_t(samples)), put(pass ? 1 : 0);
    for (int i = 0; i < colorCount; ++i) {
        put(uint32_t(colors[i]));
        if (pass) put(uint32_t(pass->colors[i].load)), put(uint32_t(pass->colors[i].store)), put(pass->colors[i].resolve ? 1 : 0);
    }
    if (pass) put(uint32_t(pass->depth.load)), put(uint32_t(pass->depth.store)), put(uint32_t(pass->depth.stencilLoad)), put(uint32_t(pass->depth.stencilStore));
    if (auto it = renderPasses_.find(key); it != renderPasses_.end()) return it->second;

    auto load = [](LoadOp op) {
        return op == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : op == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    };
    auto store = [](StoreOp op) { return op == StoreOp::Store || op == StoreOp::ResolveAndStore ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE; };
    std::vector<VkAttachmentDescription> att;
    VkAttachmentReference colorRefs[kMaxColorTargets], resolveRefs[kMaxColorTargets], depthRef{};
    bool anyResolve = false;
    for (int i = 0; i < colorCount; ++i) {
        VkAttachmentDescription a{};
        a.format = colors[i];
        a.samples = samples;
        a.loadOp = pass ? load(pass->colors[i].load) : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = pass ? store(pass->colors[i].store) : VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs[i] = {uint32_t(att.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        att.push_back(a);
    }
    if (depth != VK_FORMAT_UNDEFINED) {
        VkAttachmentDescription a{};
        a.format = depth;
        a.samples = samples;
        a.loadOp = pass ? load(pass->depth.load) : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = pass ? store(pass->depth.store) : VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = pass ? load(pass->depth.stencilLoad) : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.stencilStoreOp = pass ? store(pass->depth.stencilStore) : VK_ATTACHMENT_STORE_OP_STORE;
        a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthRef = {uint32_t(att.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        att.push_back(a);
    }
    for (int i = 0; i < colorCount; ++i) {
        resolveRefs[i] = {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
        if (pass && pass->colors[i].resolve &&
            (pass->colors[i].store == StoreOp::Resolve || pass->colors[i].store == StoreOp::ResolveAndStore) && samples != VK_SAMPLE_COUNT_1_BIT) {
            VkAttachmentDescription a{};
            a.format = colors[i];
            a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            resolveRefs[i] = {uint32_t(att.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            att.push_back(a);
            anyResolve = true;
        }
    }
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = uint32_t(colorCount);
    sp.pColorAttachments = colorRefs;
    sp.pResolveAttachments = anyResolve ? resolveRefs : nullptr;
    sp.pDepthStencilAttachment = depth != VK_FORMAT_UNDEFINED ? &depthRef : nullptr;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = uint32_t(att.size());
    ci.pAttachments = att.data();
    ci.subpassCount = 1;
    ci.pSubpasses = &sp;
    VkRenderPass rp = VK_NULL_HANDLE;
    if (VkResult r = vkCreateRenderPass(device_, &ci, nullptr, &rp); r != VK_SUCCESS) {
        fail(vkError("vkCreateRenderPass", r));
        return VK_NULL_HANDLE;
    }
    renderPasses_[key] = rp;
    return rp;
}

Pipeline VulkanDevice::createPipeline(const PipelineDesc& d) {
    const VShader* s = shaders_.get(d.shader.id);
    if (!s) {
        fail("createPipeline: no shader");
        return {};
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, s->vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, s->fs, "main", nullptr};

    VkVertexInputBindingDescription binds[kMaxVertexBuffers];
    VkVertexInputAttributeDescription attrs[kMaxVertexAttributes];
    const int nb = std::clamp(d.layout.bufferCount, 0, kMaxVertexBuffers), na = std::clamp(d.layout.attributeCount, 0, kMaxVertexAttributes);
    for (int i = 0; i < nb; ++i)
        binds[i] = {uint32_t(i), d.layout.buffers[i].stride, d.layout.buffers[i].perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
    for (int i = 0; i < na; ++i) {
        const VertexAttribute& a = d.layout.attributes[i];
        attrs[i] = {a.location, a.buffer, vkVertexFormat(a.format), a.offset};
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = uint32_t(nb);
    vi.pVertexBindingDescriptions = binds;
    vi.vertexAttributeDescriptionCount = uint32_t(na);
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = vkTopology(d.primitive);
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.depthClampEnable = d.raster.depthClamp && depthClamp_;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = d.raster.cull == CullMode::None ? VK_CULL_MODE_NONE : d.raster.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
    // The negative-height viewport keeps GL's winding (docs/render.md 3.2):
    // the front face goes through unchanged, as in SDL_GPU's Vulkan driver.
    rs.frontFace = d.raster.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rs.depthBiasEnable = d.raster.depthBiasConstant != 0 || d.raster.depthBiasSlope != 0;
    rs.depthBiasConstantFactor = d.raster.depthBiasConstant;  // glPolygonOffset units
    rs.depthBiasSlopeFactor = d.raster.depthBiasSlope;        // glPolygonOffset factor
    rs.lineWidth = 1.0f;
    const VkSampleCountFlagBits samples = sampleBits(d.samples);
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = samples;
    const bool hasDepth = d.depthFormat != Format::Undefined;
    const DepthStencilState& ds = d.depthStencil;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    // GL writes depth with the test disabled only through an Always test: the same here.
    dss.depthTestEnable = hasDepth && (ds.depthTest || ds.depthWrite);
    dss.depthWriteEnable = hasDepth && ds.depthWrite;
    dss.depthCompareOp = ds.depthTest ? vkCompare(ds.depthCompare) : VK_COMPARE_OP_ALWAYS;
    dss.stencilTestEnable = hasDepth && hasStencil(d.depthFormat) && ds.stencil;
    auto face = [&](const StencilFace& f) {
        VkStencilOpState o{};
        o.failOp = vkStencilOp(f.fail);
        o.passOp = vkStencilOp(f.pass);
        o.depthFailOp = vkStencilOp(f.depthFail);
        o.compareOp = vkCompare(f.compare);
        o.compareMask = ds.readMask;
        o.writeMask = ds.writeMask;
        return o;
    };
    dss.front = face(ds.front);
    dss.back = face(ds.back);
    dss.minDepthBounds = 0, dss.maxDepthBounds = 1;
    VkPipelineColorBlendAttachmentState cb[kMaxColorTargets]{};
    const int nc = std::clamp(d.colorCount, 0, kMaxColorTargets);
    for (int i = 0; i < nc; ++i) {
        const BlendState& b = d.blend[independentBlend_ ? i : 0];  // without independentBlend: target 0's, as GLES
        cb[i].blendEnable = b.enable;
        cb[i].srcColorBlendFactor = vkBlendFactor(b.srcColor);
        cb[i].dstColorBlendFactor = vkBlendFactor(b.dstColor);
        cb[i].colorBlendOp = vkBlendOp(b.colorOp);
        cb[i].srcAlphaBlendFactor = vkBlendFactor(b.srcAlpha);
        cb[i].dstAlphaBlendFactor = vkBlendFactor(b.dstAlpha);
        cb[i].alphaBlendOp = vkBlendOp(b.alphaOp);
        cb[i].colorWriteMask = b.writeMask & 15;
    }
    VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbs.attachmentCount = uint32_t(nc);
    cbs.pAttachments = cb;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
                                  VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    VkPipelineDynamicStateCreateInfo dys{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dys.dynamicStateCount = 4;
    dys.pDynamicStates = dyn;

    VkFormat colors[kMaxColorTargets];
    for (int i = 0; i < nc; ++i) colors[i] = vkFormatOf(d.colorFormats[i]);
    const VkFormat depth = hasDepth ? vkFormatOf(d.depthFormat) : VK_FORMAT_UNDEFINED;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    VkPipelineRenderingCreateInfoKHR ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    if (dynamicRendering_) {
        ri.colorAttachmentCount = uint32_t(nc);
        ri.pColorAttachmentFormats = colors;
        ri.depthAttachmentFormat = depth;
        ri.stencilAttachmentFormat = hasDepth && hasStencil(d.depthFormat) ? depth : VK_FORMAT_UNDEFINED;
        ci.pNext = &ri;
    } else {
        ci.renderPass = renderPassFor(nullptr, colors, nc, depth, samples);
        if (!ci.renderPass) return {};
    }
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &dss;
    ci.pColorBlendState = &cbs;
    ci.pDynamicState = &dys;
    ci.layout = pipelineLayout_;
    VPipeline p;
    p.desc = d;
    if (VkResult r = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &ci, nullptr, &p.pipeline); r != VK_SUCCESS) {
        fail(std::string("createPipeline (") + (d.label ? d.label : s->name.c_str()) + "): " + vkError("vkCreateGraphicsPipelines", r));
        return {};
    }
    return Pipeline{pipelines_.add(p)};
}

void VulkanDevice::destroyPipeline(Pipeline p) {
    VPipeline* pl = pipelines_.get(p.id);
    if (!pl) return;
    if (pipelineId_ == p.id) pipeline_ = nullptr, pipelineId_ = 0;
    VkPipeline vp = pl->pipeline;
    VkDevice dev = device_;
    defer([dev, vp] { vkDestroyPipeline(dev, vp, nullptr); });
    pipelines_.remove(p.id);
}

// ---------------------------------------------------------------- copies, readback

void VulkanDevice::copyTexture(const TextureRegion& src, const TextureRegion& dst, int w, int h) {
    VTexture* s = textures_.get(src.texture.id);
    VTexture* d = textures_.get(dst.texture.id);
    if (!s || !d || inPass_ || w <= 0 || h <= 0 || !ensureRecording()) return;
    const bool same = src.texture == dst.texture && src.mip == dst.mip && src.layer == dst.layer;
    const VkImageLayout sl = same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkImageLayout dl = same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition(cmd_, *s, s->rest, sl, src.mip, 1, src.layer, 1);
    if (!same) transition(cmd_, *d, d->rest, dl, dst.mip, 1, dst.layer, 1);
    VkImageCopy c{};
    c.srcSubresource = {s->aspect, uint32_t(src.mip), uint32_t(src.layer), 1};
    c.srcOffset = {src.x, src.y, 0};
    c.dstSubresource = {d->aspect, uint32_t(dst.mip), uint32_t(dst.layer), 1};
    c.dstOffset = {dst.x, dst.y, 0};
    c.extent = {uint32_t(w), uint32_t(h), 1};
    vkCmdCopyImage(cmd_, s->image, sl, d->image, dl, 1, &c);
    transition(cmd_, *s, sl, s->rest, src.mip, 1, src.layer, 1);
    if (!same) transition(cmd_, *d, dl, d->rest, dst.mip, 1, dst.layer, 1);
}

void VulkanDevice::blit(const TextureRegion& src, int sw, int sh, const TextureRegion& dst, int dw, int dh, Filter filter) {
    VTexture* s = textures_.get(src.texture.id);
    VTexture* d = textures_.get(dst.texture.id);
    if (!s || !d || inPass_ || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || !ensureRecording()) return;
    if (s->samples != VK_SAMPLE_COUNT_1_BIT || d->samples != VK_SAMPLE_COUNT_1_BIT) {
        fail("blit: MSAA textures cannot be blitted (resolve them with the pass)");
        return;
    }
    const bool same = src.texture == dst.texture && src.mip == dst.mip && src.layer == dst.layer;
    const VkImageLayout sl = same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkImageLayout dl = same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    const bool linear = filter == Filter::Linear && (features(s->format) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) && !isDepthFormat(s->desc.format);
    transition(cmd_, *s, s->rest, sl, src.mip, 1, src.layer, 1);
    if (!same) transition(cmd_, *d, d->rest, dl, dst.mip, 1, dst.layer, 1);
    VkImageBlit b{};
    b.srcSubresource = {s->aspect, uint32_t(src.mip), uint32_t(src.layer), 1};
    b.srcOffsets[0] = {src.x, src.y, 0};
    b.srcOffsets[1] = {src.x + sw, src.y + sh, 1};
    b.dstSubresource = {d->aspect, uint32_t(dst.mip), uint32_t(dst.layer), 1};
    b.dstOffsets[0] = {dst.x, dst.y, 0};
    b.dstOffsets[1] = {dst.x + dw, dst.y + dh, 1};
    vkCmdBlitImage(cmd_, s->image, sl, d->image, dl, 1, &b, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    transition(cmd_, *s, sl, s->rest, src.mip, 1, src.layer, 1);
    if (!same) transition(cmd_, *d, dl, d->rest, dst.mip, 1, dst.layer, 1);
}

Readback VulkanDevice::readTexture(Texture tx, const Rect& r) {
    VTexture* t = textures_.get(tx.id);
    if (!t || inPass_ || r.w <= 0 || r.h <= 0 || isDepthFormat(t->desc.format) || isCompressedFormat(t->desc.format) ||
        t->samples != VK_SAMPLE_COUNT_1_BIT || !ensureRecording())
        return {};
    VReadback rb;
    rb.size = size_t(r.w) * size_t(r.h) * formatBytes(t->desc.format);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = rb.size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    if (VkResult res = vmaCreateBuffer(vma_, &bi, &ai, &rb.buffer, &rb.alloc, nullptr); res != VK_SUCCESS) {
        fail(vkError("readTexture", res));
        return {};
    }
    transition(cmd_, *t, t->rest, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 1, 0, 1);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageOffset = {r.x, r.y, 0};
    c.imageExtent = {uint32_t(r.w), uint32_t(r.h), 1};
    // Rows come out top-down: the image's row 0 is the top (the viewport flip).
    vkCmdCopyImageToBuffer(cmd_, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buffer, 1, &c);
    transition(cmd_, *t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->rest, 0, 1, 0, 1);
    VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
    rb.serial = serial_;
    // Sent now (GL's glFlush after its glReadPixels into a PBO): readbackReady
    // can turn true without waiting for the end of the frame.
    submit();
    return Readback{readbacks_.add(rb)};
}

bool VulkanDevice::readbackReady(Readback r) {
    VReadback* rb = readbacks_.get(r.id);
    if (!rb) return false;
    poll();
    return completed_ >= rb->serial;
}

bool VulkanDevice::readbackData(Readback r, void* dst, size_t size) {
    VReadback* rb = readbacks_.get(r.id);
    if (!rb) return false;
    bool ok = waitSerial(rb->serial);
    rb = readbacks_.get(r.id);
    if (ok) {
        vmaInvalidateAllocation(vma_, rb->alloc, 0, VK_WHOLE_SIZE);
        void* p = nullptr;
        if (vmaMapMemory(vma_, rb->alloc, &p) == VK_SUCCESS) {
            std::memcpy(dst, p, std::min(size, rb->size));
            vmaUnmapMemory(vma_, rb->alloc);
        } else {
            ok = false;
        }
    }
    vmaDestroyBuffer(vma_, rb->buffer, rb->alloc);
    readbacks_.remove(r.id);
    return ok;
}

// ---------------------------------------------------------------- frames and passes

void VulkanDevice::beginFrame() {
    poll();
    ensureRecording();
    // Pushed uniforms last for the frame (docs/render.md 2.2).
    for (int i = 0; i < kMaxUniformSlots; ++i) uboBuffer_[i] = VK_NULL_HANDLE, uboBufferId_[i] = 0, uboOffset_[i] = 0;
    uboDirty_ = true;
}

void VulkanDevice::beginPass(const PassDesc& p) {
    if (inPass_) endPass();
    if (!ensureRecording()) return;
    pass_ = p;
    passW_ = passH_ = 0;
    VkFormat colorFormats[kMaxColorTargets];
    VkImageView views[kMaxColorTargets] = {}, resolveViews[kMaxColorTargets] = {};
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    std::vector<uint64_t> uids;
    const int nc = std::clamp(p.colorCount, 0, kMaxColorTargets);
    for (int i = 0; i < nc; ++i) {
        VTexture* t = textures_.get(p.colors[i].texture.id);
        if (!t) {
            fail("beginPass: a colour attachment is not a texture");
            return;
        }
        views[i] = rtView(*t, p.colors[i].mip, p.colors[i].layer);
        colorFormats[i] = t->format;
        samples = t->samples;
        uids.push_back(t->uid);
        const int w = std::max(1, t->desc.width >> p.colors[i].mip), h = std::max(1, t->desc.height >> p.colors[i].mip);
        passW_ = passW_ ? std::min(passW_, w) : w, passH_ = passH_ ? std::min(passH_, h) : h;
        // Clear / DontCare discard the old contents: from UNDEFINED (cheaper on tilers).
        const VkImageLayout from = p.colors[i].load == LoadOp::Load ? t->rest : VK_IMAGE_LAYOUT_UNDEFINED;
        transition(cmd_, *t, from, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, p.colors[i].mip, 1, p.colors[i].layer, 1);
        if (p.colors[i].resolve && (p.colors[i].store == StoreOp::Resolve || p.colors[i].store == StoreOp::ResolveAndStore) &&
            t->samples != VK_SAMPLE_COUNT_1_BIT) {
            if (VTexture* rt = textures_.get(p.colors[i].resolve.id)) {
                resolveViews[i] = rtView(*rt, 0, 0);
                uids.push_back(rt->uid);
                transition(cmd_, *rt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 1, 0, 1);
            }
        }
    }
    VTexture* dt = textures_.get(p.depth.texture.id);
    VkImageView depthView = VK_NULL_HANDLE;
    if (dt) {
        depthView = rtView(*dt, 0, 0);
        uids.push_back(dt->uid);
        if (!nc) passW_ = dt->desc.width, passH_ = dt->desc.height, samples = dt->samples;
        else passW_ = std::min(passW_, dt->desc.width), passH_ = std::min(passH_, dt->desc.height);
        const bool stencil = hasStencil(dt->desc.format);
        const bool keep = p.depth.load == LoadOp::Load || (stencil && p.depth.stencilLoad == LoadOp::Load);
        transition(cmd_, *dt, keep ? dt->rest : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, 1, 0, 1);
    }
    if (!passW_ || !passH_) {
        fail("beginPass: no attachment");
        return;
    }
    auto load = [](LoadOp op) {
        return op == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : op == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    };
    auto store = [](StoreOp op) { return op == StoreOp::Store || op == StoreOp::ResolveAndStore ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE; };
    const VkRect2D area{{0, 0}, {uint32_t(passW_), uint32_t(passH_)}};
    if (dynamicRendering_) {
        VkRenderingAttachmentInfoKHR ca[kMaxColorTargets]{};
        for (int i = 0; i < nc; ++i) {
            ca[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
            ca[i].imageView = views[i];
            ca[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ca[i].loadOp = load(p.colors[i].load);
            ca[i].storeOp = store(p.colors[i].store);
            std::memcpy(ca[i].clearValue.color.float32, p.colors[i].clear, sizeof p.colors[i].clear);
            if (resolveViews[i]) {
                ca[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                ca[i].resolveImageView = resolveViews[i];
                ca[i].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
        }
        VkRenderingAttachmentInfoKHR da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR}, sa{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
        VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
        ri.renderArea = area;
        ri.layerCount = 1;
        ri.colorAttachmentCount = uint32_t(nc);
        ri.pColorAttachments = ca;
        if (dt) {
            da.imageView = depthView;
            da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            da.loadOp = load(p.depth.load);
            da.storeOp = store(p.depth.store);
            da.clearValue.depthStencil = {p.depth.clearDepth, p.depth.clearStencil};
            ri.pDepthAttachment = &da;
            if (hasStencil(dt->desc.format)) {
                sa = da;
                sa.loadOp = load(p.depth.stencilLoad);
                sa.storeOp = store(p.depth.stencilStore);
                ri.pStencilAttachment = &sa;
            }
        }
        cmdBeginRendering_(cmd_, &ri);
    } else {
        VkRenderPass rp = renderPassFor(&p, colorFormats, nc, dt ? dt->format : VK_FORMAT_UNDEFINED, samples);
        if (!rp) return;
        // The framebuffer: attachments in the render pass's order (colours, depth, resolves).
        std::vector<VkImageView> att(views, views + nc);
        if (dt) att.push_back(depthView);
        for (int i = 0; i < nc; ++i)
            if (resolveViews[i]) att.push_back(resolveViews[i]);
        std::string key;
        auto put = [&key](uint64_t v) { key.append(reinterpret_cast<const char*>(&v), sizeof v); };
        put(uint64_t(rp)), put(uint64_t(passW_)), put(uint64_t(passH_));
        for (VkImageView v : att) put(uint64_t(v));
        auto it = framebuffers_.find(key);
        if (it == framebuffers_.end()) {
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = rp;
            fi.attachmentCount = uint32_t(att.size());
            fi.pAttachments = att.data();
            fi.width = uint32_t(passW_), fi.height = uint32_t(passH_), fi.layers = 1;
            Fb fb;
            fb.uids = uids;
            if (VkResult r = vkCreateFramebuffer(device_, &fi, nullptr, &fb.fb); r != VK_SUCCESS) {
                fail(vkError("vkCreateFramebuffer", r));
                return;
            }
            it = framebuffers_.emplace(key, fb).first;
        }
        VkClearValue clears[2 * kMaxColorTargets + 1]{};
        for (int i = 0; i < nc; ++i) std::memcpy(clears[i].color.float32, p.colors[i].clear, sizeof p.colors[i].clear);
        if (dt) clears[nc].depthStencil = {p.depth.clearDepth, p.depth.clearStencil};
        VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        bi.renderPass = rp;
        bi.framebuffer = it->second.fb;
        bi.renderArea = area;
        bi.clearValueCount = uint32_t(att.size());
        bi.pClearValues = clears;
        vkCmdBeginRenderPass(cmd_, &bi, VK_SUBPASS_CONTENTS_INLINE);
    }
    inPass_ = true;
    // The state a pass starts with (docs/render.md 2.2).
    setViewport({0, 0, passW_, passH_}, 0, 1);
    setScissor({0, 0, passW_, passH_});
    setStencilRef(0);
    const float zero[4] = {0, 0, 0, 0};
    setBlendConstants(zero);
    pipeline_ = nullptr, pipelineId_ = 0;
    for (auto& b : textures_bound_) b = {};
    texDirty_ = uboDirty_ = true;
}

void VulkanDevice::setViewport(const Rect& r, float minDepth, float maxDepth) {
    if (!inPass_ || r.w <= 0 || r.h <= 0) return;
    // Negative height (core 1.1): NDC y = +1 lands on row 0, the top (docs/render.md 3.2).
    VkViewport v{float(r.x), float(r.y + r.h), float(r.w), -float(r.h), minDepth, maxDepth};
    vkCmdSetViewport(cmd_, 0, 1, &v);
}

void VulkanDevice::setScissor(const Rect& r) {
    if (!inPass_) return;
    int x = r.x, y = r.y, w = std::max(0, r.w), h = std::max(0, r.h);
    if (x < 0) w = std::max(0, w + x), x = 0;  // Vulkan wants non-negative offsets
    if (y < 0) h = std::max(0, h + y), y = 0;
    VkRect2D s{{x, y}, {uint32_t(w), uint32_t(h)}};
    vkCmdSetScissor(cmd_, 0, 1, &s);
}

void VulkanDevice::setStencilRef(uint8_t ref) {
    if (inPass_) vkCmdSetStencilReference(cmd_, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
}

void VulkanDevice::setBlendConstants(const float c[4]) {
    if (inPass_) vkCmdSetBlendConstants(cmd_, c);
}

void VulkanDevice::bindPipeline(Pipeline p) {
    const VPipeline* pl = pipelines_.get(p.id);
    if (!pl || !inPass_ || pipelineId_ == p.id) return;
    pipeline_ = pl, pipelineId_ = p.id;
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl->pipeline);
}

void VulkanDevice::bindVertexBuffers(int first, const BufferRange* b, int count) {
    if (!inPass_) return;
    VkBuffer bufs[kMaxVertexBuffers];
    VkDeviceSize offs[kMaxVertexBuffers];
    int n = 0;
    for (int i = 0; i < count && first + i < kMaxVertexBuffers; ++i, ++n) {
        const VBuffer* buf = buffers_.get(b[i].buffer.id);
        bufs[i] = buf ? buf->buffer : buffers_.get(dummyUbo_.id)->buffer;
        offs[i] = buf ? b[i].offset : 0;
    }
    if (n) vkCmdBindVertexBuffers(cmd_, uint32_t(first), uint32_t(n), bufs, offs);
}

void VulkanDevice::bindIndexBuffer(const BufferRange& b, IndexType type) {
    const VBuffer* buf = buffers_.get(b.buffer.id);
    if (!inPass_ || !buf) return;
    vkCmdBindIndexBuffer(cmd_, buf->buffer, b.offset, type == IndexType::U16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanDevice::bindTextures(int first, const TextureBinding* b, int count) {
    for (int i = 0; i < count && first + i < kMaxTextureSlots; ++i)
        if (first + i >= 0) textures_bound_[first + i] = b[i];
    texDirty_ = true;
}

void VulkanDevice::pushUniforms(int slot, const void* data, size_t size) {
    if (slot < 0 || slot >= kMaxUniformSlots || !size || !data) return;
    size = std::min(size, kMaxUniformBytes);
    const size_t align = std::max<size_t>(size_t(props_.limits.minUniformBufferOffsetAlignment), 16);
    // The descriptor's range is kMaxUniformBytes from the dynamic offset: keep that much room.
    BufferRange r = ringAlloc(data, size, align, kMaxUniformBytes);
    const VBuffer* b = buffers_.get(r.buffer.id);
    if (!b) return;
    uboBuffer_[slot] = b->buffer;
    uboBufferId_[slot] = r.buffer.id;
    uboOffset_[slot] = uint32_t(r.offset);
    uboDirty_ = true;
}

void VulkanDevice::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (!inPass_ || !pipeline_ || !vertexCount) return;
    flushBindings();
    vkCmdDraw(cmd_, vertexCount, std::max(instanceCount, 1u), firstVertex, firstInstance);
}

void VulkanDevice::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!inPass_ || !pipeline_ || !indexCount) return;
    flushBindings();
    vkCmdDrawIndexed(cmd_, indexCount, std::max(instanceCount, 1u), firstIndex, vertexOffset, firstInstance);
}

void VulkanDevice::endPass() {
    if (!inPass_) return;
    inPass_ = false;
    if (dynamicRendering_) cmdEndRendering_(cmd_);
    else vkCmdEndRenderPass(cmd_);
    // Every attachment back to its resting layout.
    for (int i = 0; i < std::clamp(pass_.colorCount, 0, kMaxColorTargets); ++i) {
        const ColorAttachment& c = pass_.colors[i];
        if (const VTexture* t = textures_.get(c.texture.id)) {
            transition(cmd_, *t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, t->rest, c.mip, 1, c.layer, 1);
            if (c.resolve && (c.store == StoreOp::Resolve || c.store == StoreOp::ResolveAndStore) && t->samples != VK_SAMPLE_COUNT_1_BIT)
                if (const VTexture* rt = textures_.get(c.resolve.id))
                    transition(cmd_, *rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, rt->rest, 0, 1, 0, 1);
        }
    }
    if (const VTexture* dt = textures_.get(pass_.depth.texture.id))
        transition(cmd_, *dt, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, dt->rest, 0, 1, 0, 1);
    pipeline_ = nullptr, pipelineId_ = 0;
}

// ---------------------------------------------------------------- the window

void VulkanDevice::present(Texture src, const Rect& r, Filter filter, bool flipVertical) {
    if (inPass_) endPass();
    VTexture* t = textures_.get(src.id);
    if (!canPresent_ || lost_ || !t || r.w <= 0 || r.h <= 0 || !ensureRecording()) {
        if (!canPresent_ && !presentLogged_) presentLogged_ = true, SDL_Log("gdcore render (vulkan): no swapchain: present() draws nothing");
        return;
    }
    int ww = 0, wh = 0;
    drawableSize(ww, wh);
    if (ww <= 0 || wh <= 0) return;  // minimised
    if (!swapchain_ || swapDirty_ || swapExtent_.width != uint32_t(ww) || swapExtent_.height != uint32_t(wh)) {
        // Everything that used the old swapchain is done first (rare: resizes).
        if (cmd_) submit();
        vkDeviceWaitIdle(device_);
        poll();
        if (!createSwapchain()) return;
        if (!ensureRecording()) return;
        t = textures_.get(src.id);
    }
    VkSemaphore acquired = takeSemaphore();
    uint32_t index = 0;
    // A bounded wait: a window that is not shown (a hidden Wayland window gets
    // no frame callbacks) skips presenting instead of hanging.
    VkResult res = vkAcquireNextImageKHR(device_, swapchain_, 1000ull * 1000 * 1000, acquired, VK_NULL_HANDLE, &index);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        swapDirty_ = true;
        freeSemaphores_.push_back(acquired);
        return;
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        // VK_TIMEOUT / VK_NOT_READY leave the semaphore unsignalled: reusable.
        freeSemaphores_.push_back(acquired);
        if (res != VK_TIMEOUT && res != VK_NOT_READY) fail(vkError("vkAcquireNextImageKHR", res));
        return;
    }
    if (res == VK_SUBOPTIMAL_KHR) swapDirty_ = true;
    const VkImage image = swapImages_[index];
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    if (t->samples == VK_SAMPLE_COUNT_1_BIT) {
        transition(cmd_, *t, t->rest, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 1, 0, 1);
        VkImageBlit bl{};
        bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        bl.srcOffsets[0] = {r.x, r.y, 0};
        bl.srcOffsets[1] = {r.x + r.w, r.y + r.h, 1};
        bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        // The texture's row 0 is its top: it goes to the top of the window,
        // or to the bottom with flipVertical (a frame stored GL style).
        bl.dstOffsets[0] = {0, flipVertical ? int32_t(swapExtent_.height) : 0, 0};
        bl.dstOffsets[1] = {int32_t(swapExtent_.width), flipVertical ? 0 : int32_t(swapExtent_.height), 1};
        const bool linear = filter == Filter::Linear && (features(t->format) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
        vkCmdBlitImage(cmd_, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl,
                       linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
        transition(cmd_, *t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->rest, 0, 1, 0, 1);
    }
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = 0;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    usedSemaphores_.push_back({serial_, acquired});
    VkSemaphore done = renderDone_[index];
    submit(acquired, done);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &done;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &index;
    res = vkQueuePresentKHR(queue_, &pi);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) swapDirty_ = true;
    else if (res == VK_ERROR_DEVICE_LOST) lost_ = true, fail(vkError("vkQueuePresentKHR", res));
    else if (res != VK_SUCCESS) fail(vkError("vkQueuePresentKHR", res));
}

void VulkanDevice::setPresentMode(PresentMode mode) {
    if (mode == info_.presentMode) return;
    info_.presentMode = mode;
    swapDirty_ = true;  // made again at the next present
}

// At most kFramesInFlight frames on the GPU: the next frame's slot waits for
// its previous use when it starts recording.
void VulkanDevice::endFrame() {
    if (inPass_) endPass();
    if (cmd_) submit();
    slot_ = (slot_ + 1) % kFramesInFlight;
    slotReady_ = false;
    poll();
}

void VulkanDevice::pushDebugGroup(const char* name) {
    if (!debugUtils_ || !vkCmdBeginDebugUtilsLabelEXT || !ensureRecording()) return;
    VkDebugUtilsLabelEXT l{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    l.pLabelName = name ? name : "";
    vkCmdBeginDebugUtilsLabelEXT(cmd_, &l);
}

void VulkanDevice::popDebugGroup() {
    // Labels may span command buffers of one queue (a readback submits in the middle of a group).
    if (debugUtils_ && vkCmdEndDebugUtilsLabelEXT && ensureRecording()) vkCmdEndDebugUtilsLabelEXT(cmd_);
}

}  // namespace

std::unique_ptr<Device> createVulkanDevice(const DeviceCreateInfo& info, std::string& error) {
    auto d = std::make_unique<VulkanDevice>(info);
    if (!d->init(error)) return nullptr;
    return d;
}

}  // namespace gd::core::render
