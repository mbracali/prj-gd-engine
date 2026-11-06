// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The SDL3 GPU backend: gd::core::render::Device on SDL_GPU (docs/render.md
// sections 4.2 and 9). Shaders are the build-time SPIR-V (section 3.2), so
// SDL picks its Vulkan driver. What the interface allows and SDL_GPU does
// differently, and how it is bridged:
//
// - A render pass is recorded on the CPU (beginPass .. endPass) and replayed
//   at endPass: SDL_GPU forbids copy passes inside a render pass, and the
//   pass's transient data (allocTransient, usable inside a pass) must be
//   uploaded before it. So endPass uploads the pending transient data in a
//   copy pass, then begins the SDL render pass and replays the commands.
// - Everything outside passes (updates, copies, blits, mipmaps, readbacks)
//   is recorded into the current command buffer in call order, so the GPU
//   sees the same order as the calls (a draw recorded before an update sees
//   the old contents).
// - Uniforms: SDL keeps pushed data per command buffer and stage. The device
//   keeps each slot's last data (the interface: "pushed uniforms stay") and
//   pushes it to every stage whose reflection uses the slot, before the first
//   draw that needs it in each command buffer, and again when it changes.
// - Textures bind as SDL's texture + sampler pairs; a stage gets as many
//   pairs as its highest binding + 1 (reflectSpirv), unused ones a 1 x 1
//   dummy.
// - Readbacks: a download transfer buffer, and the fence of the command
//   buffer that recorded the copy (submitted early when someone waits).
//
// Images are top-down natively (SDL_GPU: NDC y up maps to row 0; its Vulkan
// driver flips the viewport itself), so nothing is drawn upside down here.

#include "sdlgpu/sdlgpu_device.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "common/spirv_reflect.h"
#include "gd/core/render/device.h"

