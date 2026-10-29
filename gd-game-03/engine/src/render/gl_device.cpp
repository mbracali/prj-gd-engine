// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The OpenGL 3.3 core / OpenGL ES 3.0 back end of render/device.h.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "render/device.h"
#include "render/gl_api.h"
#include "render/texture_codec.h"

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RG_RGTC2
#define GL_COMPRESSED_RG_RGTC2 0x8DBD
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif

namespace gfx {

SDL_WindowFlags prepareWindow() {
#ifdef __ANDROID__
    const bool gles = true;
#else
    const bool gles = SDL_getenv("GD03_GLES") != nullptr;  // development aid: the mobile (OpenGL ES 3) path
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gles ? 0 : 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, gles ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, gles ? 0 : SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // macOS needs it
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    return SDL_WINDOW_OPENGL;
}

namespace {

// A table of objects addressed by 1-based handles, with reuse of freed slots.
template <typename T>
class Slots {
public:
    uint32_t add(const T& v) {
        if (!free_.empty()) {
            uint32_t i = free_.back();
            free_.pop_back();
            items_[i] = v, used_[i] = true;
            return i + 1;
        }
        items_.push_back(v), used_.push_back(true);
        return uint32_t(items_.size());
    }
    T* get(uint32_t id) { return id && id <= items_.size() && used_[id - 1] ? &items_[id - 1] : nullptr; }
    const T* get(uint32_t id) const { return id && id <= items_.size() && used_[id - 1] ? &items_[id - 1] : nullptr; }
    void remove(uint32_t id) {
        if (!get(id)) return;
        used_[id - 1] = false;
        free_.push_back(id - 1);
    }
    template <typename F>
    void forEach(F f) {
        for (size_t i = 0; i < items_.size(); ++i)
            if (used_[i]) f(items_[i]);
    }

private:
    std::vector<T> items_;
    std::vector<bool> used_;
    std::vector<uint32_t> free_;
};

struct GlTexture {
    GLuint name = 0;
    GLenum target = GL_TEXTURE_2D;
    int w = 0, h = 0;
    TexFormat format = TexFormat::RGBA8;
};
struct GlBuffer {
    GLuint name = 0;
    GLenum target = GL_ARRAY_BUFFER;
    size_t size = 0;
    bool dynamic = false;
};
struct GlTarget {
    GLuint fbo = 0;
    TextureId color, depth;
    int w = 0, h = 0;
};

GLenum blendFactor(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero: return GL_ZERO;
    case BlendFactor::One: return GL_ONE;
    case BlendFactor::SrcColor: return GL_SRC_COLOR;
    case BlendFactor::InvSrcColor: return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstColor: return GL_DST_COLOR;
    case BlendFactor::InvDstColor: return GL_ONE_MINUS_DST_COLOR;
    case BlendFactor::DstAlpha: return GL_DST_ALPHA;
    case BlendFactor::InvDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    }
    return GL_ONE;
}

GLenum compareFunc(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never: return GL_NEVER;
    case CompareFunc::Less: return GL_LESS;
    case CompareFunc::Equal: return GL_EQUAL;
    case CompareFunc::LessEqual: return GL_LEQUAL;
    case CompareFunc::Greater: return GL_GREATER;
    case CompareFunc::NotEqual: return GL_NOTEQUAL;
    case CompareFunc::GreaterEqual: return GL_GEQUAL;
    case CompareFunc::Always: return GL_ALWAYS;
    }
    return GL_LEQUAL;
}

class GlDevice final : public RenderDevice {
public:
    explicit GlDevice(SDL_Window* window) : window_(window) {}
    ~GlDevice() override;
    bool init(std::string& error);

    const DeviceInfo& info() const override { return info_; }
    TextureId createTexture(const TextureDesc& desc, const TexLevel* levels, std::string& error) override;
    void destroyTexture(TextureId t) override;
    void updateTexture(TextureId t, const void* data, size_t pitch) override;
    BufferId createBuffer(BufferKind kind, size_t size, const void* data, bool dynamic) override;
    void updateBuffer(BufferId b, size_t offset, const void* data, size_t size) override;
    DevicePerf takePerf() override {
        DevicePerf p = perf_;
        perf_ = {};
        return p;
    }
    void destroyBuffer(BufferId b) override;
    ProgramId createProgram(const ProgramDesc& desc, std::string& error) override;
    void destroyProgram(ProgramId p) override;
    TargetId createTarget(int width, int height, bool depth, std::string& error) override {
        return createTarget(TargetDesc{width, height, depth, TargetFormat::RGBA8}, error);
    }
    TargetId createTarget(const TargetDesc& desc, std::string& error) override;
    TextureId targetTexture(TargetId t) const override;
    TextureId targetDepth(TargetId t) const override;
    void destroyTarget(TargetId t) override;
    void beginFrame() override;
    void setTarget(TargetId t) override;
    void setViewport(int x, int y, int w, int h) override;
    TargetId currentTarget() const override { return target_; }
    void currentViewport(int& x, int& y, int& w, int& h) const override { x = vp_[0], y = vp_[1], w = vp_[2], h = vp_[3]; }
    void clear(const float* rgba, bool depth) override;
    void copyToTarget(int x, int y, int w, int h, TargetId dst) override;
    void draw(const DrawCall& call) override;
    UniformData stageUniforms(const void* data, size_t size) override;
    void drawableSize(int& w, int& h) const override;
    SDL_Surface* readPixels(int x, int y, int w, int h) override;
    void endFrame() override;
    void setFrameCap(int fps) override;
    void setInternalHeight(int height) override { internalH_ = std::max(0, height); }

private:
    void applyState(const PipelineState& s, bool force = false);
    GLuint sampler(const SamplerState& s);
    void bindUniforms(int binding, const UniformData& u);
    size_t writeUniforms(const void* data, size_t size);  // returns the buffer offset

