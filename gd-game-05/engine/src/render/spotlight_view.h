// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/camera.h"
#include "render/device.h"
#include "render/scene_gpu.h"

class BandView;
class World;
namespace scene {
class Object;
}

namespace gfx {

// A SpotlightDrawer's parameters (rev 4, Load RB 0x82299660 into the
// drawer's +0x28 block) [C]: total (+0x28), base_intensity (+0x3c),
// smoke_intensity (+0x40), an unnamed f32 (+0x44: 250 in every venue, the
// volume passes' gFogRange.y), color (+0x2c); then a texture and the proxy
// (the stage fog / smoke group, +0x5c). False when the record is not rev 4.
struct DrawerParams {
    float total = 1, base = 1, smoke = 1, range = 250;
    std::array<float, 4> color{1, 1, 1, 1};
    std::string proxy;
};
bool readDrawerParams(const std::vector<unsigned char>& rec, DrawerParams& out);

// GH2's Spotlights and RndFlares (docs/venues.md "Spotlights"), drawn after
// the venue's draw list. Spotlight::DrawShowing (0x822328E8) queues a lit
// spotlight (intensity x colour > 0, showing) into the SpotlightDrawer
// (0x822312C8; the DX drawer, ctor 0x8225F778, draws the queue in
// 0x82260B40: beams 0x822601D0, then the rest). No SpotlightDrawer object
// is on the disc, so its defaults hold (0x8222F7B0). Per spotlight, with
// the preset's state (World::spots: intensity, colour, rotation, target,
// flare) over the object's own:
//
// - the light can (light_can mesh at the spotlight, light_can_offset along
//   the beam), lit by the lighting directory's Environ;
// - the floor spot (spot_material): a disc of radius spot_scale where the
//   beam meets the floor plane z = spot_height [I];
// - the beam (beam.material; every beam on the disc is a sheet, is_cone 0):
//   a camera-facing strip from `offset` to `offset + length` along the beam
//   (the light's -z [I: from the presets' rotations]), top_radius at the
//   light, bottom_radius at the end, its sides fading over
//   top_side_border .. bottom_side_border of the width and its end over
//   bottom_border of the length [I: the borders' meaning from their names],
//   tilted by angle_offset (degrees), colour x intensity x brighten, added;
// - the lens (lens_material, lens_size at lens_offset), brighter as the
//   beam faces the camera [I];
// - the flare (flare_material; RndFlare draw 0x8220D6A8 for both the
//   spotlight's flare and RndFlare objects): a screen-space square of
//   size x the picture's width (lerped from size.x to size.y as it faces
//   the camera), faded by distance over range (full at range.x, gone at
//   range.y; equal values: no fade), and by its visibility: when tested
//   (a spotlight's stored visibility_test byte is 0; RndFlares always),
//   the scene's depth at the flare decides, and the flare steps 1/steps
//   of the way towards visible or hidden each frame. The test samples the
//   HDR buffer's depth (render/post_fx.h) on the GPU into a small
//   ping-pong texture, which the flare pass reads (no readback).
class SpotlightView {
public:
    SpotlightView(World& world, SceneCache& cache);
    ~SpotlightView();
    SpotlightView(const SpotlightView&) = delete;
    SpotlightView& operator=(const SpotlightView&) = delete;

    // Draws into the bound target (`hdr`: the post-process's HDR buffer,
    // whose depth the flares test; `worldTarget` / `worldDepth` from
    // PostFx, empty without post-processing).
    void draw(MaterialRenderer& mr, const Camera& camera, int viewW, int viewH, bool hdr, TargetId worldTarget, TextureId worldDepth);

    // The band's characters, for target_shadow's shadows.
    void setBandView(BandView* v) { bandView_ = v; }
    int shadowsDrawn() const { return shadowsDrawn_; }
    int shadowsTotal() const { return shadowsTotal_; }  // since the venue's load

    int beamsDrawn() const { return beams_; }

