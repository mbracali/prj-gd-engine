// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// Renderer on gd-engine-core (gd::core::render::Device): what runs when
// Enhanced > Graphics backend is a core backend (opengl3 now). Renderer
// (renderer.h) forwards every public call here when one is active; with
// "local" this class is never made and Renderer runs its own GL code
// unchanged. It reproduces the local renderer's pixels: the same shader
// maths, the same uniform persistence, the same frame target and depth
// format (gd-engine-core/docs/render.md section 6).
//
// The Enhanced post-processing chain and True HDR output run here too
// (core_post_fx.h: local's PostFx and outputHdr on the Device). Not on this
// path yet: the FSR 3.1 / DLSS upscalers (GL <-> Vulkan interop); FSR 1
// takes their place, as on local when they cannot run.

#include <SDL3/SDL.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gd/core/render/device.h"
#include "render/core_post_fx.h"
#include "render/renderer.h"

class CoreRenderer {
public:
    explicit CoreRenderer(Renderer& owner) : r_(owner) {}
    ~CoreRenderer();
    // `hooks`: the game's own GL context calls (the launcher's module shims).
    bool init(SDL_Window* window, gd::core::render::Backend backend, const gd::core::render::GlHooks& hooks, std::string& error);
    void shutdown();
    const char* backendId() const;

