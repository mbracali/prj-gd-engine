// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "render/camera.h"
#include "render/scene_gpu.h"

class World;
namespace chr {
class Character;
}

// Draws the band's characters (game/band.h) where their venue slots sit in
// the draw list (render/venue_view.h): Character::DrawShowing 0x82153090 =
// the LOD picked from the character's screen size, then RndDir::DrawShowing
// of the character directory (its drawables not inside a Group, by
// draw_order; Groups draw their members; hidden objects skipped, so only
// the current LOD group shows), with the slot's Environ. Skinned meshes take
// their bones' world transforms from the character's skeleton (bone offset
// x bone world); what the world animates on the character's objects (the
// kit's TransAnims, MeshAnims, Mat anims) comes from the world's pose. The
// shadow group is not drawn in the colour pass (GH2 draws it into the shadow
// buffer, which the renderer does not have yet).
class BandView {
public:
    BandView(World& world, gfx::SceneCache& cache);
    explicit BandView(gfx::SceneCache& cache);  // without a venue (the character viewer)
    ~BandView();
    BandView(const BandView&) = delete;
    BandView& operator=(const BandView&) = delete;

    // The member standing in venue object `slot`, if any: drawn, true.
    // `selectEnv` selects an Environ (the slot's, else `fallback`).
    bool draw(gfx::MaterialRenderer& mr, const scene::Object* slot, const scene::Environ* fallback, const gfx::Camera& camera,
              bool hdr, const std::function<void(const scene::Environ*)>& selectEnv);
    // One character on its own (the --view character viewer).
    void drawCharacter(gfx::MaterialRenderer& mr, chr::Character& c, const gfx::Camera& camera, bool hdr);

    // The meshes Character::DrawShowing draws now (its LOD, showing flags,
    // no shadow group), in draw order; `pose`: the world's showing.
    static std::vector<const scene::Mesh*> meshesToDraw(const chr::Character& c, const gfx::ScenePose* pose);

    int meshesDrawn() const { return meshes_; }
    // Materials also write alpha (the crowd's card textures, render/crowd_view.h).
    bool forceAlphaWrite = false;
    // A ScenePose for the materials' animations (the crowd's lighter flame); null: the world's.
    const gfx::ScenePose* matPose = nullptr;
    void resetStats() { meshes_ = 0; }

private:
    void drawMesh(gfx::MaterialRenderer& mr, chr::Character& c, const scene::Mesh& m);

    World* world_ = nullptr;
    gfx::SceneCache& cache_;
    std::unique_ptr<gfx::DynamicMesh> dynamic_;
    bool hdr_ = false;
    int meshes_ = 0;
};

// The screen size GH2 compares with a LOD's screen_size (0x8214FCD0 /
// 0x8214FC60): the sphere's radius over its distance, scaled by the
// camera's projection [I: the camera fields].
float screenSize(const gfx::Camera& camera, const std::array<float, 4>& sphere);

// gd03 --view char/<c> (or a character milo): the character viewer. The
// character loads as in a venue (guitarists with guitar_sg, the drummer with
// the arena kit) in its bind pose; GD03_VIEW_CLIP=<clip> plays a clip of its
// main driver (GD03_VIEW_DRIVER=<driver> another), looping, on a 120 beats a
// minute clock (GD03_VIEW_FRAME=<frame> holds one frame); Left / Right turn
// the camera, Up / Down zoom; GD03_VIEW_YAW=<degrees>, GD03_VIEW_DIST,
// GD03_VIEW_HEIGHT place it; GD03_SCREENSHOT=<file.bmp> saves a frame.
int runCharView(const std::string& dataPath, const std::string& character);
// True when `path` names a character ("char/alterna1" or its milo).
bool isCharPath(const std::string& path);
