// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>

#include "data/matlib.h"
#include "render/device.h"
#include "render/gh3_shader.h"

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
};

struct Dof {  // dof [C params; the chain: screen_fx.cpp]
    bool on = false;
    float backDist = 1, frontDist = 0, strength = 0, blurRadius = 0;
    bool half = false;  // blurresolution half (else quarter)
};

struct BrightSat {  // bright_sat
    bool on = false;
    float contrast = 1, brightness = 1, saturation = 1, hue = 1;
    float mix[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};

struct Params {
    Bloom bloom;
    Dof dof;
    BrightSat brightSat[2];  // the TOD's BnS and global_brightness (ScreenFlash's FlashBS replaces [1]'s values while on)
    float nearZ = 0.05f, farZ = 4000.0f;  // the projection's clip planes (the depth buffer's)
    float cameraFar = 500.0f;             // GH3's camera far clip (DOF's backdist / frontdist scale)
};

class ScreenFx {
public:
    ~ScreenFx() { shutdown(); }
    bool init(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib);
    void shutdown();
    // Redirects drawing to the scene target (the current viewport's size);
    // false when the effects cannot run (draw straight to the target then).
    bool begin();
    // Runs the effects and composites into the target and viewport bound at begin().
    void end(const Params& p);

private:
    struct Target {
        gfx::TargetId id;
        int w = 0, h = 0;
    };
    gfx::RenderDevice* dev_ = nullptr;
    gh3shader::Programs gh3_;
    gfx::BufferId quad_;
    gfx::ProgramId copy_;
    Target scene_, quarterA_, quarterB_, copyA_, copyB_;
    gfx::TargetId outer_;
    int ox_ = 0, oy_ = 0, ow_ = 0, oh_ = 0;
    uint32_t immediate_ = 0, immediateAlt_ = 0, filter_ = 0;

    enum class Blend : uint8_t { Replace, Add, Alpha };  // ONE,ZERO / SRC_ALPHA,ONE / SRC_ALPHA,ONE_MINUS_SRC_ALPHA
    bool ensure(Target& t, int w, int h, bool depth);
    // One full-screen pass into `dst` (null: the target and viewport begin() saw).
    void pass(const gh3shader::Program* prog, const Target* dst, gfx::TextureId s0, gfx::TextureId s1, const gh3shader::Globals& g,
              const float (*ps)[4], int psCount, Blend blend, bool s0Point = false, bool stencilEqual1 = false);
    void downsample4x(gfx::TextureId src, const Target& dst);
    void bloomBlur(const Target& t, const Target& tmp, float gain);
    void dofBlur(const Target& t, const Target& tmp, float radius);
};

}  // namespace screenfx
