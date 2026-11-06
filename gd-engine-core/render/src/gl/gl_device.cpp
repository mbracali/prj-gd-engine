// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The OpenGL3 backend: gd::core::render::Device on OpenGL 3.3 core or OpenGL
// ES 3.0 (docs/render.md section 4.1). Every render target is drawn upside
// down (gd_clip flips y, the front face is swapped) so that GL's row 0 is the
// interface's top row; present() flips the frame once into the window.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "gd/core/render/device.h"
#include "gl/gl_api.h"
#include "gl/gl_device.h"

namespace gd::core::render {
namespace {

namespace gl = glapi;

// Compressed and sRGB enums not in every GL header.
constexpr GLenum kBc1 = 0x83F1, kBc2 = 0x83F2, kBc3 = 0x83F3;
constexpr GLenum kBc1Srgb = 0x8C4D, kBc2Srgb = 0x8C4E, kBc3Srgb = 0x8C4F;
constexpr GLenum kBc4 = 0x8DBB, kBc5 = 0x8DBD, kBc7 = 0x8E8C, kBc7Srgb = 0x8E8D;
constexpr GLenum kTextureMaxAnisotropy = 0x84FE, kMaxTextureMaxAnisotropy = 0x84FF;
constexpr GLenum kTextureBorderColor = 0x1004, kClampToBorder = 0x812D;
constexpr GLenum kDepthClamp = 0x864F, kProgramPointSize = 0x8642;

struct GlFormat {
    GLenum internal = 0, format = 0, type = 0;
    bool compressed = false;
};

GlFormat glFormat(Format f) {
    switch (f) {
    case Format::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    case Format::RGBA8_SRGB: return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE};
    case Format::BGRA8: return {};  // no BGRA storage in GL 3.3 / ES 3.0
    case Format::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
    case Format::RG8: return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
    case Format::RGB10A2: return {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV};
    case Format::RG11B10F: return {GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV};
    case Format::R16F: return {GL_R16F, GL_RED, GL_HALF_FLOAT};
    case Format::RG16F: return {GL_RG16F, GL_RG, GL_HALF_FLOAT};
    case Format::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
    case Format::R32F: return {GL_R32F, GL_RED, GL_FLOAT};
    case Format::RGBA32F: return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
    case Format::BC1: return {kBc1, 0, 0, true};
    case Format::BC1_SRGB: return {kBc1Srgb, 0, 0, true};
    case Format::BC2: return {kBc2, 0, 0, true};
    case Format::BC2_SRGB: return {kBc2Srgb, 0, 0, true};
    case Format::BC3: return {kBc3, 0, 0, true};
    case Format::BC3_SRGB: return {kBc3Srgb, 0, 0, true};
    case Format::BC4: return {kBc4, 0, 0, true};
    case Format::BC5: return {kBc5, 0, 0, true};
    case Format::BC7: return {kBc7, 0, 0, true};
    case Format::BC7_SRGB: return {kBc7Srgb, 0, 0, true};
    case Format::D16: return {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT};
    case Format::D24: return {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT};
    case Format::D24S8: return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8};
    case Format::D32F: return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
    case Format::D32FS8: return {GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV};
    default: return {};
    }
}

