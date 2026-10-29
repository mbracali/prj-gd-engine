// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The render device: the only part of the renderer that talks to a graphics
// API. Everything above it (2D canvas, GH2 materials, scenes) uses these
// handles and plain state structs, so the OpenGL back end (gl_device.cpp:
// OpenGL 3.3 core, or OpenGL ES 3.0 on mobile and with GD04_GLES=1) can be
// joined later by a Vulkan one without touching the callers.
//
// Shaders are written once in the GLSL subset shared by "330 core" and
// "300 es" (the back end adds the version line, precision and the program's
// #defines). Uniforms live in std140 blocks bound to fixed binding points
// and samplers in fixed texture slots, which is also how a Vulkan back end
// would lay out its descriptor sets.
namespace gfx {

struct TextureId { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct BufferId { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct ProgramId { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct TargetId { uint32_t id = 0; explicit operator bool() const { return id != 0; } };

// Texture formats. BC1/BC3/BC5 are GH2's DXT1, DXT5 and ATI2 (two-channel
// normal maps); the data passed in is in PC block order (the Xbox 360's
// 16-bit swap already undone, texture_codec.h). Where the GPU lacks S3TC or
// RGTC (most GLES drivers) the device decodes them to RGBA8 on the CPU.
// R8: one 8-bit channel (the video player's Y, U and V planes).
enum class TexFormat : uint8_t { RGBA8, BC1, BC3, BC5, R8 };
enum class TexKind : uint8_t { Tex2D, Cube };

struct TextureDesc {
    int width = 0, height = 0;
    int mips = 1;                 // levels supplied (largest first)
    TexFormat format = TexFormat::RGBA8;
    TexKind kind = TexKind::Tex2D;
};

// One mip level (for a cube: one face's level; faces ordered +x -x +y -y +z -z).
struct TexLevel {
    const void* data = nullptr;
    size_t size = 0;
};

enum class Filter : uint8_t { Nearest, Linear, Trilinear };
enum class Wrap : uint8_t { Clamp, Repeat, Mirror, BorderBlack, BorderWhite };
struct SamplerState {
    Filter filter = Filter::Trilinear;
    Wrap wrap = Wrap::Repeat;
    float lodBias = 0;
    bool operator==(const SamplerState&) const = default;
};

enum class BufferKind : uint8_t { Vertex, Index };

// Render target colour formats. RGB10A2 is GH2's own HDR buffer (2:10:10:10,
// colour stored / 4; render/post_fx.h), colour-renderable in OpenGL ES 3.0
// too; RGBA16F needs EXT_color_buffer_(half_)float on ES.
enum class TargetFormat : uint8_t { RGBA8, RGB10A2, RGBA16F };
struct TargetDesc {
    int width = 0, height = 0;
    bool depth = false;  // a depth buffer, sampled through targetDepth (nearest filtering only)
    TargetFormat format = TargetFormat::RGBA8;
};

enum class BlendFactor : uint8_t { Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DstColor, InvDstColor, DstAlpha, InvDstAlpha };
enum class BlendOp : uint8_t { Add, Subtract, ReverseSubtract };
enum class CompareFunc : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
// Which screen-space winding is culled, as seen on the screen (y down), the
// way Direct3D (and so GH2's Mat "cull") states it.
enum class Cull : uint8_t { None, Clockwise, CounterClockwise };

struct PipelineState {
    bool blend = false;
    BlendFactor src = BlendFactor::One, dst = BlendFactor::Zero;
    BlendOp op = BlendOp::Add;
    bool depthTest = false, depthWrite = false;
    CompareFunc depthFunc = CompareFunc::LessEqual;
    Cull cull = Cull::None;
    bool colorWrite = true, alphaWrite = false;
};

enum class AttribType : uint8_t { Float, UNorm8 };
struct VertexAttrib {
    uint8_t location = 0;
    uint8_t components = 0;   // 0: unused
    AttribType type = AttribType::Float;
    uint16_t offset = 0;
};
struct VertexLayout {
    uint16_t stride = 0;
    VertexAttrib attribs[16];
};

// Vertex attribute locations shared by every program.
enum StdAttrib : uint8_t { kAttribPos = 0, kAttribNormal = 1, kAttribColor = 2, kAttribUv = 3, kAttribWeights = 4, kAttribTangent = 5 };

constexpr int kMaxTextureSlots = 16;
constexpr int kMaxUniformBlocks = 4;

// A program: GLSL bodies (no #version), the #defines this permutation needs,
// the uniform block names in binding order and the sampler names in slot
// order (empty names skip a binding or slot).
struct ProgramDesc {
    std::string name;
    std::string vertex, fragment;
    std::string defines;  // "#define X 1\n..." lines
    std::vector<std::string> uniformBlocks;
    std::vector<std::string> samplers;
};

// A uniform block's contents for one draw: copied at draw time, or staged
// once with RenderDevice::stageUniforms and reused by many draws.
struct UniformData {
    const void* data = nullptr;
    size_t size = 0;
    size_t staged = 0;  // stageUniforms' result (offset + 1); 0: copy `data`
};

enum class Primitive : uint8_t { Triangles, Lines };

struct DrawCall {
    ProgramId program;
    PipelineState state;
    const VertexLayout* layout = nullptr;
    BufferId vertices;
    size_t vertexOffset = 0;  // bytes
    BufferId indices;         // none: non-indexed
    bool indices32 = false;
    Primitive primitive = Primitive::Triangles;
    int first = 0, count = 0;  // first index (or vertex) and how many
    TextureId textures[kMaxTextureSlots];
    SamplerState samplers[kMaxTextureSlots];
    UniformData uniforms[kMaxUniformBlocks];
};

struct DeviceInfo {
    std::string api;        // "OpenGL" or "OpenGL ES"
    std::string version, gpu;
    bool es = false;
    bool s3tc = false, rgtc = false;  // native BC1/BC3 and BC5 sampling
    bool clipDepthZeroToOne = false;  // projection: depth 0..1 instead of -1..1
};

// GD04_PERF=1: what the device did since the last read (perfStats).
struct DevicePerf {
    uint64_t draws = 0, uniformBytes = 0, bufferBytes = 0, textureBytes = 0;
    uint64_t created = 0;    // textures, buffers, targets and programs made (each one a driver allocation)
    uint64_t presentNs = 0;  // the blit to the window, pacing and SDL_GL_SwapWindow
    uint64_t fenceNs = 0;    // waiting for the frame before last
    uint64_t paceNs = 0;     // of presentNs: the software pacing's sleep
    int frames = 0;
};

class RenderDevice {
public:
    virtual ~RenderDevice() = default;
    virtual const DeviceInfo& info() const = 0;