    SDL_Window* window_;
    SDL_GLContext context_ = nullptr;
    DeviceInfo info_;
    Slots<GlTexture> textures_;
    Slots<GlBuffer> buffers_;
    Slots<GLuint> programs_;
    Slots<GlTarget> targets_;
    TextureId white_;
    GLuint vao_ = 0;

    // Uniform blocks: one buffer, a region per frame in flight (three, as
    // the fence ring allows two frames on the GPU besides the one written).
    static constexpr size_t kUniformRegion = 4u << 20;
    static constexpr int kUniformRegions = 3;
    GLuint ubo_ = 0;
    size_t uboAlign_ = 256, uboOffset_ = 0;
    int uboRegion_ = 0;
    bool uboOverflowLogged_ = false;
    // Streaming writes (uniforms, appended vertices) through an unsynchronized
    // glMapBufferRange instead of glBufferSubData: Apple's OpenGL (on Metal)
    // stalls or copies on glBufferSubData into a buffer the GPU may still
    // read; the regions and fences already keep those bytes unused.
    // Default on macOS; GD03_GL_STREAM=map | subdata chooses.
    bool streamMap_ = false;
    void streamWrite(GLenum target, size_t offset, const void* data, size_t size);
    DevicePerf perf_;

    // Cached GL state.
    PipelineState state_;
    GLuint program_ = 0;
    uint32_t enabledAttribs_ = 0;
    GLuint boundTex_[kMaxTextureSlots] = {};
    GLuint boundSampler_[kMaxTextureSlots] = {};
    std::vector<std::pair<SamplerState, GLuint>> samplers_;
    int targetH_ = 0;
    TargetId target_;
    // The internal resolution's frame (setInternalHeight): made at the
    // frame's start when its size changes.
    int internalH_ = 0;
    TargetId frame_;
    bool frameOn_ = false;
    int vp_[4] = {};

    // Frame pacing (gd01 lesson L7).
    GLsync fences_[2] = {};
    uint64_t paceNs_ = 0, nextFrameNs_ = 0;
};

GlDevice::~GlDevice() {
    if (!context_) return;
    for (GLsync& f : fences_)
        if (f) gl::glDeleteSync(f), f = nullptr;
    textures_.forEach([](GlTexture& t) { gl::glDeleteTextures(1, &t.name); });
    buffers_.forEach([](GlBuffer& b) { gl::glDeleteBuffers(1, &b.name); });
    programs_.forEach([](GLuint& p) { gl::glDeleteProgram(p); });
    targets_.forEach([](GlTarget& t) { gl::glDeleteFramebuffers(1, &t.fbo); });
    for (auto& s : samplers_) gl::glDeleteSamplers(1, &s.second);
    if (ubo_) gl::glDeleteBuffers(1, &ubo_);
    if (vao_) gl::glDeleteVertexArrays(1, &vao_);
    SDL_GL_DestroyContext(context_);
}

bool GlDevice::init(std::string& error) {
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        error = SDL_GetError();
        return false;
    }
    SDL_GL_MakeCurrent(window_, context_);
    if (const char* missing = gl::load()) {
        error = std::string("OpenGL function missing: ") + missing;
        return false;
    }
    auto str = [](GLenum e) {
        const GLubyte* s = gl::glGetString(e);
        return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
    };
    info_.version = str(GL_VERSION);
    info_.gpu = str(GL_RENDERER);
    info_.es = info_.version.rfind("OpenGL ES", 0) == 0;
    info_.api = info_.es ? "OpenGL ES" : "OpenGL";
    const bool forceDecode = SDL_getenv("GD03_DECODE_TEXTURES") != nullptr;  // development aid: the CPU decode path
    info_.s3tc = !forceDecode && (gl::hasExtension("GL_EXT_texture_compression_s3tc") ||
                                  (gl::hasExtension("GL_EXT_texture_compression_dxt1") && gl::hasExtension("GL_ANGLE_texture_compression_dxt5")));
    // RGTC is core in desktop OpenGL 3.0; on ES it is an extension.
    info_.rgtc = !forceDecode && (!info_.es || gl::hasExtension("GL_EXT_texture_compression_rgtc"));
    SDL_Log("render: %s on %s (BC1/BC3 %s, BC5 %s)", info_.version.c_str(), info_.gpu.c_str(),
            info_.s3tc ? "native" : "CPU-decoded", info_.rgtc ? "native" : "CPU-decoded");

    gl::glGenVertexArrays(1, &vao_);
    gl::glBindVertexArray(vao_);
    gl::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::glPixelStorei(GL_PACK_ALIGNMENT, 1);

