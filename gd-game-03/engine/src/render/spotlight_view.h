// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "render/camera.h"
#include "render/device.h"
#include "render/scene_gpu.h"

class World;

namespace gfx {

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

    int beamsDrawn() const { return beams_; }
    int flaresDrawn() const { return flares_; }
    int spotsDrawn() const { return spots_; }

    // The programs this draws with (--shadertest).
    static std::vector<ProgramDesc> allPrograms();

private:
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
    int slotOf(const void* key);

    World& world_;
    SceneCache& cache_;
    RenderDevice& device_;
    DynamicMesh mesh_;
    std::vector<Flare> flareList_;
    std::unordered_map<const void*, int> slots_;
    ProgramId visProgram_, flareProgram_;
    TargetId vis_[2];
    int visCurrent_ = 0;
    bool visCleared_ = false;
    BufferId triangle_, flareVb_;
    size_t flareVbSize_ = 0;
    int beams_ = 0, flares_ = 0, spots_ = 0;
};

}  // namespace gfx