GLenum glCompare(CompareOp op) {
    static const GLenum m[] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    return m[size_t(op)];
}
GLenum glBlendFactor(BlendFactor f) {
    static const GLenum m[] = {GL_ZERO,      GL_ONE,           GL_SRC_COLOR,     GL_ONE_MINUS_SRC_COLOR,   GL_DST_COLOR,
                               GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
                               GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_SRC_ALPHA_SATURATE};
    return m[size_t(f)];
}
GLenum glBlendOp(BlendOp op) {
    static const GLenum m[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
    return m[size_t(op)];
}
GLenum glStencilOp(StencilOp op) {
    static const GLenum m[] = {GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
    return m[size_t(op)];
}
GLenum glWrap(Wrap w) {
    switch (w) {
    case Wrap::Repeat: return GL_REPEAT;
    case Wrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
    case Wrap::ClampToEdge: return GL_CLAMP_TO_EDGE;
    case Wrap::ClampToBorder: return kClampToBorder;
    }
    return GL_REPEAT;
}
GLenum glPrimitive(Primitive p) {
    switch (p) {
    case Primitive::Triangles: return GL_TRIANGLES;
    case Primitive::TriangleStrip: return GL_TRIANGLE_STRIP;
    case Primitive::Lines: return GL_LINES;
    case Primitive::LineStrip: return GL_LINE_STRIP;
    case Primitive::Points: return GL_POINTS;
    }
    return GL_TRIANGLES;
}
struct GlAttrib {
    GLint size;
    GLenum type;
    GLboolean normalized;
    bool integer;
};
GlAttrib glAttrib(VertexFormat f) {
    switch (f) {
    case VertexFormat::Float1: return {1, GL_FLOAT, GL_FALSE, false};
    case VertexFormat::Float2: return {2, GL_FLOAT, GL_FALSE, false};
    case VertexFormat::Float3: return {3, GL_FLOAT, GL_FALSE, false};
    case VertexFormat::Float4: return {4, GL_FLOAT, GL_FALSE, false};
    case VertexFormat::UByte4Norm: return {4, GL_UNSIGNED_BYTE, GL_TRUE, false};
    case VertexFormat::Byte4Norm: return {4, GL_BYTE, GL_TRUE, false};
    case VertexFormat::UShort2Norm: return {2, GL_UNSIGNED_SHORT, GL_TRUE, false};
    case VertexFormat::Short2Norm: return {2, GL_SHORT, GL_TRUE, false};
    case VertexFormat::Half2: return {2, GL_HALF_FLOAT, GL_FALSE, false};
    case VertexFormat::Half4: return {4, GL_HALF_FLOAT, GL_FALSE, false};
    case VertexFormat::UInt1: return {1, GL_UNSIGNED_INT, GL_FALSE, true};
    case VertexFormat::Int1: return {1, GL_INT, GL_FALSE, true};
    }
    return {4, GL_FLOAT, GL_FALSE, false};
}

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

struct GlBuffer {
    GLuint name = 0;
    BufferDesc desc;
    GLenum target = GL_ARRAY_BUFFER;
};
struct GlTexture {
    GLuint name = 0;          // texture (sampled or 1-sample target)
    GLuint renderbuffer = 0;  // MSAA targets
    TextureDesc desc;
    GLenum target = GL_TEXTURE_2D;
};
struct GlShader {
    GLuint program = 0;
};
struct GlPipeline {
    PipelineDesc desc;
    GLuint vao = 0;
    GLuint program = 0;
};
struct GlReadback {
    GLuint pbo = 0;
    GLsync fence = nullptr;
    size_t size = 0;
};

// Per-frame ring of buffers (transient vertex / index / uniform data). Each
// frame slot has its own chunks, reused when that slot comes back
// (kFramesInFlight + 1 slots: the GPU is done with it by then).
struct Ring {
    GLenum target = GL_ARRAY_BUFFER;
    uint8_t usage = kVertexBuffer;
    size_t chunkSize = 1 << 20;
    struct Chunk {
        uint32_t buffer = 0;  // Buffer id
        size_t size = 0, used = 0;
    };
    std::vector<Chunk> slots[kFramesInFlight + 1];
    int slot = 0;
};

constexpr const char* kPreludeCommon =
    "#define GD_GL 1\n"
    "#define GD_UNIFORMS(slot, name) layout(std140) uniform name\n"
    "#define GD_SAMPLER2D(slot, name) uniform sampler2D name\n"
    "#define GD_SAMPLER2DSHADOW(slot, name) uniform sampler2DShadow name\n"
    "#define GD_SAMPLERCUBE(slot, name) uniform samplerCube name\n"
    "#define GD_ATTR(loc) layout(location = loc) in\n"
    "#define GD_IN(loc) in\n"
    "#define GD_OUT(loc) out\n"
    "#define GD_FRAG_OUT(loc) layout(location = loc) out\n"
    "#define GD_VERTEX_ID gl_VertexID\n"
    "#define GD_INSTANCE_ID gl_InstanceID\n";

class GlDevice final : public Device {
public:
    GlDevice(const DeviceCreateInfo& info) : info_(info) {}
    ~GlDevice() override;
    bool init(std::string& error);

    Backend backend() const override { return Backend::OpenGL3; }
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
        const GlTexture* tx = textures_.get(t.id);
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
    void present(Texture src, const Rect& srcRect, Filter filter) override;
    void setPresentMode(PresentMode mode) override;
    void endFrame() override;

private:
    bool fail(const std::string& e) {
        error_ = e;
        SDL_Log("gdcore render (opengl3): %s", e.c_str());
        return false;
    }
    GLuint framebufferFor(const Texture* colors, const int* mips, const int* layers, int count, Texture depth);
    GLuint readFramebuffer(const TextureRegion& r);
    void attach(GLenum point, const TextureRegion& r);
    BufferRange ringAlloc(Ring& ring, const void* data, size_t size, size_t align);
    void applyVertexBuffers(int32_t vertexOffset);
    void setFullState();
    void unbindAllDrawFbos();

    DeviceCreateInfo info_;
    Caps caps_;
    std::string error_;
    SDL_GLContext context_ = nullptr;
    bool es_ = false;
    GLint uboAlign_ = 256;

    Pool<GlBuffer> buffers_;
    Pool<GlTexture> textures_;
    Pool<GlShader> shaders_;
    Pool<GlPipeline> pipelines_;
    Pool<GlReadback> readbacks_;
    std::vector<std::pair<SamplerDesc, GLuint>> samplers_;  // id = index + 1

    // Framebuffers by attachments.
    using FboKey = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int, int>;
    std::map<FboKey, GLuint> fbos_;
    GLuint scratchFbo_[2] = {0, 0};  // copies and blits

    Ring vertexRing_, indexRing_, uniformRing_;
    GLsync fences_[2] = {nullptr, nullptr};

    // Pass state.
    bool inPass_ = false;
    PassDesc pass_;
    int passW_ = 0, passH_ = 0;
    GLuint passFbo_ = 0, resolveFbo_[kMaxColorTargets] = {};
    const GlPipeline* pipeline_ = nullptr;
    uint32_t pipelineId_ = 0;
    BufferRange vbufs_[kMaxVertexBuffers];
    bool vbufsDirty_ = true;
    int32_t appliedVertexOffset_ = 0;
    BufferRange ibuf_;
    IndexType itype_ = IndexType::U32;
    uint8_t stencilRef_ = 0;
};

GlDevice::~GlDevice() {
    if (!context_) return;
    SDL_GL_MakeCurrent(info_.window, context_);
    for (size_t i = 0; i < buffers_.items.size(); ++i)
        if (buffers_.live[i]) gl::glDeleteBuffers(1, &buffers_.items[i].name);
    for (size_t i = 0; i < textures_.items.size(); ++i)
        if (textures_.live[i]) {
            if (textures_.items[i].name) gl::glDeleteTextures(1, &textures_.items[i].name);
            if (textures_.items[i].renderbuffer) gl::glDeleteRenderbuffers(1, &textures_.items[i].renderbuffer);
        }
    for (size_t i = 0; i < shaders_.items.size(); ++i)
        if (shaders_.live[i]) gl::glDeleteProgram(shaders_.items[i].program);
    for (size_t i = 0; i < pipelines_.items.size(); ++i)
        if (pipelines_.live[i]) gl::glDeleteVertexArrays(1, &pipelines_.items[i].vao);
    for (size_t i = 0; i < readbacks_.items.size(); ++i)
        if (readbacks_.live[i]) {
            gl::glDeleteBuffers(1, &readbacks_.items[i].pbo);
            if (readbacks_.items[i].fence) gl::glDeleteSync(readbacks_.items[i].fence);
        }
    for (auto& [d, s] : samplers_) gl::glDeleteSamplers(1, &s);
    for (auto& [k, f] : fbos_) gl::glDeleteFramebuffers(1, &f);
    gl::glDeleteFramebuffers(2, scratchFbo_);
    for (GLsync& f : fences_)
        if (f) gl::glDeleteSync(f);
    if (info_.gl.destroyContext) info_.gl.destroyContext(context_);
    else SDL_GL_DestroyContext(context_);
}

bool GlDevice::init(std::string& error) {
    context_ = info_.gl.createContext ? static_cast<SDL_GLContext>(info_.gl.createContext(info_.window)) : SDL_GL_CreateContext(info_.window);
    if (!context_) return error = std::string("no OpenGL context: ") + SDL_GetError(), fail(error);
    SDL_GL_MakeCurrent(info_.window, context_);
    if (const char* missing = gl::load()) return error = std::string("OpenGL function missing: ") + missing, fail(error);
    const char* version = reinterpret_cast<const char*>(gl::glGetString(GL_VERSION));
    const char* renderer = reinterpret_cast<const char*>(gl::glGetString(GL_RENDERER));
    es_ = version && std::strncmp(version, "OpenGL ES", 9) == 0;
    caps_.api = es_ ? "OpenGL ES" : "OpenGL";
    caps_.version = version ? version : "";
    caps_.gpu = renderer ? renderer : "";
    caps_.es = es_;
    GLint v = 0;
    gl::glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
    caps_.maxTextureSize = v;
    gl::glGetIntegerv(GL_MAX_SAMPLES, &v);
    caps_.maxSamples = std::max(1, v);
    gl::glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uboAlign_);
    uboAlign_ = std::max(uboAlign_, 16);
    if (gl::hasExtension("GL_EXT_texture_filter_anisotropic") || gl::hasExtension("GL_ARB_texture_filter_anisotropic")) {
        float a = 1;
        gl::glGetFloatv(kMaxTextureMaxAnisotropy, &a);
        caps_.maxAnisotropy = a;
    }
    // Formats.
    const bool floatTargets = !es_ || gl::hasExtension("GL_EXT_color_buffer_float");
    const bool halfTargets = floatTargets || gl::hasExtension("GL_EXT_color_buffer_half_float");
    const bool s3tc = gl::hasExtension("GL_EXT_texture_compression_s3tc") || gl::hasExtension("GL_EXT_texture_compression_dxt1");
    const bool s3tcSrgb = s3tc && (gl::hasExtension("GL_EXT_texture_sRGB") || gl::hasExtension("GL_EXT_texture_compression_s3tc_srgb"));
    const bool rgtc = !es_ || gl::hasExtension("GL_EXT_texture_compression_rgtc");
    const bool bptc = gl::hasExtension("GL_ARB_texture_compression_bptc") || gl::hasExtension("GL_EXT_texture_compression_bptc");
    const uint8_t all = kSampled | kFiltered | kRenderable | kBlendable | kResolvable, tex = kSampled | kFiltered;
    auto set = [&](Format f, uint8_t bits) { caps_.formats[size_t(f)] = bits; };
    set(Format::RGBA8, all);
    set(Format::RGBA8_SRGB, all);
    set(Format::R8, all);
    set(Format::RG8, all);
    set(Format::RGB10A2, all);
    set(Format::RG11B10F, floatTargets ? all : tex);
    set(Format::R16F, halfTargets ? all : tex);
    set(Format::RG16F, halfTargets ? all : tex);
    set(Format::RGBA16F, halfTargets ? all : tex);
    set(Format::R32F, uint8_t((es_ ? uint8_t(kSampled) : tex) | (floatTargets ? uint8_t(kRenderable) : uint8_t(0))));
    set(Format::RGBA32F, uint8_t((es_ ? uint8_t(kSampled) : tex) | (floatTargets ? uint8_t(kRenderable) : uint8_t(0))));
    for (Format f : {Format::BC1, Format::BC2, Format::BC3}) set(f, s3tc ? tex : 0);
    for (Format f : {Format::BC1_SRGB, Format::BC2_SRGB, Format::BC3_SRGB}) set(f, s3tcSrgb ? tex : 0);
    for (Format f : {Format::BC4, Format::BC5}) set(f, rgtc ? tex : 0);
    for (Format f : {Format::BC7, Format::BC7_SRGB}) set(f, bptc ? tex : 0);
    for (Format f : {Format::D16, Format::D24, Format::D24S8, Format::D32F, Format::D32FS8}) set(f, kSampled | kRenderable | kResolvable);

    vertexRing_.target = GL_ARRAY_BUFFER, vertexRing_.usage = kVertexBuffer;
    indexRing_.target = GL_ELEMENT_ARRAY_BUFFER, indexRing_.usage = kIndexBuffer;
    uniformRing_.target = GL_UNIFORM_BUFFER, uniformRing_.usage = kUniformBuffer;
    gl::glGenFramebuffers(2, scratchFbo_);
    if (!es_) gl::glEnable(kProgramPointSize);
    setPresentMode(info_.presentMode);
    SDL_Log("gdcore render: %s %s on %s", caps_.api.c_str(), caps_.version.c_str(), caps_.gpu.c_str());
    return true;
}

// ---------------------------------------------------------------- buffers

Buffer GlDevice::createBuffer(const BufferDesc& desc, const void* data) {
    GlBuffer b;
    b.desc = desc;
    b.target = (desc.usage & kIndexBuffer) ? GL_ELEMENT_ARRAY_BUFFER : (desc.usage & kUniformBuffer) ? GL_UNIFORM_BUFFER : GL_ARRAY_BUFFER;
    gl::glGenBuffers(1, &b.name);
    // An element array buffer binding is VAO state: upload through COPY_WRITE so no VAO is touched.
    gl::glBindBuffer(GL_COPY_WRITE_BUFFER, b.name);
    gl::glBufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(desc.size), data, desc.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    gl::glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    return Buffer{buffers_.add(b)};
}

void GlDevice::updateBuffer(Buffer b, size_t offset, const void* data, size_t size) {
    GlBuffer* buf = buffers_.get(b.id);
    if (!buf || !size) return;
    gl::glBindBuffer(GL_COPY_WRITE_BUFFER, buf->name);
    if (offset == 0 && size == buf->desc.size) gl::glBufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(size), data, buf->desc.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    else gl::glBufferSubData(GL_COPY_WRITE_BUFFER, GLintptr(offset), GLsizeiptr(size), data);
    gl::glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void GlDevice::destroyBuffer(Buffer b) {
    if (GlBuffer* buf = buffers_.get(b.id)) gl::glDeleteBuffers(1, &buf->name);
    buffers_.remove(b.id);
}

BufferRange GlDevice::ringAlloc(Ring& ring, const void* data, size_t size, size_t align) {
    auto& chunks = ring.slots[ring.slot];
    for (auto& c : chunks) {
        size_t at = (c.used + align - 1) / align * align;
        if (at + size <= c.size) {
            c.used = at + size;
            GlBuffer* buf = buffers_.get(c.buffer);
            gl::glBindBuffer(GL_COPY_WRITE_BUFFER, buf->name);
            gl::glBufferSubData(GL_COPY_WRITE_BUFFER, GLintptr(at), GLsizeiptr(size), data);
            gl::glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            return BufferRange{Buffer{c.buffer}, at, size};
        }
    }
    BufferDesc d;
    d.size = std::max(ring.chunkSize, size);
    d.usage = ring.usage;
    d.dynamic = true;
    Ring::Chunk c;
    c.buffer = createBuffer(d, nullptr).id;
    c.size = d.size;
    chunks.push_back(c);
    return ringAlloc(ring, data, size, align);
}

BufferRange GlDevice::allocTransient(uint8_t usage, const void* data, size_t size) {
    if (!size) return {};
    if (usage & kUniformBuffer) return ringAlloc(uniformRing_, data, size, size_t(uboAlign_));
    if (usage & kIndexBuffer) return ringAlloc(indexRing_, data, size, 4);
    return ringAlloc(vertexRing_, data, size, 16);
}

// ---------------------------------------------------------------- textures

Texture GlDevice::createTexture(const TextureDesc& in) {
    TextureDesc desc = in;
    const GlFormat f = glFormat(desc.format);
    if (!f.internal || desc.width <= 0 || desc.height <= 0) {
        fail("createTexture: unsupported format or size");
        return {};
    }
    if (desc.mips <= 0) {
        int n = 1;
        for (int s = std::max(desc.width, desc.height); s > 1; s >>= 1) ++n;
        desc.mips = n;
    }
    GlTexture t;
    t.desc = desc;
    t.target = desc.type == TextureType::Cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    if (desc.samples > 1) {
        gl::glGenRenderbuffers(1, &t.renderbuffer);
        gl::glBindRenderbuffer(GL_RENDERBUFFER, t.renderbuffer);
        gl::glRenderbufferStorageMultisample(GL_RENDERBUFFER, std::min(desc.samples, caps_.maxSamples), f.internal, desc.width, desc.height);
        gl::glBindRenderbuffer(GL_RENDERBUFFER, 0);
        return Texture{textures_.add(t)};
    }
    gl::glGenTextures(1, &t.name);
    gl::glBindTexture(t.target, t.name);
    const int faces = desc.type == TextureType::Cube ? 6 : 1;
    for (int m = 0; m < desc.mips; ++m) {
        int w = std::max(1, desc.width >> m), h = std::max(1, desc.height >> m);
        for (int face = 0; face < faces; ++face) {
            GLenum target = desc.type == TextureType::Cube ? GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face) : GL_TEXTURE_2D;
            if (f.compressed) {
                // Storage only (data comes with updateTexture): a zeroed image of the right size.
                std::vector<uint8_t> zero(imageBytes(desc.format, w, h));
                gl::glCompressedTexImage2D(target, m, f.internal, w, h, 0, GLsizei(zero.size()), zero.data());
            } else {
                gl::glTexImage2D(target, m, GLint(f.internal), w, h, 0, f.format, f.type, nullptr);
            }
        }
    }
    gl::glTexParameteri(t.target, GL_TEXTURE_BASE_LEVEL, 0);
    gl::glTexParameteri(t.target, GL_TEXTURE_MAX_LEVEL, desc.mips - 1);
    // Sampler objects override these; defaults for completeness without one.
    // (depth: nearest, the only filtering OpenGL ES allows on depth textures)
    const bool depth = isDepthFormat(desc.format);
    gl::glTexParameteri(t.target, GL_TEXTURE_MIN_FILTER, GLint(depth ? GL_NEAREST : desc.mips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR));
    gl::glTexParameteri(t.target, GL_TEXTURE_MAG_FILTER, GLint(depth ? GL_NEAREST : GL_LINEAR));
    gl::glBindTexture(t.target, 0);
    return Texture{textures_.add(t)};
}

void GlDevice::updateTexture(const TextureRegion& dst, int w, int h, const void* data, size_t rowBytes) {
    GlTexture* t = textures_.get(dst.texture.id);
    if (!t || !t->name || w <= 0 || h <= 0) return;
    const GlFormat f = glFormat(t->desc.format);
    const GLenum target = t->desc.type == TextureType::Cube ? GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + dst.layer) : GL_TEXTURE_2D;
    gl::glBindTexture(t->target, t->name);
    if (f.compressed) {
        // Whole blocks; rows of blocks must be tightly packed (rowBytes ignored).
        gl::glCompressedTexSubImage2D(target, dst.mip, dst.x, dst.y, w, h, f.internal, GLsizei(imageBytes(t->desc.format, w, h)), data);
    } else {
        const size_t px = formatBytes(t->desc.format);
        const size_t tight = size_t(w) * px;
        gl::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (rowBytes && rowBytes != tight) {
            if (rowBytes % px == 0) {
                gl::glPixelStorei(GL_UNPACK_ROW_LENGTH, GLint(rowBytes / px));
                gl::glTexSubImage2D(target, dst.mip, dst.x, dst.y, w, h, f.format, f.type, data);
                gl::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            } else {
                std::vector<uint8_t> packed(tight * size_t(h));
                for (int y = 0; y < h; ++y) std::memcpy(&packed[tight * size_t(y)], static_cast<const uint8_t*>(data) + rowBytes * size_t(y), tight);
                gl::glTexSubImage2D(target, dst.mip, dst.x, dst.y, w, h, f.format, f.type, packed.data());
            }
        } else {
            gl::glTexSubImage2D(target, dst.mip, dst.x, dst.y, w, h, f.format, f.type, data);
        }
    }
    gl::glBindTexture(t->target, 0);
}

