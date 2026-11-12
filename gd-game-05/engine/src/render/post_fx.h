// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <functional>
#include <memory>

#include "render/device.h"
#include "render/enhanced_fx.h"

namespace scene {
class PostProc;
}

// Rock Band's post-process of the 3D world (NgPostProc::DoPost RB
// 0x827DA600, the shaders in ark/xbox_shaders "postprocess", "bloom",
// "downsample", "blur"; docs/renderer.md "Post-process"):
//
//   the world is drawn into the world buffer with its depth (the port keeps
//   GH2's colours / 4 in a 10-bit buffer; RB's frame is 8 bits a channel
//   with the bloom mask in alpha, so the composite clamps the world at 1
//   [I]);
//   DOF (when the camera shot asks): a low-resolution copy blurred twice,
//   blended by depth (the port keeps GH2's passes: RB's "DOF" variant
//   blends its low-resolution buffer the same way);
//   bloom (intensity > 0 and not (threshold >= 4 and bloom alpha <= 0),
//   0x827D94D0): each world pixel times its bloom mask
//   sat((bloom_color . rgb - threshold) / (4 - threshold)) (gBloomColor,
//   0x827D8C18; RB's scene shaders write the mask in alpha, the port works
//   it out from the colour [I]), two 2x downsamples, then a 15-tap blur
//   down then across (0x827D8970: taps at (i + 0.5) * bloom_size / 7 of
//   1/180 of the height / 1/320 of the width, weights exp(ln 0.1 * i^2 /
//   49), normalised [I]);
//   one full-screen pass to the screen (postprocess 0x16 / 0x1e): DOF
//   blend, + bloom * bloom_intensity, the film grain as an overlay blend
//   of two noise_map layers mix(c, overlay(c, n0 * n1), noise_intensity)
//   (0x827D9190), then the ColorXfm with its 3x3 scaled by the flicker
//   (0x827D93D8, 0x8218E948), clamped.
//
// Rnd also paces the world by the PostProc's emulate_fps (0x8218EF80 /
// 0x8218EEA0, Rnd 0x821876F0): ProFilm's 24 draws the world on two of
// every five 60 Hz frames (periods of 3 and 2 frames), showing the last
// world picture in between; the track and HUD go on at the full rate.
//
// GH2 / RB draw the track and HUD into the same buffer; gd05 draws them
// after end(), over the post-processed world [I].
namespace gfx {

class Renderer;

// RndPostProc rev 18 as NgPostProc uses it (fields: Load RB 0x82190F08,
// SyncProperty 0x8218FC00, defaults: the constructor 0x8218F3E8): what a
// camera shot selects. The venue loader fills it from scene::PostProc
// (postProcState).
struct PostProcState {
    std::array<float, 4> bloomColor{1, 1, 1, 0};  // +0x2c rgb, +0x38 bloom_alpha
    float bloomThreshold = 1;  // +0x3c (world colour units, 0..4)
    float bloomSize = 7;       // +0x40: the blur's reach (1/7 of it a tap)
    float bloomIntensity = 0;  // +0x44: the blurred bloom's gain
    TextureId luminanceMap;    // +0x48: 1D tone curve (luminance / 4 -> factor); none: off
    // +0xa8: the ColorXfm's matrix: rows red / green / blue select (rgb) and the biases (w).
    std::array<float, 12> colorXfm{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};  // as stored: 3 selects, then 3 biases
    std::array<float, 2> flickerIntensity{0, 0};  // +0xf4: the flicker's depth range (the ColorXfm x 1 - depth)
    std::array<float, 2> flickerSecs{0.001f, 0.007f};  // +0xfc: seconds between flicker targets
    std::array<float, 2> noiseBaseScale{32, 24};  // +0x118
    float noiseTopScale = 1.3591f;                // +0x120: the second noise layer's extra scale
    float noiseIntensity = 0;                     // +0x124: the grain's overlay weight
    bool noiseStationary = false;                 // +0x128: a fixed phase
    TextureId noiseMap;                           // +0x12c; the grain is off without one
    float emulateFps = 30;                        // +0x150: the world's frame rate (0: every frame)
    // Not drawn (no gameplay shot's PostProc uses them): posterize
    // (num_levels / min_intensity), kaleidoscope (mirror), trails
    // (threshold / duration), distortion.