#ifdef __APPLE__
    streamMap_ = true;
#endif
    if (const char* m = SDL_getenv("GD03_GL_STREAM")) streamMap_ = std::strcmp(m, "map") == 0;
    GLint align = 256;
    gl::glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
    uboAlign_ = size_t(std::max(align, 16));
    gl::glGenBuffers(1, &ubo_);
    gl::glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    gl::glBufferData(GL_UNIFORM_BUFFER, GLsizeiptr(kUniformRegion * kUniformRegions), nullptr, GL_DYNAMIC_DRAW);

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    TexLevel level{whitePixel, 4};
    white_ = createTexture({1, 1, 1, TexFormat::RGBA8, TexKind::Tex2D}, &level, error);
    if (!white_) return false;

    applyState(state_, true);
    gl::glEnable(GL_SCISSOR_TEST);
    setFrameCap(60);
    return true;
}

TextureId GlDevice::createTexture(const TextureDesc& desc, const TexLevel* levels, std::string& error) {
    ++perf_.created;
    if (desc.width <= 0 || desc.height <= 0 || desc.mips < 1) {
        error = "texture: bad size";
        return {};
    }
    const bool native = desc.format == TexFormat::RGBA8 || desc.format == TexFormat::R8 || (desc.format == TexFormat::BC5 ? info_.rgtc : info_.s3tc);
    GLenum internal = GL_RGBA8;
    if (native && desc.format == TexFormat::BC1) internal = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    if (native && desc.format == TexFormat::BC3) internal = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    if (native && desc.format == TexFormat::BC5) internal = GL_COMPRESSED_RG_RGTC2;
    if (desc.format == TexFormat::R8) internal = GL_R8;

    GlTexture t;
    t.w = desc.width, t.h = desc.height, t.format = desc.format;
    t.target = desc.kind == TexKind::Cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    gl::glGenTextures(1, &t.name);
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glBindTexture(t.target, t.name);
    boundTex_[0] = 0;  // slot 0's cache no longer holds
    const int faces = desc.kind == TexKind::Cube ? 6 : 1;
    std::vector<uint8_t> rgba;
    for (int face = 0; face < faces; ++face) {
        const GLenum target = desc.kind == TexKind::Cube ? GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face) : GL_TEXTURE_2D;
        for (int level = 0; level < desc.mips; ++level) {
            const int w = std::max(1, desc.width >> level), h = std::max(1, desc.height >> level);
            const TexLevel& l = levels[face * desc.mips + level];
            if (l.size < levelSize(desc.format, w, h) || !l.data) {
                error = "texture: level " + std::to_string(level) + " is too small";
                gl::glDeleteTextures(1, &t.name);
                return {};
            }
            if (internal == GL_RGBA8 && desc.format != TexFormat::RGBA8) {
                decodeToRgba8(desc.format, static_cast<const uint8_t*>(l.data), l.size, w, h, rgba);
                gl::glTexImage2D(target, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            } else if (internal == GL_RGBA8) {
                gl::glTexImage2D(target, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, l.data);
            } else if (internal == GL_R8) {
                gl::glTexImage2D(target, level, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, l.data);
            } else {
                gl::glCompressedTexImage2D(target, level, internal, w, h, 0, GLsizei(levelSize(desc.format, w, h)), l.data);
            }
        }
    }
    gl::glTexParameteri(t.target, GL_TEXTURE_MAX_LEVEL, desc.mips - 1);
    if (GLenum e = gl::glGetError(); e != GL_NO_ERROR) {
        error = "texture: OpenGL error " + std::to_string(e);
        gl::glDeleteTextures(1, &t.name);
        return {};
    }
    return {textures_.add(t)};
}

void GlDevice::destroyTexture(TextureId t) {
    if (GlTexture* p = textures_.get(t.id)) {
        for (GLuint& b : boundTex_)
            if (b == p->name) b = 0;
        gl::glDeleteTextures(1, &p->name);
        textures_.remove(t.id);
    }
}

