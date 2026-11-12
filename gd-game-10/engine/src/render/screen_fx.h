// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>

#include "data/matlib.h"
#include "render/device.h"
#include "render/gh3_shader.h"

class Globals;
namespace qb {
struct Struct;
}

// GH3's screen effects on the venue viewport (bg_viewport): the ScreenFX
// instances the scripts add (TOD_Proxim_Update_LightFX_Viewport:
// Default_TOD_Manager's bright_sat + simple_bloom, the cameras' venue_DOF,
// ScreenFlash's FlashBS) run with the engine's own filter passes from the
// material library (ImmediateMode / ImmediateModeAlt / ImmediateModeFilter
// techniques, translated like every other GH3 shader) over the venue and
// band drawn into an off-screen colour + depth target; the result is
// composited where the venue would have drawn. The highway and HUD (the
// UI viewport) are drawn afterwards, untouched. docs/venues.md "Screen
// effects".
namespace screenfx {

struct Bloom {  // simple_bloom (Function_8241BC20 parses, Function_8243D240 draws) [C]
    bool on = false;
    float cutoff[3] = {0, 0, 0};    // color / 255: m_sCutoffColor
    float strength[3] = {1, 1, 1};  // strength / 255
    float bloom1 = 0, bloom2 = 0;   // the two blooms' amounts
    float falloff1 = 0, falloff2 = 0;
    bool materialFilter = false, autoLuminance = false;
    float distanceMod = 0;
    // gh4's bloom2 (parsed by 825074F8 [C: the keys]): `cutoff` holds
    // subtract, `strength` colormodulate (x its fourth value), drawn through
    // _Bloom_SubtractModulate(Stencil) and the uber pass (docs/render.md).
    bool gh4 = false;
    float saturation = 1, thickness = 0;
};

struct Dof {  // dof (Function_8241EE68 parses, Function_824440F8 draws) [C params; the chain: screen_fx.cpp]
    bool on = false;
    float backDist = 1, frontDist = 0, strength = 0, blurRadius = 0;
    int resolution = 1;  // blurresolution: 0 half, 1 quarter, 2 sixteenth [C 8241F160]
    // gh4's dof3 (parsed by 82503388 [C: the key]): the far blur from
    // farblurbegin to farblurend in metres (a = strength x saturate((z -
    // begin) / (end - begin))), the near blur (nearblurbegin / end) not drawn [I].
    bool metres = false;
    // dof3's near blur: nearblurbegin..nearblurend in metres (g_DOF0.xy) [C: the uber shader; I: the constants].
    float nearBegin = 0, nearEnd = 0;
};

struct BrightSat {  // bright_sat
    bool on = false;
    float contrast = 1, brightness = 1, saturation = 1, hue = 1;
    float mix[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};

// gh4's noise2 (the uber pass's noise1: sys_noise tiled `uv` times, moved
// each frame, g_Color = color / intensity) [C data, shader; I: the constants].
struct Noise {
    bool on = false;
    float intensity = 0;
    float color[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    float uv[2] = {8, 6};
};

// gh4's vignette (vignette1: lerp to colour by the sys_vignettemask texture) [C shader; I: inner / outer / alpha].
struct Vignette {
    bool on = false, aspect = false;
    float inner = 0.5f, outer = 1, alpha = 0.5f;
    float color[4] = {0, 0, 0, 0};
};

// gh4's inkblotch (inkblot1: sys_inkblot's colour x scratchcolor added, its
// alpha x blotchcolor.a lerps to blotchcolor; the texture's placement moved
// every intervala..intervalb s, rotated rotationa..rotationb degrees) [C data, shader; I: the motion].
struct InkBlotch {
    bool on = false;
    float scratch[4] = {0, 0, 0, 0}, blotch[4] = {0, 0, 0, 0};
    float intervalA = 0.06f, intervalB = 0.09f, xTiling = 1, yTiling = 1, rotA = 0, rotB = 0;
};

struct Params {
    Bloom bloom;
    Dof dof;
    BrightSat brightSat[2];  // the TOD's BnS and global_brightness (ScreenFlash's FlashBS replaces [1]'s values while on)
    float nearZ = 0.05f, farZ = 4000.0f;  // the projection's clip planes (the depth buffer's)
    // GH3's camera far clip, DOF's backdist / frontdist scale: the render's
    // far plane (0x82c18acc, from the camera component's +0xe0: 823A8DB8),
    // far_clip of the camera's params, default 400 (822BF1E0, 0x8200fa1c);
    // PlayIGCCam's cameras (the songs' cuts, the menus') give none [C].
    float cameraFar = 400.0f;
    // gh4: an uberscreenfx entry composes bloom2, dof3, noise2, the first
    // bright_sat, vignette and inkblotch in one pass (Function_8251EC80) [C].
    bool uber = false;
    Noise noise;
    Vignette vignette;
    InkBlotch ink;
    bool godray = false;  // on in Metal_HotBlue: not drawn [open]
};

// One screen_FX entry (ScreenFX_AddFXInstance's params: Type dof /
// bright_sat / simple_bloom) into `p`; bright_sat fills brightSat[slot++].
void readEntry(const qb::Struct& e, Params& p, int& brightSlot);
// TOD_Proxim_Update_LightFX_Viewport on bg_viewport: venue_DOF from
// DOF_Off_TOD_Manager's screen_FX[0], then `manager`'s screen_FX
// (Default_TOD_Manager: BnS, sbloom) [C scripts tod_proxims.qb].
void readViewport(const Globals& g, uint32_t manager, Params& p);
// venue_DOF set to a DOF_*_tod_manager's screen_FX[0] (CameraCuts_StartCallback).
void readDof(const Globals& g, uint32_t manager, Params& p);
// ApplyScreenFx effect = <next_screenfx> (the songs' camera notes 81..127,
// CameraCuts_SetNextScreenFx): the TOD manager TOD_Proxim_Apply_<effect>
// gives bg_viewport, Default_TOD_Manager for none / default [C scripts
// guitar_cameras.qb, tod_proxims.qb].
uint32_t effectManager(const Globals& g, uint32_t effect);
// The viewport's params for a shot: its effect's manager (cached), then its dof.
void readShot(const Globals& g, uint32_t effect, uint32_t dof, Params& p);
// The camera notes' scripts on the shot's params [C scripts misc_fx.qb,
// bg_screenfx.qb]: CameraBlur_FastZoom_InandOut / SlowZoom_InandOut /
// FocusIn / FocusOut (BG_fx_set_blur: dof3 strength with every blur
// distance 0, back to the start values), CameraFX_BlackFadeIn (brightness to
// 0 over the note's length) / BlackFadeOut (0, then back over 0.75 s) on the
// TOD's Brightness___Saturation, and CameraFX_WhiteFlash (brightness 100,
// contrast 0, saturation 0.5348 for 0.06 s, back over 0.06 s). `sinceMs`:
// song ms since the script ran (< 0: none); `flashSinceMs` likewise for the
// white flash. ScreenFX_UpdateFXInstanceParams' `time` is taken as a linear
// blend from the values then [I].
void cameraNoteFx(uint32_t script, double sinceMs, double lengthMs, double flashSinceMs, Params& p);

class ScreenFx {
public:
    ~ScreenFx() { shutdown(); }
    bool init(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib);
    // Its passes' programs compiled (init only asks for them: no-stutter
    // rule); true once they are all ready, polled a frame at a time.
    bool settle();
    void shutdown();
    // Redirects drawing to the scene target (the current viewport's size);
    // false when the effects cannot run (draw straight to the target then).
    bool begin(float scale = 1.0f);
    // Runs the effects and composites into the target and viewport bound at begin().
    void end(const Params& p, gfx::TargetId into = {});
    // The scene target's depth and size, and the viewport begin() saw (Enhanced's chain).
    gfx::TextureId sceneDepth() const { return dev_ && scene_.id ? dev_->targetDepth(scene_.id) : gfx::TextureId{}; }
    // A copy of the scene's depth so far (the raytraced light beams sample
    // it while the scene target stays bound) [I: GH3 resolves its depth].
    gfx::TextureId depthSnapshot();
    int sceneW() const { return scene_.w; }
    int sceneH() const { return scene_.h; }
    gfx::TargetId outer() const { return outer_; }
    void outerViewport(int& x, int& y, int& w, int& h) const { x = ox_, y = oy_, w = ow_, h = oh_; }

private:
    struct Target {
        gfx::TargetId id;
        int w = 0, h = 0;
    };
    gfx::RenderDevice* dev_ = nullptr;
    gh3shader::Programs gh3_;
    gfx::BufferId quad_;
    gfx::ProgramId copy_;
    bool copyReady_ = false;  // its programStatus said so (settle)
    Target scene_, quarterA_, quarterB_, copyA_, copyB_, depthCopy_;
    Target sceneQ_, halves_[4];  // the scene's quarter, and the steps down to the effects' fixed size
    Target sixteenA_, sixteenB_;  // dof blurresolution sixteenth
    gfx::TargetId outer_;
    int ox_ = 0, oy_ = 0, ow_ = 0, oh_ = 0;
    uint32_t immediate_ = 0, immediateAlt_ = 0, filter_ = 0;