void GlDevice::generateMipmaps(Texture tx) {
    GlTexture* t = textures_.get(tx.id);
    if (!t || !t->name || t->desc.mips <= 1) return;
    gl::glBindTexture(t->target, t->name);
    gl::glGenerateMipmap(t->target);
    gl::glBindTexture(t->target, 0);
}

void GlDevice::destroyTexture(Texture tx) {
    GlTexture* t = textures_.get(tx.id);
    if (!t) return;
    // Framebuffers that use it go too.
    for (auto it = fbos_.begin(); it != fbos_.end();) {
        const FboKey& k = it->first;
        if (std::get<0>(k) == tx.id || std::get<1>(k) == tx.id || std::get<2>(k) == tx.id || std::get<3>(k) == tx.id ||
            std::get<4>(k) == tx.id) {
            gl::glDeleteFramebuffers(1, &it->second);
            it = fbos_.erase(it);
        } else {
            ++it;
        }
    }
    if (t->name) gl::glDeleteTextures(1, &t->name);
    if (t->renderbuffer) gl::glDeleteRenderbuffers(1, &t->renderbuffer);
    textures_.remove(tx.id);
}

Sampler GlDevice::createSampler(const SamplerDesc& d) {
    for (size_t i = 0; i < samplers_.size(); ++i)
        if (samplers_[i].first == d) return Sampler{uint32_t(i + 1)};
    GLuint s = 0;
    gl::glGenSamplers(1, &s);
    GLenum minf = d.minFilter == Filter::Nearest ? GL_NEAREST : GL_LINEAR;
    if (d.mipFilter == MipFilter::Nearest) minf = d.minFilter == Filter::Nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST;
    if (d.mipFilter == MipFilter::Linear) minf = d.minFilter == Filter::Nearest ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
    gl::glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, GLint(minf));
    gl::glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, d.magFilter == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
    gl::glSamplerParameteri(s, GL_TEXTURE_WRAP_S, GLint(glWrap(d.wrapU)));
    gl::glSamplerParameteri(s, GL_TEXTURE_WRAP_T, GLint(glWrap(d.wrapV)));
    gl::glSamplerParameteri(s, GL_TEXTURE_WRAP_R, GLint(glWrap(d.wrapW)));
    gl::glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, d.minLod);
    gl::glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, d.maxLod);
    if (!es_ && d.lodBias != 0) gl::glSamplerParameterf(s, GL_TEXTURE_LOD_BIAS, d.lodBias);
    if (d.maxAnisotropy > 1 && caps_.maxAnisotropy > 1)
        gl::glSamplerParameterf(s, kTextureMaxAnisotropy, std::min(d.maxAnisotropy, caps_.maxAnisotropy));
    if (d.compare) {
        gl::glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        gl::glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC, GLint(glCompare(d.compareOp)));
    }
    if (!es_ && (d.wrapU == Wrap::ClampToBorder || d.wrapV == Wrap::ClampToBorder || d.wrapW == Wrap::ClampToBorder)) {
        const float c[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1, 1}};
        gl::glSamplerParameterfv(s, kTextureBorderColor, c[size_t(d.border)]);
    }
    samplers_.push_back({d, s});
    return Sampler{uint32_t(samplers_.size())};
}