void GlDevice::updateTexture(TextureId id, const void* data, size_t pitch) {
    GlTexture* t = textures_.get(id.id);
    if (!t || t->target != GL_TEXTURE_2D || (t->format != TexFormat::RGBA8 && t->format != TexFormat::R8)) return;
    const int bpp = t->format == TexFormat::R8 ? 1 : 4;
    gl::glActiveTexture(GL_TEXTURE0);
    gl::glBindTexture(GL_TEXTURE_2D, t->name);
    boundTex_[0] = 0;
    gl::glPixelStorei(GL_UNPACK_ROW_LENGTH, GLint(pitch / size_t(bpp)));
    gl::glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->w, t->h, bpp == 1 ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, data);
    perf_.textureBytes += uint64_t(t->w) * uint64_t(t->h) * uint64_t(bpp);
    gl::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

BufferId GlDevice::createBuffer(BufferKind kind, size_t size, const void* data, bool dynamic) {
    ++perf_.created;
    GlBuffer b;
    b.target = kind == BufferKind::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    b.size = size;
    b.dynamic = dynamic;
    gl::glGenBuffers(1, &b.name);
    gl::glBindBuffer(b.target, b.name);
    gl::glBufferData(b.target, GLsizeiptr(size), data, dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
    return {buffers_.add(b)};
}

void GlDevice::streamWrite(GLenum target, size_t offset, const void* data, size_t size) {
    if (streamMap_)
        if (void* p = gl::glMapBufferRange(target, GLintptr(offset), GLsizeiptr(size),
                                           GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT)) {
            std::memcpy(p, data, size);
            if (gl::glUnmapBuffer(target)) return;
        }
    gl::glBufferSubData(target, GLintptr(offset), GLsizeiptr(size), data);
}

void GlDevice::updateBuffer(BufferId id, size_t offset, const void* data, size_t size) {
    GlBuffer* b = buffers_.get(id.id);
    if (!b || size == 0) return;
    perf_.bufferBytes += size;
    gl::glBindBuffer(b->target, b->name);
    if (offset + size > b->size) {  // grow (contents not kept beyond this write)
        b->size = std::max(offset + size, b->size * 2);
        gl::glBufferData(b->target, GLsizeiptr(b->size), nullptr, b->dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
    } else if (b->dynamic && offset == 0) {  // rename: the GPU may still read the old contents
        gl::glBufferData(b->target, GLsizeiptr(b->size), nullptr, GL_STREAM_DRAW);
    }
    // A dynamic buffer's bytes past the last write were not used since its
    // rename (Canvas appends): unsynchronized is safe there.
    if (b->dynamic) streamWrite(b->target, offset, data, size);
    else gl::glBufferSubData(b->target, GLintptr(offset), GLsizeiptr(size), data);
}

void GlDevice::destroyBuffer(BufferId id) {
    if (GlBuffer* b = buffers_.get(id.id)) {
        gl::glDeleteBuffers(1, &b->name);
        buffers_.remove(id.id);
    }
}

GLuint compileStage(GLenum stage, const std::string& source, std::string& error) {
    GLuint s = gl::glCreateShader(stage);
    const char* text = source.c_str();
    gl::glShaderSource(s, 1, &text, nullptr);
    gl::glCompileShader(s);
    GLint ok = 0;
    gl::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl::glGetShaderInfoLog(s, sizeof log, nullptr, log);
        error = std::string(stage == GL_VERTEX_SHADER ? "vertex shader: " : "fragment shader: ") + log;
        gl::glDeleteShader(s);
        return 0;
    }
    return s;
}

ProgramId GlDevice::createProgram(const ProgramDesc& desc, std::string& error) {
    ++perf_.created;
    const std::string header = info_.es ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
                                          "precision highp sampler2D;\nprecision highp samplerCube;\n"
                                        : "#version 330 core\n";
    const std::string common = header + desc.defines + "#line 1\n";
    GLuint vs = compileStage(GL_VERTEX_SHADER, common + "#define GD_VERTEX 1\n" + desc.vertex, error);
    if (!vs) return {};
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, common + "#define GD_FRAGMENT 1\n" + desc.fragment, error);
    if (!fs) {
        gl::glDeleteShader(vs);
        return {};
    }
    GLuint p = gl::glCreateProgram();
    gl::glAttachShader(p, vs);
    gl::glAttachShader(p, fs);
    gl::glLinkProgram(p);
    gl::glDeleteShader(vs);
    gl::glDeleteShader(fs);
    GLint ok = 0;
    gl::glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl::glGetProgramInfoLog(p, sizeof log, nullptr, log);
        error = std::string("link: ") + log;
        gl::glDeleteProgram(p);
        return {};
    }
    for (size_t i = 0; i < desc.uniformBlocks.size() && i < size_t(kMaxUniformBlocks); ++i) {
        if (desc.uniformBlocks[i].empty()) continue;
        GLuint index = gl::glGetUniformBlockIndex(p, desc.uniformBlocks[i].c_str());
        if (index != GL_INVALID_INDEX) gl::glUniformBlockBinding(p, index, GLuint(i));
    }
    gl::glUseProgram(p);
    for (size_t i = 0; i < desc.samplers.size() && i < size_t(kMaxTextureSlots); ++i) {
        if (desc.samplers[i].empty()) continue;
        GLint loc = gl::glGetUniformLocation(p, desc.samplers[i].c_str());
        if (loc >= 0) gl::glUniform1i(loc, GLint(i));
    }
    gl::glUseProgram(program_);
    return {programs_.add(p)};
}