namespace gd::core::render {
namespace {

// SDL_GPU's Vulkan driver binds each uniform slot with a 4 KiB range
// [C: SDL 3.4.18 SDL_gpu_vulkan.c MAX_UBO_SECTION_SIZE]: a larger block cannot be read.
constexpr size_t kSdlUniformBytes = 4096;

// A pool of objects addressed by 1-based ids (0 is "none").
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

SDL_GPUTextureFormat sdlFormat(Format f) {
    switch (f) {
    case Format::RGBA8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_SRGB: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::BGRA8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case Format::R8: return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case Format::RG8: return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    case Format::RGB10A2: return SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM;
    case Format::RG11B10F: return SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT;
    case Format::R16F: return SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    case Format::RG16F: return SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
    case Format::RGBA16F: return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    case Format::R32F: return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    case Format::RGBA32F: return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    case Format::BC1: return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case Format::BC1_SRGB: return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB;
    case Format::BC2: return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case Format::BC2_SRGB: return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM_SRGB;
    case Format::BC3: return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case Format::BC3_SRGB: return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB;
    case Format::BC4: return SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM;
    case Format::BC5: return SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
    case Format::BC7: return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
    case Format::BC7_SRGB: return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB;
    case Format::D16: return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    case Format::D24: return SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    case Format::D24S8: return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    case Format::D32F: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    case Format::D32FS8: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    default: return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}

SDL_GPUCompareOp sdlCompare(CompareOp op) {
    static const SDL_GPUCompareOp m[] = {SDL_GPU_COMPAREOP_NEVER,      SDL_GPU_COMPAREOP_LESS,      SDL_GPU_COMPAREOP_EQUAL,
                                         SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_COMPAREOP_GREATER, SDL_GPU_COMPAREOP_NOT_EQUAL,
                                         SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, SDL_GPU_COMPAREOP_ALWAYS};
    return m[size_t(op)];
}
SDL_GPUBlendFactor sdlBlendFactor(BlendFactor f) {
    static const SDL_GPUBlendFactor m[] = {
        SDL_GPU_BLENDFACTOR_ZERO,           SDL_GPU_BLENDFACTOR_ONE,
        SDL_GPU_BLENDFACTOR_SRC_COLOR,      SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
        SDL_GPU_BLENDFACTOR_DST_COLOR,      SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR,
        SDL_GPU_BLENDFACTOR_SRC_ALPHA,      SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_GPU_BLENDFACTOR_DST_ALPHA,      SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA,
        SDL_GPU_BLENDFACTOR_CONSTANT_COLOR, SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR,
        SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE};
    return m[size_t(f)];
}
SDL_GPUBlendOp sdlBlendOp(BlendOp op) {
    static const SDL_GPUBlendOp m[] = {SDL_GPU_BLENDOP_ADD, SDL_GPU_BLENDOP_SUBTRACT, SDL_GPU_BLENDOP_REVERSE_SUBTRACT, SDL_GPU_BLENDOP_MIN,
                                       SDL_GPU_BLENDOP_MAX};
    return m[size_t(op)];
}
SDL_GPUStencilOp sdlStencilOp(StencilOp op) {
    static const SDL_GPUStencilOp m[] = {SDL_GPU_STENCILOP_KEEP,
                                         SDL_GPU_STENCILOP_ZERO,
                                         SDL_GPU_STENCILOP_REPLACE,
                                         SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP,
                                         SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP,
                                         SDL_GPU_STENCILOP_INVERT,
                                         SDL_GPU_STENCILOP_INCREMENT_AND_WRAP,
                                         SDL_GPU_STENCILOP_DECREMENT_AND_WRAP};
    return m[size_t(op)];
}
SDL_GPUSamplerAddressMode sdlWrap(Wrap w) {
    switch (w) {
    case Wrap::Repeat: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    case Wrap::MirroredRepeat: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    case Wrap::ClampToEdge:
    case Wrap::ClampToBorder: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;  // no border colour in SDL_GPU (Caps::clampToBorder false)
    }
    return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
}
SDL_GPUPrimitiveType sdlPrimitive(Primitive p) {
    switch (p) {
    case Primitive::Triangles: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    case Primitive::TriangleStrip: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    case Primitive::Lines: return SDL_GPU_PRIMITIVETYPE_LINELIST;
    case Primitive::LineStrip: return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
    case Primitive::Points: return SDL_GPU_PRIMITIVETYPE_POINTLIST;
    }
    return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}
SDL_GPUVertexElementFormat sdlVertexFormat(VertexFormat f) {
    switch (f) {
    case VertexFormat::Float1: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case VertexFormat::Float2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case VertexFormat::Float3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case VertexFormat::Float4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case VertexFormat::UByte4Norm: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    case VertexFormat::Byte4Norm: return SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM;
    case VertexFormat::UShort2Norm: return SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM;
    case VertexFormat::Short2Norm: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM;
    case VertexFormat::Half2: return SDL_GPU_VERTEXELEMENTFORMAT_HALF2;
    case VertexFormat::Half4: return SDL_GPU_VERTEXELEMENTFORMAT_HALF4;
    case VertexFormat::UInt1: return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    case VertexFormat::Int1: return SDL_GPU_VERTEXELEMENTFORMAT_INT;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
}
SDL_GPULoadOp sdlLoad(LoadOp op) {
    return op == LoadOp::Load ? SDL_GPU_LOADOP_LOAD : op == LoadOp::Clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_DONT_CARE;
}
SDL_GPUStoreOp sdlStore(StoreOp op) {
    switch (op) {
    case StoreOp::Store: return SDL_GPU_STOREOP_STORE;
    case StoreOp::DontCare: return SDL_GPU_STOREOP_DONT_CARE;
    case StoreOp::Resolve: return SDL_GPU_STOREOP_RESOLVE;
    case StoreOp::ResolveAndStore: return SDL_GPU_STOREOP_RESOLVE_AND_STORE;
    }
    return SDL_GPU_STOREOP_STORE;
}
SDL_GPUSampleCount sdlSamples(int n) {
    return n >= 8 ? SDL_GPU_SAMPLECOUNT_8 : n >= 4 ? SDL_GPU_SAMPLECOUNT_4 : n >= 2 ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_1;
}

struct GpuBuffer {
    SDL_GPUBuffer* buffer = nullptr;
    BufferDesc desc;
};
struct GpuTexture {
    SDL_GPUTexture* texture = nullptr;
    TextureDesc desc;
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
};
struct GpuShader {
    SDL_GPUShader* vertex = nullptr;
    SDL_GPUShader* fragment = nullptr;
    SpirvStageInfo vi, fi;
};
struct GpuPipeline {
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    PipelineDesc desc;
    SpirvStageInfo vi, fi;
};
struct GpuReadback {
    SDL_GPUTransferBuffer* transfer = nullptr;
    size_t size = 0;
    uint64_t serial = 0;  // the command buffer that recorded the download
};

// Per-frame ring of GPU buffers (transient vertex / index data), as the GL
// backend's: each frame slot has its own chunks, reused when the slot comes
// back (kFramesInFlight + 1 slots: beginFrame has waited for that frame).
struct Ring {
    uint8_t usage = kVertexBuffer;
    size_t chunkSize = 1 << 20;
    struct Chunk {
        uint32_t buffer = 0;  // Buffer id
        size_t size = 0, used = 0;
    };
    std::vector<Chunk> slots[kFramesInFlight + 1];
    int slot = 0;
};

// What a pass records until endPass replays it.
enum class Op : uint8_t { Viewport, Scissor, StencilRef, BlendConstants, Pipeline, VertexBuffers, IndexBuffer, Textures, Uniforms, Draw, DrawIndexed };
struct Cmd {
    Op op = Op::Draw;
    uint32_t a = 0, b = 0, c = 0, d = 0;
    int32_t e = 0;
    float f[6] = {};
};

class SdlGpuDevice final : public Device {
public:
    explicit SdlGpuDevice(const DeviceCreateInfo& info) : info_(info) {}
    ~SdlGpuDevice() override;
    bool init(std::string& error);

    Backend backend() const override { return Backend::SdlGpu; }
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
        const GpuTexture* tx = textures_.get(t.id);
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

private:
    bool fail(const std::string& e) {
        error_ = e;
        SDL_Log("gdcore render (sdl3): %s", e.c_str());
        return false;
    }
    SDL_GPUCommandBuffer* cmd();
    void submit();
    void pollFences();
    void waitSerial(uint64_t serial);
    // Upload staging: `size` bytes in the current command buffer's upload
    // arena (a transfer buffer cycled once per command buffer). Returns the
    // mapped pointer; `tb` / `offset` locate it for the copy.
    uint8_t* stage(size_t size, SDL_GPUTransferBuffer*& tb, uint32_t& offset);
    void unstage();
    void flushTransient();
    BufferRange ringAlloc(Ring& ring, const void* data, size_t size, size_t align);
    void replayPass();
    void releaseDeferred();
    SDL_GPUTextureFormat mapFormat(Format f) const;
    uint8_t formatSupport(Format f);

    DeviceCreateInfo info_;
    Caps caps_;
    std::string error_;
    SDL_GPUDevice* dev_ = nullptr;
    bool claimed_ = false;
    bool d24_ = true, d24s8_ = true;  // else D32F / D32FS8 behind the same Format
    SDL_GPUPresentMode presentMode_ = SDL_GPU_PRESENTMODE_VSYNC;

    Pool<GpuBuffer> buffers_;
    Pool<GpuTexture> textures_;
    Pool<GpuShader> shaders_;
    Pool<GpuPipeline> pipelines_;
    Pool<GpuReadback> readbacks_;
    std::vector<std::pair<SamplerDesc, SDL_GPUSampler*>> samplers_;  // id = index + 1

    // The command buffer being recorded and the submitted ones' fences.
    SDL_GPUCommandBuffer* cmd_ = nullptr;
    uint64_t cmdSerial_ = 0;   // serial of cmd_ (or of the next one)
    uint64_t completed_ = 0;   // every serial <= this has finished on the GPU
    std::deque<std::pair<uint64_t, SDL_GPUFence*>> fences_;
    uint64_t frameIndex_ = 0;
    uint64_t frameSerial_[kFramesInFlight + 1] = {};  // the last serial of each recent frame

    // Upload arena.
    SDL_GPUTransferBuffer* upload_ = nullptr;
    size_t uploadCap_ = 0, uploadUsed_ = 0;
    uint64_t uploadSerial_ = ~uint64_t(0);
    bool uploadMapped_ = false;
    uint8_t* uploadPtr_ = nullptr;

    // Transient data waiting for its copy pass (before the next render pass).
    struct PendingUpload {
        uint32_t buffer = 0;
        size_t offset = 0, size = 0, src = 0;
    };
    std::vector<PendingUpload> pending_;
    std::vector<uint8_t> pendingBytes_;
    Ring vertexRing_, indexRing_;

    // Uniform slots: the last data of each (kept across passes and frames) and
    // what each stage got in the current command buffer.
    std::vector<uint8_t> uniformData_[kMaxUniformSlots];
    uint64_t uniformVersion_[kMaxUniformSlots] = {};
    uint64_t pushedVersion_[2][kMaxUniformSlots] = {};
    bool toldUniformSize_ = false;

    // The pass being recorded.
    bool inPass_ = false;
    PassDesc pass_;
    int passW_ = 0, passH_ = 0;
    std::vector<Cmd> cmds_;
    std::vector<BufferRange> recBuffers_;
    std::vector<TextureBinding> recTextures_;
    std::vector<uint8_t> recBytes_;
    // Destroyed while a pass that may use them is being recorded: released after it.
    std::vector<SDL_GPUTexture*> deadTextures_;
    std::vector<SDL_GPUBuffer*> deadBuffers_;
    std::vector<SDL_GPUGraphicsPipeline*> deadPipelines_;

    SDL_GPUTexture* dummy_ = nullptr;  // unused sampler slots
    SDL_GPUSampler* dummySampler_ = nullptr;
};

SdlGpuDevice::~SdlGpuDevice() {
    if (!dev_) return;
    if (inPass_) endPass();
    submit();
    SDL_WaitForGPUIdle(dev_);
    for (auto& [s, f] : fences_) SDL_ReleaseGPUFence(dev_, f);
    fences_.clear();
    releaseDeferred();
    for (size_t i = 0; i < buffers_.items.size(); ++i)
        if (buffers_.live[i]) SDL_ReleaseGPUBuffer(dev_, buffers_.items[i].buffer);
    for (size_t i = 0; i < textures_.items.size(); ++i)
        if (textures_.live[i]) SDL_ReleaseGPUTexture(dev_, textures_.items[i].texture);
    for (size_t i = 0; i < shaders_.items.size(); ++i)
        if (shaders_.live[i]) SDL_ReleaseGPUShader(dev_, shaders_.items[i].vertex), SDL_ReleaseGPUShader(dev_, shaders_.items[i].fragment);
    for (size_t i = 0; i < pipelines_.items.size(); ++i)
        if (pipelines_.live[i]) SDL_ReleaseGPUGraphicsPipeline(dev_, pipelines_.items[i].pipeline);
    for (size_t i = 0; i < readbacks_.items.size(); ++i)
        if (readbacks_.live[i]) SDL_ReleaseGPUTransferBuffer(dev_, readbacks_.items[i].transfer);
    for (auto& [d, s] : samplers_) SDL_ReleaseGPUSampler(dev_, s);
    if (upload_) SDL_ReleaseGPUTransferBuffer(dev_, upload_);
    if (dummy_) SDL_ReleaseGPUTexture(dev_, dummy_);
    if (dummySampler_) SDL_ReleaseGPUSampler(dev_, dummySampler_);
    // The window goes back to its owner (the launcher may make it again for another API).
    if (claimed_) SDL_ReleaseWindowFromGPUDevice(dev_, info_.window);
    SDL_DestroyGPUDevice(dev_);
}

SDL_GPUTextureFormat SdlGpuDevice::mapFormat(Format f) const {
    if (f == Format::D24 && !d24_) return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    if (f == Format::D24S8 && !d24s8_) return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    return sdlFormat(f);
}

uint8_t SdlGpuDevice::formatSupport(Format f) {
    const SDL_GPUTextureFormat sf = mapFormat(f);
    if (sf == SDL_GPU_TEXTUREFORMAT_INVALID) return 0;
    uint8_t bits = 0;
    if (isDepthFormat(f)) {
        if (SDL_GPUTextureSupportsFormat(dev_, sf, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
            bits |= kSampled | kRenderable;
        if ((bits & kRenderable) && SDL_GPUTextureSupportsSampleCount(dev_, sf, SDL_GPU_SAMPLECOUNT_4)) bits |= kResolvable;
        return bits;
    }
    if (SDL_GPUTextureSupportsFormat(dev_, sf, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        bits |= kSampled;
        // [I] Vulkan guarantees linear filtering for these except 32-bit floats.
        if (f != Format::R32F && f != Format::RGBA32F) bits |= kFiltered;
    }
    if (!isCompressedFormat(f) &&
        SDL_GPUTextureSupportsFormat(dev_, sf, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) {
        bits |= kRenderable;
        if (f != Format::R32F && f != Format::RGBA32F) bits |= kBlendable;
        if (SDL_GPUTextureSupportsSampleCount(dev_, sf, SDL_GPU_SAMPLECOUNT_4)) bits |= kResolvable;
    }
    return bits;
}

bool SdlGpuDevice::init(std::string& error) {
    dev_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, info_.debug, nullptr);
    if (!dev_) return error = std::string("no SDL_GPU device with SPIR-V shaders: ") + SDL_GetError(), fail(error);
    if (!SDL_ClaimWindowForGPUDevice(dev_, info_.window))
        return error = std::string("SDL_GPU cannot use the window: ") + SDL_GetError(), fail(error);
    claimed_ = true;
    const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(dev_);
    const char* driver = SDL_GetGPUDeviceDriver(dev_);
    caps_.api = std::string("SDL_GPU (") + (driver ? driver : "?") + ")";
    const char* version = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, nullptr);
    if (!version) version = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "");
    caps_.version = version;
    caps_.gpu = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "");
    caps_.es = false;
    caps_.clampToBorder = false;  // SDL_GPU has no border colour
    caps_.maxTextureSize = 8192;  // [I] SDL_GPU does not report it; 8192 is what its backends all guarantee
    caps_.maxAnisotropy = 16;
    caps_.maxSamples = 1;
    for (SDL_GPUSampleCount s : {SDL_GPU_SAMPLECOUNT_2, SDL_GPU_SAMPLECOUNT_4, SDL_GPU_SAMPLECOUNT_8})
        if (SDL_GPUTextureSupportsSampleCount(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, s)) caps_.maxSamples = 1 << int(s);
    // D24 (and D24S8) where the GPU has it as a sampled depth target, else the 32-bit float formats [C: SDL_gpu.h: one of the two is always there].
    const auto depthOk = [&](SDL_GPUTextureFormat f) {
        return SDL_GPUTextureSupportsFormat(dev_, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    };
    d24_ = depthOk(SDL_GPU_TEXTUREFORMAT_D24_UNORM);
    d24s8_ = depthOk(SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT);
    for (size_t f = 1; f < size_t(Format::Count); ++f) caps_.formats[f] = formatSupport(Format(f));

    vertexRing_.usage = kVertexBuffer;
    indexRing_.usage = kIndexBuffer;
    for (int s = 0; s < kMaxUniformSlots; ++s) uniformData_[s].assign(256, 0), uniformVersion_[s] = 1;

    // The dummy: a 1 x 1 opaque black texture (what GL samples from an unbound unit).
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D, ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = ti.height = 1, ti.layer_count_or_depth = 1, ti.num_levels = 1;
    dummy_ = SDL_CreateGPUTexture(dev_, &ti);
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    dummySampler_ = SDL_CreateGPUSampler(dev_, &si);
    if (!dummy_ || !dummySampler_) return error = std::string("SDL_GPU: ") + SDL_GetError(), fail(error);
    {
        SDL_GPUTransferBuffer* tb = nullptr;
        uint32_t off = 0;
        uint8_t* p = stage(4, tb, off);
        const uint8_t black[4] = {0, 0, 0, 255};
        std::memcpy(p, black, 4);
        unstage();
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd());
        SDL_GPUTextureTransferInfo src{tb, off, 0, 0};
        SDL_GPUTextureRegion dst{};
        dst.texture = dummy_, dst.w = dst.h = dst.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
    }
    setPresentMode(info_.presentMode);
    SDL_Log("gdcore render: %s %s on %s%s", caps_.api.c_str(), caps_.version.c_str(), caps_.gpu.c_str(),
            d24_ ? "" : " (no sampled D24: D32F instead)");
    return true;
}

// ---------------------------------------------------------------- command buffers, fences

SDL_GPUCommandBuffer* SdlGpuDevice::cmd() {
    if (!cmd_) {
        cmd_ = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd_) {
            fail(std::string("SDL_AcquireGPUCommandBuffer: ") + SDL_GetError());
            return nullptr;
        }
        ++cmdSerial_;
        for (auto& stage : pushedVersion_)
            for (uint64_t& v : stage) v = 0;  // SDL's pushed uniforms live per command buffer
    }
    return cmd_;
}

void SdlGpuDevice::submit() {
    if (!cmd_) return;
    unstage();
    SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd_);
    cmd_ = nullptr;
    if (f) fences_.push_back({cmdSerial_, f});
    else completed_ = cmdSerial_, fail(std::string("submit: ") + SDL_GetError());  // nothing to wait for
}

void SdlGpuDevice::pollFences() {
    while (!fences_.empty() && SDL_QueryGPUFence(dev_, fences_.front().second)) {
        completed_ = fences_.front().first;
        SDL_ReleaseGPUFence(dev_, fences_.front().second);
        fences_.pop_front();
    }
    if (fences_.empty() && !cmd_) completed_ = cmdSerial_;
}

void SdlGpuDevice::waitSerial(uint64_t serial) {
    if (cmd_ && serial >= cmdSerial_) submit();
    while (completed_ < serial && !fences_.empty()) {
        SDL_GPUFence* f = fences_.front().second;
        SDL_WaitForGPUFences(dev_, true, &f, 1);
        completed_ = fences_.front().first;
        SDL_ReleaseGPUFence(dev_, f);
        fences_.pop_front();
    }
    if (completed_ < serial && fences_.empty()) completed_ = serial;
}

uint8_t* SdlGpuDevice::stage(size_t size, SDL_GPUTransferBuffer*& tb, uint32_t& offset) {
    cmd();  // the arena belongs to the current command buffer
    size_t at = (uploadUsed_ + 15) / 16 * 16;  // 16: a multiple of every texel and block size
    const bool fresh = uploadSerial_ != cmdSerial_;
    if (!upload_ || (!fresh && at + size > uploadCap_) || size > uploadCap_) {
        // Too small: a bigger one (the old one is released when the GPU is done with it).
        unstage();
        if (upload_) SDL_ReleaseGPUTransferBuffer(dev_, upload_);
        uploadCap_ = std::max<size_t>({size_t(4) << 20, size, uploadCap_ * 2});
        SDL_GPUTransferBufferCreateInfo ci{};
        ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, ci.size = uint32_t(uploadCap_);
        upload_ = SDL_CreateGPUTransferBuffer(dev_, &ci);
        uploadSerial_ = cmdSerial_, uploadUsed_ = 0, at = 0;
        if (!upload_) return fail(std::string("transfer buffer: ") + SDL_GetError()), nullptr;
    } else if (fresh) {
        // First use in this command buffer: cycle, so the GPU's pending reads of the old contents are kept.
        unstage();
        uploadSerial_ = cmdSerial_, uploadUsed_ = 0, at = 0;
        uploadPtr_ = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(dev_, upload_, true));
        uploadMapped_ = uploadPtr_ != nullptr;
    }
    if (!uploadMapped_) {
        // Within one command buffer the arena is never cycled: it fills up behind the copies already recorded.
        uploadPtr_ = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(dev_, upload_, false));
        uploadMapped_ = uploadPtr_ != nullptr;
        if (!uploadPtr_) return fail(std::string("map: ") + SDL_GetError()), nullptr;
    }
    uploadUsed_ = at + size;
    tb = upload_;
    offset = uint32_t(at);
    return uploadPtr_ + at;
}