// ---------------------------------------------------------------- shaders, pipelines

namespace {
GLuint compileStage(GLenum type, const std::string& src, std::string& error) {
    GLuint s = gl::glCreateShader(type);
    const char* p = src.c_str();
    gl::glShaderSource(s, 1, &p, nullptr);
    gl::glCompileShader(s);
    GLint ok = 0;
    gl::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl::glGetShaderInfoLog(s, sizeof log, nullptr, log);
        error = std::string(type == GL_VERTEX_SHADER ? "vertex: " : "fragment: ") + log;
        gl::glDeleteShader(s);
        return 0;
    }
    return s;
}
}  // namespace

Shader GlDevice::createShader(const ShaderDesc& d) {
    if (!d.glslVertex || !d.glslFragment) {
        fail(std::string("shader ") + d.name + ": no GLSL source (this backend compiles the gd shader dialect)");
        return {};
    }
    std::string head = es_ ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
                             "precision highp samplerCube;\nprecision highp sampler2DShadow;\n"
                           : "#version 330 core\n";
    head += kPreludeCommon;
    if (d.defines) head += d.defines;
    // Every target is drawn upside down (docs/render.md 2.3): y flips in clip space.
    std::string vs = head + "#define GD_VERTEX 1\nvec4 gd_clip(vec4 p) { return vec4(p.x, -p.y, p.z, p.w); }\n#line 1\n" + d.glslVertex;
    std::string fs = head + "#define GD_FRAGMENT 1\n#line 1\n" + d.glslFragment;
    std::string err;
    GLuint v = compileStage(GL_VERTEX_SHADER, vs, err);
    GLuint f = v ? compileStage(GL_FRAGMENT_SHADER, fs, err) : 0;
    if (!f) {
        if (v) gl::glDeleteShader(v);
        fail(std::string("shader ") + d.name + ": " + err);
        return {};
    }
    GLuint prog = gl::glCreateProgram();
    gl::glAttachShader(prog, v);
    gl::glAttachShader(prog, f);
    gl::glLinkProgram(prog);
    gl::glDeleteShader(v);
    gl::glDeleteShader(f);
    GLint ok = 0;
    gl::glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl::glGetProgramInfoLog(prog, sizeof log, nullptr, log);
        gl::glDeleteProgram(prog);
        fail(std::string("shader ") + d.name + ": link: " + log);
        return {};
    }
    for (int i = 0; i < kMaxUniformSlots; ++i)
        if (d.uniformBlocks[i] && *d.uniformBlocks[i]) {
            GLuint idx = gl::glGetUniformBlockIndex(prog, d.uniformBlocks[i]);
            if (idx != GL_INVALID_INDEX) gl::glUniformBlockBinding(prog, idx, GLuint(i));
        }
    gl::glUseProgram(prog);
    for (int i = 0; i < kMaxTextureSlots; ++i)
        if (d.samplers[i] && *d.samplers[i]) {
            GLint loc = gl::glGetUniformLocation(prog, d.samplers[i]);
            if (loc >= 0) gl::glUniform1i(loc, i);
        }
    gl::glUseProgram(0);
    pipeline_ = nullptr, pipelineId_ = 0;
    GlShader s;
    s.program = prog;
    return Shader{shaders_.add(s)};
}