void GlDevice::destroyProgram(ProgramId id) {
    if (GLuint* p = programs_.get(id.id)) {
        if (program_ == *p) gl::glUseProgram(0), program_ = 0;
        gl::glDeleteProgram(*p);
        programs_.remove(id.id);
    }
}

TargetId GlDevice::createTarget(const TargetDesc& desc, std::string& error) {
    ++perf_.created;
    const int width = desc.width, height = desc.height;
    const bool depth = desc.depth;
    if (width <= 0 || height <= 0) {
        error = "render target: bad size";
        return {};
    }
    GlTarget t;
    t.w = width, t.h = height;
    auto makeTex = [&](GLenum internal, GLenum format, GLenum type) {
        GlTexture tex;
        gl::glGenTextures(1, &tex.name);
        gl::glActiveTexture(GL_TEXTURE0);
        gl::glBindTexture(GL_TEXTURE_2D, tex.name);
        boundTex_[0] = 0;
        gl::glTexImage2D(GL_TEXTURE_2D, 0, GLint(internal), width, height, 0, format, type, nullptr);
        gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        return TextureId{textures_.add(tex)};
    };
    if (desc.format == TargetFormat::RGB10A2) t.color = makeTex(GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV);
    else if (desc.format == TargetFormat::RGBA16F) t.color = makeTex(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    else t.color = makeTex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    if (desc.opaque) gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    while (gl::glGetError() != GL_NO_ERROR) {}  // an unsupported format shows as an incomplete target below
    if (depth) t.depth = makeTex(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
    gl::glGenFramebuffers(1, &t.fbo);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures_.get(t.color.id)->name, 0);
    if (depth)
        gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, textures_.get(t.depth.id)->name, 0);
    const bool complete = gl::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
    targetH_ = 0;
    target_ = {};
    if (!complete) {
        error = "render target " + std::to_string(width) + "x" + std::to_string(height) + " is incomplete";
        gl::glDeleteFramebuffers(1, &t.fbo);
        destroyTexture(t.color);
        destroyTexture(t.depth);
        return {};
    }
    return {targets_.add(t)};
}

TextureId GlDevice::targetTexture(TargetId t) const {
    const GlTarget* p = targets_.get(t.id);
    return p ? p->color : TextureId{};
}

TextureId GlDevice::targetDepth(TargetId t) const {
    const GlTarget* p = targets_.get(t.id);
    return p ? p->depth : TextureId{};
}

void GlDevice::destroyTarget(TargetId id) {
    if (GlTarget* t = targets_.get(id.id)) {
        gl::glDeleteFramebuffers(1, &t->fbo);
        destroyTexture(t->color);
        destroyTexture(t->depth);
        targets_.remove(id.id);
    }
}

void GlDevice::drawableSize(int& w, int& h) const {
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (frameOn_ && h > 0) {
        w = std::max(1, int(std::lround(double(w) * internalH_ / h)));
        h = internalH_;
    }
}

void GlDevice::beginFrame() {
    uboRegion_ = (uboRegion_ + 1) % kUniformRegions;
    uboOffset_ = 0;
    // The internal resolution's frame, at the window's aspect.
    frameOn_ = false;
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(window_, &ww, &wh);
    if (internalH_ > 0 && wh > 0 && internalH_ != wh) {
        const int w = std::max(1, int(std::lround(double(ww) * internalH_ / wh))), h = internalH_;
        const GlTarget* f = targets_.get(frame_.id);
        if (!f || f->w != w || f->h != h) {
            if (f) destroyTarget(frame_);
            std::string error;
            frame_ = createTarget(TargetDesc{w, h, true, TargetFormat::RGBA8}, error);
            if (!frame_) SDL_Log("render: internal resolution %dx%d: %s; drawing to the window", w, h, error.c_str()), internalH_ = 0;
        }
        frameOn_ = bool(frame_);
    }
    setTarget({});
}

void GlDevice::setTarget(TargetId id) {
    const GlTarget* t = targets_.get(id.id);
    const GlTarget* frame = frameOn_ ? targets_.get(frame_.id) : nullptr;
    gl::glBindFramebuffer(GL_FRAMEBUFFER, t ? t->fbo : frame ? frame->fbo : 0);
    target_ = t ? id : TargetId{};
    int w = 0, h = 0;
    if (t) w = t->w, h = t->h;
    else drawableSize(w, h);
    targetH_ = h;
    setViewport(0, 0, w, h);
}