void SdlGpuDevice::unstage() {
    if (uploadMapped_) SDL_UnmapGPUTransferBuffer(dev_, upload_);
    uploadMapped_ = false, uploadPtr_ = nullptr;
}

void SdlGpuDevice::releaseDeferred() {
    for (SDL_GPUTexture* t : deadTextures_) SDL_ReleaseGPUTexture(dev_, t);
    for (SDL_GPUBuffer* b : deadBuffers_) SDL_ReleaseGPUBuffer(dev_, b);
    for (SDL_GPUGraphicsPipeline* p : deadPipelines_) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
    deadTextures_.clear(), deadBuffers_.clear(), deadPipelines_.clear();
}

// ---------------------------------------------------------------- buffers

Buffer SdlGpuDevice::createBuffer(const BufferDesc& desc, const void* data) {
    SDL_GPUBufferCreateInfo ci{};
    // SDL_GPU binds no uniform buffers (uniforms are pushed): such a buffer is plain storage here.
    ci.usage = (desc.usage & kIndexBuffer) ? SDL_GPU_BUFFERUSAGE_INDEX : SDL_GPU_BUFFERUSAGE_VERTEX;
    if ((desc.usage & kIndexBuffer) && (desc.usage & kVertexBuffer)) ci.usage |= SDL_GPU_BUFFERUSAGE_VERTEX;
    ci.size = uint32_t(std::max<size_t>(desc.size, 4));
    GpuBuffer b;
    b.desc = desc;
    b.buffer = SDL_CreateGPUBuffer(dev_, &ci);
    if (!b.buffer) {
        fail(std::string("createBuffer: ") + SDL_GetError());
        return {};
    }
    if (desc.label) SDL_SetGPUBufferName(dev_, b.buffer, desc.label);
    Buffer h{buffers_.add(b)};
    if (data && desc.size) updateBuffer(h, 0, data, desc.size);
    return h;
}

