// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// gd::core::render: the graphics interface every game of the engine draws
// through, and the factory of its backends (OpenGL 3.3 / ES 3.0 now; SDL3 GPU
// and Vulkan next). The design, the conventions and how each call maps onto
// each API: gd-engine-core/docs/render.md. Plain C++17, no exceptions: a
// failed creation returns an empty handle and lastError() says why.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct SDL_Window;

namespace gd::core::render {

// ---------------------------------------------------------------- limits

constexpr int kMaxColorTargets = 4;
constexpr int kMaxTextureSlots = 8;   // combined texture + sampler slots
constexpr int kMaxUniformSlots = 4;   // std140 blocks (SDL_GPU: 4 per stage)
constexpr int kMaxVertexBuffers = 4;
constexpr int kMaxVertexAttributes = 16;
constexpr size_t kMaxUniformBytes = 16384;  // per pushUniforms
constexpr int kFramesInFlight = 2;

// ---------------------------------------------------------------- handles

#define GD_RENDER_HANDLE(Name)                                         \
    struct Name {                                                      \
        uint32_t id = 0;                                               \
        explicit operator bool() const { return id != 0; }             \
        bool operator==(const Name& o) const { return id == o.id; }    \
        bool operator!=(const Name& o) const { return id != o.id; }    \
    };
GD_RENDER_HANDLE(Buffer)
GD_RENDER_HANDLE(Texture)
GD_RENDER_HANDLE(Sampler)
GD_RENDER_HANDLE(Shader)
GD_RENDER_HANDLE(Pipeline)
GD_RENDER_HANDLE(Readback)
#undef GD_RENDER_HANDLE

// ---------------------------------------------------------------- backends

enum class Backend : uint8_t { OpenGL3, SdlGpu, Vulkan };
// The settings ids: "opengl3", "sdl3", "vulkan".
const char* backendId(Backend b);
// False (and `out` untouched) for any other text, e.g. "local".
bool parseBackend(const char* id, Backend& out);
// Whether this build contains the backend (it may still fail to start).
bool backendCompiled(Backend b);

// ---------------------------------------------------------------- formats

enum class Format : uint8_t {
    Undefined,
    RGBA8, RGBA8_SRGB, BGRA8, R8, RG8, RGB10A2, RG11B10F,
    R16F, RG16F, RGBA16F, R32F, RGBA32F,
    BC1, BC1_SRGB, BC2, BC2_SRGB, BC3, BC3_SRGB, BC4, BC5, BC7, BC7_SRGB,
    D16, D24, D24S8, D32F, D32FS8,
    Count
};
bool isDepthFormat(Format f);
bool hasStencil(Format f);
bool isCompressedFormat(Format f);
// Bytes per pixel (uncompressed) or per 4x4 block (compressed).
size_t formatBytes(Format f);
// Bytes of one w x h image of the format (rows tightly packed).
size_t imageBytes(Format f, int w, int h);

enum FormatSupport : uint8_t {
    kSampled = 1,      // can be sampled
    kFiltered = 2,     // linear filtering
    kRenderable = 4,   // colour or depth target
    kBlendable = 8,    // blending while rendering to it
    kResolvable = 16,  // MSAA target that can be resolved
};

// ---------------------------------------------------------------- buffers

enum BufferUsage : uint8_t { kVertexBuffer = 1, kIndexBuffer = 2, kUniformBuffer = 4 };
struct BufferDesc {
    size_t size = 0;
    uint8_t usage = kVertexBuffer;  // BufferUsage bits
    bool dynamic = false;           // updated often (a hint)
    const char* label = nullptr;
};
// A slice of a buffer: what allocTransient returns and what draws bind.
struct BufferRange {
    Buffer buffer;
    size_t offset = 0;
    size_t size = 0;
    explicit operator bool() const { return bool(buffer); }
};
enum class IndexType : uint8_t { U16, U32 };

// ---------------------------------------------------------------- textures

enum class TextureType : uint8_t { Tex2D, Cube };
enum TextureUsage : uint8_t { kSampledTexture = 1, kColorTarget = 2, kDepthTarget = 4 };
struct TextureDesc {
    TextureType type = TextureType::Tex2D;
    Format format = Format::RGBA8;
    int width = 0, height = 0;
    int mips = 1;     // 0: the full chain down to 1 x 1
    int samples = 1;  // > 1: an MSAA target (render target only, not sampled; resolve it)
    uint8_t usage = kSampledTexture;  // TextureUsage bits
    const char* label = nullptr;
};
// A rectangle of pixels, origin top-left.
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};
// One region of one mip level (and cube face, 0..5: +x -x +y -y +z -z) of a texture.
struct TextureRegion {
    Texture texture;
    int mip = 0, layer = 0;
    int x = 0, y = 0;  // top-left
};

