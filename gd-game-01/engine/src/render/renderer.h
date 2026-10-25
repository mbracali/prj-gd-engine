// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "data/bitmap.h"
#include "render/post_fx.h"

constexpr float kScreenW = 1280, kScreenH = 720;

enum class Blend { Opaque, Alpha, Add, AddAlpha, Multiply, Subtract, None };

struct Vertex {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
    float nx = 0, ny = 0, nz = 0;  // normal, for lit meshes
    float w[4] = {0, 0, 0, 0};     // bone weights, for skinned meshes
};

// Per-vertex lighting for a mesh (RndEnviron + RndLight): colour *= ambient +
// the sum of the lights' diffuse terms, in world space.
struct Lighting {
    static constexpr int kMaxLights = 8;
    float ambient[3] = {1, 1, 1};
    int count = 0;
    float dir[kMaxLights][4];    // directional: -Y of the light (it travels along +Y), w = 1; point: position, w = 0
    float color[kMaxLights][4];  // rgb, range (point lights)
};

// OpenGL 3.3 renderer. 2D drawing uses a virtual 1280x720 screen (y down),
// letterboxed into the window; 3D drawing takes a full model-view-projection.
class Renderer {
public:
    // Creates the GL context for `window` (created with SDL_WINDOW_OPENGL).
    bool init(SDL_Window* window, std::string& error);
    // GL_RENDERER of the context, e.g. "NVIDIA GeForce RTX 3060 Laptop GPU/PCIe/SSE2".
    const std::string& gpuName() const { return gpuName_; }
    void shutdown();

    void beginFrame(SDL_Color clear);
    // Gameplay scenery may use more of a wide window than the 16:9 menus and
    // HUD: the viewport widens to the window, up to `maxAspect`. Returns the
    // aspect used; endWide restores the 16:9 viewport.
    float beginWide(float maxAspect);
    void endWide();
    // Clips drawing to the centred 4:3 part of the viewport (what a PS2 screen
    // showed), until clipEnd.
    void clip43();
    void clipEnd();
    void endFrame();

    // Returns a texture handle (0 = none).
    int createTexture(const Image& image, bool repeat = false);
    // Replaces a texture's pixels (same or new size; no mipmaps), for movies.
    void updateTexture(int texture, const Image& image);

    // Immediate drawing. `mvp` is column-major; nullptr = the 2D screen projection.
    void draw(const Vertex* v, size_t count, int texture, Blend blend, const float* mvp = nullptr,
              bool depthTest = false, bool depthWrite = false);
    void clearDepth();
    // The PS2 depth-of-field pass (FUN_0019afa8): pixels at or beyond window
    // depth `depth` get a 4-tap box blur (taps 0 and +tapPx in x and y).
    void depthBlur(float depth, float tapPx);
    bool ensureBlurProgram();  // builds the depth-of-field program on first use
    int viewportHeight() const { return viewport_[3]; }
    int viewportWidth() const { return viewport_[2]; }
    // Window depth (0..1) at viewport pixel (x, y from the bottom), or 1.
    float readDepth(int x, int y);
    // Depth samples read back without stalling the GPU: readDepthsAsync copies
    // the points' depths into the pixel-pack buffer `buffer` (created / grown
    // as needed); mapDepths reads them once the GPU is done (a few frames on).
    void readDepthsAsync(unsigned& buffer, size_t& capacity, const std::vector<std::array<int, 2>>& points);
    std::vector<float> mapDepths(unsigned buffer, size_t count);

    // Track fade (docs/track_fade_plan.md): copies the framebuffer under the
    // NDC rect (x0, y0)-(x1, y1) of the current viewport into grab texture
    // `slot` (0..1), then lays it back with alpha 1 at the top falling to 0 at
    // the bottom, and adds `add` (0..1) to the same rect.
    void grabNdc(int slot, float x0, float y0, float x1, float y1);
    void drawGrab(int slot, float add);
    // Enhanced Settings "Sharper Text & Sprites": menu textures created from now on are upscaled.
    void setUiEnhance(bool on) { uiEnhance_ = on; }
    // Enhanced Settings: the internal resolution's height (0 = the window's;
    // frames render off screen at that height, the window's aspect, and are
    // scaled to the window) and the frame-rate cap.
    void setInternalHeight(int h) { internalH_ = h; }
    void setFrameCap(int fps);
    bool uiEnhance() const { return uiEnhance_; }
    // A window position (SDL window coordinates) in the menus' 1280 x 720 units.
    void windowToMenu(float x, float y, float& mx, float& my) const;
    // 2D helpers in virtual screen coordinates.
    void fillRect(float x, float y, float w, float h, SDL_Color c);
    void image(int texture, float x, float y, float w, float h, SDL_Color tint = {255, 255, 255, 255},
               Blend blend = Blend::Alpha);
    void text(float x, float y, float scale, SDL_Color c, const std::string& s);
    void textCentered(float cx, float y, float scale, SDL_Color c, const std::string& s);