    void beginFrame(SDL_Color clear);
    float beginWide(float maxAspect);
    void endWide();
    void clip43();
    void clipEnd();
    void endFrame();
    int createTexture(const Image& image, bool repeat);
    void updateTexture(int texture, const Image& image);
    void setTextureNearest(int texture);  // the debug font
    void draw(const Vertex* v, size_t count, int texture, Blend blend, const float* mvp, bool depthTest, bool depthWrite);
    void clearDepth();
    void depthBlur(float depth, float tapPx);
    bool ensureBlurProgram();
    float readDepth(int x, int y);
    void readDepthsAsync(unsigned& buffer, size_t& capacity, const std::vector<std::array<int, 2>>& points);
    std::vector<float> mapDepths(unsigned buffer, size_t count);
    void grabNdc(int slot, float x0, float y0, float x1, float y1);
    void drawGrab(int slot, float add);
    void setFrameCap(int fps);
    int createMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void updateMesh(int mesh, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void drawMesh(int mesh, const float* mvp, const Renderer::RenderState& state);
    int createRenderTarget(int width, int height);
    void beginRenderTarget(int texture);
    void endRenderTarget();
    std::string upscalerStatus() const;
    bool beginScene();
    void endScene(const PostFrame& frame);
    std::string hdrStatus() const;
    Image readPixels();
    std::vector<std::string> compileAllShaders(int& programs);

private:
    using Texture = gd::core::render::Texture;
    using Sampler = gd::core::render::Sampler;
    using Buffer = gd::core::render::Buffer;
    using Pipeline = gd::core::render::Pipeline;
    using Shader = gd::core::render::Shader;
    // The main program's uniforms, std140 (block DrawState, slot 0). Every
    // field keeps its last value between draws, as local's GL uniforms do.
    struct DrawState {
        float mvp[16];
        float model[16];
        float bones[64];
        float texU[16];               // the mat3 as four columns
        float tint[4];
        float uvOffsetJitter[4];      // uv offset, TAA jitter (0 here)
        float matFlags[4];            // use_environ, vert_amb, vert_dyn
        float fog[4];                 // enabled, start, end
        float fogColorCutout[4];      // fog colour, alpha cutout
        float texOColorScale[4];      // texgen origin, colour scale
        float eye[4], camRight[4], camUp[4];
        int32_t flags[4];             // use texture, skin, lit, texgen
        int32_t flags2[4];            // TFX decal, light count
    };
    struct LightState {               // block Lights, slot 1
        float ambient[4];
        float dir[8][4];
        float color[8][4];
    };
    struct Tex {
        Texture tex;
        Sampler sampler;
        int w = 0, h = 0;
        bool repeat = false;
    };
    struct Mesh {
        Buffer vb, ib;
        size_t vbBytes = 0, ibBytes = 0;
        int count = 0;
    };
    enum Program { kMain, kFill, kBlur, kProbe, kPrograms };

    void setMvp(const float* m);
    void setJitter(const std::array<float, 2>& j);  // TAA's sub-pixel offset of the scene's draws (local's uJitter)
    void ensureFrame();
    // What target_ 0 is: the post-processing scene while one is active (Renderer::beginScene), else the frame.
    Texture mainColor() const { return r_.sceneActive_ ? sceneColor_ : frameColor_; }
    Texture mainDepth() const { return r_.sceneActive_ ? sceneDepth_ : frameDepth_; }
    int mainWidth() const { return r_.sceneActive_ ? sceneW_ : frameW_; }
    int mainHeight() const { return r_.sceneActive_ ? sceneH_ : frameH_; }
    bool targetF16() const;  // the current target is RGBA16F (an HDR scene or frame)
    // Copies a w x h rectangle of the current target into `dst` (RGBA8), as local's glCopyTexSubImage2D.
    void copyFromTarget(int x, int y, Texture dst, int w, int h);
    void ensurePass();  // begins the current target's pass (loading what it holds) if none is open
    void suspend();     // ends the pass: resource updates and copies happen between passes
    int targetHeight() const;
    void applyRects();
    Pipeline pipeline(Program program, Blend blend, bool depthTest, bool depthWrite, int cull, bool decal, uint8_t writeMask,
                      bool f16 = false);
    uint8_t colorMask() const;  // the current target's (local's glColorMask)
    Sampler sampler(gd::core::render::Filter min, gd::core::render::Filter mag, bool mips, bool repeat);
    void drawMain(int texture, Blend blend, bool depthTest, bool depthWrite, int cull, bool decal);
    void fillRectPixels(const int rect[4], const float rgba[4], bool depthOnly);

    Renderer& r_;
    gd::core::render::Backend backend_ = gd::core::render::Backend::OpenGL3;
    std::unique_ptr<gd::core::render::Device> dev_;
    Shader shaders_[kPrograms];
    bool shaderOk_[kPrograms] = {};
    std::map<uint32_t, Pipeline> pipelines_;
    std::vector<Tex> textures_{Tex{}};
    std::vector<Mesh> meshes_{Mesh{}};
    std::map<int, Texture> targetDepth_;  // render target handle -> its depth
    Texture frameColor_, frameDepth_;
    int frameW_ = 0, frameH_ = 0;
    bool frameF16_ = false;  // True HDR: an RGBA16F frame, converted for the surface by endFrame
    // Enhanced post-processing: the scene target (Renderer::beginScene) and the chain.
    std::unique_ptr<CorePostFx> post_;
    Texture sceneColor_, sceneDepth_;
    int sceneW_ = 0, sceneH_ = 0;
    bool sceneF16_ = false;
    // True HDR: the converted frame (RGBA32F: local writes the shader's floats
    // straight to the surface) and an RGBA8 copy of the frame for readPixels.
    Texture hdrOut_, readCopy_;
    int hdrOutW_ = 0, hdrOutH_ = 0, readCopyW_ = 0, readCopyH_ = 0;
    int target_ = 0;  // 0: the frame; else the render target being drawn
    // Viewport and scissor as local sets them (GL style: origin bottom-left).
    int vp_[4] = {0, 0, 0, 0}, sc_[4] = {0, 0, 0, 0};
    bool scissorOn_ = false, rectsDirty_ = true;
    DrawState ds_{};
    LightState ls_{};
    Tex white_;
    // Depth of field, track fade, lens-flare depth probes.
    Texture blurTex_;
    int blurW_ = 0, blurH_ = 0;
    struct Grab {
        int texture = 0, w = 0, h = 0;
        float vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0;
        bool valid = false;
    } grabs_[2];
    Texture probeTex_;
    int probeW_ = 0;
    std::map<unsigned, std::pair<gd::core::render::Readback, size_t>> probes_;
    unsigned nextProbe_ = 1;
    uint64_t paceNs_ = 0, nextFrameNs_ = 0;
    bool toldPost_ = false, toldHdr_ = false;
};