    enum class Blend : uint8_t { Replace, Add, Alpha };  // ONE,ZERO / SRC_ALPHA,ONE / SRC_ALPHA,ONE_MINUS_SRC_ALPHA
    bool ensure(Target& t, int w, int h, bool depth);
    // One full-screen pass into `dst` (null: the target and viewport begin() saw).
    void pass(const gh3shader::Program* prog, const Target* dst, gfx::TextureId s0, gfx::TextureId s1, const gh3shader::Globals& g,
              const float (*ps)[4], int psCount, Blend blend, bool s0Point = false, bool stencilEqual1 = false);
    void downsample4x(gfx::TextureId src, const Target& dst);
    // `src` (w x h) into `dst` by bilinear copies halving the size (2 x 2 means) [I].
    void resample(gfx::TextureId src, int w, int h, const Target& dst);
    // The scene colour (or a quarter-size pass's result in sceneQ_) into quarterA_.
    void toQuarter(gfx::TextureId sceneColor);
    void bloomBlur(const Target& t, const Target& tmp, float gain);
    // gh4's chain (p.uber): bloom2's and dof3's buffers, then _Uber_ScreenFX_<parts>.
    void endUber(const Params& p, gfx::TextureId& cur, const Target*& curT, bool quarters, bool copies);
    void passN(const gh3shader::Program* prog, const Target* dst, const gfx::TextureId* tex, int n, const gh3shader::Globals& g,
               const float (*vs)[4], int vsCount, const float (*ps)[4], int psCount, uint32_t pointMask);
    // sys_noise / sys_vignettemask / sys_inkblot (global_gfx.scn's materials), decoded on a worker.
    void startSysTextures();
    bool pollSysTextures();
    Target bloomA_, bloomB_;
    gfx::TextureId noiseTex_, vignetteTex_, inkTex_, black_;
    struct SysTextures;
    std::shared_ptr<SysTextures> sys_;
    bool sysDone_ = false;
    uint32_t frame_ = 0;
    float vignetteFor_[4] = {-1, -1, -1, -1};  // inner, outer, alpha, aspect of vignetteTex_
    bool sysVignette_ = false;                 // the disc's sys_vignettemask (else made from the params)
    void makeVignette(const Vignette& v);
    double inkNext_ = 0, inkClock_ = 0;
    float inkMat_[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}};
    void dofBlur(const Target& t, const Target& tmp, float radius);
};

}  // namespace screenfx
