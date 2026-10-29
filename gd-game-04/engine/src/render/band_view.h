// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/band.h"
#include "render/band_anim.h"
#include "render/device.h"
#include "render/venue_camera.h"
#include "render/venue_lights.h"

// Draws band members (data/band.h) in 3D: each member's skins (body and
// instrument) skinned on the CPU with the animator's bone matrices
// (render/band_anim.h), placed at the member's waypoint, textured with the
// material's colour map and lit by the venue lights of the member's light
// group (docs/band.md "Drawing"). One GL program approximates GH3's
// character material templates, like the venue's.
class BandView {
public:
    BandView() = default;
    ~BandView();
    BandView(const BandView&) = delete;
    BandView& operator=(const BandView&) = delete;

    // Uploads one member's skins and textures; its index, or -1 and `error`.
    int add(gfx::RenderDevice& dev, const band::Model& m, std::string& error);
    void shutdown();

    struct Instance {
        int member = -1;                            // add()'s index
        const std::vector<bandanim::Mat34>* skin = nullptr;  // skinning matrices (model space)
        bandanim::Mat34 root;                       // model -> world
        std::vector<uint32_t> lightGroups;          // the venue light groups lighting it
    };
    // Draws into the bound target (depth tested): the instances through `cam`.
    void draw(const venue::CamPose& cam, const std::vector<Instance>& instances, const venue::Venue& v,
              const std::vector<venue::LightState>& lights, const venue::LightShow& show);

    static gfx::ProgramDesc programDesc();

    struct Stats {
        size_t meshes = 0, triangles = 0, vertices = 0, textures = 0, missingTextures = 0, drawsLastFrame = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    enum class Mode : uint8_t { Opaque, Blend, Add };
    struct Mesh {
        uint32_t firstIndex = 0, indexCount = 0;
        gfx::TextureId diffuse;
        Mode mode = Mode::Opaque;
        float alphaRef = 0;
    };
    struct SrcVertex {
        float pos[3], nrm[3], uv[2];
        uint8_t bones[3];
        float weights[3];
        uint8_t count = 0;
    };
    struct Member {
        std::vector<SrcVertex> src;
        std::vector<Mesh> meshes;
        gfx::BufferId vb, ib;
        std::vector<gfx::TextureId> textures;
    };
    gfx::RenderDevice* dev_ = nullptr;
    gfx::ProgramId program_;
    std::vector<Member> members_;
    std::vector<float> scratch_;
    Stats stats_;
};