    // Rock Band's beams (NgSpotlightDrawer, GD05-093, D91): the venue's
    // SpotlightDrawer and its proxy; VenueView draws the proxy into the
    // density map between beginDensity and endDensity (0x822A9E58: through
    // the drawer's camera, a copy of the shot's, into a quarter-size
    // texture), then draw() runs the volume passes (0x822ABC28) into the
    // half-size beam buffer and fog() hands PostFx the composite's inputs
    // (0x822A8E78 / 0x822AB5B8). Off without post-processing or with
    // GD05_BEAMS_LEGACY=1 (GH2's beam sheets, the port before).
    void setDrawer(const scene::Object* drawer, const scene::Object* proxy);
    const scene::Object* drawerProxy() const { return proxy_; }
    bool rbBeams() const;
    bool beginDensity(int viewW, int viewH);
    void endDensity(TargetId worldTarget, int viewW, int viewH);
    struct Fog {
        bool on = false;
        TextureId beams, density;
        float c82x = 0, c82y = 0, c91x = 0;  // gFogDensity.xy, gFogRange.x
    };
    const Fog& fog() const { return fog_; }
    int rbBeamsDrawn() const { return rbBeamsDrawn_; }
    // GD05_BEAM_PROBE=1: each frame's beam buffer and density map read back:
    // the pixels the composite adds at least 1/255 to (-1: not measured).
    long long beamPixels() const { return beamPixels_; }
    int flaresDrawn() const { return flares_; }
    int spotsDrawn() const { return spots_; }

    // The programs this draws with (--shadertest).
    static std::vector<ProgramDesc> allPrograms();

private:
    BandView* bandView_ = nullptr;
    int shadowsDrawn_ = 0, shadowsTotal_ = 0;
    std::unordered_map<const scene::Spotlight*, Vec3> damped_;  // damping_constant's eased aim
    uint64_t lastDrawNs_ = 0;
    struct Flare {
        Vec3 pos;
        float size = 0.1f;   // fraction of the picture's width
        float rgba[4] = {1, 1, 1, 1};
        TextureId tex;
        bool test = false;
        int steps = 1;
        int slot = 0;        // its texel in the visibility texture
    };
    void drawFlares(const Camera& camera, int viewW, int viewH, bool hdr, TargetId worldTarget, TextureId worldDepth);
    void drawBeams(const Camera& camera, int viewW, int viewH, bool hdr, TargetId worldTarget, TextureId worldDepth);
    static constexpr float kBeamGain = 1.0f;  // FogPixel's 1024 x 0.2 on the 360's fixed-point buffer: effective gain [I]
    int slotOf(const void* key);
    struct RbBeam {
        Vec3 pos, dir;
        float length = 0, rTop = 0, rBottom = 0;
        float color[3] = {0, 0, 0};  // gFogColor: colour x intensity x 8 x brighten
        TextureId xsection;
    };
    void drawRbBeams(const Camera& camera, int viewW, int viewH, TextureId worldDepth);
    void probeBeams();
    const scene::Object* drawer_ = nullptr;
    const scene::Object* proxy_ = nullptr;
    DrawerParams drawerParams_;
    std::vector<RbBeam> rbList_;
    ProgramId rbBeamProgram_;
    TargetId rbBeamTarget_, densityTarget_;
    int rbW_ = 0, rbH_ = 0, densW_ = 0, densH_ = 0;
    BufferId rbVb_;
    size_t rbVbSize_ = 0;
    TextureId white_;
    Fog fog_;
    int rbBeamsDrawn_ = 0;
    bool densityDrawn_ = false;
    long long beamPixels_ = -1;

    World& world_;
    SceneCache& cache_;
    RenderDevice& device_;
    DynamicMesh mesh_;
    std::vector<Flare> flareList_;
    std::unordered_map<const void*, int> slots_;
    ProgramId visProgram_, flareProgram_, beamProgram_, beamCompProgram_;
    TargetId beamTarget_;
    int beamW_ = 0, beamH_ = 0;
    std::vector<float> beamPx_;  // this frame's beam sheets, 12 floats a vertex (beamLayout)
    size_t beamVbSize_ = 0;
    TargetId vis_[2];
    int visCurrent_ = 0;
    bool visCleared_ = false;
    BufferId triangle_, flareVb_, beamVb_;
    size_t flareVbSize_ = 0;
    int beams_ = 0, flares_ = 0, spots_ = 0;
};

}  // namespace gfx