void GlDevice::setViewport(int x, int y, int w, int h) {
    vp_[0] = x, vp_[1] = y, vp_[2] = w, vp_[3] = h;
    const int glY = targetH_ - y - h;  // GL's origin is bottom-left
    gl::glViewport(x, glY, w, h);
    gl::glScissor(x, glY, w, h);
}

void GlDevice::clear(const float* rgba, bool depth) {
    GLbitfield bits = 0;
    if (rgba) {
        gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl::glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
        bits |= GL_COLOR_BUFFER_BIT;
    }
    if (depth) {
        gl::glDepthMask(GL_TRUE);
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    gl::glClear(bits);
    applyState(state_, true);  // restore the masks
}

// A framebuffer blit (OpenGL 3.3 and ES 3.0 alike; formats convert, no
// multisampling is involved), flipped so the copy's first row is the
// rectangle's top. Blits obey the scissor test, so it is off meanwhile.
void GlDevice::copyToTarget(int x, int y, int w, int h, TargetId dst) {
    const GlTarget* d = targets_.get(dst.id);
    if (!d || w <= 0 || h <= 0) return;
    const GlTarget* t = targets_.get(target_.id);
    const GlTarget* frame = frameOn_ ? targets_.get(frame_.id) : nullptr;
    const GLuint src = t ? t->fbo : frame ? frame->fbo : 0;
    int sw = 0, sh = targetH_;
    if (t) sw = t->w;
    else drawableSize(sw, sh);
    // The part inside both the source and the destination.
    const int x0 = std::max({x, 0}), y0 = std::max({y, 0});
    const int x1 = std::min({x + w, sw, x + d->w}), y1 = std::min({y + h, sh, y + d->h});
    if (x1 <= x0 || y1 <= y0) return;
    gl::glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    gl::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, d->fbo);
    gl::glDisable(GL_SCISSOR_TEST);
    gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const int dx0 = x0 - x, dy0 = y0 - y, dy1 = y1 - y;
    gl::glBlitFramebuffer(x0, sh - y1, x1, sh - y0, dx0, dy1, dx0 + (x1 - x0), dy0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, src);
    gl::glEnable(GL_SCISSOR_TEST);
    applyState(state_, true);  // restore the masks
}