enum class Filter : uint8_t { Nearest, Linear };
enum class MipFilter : uint8_t { None, Nearest, Linear };
enum class Wrap : uint8_t { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };
enum class BorderColor : uint8_t { TransparentBlack, OpaqueBlack, OpaqueWhite };
enum class CompareOp : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
struct SamplerDesc {
    Filter minFilter = Filter::Linear, magFilter = Filter::Linear;
    MipFilter mipFilter = MipFilter::Linear;
    Wrap wrapU = Wrap::Repeat, wrapV = Wrap::Repeat, wrapW = Wrap::Repeat;
    float lodBias = 0, minLod = -1000, maxLod = 1000;
    float maxAnisotropy = 1;  // > 1: anisotropic (where supported)
    bool compare = false;     // depth comparison (shadow samplers)
    CompareOp compareOp = CompareOp::LessEqual;
    BorderColor border = BorderColor::TransparentBlack;
    bool operator==(const SamplerDesc& o) const;
};

// ---------------------------------------------------------------- shaders

enum ShaderStageBits : uint8_t { kVertexStage = 1, kFragmentStage = 2 };

// One program: a vertex and a fragment shader in every form this build has,
// and the names the backend needs to bind it. The backend uses the first
// form it can (docs/render.md section 3).
struct ShaderDesc {
    const char* name = "";
    // The gd shader dialect (GLSL, docs/render.md section 3): the bodies
    // without a #version line, and "#define X 1\n" lines for this permutation.
    const char* glslVertex = nullptr;
    const char* glslFragment = nullptr;
    const char* defines = nullptr;
    // SPIR-V (SDL3 GPU and Vulkan), compiled from the same source at build
    // time (gdcore_spirv_shaders(), docs/render.md 3.3): the dialect's sets
    // and bindings, SPIR-V 1.0, entry point "main".
    const uint32_t* spirvVertex = nullptr;
    size_t spirvVertexBytes = 0;
    const uint32_t* spirvFragment = nullptr;
    size_t spirvFragmentBytes = 0;
    // Reflection: the uniform block name of each slot and the sampler name of
    // each texture slot (nullptr / "" = unused), and which stages use each.
    const char* uniformBlocks[kMaxUniformSlots] = {};
    uint8_t uniformStages[kMaxUniformSlots] = {kVertexStage | kFragmentStage, kVertexStage | kFragmentStage,
                                               kVertexStage | kFragmentStage, kVertexStage | kFragmentStage};
    const char* samplers[kMaxTextureSlots] = {};
    uint8_t samplerStages[kMaxTextureSlots] = {kFragmentStage, kFragmentStage, kFragmentStage, kFragmentStage,
                                               kFragmentStage, kFragmentStage, kFragmentStage, kFragmentStage};

    // Sets the SPIR-V form from the arrays gdcore_shaderc generates
    // (k<Name>VsSpirv, k<Name>FsSpirv): desc.spirv(kMainVsSpirv, kMainFsSpirv).
    template <size_t V, size_t F>
    ShaderDesc& spirv(const uint32_t (&vertex)[V], const uint32_t (&fragment)[F]) {
        spirvVertex = vertex, spirvVertexBytes = sizeof vertex;
        spirvFragment = fragment, spirvFragmentBytes = sizeof fragment;
        return *this;
    }
};

// ---------------------------------------------------------------- pipelines

