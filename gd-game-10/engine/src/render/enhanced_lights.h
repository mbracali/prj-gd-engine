// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <vector>

#include "render/device.h"
#include "render/rmath.h"

// [I] The Enhanced Options' venue extras (not in the original game), after
// gd01's VenueRenderer::drawEnhancedLights
// (gd-game-01/engine/src/render/venue_renderer.cpp), drawn after the
// venue's draw list:
// - All Band Mates Cast Shadows: a light of its own above each band member
//   (a little toward the audience) and a soft shadow on the stage floor,
//   stretched away from that light, with the light's pool around it. gd02
//   has no shadow buffer yet (GH2's own shadow pass is open, GD02-050), so
//   the shadow is a soft blob, not the silhouette gd01 projects;
// - More Lights on Venues (subtle / strong): a fill on the band (the
//   caller raises the band's ambient), the members' floor pools and a soft
//   beam from each light to its pool;
// - Dust in Light Beams: motes drifting and twinkling in those beams.
// Everything is one alpha-blended and one additive mesh a frame.
namespace gfx {

// gd10: the view the extras are drawn for, in their own space (gd02's:
// z up, ~44 units a metre); VenueStage maps GH3's world to it.
struct LightsView {
    Mat4 viewProj;  // the extras' space to clip space (this device's depth range)
    Vec3 pos, right, up;
};

class EnhancedLights {
public:
    explicit EnhancedLights(RenderDevice& device);
    ~EnhancedLights();
    EnhancedLights(const EnhancedLights&) = delete;
    EnhancedLights& operator=(const EnhancedLights&) = delete;

    struct Settings {
        bool shadowsAll = false;
        int moreLights = 0;  // 0 none, 1 1.5x, 2 2x, 3 2.5x
        int lightDust = 0;   // 0 none, 1 1.5x, 2 2x, 3 2.5x (with or without More Lights)
        int groundFog = 0;   // gd10: 0 none, 1 low, 2 full, 3 super
        bool any() const { return shadowsAll || moreLights > 0 || lightDust > 0 || groundFog > 0; }
    };
    // The band members' feet (world), for this frame.
    void draw(const Settings& s, const std::vector<Vec3>& members, const LightsView& camera, bool hdr, float timeSec);
    // gd10: the projectors' light pools on the stage floor (additive discs,
    // in the extras' space): centre, radius, colour (0..1), alpha.
    struct Pool {
        Vec3 centre;
        float radius;
        Vec3 colour;
        float alpha;
    };
    void drawPools(const std::vector<Pool>& pools, const LightsView& camera);
    // gd10: the Tesla Star Power's bolts (a jagged line from a to b, its
    // zigzag `amp` wide; a white core in a blue glow, re-struck ~30 times a
    // second) [I: GH3's TeslaEffectObject look].
    struct Bolt {
        Vec3 a, b;
        float amp;
        uint32_t seed;
    };
    void drawBolts(const std::vector<Bolt>& bolts, const LightsView& camera, float timeSec, float coreWidth, float glowWidth);
    // What the last frame drew (the walk tests' checks).
    int shadowsDrawn() const { return shadows_; }
    int beamsDrawn() const { return beams_; }
    int motesDrawn() const { return motes_; }
    // Every program this draws with (--shadertest).
    static ProgramDesc programDesc();

private:
    struct Vertex {
        float x, y, z;
        uint8_t r, g, b, a;
    };
    void flush(std::vector<Vertex>& v, std::vector<uint16_t>& idx, bool additive, const LightsView& camera, bool hdr);

    RenderDevice& device_;
    ProgramId program_;
    bool failed_ = false;
    BufferId vbo_[2], ibo_[2];
    size_t vboSize_[2] = {}, iboSize_[2] = {};
    int shadows_ = 0, beams_ = 0, motes_ = 0;
};

}  // namespace gfx
