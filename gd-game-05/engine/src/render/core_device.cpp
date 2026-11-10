// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gfx::RenderDevice over gd::core::render::Device (render/core_backend.h):
// gd05's device interface on gd-engine-core's opengl3, sdl3 and vulkan
// backends (gd-engine-core/docs/render.md section 6b, "gd-game-05 on the core").
//
// How gd05's GL-shaped calls map onto the core's explicit ones:
// - Pictures are stored as local's are (GL style, row 0 at the bottom): every
//   vertex shader ends with gl_Position's y negated before the core's
//   gd_clip, so on opengl3 the core's own flip cancels it and the rasteriser
//   sees exactly local's coordinates (gd01's CoreRenderer does the same in
//   its MVPs). gd05's top-left viewports become y = height - y - h, the cull
//   winding is mirrored, the frame is presented flipped and readPixels flips
//   its rows, as local does.
// - Pipelines (gd05's DrawCall carries its whole state) are made on first
//   use and cached per program, state, vertex layout, primitive and target.
// - setTarget only notes the target; the pass begins at the first draw. A
//   clear of the whole target before any draw is the pass's load op, any
//   other clear a quad (local's glClear is scissored to the viewport).
// - Resource updates inside a pass end it (the next draw loads the target
//   again). Dynamic buffers (the canvas, particles, lights...) live in the
//   frame's transient memory instead: each updateBuffer is a new slice.
// - Uniform blocks are pushed per draw; stageUniforms keeps a copy for the
//   frame.
#include <gd/core/render/device.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "platform/paths.h"
#include "render/core_backend.h"
#include "render/device.h"
#include "render/spirv_compile.h"
#include "render/texture_codec.h"

namespace gfx {
namespace {

namespace core = gd::core::render;

template <typename T>
class Slots {
public:
    uint32_t add(T v) {
        if (!free_.empty()) {
            uint32_t i = free_.back();
            free_.pop_back();
            items_[i] = std::move(v), used_[i] = true;
            return i + 1;
        }
        items_.push_back(std::move(v)), used_.push_back(true);
        return uint32_t(items_.size());
    }
    T* get(uint32_t id) { return id && id <= items_.size() && used_[id - 1] ? &items_[id - 1] : nullptr; }
    const T* get(uint32_t id) const { return id && id <= items_.size() && used_[id - 1] ? &items_[id - 1] : nullptr; }
    void remove(uint32_t id) {
        if (!get(id)) return;
        used_[id - 1] = false;
        items_[id - 1] = T{};
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

struct CTexture {
    core::Texture tex;
    int w = 0, h = 0;
    TexFormat format = TexFormat::RGBA8;
    bool cube = false, owned = true;
};
// A gd05 buffer: static ones are core buffers; dynamic ones are a CPU copy
// whose writes become slices of the frame's transient memory.
struct CBuffer {
    BufferKind kind = BufferKind::Vertex;
    bool dynamic = false;
    core::Buffer buf;  // static
    size_t size = 0;
    std::vector<uint8_t> shadow;  // dynamic
    struct Slice {
        size_t lo = 0, hi = 0;
        core::BufferRange range;
    };
    std::vector<Slice> slices;  // this frame's
    uint64_t frame = 0;
};
struct CProgram {
    core::Shader shader;
    std::string name;
    bool samplerUsed[kMaxTextureSlots] = {};
};
struct CTarget {
    TextureId color, depth;
    core::Format format = core::Format::RGBA8;
    int w = 0, h = 0;
};

core::BlendFactor blendFactor(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero: return core::BlendFactor::Zero;
    case BlendFactor::One: return core::BlendFactor::One;
    case BlendFactor::SrcColor: return core::BlendFactor::SrcColor;
    case BlendFactor::InvSrcColor: return core::BlendFactor::OneMinusSrcColor;
    case BlendFactor::SrcAlpha: return core::BlendFactor::SrcAlpha;
    case BlendFactor::InvSrcAlpha: return core::BlendFactor::OneMinusSrcAlpha;
    case BlendFactor::DstColor: return core::BlendFactor::DstColor;
    case BlendFactor::InvDstColor: return core::BlendFactor::OneMinusDstColor;
    case BlendFactor::DstAlpha: return core::BlendFactor::DstAlpha;
    case BlendFactor::InvDstAlpha: return core::BlendFactor::OneMinusDstAlpha;
    }
    return core::BlendFactor::One;
}

core::CompareOp compareOp(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never: return core::CompareOp::Never;
    case CompareFunc::Less: return core::CompareOp::Less;
    case CompareFunc::Equal: return core::CompareOp::Equal;
    case CompareFunc::LessEqual: return core::CompareOp::LessEqual;
    case CompareFunc::Greater: return core::CompareOp::Greater;
    case CompareFunc::NotEqual: return core::CompareOp::NotEqual;
    case CompareFunc::GreaterEqual: return core::CompareOp::GreaterEqual;
    case CompareFunc::Always: return core::CompareOp::Always;
    }
    return core::CompareOp::LessEqual;
}

// The clear quad (a clear inside a pass or of part of a target): the whole
// viewport at depth 1 (window depth), in the core's dialect.
const char* kClearVertex = R"(
void main() {
    vec2 p = vec2((GD_VERTEX_ID & 1) != 0 ? 3.0 : -1.0, (GD_VERTEX_ID & 2) != 0 ? 3.0 : -1.0);
    gl_Position = gd_clip(vec4(p, 1.0, 1.0));
}
)";
const char* kClearFragment = R"(
GD_UNIFORMS(0, GdClear) {
    vec4 uColor;
};
GD_FRAG_OUT(0) vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

class CoreDevice final : public RenderDevice {
public:
    CoreDevice(Backend backend, SDL_Window* window) : backend_(backend), window_(window) {}
    ~CoreDevice() override;
    bool init(std::string& error);