enum class VertexFormat : uint8_t {
    Float1, Float2, Float3, Float4,
    UByte4Norm, Byte4Norm, UShort2Norm, Short2Norm, Half2, Half4, UInt1, Int1
};
struct VertexAttribute {
    uint8_t location = 0;
    uint8_t buffer = 0;  // vertex buffer slot
    VertexFormat format = VertexFormat::Float4;
    uint32_t offset = 0;
};
struct VertexBufferLayout {
    uint32_t stride = 0;
    bool perInstance = false;
};
struct VertexLayout {
    VertexBufferLayout buffers[kMaxVertexBuffers];
    int bufferCount = 0;
    VertexAttribute attributes[kMaxVertexAttributes];
    int attributeCount = 0;
};

enum class Primitive : uint8_t { Triangles, TriangleStrip, Lines, LineStrip, Points };
enum class CullMode : uint8_t { None, Front, Back };
enum class FrontFace : uint8_t { CounterClockwise, Clockwise };  // in GL clip space (y up)
enum class BlendFactor : uint8_t {
    Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor,
    SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha, ConstantColor, OneMinusConstantColor, SrcAlphaSaturate
};
enum class BlendOp : uint8_t { Add, Subtract, ReverseSubtract, Min, Max };
enum ColorWrite : uint8_t { kWriteR = 1, kWriteG = 2, kWriteB = 4, kWriteA = 8, kWriteRGB = 7, kWriteAll = 15 };
enum class StencilOp : uint8_t { Keep, Zero, Replace, IncrementClamp, DecrementClamp, Invert, IncrementWrap, DecrementWrap };

struct BlendState {
    bool enable = false;
    BlendFactor srcColor = BlendFactor::One, dstColor = BlendFactor::Zero;
    BlendOp colorOp = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One, dstAlpha = BlendFactor::Zero;
    BlendOp alphaOp = BlendOp::Add;
    uint8_t writeMask = kWriteAll;
};
struct StencilFace {
    StencilOp fail = StencilOp::Keep, depthFail = StencilOp::Keep, pass = StencilOp::Keep;
    CompareOp compare = CompareOp::Always;
};
struct DepthStencilState {
    bool depthTest = false, depthWrite = false;
    CompareOp depthCompare = CompareOp::LessEqual;
    bool stencil = false;
    StencilFace front, back;
    uint8_t readMask = 0xff, writeMask = 0xff;
};
struct RasterState {
    CullMode cull = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    // Depth bias, in GL glPolygonOffset terms (constant = units, slope = factor).
    float depthBiasConstant = 0, depthBiasSlope = 0;
    bool depthClamp = false;
};
struct PipelineDesc {
    Shader shader;
    VertexLayout layout;
    Primitive primitive = Primitive::Triangles;
    RasterState raster;
    DepthStencilState depthStencil;
    BlendState blend[kMaxColorTargets];
    // What it draws into: the formats of the pass's attachments and their samples.
    Format colorFormats[kMaxColorTargets] = {Format::RGBA8};
    int colorCount = 1;
    Format depthFormat = Format::Undefined;
    int samples = 1;
    const char* label = nullptr;
};

// ---------------------------------------------------------------- passes

enum class LoadOp : uint8_t { Load, Clear, DontCare };
enum class StoreOp : uint8_t { Store, DontCare, Resolve, ResolveAndStore };
struct ColorAttachment {
    Texture texture;
    int mip = 0, layer = 0;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    float clear[4] = {0, 0, 0, 0};
    Texture resolve;  // with StoreOp::Resolve*: the 1-sample texture it resolves into
};
struct DepthAttachment {
    Texture texture;
    LoadOp load = LoadOp::Clear, stencilLoad = LoadOp::Clear;
    StoreOp store = StoreOp::Store, stencilStore = StoreOp::DontCare;
    float clearDepth = 1;
    uint8_t clearStencil = 0;
};
struct PassDesc {
    ColorAttachment colors[kMaxColorTargets];
    int colorCount = 0;
    DepthAttachment depth;  // texture empty: none
    const char* label = nullptr;
};

struct TextureBinding {
    Texture texture;
    Sampler sampler;
};