void GlDevice::destroyShader(Shader s) {
    if (GlShader* sh = shaders_.get(s.id)) gl::glDeleteProgram(sh->program);
    shaders_.remove(s.id);
}

Pipeline GlDevice::createPipeline(const PipelineDesc& d) {
    const GlShader* s = shaders_.get(d.shader.id);
    if (!s) {
        fail("createPipeline: no shader");
        return {};
    }
    GlPipeline p;
    p.desc = d;
    p.program = s->program;
    gl::glGenVertexArrays(1, &p.vao);
    gl::glBindVertexArray(p.vao);
    for (int i = 0; i < d.layout.attributeCount; ++i) {
        const VertexAttribute& a = d.layout.attributes[i];
        gl::glEnableVertexAttribArray(a.location);
        if (d.layout.buffers[a.buffer].perInstance) gl::glVertexAttribDivisor(a.location, 1);
    }
    gl::glBindVertexArray(0);
    pipeline_ = nullptr, pipelineId_ = 0;
    return Pipeline{pipelines_.add(p)};
}

void GlDevice::destroyPipeline(Pipeline p) {
    if (GlPipeline* pl = pipelines_.get(p.id)) {
        gl::glDeleteVertexArrays(1, &pl->vao);
        if (pipelineId_ == p.id) pipeline_ = nullptr, pipelineId_ = 0;
    }
    pipelines_.remove(p.id);
}

// ---------------------------------------------------------------- framebuffers, copies

void GlDevice::attach(GLenum point, const TextureRegion& r) {
    const GlTexture* t = textures_.get(r.texture.id);
    if (!t) return;
    if (t->renderbuffer) {
        gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, point, GL_RENDERBUFFER, t->renderbuffer);
    } else {
        GLenum target = t->desc.type == TextureType::Cube ? GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + r.layer) : GL_TEXTURE_2D;
        gl::glFramebufferTexture2D(GL_FRAMEBUFFER, point, target, t->name, r.mip);
    }
}

static GLenum depthPoint(Format f) { return hasStencil(f) ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT; }