void SdlGpuDevice::updateBuffer(Buffer b, size_t offset, const void* data, size_t size) {
    GpuBuffer* buf = buffers_.get(b.id);
    if (!buf || !size || !data || offset + size > buf->desc.size) return;
    SDL_GPUTransferBuffer* tb = nullptr;
    uint32_t off = 0;
    uint8_t* p = stage(size, tb, off);
    if (!p) return;
    std::memcpy(p, data, size);
    unstage();
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd());
    SDL_GPUTransferBufferLocation src{tb, off};
    SDL_GPUBufferRegion dst{buf->buffer, uint32_t(offset), uint32_t(size)};
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);  // no cycle: draws recorded before keep the old contents (GPU order)
    SDL_EndGPUCopyPass(cp);
}

void SdlGpuDevice::destroyBuffer(Buffer b) {
    GpuBuffer* buf = buffers_.get(b.id);
    if (!buf) return;
    // Pending transient uploads into it go too.
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const PendingUpload& u) { return u.buffer == b.id; }), pending_.end());
    if (inPass_) deadBuffers_.push_back(buf->buffer);
    else SDL_ReleaseGPUBuffer(dev_, buf->buffer);  // SDL keeps it until the GPU is done with it
    buffers_.remove(b.id);
}

BufferRange SdlGpuDevice::ringAlloc(Ring& ring, const void* data, size_t size, size_t align) {
    auto& chunks = ring.slots[ring.slot];
    for (auto& c : chunks) {
        size_t at = (c.used + align - 1) / align * align;
        if (at + size <= c.size) {
            c.used = at + size;
            // Copied now; uploaded in a copy pass before the next render pass (endPass, or a pass that begins later).
            PendingUpload u;
            u.buffer = c.buffer, u.offset = at, u.size = size, u.src = pendingBytes_.size();
            pendingBytes_.insert(pendingBytes_.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
            if (!pending_.empty() && pending_.back().buffer == u.buffer && pending_.back().offset + pending_.back().size == at &&
                pending_.back().src + pending_.back().size == u.src)
                pending_.back().size += size;  // contiguous with the last one: one copy
            else
                pending_.push_back(u);
            return BufferRange{Buffer{c.buffer}, at, size};
        }
    }
    BufferDesc d;
    d.size = std::max(ring.chunkSize, size);
    d.usage = ring.usage;
    d.dynamic = true;
    Ring::Chunk c;
    c.buffer = createBuffer(d, nullptr).id;
    if (!c.buffer) return {};
    c.size = d.size;
    chunks.push_back(c);
    return ringAlloc(ring, data, size, align);
}

BufferRange SdlGpuDevice::allocTransient(uint8_t usage, const void* data, size_t size) {
    if (!size || !data) return {};
    if (usage & kIndexBuffer) return ringAlloc(indexRing_, data, size, 4);
    return ringAlloc(vertexRing_, data, size, 16);  // vertex (and uniform data: SDL_GPU pushes uniforms instead)
}

void SdlGpuDevice::flushTransient() {
    if (pending_.empty()) {
        pendingBytes_.clear();
        return;
    }
    SDL_GPUTransferBuffer* tb = nullptr;
    uint32_t off = 0;
    uint8_t* p = stage(pendingBytes_.size(), tb, off);
    if (p) {
        std::memcpy(p, pendingBytes_.data(), pendingBytes_.size());
        unstage();
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd());
        for (const PendingUpload& u : pending_) {
            const GpuBuffer* buf = buffers_.get(u.buffer);
            if (!buf) continue;
            SDL_GPUTransferBufferLocation src{tb, uint32_t(off + u.src)};
            SDL_GPUBufferRegion dst{buf->buffer, uint32_t(u.offset), uint32_t(u.size)};
            SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        }
        SDL_EndGPUCopyPass(cp);
    }
    pending_.clear();
    pendingBytes_.clear();
}

// ---------------------------------------------------------------- textures

Texture SdlGpuDevice::createTexture(const TextureDesc& in) {
    TextureDesc desc = in;
    const SDL_GPUTextureFormat sf = mapFormat(desc.format);
    if (sf == SDL_GPU_TEXTUREFORMAT_INVALID || desc.width <= 0 || desc.height <= 0) {
        fail("createTexture: unsupported format or size");
        return {};
    }
    if (desc.mips <= 0) {
        int n = 1;
        for (int s = std::max(desc.width, desc.height); s > 1; s >>= 1) ++n;
        desc.mips = n;
    }
    const bool depth = isDepthFormat(desc.format);
    const uint8_t support = caps_.formats[size_t(desc.format)];
    SDL_GPUTextureCreateInfo ci{};
    ci.type = desc.type == TextureType::Cube ? SDL_GPU_TEXTURETYPE_CUBE : SDL_GPU_TEXTURETYPE_2D;
    ci.format = sf;
    ci.width = uint32_t(desc.width), ci.height = uint32_t(desc.height);
    ci.layer_count_or_depth = desc.type == TextureType::Cube ? 6 : 1;
    ci.num_levels = uint32_t(desc.mips);
    if (desc.samples > 1) {
        // MSAA: a target only (resolved by the pass's Resolve store op).
        desc.mips = 1, ci.num_levels = 1;
        desc.samples = std::min(desc.samples, caps_.maxSamples);
        ci.sample_count = sdlSamples(desc.samples);
        ci.usage = depth ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    } else {
        ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ci.usage = (support & kSampled) ? SDL_GPU_TEXTUREUSAGE_SAMPLER : 0;
        if (depth) {
            if (desc.usage & kDepthTarget) ci.usage |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        } else if (support & kRenderable) {
            // Every renderable colour texture is a target too: a blit's destination,
            // and SDL_GenerateMipmapsForGPUTexture needs it.
            ci.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        }
    }
    GpuTexture t;
    t.desc = desc;
    t.format = sf;
    t.texture = SDL_CreateGPUTexture(dev_, &ci);
    if (!t.texture) {
        fail(std::string("createTexture: ") + SDL_GetError());
        return {};
    }
    if (desc.label) SDL_SetGPUTextureName(dev_, t.texture, desc.label);
    return Texture{textures_.add(t)};
}

void SdlGpuDevice::updateTexture(const TextureRegion& dst, int w, int h, const void* data, size_t rowBytes) {
    GpuTexture* t = textures_.get(dst.texture.id);
    if (!t || w <= 0 || h <= 0 || !data || t->desc.samples > 1) return;
    if (inPass_) fail("updateTexture inside a pass (it happens before the pass's draws)");
    const Format f = t->desc.format;
    const size_t bytes = imageBytes(f, w, h);
    SDL_GPUTransferBuffer* tb = nullptr;
    uint32_t off = 0;
    uint8_t* p = stage(bytes, tb, off);
    if (!p) return;
    if (isCompressedFormat(f)) {
        std::memcpy(p, data, bytes);  // whole blocks, rows of blocks tightly packed (rowBytes ignored, as the GL backend)
    } else {
        const size_t tight = size_t(w) * formatBytes(f);
        if (!rowBytes || rowBytes == tight) std::memcpy(p, data, bytes);
        else
            for (int y = 0; y < h; ++y) std::memcpy(p + tight * size_t(y), static_cast<const uint8_t*>(data) + rowBytes * size_t(y), tight);
    }
    unstage();
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd());
    SDL_GPUTextureTransferInfo src{tb, off, 0, 0};  // 0: tightly packed
    SDL_GPUTextureRegion r{};
    r.texture = t->texture, r.mip_level = uint32_t(dst.mip), r.layer = uint32_t(dst.layer);
    r.x = uint32_t(dst.x), r.y = uint32_t(dst.y), r.w = uint32_t(w), r.h = uint32_t(h), r.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &r, false);
    SDL_EndGPUCopyPass(cp);
}