// ---------------------------------------------------------------- device

struct Caps {
    std::string api;      // "OpenGL", "OpenGL ES", "Vulkan", "SDL_GPU (vulkan)"...
    std::string version;  // driver version
    std::string gpu;      // adapter name
    bool es = false;      // OpenGL ES / mobile class
    int maxTextureSize = 2048;
    int maxSamples = 1;
    float maxAnisotropy = 1;
    // Wrap::ClampToBorder and SamplerDesc::border work (OpenGL 3.3; not
    // SDL_GPU, which has no border colour, nor GLES 3.0: they clamp to the
    // edge instead).
    bool clampToBorder = false;
    uint8_t formats[size_t(Format::Count)] = {};  // FormatSupport bits per Format
    bool supports(Format f, uint8_t bits) const { return (formats[size_t(f)] & bits) == bits; }
};

enum class PresentMode : uint8_t { Vsync, Immediate, Mailbox };

// What a game gives the device to use the window. For OpenGL, a game that
// runs as a launcher module passes its own (shimmed) context calls, so the
// context belongs to the module (docs/render.md section 5); empty functions
// use SDL's.
struct GlHooks {
    std::function<void*(SDL_Window*)> createContext;     // SDL_GL_CreateContext
    std::function<void(void*)> destroyContext;           // SDL_GL_DestroyContext
    std::function<bool(SDL_Window*)> swapWindow;         // SDL_GL_SwapWindow
};
struct DeviceCreateInfo {
    Backend backend = Backend::OpenGL3;
    SDL_Window* window = nullptr;  // made with windowFlags(backend) (and prepareWindow called first)
    bool debug = false;            // API validation / debug output where available
    PresentMode presentMode = PresentMode::Vsync;
    GlHooks gl;
    // A folder the backend may keep its caches in across runs (the game's
    // pref folder; Vulkan: its pipeline cache). Empty: nothing on disk.
    std::string cacheDir;
};

// Window flags and attributes a backend needs: call prepareWindow before
// SDL_CreateWindow (it sets the GL attributes for OpenGL) and pass
// windowFlags in the window's flags. Decide the backend BEFORE making the
// window: in a launcher module, prepareWindow is also what asks the launcher
// for a window made for that API (docs/render.md section 5; the SDL global
// property kWindowApiRequestProperty, read by the module shim's
// SDL_CreateWindow). A game whose device then fails to start destroys the
// window, calls prepareWindow(Backend::OpenGL3, ...) and makes it again with
// SDL_WINDOW_OPENGL (rule 2: never a black screen).
uint64_t windowFlags(Backend b);
void prepareWindow(Backend b, bool gles);

// The SDL global properties shared with the launcher's module shim
// (gd-engine-launcher/sdk/gd_host.h GD_PROP_WINDOW_API_REQUEST /
// GD_PROP_WINDOW_API / GD_PROP_WINDOW): what the game asks for (a string, set
// by prepareWindow), and the launcher's window with what it is made for (a
// pointer and a string, set by the shim; absent outside a launcher).
// createDevice refuses a backend that window is not made for.
constexpr const char* kWindowApiRequestProperty = "gd.render.window_api_request";
constexpr const char* kWindowApiProperty = "gd.render.window_api";
constexpr const char* kWindowProperty = "gd.render.window";

class Device {
public:
    virtual ~Device() = default;
    virtual Backend backend() const = 0;
    virtual const Caps& caps() const = 0;
    // Why the last creation failed (empty if none did).
    virtual const std::string& lastError() const = 0;

    // Resources ---------------------------------------------------------
    virtual Buffer createBuffer(const BufferDesc& desc, const void* data = nullptr) = 0;
    // Outside a pass. Draws recorded before see the old contents.
    virtual void updateBuffer(Buffer b, size_t offset, const void* data, size_t size) = 0;
    virtual void destroyBuffer(Buffer b) = 0;
    // Data that lives this frame only (vertices of immediate draws,
    // uniform data shared by many draws): copied now; usable inside a pass.
    virtual BufferRange allocTransient(uint8_t usage, const void* data, size_t size) = 0;

