// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <vector>

#include "render/camera.h"
#include "render/scene_gpu.h"
#include "scene/milo.h"

class World;

// Draws a WorldCrowd (game/crowd.h) as its archetypes' characters in their
// bind pose (the crowd clips are M9's), each shown placement standing at
// z - height / 2, turned to the camera when the crowd's `rotate` is on
// (0x82247310) [I: GH2 draws most placements as 2D cards rendered from the
// archetype each frame; the port draws every one in 3D].
//
// Cheap on purpose (Steam Deck / Android): each archetype's meshes are
// skinned once into their bind pose and merged per material, four copies in
// one buffer, each copy weighted fully to one of the standard shader's four
// object matrices (GdObject uModel[4]); one draw then puts four placements
// on screen, with no instancing extension. Placements outside the view are
// skipped.
class CrowdView {
public:
    CrowdView(World& world, gfx::SceneCache& cache);
    ~CrowdView();
    CrowdView(const CrowdView&) = delete;
    CrowdView& operator=(const CrowdView&) = delete;

    // Draws crowd `c` (its Environ already selected).
    void draw(gfx::MaterialRenderer& mr, const scene::WorldCrowd& c, const gfx::Camera& camera, bool hdr);

    int drawCalls() const { return drawCalls_; }
    int charactersDrawn() const { return characters_; }
    void resetStats() { drawCalls_ = characters_ = 0; }

private:
    struct Batch {
        const scene::Mat* mat = nullptr;
        gfx::GpuMesh mesh;      // four copies
        int indicesPerCopy = 0;
    };
    const std::vector<Batch>& batches(const scene::Milo* milo);

    World& world_;
    gfx::SceneCache& cache_;
    std::map<const scene::Milo*, std::vector<Batch>> batches_;
    std::vector<gfx::BufferId> buffers_;
    int drawCalls_ = 0, characters_ = 0;
};