    const DeviceInfo& info() const override { return info_; }
    TextureId createTexture(const TextureDesc& desc, const TexLevel* levels, std::string& error) override;
    void destroyTexture(TextureId t) override;
    void updateTexture(TextureId t, const void* data, size_t pitch) override;
    BufferId createBuffer(BufferKind kind, size_t size, const void* data, bool dynamic) override;
    void updateBuffer(BufferId b, size_t offset, const void* data, size_t size) override;
    void destroyBuffer(BufferId b) override;
    ProgramId createProgram(const ProgramDesc& desc, std::string& error) override;
    void destroyProgram(ProgramId p) override;
    TargetId createTarget(int width, int height, bool depth, std::string& error) override {
        return createTarget(TargetDesc{width, height, depth, TargetFormat::RGBA8}, error);
    }
    TargetId createTarget(const TargetDesc& desc, std::string& error) override;
    TextureId targetTexture(TargetId t) const override {
        const CTarget* p = targets_.get(t.id);
        return p ? p->color : TextureId{};
    }
    TextureId targetDepth(TargetId t) const override {
        const CTarget* p = targets_.get(t.id);
        return p ? p->depth : TextureId{};
    }
    void destroyTarget(TargetId t) override;
    void beginFrame() override;
    void setTarget(TargetId t) override;
    void setViewport(int x, int y, int w, int h) override;
    TargetId currentTarget() const override { return target_; }
    void currentViewport(int& x, int& y, int& w, int& h) const override { x = vp_[0], y = vp_[1], w = vp_[2], h = vp_[3]; }
    void clear(const float* rgba, bool depth) override;
    void draw(const DrawCall& call) override;
    UniformData stageUniforms(const void* data, size_t size) override;
    void drawableSize(int& w, int& h) const override;
    SDL_Surface* readPixels(int x, int y, int w, int h) override;
    void endFrame() override;
    void setFrameCap(int fps) override;
    void setInternalHeight(int height) override { internalH_ = std::max(0, height); }
    void setOutputGamma(float gamma) override { gamma_ = gamma > 0 ? gamma : 0.0f; }
    std::string hdrStatus() const override {
        return std::string("off: True HDR is not on the ") + backendId(backend_) + " graphics backend yet";
    }
    DevicePerf takePerf() override {
        DevicePerf p = perf_;
        perf_ = {};
        return p;
    }

private:
    const CTarget* boundTarget() const;  // the target drawn into now (the frame for none)
    void endPass();
    void ensurePass();
    void applyViewport();
    core::Pipeline pipeline(const DrawCall& c, const CTarget& t);
    core::Sampler sampler(const SamplerState& s);
    core::BufferRange dynamicRange(CBuffer& b, size_t offset);
    core::Shader makeShader(const std::string& name, const std::string& vertex, const std::string& fragment, const std::string& defines,
                            const std::vector<std::string>& blocks, const std::vector<std::string>& samplers, bool dialect, std::string& error);

    Backend backend_;
    SDL_Window* window_;
    std::unique_ptr<core::Device> dev_;
    DeviceInfo info_;
    DevicePerf perf_;
    Slots<CTexture> textures_;
    Slots<CBuffer> buffers_;
    Slots<CProgram> programs_;
    Slots<CTarget> targets_;
    TextureId white_;
    core::Sampler whiteSampler_;
    std::vector<std::pair<SamplerState, core::Sampler>> samplers_;
    std::map<std::string, core::Pipeline> pipelines_;
    core::Shader clearShader_;
    uint64_t frame_ = 1;

    // The frame: every picture is drawn into this target (the window's size,
    // or the internal resolution's) and presented scaled.
    int internalH_ = 0;
    TargetId frameTarget_;
    // setOutputGamma: the frame converted into gammaTarget_, which is presented (0: none).
    float gamma_ = 0;
    TargetId gammaTarget_;
    ProgramId gammaProgram_;
    BufferId gammaTriangle_;
    bool gammaTried_ = false;
    TargetId applyGamma();
    int frameW_ = 0, frameH_ = 0;

    // Pass state.
    TargetId target_;
    bool inPass_ = false;
    bool clearColor_ = false, clearDepth_ = false;
    float clearRgba_[4] = {0, 0, 0, 0};
    int vp_[4] = {};
    std::vector<uint8_t> staged_;  // stageUniforms' copies, this frame