    virtual Texture createTexture(const TextureDesc& desc) = 0;
    // Outside a pass: replaces a w x h rectangle of one level (and face);
    // `rowBytes` 0 = tightly packed. Compressed formats: whole 4x4 blocks.
    virtual void updateTexture(const TextureRegion& dst, int w, int h, const void* data, size_t rowBytes = 0) = 0;
    // Outside a pass: fills levels 1.. from level 0 (colour formats that are filterable and renderable).
    virtual void generateMipmaps(Texture t) = 0;
    virtual void destroyTexture(Texture t) = 0;
    virtual TextureDesc textureDesc(Texture t) const = 0;

    virtual Sampler createSampler(const SamplerDesc& desc) = 0;  // the same desc returns the same sampler
    virtual Shader createShader(const ShaderDesc& desc) = 0;
    virtual void destroyShader(Shader s) = 0;
    virtual Pipeline createPipeline(const PipelineDesc& desc) = 0;
    virtual void destroyPipeline(Pipeline p) = 0;

    // Copies (outside a pass) ---------------------------------------------
    // Same format, same size: an exact copy of a w x h rectangle.
    virtual void copyTexture(const TextureRegion& src, const TextureRegion& dst, int w, int h) = 0;
    // Scaled copy between colour textures (formats may differ).
    virtual void blit(const TextureRegion& src, int sw, int sh, const TextureRegion& dst, int dw, int dh, Filter filter) = 0;
    // Starts an asynchronous read of a colour texture's rectangle (level 0).
    virtual Readback readTexture(Texture t, const Rect& r) = 0;
    virtual bool readbackReady(Readback r) = 0;
    // Copies the pixels (rows top-down, tightly packed) into `dst`, waiting if
    // they are not ready yet, and frees the readback. False if `r` is unknown.
    virtual bool readbackData(Readback r, void* dst, size_t size) = 0;

    // Frames and passes ---------------------------------------------------
    // State rules every backend keeps (docs/render.md 2.2): beginPass resets
    // the viewport and the scissor to the whole attachment and unbinds the
    // pipeline and the vertex / index buffers; texture bindings, the stencil
    // reference and the blend constants must be set again after it (SDL_GPU
    // and Vulkan lose them with the pass); pushed uniforms stay until the
    // frame ends (push them in every frame before its first draw).
    virtual void beginFrame() = 0;
    virtual void beginPass(const PassDesc& pass) = 0;
    virtual void setViewport(const Rect& r, float minDepth = 0, float maxDepth = 1) = 0;
    virtual void setScissor(const Rect& r) = 0;
    virtual void setStencilRef(uint8_t ref) = 0;
    virtual void setBlendConstants(const float rgba[4]) = 0;
    virtual void bindPipeline(Pipeline p) = 0;
    virtual void bindVertexBuffers(int firstSlot, const BufferRange* buffers, int count) = 0;
    virtual void bindIndexBuffer(const BufferRange& buffer, IndexType type) = 0;
    virtual void bindTextures(int firstSlot, const TextureBinding* bindings, int count) = 0;
    virtual void pushUniforms(int slot, const void* data, size_t size) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                             uint32_t firstInstance = 0) = 0;
    virtual void endPass() = 0;
    virtual bool inPass() const = 0;

    // The window ------------------------------------------------------------
    virtual void drawableSize(int& w, int& h) const = 0;
    // Scales `src`'s rectangle to the whole window and presents it;
    // flipVertical shows it upside down (a game that stores its frame
    // bottom-up, GL style).
    virtual void present(Texture src, const Rect& srcRect, Filter filter, bool flipVertical = false) = 0;
    virtual void setPresentMode(PresentMode mode) = 0;
    virtual void endFrame() = 0;

    // Debugging ---------------------------------------------------------------
    virtual void pushDebugGroup(const char* name) { (void)name; }
    virtual void popDebugGroup() {}
};

// The backend's device on `info.window`, or nullptr with `error` set (the
// game then falls back to its own renderer and logs `error`).
std::unique_ptr<Device> createDevice(const DeviceCreateInfo& info, std::string& error);

}  // namespace gd::core::render