    // Resources. Creation returns an empty handle and sets `error` on failure.
    virtual TextureId createTexture(const TextureDesc& desc, const TexLevel* levels, std::string& error) = 0;
    virtual void destroyTexture(TextureId t) = 0;
    // Replaces mip level 0 of an RGBA8 or R8 2D texture (streamed images:
    // video frames); `pitch` is the source's bytes per row.
    virtual void updateTexture(TextureId t, const void* data, size_t pitch) = 0;
    virtual BufferId createBuffer(BufferKind kind, size_t size, const void* data, bool dynamic) = 0;
    // Replaces `size` bytes at `offset`. A dynamic buffer written at offset 0
    // is renamed first, so the GPU may still read the previous contents.
    virtual void updateBuffer(BufferId b, size_t offset, const void* data, size_t size) = 0;
    virtual void destroyBuffer(BufferId b) = 0;
    virtual ProgramId createProgram(const ProgramDesc& desc, std::string& error) = 0;
    virtual void destroyProgram(ProgramId p) = 0;
    // An RGBA8 colour target with an optional depth buffer; its colour (and
    // depth) can be sampled through targetTexture / targetDepth.
    virtual TargetId createTarget(int width, int height, bool depth, std::string& error) = 0;
    // The same in another colour format (empty with `error` set when the
    // driver cannot render to it).
    virtual TargetId createTarget(const TargetDesc& desc, std::string& error) = 0;
    virtual TextureId targetTexture(TargetId t) const = 0;
    virtual TextureId targetDepth(TargetId t) const = 0;
    virtual void destroyTarget(TargetId t) = 0;

    // Frames. beginFrame waits until at most one earlier frame is still on
    // the GPU (no deeper queue, gd01 lesson L7) and binds the window.
    virtual void beginFrame() = 0;
    // Draws into `t` (empty: the window), viewport in that target's pixels
    // (origin top-left).
    virtual void setTarget(TargetId t) = 0;
    virtual void setViewport(int x, int y, int w, int h) = 0;
    // The target and viewport bound now, to bind them again after drawing
    // into another target mid-frame (the crowd's cards).
    virtual TargetId currentTarget() const = 0;
    virtual void currentViewport(int& x, int& y, int& w, int& h) const = 0;
    virtual void clear(const float* rgba, bool depth) = 0;
    virtual void draw(const DrawCall& call) = 0;
    // Copies a uniform block for draws of this frame (until endFrame), so
    // shared blocks (camera, lights) are uploaded once.
    virtual UniformData stageUniforms(const void* data, size_t size) = 0;
    // The window's size in pixels (the internal resolution's, when one is set).
    virtual void drawableSize(int& w, int& h) const = 0;
    // Reads a rectangle of the window's back buffer (origin top-left), as
    // RGBA32. Synchronous: development aids only (screenshots).
    virtual SDL_Surface* readPixels(int x, int y, int w, int h) = 0;
    // Presents, keeping to the frame cap (vsync where the display's rate
    // matches it, otherwise paced in software; headless is not paced).
    virtual void endFrame() = 0;
    virtual void setFrameCap(int fps) = 0;
    // The internal resolution (Enhanced Options): every frame is drawn into
    // an off-screen colour + depth target `height` lines tall (the window's
    // aspect), which drawableSize then reports, and scaled to the window as
    // it is presented. 0: straight to the window.
    virtual void setInternalHeight(int height) = 0;
    // The counters since the last call, then cleared.
    virtual DevicePerf takePerf() = 0;
};

// The window flags and GL attributes the device needs: call before
// SDL_CreateWindow and pass the result in its flags. OpenGL 3.3 core
// (forward compatible, as macOS requires) or OpenGL ES 3.0 on Android and
// with GD04_GLES=1.
SDL_WindowFlags prepareWindow();
// Creates the OpenGL device for a window made with prepareWindow's flags.
std::unique_ptr<RenderDevice> createGlDevice(SDL_Window* window, std::string& error);

}  // namespace gfx