void SdlGpuDevice::generateMipmaps(Texture tx) {
    GpuTexture* t = textures_.get(tx.id);
    if (!t || t->desc.mips <= 1 || inPass_) return;
    if (!(caps_.formats[size_t(t->desc.format)] & kRenderable) || isDepthFormat(t->desc.format)) return;
    SDL_GenerateMipmapsForGPUTexture(cmd(), t->texture);
}

void SdlGpuDevice::destroyTexture(Texture tx) {
    GpuTexture* t = textures_.get(tx.id);
    if (!t) return;
    if (inPass_) deadTextures_.push_back(t->texture);
    else SDL_ReleaseGPUTexture(dev_, t->texture);
    textures_.remove(tx.id);
}

Sampler SdlGpuDevice::createSampler(const SamplerDesc& d) {
    for (size_t i = 0; i < samplers_.size(); ++i)
        if (samplers_[i].first == d) return Sampler{uint32_t(i + 1)};
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = d.minFilter == Filter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    si.mag_filter = d.magFilter == Filter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = d.mipFilter == MipFilter::Linear ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = sdlWrap(d.wrapU), si.address_mode_v = sdlWrap(d.wrapV), si.address_mode_w = sdlWrap(d.wrapW);
    si.mip_lod_bias = d.lodBias;
    si.min_lod = std::max(d.minLod, 0.0f);
    si.max_lod = d.maxLod;
    if (d.mipFilter == MipFilter::None) {
        // GL's non-mipmapped minification: level 0 only, while min / mag still
        // follow the level of detail [C: the Vulkan spec's note on maxLod 0.25].
        si.min_lod = 0, si.max_lod = 0.25f;
    }
    si.enable_anisotropy = d.maxAnisotropy > 1;
    si.max_anisotropy = std::min(d.maxAnisotropy, caps_.maxAnisotropy);
    si.enable_compare = d.compare;
    si.compare_op = sdlCompare(d.compareOp);
    SDL_GPUSampler* s = SDL_CreateGPUSampler(dev_, &si);
    if (!s) {
        fail(std::string("createSampler: ") + SDL_GetError());
        return {};
    }
    samplers_.push_back({d, s});
    return Sampler{uint32_t(samplers_.size())};
}

// ---------------------------------------------------------------- shaders, pipelines

Shader SdlGpuDevice::createShader(const ShaderDesc& d) {
    if (!d.spirvVertex || !d.spirvFragment) {
        fail(std::string("shader ") + d.name + ": no SPIR-V (this backend takes the build-time SPIR-V, ShaderDesc::spirv)");
        return {};
    }
    GpuShader s;
    std::string why;
    if (!reflectSpirv(d.spirvVertex, d.spirvVertexBytes, s.vi, why) || s.vi.stage != SpirvStage::Vertex) {
        fail(std::string("shader ") + d.name + ": vertex SPIR-V: " + (why.empty() ? "not a vertex shader" : why));
        return {};
    }
    if (!reflectSpirv(d.spirvFragment, d.spirvFragmentBytes, s.fi, why) || s.fi.stage != SpirvStage::Fragment) {
        fail(std::string("shader ") + d.name + ": fragment SPIR-V: " + (why.empty() ? "not a fragment shader" : why));
        return {};
    }
    auto make = [&](const uint32_t* words, size_t bytes, const SpirvStageInfo& info, SDL_GPUShaderStage stage) {
        SDL_GPUShaderCreateInfo ci{};
        ci.code = reinterpret_cast<const Uint8*>(words), ci.code_size = bytes;
        ci.entrypoint = "main";
        ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
        ci.stage = stage;
        ci.num_samplers = info.numSamplers();
        ci.num_uniform_buffers = info.numUniformBuffers();
        return SDL_CreateGPUShader(dev_, &ci);
    };
    s.vertex = make(d.spirvVertex, d.spirvVertexBytes, s.vi, SDL_GPU_SHADERSTAGE_VERTEX);
    s.fragment = s.vertex ? make(d.spirvFragment, d.spirvFragmentBytes, s.fi, SDL_GPU_SHADERSTAGE_FRAGMENT) : nullptr;
    if (!s.fragment) {
        if (s.vertex) SDL_ReleaseGPUShader(dev_, s.vertex);
        fail(std::string("shader ") + d.name + ": " + SDL_GetError());
        return {};
    }
    return Shader{shaders_.add(s)};
}

void SdlGpuDevice::destroyShader(Shader s) {
    // Pipelines made from it keep their own compiled copy (SDL / Vulkan): released now.
    if (GpuShader* sh = shaders_.get(s.id)) SDL_ReleaseGPUShader(dev_, sh->vertex), SDL_ReleaseGPUShader(dev_, sh->fragment);
    shaders_.remove(s.id);
}

