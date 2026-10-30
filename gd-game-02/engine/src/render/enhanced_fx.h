// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "render/device.h"
#include "render/rmath.h"

// [I] The port's post-processing of the 3D world (Enhanced Options; not in
// the original game), gd01's chain (gd-game-01/engine/src/render/post_fx.h)
// on gd02's device layer. GH2's own post-process (render/post_fx.h) runs
// first, into an 8-bit buffer at the render scale, with the world's depth;
// then, in order:
//   1. the blur pyramid (dual Kawase, 1/2 .. 1/32) when bloom is on;
//      ambient occlusion (from the depth buffer);
//   2. one composite pass: ambient occlusion, bloom, cinematic colour,
//      colour grading;
//   2b. Blur on Star Power: camera motion blur and frame accumulation, only
//      while a player's Star Power is on;
//   3. anti-aliasing at render resolution: TAA (jittered projection, history
//      reprojected through the depth buffer) or FXAA;
//   4. the resample to the picture: SSAA's 2x2 downsample, FSR 1 (EASU +
//      RCAS) below scale 1, or a copy;
//   5. at the picture's size: sharpening (CAS), chromatic aberration, grain.
// The track, HUD and menus are drawn afterwards, untouched. Any program that
// does not compile turns the chain off (logged once): the world is copied
// as GH2's pass left it (lesson L5).
namespace gfx {

struct PostSettings {
    enum class AA { None, Fxaa, Ssaa, Taa };
    // GH2's own bloom, depth of field, noise, colour transform and tone
    // curve (Remove All Effects turns them off with everything else).
    bool original = true;
    int bokeh = 2;          // GH2's depth of field: 0 none, 1 light, 2 medium (the original), 3 strong, 4 stronger
    AA aa = AA::None;
    bool fsr1 = false;      // AI Upscaling: FSR 1
    float fsrScale = 1;     // its render scale: 1 / 1.5, 1 / 1.7, 1 / 2
    int bloom = 0;          // 0 none, 1 original, 2 2x, 3 3x
    bool cinematic = false; // filmic curve, grade and vignette
    int grade = 0;          // 0 off, 1 warm, 2 cold, 3 vintage, 4 noir, 5 neon, 6 sepia
    int grain = 0;          // 0 none, 1 light, 2 medium, 3 strong, 4 stronger
    int chromatic = 0;      // 0 none, 1 light, 2 medium, 3 strong
    int ssao = 0;           // 0 none, 1 light, 2 medium, 3 strong, 4 stronger
    int sharpen = 0;        // 0 off, 1 low, 2 high
    int starBlur = 0;       // 0 none, 1 light, 2 medium, 3 strong, 4 stronger
    // The world's size against the picture (SSAA 2x, FSR 1 below 1).
    float renderScale() const { return (fsr1 ? fsrScale : 1.0f) * (aa == AA::Ssaa ? 2.0f : 1.0f); }
    // Whether the port's chain runs at all.
    bool any() const {
        return aa != AA::None || fsr1 || bloom || cinematic || grade || grain || chromatic || ssao || sharpen || starBlur;
    }
};

// What the frame's effects need from the venue.
struct PostFrame {
    Mat4 viewProj;           // the camera's, without the TAA jitter (GL clip depth)
    bool hasCamera = false;
    float timeSec = 0;
    float starBlur = 0;      // Blur on Star Power this frame (0 off .. 1 full; the caller fades it)
};

// The general 4x4 inverse (cofactors); identity when singular.
Mat4 inverse(const Mat4& m);

class EnhancedFx {
public:
    explicit EnhancedFx(RenderDevice& device);
    ~EnhancedFx();
    EnhancedFx(const EnhancedFx&) = delete;
    EnhancedFx& operator=(const EnhancedFx&) = delete;

    // Runs the chain on `color` + `depth` (sw x sh) and draws the result into
    // the window's viewport (x, y, ow, oh). False when the chain is off (a
    // program failed): the caller draws `color` itself.
    bool process(TextureId color, TextureId depth, int sw, int sh, int x, int y, int ow, int oh, const PostSettings& s, const PostFrame& f);
    // TAA: this frame's sub-pixel jitter in NDC for an sw x sh world (0 otherwise).
    std::array<float, 2> jitter(const PostSettings& s, int sw, int sh);
    // Every program of the chain (--shadertest).
    static std::vector<ProgramDesc> allPrograms();
    // Frames the chain ran, and the passes of the last one (the walk tests'
    // checks: an effect switched on runs its pass).
    int frames() const { return frames_; }
    bool failed() const { return failed_; }
    const std::vector<std::string>& lastPasses() const { return lastPasses_; }

private:
    enum class Pass : uint8_t { Down, Up, Ao, Composite, StarBlur, Taa, Fxaa, Downsample, Easu, Rcas, Finish, Copy, Count };
    static ProgramDesc describe(Pass p);
    ProgramId program(Pass p);
    struct Target {
        TargetId id;
        int w = 0, h = 0;
        bool hdr = false;
    };
    TextureId ensure(Target& t, int w, int h, bool hdr);
    void pass(Pass p, Target* dst, const TextureId* tex, int slots, const void* block, size_t size, const char* name);

    RenderDevice& device_;
    bool failed_ = false;
    BufferId triangle_;
    ProgramId programs_[int(Pass::Count)];
    bool tried_[int(Pass::Count)] = {};
    static constexpr int kLevels = 5;
    Target down_[kLevels], up_[kLevels], a_, b_, ao_, star_[2], history_[2], out_, out2_;
    int starIndex_ = 0, historyIndex_ = 0;
    bool starValid_ = false, historyValid_ = false;
    Mat4 starPrev_, taaPrev_;
    unsigned frame_ = 0;
    int frames_ = 0;
    std::vector<std::string> lastPasses_;
};

}  // namespace gfx