GLuint GlDevice::framebufferFor(const Texture* colors, const int* mips, const int* layers, int count, Texture depth) {
    uint32_t c[4] = {0, 0, 0, 0};
    for (int i = 0; i < count && i < 4; ++i) c[i] = colors[i].id;
    // Mip / layer of colour 0 (the common case); others at 0.
    FboKey key{c[0], c[1], c[2], c[3], depth.id, count > 0 ? mips[0] : 0, count > 0 ? layers[0] : 0};
    if (auto it = fbos_.find(key); it != fbos_.end()) return it->second;
    GLuint fbo = 0;
    gl::glGenFramebuffers(1, &fbo);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLenum draws[kMaxColorTargets] = {GL_NONE, GL_NONE, GL_NONE, GL_NONE};
    for (int i = 0; i < count; ++i) {
        attach(GLenum(GL_COLOR_ATTACHMENT0 + i), TextureRegion{colors[i], i == 0 ? mips[0] : 0, i == 0 ? layers[0] : 0});
        draws[i] = GLenum(GL_COLOR_ATTACHMENT0 + i);
    }
    if (const GlTexture* dt = textures_.get(depth.id)) attach(depthPoint(dt->desc.format), TextureRegion{depth});
    gl::glDrawBuffers(std::max(count, 1), draws);
    gl::glReadBuffer(count > 0 ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    GLenum status = gl::glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) fail("framebuffer incomplete (0x" + std::to_string(status) + ")");
    fbos_[key] = fbo;
    return fbo;
}

GLuint GlDevice::readFramebuffer(const TextureRegion& r) {
    const GlTexture* t = textures_.get(r.texture.id);
    if (!t) return 0;
    if (isDepthFormat(t->desc.format)) return framebufferFor(nullptr, nullptr, nullptr, 0, r.texture);
    int mip = r.mip, layer = r.layer;
    return framebufferFor(&r.texture, &mip, &layer, 1, Texture{});
}

void GlDevice::copyTexture(const TextureRegion& src, const TextureRegion& dst, int w, int h) {
    const GlTexture* s = textures_.get(src.texture.id);
    const GlTexture* d = textures_.get(dst.texture.id);
    if (!s || !d || inPass_) return;
    GLuint rf = readFramebuffer(src), df = readFramebuffer(dst);
    const bool depth = isDepthFormat(s->desc.format);
    gl::glDisable(GL_SCISSOR_TEST);
    gl::glBindFramebuffer(GL_READ_FRAMEBUFFER, rf);
    gl::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, df);
    if (!depth) gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    else gl::glDepthMask(GL_TRUE);
    GLbitfield mask = depth ? (GL_DEPTH_BUFFER_BIT | (hasStencil(s->desc.format) ? GL_STENCIL_BUFFER_BIT : 0)) : GL_COLOR_BUFFER_BIT;
    gl::glBlitFramebuffer(src.x, src.y, src.x + w, src.y + h, dst.x, dst.y, dst.x + w, dst.y + h, mask, GL_NEAREST);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
    pipeline_ = nullptr, pipelineId_ = 0;  // masks changed
}

void GlDevice::blit(const TextureRegion& src, int sw, int sh, const TextureRegion& dst, int dw, int dh, Filter filter) {
    if (inPass_ || !textures_.get(src.texture.id) || !textures_.get(dst.texture.id)) return;
    GLuint rf = readFramebuffer(src), df = readFramebuffer(dst);
    gl::glDisable(GL_SCISSOR_TEST);
    gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::glBindFramebuffer(GL_READ_FRAMEBUFFER, rf);
    gl::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, df);
    gl::glBlitFramebuffer(src.x, src.y, src.x + sw, src.y + sh, dst.x, dst.y, dst.x + dw, dst.y + dh, GL_COLOR_BUFFER_BIT,
                          filter == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
    pipeline_ = nullptr, pipelineId_ = 0;
}

Readback GlDevice::readTexture(Texture tx, const Rect& r) {
    const GlTexture* t = textures_.get(tx.id);
    if (!t || inPass_ || r.w <= 0 || r.h <= 0 || isDepthFormat(t->desc.format) || isCompressedFormat(t->desc.format)) return {};
    const GlFormat f = glFormat(t->desc.format);
    GlReadback rb;
    rb.size = size_t(r.w) * size_t(r.h) * formatBytes(t->desc.format);
    gl::glGenBuffers(1, &rb.pbo);
    gl::glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo);
    gl::glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(rb.size), nullptr, GL_STREAM_READ);
    gl::glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer(TextureRegion{tx}));
    gl::glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // Rows come out in GL order, which is the interface's top-down order (targets are drawn upside down).
    gl::glReadPixels(r.x, r.y, r.w, r.h, f.format, f.type, nullptr);
    gl::glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
    rb.fence = gl::glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    gl::glFlush();
    return Readback{readbacks_.add(rb)};
}

bool GlDevice::readbackReady(Readback r) {
    GlReadback* rb = readbacks_.get(r.id);
    if (!rb) return false;
    if (!rb->fence) return true;
    GLenum s = gl::glClientWaitSync(rb->fence, 0, 0);
    return s == GL_ALREADY_SIGNALED || s == GL_CONDITION_SATISFIED;
}

bool GlDevice::readbackData(Readback r, void* dst, size_t size) {
    GlReadback* rb = readbacks_.get(r.id);
    if (!rb) return false;
    if (rb->fence) {
        gl::glClientWaitSync(rb->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 5000000000ull);
        gl::glDeleteSync(rb->fence);
        rb->fence = nullptr;
    }
    gl::glBindBuffer(GL_PIXEL_PACK_BUFFER, rb->pbo);
    bool ok = false;
    if (const void* p = gl::glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(rb->size), GL_MAP_READ_BIT)) {
        std::memcpy(dst, p, std::min(size, rb->size));
        gl::glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        ok = true;
    }
    gl::glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl::glDeleteBuffers(1, &rb->pbo);
    readbacks_.remove(r.id);
    return ok;
}

// ---------------------------------------------------------------- frames and passes

void GlDevice::beginFrame() {
    for (Ring* r : {&vertexRing_, &indexRing_, &uniformRing_}) {
        r->slot = (r->slot + 1) % (kFramesInFlight + 1);
        for (auto& c : r->slots[r->slot]) c.used = 0;
    }
}