    bool bloomOn() const { return bloomIntensity > 0 && !(bloomThreshold >= 4 && bloomColor[3] <= 0); }
    bool colorXfmOn() const;
    bool noiseOn() const { return noiseIntensity != 0 && noiseMap; }
};

// RndPostProc from a decoded scene object: textures resolved by the caller
// (SceneCache::texture(&pp, pp.luminanceMap), empty when unnamed).
// `texture` resolves a map name (empty: none) to the venue's texture.
PostProcState postProcState(const scene::PostProc& pp, const std::function<TextureId(const std::string&)>& texture);
// PostProcBlendTask (RB 0x8220A508) [C]: `from` towards `to` by t (0..1);
// the maps are the destination's from the start.
PostProcState blendPostProc(const PostProcState& from, const PostProcState& to, float t);

// Depth of field, set per frame by the camera director. GH2 [C]: a CamShot
// with focal targets calls Rnd::SetDOF (vtable +0x70, DX 0x82302728) with
//   focus distance = |camera - focal target| * (1 + focus_blur_multiplier),
//   blur_depth and max_blur, each lerped between the two keyframes (when
//   only the outgoing keyframe has a focal target, max_blur fades to 0)
// (CamShot keyframe apply 0x82227440); without focal targets it turns DOF
// off (vtable +0x74).
struct DofState {
    bool on = false;
    float focusDistance = 0;  // world units
    float blurDepth = 0.5f;   // the blur reaches 1 at focus * (1 - blur_depth) (and as far behind, in depth)
    float maxBlur = 0;        // the most the low-resolution blur is blended in (0..1); DOF on when > 0
    float nearPlane = 1, farPlane = 1000;  // the shot's camera
    static DofState fromShot(float focusDistance, float blurDepth, float maxBlur, float nearPlane, float farPlane);
};

// The post-process program keys (the game's postprocess.fx switches reuse
// the standard key's low bits; render/standard_shader.h): built by
// PostFx::compositeKey and named postprocess_<hex> like the disc's cache.
namespace postkey {
constexpr uint32_t ColorXfm = 1u << 1;
constexpr uint32_t Noise = 1u << 2;
constexpr uint32_t Dof = 1u << 3;
constexpr uint32_t Bloom = 1u << 4;
constexpr uint32_t BlendPrevious = 1u << 5;  // not built (see PostProcState)
constexpr uint32_t CopyPrevious = 1u << 6;   // not built
constexpr int NumTapsShift = 14;             // blur.fx: taps - 1
constexpr uint32_t LumMap = 1u << 20;
constexpr uint32_t HdrOut = 1u << 21;  // the port's: Enhanced > HDR, no clamp at 1.0 (not GH2's)
constexpr uint32_t All = ColorXfm | Noise | Dof | Bloom | LumMap;
}  // namespace postkey

class PostFx {
public:
    explicit PostFx(Renderer& renderer);
    ~PostFx();

    // The selected PostProc (RndPostProc::Select, 0x821EA0F8: a shot's
    // `postprocess` flag), and `rnd reset_postproc` (0x821E5088: back to
    // the default PostProc, no bloom / luminance map / colour change).
    void select(const PostProcState& s) { state_ = s; }
    void reset() { state_ = PostProcState{}; }
    const PostProcState& selected() const { return state_; }
    // The frame's depth of field (cleared with clearDof: GH2's DOF off).
    void setDof(const DofState& d) { dof_ = d; }
    void clearDof() { dof_ = {}; }

    // The Enhanced Options' effects (render/enhanced_fx.h) and what this
    // frame's need from the venue (set before end()).
    void setSettings(const PostSettings& s) { settings_ = s; }
    const PostSettings& settings() const { return settings_; }
    void setFrame(const PostFrame& f) { frame_ = f; }
    // TAA's sub-pixel jitter for this frame's world (NDC; 0 when off).
    std::array<float, 2> jitter() { return capturing_ && enhanced_ && settings_.any() ? enhanced_->jitter(settings_, w_, h_) : std::array<float, 2>{0, 0}; }
    // The port's chain (tests: frames run, the last frame's passes); nullptr before the first.
    const EnhancedFx* enhanced() const { return enhanced_.get(); }
    // The composite program this frame used (tests: which of GH2's effects ran).
    uint32_t lastKey() const { return lastKey_; }
    // Tests: worlds drawn (end) and frames that showed the last one again (hold).
    int worldDraws() const { return worldDraws_; }
    int worldHolds() const { return worldHolds_; }
    bool lastDof() const { return lastDof_; }