void GlDevice::applyState(const PipelineState& s, bool force) {
    const PipelineState& o = state_;
    if (force || s.blend != o.blend) (s.blend ? gl::glEnable : gl::glDisable)(GL_BLEND);
    if (force || s.src != o.src || s.dst != o.dst)
        gl::glBlendFuncSeparate(blendFactor(s.src), blendFactor(s.dst), GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (force || s.op != o.op)
        gl::glBlendEquation(s.op == BlendOp::Subtract ? GL_FUNC_SUBTRACT : s.op == BlendOp::ReverseSubtract ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
    if (force || s.depthTest != o.depthTest) (s.depthTest ? gl::glEnable : gl::glDisable)(GL_DEPTH_TEST);
    if (force || s.depthWrite != o.depthWrite) gl::glDepthMask(s.depthWrite ? GL_TRUE : GL_FALSE);
    if (force || s.depthFunc != o.depthFunc) gl::glDepthFunc(compareFunc(s.depthFunc));
    if (force || s.cull != o.cull) {
        if (s.cull == Cull::None) {
            gl::glDisable(GL_CULL_FACE);
        } else {
            // The cull winding is stated on a y-down screen; GL's window is
            // y-up, which mirrors it: culling clockwise there culls GL's
            // counter-clockwise triangles, the "back" ones when front is CW.
            gl::glEnable(GL_CULL_FACE);
            gl::glCullFace(GL_BACK);
            gl::glFrontFace(s.cull == Cull::Clockwise ? GL_CW : GL_CCW);
        }
    }
    if (force || s.colorWrite != o.colorWrite || s.alphaWrite != o.alphaWrite) {
        const GLboolean c = s.colorWrite ? GL_TRUE : GL_FALSE;
        gl::glColorMask(c, c, c, s.alphaWrite ? GL_TRUE : GL_FALSE);
    }
    state_ = s;
}

GLuint GlDevice::sampler(const SamplerState& s) {
    for (auto& [state, name] : samplers_)
        if (state == s) return name;
    GLuint name = 0;
    gl::glGenSamplers(1, &name);
    const GLint minF = s.filter == Filter::Nearest ? GL_NEAREST : s.filter == Filter::Linear ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
    gl::glSamplerParameteri(name, GL_TEXTURE_MIN_FILTER, minF);
    gl::glSamplerParameteri(name, GL_TEXTURE_MAG_FILTER, s.filter == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
    GLint wrap = GL_REPEAT;
    // OpenGL ES 3.0 has no border colour: those clamp to the edge there.
    const bool border = (s.wrap == Wrap::BorderBlack || s.wrap == Wrap::BorderWhite) && !info_.es;
    if (s.wrap == Wrap::Clamp || ((s.wrap == Wrap::BorderBlack || s.wrap == Wrap::BorderWhite) && !border)) wrap = GL_CLAMP_TO_EDGE;
    if (s.wrap == Wrap::Mirror) wrap = GL_MIRRORED_REPEAT;
    if (border) {
        wrap = GL_CLAMP_TO_BORDER;
        const float c = s.wrap == Wrap::BorderWhite ? 1.0f : 0.0f;
        const float color[4] = {c, c, c, c};
        gl::glSamplerParameterfv(name, GL_TEXTURE_BORDER_COLOR, color);
    }
    gl::glSamplerParameteri(name, GL_TEXTURE_WRAP_S, wrap);
    gl::glSamplerParameteri(name, GL_TEXTURE_WRAP_T, wrap);
    gl::glSamplerParameteri(name, GL_TEXTURE_WRAP_R, wrap);
    if (!info_.es) gl::glSamplerParameterf(name, GL_TEXTURE_LOD_BIAS, s.lodBias);
    samplers_.emplace_back(s, name);
    return name;
}

size_t GlDevice::writeUniforms(const void* data, size_t size) {
    size_t at = (uboOffset_ + uboAlign_ - 1) / uboAlign_ * uboAlign_;
    if (at + size > kUniformRegion) {
        // More uniform data in one frame than a region holds: rename the
        // whole buffer and start over (slower, but nothing is overwritten;
        // blocks staged earlier this frame stay valid in the old storage
        // only for draws already issued, so stage before drawing).
        if (!uboOverflowLogged_) SDL_Log("render: more than %d KB of uniforms in a frame", int(kUniformRegion >> 10));
        uboOverflowLogged_ = true;
        gl::glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
        gl::glBufferData(GL_UNIFORM_BUFFER, GLsizeiptr(kUniformRegion * kUniformRegions), nullptr, GL_DYNAMIC_DRAW);
        at = 0;
    }
    const size_t base = size_t(uboRegion_) * kUniformRegion + at;
    gl::glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    streamWrite(GL_UNIFORM_BUFFER, base, data, size);
    perf_.uniformBytes += size;
    uboOffset_ = at + size;
    return base;
}

UniformData GlDevice::stageUniforms(const void* data, size_t size) {
    UniformData u{data, size, 0};
    if (data && size) u.staged = writeUniforms(data, size) + 1;
    return u;
}

void GlDevice::bindUniforms(int binding, const UniformData& u) {
    if (!u.size || (!u.data && !u.staged)) return;
    const size_t base = u.staged ? u.staged - 1 : writeUniforms(u.data, u.size);
    gl::glBindBufferRange(GL_UNIFORM_BUFFER, GLuint(binding), ubo_, GLintptr(base), GLsizeiptr(u.size));
}

void GlDevice::draw(const DrawCall& c) {
    const GLuint* prog = programs_.get(c.program.id);
    const GlBuffer* vb = buffers_.get(c.vertices.id);
    if (!prog || !vb || !c.layout || c.count <= 0) return;
    if (*prog != program_) gl::glUseProgram(program_ = *prog);
    applyState(c.state);

    for (int i = 0; i < kMaxTextureSlots; ++i) {
        const GlTexture* t = textures_.get(c.textures[i].id);
        const GlTexture* use = t ? t : textures_.get(white_.id);
        GLuint s = c.textures[i] ? sampler(c.samplers[i]) : 0;
        if (boundTex_[i] == use->name && boundSampler_[i] == s) continue;
        if (!t && boundTex_[i] == 0 && i > 0) continue;  // nothing there, nothing needed
        gl::glActiveTexture(GLenum(GL_TEXTURE0 + i));
        gl::glBindTexture(use->target, use->name);
        gl::glBindSampler(GLuint(i), s);
        boundTex_[i] = use->name, boundSampler_[i] = s;
    }
    for (int i = 0; i < kMaxUniformBlocks; ++i) bindUniforms(i, c.uniforms[i]);

    gl::glBindBuffer(GL_ARRAY_BUFFER, vb->name);
    uint32_t enabled = 0;
    for (const VertexAttrib& a : c.layout->attribs) {
        if (!a.components) continue;
        enabled |= 1u << a.location;
        const auto offset = reinterpret_cast<const void*>(uintptr_t(c.vertexOffset + a.offset));
        if (a.type == AttribType::Float)
            gl::glVertexAttribPointer(a.location, a.components, GL_FLOAT, GL_FALSE, c.layout->stride, offset);
        else
            gl::glVertexAttribPointer(a.location, a.components, GL_UNSIGNED_BYTE, GL_TRUE, c.layout->stride, offset);
    }
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t bit = 1u << i;
        if ((enabled & bit) && !(enabledAttribs_ & bit)) gl::glEnableVertexAttribArray(i);
        if (!(enabled & bit) && (enabledAttribs_ & bit)) gl::glDisableVertexAttribArray(i);
    }
    enabledAttribs_ = enabled;

    ++perf_.draws;
    const GLenum mode = c.primitive == Primitive::Lines ? GL_LINES : GL_TRIANGLES;
    if (const GlBuffer* ib = buffers_.get(c.indices.id)) {
        gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->name);
        const size_t indexSize = c.indices32 ? 4 : 2;
        gl::glDrawElements(mode, c.count, c.indices32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(uintptr_t(size_t(c.first) * indexSize)));
    } else {
        gl::glDrawArrays(mode, c.first, c.count);
    }
}

SDL_Surface* GlDevice::readPixels(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return nullptr;
    int dw = 0, dh = 0;
    drawableSize(dw, dh);
    std::vector<uint8_t> rows(size_t(w) * size_t(h) * 4);
    const GlTarget* frame = frameOn_ ? targets_.get(frame_.id) : nullptr;
    gl::glBindFramebuffer(GL_FRAMEBUFFER, frame ? frame->fbo : 0);
    gl::glReadPixels(x, dh - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    for (int r = 0; r < h; ++r)  // GL rows run bottom-up
        std::memcpy(static_cast<uint8_t*>(s->pixels) + size_t(r) * size_t(s->pitch), &rows[size_t(h - 1 - r) * size_t(w) * 4], size_t(w) * 4);
    return s;
}

// The frame cap. When the display's refresh rate is a whole multiple of the
// cap, vsync stays on at interval 1 (many drivers and compositors ignore
// larger intervals) and endFrame paces to the cap, so every swap lands on a
// refresh; otherwise vsync is off and endFrame paces alone. Headless
// (offscreen) runs are not paced. gd01 lesson L7.
void GlDevice::setFrameCap(int fps) {
    fps = std::max(fps, 30);
    int interval = 0;
    if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_)); m && m->refresh_rate > 0) {
        const float n = m->refresh_rate / float(fps);
        if (n >= 0.95f && std::fabs(n - std::round(n)) < 0.05f) interval = int(std::round(n));
    }
    const char* driver = SDL_GetCurrentVideoDriver();
    const bool headless = driver && std::strcmp(driver, "offscreen") == 0;
    const bool vsync = interval > 0 && SDL_GL_SetSwapInterval(1);
    if (!vsync) SDL_GL_SetSwapInterval(0);
    paceNs_ = headless || (vsync && interval == 1) ? 0 : 1000000000ull / uint64_t(fps);
    nextFrameNs_ = 0;
}

