// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gfx::RenderDevice over gd::core::render::Device (render/core_backend.h):
// gd10's device interface on gd-engine-core's opengl3, sdl3 and vulkan
// backends (gd-engine-core/docs/render.md section 6c, "gd-game-10 on the
// core"). gd02's adapter (gd-game-02/engine/src/render/core_device.cpp) with
// what GH3's device adds: D24S8 targets and the stencil state, copyDepth,
// the 360's display gamma at present, occlusion queries (not in the core:
// see queryResult) and 16 vertex attributes, those a shader reads but a
// layout does not give fed (0, 0, 0, 1) as GL does.
//
// How gd10's GL-shaped calls map onto the core's explicit ones:
// - Pictures are stored as local's are (GL style, row 0 at the bottom): every
//   vertex shader ends with gl_Position's y negated before the core's
//   gd_clip, so on opengl3 the core's own flip cancels it and the rasteriser
//   sees exactly local's coordinates (gd01's CoreRenderer does the same in
//   its MVPs). gd10's top-left viewports become y = height - y - h, the cull
//   winding is mirrored, the frame is presented flipped and readPixels flips
//   its rows, as local does.
// - Pipelines (gd10's DrawCall carries its whole state) are made on first
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
#include <cctype>
#include <cmath>
#include <cstdlib>
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
// A gd10 buffer: static ones are core buffers; dynamic ones are a CPU copy
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
    bool samplerCube[kMaxTextureSlots] = {};
    uint32_t attribs = 0;  // bit l: the vertex shader reads location l
};
struct CTarget {
    TextureId color, depth;
    core::Format format = core::Format::RGBA8;
    bool stencil = false;  // depth is D24S8 (else D24)
    int w = 0, h = 0;
    core::Format depthFormat() const { return !depth ? core::Format::Undefined : stencil ? core::Format::D24S8 : core::Format::D24; }
};

// The names a GLSL source declares as `uniform samplerCube name;`, and the
// locations of its `layout(location = N) in` inputs.
bool declaresCube(const std::string& src, const std::string& name) {
    for (size_t at = src.find("samplerCube"); at != std::string::npos; at = src.find("samplerCube", at + 1)) {
        size_t p = at + 11;
        while (p < src.size() && (src[p] == ' ' || src[p] == '\t')) ++p;
        if (src.compare(p, name.size(), name) == 0 && p + name.size() < src.size() &&
            !(std::isalnum(uint8_t(src[p + name.size()])) || src[p + name.size()] == '_'))
            return true;
    }
    return false;
}
uint32_t inputLocations(const std::string& src) {
    uint32_t mask = 0;
    for (size_t at = src.find("location"); at != std::string::npos; at = src.find("location", at + 1)) {
        size_t p = at + 8;
        while (p < src.size() && (src[p] == ' ' || src[p] == '=')) ++p;
        const int loc = std::atoi(src.c_str() + p);
        while (p < src.size() && std::isdigit(uint8_t(src[p]))) ++p;
        while (p < src.size() && (src[p] == ' ' || src[p] == ')')) ++p;
        if (src.compare(p, 3, "in ") == 0 && loc >= 0 && loc < 32) mask |= 1u << loc;
    }
    return mask;
}