Pipeline SdlGpuDevice::createPipeline(const PipelineDesc& d) {
    const GpuShader* s = shaders_.get(d.shader.id);
    if (!s) {
        fail("createPipeline: no shader");
        return {};
    }
    SDL_GPUVertexBufferDescription vb[kMaxVertexBuffers] = {};
    SDL_GPUVertexAttribute va[kMaxVertexAttributes] = {};
    const VertexLayout& l = d.layout;
    for (int i = 0; i < l.bufferCount && i < kMaxVertexBuffers; ++i) {
        vb[i].slot = uint32_t(i);
        vb[i].pitch = l.buffers[i].stride;
        vb[i].input_rate = l.buffers[i].perInstance ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX;
    }
    for (int i = 0; i < l.attributeCount && i < kMaxVertexAttributes; ++i) {
        va[i].location = l.attributes[i].location;
        va[i].buffer_slot = l.attributes[i].buffer;
        va[i].format = sdlVertexFormat(l.attributes[i].format);
        va[i].offset = l.attributes[i].offset;
    }
    SDL_GPUColorTargetDescription ct[kMaxColorTargets] = {};
    for (int i = 0; i < d.colorCount && i < kMaxColorTargets; ++i) {
        const BlendState& b = d.blend[i];
        ct[i].format = mapFormat(d.colorFormats[i]);
        ct[i].blend_state.enable_blend = b.enable;
        ct[i].blend_state.src_color_blendfactor = sdlBlendFactor(b.srcColor);
        ct[i].blend_state.dst_color_blendfactor = sdlBlendFactor(b.dstColor);
        ct[i].blend_state.color_blend_op = sdlBlendOp(b.colorOp);
        ct[i].blend_state.src_alpha_blendfactor = sdlBlendFactor(b.srcAlpha);
        ct[i].blend_state.dst_alpha_blendfactor = sdlBlendFactor(b.dstAlpha);
        ct[i].blend_state.alpha_blend_op = sdlBlendOp(b.alphaOp);
        ct[i].blend_state.enable_color_write_mask = true;
        ct[i].blend_state.color_write_mask = SDL_GPUColorComponentFlags(((b.writeMask & kWriteR) ? SDL_GPU_COLORCOMPONENT_R : 0) |
                                                                        ((b.writeMask & kWriteG) ? SDL_GPU_COLORCOMPONENT_G : 0) |
                                                                        ((b.writeMask & kWriteB) ? SDL_GPU_COLORCOMPONENT_B : 0) |
                                                                        ((b.writeMask & kWriteA) ? SDL_GPU_COLORCOMPONENT_A : 0));
    }
    SDL_GPUGraphicsPipelineCreateInfo ci{};
    ci.vertex_shader = s->vertex, ci.fragment_shader = s->fragment;
    ci.vertex_input_state.vertex_buffer_descriptions = vb;
    ci.vertex_input_state.num_vertex_buffers = uint32_t(std::min(l.bufferCount, kMaxVertexBuffers));
    ci.vertex_input_state.vertex_attributes = va;
    ci.vertex_input_state.num_vertex_attributes = uint32_t(std::min(l.attributeCount, kMaxVertexAttributes));
    ci.primitive_type = sdlPrimitive(d.primitive);
    SDL_GPURasterizerState& r = ci.rasterizer_state;
    r.fill_mode = SDL_GPU_FILLMODE_FILL;
    r.cull_mode = d.raster.cull == CullMode::None ? SDL_GPU_CULLMODE_NONE : d.raster.cull == CullMode::Front ? SDL_GPU_CULLMODE_FRONT : SDL_GPU_CULLMODE_BACK;
    // As given: SDL_GPU's NDC is y up like GL's and its Vulkan driver flips the viewport itself (docs/render.md 3.2).
    r.front_face = d.raster.frontFace == FrontFace::CounterClockwise ? SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE : SDL_GPU_FRONTFACE_CLOCKWISE;
    r.enable_depth_bias = d.raster.depthBiasConstant != 0 || d.raster.depthBiasSlope != 0;
    r.depth_bias_constant_factor = d.raster.depthBiasConstant;
    r.depth_bias_slope_factor = d.raster.depthBiasSlope;
    r.enable_depth_clip = !d.raster.depthClamp;
    ci.multisample_state.sample_count = sdlSamples(std::min(d.samples, caps_.maxSamples));
    const DepthStencilState& ds = d.depthStencil;
    SDL_GPUDepthStencilState& z = ci.depth_stencil_state;
    // A depth write without a test is GL's "test Always" (the GL backend's choice too).
    z.enable_depth_test = ds.depthTest || ds.depthWrite;
    z.enable_depth_write = ds.depthWrite;
    z.compare_op = ds.depthTest ? sdlCompare(ds.depthCompare) : SDL_GPU_COMPAREOP_ALWAYS;
    z.enable_stencil_test = ds.stencil;
    z.compare_mask = ds.readMask, z.write_mask = ds.writeMask;
    auto face = [](const StencilFace& f) {
        SDL_GPUStencilOpState s{};
        s.fail_op = sdlStencilOp(f.fail), s.depth_fail_op = sdlStencilOp(f.depthFail), s.pass_op = sdlStencilOp(f.pass);
        s.compare_op = sdlCompare(f.compare);
        return s;
    };
    z.front_stencil_state = face(ds.front), z.back_stencil_state = face(ds.back);
    ci.target_info.color_target_descriptions = ct;
    ci.target_info.num_color_targets = uint32_t(std::min(d.colorCount, kMaxColorTargets));
    ci.target_info.has_depth_stencil_target = d.depthFormat != Format::Undefined;
    ci.target_info.depth_stencil_format = d.depthFormat != Format::Undefined ? mapFormat(d.depthFormat) : SDL_GPU_TEXTUREFORMAT_INVALID;
    GpuPipeline p;
    p.desc = d;
    p.vi = s->vi, p.fi = s->fi;
    p.pipeline = SDL_CreateGPUGraphicsPipeline(dev_, &ci);
    if (!p.pipeline) {
        fail(std::string("createPipeline") + (d.label ? std::string(" ") + d.label : std::string()) + ": " + SDL_GetError());
        return {};
    }
    return Pipeline{pipelines_.add(p)};
}

void SdlGpuDevice::destroyPipeline(Pipeline p) {
    GpuPipeline* pl = pipelines_.get(p.id);
    if (!pl) return;
    if (inPass_) deadPipelines_.push_back(pl->pipeline);
    else SDL_ReleaseGPUGraphicsPipeline(dev_, pl->pipeline);
    pipelines_.remove(p.id);
}

// ---------------------------------------------------------------- copies, readback

void SdlGpuDevice::copyTexture(const TextureRegion& src, const TextureRegion& dst, int w, int h) {
    const GpuTexture* s = textures_.get(src.texture.id);
    const GpuTexture* d = textures_.get(dst.texture.id);
    if (!s || !d || inPass_ || w <= 0 || h <= 0) return;
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd());
    SDL_GPUTextureLocation a{s->texture, uint32_t(src.mip), uint32_t(src.layer), uint32_t(src.x), uint32_t(src.y), 0};
    SDL_GPUTextureLocation b{d->texture, uint32_t(dst.mip), uint32_t(dst.layer), uint32_t(dst.x), uint32_t(dst.y), 0};
    SDL_CopyGPUTextureToTexture(cp, &a, &b, uint32_t(w), uint32_t(h), 1, false);
    SDL_EndGPUCopyPass(cp);
}

