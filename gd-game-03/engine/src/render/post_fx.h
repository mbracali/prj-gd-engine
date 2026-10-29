// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/device.h"

namespace scene {
class PostProc;
}

// GH2's post-process of the 3D world, as the game's DX renderer does it
// (DoPost 0x82303E68, called at the end of each frame from 0x823055E8;
// docs/renderer.md "Post-process"):
//
//   the world is drawn into the HDR buffer (2:10:10:10, colours / 4: the
//   materials' HDR key bit) with its depth;
//   DOF (when the camera shot asks): the buffer, unpacked, is box-filtered to
//   a low-resolution copy and blurred twice (7 taps across, 7 down);
//   bloom (when the PostProc's intensity > 0): a bright pass (per-channel
//   threshold, plus a "white" part from the luminance), two 2x downsamples
//   and a 15-tap Gaussian, down then across, at a quarter of the size;
//   one full-screen pass to the screen: unpack, then for world pixels only
//   (depth beyond the UI plane) DOF blend, bloom add, noise, colour
//   transform and the luminance map (a 1D tone curve).
//
// GH2 draws the track and HUD into the same buffer in a near depth range so
// that the final pass leaves them alone; gd03 draws them after end(), over
// the post-processed world (the same picture, except that GH2's bright
// gems also feed the world's bloom) [I].
namespace gfx {

class Renderer;

// RndPostProc v9 (Load 0x821EB3D0, field offsets from SyncProperty
// 0x821EB598, defaults from the constructor 0x821EAFB8): what a camera shot
// selects. The venue loader fills it from scene::PostProc (postProcState).
struct PostProcState {
    std::array<float, 4> bloomThreshold{4, 4, 4, 4};  // +0x28: r, g, b, white (unpacked HDR units)
    float bloomKernel = 1;      // +0x38 bloom_kernel_multiplier: the Gaussian's gain per pass
    float bloomScale = 1;       // +0x3c bloom_scale: the Gaussian's sigma, in quarter-size texels
    float bloomIntensity = 0;   // +0x40: bloom on when > 0
    TextureId luminanceMap;     // +0x44: 1D tone curve (luminance / 4 -> factor); none: off
    // +0x64..+0x9c: rows red / green / blue select (rgb) and the biases (w).
    std::array<float, 12> colorXfm{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};  // as stored: 3 selects, then 3 biases
    std::array<float, 2> noiseBaseScale{32, 24};  // +0xc8
    float noiseLayerScale = 1.3591f;              // +0xd0: the second noise layer's extra scale
    float noiseIntensity = 0;                     // +0xd4: noise on when != 0
    TextureId noiseMap;                           // +0xd8; none: the renderer's own noise
    // Not drawn (no shipped PostProc uses them, [C] all 28 have the values
    // that switch them off): +0xa4 flicker "intensity" / +0xac secs_range
    // (0x821EA128), +0xe4 threshold / +0xe8 duration (BLENDPREVIOUS trails,
    // on when threshold < 1 and duration > 0, 0x821EA228), COPYPREVIOUS.

    bool bloomOn() const { return bloomIntensity > 0; }
    bool colorXfmOn() const;
};

// RndPostProc from a decoded scene object: textures resolved by the caller
// (SceneCache::texture(&pp, pp.luminanceMap), empty when unnamed).
PostProcState postProcState(const scene::PostProc& pp, TextureId luminanceMap, TextureId noiseMap = {});

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

    // Starts drawing the world into the HDR buffer (sized to the picture),
    // cleared to `clear` (rgba 0..1, stored / 4) with its depth. Returns
    // false when post-processing is off (GD03_POST=0, or the driver cannot
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
    enum class Pass : uint8_t { Composite, Collapse, Bright, Downsample, Blur7, Blur15, Debug };
    static ProgramDesc describe(Pass pass, uint32_t key);
    ProgramId program(Pass pass, uint32_t key);
    bool ensureTargets(int w, int h);
    void destroyTargets();
    void quad(TargetId target, ProgramId p, const TextureId* tex, const SamplerState* samp, int slots, const void* block, size_t size);

    Renderer& renderer_;
    RenderDevice& device_;
    PostProcState state_;
    DofState dof_;
    bool disabled_ = false;  // GD03_POST=0, or the buffers failed
    int debug_ = 0;          // GD03_POST_DEBUG: 1 bloom, 2 dof, 3 lum, 4 doflow
    bool capturing_ = false;
    int w_ = 0, h_ = 0;
    TargetId hdr_, dofA_, dofB_, bloomHalf_, bloomA_, bloomB_;
    bool hdr10_ = true;
    BufferId triangle_;
    TextureId noise_;
    uint32_t seed_ = 0x2545f491u;
    std::unordered_map<uint32_t, ProgramId> programs_;  // (pass << 24) ^ key
};

}  // namespace gfx