uint32_t givenAttribs(const VertexLayout& l) {
    uint32_t m = 0;
    for (const VertexAttrib& a : l.attribs)
        if (a.components) m |= 1u << a.location;
    return m;
}

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
// copyDepth: the source's depth, rows reversed (local's glBlitFramebuffer
// from 0..h to h..0). gl_FragCoord and texelFetch both count rows from the
// first one in memory on every backend.
const char* kCopyDepthFragment = R"(
GD_SAMPLER2D(0, sSrc);
GD_FRAG_OUT(0) vec4 fragColor;
void main() {
    ivec2 size = textureSize(sSrc, 0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    gl_FragDepth = texelFetch(sSrc, ivec2(p.x, size.y - 1 - p.y), 0).r;
    fragColor = vec4(0.0);
}
)";
// The display gamma (gl_device.cpp initGamma): each channel through the
// 256-entry ramp, interpolated; the frame read 1:1 (present scales it).
const char* kGammaFragment = R"(
GD_SAMPLER2D(0, sFrame);
GD_SAMPLER2D(1, sRamp);
GD_FRAG_OUT(0) vec4 fragColor;
float lut(float v) { return texture(sRamp, vec2(clamp(v, 0.0, 1.0) * (255.0 / 256.0) + 0.5 / 256.0, 0.5)).r; }
void main() {
    vec3 c = texelFetch(sFrame, ivec2(gl_FragCoord.xy), 0).rgb;
    fragColor = vec4(lut(c.r), lut(c.g), lut(c.b), 1.0);
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
    void copyDepth(TargetId src, TargetId dst) override;
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
    // Occlusion queries: the core's (opengl3, vulkan). A backend without them
    // (sdl3: SDL_GPU has none) answers every query at once with one
    // sample, so the flares (venue_view.cpp drawFlare, B / A) are drawn at
    // full size, occluded or not (gd-engine-core/docs/render.md 6c).
    QueryId createQuery() override {
        queries_.push_back(dev_->createQuery());
        return {uint32_t(queries_.size())};
    }
    void destroyQuery(QueryId q) override {
        if (!q.id || q.id > queries_.size()) return;
        if (queries_[q.id - 1]) dev_->destroyQuery(queries_[q.id - 1]);
        queries_[q.id - 1] = {};
    }
    void beginQuery(QueryId q) override {
        if (!q.id || q.id > queries_.size() || !queries_[q.id - 1]) return;
        ensurePass();  // the core counts inside a pass
        dev_->beginQuery(queries_[q.id - 1]);
    }
    void endQuery() override { dev_->endQuery(); }
    bool queryResult(QueryId q, uint64_t& samples) override {
        if (!q.id || q.id > queries_.size()) return false;
        if (!queries_[q.id - 1]) return samples = 1, true;  // no queries on this backend
        return dev_->queryResult(queries_[q.id - 1], samples);
    }
    DevicePerf takePerf() override {
        DevicePerf p = perf_;
        perf_ = {};
        return p;
    }
    uint64_t drawCalls() const override { return drawCalls_; }

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
    TextureId white_, whiteCube_;
    core::Sampler whiteSampler_;
    core::Buffer constantAttrib_;  // (0, 0, 0, 1): the inputs a layout does not give
    core::Shader copyDepthShader_, gammaShader_;
    core::Sampler nearestSampler_, rampSampler_;
    std::vector<core::Query> queries_;  // QueryId = index + 1; empty: the backend has none
    uint64_t drawCalls_ = 0;
    bool coreFrameOpen_ = false;
    // The 360's display gamma (gl_device.cpp displayGammaRamp), applied as
    // the frame is presented and to readPixels, as local's.
    bool gamma_ = true;
    uint8_t gammaRamp_[256] = {};
    TextureId gammaLut_;
    TargetId gammaTarget_;
    void presentGamma(CTarget frame);  // a copy: making the gamma target moves targets_
    std::vector<std::pair<SamplerState, core::Sampler>> samplers_;
    std::map<std::string, core::Pipeline> pipelines_;
    core::Shader clearShader_;
    uint64_t frame_ = 1;

    // The frame: every picture is drawn into this target (the window's size,
    // or the internal resolution's) and presented scaled.
    int internalH_ = 0;
    TargetId frameTarget_;
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
    if (copyDepthShader_) dev_->destroyShader(copyDepthShader_);
    if (gammaShader_) dev_->destroyShader(gammaShader_);
    if (constantAttrib_) dev_->destroyBuffer(constantAttrib_);
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
    ci.cacheDir = prefDir();
#if GD10_MODULE
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
    const bool forceDecode = SDL_getenv("GD10_DECODE_TEXTURES") != nullptr;
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
    const TexLevel faces[6] = {level, level, level, level, level, level};
    whiteCube_ = createTexture({1, 1, 1, TexFormat::RGBA8, TexKind::Cube}, faces, error);
    if (!whiteCube_) return false;
    whiteSampler_ = sampler(SamplerState{Filter::Linear, Wrap::Repeat, 0});
    const float constant[4] = {0, 0, 0, 1};
    core::BufferDesc bd;
    bd.size = sizeof constant, bd.usage = core::kVertexBuffer, bd.label = "gd10 constant attribute";
    constantAttrib_ = dev_->createBuffer(bd, constant);
    if (!constantAttrib_) return error = "constant attribute: " + dev_->lastError(), false;
    clearShader_ = makeShader("clear", kClearVertex, kClearFragment, "", {"GdClear"}, {}, true, error);
    if (!clearShader_) return false;
    copyDepthShader_ = makeShader("copy_depth", kClearVertex, kCopyDepthFragment, "", {}, {"sSrc"}, true, error);
    if (!copyDepthShader_) return false;
    core::SamplerDesc nd;
    nd.minFilter = nd.magFilter = core::Filter::Nearest, nd.mipFilter = core::MipFilter::None;
    nd.wrapU = nd.wrapV = nd.wrapW = core::Wrap::ClampToEdge;
    nearestSampler_ = dev_->createSampler(nd);
    core::SamplerDesc rd = nd;
    rd.minFilter = rd.magFilter = core::Filter::Linear;
    rampSampler_ = dev_->createSampler(rd);
    setFrameCap(60);
    // GD10_GAMMA=0: no display gamma (development aid, the frame as drawn).
    if (const char* g = SDL_getenv("GD10_GAMMA")) gamma_ = std::strcmp(g, "0") != 0;
    if (gamma_) {
        displayGammaRamp(gammaRamp_);
        uint8_t lut[256 * 4];
        for (int i = 0; i < 256; ++i) lut[i * 4] = lut[i * 4 + 1] = lut[i * 4 + 2] = gammaRamp_[i], lut[i * 4 + 3] = 255;
        TexLevel l{lut, sizeof lut};
        std::string gammaError;
        gammaLut_ = createTexture({256, 1, 1, TexFormat::RGBA8, TexKind::Tex2D}, &l, gammaError);
        if (gammaLut_) gammaShader_ = makeShader("display_gamma", kClearVertex, kGammaFragment, "", {}, {"sFrame", "sRamp"}, true, gammaError);
        if (!gammaShader_) SDL_Log("render: display gamma off: %s", gammaError.c_str()), gamma_ = false;
    }
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
    // gd10's vertex shaders end with gl_Position as local computes it; the
    // wrapper negates its y (pictures stored as local's, see the top) and
    // gives it to the core's gd_clip (on opengl3 the two flips cancel).
    std::string vs = vertex;
    if (!dialect)
        vs = "#define main gd10_vertex_main\n" + vertex +
             "\n#undef main\nvoid main() {\n    gd10_vertex_main();\n"
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
    for (size_t i = 0; i < desc.samplers.size() && i < size_t(kMaxTextureSlots); ++i) {
        p.samplerUsed[i] = !desc.samplers[i].empty();
        p.samplerCube[i] = p.samplerUsed[i] && (declaresCube(desc.fragment, desc.samplers[i]) || declaresCube(desc.vertex, desc.samplers[i]));
    }
    p.attribs = inputLocations(desc.vertex);
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
    t.stencil = desc.depth && desc.stencil;
    t.color = make(f, core::kSampledTexture | core::kColorTarget);
    if (desc.depth) t.depth = make(t.stencil ? core::Format::D24S8 : core::Format::D24, core::kSampledTexture | core::kDepthTarget);
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
    // A frame Canvas::end held (it drew nothing: no endFrame, the window
    // keeps the last picture) goes on as this one: the core's frames pair
    // beginFrame with endFrame.
    if (inPass_) endPass();
    if (!coreFrameOpen_) dev_->beginFrame();
    coreFrameOpen_ = true;
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
    // gd10's rectangles are top-left; the pictures are stored bottom-up (GL style).
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
        // local's clear(depth) clears the stencil to 0 too.
        p.depth.stencilLoad = !t->stencil ? core::LoadOp::DontCare : clearDepth_ ? core::LoadOp::Clear : core::LoadOp::Load;
        p.depth.stencilStore = t->stencil ? core::StoreOp::Store : core::StoreOp::DontCare;
        p.depth.clearDepth = 1;
        p.depth.clearStencil = 0;
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
    d.depthFormat = t->depthFormat();
    d.blend[0].writeMask = rgba ? core::kWriteAll : 0;
    d.depthStencil.depthTest = depth && t->depth;
    d.depthStencil.depthWrite = depth && t->depth;
    d.depthStencil.depthCompare = core::CompareOp::Always;
    if (depth && t->stencil) {  // the stencil to 0 (the reference) too
        d.depthStencil.stencil = true;
        d.depthStencil.front.pass = d.depthStencil.front.fail = d.depthStencil.front.depthFail = core::StencilOp::Replace;
        d.depthStencil.back = d.depthStencil.front;
        d.depthStencil.writeMask = 0xFF;
    }
    const std::string key = "clear:" + std::to_string(int(t->format)) + ":" + std::to_string(int(t->depthFormat())) + ":" +
                            std::to_string(rgba ? 1 : 0) + std::to_string(depth ? 1 : 0);
    core::Pipeline& pl = pipelines_[key];
    if (!pl) pl = dev_->createPipeline(d);
    if (!pl) return;
    const float zero[4] = {0, 0, 0, 0};
    dev_->bindPipeline(pl);
    dev_->setStencilRef(0);
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
    const bool stencil = s.stencilTest && t.stencil;
    const uint8_t st[] = {uint8_t(s.blend), uint8_t(s.src), uint8_t(s.dst), uint8_t(s.op), uint8_t(s.depthTest), uint8_t(s.depthWrite),
                          uint8_t(s.depthFunc), uint8_t(s.cull), uint8_t(s.colorWrite), uint8_t(s.alphaWrite), uint8_t(c.primitive),
                          uint8_t(t.format), uint8_t(t.depthFormat()), uint8_t(stencil),
                          uint8_t(stencil ? uint8_t(s.stencilFunc) : 0), uint8_t(stencil ? s.stencilMask : 0),
                          uint8_t(stencil ? s.stencilWriteMask : 0), uint8_t(stencil && s.stencilReplace)};
    key.append(reinterpret_cast<const char*>(st), sizeof st);
    key.append(reinterpret_cast<const char*>(&c.layout->stride), sizeof c.layout->stride);
    for (const VertexAttrib& a : c.layout->attribs) {
        if (!a.components) continue;
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
    uint32_t given = 0;
    for (const VertexAttrib& a : c.layout->attribs) {
        if (!a.components || d.layout.attributeCount >= core::kMaxVertexAttributes) continue;
        given |= 1u << a.location;
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
            va.format = core::VertexFormat::UByte4Norm;  // gd10's UNorm8 attributes are RGBA colours
        }
    }
    // Inputs the shader reads that the layout does not give: GL's (0, 0, 0, 1)
    // (local leaves those arrays disabled), from a one-element buffer in slot 1.
    if (const uint32_t missing = prog->attribs & ~given) {
        d.layout.bufferCount = 2;
        d.layout.buffers[1].stride = 16;
        d.layout.buffers[1].perInstance = true;
        for (int l = 0; l < 32 && d.layout.attributeCount < core::kMaxVertexAttributes; ++l)
            if (missing >> l & 1) d.layout.attributes[d.layout.attributeCount++] = core::VertexAttribute{uint8_t(l), 1, core::VertexFormat::Float4, 0};
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
    if (stencil) {  // local: glStencilFunc(func, ref, mask), glStencilMask, glStencilOp(KEEP, KEEP, REPLACE or KEEP)
        d.depthStencil.stencil = true;
        d.depthStencil.front.compare = compareOp(s.stencilFunc);
        d.depthStencil.front.pass = s.stencilReplace ? core::StencilOp::Replace : core::StencilOp::Keep;
        d.depthStencil.back = d.depthStencil.front;
        d.depthStencil.readMask = s.stencilMask;
        d.depthStencil.writeMask = s.stencilWriteMask;
    }
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
    d.depthFormat = t.depthFormat();
    core::Pipeline p = dev_->createPipeline(d);
    if (!p) SDL_Log("render: pipeline for %s: %s", prog->name.c_str(), dev_->lastError().c_str());
    pipelines_[key] = p;
    return p;
}

void CoreDevice::draw(const DrawCall& c) {
    const CProgram* prog = programs_.get(c.program.id);
    CBuffer* vb = buffers_.get(c.vertices.id);
    if (!prog || !vb || !c.layout || c.count <= 0) return;
    ++drawCalls_;  // as local's: a draw asked for (Canvas::end tells empty frames by it)
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
    const core::BufferRange vbs[2] = {vr, core::BufferRange{constantAttrib_, 0, 16}};
    dev_->bindVertexBuffers(0, vbs, prog->attribs & ~givenAttribs(*c.layout) ? 2 : 1);
    if (ib) dev_->bindIndexBuffer(ir, c.indices32 ? core::IndexType::U32 : core::IndexType::U16);
    if (c.state.stencilTest && t->stencil) dev_->setStencilRef(c.state.stencilRef);
    core::TextureBinding tb[kMaxTextureSlots];
    int count = 0;
    for (int i = 0; i < kMaxTextureSlots; ++i) {
        const CTexture* tex = textures_.get(c.textures[i].id);
        if (tex && tex->cube == prog->samplerCube[i]) tb[i] = {tex->tex, sampler(c.samplers[i])}, count = i + 1;
        else if (prog->samplerUsed[i])  // local binds white
            tb[i] = {textures_.get((prog->samplerCube[i] ? whiteCube_ : white_).id)->tex, whiteSampler_}, count = i + 1;
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
    if (gamma_)  // what the window shows: through the display gamma, as local
        for (size_t i = 0; i < rows.size(); ++i)
            if ((i & 3) != 3) rows[i] = gammaRamp_[rows[i]];
    for (int row = 0; row < h; ++row)
        std::memcpy(static_cast<uint8_t*>(s->pixels) + size_t(row) * size_t(s->pitch), &rows[size_t(h - 1 - row) * size_t(w) * 4], size_t(w) * 4);
    return s;
}

// The frame cap, as local's (gl_device.cpp setFrameCap): vsync where the
// display's rate is a whole multiple of the cap, software pacing otherwise;
// headless runs are not paced.
void CoreDevice::setFrameCap(int fps) {
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
    if (const CTarget* f = targets_.get(frameTarget_.id)) {
        // Stored bottom-up (GL style): presented flipped, scaled as local's blit;
        // through the display gamma first (presentGamma).
        if (gamma_) presentGamma(*f);
        const CTarget* g = gamma_ ? targets_.get(gammaTarget_.id) : nullptr;
        const CTarget* shown = g ? g : targets_.get(frameTarget_.id);
        dev_->present(textures_.get(shown->color.id)->tex, core::Rect{0, 0, shown->w, shown->h}, core::Filter::Linear, true);
    }
    dev_->endFrame();
    coreFrameOpen_ = false;
    target_ = {};
    perf_.presentNs += SDL_GetTicksNS() - start;
    ++perf_.frames;
}

// The frame through the display gamma into gammaTarget_ (the frame's size).
void CoreDevice::presentGamma(CTarget frame) {
    const CTarget* g = targets_.get(gammaTarget_.id);
    if (!g || g->w != frame.w || g->h != frame.h) {
        if (g) destroyTarget(gammaTarget_);
        std::string error;
        gammaTarget_ = createTarget(TargetDesc{frame.w, frame.h, false, TargetFormat::RGBA8}, error);
        g = targets_.get(gammaTarget_.id);
        if (!g) {
            SDL_Log("render: display gamma %dx%d: %s; off", frame.w, frame.h, error.c_str());
            gamma_ = false;
            return;
        }
    }
    if (inPass_) endPass();
    core::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = textures_.get(g->color.id)->tex;
    p.colors[0].load = core::LoadOp::DontCare;
    dev_->beginPass(p);
    core::Pipeline& pl = pipelines_["display_gamma"];
    if (!pl) {
        core::PipelineDesc d;
        d.shader = gammaShader_;
        d.colorFormats[0] = g->format;
        pl = dev_->createPipeline(d);
    }
    if (pl) {
        dev_->bindPipeline(pl);
        const core::TextureBinding tb[2] = {{textures_.get(frame.color.id)->tex, nearestSampler_}, {textures_.get(gammaLut_.id)->tex, rampSampler_}};
        dev_->bindTextures(0, tb, 2);
        dev_->draw(3);
    }
    dev_->endPass();
}

void CoreDevice::copyDepth(TargetId src, TargetId dst) {
    const CTarget* a = targets_.get(src.id);
    const CTarget* b = targets_.get(dst.id);
    if (!a || !b || !a->depth || !b->depth || a->w != b->w || a->h != b->h || a == b) return;
    if (!inPass_ && (clearColor_ || clearDepth_)) ensurePass();  // the source's pending clear first
    if (inPass_) endPass();
    // A pass on the copy that writes every pixel's depth (its stencil is not
    // copied: only the copy's depth is sampled, screen_fx.cpp depthSnapshot).
    core::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = textures_.get(b->color.id)->tex;
    p.colors[0].load = core::LoadOp::Load;
    p.depth.texture = textures_.get(b->depth.id)->tex;
    p.depth.load = core::LoadOp::DontCare;
    p.depth.stencilLoad = b->stencil ? core::LoadOp::Clear : core::LoadOp::DontCare;
    p.depth.stencilStore = b->stencil ? core::StoreOp::Store : core::StoreOp::DontCare;
    dev_->beginPass(p);
    const std::string key = "copy_depth:" + std::to_string(int(b->format)) + ":" + std::to_string(int(b->depthFormat()));
    core::Pipeline& pl = pipelines_[key];
    if (!pl) {
        core::PipelineDesc d;
        d.shader = copyDepthShader_;
        d.colorFormats[0] = b->format;
        d.depthFormat = b->depthFormat();
        d.blend[0].writeMask = 0;
        d.depthStencil.depthTest = true;
        d.depthStencil.depthWrite = true;
        d.depthStencil.depthCompare = core::CompareOp::Always;
        pl = dev_->createPipeline(d);
        if (!pl) SDL_Log("render: copyDepth: %s", dev_->lastError().c_str());
    }
    if (pl) {
        dev_->bindPipeline(pl);
        const core::TextureBinding tb{textures_.get(a->depth.id)->tex, nearestSampler_};
        dev_->bindTextures(0, &tb, 1);
        dev_->draw(3);
        ++perf_.draws;
    }
    dev_->endPass();
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