    // Rnd's world pacing by the selected PostProc's emulate_fps: true on the
    // frames that show the last world picture again (drawn by this call into
    // the picture, as end() would); the caller then draws no world. Call
    // once a frame before begin().
    bool hold();

    // Starts drawing the world into the HDR buffer (sized to the picture),
    // cleared to `clear` (rgba 0..1, stored / 4) with its depth. Returns
    // false when post-processing is off (GD05_POST=0, or the driver cannot
    // make the buffers): draw the world to the screen as before then.
    bool begin(const float clear[4]);
    // True between begin and end: draw materials with DrawContext::hdr.
    bool hdr() const { return capturing_; }
    // While capturing: the HDR buffer (to bind it again after drawing into
    // another target) and its depth (render/spotlight_view.h's flare test).
    TargetId worldTarget() const { return capturing_ ? hdr_ : TargetId{}; }
    TextureId worldDepth() const { return capturing_ ? device_.targetDepth(hdr_) : TextureId{}; }
    // The HDR buffer's size (the viewport for MaterialRenderer::setFrame).
    void size(int& w, int& h) const { w = w_, h = h_; }
    // Runs the passes and draws the result into the picture; the window and
    // the picture's viewport are bound again for the track, HUD and 2D.
    void end();

    // Every program this can build (--shadertest).
    static std::vector<ProgramDesc> allPrograms();
    // The composite program's key for a PostProc and DOF state.
    static uint32_t compositeKey(const PostProcState& s, bool dof);
    static std::string keyName(uint32_t key);

private:
    enum class Pass : uint8_t { Composite, Collapse, Bright, Downsample, Blur7, Blur15, Debug, Copy };
    static ProgramDesc describe(Pass pass, uint32_t key);
    ProgramId program(Pass pass, uint32_t key);
    bool ensureTargets(int w, int h);
    void destroyTargets();
    void quad(TargetId target, ProgramId p, const TextureId* tex, const SamplerState* samp, int slots, const void* block, size_t size);
    void updateFlicker(float dt);
    void present(int x, int y, int pw, int ph);  // the held picture (ldr_) into the picture rect

    Renderer& renderer_;
    RenderDevice& device_;
    PostProcState state_;
    DofState dof_;
    bool disabled_ = false;  // GD05_POST=0, or the buffers failed
    int debug_ = 0;          // GD05_POST_DEBUG: 1 bloom, 2 dof, 3 lum, 4 doflow
    bool capturing_ = false;
    int w_ = 0, h_ = 0;
    TargetId hdr_, dofA_, dofB_, bloomHalf_, bloomA_, bloomB_;
    TargetId ldr_;  // the composite's result when the port's chain follows, or when the world is paced
    bool ldrF16_ = false, worldF16_ = false;  // Enhanced > HDR: RGBA16F
    PostSettings settings_;
    PostFrame frame_;
    std::unique_ptr<EnhancedFx> enhanced_;
    uint32_t lastKey_ = 0;
    bool lastDof_ = false;
    bool hdr10_ = true;
    BufferId triangle_;
    TextureId noise_;
    uint32_t seed_ = 0x2545f491u;
    float random();  // 0..1
    // The flicker (RB 0x8218E948): the ColorXfm's factor, eased from the
    // last target to the next over a random period.
    float flicker_ = 1, flickerFrom_ = 0, flickerTo_ = 0, flickerT_ = 0, flickerPeriod_ = 0;
    uint64_t lastNs_ = 0;
    std::array<float, 2> stationaryPhase_{0, 0};  // NgPostProc +0x168 (random at construction)
    // The pacing (RB 0x8218EF80): 60 Hz ticks, the period's tick, its
    // length and the alternating remainder.
    uint64_t tick_ = 0, tickStartNs_ = 0;
    int paceFps_ = 0, paceStep_ = 0, paceLen_ = 0, paceRem_ = 0;
    int worldDraws_ = 0, worldHolds_ = 0;
    bool held_ = false;     // ldr_ holds the last world picture
    bool paced_ = false;    // this frame's world goes through ldr_
    std::unordered_map<uint32_t, ProgramId> programs_;  // (pass << 24) ^ key
};

}  // namespace gfx