void GlDevice::endFrame() {
    const uint64_t start = SDL_GetTicksNS();
    // The internal resolution's frame, scaled to the window.
    if (const GlTarget* frame = frameOn_ ? targets_.get(frame_.id) : nullptr) {
        int ww = 0, wh = 0;
        SDL_GetWindowSizeInPixels(window_, &ww, &wh);
        gl::glBindFramebuffer(GL_READ_FRAMEBUFFER, frame->fbo);
        gl::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl::glDisable(GL_SCISSOR_TEST);
        gl::glBlitFramebuffer(0, 0, frame->w, frame->h, 0, 0, ww, wh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        gl::glEnable(GL_SCISSOR_TEST);
        gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        target_ = {};
    }
    if (paceNs_) {
        uint64_t now = SDL_GetTicksNS();
        if (nextFrameNs_ > now && nextFrameNs_ - now <= paceNs_) SDL_DelayPrecise(nextFrameNs_ - now);
        perf_.paceNs += SDL_GetTicksNS() - now;
        now = SDL_GetTicksNS();
        // Keep the cap's grid unless a slow frame fell behind it.
        nextFrameNs_ = nextFrameNs_ + paceNs_ >= now ? nextFrameNs_ + paceNs_ : now + paceNs_;
    }
    SDL_GL_SwapWindow(window_);
    const uint64_t swapped = SDL_GetTicksNS();
    perf_.presentNs += swapped - start;
    // At most two frames in flight: wait for the one before last.
    if (fences_[1]) {
        gl::glClientWaitSync(fences_[1], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        gl::glDeleteSync(fences_[1]);
    }
    perf_.fenceNs += SDL_GetTicksNS() - swapped;
    ++perf_.frames;
    fences_[1] = fences_[0];
    fences_[0] = gl::glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

}  // namespace

std::unique_ptr<RenderDevice> createGlDevice(SDL_Window* window, std::string& error) {
    auto device = std::make_unique<GlDevice>(window);
    if (!device->init(error)) return nullptr;
    return device;
}

}  // namespace gfx