void GlDevice::beginPass(const PassDesc& p) {
    if (inPass_) endPass();
    pass_ = p;
    Texture colors[kMaxColorTargets];
    int mips[kMaxColorTargets] = {}, layers[kMaxColorTargets] = {};
    passW_ = passH_ = 0;
    for (int i = 0; i < p.colorCount; ++i) {
        colors[i] = p.colors[i].texture;
        mips[i] = p.colors[i].mip, layers[i] = p.colors[i].layer;
        if (const GlTexture* t = textures_.get(colors[i].id); t && i == 0)
            passW_ = std::max(1, t->desc.width >> p.colors[i].mip), passH_ = std::max(1, t->desc.height >> p.colors[i].mip);
    }
    if (p.colorCount == 0)
        if (const GlTexture* t = textures_.get(p.depth.texture.id)) passW_ = t->desc.width, passH_ = t->desc.height;
    passFbo_ = framebufferFor(colors, mips, layers, p.colorCount, p.depth.texture);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, passFbo_);
    inPass_ = true;
    // Clears: every mask open, no scissor.
    gl::glDisable(GL_SCISSOR_TEST);
    for (int i = 0; i < p.colorCount; ++i)
        if (p.colors[i].load == LoadOp::Clear) {
            gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            gl::glClearBufferfv(GL_COLOR, i, p.colors[i].clear);
        }
    if (const GlTexture* dt = textures_.get(p.depth.texture.id)) {
        const bool stencil = hasStencil(dt->desc.format);
        const bool cd = p.depth.load == LoadOp::Clear, cs = stencil && p.depth.stencilLoad == LoadOp::Clear;
        if (cd) gl::glDepthMask(GL_TRUE);
        if (cs) gl::glStencilMask(0xff);
        if (cd && cs) gl::glClearBufferfi(GL_DEPTH_STENCIL, 0, p.depth.clearDepth, p.depth.clearStencil);
        else if (cd) gl::glClearBufferfv(GL_DEPTH, 0, &p.depth.clearDepth);
        else if (cs) {
            GLint s = p.depth.clearStencil;
            gl::glClearBufferiv(GL_STENCIL, 0, &s);
        }
    }
    gl::glEnable(GL_SCISSOR_TEST);
    gl::glViewport(0, 0, passW_, passH_);
    gl::glScissor(0, 0, passW_, passH_);
    if (gl::glDepthRangeF) gl::glDepthRangeF(0, 1);
    else gl::glDepthRangeD(0, 1);
    pipeline_ = nullptr, pipelineId_ = 0;
    for (auto& v : vbufs_) v = {};
    ibuf_ = {};
    vbufsDirty_ = true;
}

void GlDevice::setViewport(const Rect& r, float minDepth, float maxDepth) {
    gl::glViewport(r.x, r.y, r.w, r.h);  // GL rows = the interface's rows (targets drawn upside down)
    if (gl::glDepthRangeF) gl::glDepthRangeF(minDepth, maxDepth);
    else gl::glDepthRangeD(minDepth, maxDepth);
}

void GlDevice::setScissor(const Rect& r) { gl::glScissor(r.x, r.y, std::max(0, r.w), std::max(0, r.h)); }

void GlDevice::setStencilRef(uint8_t ref) {
    stencilRef_ = ref;
    if (pipeline_ && pipeline_->desc.depthStencil.stencil) setFullState();
}

void GlDevice::setBlendConstants(const float c[4]) { gl::glBlendColor(c[0], c[1], c[2], c[3]); }

void GlDevice::setFullState() {
    const PipelineDesc& d = pipeline_->desc;
    gl::glUseProgram(pipeline_->program);
    gl::glBindVertexArray(pipeline_->vao);
    // Cull: targets are drawn upside down, so GL sees the opposite winding.
    if (d.raster.cull == CullMode::None) {
        gl::glDisable(GL_CULL_FACE);
    } else {
        gl::glEnable(GL_CULL_FACE);
        gl::glCullFace(d.raster.cull == CullMode::Front ? GL_FRONT : GL_BACK);
    }
    gl::glFrontFace(d.raster.frontFace == FrontFace::CounterClockwise ? GL_CW : GL_CCW);
    if (d.raster.depthBiasConstant != 0 || d.raster.depthBiasSlope != 0) {
        gl::glEnable(GL_POLYGON_OFFSET_FILL);
        gl::glPolygonOffset(d.raster.depthBiasSlope, d.raster.depthBiasConstant);
    } else {
        gl::glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (!es_) {
        if (d.raster.depthClamp) gl::glEnable(kDepthClamp);
        else gl::glDisable(kDepthClamp);
    }
    const DepthStencilState& ds = d.depthStencil;
    if (ds.depthTest || ds.depthWrite) {
        gl::glEnable(GL_DEPTH_TEST);
        gl::glDepthFunc(ds.depthTest ? glCompare(ds.depthCompare) : GL_ALWAYS);
    } else {
        gl::glDisable(GL_DEPTH_TEST);
    }
    gl::glDepthMask(ds.depthWrite ? GL_TRUE : GL_FALSE);
    if (ds.stencil) {
        gl::glEnable(GL_STENCIL_TEST);
        gl::glStencilFuncSeparate(GL_FRONT, glCompare(ds.front.compare), stencilRef_, ds.readMask);
        gl::glStencilFuncSeparate(GL_BACK, glCompare(ds.back.compare), stencilRef_, ds.readMask);
        // glFrontFace already compensates the upside-down targets: GL's front is the interface's.
        gl::glStencilOpSeparate(GL_FRONT, glStencilOp(ds.front.fail), glStencilOp(ds.front.depthFail), glStencilOp(ds.front.pass));
        gl::glStencilOpSeparate(GL_BACK, glStencilOp(ds.back.fail), glStencilOp(ds.back.depthFail), glStencilOp(ds.back.pass));
        gl::glStencilMask(ds.writeMask);
    } else {
        gl::glDisable(GL_STENCIL_TEST);
    }
    // Blend: OpenGL ES 3.0 has one blend state for every target: target 0's.
    const BlendState& b = d.blend[0];
    if (b.enable) {
        gl::glEnable(GL_BLEND);
        gl::glBlendFuncSeparate(glBlendFactor(b.srcColor), glBlendFactor(b.dstColor), glBlendFactor(b.srcAlpha), glBlendFactor(b.dstAlpha));
        gl::glBlendEquationSeparate(glBlendOp(b.colorOp), glBlendOp(b.alphaOp));
    } else {
        gl::glDisable(GL_BLEND);
    }
    gl::glColorMask((b.writeMask & kWriteR) != 0, (b.writeMask & kWriteG) != 0, (b.writeMask & kWriteB) != 0, (b.writeMask & kWriteA) != 0);
}

void GlDevice::bindPipeline(Pipeline p) {
    const GlPipeline* pl = pipelines_.get(p.id);
    if (!pl || !inPass_) return;
    if (pipelineId_ == p.id) return;
    const bool newVao = !pipeline_ || pipeline_->vao != pl->vao;
    pipeline_ = pl, pipelineId_ = p.id;
    setFullState();
    if (newVao) {
        vbufsDirty_ = true;
        if (ibuf_) gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers_.get(ibuf_.buffer.id)->name);
    }
}

void GlDevice::bindVertexBuffers(int first, const BufferRange* b, int count) {
    for (int i = 0; i < count && first + i < kMaxVertexBuffers; ++i) vbufs_[first + i] = b[i];
    vbufsDirty_ = true;
}

void GlDevice::bindIndexBuffer(const BufferRange& b, IndexType type) {
    ibuf_ = b, itype_ = type;
    if (pipeline_)
        if (const GlBuffer* buf = buffers_.get(b.buffer.id)) gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf->name);
}

