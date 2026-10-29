// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/venue.h"
#include "render/device.h"
#include "render/venue_camera.h"
#include "render/venue_lights.h"

class Disc;

// Draws a venue (data/venue.h) in 3D: every visible sector of its scenes,
// textured, lit by its lights as the light show sets them, through a
// camera pose (render/venue_camera.h). GH3's material templates are
// approximated by one GL program (docs/venues.md "Shading"): diffuse x
// lightmap x (vertex colour + the zone's dynamic lights), with alpha test,
// alpha blend and additive variants from the material's flags.
class VenueView {
public:
    VenueView();
    ~VenueView();
    // Uploads the geometry and the zone's textures; meshes using a texture
    // of `hide` are left out (the crowd's cards). False and `error` on failure.
    bool init(gfx::RenderDevice& dev, const Disc& disc, const venue::Venue& v, const std::vector<uint32_t>& hide, std::string& error);
    void shutdown();
    // Draws into the bound target and viewport (depth tested; the caller's
    // frame cleared depth). `lights`: LightShow::evaluate's values.
    void draw(const venue::CamPose& cam, const std::vector<venue::LightState>& lights, const venue::LightShow& show);

    static gfx::ProgramDesc programDesc();

    struct Stats {
        size_t meshes = 0, triangles = 0, hiddenSectors = 0, hiddenMeshes = 0, missingTextures = 0, textures = 0;
        size_t drawsLastFrame = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    enum class Mode : uint8_t { Opaque, Blend, Add, Subtract };
    struct Item {
        uint32_t first = 0, count = 0;
        gfx::TextureId diffuse, lightmap;
        Mode mode = Mode::Opaque;
        float alphaRef = 0, drawOrder = 0;
        bool unlit = false;
        int group = 0;            // index into groups_
        int node = -1;            // the node making it visible (-1: always)
        venue::Vec3 center{};
        float radius = 0;
    };
    gfx::RenderDevice* dev_ = nullptr;
    const venue::Venue* v_ = nullptr;
    struct Tex {
        gfx::TextureId id;
        uint8_t usage = 0;  // the image header's class: 0 colour, 1 normal map, 3, 4 cube [I]
    };
    std::unordered_map<uint32_t, Tex> textures_;
    gfx::ProgramId program_;
    gfx::BufferId vb_, ib_;
    std::vector<Item> items_;
    std::vector<uint32_t> groups_;  // light group keys
    Stats stats_;
};