void SdlGpuDevice::blit(const TextureRegion& src, int sw, int sh, const TextureRegion& dst, int dw, int dh, Filter filter) {
    const GpuTexture* s = textures_.get(src.texture.id);
    const GpuTexture* d = textures_.get(dst.texture.id);
    if (!s || !d || inPass_ || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    SDL_GPUBlitInfo bi{};
    bi.source = {s->texture, uint32_t(src.mip), uint32_t(src.layer), uint32_t(src.x), uint32_t(src.y), uint32_t(sw), uint32_t(sh)};
    bi.destination = {d->texture, uint32_t(dst.mip), uint32_t(dst.layer), uint32_t(dst.x), uint32_t(dst.y), uint32_t(dw), uint32_t(dh)};
    bi.load_op = SDL_GPU_LOADOP_LOAD;  // the rest of the destination stays
    bi.flip_mode = SDL_FLIP_NONE;
    bi.filter = filter == Filter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(cmd(), &bi);
}

Readback SdlGpuDevice::readTexture(Texture tx, const Rect& r) {
    const GpuTexture* t = textures_.get(tx.id);
    if (!t || inPass_ || r.w <= 0 || r.h <= 0 || isDepthFormat(t->desc.format) || isCompressedFormat(t->desc.format) || t->desc.samples > 1)
        return {};
    GpuReadback rb;
    rb.size = size_t(r.w) * size_t(r.h) * formatBytes(t->desc.format);
    SDL_GPUTransferBufferCreateInfo ci{};
    ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, ci.size = uint32_t(rb.size);
    rb.transfer = SDL_CreateGPUTransferBuffer(dev_, &ci);
    if (!rb.transfer) {
        fail(std::string("readTexture: ") + SDL_GetError());
        return {};
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd());
    SDL_GPUTextureRegion src{};
    src.texture = t->texture, src.x = uint32_t(r.x), src.y = uint32_t(r.y), src.w = uint32_t(r.w), src.h = uint32_t(r.h), src.d = 1;
    SDL_GPUTextureTransferInfo dst{rb.transfer, 0, uint32_t(r.w), uint32_t(r.h)};
    SDL_DownloadFromGPUTexture(cp, &src, &dst);  // rows top-down, as the interface's
    SDL_EndGPUCopyPass(cp);
    rb.serial = cmdSerial_;
    return Readback{readbacks_.add(rb)};
}

bool SdlGpuDevice::readbackReady(Readback r) {
    GpuReadback* rb = readbacks_.get(r.id);
    if (!rb) return false;
    if (cmd_ && rb->serial == cmdSerial_) submit();  // still being recorded: send it (a poll must make progress)
    pollFences();
    return completed_ >= rb->serial;
}

bool SdlGpuDevice::readbackData(Readback r, void* dst, size_t size) {
    GpuReadback* rb = readbacks_.get(r.id);
    if (!rb) return false;
    waitSerial(rb->serial);
    bool ok = false;
    if (const void* p = SDL_MapGPUTransferBuffer(dev_, rb->transfer, false)) {
        std::memcpy(dst, p, std::min(size, rb->size));
        SDL_UnmapGPUTransferBuffer(dev_, rb->transfer);
        ok = true;
    }
    SDL_ReleaseGPUTransferBuffer(dev_, rb->transfer);
    readbacks_.remove(r.id);
    return ok;
}

// ---------------------------------------------------------------- frames and passes

void SdlGpuDevice::beginFrame() {
    if (inPass_) endPass();
    ++frameIndex_;
    // At most kFramesInFlight frames on the GPU: wait for frame N - 2.
    if (frameIndex_ > uint64_t(kFramesInFlight)) waitSerial(frameSerial_[(frameIndex_ - kFramesInFlight) % (kFramesInFlight + 1)]);
    else pollFences();
    for (Ring* r : {&vertexRing_, &indexRing_}) {
        r->slot = int(frameIndex_ % (kFramesInFlight + 1));
        for (auto& c : r->slots[r->slot]) c.used = 0;
    }
}

void SdlGpuDevice::beginPass(const PassDesc& p) {
    if (inPass_) endPass();
    pass_ = p;
    passW_ = passH_ = 0;
    if (p.colorCount > 0) {
        if (const GpuTexture* t = textures_.get(p.colors[0].texture.id))
            passW_ = std::max(1, t->desc.width >> p.colors[0].mip), passH_ = std::max(1, t->desc.height >> p.colors[0].mip);
    } else if (const GpuTexture* t = textures_.get(p.depth.texture.id)) {
        passW_ = t->desc.width, passH_ = t->desc.height;
    }
    cmds_.clear(), recBuffers_.clear(), recTextures_.clear(), recBytes_.clear();
    inPass_ = true;
}

void SdlGpuDevice::setViewport(const Rect& r, float minDepth, float maxDepth) {
    if (!inPass_) return;
    Cmd c;
    c.op = Op::Viewport;
    c.f[0] = float(r.x), c.f[1] = float(r.y), c.f[2] = float(r.w), c.f[3] = float(r.h), c.f[4] = minDepth, c.f[5] = maxDepth;
    cmds_.push_back(c);
}

void SdlGpuDevice::setScissor(const Rect& r) {
    if (!inPass_) return;
    Cmd c;
    c.op = Op::Scissor;
    c.e = r.x, c.a = uint32_t(r.y), c.b = uint32_t(std::max(0, r.w)), c.c = uint32_t(std::max(0, r.h));
    cmds_.push_back(c);
}

void SdlGpuDevice::setStencilRef(uint8_t ref) {
    if (!inPass_) return;
    Cmd c;
    c.op = Op::StencilRef, c.a = ref;
    cmds_.push_back(c);
}

void SdlGpuDevice::setBlendConstants(const float rgba[4]) {
    if (!inPass_) return;
    Cmd c;
    c.op = Op::BlendConstants;
    std::copy_n(rgba, 4, c.f);
    cmds_.push_back(c);
}

void SdlGpuDevice::bindPipeline(Pipeline p) {
    if (!inPass_ || !pipelines_.get(p.id)) return;
    Cmd c;
    c.op = Op::Pipeline, c.a = p.id;
    cmds_.push_back(c);
}

void SdlGpuDevice::bindVertexBuffers(int first, const BufferRange* b, int count) {
    if (!inPass_ || count <= 0) return;
    Cmd c;
    c.op = Op::VertexBuffers, c.a = uint32_t(first), c.b = uint32_t(count), c.c = uint32_t(recBuffers_.size());
    recBuffers_.insert(recBuffers_.end(), b, b + count);
    cmds_.push_back(c);
}

void SdlGpuDevice::bindIndexBuffer(const BufferRange& b, IndexType type) {
    if (!inPass_) return;
    Cmd c;
    c.op = Op::IndexBuffer, c.a = b.buffer.id, c.b = uint32_t(b.offset), c.c = type == IndexType::U16 ? 2 : 4;
    cmds_.push_back(c);
}

void SdlGpuDevice::bindTextures(int first, const TextureBinding* b, int count) {
    if (!inPass_ || count <= 0) return;
    Cmd c;
    c.op = Op::Textures, c.a = uint32_t(first), c.b = uint32_t(count), c.c = uint32_t(recTextures_.size());
    recTextures_.insert(recTextures_.end(), b, b + count);
    cmds_.push_back(c);
}

void SdlGpuDevice::pushUniforms(int slot, const void* data, size_t size) {
    if (slot < 0 || slot >= kMaxUniformSlots || !size || !data) return;
    size = std::min(size, kMaxUniformBytes);
    if (size > kSdlUniformBytes) {
        if (!toldUniformSize_) fail("pushUniforms: " + std::to_string(size) + " bytes; SDL_GPU reads at most 4096 per block");
        toldUniformSize_ = true;
        size = kSdlUniformBytes;
    }
    if (!inPass_) {
        // Between passes: the slot's data now (the passes recorded so far were replayed already).
        uniformData_[slot].assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        ++uniformVersion_[slot];
        return;
    }
    Cmd c;
    c.op = Op::Uniforms, c.a = uint32_t(slot), c.c = uint32_t(recBytes_.size()), c.d = uint32_t(size);
    recBytes_.insert(recBytes_.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    cmds_.push_back(c);
}

void SdlGpuDevice::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (!inPass_ || !vertexCount || !instanceCount) return;
    Cmd c;
    c.op = Op::Draw, c.a = vertexCount, c.b = instanceCount, c.c = firstVertex, c.d = firstInstance;
    cmds_.push_back(c);
}

void SdlGpuDevice::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!inPass_ || !indexCount || !instanceCount) return;
    Cmd c;
    c.op = Op::DrawIndexed, c.a = indexCount, c.b = instanceCount, c.c = firstIndex, c.d = firstInstance, c.e = vertexOffset;
    cmds_.push_back(c);
}

void SdlGpuDevice::endPass() {
    if (!inPass_) return;
    inPass_ = false;
    replayPass();
    cmds_.clear(), recBuffers_.clear(), recTextures_.clear(), recBytes_.clear();
    releaseDeferred();
}