    // Static GPU meshes (indexed triangles). Returns a mesh handle (0 = none).
    int createMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    // Replaces a mesh's contents (for text that changes).
    void updateMesh(int mesh, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    struct RenderState {
        int texture = 0;
        Blend blend = Blend::Opaque;
        bool depthTest = true, depthWrite = true, cull = false, alphaCutout = false, decal = false;
        bool mirrored = false;  // the model matrix flips handedness: cull the other face
        float uvOffset[2] = {0, 0};
        float tint[4] = {1, 1, 1, 1};  // multiplies the vertex color
        const Lighting* lighting = nullptr;  // unlit when null
        const float* model = nullptr;        // world matrix, required when lit
        const float* bones = nullptr;        // skinned: 4 column-major bone matrices
        bool useEnviron = true, vertAmb = false, vertDyn = false;  // RndMat lighting flags
        float colorScale = 1;                // 2 for "Multiply2" texture stages
        bool fog = false;                    // the selected environ's fog (lit or not)
        float fogStart = 0, fogEnd = 0, fogColor[3] = {0, 0, 0};
        // Texgen computed per vertex (5 Environ, 2 Sphere; docs/material_stages.md §4),
        // which needs `model` and the camera.
        int texgen = 0;
        float texU[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, texO[2] = {0.5f, 0.5f};
        float eye[3] = {0, 0, 0}, camRight[3] = {1, 0, 0}, camUp[3] = {0, 0, 1};
        int texDecal = 0;  // TFX DECAL: 1 the texture alone, 2 texture colour with the fragment alpha (pass 0)
    };
    void drawMesh(int mesh, const float* mvp, const RenderState& state);

    // Off-screen colour + depth targets. createRenderTarget returns a texture
    // handle usable in RenderState::texture (0 on failure); between begin and
    // end, draws go to the target (cleared to transparent).
    int createRenderTarget(int width, int height);
    void beginRenderTarget(int texture);
    void endRenderTarget();

    // Enhanced Settings post-processing (render/post_fx.h): anti-aliasing,
    // FSR 1, bloom, cinematic colour, ground fog. Between beginScene and
    // endScene (inside beginWide / endWide) the 3D game frame draws off
    // screen at the chain's render scale; endScene runs the effects and lays
    // the result into the viewport. beginScene returns false (and draws go
    // straight to the frame as before) when every effect is off, or when the
    // effects' shaders do not compile on this driver.
    void setPost(const PostSettings& s) { post_ = s; }
    // What the AI upscaler is doing, or why it fell back (Enhanced Settings shows it).
    std::string upscalerStatus() const;
    const PostSettings& post() const { return post_; }
    bool beginScene();
    void endScene(const PostFrame& frame);

    // Reads back the current frame as RGBA (for screenshots).
    Image readPixels();

    // --shadertest: compiles every GL shader program now (most are built on
    // first use) and returns the names of those that failed; `programs` is how
    // many were tried. The main program is already built by init().
    std::vector<std::string> compileAllShaders(int& programs);

private:
    std::string gpuName_;
    void createFontTexture();
    void setJitter(float x, float y);
    PostSettings post_;
    PostFx postFx_;
    struct SceneTarget {
        unsigned fbo = 0, color = 0, depth = 0;
        int w = 0, h = 0;
    } scene_;
    bool sceneActive_ = false;
    int sceneSaved_[4] = {0, 0, 0, 0};
    std::array<float, 2> sceneJitter_{0, 0};
    int uJitter_ = -1;
    void applyState(int texture, Blend blend, bool depthTest, bool depthWrite, const float* mvp);

    struct GpuMesh { unsigned vao = 0, vbo = 0, ibo = 0; int count = 0; };
    std::vector<GpuMesh> meshes_{GpuMesh{}};
    int uCutout_ = -1, uUvOffset_ = -1, uTint_ = -1;
    int uSkin_ = -1, uBones_ = -1, uMatFlags_ = -1, uColorScale_ = -1, uFog_ = -1, uFogColor_ = -1;
    int uLit_ = -1, uModel_ = -1, uAmbient_ = -1, uLightCount_ = -1, uLightDir_ = -1, uLightColor_ = -1;

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    unsigned program_ = 0, vao_ = 0, vbo_ = 0;
    int uMvp_ = -1, uTex_ = -1, uUseTex_ = -1;
    int uTexgen_ = -1, uTexU_ = -1, uTexO_ = -1, uEye_ = -1, uCamRight_ = -1, uCamUp_ = -1, uTexDecal_ = -1;
    unsigned white_ = 0;
    std::vector<unsigned> textures_{0};
    int font_ = 0;
    int viewport_[4] = {0, 0, 1280, 720};
    int saved_[4] = {0, 0, 1280, 720};
    void* fences_[2] = {nullptr, nullptr};  // GLsync of the last two frames
    uint64_t paceNs_ = 0, nextFrameNs_ = 0;
    bool uiEnhance_ = false;
    int internalH_ = 0;
    struct MainTarget {
        unsigned fbo = 0, color = 0, depth = 0;
        int w = 0, h = 0;
    } main_;
    bool mainBound_ = false;
    void frameSize(int& w, int& h) const;  // the drawn frame's pixels (window or internal)
    int internalHeight(int windowH) const;  // the internal target's height, 0 for none
    void bindMain();                        // the frame's framebuffer (main_ or the window's)
    struct Grab {
        int texture = 0, w = 0, h = 0;
        int px = 0, py = 0;           // window pixels, GL bottom-up
        float vx0, vy0, vx1, vy1;     // the same rect in 1280 x 720 units
        bool valid = false;
    } grabs_[2];  // frame pacing when there is no vsync

    unsigned blurProgram_ = 0, blurTex_ = 0;
    int blurSize_[2] = {0, 0};
    int uBlurTex_ = -1, uBlurDepth_ = -1, uBlurTap_ = -1, uBlurSize_ = -1, uBlurOrigin_ = -1;
    struct RenderTarget { unsigned fbo = 0; int width = 0, height = 0; };
    std::map<int, RenderTarget> targets_;  // by texture handle
    float screenMvp_[16];
};