    // Frame pacing (as gl_device.cpp: gd01 lesson L7).
    uint64_t paceNs_ = 0, nextFrameNs_ = 0;
};

CoreDevice::~CoreDevice() {
    if (!dev_) return;
    if (inPass_) dev_->endPass();
    for (auto& [k, p] : pipelines_) dev_->destroyPipeline(p);
    programs_.forEach([this](CProgram& p) { dev_->destroyShader(p.shader); });
    if (clearShader_) dev_->destroyShader(clearShader_);
    buffers_.forEach([this](CBuffer& b) {
        if (b.buf) dev_->destroyBuffer(b.buf);
    });
    textures_.forEach([this](CTexture& t) { dev_->destroyTexture(t.tex); });
    dev_.reset();
}

bool CoreDevice::init(std::string& error) {
    core::DeviceCreateInfo ci;
    ci.backend = backend_ == Backend::Sdl3 ? core::Backend::SdlGpu : backend_ == Backend::Vulkan ? core::Backend::Vulkan : core::Backend::OpenGL3;
    ci.window = window_;
    ci.presentMode = core::PresentMode::Vsync;
    // The pref folder (as enhanced.json's): the Vulkan backend's pipeline cache.
    const char* saveDir = SDL_getenv("GD05_SAVE_DIR");
    ci.cacheDir = saveDir && *saveDir ? std::string(saveDir) : prefDir();
#if GD05_MODULE
    // A launcher module: the context is the module's (the shim tracks it and
    // its swap), as gd01's (gd-engine-core/docs/render.md section 5).
    ci.gl.createContext = [](SDL_Window* w) -> void* { return SDL_GL_CreateContext(w); };
    ci.gl.destroyContext = [](void* c) { SDL_GL_DestroyContext(static_cast<SDL_GLContext>(c)); };
    ci.gl.swapWindow = [](SDL_Window* w) { return SDL_GL_SwapWindow(w); };
#endif
    dev_ = core::createDevice(ci, error);
    if (!dev_) return false;
    const core::Caps& caps = dev_->caps();
    info_.api = caps.api;
    info_.version = caps.version;
    info_.gpu = caps.gpu;
    info_.es = caps.es;
    const bool forceDecode = SDL_getenv("GD05_DECODE_TEXTURES") != nullptr;
    info_.s3tc = !forceDecode && caps.supports(core::Format::BC1, core::kSampled | core::kFiltered) &&
                 caps.supports(core::Format::BC3, core::kSampled | core::kFiltered);
    info_.rgtc = !forceDecode && caps.supports(core::Format::BC5, core::kSampled | core::kFiltered);
    info_.clipDepthZeroToOne = false;  // GL's -1..1: the core's gd_clip maps it
    SDL_Log("render: %s %s on %s, gd-engine-core %s backend (BC1/BC3 %s, BC5 %s)", info_.api.c_str(), info_.version.c_str(),
            info_.gpu.c_str(), backendId(backend_), info_.s3tc ? "native" : "CPU-decoded", info_.rgtc ? "native" : "CPU-decoded");

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    TexLevel level{whitePixel, 4};
    white_ = createTexture({1, 1, 1, TexFormat::RGBA8, TexKind::Tex2D}, &level, error);
    if (!white_) return false;
    whiteSampler_ = sampler(SamplerState{Filter::Linear, Wrap::Repeat, 0});
    clearShader_ = makeShader("clear", kClearVertex, kClearFragment, "", {"GdClear"}, {}, true, error);
    if (!clearShader_) return false;
    setFrameCap(60);
    return true;
}

// ---- resources

TextureId CoreDevice::createTexture(const TextureDesc& desc, const TexLevel* levels, std::string& error) {
    ++perf_.created;
    if (desc.width <= 0 || desc.height <= 0 || desc.mips < 1) {
        error = "texture: bad size";
        return {};
    }
    const bool native = desc.format == TexFormat::RGBA8 || desc.format == TexFormat::R8 || (desc.format == TexFormat::BC5 ? info_.rgtc : info_.s3tc);
    core::Format f = core::Format::RGBA8;
    if (native && desc.format == TexFormat::BC1) f = core::Format::BC1;
    if (native && desc.format == TexFormat::BC3) f = core::Format::BC3;
    if (native && desc.format == TexFormat::BC5) f = core::Format::BC5;
    if (desc.format == TexFormat::R8) f = core::Format::R8;
    core::TextureDesc td;
    td.type = desc.kind == TexKind::Cube ? core::TextureType::Cube : core::TextureType::Tex2D;
    td.format = f;
    td.width = desc.width, td.height = desc.height;
    td.mips = desc.mips;
    td.usage = core::kSampledTexture;
    if (inPass_) endPass();  // uploads happen outside passes
    core::Texture t = dev_->createTexture(td);
    if (!t) {
        error = "texture: " + dev_->lastError();
        return {};
    }
    const int faces = desc.kind == TexKind::Cube ? 6 : 1;
    std::vector<uint8_t> rgba;
    for (int face = 0; face < faces; ++face) {
        for (int level = 0; level < desc.mips; ++level) {
            const int w = std::max(1, desc.width >> level), h = std::max(1, desc.height >> level);
            const TexLevel& l = levels[face * desc.mips + level];
            if (l.size < levelSize(desc.format, w, h) || !l.data) {
                error = "texture: level " + std::to_string(level) + " is too small";
                dev_->destroyTexture(t);
                return {};
            }
            core::TextureRegion r{t, level, face, 0, 0};
            if (f == core::Format::RGBA8 && desc.format != TexFormat::RGBA8) {
                decodeToRgba8(desc.format, static_cast<const uint8_t*>(l.data), l.size, w, h, rgba);
                dev_->updateTexture(r, w, h, rgba.data());
            } else {
                dev_->updateTexture(r, w, h, l.data);
            }
            perf_.textureBytes += l.size;
        }
    }
    CTexture ct;
    ct.tex = t, ct.w = desc.width, ct.h = desc.height, ct.format = desc.format, ct.cube = desc.kind == TexKind::Cube;
    return {textures_.add(ct)};
}

void CoreDevice::destroyTexture(TextureId id) {
    if (CTexture* t = textures_.get(id.id)) {
        if (t->owned) dev_->destroyTexture(t->tex);
        textures_.remove(id.id);
    }
}

void CoreDevice::updateTexture(TextureId id, const void* data, size_t pitch) {
    CTexture* t = textures_.get(id.id);
    if (!t || t->cube || (t->format != TexFormat::RGBA8 && t->format != TexFormat::R8)) return;
    if (inPass_) endPass();
    dev_->updateTexture(core::TextureRegion{t->tex, 0, 0, 0, 0}, t->w, t->h, data, pitch);
    perf_.textureBytes += uint64_t(t->w) * uint64_t(t->h) * (t->format == TexFormat::R8 ? 1u : 4u);
}

BufferId CoreDevice::createBuffer(BufferKind kind, size_t size, const void* data, bool dynamic) {
    ++perf_.created;
    CBuffer b;
    b.kind = kind, b.dynamic = dynamic, b.size = size;
    if (dynamic) {
        b.shadow.assign(size, 0);
        if (data) std::memcpy(b.shadow.data(), data, size);
    } else {
        if (inPass_) endPass();
        core::BufferDesc d;
        d.size = std::max<size_t>(size, 4);
        d.usage = kind == BufferKind::Index ? core::kIndexBuffer : core::kVertexBuffer;
        b.buf = dev_->createBuffer(d, nullptr);
        if (b.buf && data && size) dev_->updateBuffer(b.buf, 0, data, size);
    }
    return {buffers_.add(std::move(b))};
}

void CoreDevice::updateBuffer(BufferId id, size_t offset, const void* data, size_t size) {
    CBuffer* b = buffers_.get(id.id);
    if (!b || size == 0) return;
    perf_.bufferBytes += size;
    if (!b->dynamic) {
        if (offset + size > b->size) {  // grow (contents not kept beyond this write, as local)
            if (inPass_) endPass();
            dev_->destroyBuffer(b->buf);
            b->size = std::max(offset + size, b->size * 2);
            core::BufferDesc d;
            d.size = b->size;
            d.usage = b->kind == BufferKind::Index ? core::kIndexBuffer : core::kVertexBuffer;
            b->buf = dev_->createBuffer(d, nullptr);
        }
        if (inPass_) endPass();
        dev_->updateBuffer(b->buf, offset, data, size);
        return;
    }
    if (offset + size > b->shadow.size()) b->shadow.resize(std::max(offset + size, b->shadow.size() * 2));
    std::memcpy(b->shadow.data() + offset, data, size);
    if (b->frame != frame_) b->slices.clear(), b->frame = frame_;
    // Draws already made keep their slices (GL's rename: they see the old contents).
    CBuffer::Slice s;
    s.lo = offset, s.hi = offset + size;
    s.range = dev_->allocTransient(b->kind == BufferKind::Index ? core::kIndexBuffer : core::kVertexBuffer, data, size);
    b->slices.push_back(s);
}

// The transient slice holding `offset` of a dynamic buffer: the last write
// that covers it this frame, else (contents from an earlier frame) the whole
// copy, uploaded once this frame.
core::BufferRange CoreDevice::dynamicRange(CBuffer& b, size_t offset) {
    if (b.frame != frame_) b.slices.clear(), b.frame = frame_;
    for (auto it = b.slices.rbegin(); it != b.slices.rend(); ++it)
        if (offset >= it->lo && offset < it->hi) {
            core::BufferRange r = it->range;
            r.offset += offset - it->lo;
            r.size -= offset - it->lo;
            return r;
        }
    if (b.shadow.empty()) return {};
    CBuffer::Slice s;
    s.lo = 0, s.hi = b.shadow.size();
    s.range = dev_->allocTransient(b.kind == BufferKind::Index ? core::kIndexBuffer : core::kVertexBuffer, b.shadow.data(), b.shadow.size());
    b.slices.insert(b.slices.begin(), s);
    core::BufferRange r = s.range;
    r.offset += offset, r.size -= offset;
    return r;
}

void CoreDevice::destroyBuffer(BufferId id) {
    if (CBuffer* b = buffers_.get(id.id)) {
        if (b->buf) dev_->destroyBuffer(b->buf);
        buffers_.remove(id.id);
    }
}

core::Shader CoreDevice::makeShader(const std::string& name, const std::string& vertex, const std::string& fragment, const std::string& defines,
                                    const std::vector<std::string>& blocks, const std::vector<std::string>& samplers, bool dialect,
                                    std::string& error) {
    // gd05's vertex shaders end with gl_Position as local computes it; the
    // wrapper negates its y (pictures stored as local's, see the top) and
    // gives it to the core's gd_clip (on opengl3 the two flips cancel).
    std::string vs = vertex;
    if (!dialect)
        vs = "#define main gd05_vertex_main\n" + vertex +
             "\n#undef main\nvoid main() {\n    gd05_vertex_main();\n"
             "    gl_Position = gd_clip(vec4(gl_Position.x, -gl_Position.y, gl_Position.z, gl_Position.w));\n}\n";
    core::ShaderDesc d;
    d.name = name.c_str();
    d.defines = defines.c_str();
    for (size_t i = 0; i < blocks.size() && i < size_t(core::kMaxUniformSlots); ++i) d.uniformBlocks[i] = blocks[i].c_str();
    for (size_t i = 0; i < samplers.size() && i < size_t(core::kMaxTextureSlots); ++i) d.samplers[i] = samplers[i].c_str();
    d.glslVertex = vs.c_str();
    d.glslFragment = fragment.c_str();
    std::vector<uint32_t> spvV, spvF;
    if (backend_ != Backend::OpenGL3) {
        if (!compileSpirv(name, vs, fragment, defines, blocks, samplers, dialect, spvV, spvF, error)) return {};
        d.spirvVertex = spvV.data(), d.spirvVertexBytes = spvV.size() * 4;
        d.spirvFragment = spvF.data(), d.spirvFragmentBytes = spvF.size() * 4;
    }
    core::Shader s = dev_->createShader(d);
    if (!s) error = dev_->lastError();
    return s;
}

ProgramId CoreDevice::createProgram(const ProgramDesc& desc, std::string& error) {
    ++perf_.created;
    CProgram p;
    p.name = desc.name;
    p.shader = makeShader(desc.name, desc.vertex, desc.fragment, desc.defines, desc.uniformBlocks, desc.samplers, false, error);
    if (!p.shader) return {};
    for (size_t i = 0; i < desc.samplers.size() && i < size_t(kMaxTextureSlots); ++i) p.samplerUsed[i] = !desc.samplers[i].empty();
    return {programs_.add(std::move(p))};
}

void CoreDevice::destroyProgram(ProgramId id) {
    if (CProgram* p = programs_.get(id.id)) {
        const std::string prefix = std::to_string(p->shader.id) + ":";
        for (auto it = pipelines_.begin(); it != pipelines_.end();)
            if (it->first.compare(0, prefix.size(), prefix) == 0) dev_->destroyPipeline(it->second), it = pipelines_.erase(it);
            else ++it;
        dev_->destroyShader(p->shader);
        programs_.remove(id.id);
    }
}

TargetId CoreDevice::createTarget(const TargetDesc& desc, std::string& error) {
    ++perf_.created;
    if (desc.width <= 0 || desc.height <= 0) {
        error = "render target: bad size";
        return {};
    }
    const core::Format f = desc.format == TargetFormat::RGB10A2 ? core::Format::RGB10A2
                           : desc.format == TargetFormat::RGBA16F ? core::Format::RGBA16F
                                                                  : core::Format::RGBA8;
    if (!dev_->caps().supports(f, core::kRenderable | core::kSampled)) {
        error = "render target " + std::to_string(desc.width) + "x" + std::to_string(desc.height) + ": format not renderable here";
        return {};
    }
    if (inPass_) endPass();
    auto make = [&](core::Format fmt, uint8_t usage) {
        core::TextureDesc td;
        td.format = fmt, td.width = desc.width, td.height = desc.height, td.usage = usage;
        core::Texture t = dev_->createTexture(td);
        if (!t) return TextureId{};
        CTexture ct;
        ct.tex = t, ct.w = desc.width, ct.h = desc.height;
        return TextureId{textures_.add(ct)};
    };
    CTarget t;
    t.w = desc.width, t.h = desc.height, t.format = f;
    t.color = make(f, core::kSampledTexture | core::kColorTarget);
    if (desc.depth) t.depth = make(core::Format::D24, core::kSampledTexture | core::kDepthTarget);
    if (!t.color || (desc.depth && !t.depth)) {
        error = "render target " + std::to_string(desc.width) + "x" + std::to_string(desc.height) + ": " + dev_->lastError();
        destroyTexture(t.color);
        destroyTexture(t.depth);
        return {};
    }
    return {targets_.add(t)};
}

void CoreDevice::destroyTarget(TargetId id) {
    if (CTarget* t = targets_.get(id.id)) {
        if (inPass_ && target_.id == id.id) endPass();
        destroyTexture(t->color);
        destroyTexture(t->depth);
        targets_.remove(id.id);
    }
}

core::Sampler CoreDevice::sampler(const SamplerState& s) {
    for (auto& [state, smp] : samplers_)
        if (state == s) return smp;
    core::SamplerDesc d;
    d.minFilter = d.magFilter = s.filter == Filter::Nearest ? core::Filter::Nearest : core::Filter::Linear;
    d.mipFilter = s.filter == Filter::Trilinear ? core::MipFilter::Linear : core::MipFilter::None;
    core::Wrap w = core::Wrap::Repeat;
    // No border colour (OpenGL ES 3.0, SDL_GPU): those clamp to the edge, as local on ES.
    const bool border = (s.wrap == Wrap::BorderBlack || s.wrap == Wrap::BorderWhite) && dev_->caps().clampToBorder && !info_.es;
    if (s.wrap == Wrap::Clamp || ((s.wrap == Wrap::BorderBlack || s.wrap == Wrap::BorderWhite) && !border)) w = core::Wrap::ClampToEdge;
    if (s.wrap == Wrap::Mirror) w = core::Wrap::MirroredRepeat;
    if (border) {
        w = core::Wrap::ClampToBorder;
        d.border = s.wrap == Wrap::BorderWhite ? core::BorderColor::OpaqueWhite : core::BorderColor::TransparentBlack;
    }
    d.wrapU = d.wrapV = d.wrapW = w;
    if (!info_.es) d.lodBias = s.lodBias;  // as local: no LOD bias on ES
    core::Sampler smp = dev_->createSampler(d);
    samplers_.emplace_back(s, smp);
    return smp;
}

// ---- frames and passes

void CoreDevice::drawableSize(int& w, int& h) const {
    if (frameW_ > 0 && frameH_ > 0) {
        w = frameW_, h = frameH_;
        return;
    }
    dev_->drawableSize(w, h);
}

const CTarget* CoreDevice::boundTarget() const {
    if (const CTarget* t = targets_.get(target_.id)) return t;
    return targets_.get(frameTarget_.id);
}

void CoreDevice::beginFrame() {
    ++frame_;
    staged_.clear();
    dev_->beginFrame();
    int ww = 0, wh = 0;
    dev_->drawableSize(ww, wh);
    ww = std::max(ww, 1), wh = std::max(wh, 1);
    const bool scaled = internalH_ > 0 && internalH_ != wh;
    const int w = scaled ? std::max(1, int(std::lround(double(ww) * internalH_ / wh))) : ww, h = scaled ? internalH_ : wh;
    const CTarget* f = targets_.get(frameTarget_.id);
    if (!f || f->w != w || f->h != h) {
        if (f) destroyTarget(frameTarget_);
        std::string error;
        frameTarget_ = createTarget(TargetDesc{w, h, true, TargetFormat::RGBA8}, error);
        if (!frameTarget_) SDL_Log("render: frame %dx%d: %s", w, h, error.c_str());
    }
    frameW_ = w, frameH_ = h;
    setTarget({});
}

void CoreDevice::setTarget(TargetId id) {
    if (!inPass_ && (clearColor_ || clearDepth_)) ensurePass();  // a clear made before switching: its pass, empty
    if (inPass_) endPass();
    const CTarget* t = targets_.get(id.id);
    target_ = t ? id : TargetId{};
    clearColor_ = clearDepth_ = false;
    const CTarget* b = boundTarget();
    setViewport(0, 0, b ? b->w : 0, b ? b->h : 0);
}

void CoreDevice::applyViewport() {
    const CTarget* t = boundTarget();
    if (!t) return;
    // gd05's rectangles are top-left; the pictures are stored bottom-up (GL style).
    const core::Rect r{vp_[0], t->h - vp_[1] - vp_[3], vp_[2], vp_[3]};
    dev_->setViewport(r);
    dev_->setScissor(r);
}

void CoreDevice::setViewport(int x, int y, int w, int h) {
    vp_[0] = x, vp_[1] = y, vp_[2] = w, vp_[3] = h;
    if (inPass_) applyViewport();
}

void CoreDevice::endPass() {
    if (!inPass_) return;
    dev_->endPass();
    inPass_ = false;
}

void CoreDevice::ensurePass() {
    if (inPass_) return;
    const CTarget* t = boundTarget();
    if (!t) return;
    core::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = textures_.get(t->color.id)->tex;
    p.colors[0].load = clearColor_ ? core::LoadOp::Clear : core::LoadOp::Load;
    std::memcpy(p.colors[0].clear, clearRgba_, sizeof clearRgba_);
    if (const CTexture* d = textures_.get(t->depth.id)) {
        p.depth.texture = d->tex;
        p.depth.load = clearDepth_ ? core::LoadOp::Clear : core::LoadOp::Load;
        p.depth.stencilLoad = core::LoadOp::DontCare;
        p.depth.clearDepth = 1;
    }
    dev_->beginPass(p);
    inPass_ = true;
    clearColor_ = clearDepth_ = false;
    applyViewport();
}

void CoreDevice::clear(const float* rgba, bool depth) {
    const CTarget* t = boundTarget();
    if (!t || (!rgba && !depth)) return;
    const bool whole = vp_[0] <= 0 && vp_[1] <= 0 && vp_[0] + vp_[2] >= t->w && vp_[1] + vp_[3] >= t->h;
    if (!inPass_ && whole) {  // the next pass's load op
        if (rgba) clearColor_ = true, std::memcpy(clearRgba_, rgba, sizeof clearRgba_);
        if (depth) clearDepth_ = true;
        return;
    }
    ensurePass();
    core::PipelineDesc d;
    d.shader = clearShader_;
    d.primitive = core::Primitive::Triangles;
    d.colorFormats[0] = t->format;
    d.depthFormat = t->depth ? core::Format::D24 : core::Format::Undefined;
    d.blend[0].writeMask = rgba ? core::kWriteAll : 0;
    d.depthStencil.depthTest = depth && t->depth;
    d.depthStencil.depthWrite = depth && t->depth;
    d.depthStencil.depthCompare = core::CompareOp::Always;
    const std::string key = "clear:" + std::to_string(int(t->format)) + ":" + std::to_string(t->depth ? 1 : 0) + ":" +
                            std::to_string(rgba ? 1 : 0) + std::to_string(depth ? 1 : 0);
    core::Pipeline& pl = pipelines_[key];
    if (!pl) pl = dev_->createPipeline(d);
    if (!pl) return;
    const float zero[4] = {0, 0, 0, 0};
    dev_->bindPipeline(pl);
    dev_->pushUniforms(0, rgba ? rgba : zero, 16);
    dev_->draw(3);
    ++perf_.draws;
}

UniformData CoreDevice::stageUniforms(const void* data, size_t size) {
    UniformData u{data, size, 0};
    if (data && size) {
        const size_t at = staged_.size();
        staged_.resize(at + size);
        std::memcpy(staged_.data() + at, data, size);
        u.staged = at + 1;
    }
    return u;
}

core::Pipeline CoreDevice::pipeline(const DrawCall& c, const CTarget& t) {
    const CProgram* prog = programs_.get(c.program.id);
    const PipelineState& s = c.state;
    // The key: shader, state, layout, primitive, target.
    std::string key = std::to_string(prog->shader.id) + ":";
    const uint8_t st[] = {uint8_t(s.blend), uint8_t(s.src), uint8_t(s.dst), uint8_t(s.op), uint8_t(s.depthTest), uint8_t(s.depthWrite),
                          uint8_t(s.depthFunc), uint8_t(s.cull), uint8_t(s.colorWrite), uint8_t(s.alphaWrite), uint8_t(c.primitive),
                          uint8_t(t.format), uint8_t(t.depth ? 1 : 0)};
    key.append(reinterpret_cast<const char*>(st), sizeof st);
    key.append(reinterpret_cast<const char*>(&c.layout->stride), sizeof c.layout->stride);
    for (const VertexAttrib& a : c.layout->attribs) {
        const uint8_t ab[] = {a.location, a.components, uint8_t(a.type)};
        key.append(reinterpret_cast<const char*>(ab), sizeof ab);
        key.append(reinterpret_cast<const char*>(&a.offset), sizeof a.offset);
    }
    auto it = pipelines_.find(key);
    if (it != pipelines_.end()) return it->second;

    core::PipelineDesc d;
    d.shader = prog->shader;
    d.label = prog->name.c_str();
    d.primitive = c.primitive == Primitive::Lines ? core::Primitive::Lines : core::Primitive::Triangles;
    d.layout.bufferCount = 1;
    d.layout.buffers[0].stride = c.layout->stride;
    for (const VertexAttrib& a : c.layout->attribs) {
        if (!a.components) continue;
        core::VertexAttribute& va = d.layout.attributes[d.layout.attributeCount++];
        va.location = a.location;
        va.buffer = 0;
        va.offset = a.offset;
        if (a.type == AttribType::Float) {
            va.format = a.components == 1 ? core::VertexFormat::Float1
                        : a.components == 2 ? core::VertexFormat::Float2
                        : a.components == 3 ? core::VertexFormat::Float3
                                            : core::VertexFormat::Float4;
        } else {
            va.format = core::VertexFormat::UByte4Norm;  // gd05's UNorm8 attributes are RGBA colours and weights
        }
    }
    // Cull: stated on a y-down screen (render/device.h); local makes GL's
    // front face CW for Cull::Clockwise and culls the back. The y negation
    // (see the top) mirrors the winding the core sees, so the core's front
    // face is the other one; on opengl3 the core swaps it back to local's.
    if (s.cull != Cull::None) {
        d.raster.cull = core::CullMode::Back;
        d.raster.frontFace = s.cull == Cull::Clockwise ? core::FrontFace::CounterClockwise : core::FrontFace::Clockwise;
    }
    // GL writes no depth while its test is off, and a target without depth has none.
    d.depthStencil.depthTest = s.depthTest && t.depth;
    d.depthStencil.depthWrite = s.depthTest && s.depthWrite && t.depth;
    d.depthStencil.depthCompare = compareOp(s.depthFunc);
    core::BlendState& b = d.blend[0];
    b.enable = s.blend;
    b.srcColor = blendFactor(s.src), b.dstColor = blendFactor(s.dst);
    b.colorOp = s.op == BlendOp::Subtract ? core::BlendOp::Subtract : s.op == BlendOp::ReverseSubtract ? core::BlendOp::ReverseSubtract : core::BlendOp::Add;
    // Local's glBlendFuncSeparate(src, dst, ONE, ONE_MINUS_SRC_ALPHA), one equation.
    b.srcAlpha = core::BlendFactor::One, b.dstAlpha = core::BlendFactor::OneMinusSrcAlpha;
    b.alphaOp = b.colorOp;
    b.writeMask = uint8_t((s.colorWrite ? core::kWriteRGB : 0) | (s.alphaWrite ? core::kWriteA : 0));
    d.colorFormats[0] = t.format;
    d.colorCount = 1;
    d.depthFormat = t.depth ? core::Format::D24 : core::Format::Undefined;
    core::Pipeline p = dev_->createPipeline(d);
    if (!p) SDL_Log("render: pipeline for %s: %s", prog->name.c_str(), dev_->lastError().c_str());
    pipelines_[key] = p;
    return p;
}

void CoreDevice::draw(const DrawCall& c) {
    const CProgram* prog = programs_.get(c.program.id);
    CBuffer* vb = buffers_.get(c.vertices.id);
    if (!prog || !vb || !c.layout || c.count <= 0) return;
    const CTarget* t = boundTarget();
    if (!t) return;
    // Slices of dynamic buffers are allocated before the pass when possible.
    core::BufferRange vr;
    if (vb->dynamic) vr = dynamicRange(*vb, c.vertexOffset);
    else vr = core::BufferRange{vb->buf, c.vertexOffset, vb->size > c.vertexOffset ? vb->size - c.vertexOffset : 0};
    CBuffer* ib = buffers_.get(c.indices.id);
    core::BufferRange ir;
    const size_t indexSize = c.indices32 ? 4 : 2;
    uint32_t firstIndex = uint32_t(c.first);
    if (ib) {
        if (ib->dynamic) {
            ir = dynamicRange(*ib, size_t(c.first) * indexSize);
            firstIndex = 0;
        } else {
            ir = core::BufferRange{ib->buf, 0, ib->size};
        }
    }
    if (!vr || (ib && !ir)) return;
    const core::Pipeline pl = pipeline(c, *t);
    if (!pl) return;
    ensurePass();
    dev_->bindPipeline(pl);
    dev_->bindVertexBuffers(0, &vr, 1);
    if (ib) dev_->bindIndexBuffer(ir, c.indices32 ? core::IndexType::U32 : core::IndexType::U16);
    core::TextureBinding tb[kMaxTextureSlots];
    int count = 0;
    for (int i = 0; i < kMaxTextureSlots; ++i) {
        const CTexture* tex = textures_.get(c.textures[i].id);
        if (tex) tb[i] = {tex->tex, sampler(c.samplers[i])}, count = i + 1;
        else if (prog->samplerUsed[i]) tb[i] = {textures_.get(white_.id)->tex, whiteSampler_}, count = i + 1;  // local binds white
    }
    if (count) dev_->bindTextures(0, tb, count);
    for (int i = 0; i < kMaxUniformBlocks; ++i) {
        const UniformData& u = c.uniforms[i];
        if (!u.size) continue;
        const void* data = u.staged ? static_cast<const void*>(staged_.data() + (u.staged - 1)) : u.data;
        if (!data) continue;
        dev_->pushUniforms(i, data, u.size);
        perf_.uniformBytes += u.size;
    }
    ++perf_.draws;
    if (ib) dev_->drawIndexed(uint32_t(c.count), 1, firstIndex, 0);
    else dev_->draw(uint32_t(c.count), 1, uint32_t(c.first));
}

SDL_Surface* CoreDevice::readPixels(int x, int y, int w, int h) {
    const CTarget* f = targets_.get(frameTarget_.id);
    if (w <= 0 || h <= 0 || !f) return nullptr;
    if (inPass_) endPass();
    // The frame is stored bottom-up: the rectangle from the bottom, rows flipped.
    const core::Readback r = dev_->readTexture(textures_.get(f->color.id)->tex, core::Rect{x, f->h - y - h, w, h});
    std::vector<uint8_t> rows(size_t(w) * size_t(h) * 4);
    if (!r || !dev_->readbackData(r, rows.data(), rows.size())) return nullptr;
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    for (int row = 0; row < h; ++row)
        std::memcpy(static_cast<uint8_t*>(s->pixels) + size_t(row) * size_t(s->pitch), &rows[size_t(h - 1 - row) * size_t(w) * 4], size_t(w) * 4);
    applyOutputGamma(s, gamma_);
    return s;
}

// The Xbox's output to a TV (setOutputGamma, outputCurve): the frame through
// it into gammaTarget_, presented instead of it. Empty: present the frame.
TargetId CoreDevice::applyGamma() {
    const CTarget* f = targets_.get(frameTarget_.id);
    if (gamma_ <= 0 || !f) return {};
    if (!gammaTried_) {
        gammaTried_ = true;
        ProgramDesc d;
        d.name = "output_gamma";
        d.vertex = "layout(location = 0) in vec2 aPos;\nout vec2 vUv;\nvoid main() {\n    vUv = aPos * 0.5 + 0.5;\n    gl_Position = vec4(aPos, 0.0, 1.0);\n}\n";
        d.fragment = std::string("in vec2 vUv;\nout vec4 fragColor;\nlayout(std140) uniform GdGamma {\n    vec4 uGamma;\n};\nuniform sampler2D sTex;\n") +
                     kOutputCurveGlsl + "void main() {\n    fragColor = vec4(gdOutputCurve(texture(sTex, vUv).rgb, uGamma.x), 1.0);\n}\n";
        d.uniformBlocks = {"GdGamma"};
        d.samplers = {"sTex"};
        std::string error;
        gammaProgram_ = createProgram(d, error);
        const float tri[6] = {-1, -1, 3, -1, -1, 3};
        gammaTriangle_ = createBuffer(BufferKind::Vertex, sizeof tri, tri, false);
        if (!gammaProgram_ || !gammaTriangle_) SDL_Log("render: gamma ramp shader: %s; showing the frame unramped", error.c_str());
    }
    if (!gammaProgram_ || !gammaTriangle_) return {};
    const CTarget* g = targets_.get(gammaTarget_.id);
    if (!g || g->w != f->w || g->h != f->h) {
        if (g) destroyTarget(gammaTarget_);
        std::string error;
        gammaTarget_ = createTarget(TargetDesc{f->w, f->h, false, TargetFormat::RGBA8}, error);
        if (!gammaTarget_) return {};
    }
    static VertexLayout layout = [] {
        VertexLayout l;
        l.stride = 8;
        l.attribs[0] = {0, 2, AttribType::Float, 0};
        return l;
    }();
    setTarget(gammaTarget_);
    const float block[4] = {gamma_, 0, 0, 0};
    DrawCall c;
    c.program = gammaProgram_;
    c.state.alphaWrite = true;
    c.layout = &layout;
    c.vertices = gammaTriangle_;
    c.count = 3;
    c.textures[0] = targetTexture(frameTarget_);
    c.samplers[0] = SamplerState{Filter::Nearest, Wrap::Clamp, 0};
    c.uniforms[0] = UniformData{block, sizeof block, 0};
    draw(c);
    if (inPass_) endPass();
    return gammaTarget_;
}

// The frame cap, as local's (gl_device.cpp setFrameCap): vsync where the
// display's rate is a whole multiple of the cap, software pacing otherwise;
// headless runs are not paced.
void CoreDevice::setFrameCap(int fps) {
    if (fps <= 0) {
        dev_->setPresentMode(core::PresentMode::Immediate);
        paceNs_ = 0, nextFrameNs_ = 0;
        return;
    }
    fps = std::max(fps, 30);
    int interval = 0;
    if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_)); m && m->refresh_rate > 0) {
        const float n = m->refresh_rate / float(fps);
        if (n >= 0.95f && std::fabs(n - std::round(n)) < 0.05f) interval = int(std::round(n));
    }
    const char* driver = SDL_GetCurrentVideoDriver();
    const bool headless = driver && std::strcmp(driver, "offscreen") == 0;
    dev_->setPresentMode(interval > 0 ? core::PresentMode::Vsync : core::PresentMode::Immediate);
    paceNs_ = headless || interval == 1 ? 0 : 1000000000ull / uint64_t(fps);
    nextFrameNs_ = 0;
}