void SdlGpuDevice::replayPass() {
    // 1. The transient data the pass draws from.
    flushTransient();
    SDL_GPUCommandBuffer* cb = cmd();
    if (!cb) return;
    // 2. The SDL render pass.
    SDL_GPUColorTargetInfo colors[kMaxColorTargets] = {};
    int colorCount = 0;
    for (int i = 0; i < pass_.colorCount && i < kMaxColorTargets; ++i) {
        const ColorAttachment& a = pass_.colors[i];
        const GpuTexture* t = textures_.get(a.texture.id);
        if (!t) continue;
        SDL_GPUColorTargetInfo& c = colors[colorCount++];
        c.texture = t->texture;
        c.mip_level = uint32_t(a.mip), c.layer_or_depth_plane = uint32_t(a.layer);
        c.clear_color = SDL_FColor{a.clear[0], a.clear[1], a.clear[2], a.clear[3]};
        c.load_op = sdlLoad(a.load);
        c.store_op = sdlStore(a.store);
        const GpuTexture* rt = textures_.get(a.resolve.id);
        if ((a.store == StoreOp::Resolve || a.store == StoreOp::ResolveAndStore) && !rt) c.store_op = SDL_GPU_STOREOP_STORE;
        if (rt && (a.store == StoreOp::Resolve || a.store == StoreOp::ResolveAndStore)) c.resolve_texture = rt->texture;
    }
    SDL_GPUDepthStencilTargetInfo depth{};
    const GpuTexture* dt = textures_.get(pass_.depth.texture.id);
    if (dt) {
        const bool stencil = hasStencil(dt->desc.format);
        depth.texture = dt->texture;
        depth.clear_depth = pass_.depth.clearDepth;
        depth.load_op = sdlLoad(pass_.depth.load);
        depth.store_op = pass_.depth.store == StoreOp::DontCare ? SDL_GPU_STOREOP_DONT_CARE : SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = stencil ? sdlLoad(pass_.depth.stencilLoad) : SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = stencil && pass_.depth.stencilStore != StoreOp::DontCare ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
        depth.clear_stencil = pass_.depth.clearStencil;
    }
    if (colorCount == 0 && !dt) return;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cb, colors, uint32_t(colorCount), dt ? &depth : nullptr);
    if (!rp) {
        fail(std::string("SDL_BeginGPURenderPass: ") + SDL_GetError());
        return;
    }
    // 3. The commands. The state rules (docs/render.md 2.2): viewport and scissor
    // start as the whole attachment; pipeline, buffers, textures unbound.
    const SDL_GPUViewport full{0, 0, float(passW_), float(passH_), 0, 1};
    SDL_SetGPUViewport(rp, &full);
    const SDL_Rect fullRect{0, 0, passW_, passH_};
    SDL_SetGPUScissor(rp, &fullRect);
    const GpuPipeline* pl = nullptr;
    uint32_t plId = 0;
    BufferRange vbufs[kMaxVertexBuffers];
    bool vbDirty = true;
    uint32_t ibuf = 0, ibOffset = 0, ibSize = 4;
    bool ibDirty = true;
    TextureBinding tex[kMaxTextureSlots];
    bool texDirty = true;
    const auto uniformsFor = [&](const SpirvStageInfo& info, int stage) {
        for (int s = 0; s < kMaxUniformSlots; ++s) {
            if (!(info.uniformMask & (1u << s)) || pushedVersion_[stage][s] == uniformVersion_[s]) continue;
            const std::vector<uint8_t>& data = uniformData_[s];
            if (stage == 0) SDL_PushGPUVertexUniformData(cb, uint32_t(s), data.data(), uint32_t(data.size()));
            else SDL_PushGPUFragmentUniformData(cb, uint32_t(s), data.data(), uint32_t(data.size()));
            pushedVersion_[stage][s] = uniformVersion_[s];
        }
    };
    const auto samplersFor = [&](const SpirvStageInfo& info, bool vertex) {
        const uint32_t n = info.numSamplers();
        if (!n) return;
        SDL_GPUTextureSamplerBinding b[kMaxTextureSlots] = {};
        for (uint32_t s = 0; s < n && s < uint32_t(kMaxTextureSlots); ++s) {
            const GpuTexture* t = textures_.get(tex[s].texture.id);
            const uint32_t sid = tex[s].sampler.id;
            const bool used = info.samplerMask & (1u << s);
            if (used && t && t->desc.samples == 1 && sid && sid <= samplers_.size()) b[s] = {t->texture, samplers_[sid - 1].second};
            else b[s] = {dummy_, dummySampler_};  // unused below the highest, or nothing bound (GL: an empty unit)
        }
        if (vertex) SDL_BindGPUVertexSamplers(rp, 0, b, n);
        else SDL_BindGPUFragmentSamplers(rp, 0, b, n);
    };
    const auto prepare = [&]() -> bool {
        if (!pl) return false;
        if (vbDirty) {
            const int n = std::min(pl->desc.layout.bufferCount, kMaxVertexBuffers);
            SDL_GPUBufferBinding b[kMaxVertexBuffers] = {};
            for (int i = 0; i < n; ++i) {
                const GpuBuffer* buf = buffers_.get(vbufs[i].buffer.id);
                if (!buf) return false;
                b[i] = {buf->buffer, uint32_t(vbufs[i].offset)};
            }
            if (n) SDL_BindGPUVertexBuffers(rp, 0, b, uint32_t(n));
            vbDirty = false;
        }
        if (texDirty) {
            samplersFor(pl->vi, true);
            samplersFor(pl->fi, false);
            texDirty = false;
        }
        uniformsFor(pl->vi, 0);
        uniformsFor(pl->fi, 1);
        return true;
    };
    for (const Cmd& c : cmds_) {
        switch (c.op) {
        case Op::Viewport: {
            const SDL_GPUViewport v{c.f[0], c.f[1], std::max(c.f[2], 1.0f), std::max(c.f[3], 1.0f), c.f[4], c.f[5]};
            SDL_SetGPUViewport(rp, &v);
            break;
        }
        case Op::Scissor: {
            // Vulkan wants a non-negative offset: cut the part outside instead.
            int x = c.e, y = int32_t(c.a), w = int(c.b), h = int(c.c);
            if (x < 0) w += x, x = 0;
            if (y < 0) h += y, y = 0;
            const SDL_Rect r{x, y, std::max(w, 0), std::max(h, 0)};
            SDL_SetGPUScissor(rp, &r);
            break;
        }
        case Op::StencilRef: SDL_SetGPUStencilReference(rp, Uint8(c.a)); break;
        case Op::BlendConstants: SDL_SetGPUBlendConstants(rp, SDL_FColor{c.f[0], c.f[1], c.f[2], c.f[3]}); break;
        case Op::Pipeline:
            if (c.a != plId) {
                pl = pipelines_.get(c.a), plId = c.a;
                if (pl) SDL_BindGPUGraphicsPipeline(rp, pl->pipeline);
                // A new pipeline layout: SDL wants the vertex buffers and samplers bound again for it.
                vbDirty = texDirty = ibDirty = true;
            }
            break;
        case Op::VertexBuffers:
            for (uint32_t i = 0; i < c.b && c.a + i < uint32_t(kMaxVertexBuffers); ++i) vbufs[c.a + i] = recBuffers_[c.c + i];
            vbDirty = true;
            break;
        case Op::IndexBuffer:
            ibuf = c.a, ibOffset = c.b, ibSize = c.c;
            ibDirty = true;
            break;
        case Op::Textures:
            for (uint32_t i = 0; i < c.b && c.a + i < uint32_t(kMaxTextureSlots); ++i) tex[c.a + i] = recTextures_[c.c + i];
            texDirty = true;
            break;
        case Op::Uniforms:
            uniformData_[c.a].assign(recBytes_.begin() + c.c, recBytes_.begin() + c.c + c.d);
            ++uniformVersion_[c.a];
            break;
        case Op::Draw:
            if (prepare()) SDL_DrawGPUPrimitives(rp, c.a, c.b, c.c, c.d);
            break;
        case Op::DrawIndexed: {
            if (!prepare()) break;
            const GpuBuffer* ib = buffers_.get(ibuf);
            if (!ib) break;
            if (ibDirty) {
                const SDL_GPUBufferBinding b{ib->buffer, ibOffset};
                SDL_BindGPUIndexBuffer(rp, &b, ibSize == 2 ? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT);
                ibDirty = false;
            }
            SDL_DrawGPUIndexedPrimitives(rp, c.a, c.b, c.c, c.e, c.d);
            break;
        }
        }
    }
    SDL_EndGPURenderPass(rp);
}

void SdlGpuDevice::present(Texture src, const Rect& r, Filter filter, bool flipVertical) {
    if (inPass_) endPass();
    SDL_GPUCommandBuffer* cb = cmd();
    if (!cb) return;
    SDL_GPUTexture* swap = nullptr;
    Uint32 sw = 0, sh = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cb, info_.window, &swap, &sw, &sh)) fail(std::string("swapchain: ") + SDL_GetError());
    const GpuTexture* t = textures_.get(src.id);
    if (swap && t && r.w > 0 && r.h > 0) {  // no swapchain texture: minimised, nothing to show
        SDL_GPUBlitInfo bi{};
        bi.source = {t->texture, 0, 0, uint32_t(r.x), uint32_t(r.y), uint32_t(r.w), uint32_t(r.h)};
        bi.destination = {swap, 0, 0, 0, 0, sw, sh};
        bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
        bi.flip_mode = flipVertical ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE;
        bi.filter = filter == Filter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(cb, &bi);
    }
    submit();  // presents the swapchain texture
}

void SdlGpuDevice::setPresentMode(PresentMode mode) {
    info_.presentMode = mode;
    SDL_GPUPresentMode m = mode == PresentMode::Immediate ? SDL_GPU_PRESENTMODE_IMMEDIATE
                           : mode == PresentMode::Mailbox ? SDL_GPU_PRESENTMODE_MAILBOX
                                                          : SDL_GPU_PRESENTMODE_VSYNC;
    // Mailbox, else immediate, else vsync (always there [C: SDL_gpu.h]).
    if (m == SDL_GPU_PRESENTMODE_MAILBOX && !SDL_WindowSupportsGPUPresentMode(dev_, info_.window, m)) m = SDL_GPU_PRESENTMODE_IMMEDIATE;
    if (m == SDL_GPU_PRESENTMODE_IMMEDIATE && !SDL_WindowSupportsGPUPresentMode(dev_, info_.window, m)) m = SDL_GPU_PRESENTMODE_VSYNC;
    if (m == presentMode_ && frameIndex_ > 0) return;
    if (SDL_SetGPUSwapchainParameters(dev_, info_.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, m)) presentMode_ = m;
}

void SdlGpuDevice::endFrame() {
    if (inPass_) endPass();
    submit();
    frameSerial_[frameIndex_ % (kFramesInFlight + 1)] = cmdSerial_;
}

}  // namespace

std::unique_ptr<Device> createSdlGpuDevice(const DeviceCreateInfo& info, std::string& error) {
    auto d = std::make_unique<SdlGpuDevice>(info);
    if (!d->init(error)) return nullptr;
    return d;
}

}  // namespace gd::core::render
