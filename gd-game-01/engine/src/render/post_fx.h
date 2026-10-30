// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// [I] The port's post-processing of the 3D game frame (Enhanced Settings;
// not in the original game). The venue draws into an off-screen colour +
// depth target at the chain's render scale (Renderer::beginScene); endScene
// runs, in order:
//   1. the blur pyramid (dual Kawase, 1/2 .. 1/32), when bloom is on;
//      ambient occlusion (from the depth buffer);
//   2. one composite pass at render resolution: ambient occlusion, bloom,
//      cinematic colour, colour grading;
//   2b. Blur on Star Power: camera motion blur plus frame accumulation, only
//      while a player's Star Power is on;
//   3. anti-aliasing at render resolution: TAA (jittered projection, history
//      reprojected through the depth buffer, neighbourhood clamp) or FXAA;
//   4. the resample to the viewport: SSAA's 2x2 downsample, FSR 1 (EASU
//      upscale + RCAS sharpen) when the render scale is below 1, or a copy;
//   5. at the output size: sharpening (CAS), chromatic aberration, film grain.
// HDR (Enhanced > HDR; universal_mechanisms.md 1.2.2): with Emulated or True
// HDR the scene target and the blur pyramid are RGBA16F, so lights, additive
// effects and bloom keep values above 1.0; an auto-exposure pass (the mean
// log luminance of the scene, mip-reduced on the GPU and smoothed over time)
// and a filmic tone map (ACES, Narkowicz's fit) then run in step 2, after
// ambient occlusion and bloom and before cinematic colour and grading, so
// every later step works on a display-referred image. Emulated tone maps to
// SDR; True HDR keeps highlights up to the display's headroom (hdrPeak, in
// paper whites), and the steps after the composite run on RGBA16F targets.
// With HDR None nothing of this runs: the chain is the SDR one, unchanged.
// With a temporal AI upscaler (FSR 3.1, DLSS 4; render/vk_upscaler.h) the
// scene is jittered every frame and steps 3-4 are the upscaler (it does the
// anti-aliasing too); if it cannot run, FSR 1 takes its place.
// The highway, HUD and menus are drawn afterwards, untouched.
struct PostSettings {
    enum class AA { None, Fxaa, Ssaa, Taa };
    enum class Upscaler { None, Fsr1, Fsr31, Dlss4 };
    AA aa = AA::None;
    Upscaler upscaler = Upscaler::None;
    float fsrScale = 1;     // render scale with an upscaler: 0.5 / 0.59 / 0.67
    int quality = 2;        // the upscaler's mode: 0 performance, 1 balanced, 2 quality
    int bloom = 0;          // 0 off, 1 low, 2 medium, 3 high
    bool cinematic = false; // filmic curve, grade and vignette
    // Colour grading after the cinematic curve: 0 off, 1 warm, 2 cold, 3 vintage, 4 noir, 5 neon.
    // 6 sepia (Enhanced > Modes > Good old days only).
    int grade = 0;
    int grain = 0;          // film grain: 0 none, 1 light, 2 medium, 3 strong, 4 stronger
    int chromatic = 0;      // chromatic aberration (lens fringes towards the edges): 0 none, 1 light, 2 medium, 3 strong
    int ssao = 0;           // screen-space ambient occlusion: 0 none, 1 light, 2 medium, 3 strong, 4 stronger
    int sharpen = 0;        // contrast-adaptive sharpening: 0 off, 1 low, 2 high
    int starBlur = 0;       // motion blur during Star Power: 0 none, 1 light, 2 medium, 3 strong, 4 stronger
    int hdr = 0;            // 0 none (the original look), 1 emulated (tone mapped to SDR), 2 HDR output
    // True HDR: the display's headroom over paper white (1 = SDR: tone mapped as Emulated). Set by the
    // Renderer from the window's HDR state.
    float hdrPeak = 1;
    bool temporalUpscaler() const { return upscaler == Upscaler::Fsr31 || upscaler == Upscaler::Dlss4; }
    // The scene's size relative to the viewport (SSAA does not combine with a temporal upscaler).
    float renderScale() const {
        if (temporalUpscaler()) return fsrScale;
        return (upscaler == Upscaler::Fsr1 ? fsrScale : 1.0f) * (aa == AA::Ssaa ? 2.0f : 1.0f);
    }
    bool any() const {
        return aa != AA::None || upscaler != Upscaler::None || bloom || cinematic || grade || grain || chromatic || ssao ||
               sharpen || starBlur || hdr;
    }
};

