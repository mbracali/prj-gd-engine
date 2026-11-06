// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// The Enhanced post-processing chain (post_fx.h) and the True HDR output
// pass (hdr_output.h) on gd-engine-core (gd::core::render::Device): what
// CoreRenderer runs when Enhanced > Graphics backend is a core backend.
// PostFx (post_fx.cpp) stays local's own GL code, untouched; this class is
// its twin, pass for pass: the same 17 programs (their bodies are the same
// text, in the gd shader dialect), the same targets, formats, samplers and
// order, the same uniform values, so it draws local's pixels
// (gd-engine-core/docs/render.md section 6).
//
// Not here: the temporal AI upscalers (FSR 3.1, DLSS 4; vk_upscaler.h). They
// share GL textures with a Vulkan device (GL <-> Vulkan interop), which a
// core backend does not expose; with one selected the chain does what local
// does when they cannot run: FSR 1 in their place (logged, and shown in the
// Enhanced menu's upscaler status). They become native on the Vulkan backend.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gd/core/render/device.h"
#include "render/post_fx.h"

class CorePostFx {
public:
    using Texture = gd::core::render::Texture;
    explicit CorePostFx(gd::core::render::Device& dev) : dev_(dev) {}
    ~CorePostFx();
    // Compiles every program now (normally on first use); false when any failed
    // (the chain then stays off and failedPrograms() names them).
    bool ready() { return init(); }
    static constexpr int kPrograms = PostFx::kPrograms + 1;  // + the True HDR output pass
    const std::vector<std::string>& failedPrograms() const { return failedPrograms_; }
    // TAA: this frame's sub-pixel jitter in NDC for an sw x sh scene (0 otherwise).
    std::array<float, 2> jitter(const PostSettings& s, int sw, int sh);
    // Outside a pass: the chain on the scene (colour + depth, sw x sh) for an
    // ow x oh output; returns the texture holding the result (empty on failure).
    Texture process(Texture color, Texture depth, int sw, int sh, int ow, int oh, const PostSettings& s, const PostFrame& f);
    // Inside the caller's pass (viewport and scissor set): draws `texture`
    // over the viewport, no blending, no depth test, into a `format` target
    // (with a `depth` attachment, or Undefined) with `writeMask`.
    void blit(Texture texture, gd::core::render::Format format, uint8_t writeMask, gd::core::render::Format depth);
    // Outside a pass: True HDR's conversion of the frame (`src`, sRGB-encoded,
    // paper white at 1.0) for the window's surface into `dst` (w x h):
    // linear scRGB scaled to paper white `white`, or BT.2020 PQ. False when
    // its program did not compile (the caller then shows `src` unconverted).
    bool hdrOutput(Texture src, Texture dst, int w, int h, float white, bool pq);
    std::string upscalerStatus() const { return upscalerStatus_; }

private:
    struct Target {
        Texture tex;
        int w = 0, h = 0;
        bool hdr = false;
    };
    enum Program {
        kDown, kUp, kComposite, kCompositeHdr, kFinishHdr, kRcasHdr, kTaa, kFxaa, kDownsample, kEasu, kRcas, kCopy, kAo, kStarBlur,
        kFinish, kLum, kAdapt, kHdrOut, kProgramCount
    };
    bool init();
    void ensure(Target& t, int w, int h, bool hdr, bool mips = false);
    // Begins a pass that draws over all of `t` with `program` (nothing loaded: every pixel is written).
    void begin(const Target& t, Program program);
    void bind(int slot, Texture tex, gd::core::render::Sampler sampler);
    void finishPass();
    gd::core::render::Pipeline pipeline(Program program, gd::core::render::Format format, uint8_t writeMask,
                                        gd::core::render::Format depth = gd::core::render::Format::Undefined);
    Texture finish(Texture cur, int ow, int oh, const PostSettings& s, const PostFrame& f);
    void autoExposure(Texture color);

    gd::core::render::Device& dev_;
    bool ready_ = false, failed_ = false;
    std::vector<std::string> failedPrograms_;
    gd::core::render::Shader shaders_[kProgramCount];
    std::map<uint32_t, gd::core::render::Pipeline> pipelines_;
    // Local's texture parameters: LINEAR + CLAMP_TO_EDGE (targets, the scene's
    // colour), NEAREST + CLAMP_TO_EDGE (the scene's depth), the luminance
    // map's NEAREST_MIPMAP_NEAREST, and the HDR frame's LINEAR + REPEAT (its
    // wrap was never set).
    gd::core::render::Sampler linear_, nearest_, lumSampler_, repeat_;

    Target lum_, expo_[2];
    int expoIndex_ = 0;
    bool expoValid_ = false;
    uint64_t expoNs_ = 0;
    static constexpr int kLevels = 5;
    Target down_[kLevels], up_[kLevels];
    Target a_, b_, history_[2], out_, out2_;
    Target ao_, starAccum_[2], fin_;
    float peakEnc_ = 1;
    int starIndex_ = 0;
    bool starValid_ = false;
    std::array<float, 16> starPrevViewProj_{};
    int historyIndex_ = 0;
    bool historyValid_ = false;
    std::array<float, 16> prevViewProj_{};
    unsigned frame_ = 0;
    std::array<float, 2> lastJitter_{0, 0};
    std::string upscalerStatus_, loggedStatus_;
};
