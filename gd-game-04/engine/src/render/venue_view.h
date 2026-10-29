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
#include "render/gh3_shader.h"
#include "render/venue_camera.h"
#include "render/venue_lights.h"

class Disc;

// Draws a venue (data/venue.h) in 3D: every visible sector of its scenes,
// textured, lit by its lights as the light show sets them, through a
// camera pose (render/venue_camera.h). Each material draws with its GH3
// template's own shaders (the material library's Xenos microcode
// translated to GLSL, render/gh3_shader.h); a template that cannot be
// translated falls back to one approximate program (docs/venues.md
// "Shading"): diffuse x lightmap x (vertex colour + the zone's dynamic
// lights). Blend modes and alpha test come from the material's flags.
class VenueView {
public:
    VenueView();
    ~VenueView();
    // Uploads the geometry and the zone's textures; meshes using a texture
    // of `hide` (the crowd's cards) draw only while setOverride gives that
    // texture a replacement. False and `error` on failure.
    bool init(gfx::RenderDevice& dev, const Disc& disc, const venue::Venue& v, const std::vector<uint32_t>& hide, std::string& error);
    void shutdown();
    // Draws into the bound target and viewport (depth tested; the caller's
    // frame cleared depth). `lights`: LightShow::evaluate's values.
    void draw(const venue::CamPose& cam, const std::vector<venue::LightState>& lights, const venue::LightShow& show);

    static gfx::ProgramDesc programDesc();
    // The dynamic shadow map for this frame (the band from the shadow light,
    // depth 0..1): opaque meshes draw with their templates'
    // DynamicShadowReciever technique. An empty texture: none.
    void setShadow(gfx::TextureId depth, const gfx::Mat4& lightViewProj, int size, float intensity) {
        shadowTex_ = depth, shadowVP_ = lightViewProj, shadowSize_ = size, shadowIntensity_ = intensity;
    }
    // The light housings' volumes (lightvolumetype ConeImpostor: the
    // _Cone_Impostor template on a camera-facing quad along the beam), drawn
    // after the band (additive, depth tested).
    void drawVolumes(const venue::CamPose& cam, const venue::LightShow& show);
    // A viewport texture override (CreateViewportTextureOverride: the crowd's
    // renders on the cards); an empty id removes it.
    void setOverride(uint32_t texture, gfx::TextureId id);
    // Engine constants of a camera (view and projection matrices, time) as
    // GH3's shaders read them (also the band's, render/band_view.cpp).
    // `flipY`: upside down, for a render target sampled as a texture
    // (Direct3D's v runs down, OpenGL's target rows up).
    static void cameraGlobals(const venue::CamPose& cam, bool clipZeroToOne, double seconds, gh3shader::Globals& g,
                              gfx::Mat4* viewProj = nullptr, float aspect = 16.0f / 9.0f, bool flipY = false,
                              gfx::Mat4* viewOut = nullptr);

    struct Stats {
        size_t meshes = 0, triangles = 0, hiddenSectors = 0, hiddenMeshes = 0, missingTextures = 0, textures = 0;
        size_t drawsLastFrame = 0;
        size_t gh3Meshes = 0, fallbackMeshes = 0;  // drawn with GH3's shaders / the approximation
        size_t housings = 0;
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
        const gh3shader::Program* gh3 = nullptr;  // the template's program (null: the approximation)
        int mat = -1;             // index into mats_
        int group = 0;            // index into groups_
        int node = -1;            // the node making it visible (-1: always)
        float lodDist = 0;        // the node's lod_dist2 (0: none)
        uint32_t card = 0;        // a crowd card texture: drawn with its override only
        int housing = -1;         // a light housing's node: model space, drawn at the housing's live transform
        bool flare = false;       // the Flare template
        uint32_t shaderKey = 0;   // the material's template
        venue::Vec3 center{};
        float radius = 0;
    };
    gfx::RenderDevice* dev_ = nullptr;
    const venue::Venue* v_ = nullptr;
    struct Tex {
        gfx::TextureId id;
        uint8_t usage = 0;  // the image header's class: 0 colour, 1 normal map, 3, 4 cube [I]
        bool cube = false;
    };
    std::unordered_map<uint32_t, Tex> textures_;
    gfx::ProgramId program_;
    gfx::BufferId vb_, ib_;
    std::vector<Item> items_;
    std::vector<uint32_t> groups_;  // light group keys
    struct Mat {
        gh3shader::MaterialConstants consts;
        gfx::TextureId tex[16];
    };
    std::vector<Mat> mats_;
    std::unordered_map<uint32_t, gfx::TextureId> overrides_;
    std::vector<int> cones_;  // nodes of the ConeImpostor housings
    gfx::TextureId shadowTex_;
    gfx::Mat4 shadowVP_;
    int shadowSize_ = 1024;
    float shadowIntensity_ = 0.35f;
    gfx::BufferId coneVb_, coneIb_;
    gh3shader::Programs gh3_;
    double seconds_ = 0;
    Stats stats_;
public:
    // The song time in seconds for g_Time (set before draw).
    void setTime(double seconds) { seconds_ = seconds; }
};