void CoreDevice::endFrame() {
    const uint64_t start = SDL_GetTicksNS();
    if (inPass_) endPass();
    if (paceNs_) {
        uint64_t now = SDL_GetTicksNS();
        if (nextFrameNs_ > now && nextFrameNs_ - now <= paceNs_) SDL_DelayPrecise(nextFrameNs_ - now);
        perf_.paceNs += SDL_GetTicksNS() - now;
        now = SDL_GetTicksNS();
        nextFrameNs_ = nextFrameNs_ + paceNs_ >= now ? nextFrameNs_ + paceNs_ : now + paceNs_;
    }
    const TargetId ramped = applyGamma();
    if (const CTarget* f = targets_.get((ramped ? ramped : frameTarget_).id)) {
        // Stored bottom-up (GL style): presented flipped, scaled as local's blit.
        dev_->present(textures_.get(f->color.id)->tex, core::Rect{0, 0, f->w, f->h}, core::Filter::Linear, true);
    }
    dev_->endFrame();
    target_ = {};
    perf_.presentNs += SDL_GetTicksNS() - start;
    ++perf_.frames;
}

}  // namespace

std::unique_ptr<RenderDevice> createCoreDevice(Backend b, SDL_Window* window, std::string& error) {
    if (b == Backend::Local) {
        error = "local is not a core backend";
        return nullptr;
    }
    auto device = std::make_unique<CoreDevice>(b, window);
    if (!device->init(error)) return nullptr;
    return device;
}

}  // namespace gfx
