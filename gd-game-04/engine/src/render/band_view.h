// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/band.h"
#include "render/band_anim.h"
#include "render/device.h"
#include "render/gh3_shader.h"
#include "render/venue_camera.h"
#include "render/venue_lights.h"

// Draws band members (data/band.h) in 3D: each member's skins (body and
// instrument) skinned on the CPU with the animator's bone matrices
// (render/band_anim.h), placed at the member's waypoint, textured with the
// material's colour map and lit by the venue lights of the member's light
// group (docs/band.md "Drawing"). Each material draws with its GH3
// template's own shaders (render/gh3_shader.h: normal, specular, skin and
// hair shading) once setLibrary gave the material library; otherwise, and
// for templates that cannot be translated, one GL program approximates them.
class BandView {
public:
    BandView() = default;
    ~BandView();
    BandView(const BandView&) = delete;
    BandView& operator=(const BandView&) = delete;

    // GH3's material library (before add; null: the approximation only).
    void setLibrary(std::shared_ptr<const matlib::Library> lib) { lib_ = std::move(lib); }
    // Uploads one member's skins and textures; its index, or -1 and `error`.
    int add(gfx::RenderDevice& dev, const band::Model& m, std::string& error);
    // Frees one member's buffers and textures (the menus' objects that die);
    // its index stays, drawing nothing.
    void remove(int member);
    // The song time in seconds for the shaders' g_Time (set before draw).
    void setTime(double seconds) { seconds_ = seconds; }
    // The target's aspect (width / height) and whether alpha is written
    // (the crowd's imposters: render/crowd.h).
    // A shadow pass: GH3's shaders with these engine constants (the light's
    // view and projection) instead of the camera's; null: off.
    void setShadowPass(const gh3shader::Globals* g) { shadowPass_ = g; }
    // Reuse the vertices the last draw skinned (a second pass in the same frame).
    void setReuseSkinning(bool on) { reuseSkin_ = on; }
    // `flipY`: drawn upside down (a target sampled as a texture).
    void setView(float aspect, bool writeAlpha, bool flipY = false) { aspect_ = aspect, writeAlpha_ = writeAlpha, flipY_ = flipY; }
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
        size_t gh3Meshes = 0;  // drawn with GH3's shaders
    };
    const Stats& stats() const { return stats_; }

private:
    enum class Mode : uint8_t { Opaque, Blend, Add };
    struct Mesh {
        uint32_t firstIndex = 0, indexCount = 0;
        gfx::TextureId diffuse;
        Mode mode = Mode::Opaque;
        float alphaRef = 0;
        int8_t stencil = -1;  // the material's stencil reference (+0xdc; -1: none) [C 823DC180]
        const gh3shader::Program* gh3 = nullptr;
        int mat = -1;  // index into mats_
    };
    struct SrcVertex {
        float pos[3], nrm[3], tan[3], bin[3], uv[7][2];
        uint8_t col[4];
        uint8_t bones[3];
        float weights[3];
        uint8_t count = 0;
    };
    struct Mat {
        gh3shader::MaterialConstants consts;
        gfx::TextureId tex[16];
    };
    struct Member {
        std::vector<SrcVertex> src;
        std::vector<Mesh> meshes;
        gfx::BufferId vb, ib;
        std::vector<gfx::TextureId> textures;
    };
    gfx::RenderDevice* dev_ = nullptr;
    gfx::ProgramId program_;
    std::shared_ptr<const matlib::Library> lib_;
    gh3shader::Programs gh3_;
    std::vector<Mat> mats_;
    double seconds_ = 0;
    float aspect_ = 16.0f / 9.0f;
    bool writeAlpha_ = false, flipY_ = false, reuseSkin_ = false;
    const gh3shader::Globals* shadowPass_ = nullptr;
    std::vector<Member> members_;
    std::vector<float> scratch_;
    Stats stats_;
};