// What the frame's effects need from the venue.
struct PostFrame {
    std::array<float, 16> viewProj{};  // column-major, without the TAA jitter
    bool hasCamera = false;
    float timeSec = 0;
    // Blur on Star Power: how much of it this frame (0 off .. 1 full; the caller fades it in and out).
    float starBlur = 0;
};

class VkUpscaler;

class PostFx {
public:
    PostFx();
    ~PostFx();
    // Runs the chain on the scene (colour + depth textures, sw x sh) for an
    // ow x oh output; returns the texture holding the result (0 on failure).
    unsigned process(unsigned color, unsigned depth, int sw, int sh, int ow, int oh, const PostSettings& s, const PostFrame& f);
    // Draws `texture` over the current viewport (no blending, no depth).
    void blit(unsigned texture);
    // TAA or a temporal upscaler: this frame's sub-pixel jitter in NDC for an
    // sw x sh scene (0 otherwise).
    std::array<float, 2> jitter(const PostSettings& s, int sw, int sh);
    // What the AI upscaler is doing, or why it fell back to FSR 1 ("" when none is selected).
    std::string upscalerStatus() const { return upscalerStatus_; }
    // FSR 3.1 / DLSS: the upscaler's GPU time per frame (0 when none runs).
    float upscalerMs() const;
    // Compiles every shader of the chain now (normally on first use); false when
    // any failed (then the chain stays off and failedPrograms() names them).
    bool ready() { return init(); }
    static constexpr int kPrograms = 17;  // built by init()
    const std::vector<std::string>& failedPrograms() const { return failedPrograms_; }

private:
    struct Target {
        unsigned fbo = 0, tex = 0;
        int w = 0, h = 0;
        bool hdr = false;
    };
    bool init();
    void ensure(Target& t, int w, int h, bool hdr);
    void bindTarget(const Target& t);
    unsigned program(const char* fragment, const char* name, bool hdr = false);
    void drawQuad();

    bool ready_ = false, failed_ = false;
    std::vector<std::string> failedPrograms_;
    unsigned vao_ = 0;
    unsigned pDown_ = 0, pUp_ = 0, pComposite_ = 0, pTaa_ = 0, pFxaa_ = 0, pDownsample_ = 0, pEasu_ = 0, pRcas_ = 0, pCopy_ = 0;
    unsigned pAo_ = 0, pStarBlur_ = 0, pFinish_ = 0, pLum_ = 0, pAdapt_ = 0;
    unsigned pCompositeHdr_ = 0, pFinishHdr_ = 0, pRcasHdr_ = 0;  // the HDR variants (GD_HDR 1)
    // HDR auto exposure: the scene's log luminance (mipmapped to 1x1) and the smoothed exposure (ping-pong 1x1).
    void autoExposure(unsigned color);
    Target lum_, expo_[2];
    int expoIndex_ = 0;
    bool expoValid_ = false;
    uint64_t expoNs_ = 0;
    // The output-size pass (sharpening, chromatic aberration, grain) on `cur`; returns its result.
    unsigned finish(unsigned cur, int ow, int oh, const PostSettings& s, const PostFrame& f);
    static constexpr int kLevels = 5;
    Target down_[kLevels], up_[kLevels];
    Target a_, b_, history_[2], out_, out2_;
    Target ao_, starAccum_[2], fin_;
    float peakEnc_ = 1;  // this frame's encoded white for the output-size passes (above 1 with True HDR)
    int starIndex_ = 0;
    bool starValid_ = false;
    std::array<float, 16> starPrevViewProj_{};
    int historyIndex_ = 0;
    bool historyValid_ = false;
    std::array<float, 16> prevViewProj_{};
    unsigned frame_ = 0;
    // The temporal AI upscalers (Vulkan, created on first use).
    std::unique_ptr<VkUpscaler> vk_;
    std::array<float, 2> lastJitter_{0, 0};
    std::array<float, 16> upPrevViewProj_{};
    float upLastTime_ = -1;
    bool upHistory_ = false;
    int upFailed_ = -1;  // the Upscaler that could not run (no jitter for its FSR 1 stand-in)
    std::string upscalerStatus_, loggedStatus_;
};