void GlDevice::applyVertexBuffers(int32_t vertexOffset) {
    if (!pipeline_ || (!vbufsDirty_ && vertexOffset == appliedVertexOffset_)) return;
    const VertexLayout& l = pipeline_->desc.layout;
    for (int i = 0; i < l.attributeCount; ++i) {
        const VertexAttribute& a = l.attributes[i];
        const BufferRange& r = vbufs_[a.buffer];
        const GlBuffer* buf = buffers_.get(r.buffer.id);
        if (!buf) continue;
        const VertexBufferLayout& bl = l.buffers[a.buffer];
        size_t off = r.offset + a.offset + (bl.perInstance ? 0 : size_t(int64_t(vertexOffset) * bl.stride));
        gl::glBindBuffer(GL_ARRAY_BUFFER, buf->name);
        GlAttrib g = glAttrib(a.format);
        if (g.integer) gl::glVertexAttribIPointer(a.location, g.size, g.type, GLsizei(bl.stride), reinterpret_cast<const void*>(off));
        else gl::glVertexAttribPointer(a.location, g.size, g.type, g.normalized, GLsizei(bl.stride), reinterpret_cast<const void*>(off));
    }
    vbufsDirty_ = false;
    appliedVertexOffset_ = vertexOffset;
}

void GlDevice::bindTextures(int first, const TextureBinding* b, int count) {
    for (int i = 0; i < count && first + i < kMaxTextureSlots; ++i) {
        const GlTexture* t = textures_.get(b[i].texture.id);
        gl::glActiveTexture(GLenum(GL_TEXTURE0 + first + i));
        if (t) gl::glBindTexture(t->target, t->name);
        else gl::glBindTexture(GL_TEXTURE_2D, 0);
        gl::glBindSampler(GLuint(first + i), b[i].sampler.id && b[i].sampler.id <= samplers_.size() ? samplers_[b[i].sampler.id - 1].second : 0);
    }
    gl::glActiveTexture(GL_TEXTURE0);
}

void GlDevice::pushUniforms(int slot, const void* data, size_t size) {
    if (slot < 0 || slot >= kMaxUniformSlots || !size) return;
    BufferRange r = ringAlloc(uniformRing_, data, std::min(size, kMaxUniformBytes), size_t(uboAlign_));
    gl::glBindBufferRange(GL_UNIFORM_BUFFER, GLuint(slot), buffers_.get(r.buffer.id)->name, GLintptr(r.offset), GLsizeiptr(r.size));
}

void GlDevice::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (!pipeline_ || !vertexCount) return;
    (void)firstInstance;  // GL 3.3 / ES 3.0 have no base instance: per-instance data starts at the bound offset
    applyVertexBuffers(0);
    const GLenum prim = glPrimitive(pipeline_->desc.primitive);
    if (instanceCount <= 1) gl::glDrawArrays(prim, GLint(firstVertex), GLsizei(vertexCount));
    else gl::glDrawArraysInstanced(prim, GLint(firstVertex), GLsizei(vertexCount), GLsizei(instanceCount));
}

void GlDevice::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!pipeline_ || !indexCount || !ibuf_) return;
    (void)firstInstance;
    applyVertexBuffers(vertexOffset);  // no base vertex in ES 3.0: the attribute offsets move instead
    const GLenum prim = glPrimitive(pipeline_->desc.primitive);
    const GLenum type = itype_ == IndexType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    const size_t off = ibuf_.offset + size_t(firstIndex) * (itype_ == IndexType::U16 ? 2 : 4);
    if (instanceCount <= 1) gl::glDrawElements(prim, GLsizei(indexCount), type, reinterpret_cast<const void*>(off));
    else gl::glDrawElementsInstanced(prim, GLsizei(indexCount), type, reinterpret_cast<const void*>(off), GLsizei(instanceCount));
}

void GlDevice::endPass() {
    if (!inPass_) return;
    inPass_ = false;
    // MSAA resolves.
    for (int i = 0; i < pass_.colorCount; ++i) {
        const ColorAttachment& c = pass_.colors[i];
        if ((c.store == StoreOp::Resolve || c.store == StoreOp::ResolveAndStore) && c.resolve) {
            const GlTexture* src = textures_.get(c.texture.id);
            const GlTexture* dst = textures_.get(c.resolve.id);
            if (!src || !dst) continue;
            int mip = 0, layer = 0;
            GLuint rf = 0;
            {
                Texture single = c.texture;
                rf = framebufferFor(&single, &mip, &layer, 1, Texture{});
            }
            GLuint df = framebufferFor(&c.resolve, &mip, &layer, 1, Texture{});
            gl::glDisable(GL_SCISSOR_TEST);
            gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            gl::glBindFramebuffer(GL_READ_FRAMEBUFFER, rf);
            gl::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, df);
            gl::glBlitFramebuffer(0, 0, src->desc.width, src->desc.height, 0, 0, dst->desc.width, dst->desc.height, GL_COLOR_BUFFER_BIT,
                                  GL_NEAREST);
        }
    }
    gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::glBindVertexArray(0);
    pipeline_ = nullptr, pipelineId_ = 0;
}

void GlDevice::present(Texture src, const Rect& r, Filter filter) {
    if (inPass_) endPass();
    int ww = 0, wh = 0;
    drawableSize(ww, wh);
    if (textures_.get(src.id) && ww > 0 && wh > 0) {
        gl::glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer(TextureRegion{src}));
        gl::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl::glDisable(GL_SCISSOR_TEST);
        gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        // The texture's row 0 is the top: flipped into the window (GL's row 0 is the bottom).
        gl::glBlitFramebuffer(r.x, r.y, r.x + r.w, r.y + r.h, 0, wh, ww, 0, GL_COLOR_BUFFER_BIT, filter == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
        gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        pipeline_ = nullptr, pipelineId_ = 0;
    }
    if (info_.gl.swapWindow) info_.gl.swapWindow(info_.window);
    else SDL_GL_SwapWindow(info_.window);
}

void GlDevice::setPresentMode(PresentMode mode) {
    info_.presentMode = mode;
    if (mode == PresentMode::Vsync) {
        if (!SDL_GL_SetSwapInterval(1)) SDL_GL_SetSwapInterval(0);
    } else {
        SDL_GL_SetSwapInterval(0);
    }
}

// At most two frames in flight (gd01 lesson L7: deeper queues stall uploads).
void GlDevice::endFrame() {
    if (inPass_) endPass();
    if (fences_[1]) {
        gl::glClientWaitSync(fences_[1], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        gl::glDeleteSync(fences_[1]);
    }
    fences_[1] = fences_[0];
    fences_[0] = gl::glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

}  // namespace

std::unique_ptr<Device> createGlDevice(const DeviceCreateInfo& info, std::string& error) {
    auto d = std::make_unique<GlDevice>(info);
    if (!d->init(error)) return nullptr;
    return d;
}

}  // namespace gd::core::render
