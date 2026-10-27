// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "render/camera.h"
#include "render/scene_gpu.h"
#include "scene/milo.h"

class World;
class BandView;
namespace chr {
class Character;
}

// Draws a WorldCrowd (game/crowd.h) the way GH2's WorldCrowd draw does
// (0x82247310) [C], per archetype (CharData) in list order:
//
// 1. Unless show_3d_only: the card. The archetype's one Character stands at
//    the origin turned like the crowd (rotate: x = normalize(z x camera
//    forward), y = z x x, z = the placement mesh's z; else the placement
//    mesh's world rotation) and is rendered into its card texture (256 x 512,
//    32 bits, one per archetype: gfx_mode 1, 0x822496A0; gfx_mode 0 would
//    share one 128 x 256 16-bit texture; the port keeps one per character,
//    which crowds naming the same archetype reuse, as each texture is drawn
//    right after it is rendered) through the card camera: the main
//    camera's rotation, at d = max(height / 2 + near, |camera - (mesh +
//    (0, 0, height / 2))|) back along its forward axis from (0, 0, height /
//    2), y fov 2 atan(height / 2 / d), the main camera's near / far.
//    The card's texture rectangle comes from the projected feet (origin),
//    spot_head, spot_L-hand, spot_R-hand (and spot_L / R-forearm when the
//    archetype has them): u from min(x, 0.25) - 0.05 to max(x, 0.75) +
//    0.05, v from min(y, 0.4) - 0.05 to max(y, 0.6) + 0.05, clamped to 0..1.
//    The card quad (a RndMultiMesh of one mesh, 0x822490F8) spans that
//    rectangle of a height / 2 x height plane in each placement's xz plane
//    (x = u height / 2 - height / 4, z = height / 2 - v height), centred
//    on the placement (the character's middle), with the placement's own
//    rotation; its Mat (0x82249368): unlit (use_environ off), opaque with
//    alpha_cut, clamped.
// 2. The shot's 3D placements: the same Character drawn at each, turned as
//    above, standing at z - height / 2.
//
// The texture is rendered every frame (all placements of an archetype show
// the same pose: GH2 keeps one Character per archetype). [I] GH2 renders it
// in the middle of the world's draw list; the port too, then binds the
// world's target and viewport again. [I] The crowd Mats have alpha_write off
// and how GH2's card texture gets its coverage alpha is not traced: the
// port writes alpha in the card pass (cleared to 0).
class CrowdView {
public:
    CrowdView(World& world, gfx::SceneCache& cache);
    ~CrowdView();
    CrowdView(const CrowdView&) = delete;
    CrowdView& operator=(const CrowdView&) = delete;

    // Draws crowd `c` with the Environ `env` (the draw list's); `selectEnv`
    // selects an Environ for the world's camera again (after a card pass).
    void draw(gfx::MaterialRenderer& mr, const scene::WorldCrowd& c, const scene::Environ* env, const gfx::Camera& camera, bool hdr,
              const std::function<void(const scene::Environ*)>& selectEnv);

    // The card of an archetype for a camera (0x82247310): the card camera,
    // the archetype's rotation and the texture rectangle (u0 v0 u1 v1, v
    // down). Leaves the character at the origin with that rotation.
    struct Card {
        gfx::Camera camera;
        scene::Xfm turn;  // rotation only
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    };
    static Card cardFor(chr::Character& c, const scene::Xfm& placementMesh, float height, bool rotate, const gfx::Camera& camera);
    // The card quad's four corners in the placement's frame (x, z) and
    // their texture coordinates (u, v, v down), in GH2's vertex order.
    static void cardQuad(const Card& card, float height, float xz[4][2], float uv[4][2]);

    int drawCalls() const { return drawCalls_; }
    int charactersDrawn() const { return characters_; }  // cards + 3D
    int cardsDrawn() const { return cards_; }
    int texturesRendered() const { return textures_; }
    void resetStats() { drawCalls_ = characters_ = cards_ = textures_ = 0; }

private:
    gfx::TargetId target(const void* key);
    void drawFlame(gfx::MaterialRenderer& mr, chr::Character& c, bool hdr);

    World& world_;
    gfx::SceneCache& cache_;
    std::unique_ptr<BandView> chars_;
    std::unique_ptr<gfx::DynamicMesh> dynamic_;
    std::map<const void*, gfx::TargetId> targets_;
    gfx::ScenePose flamePose_;
    int drawCalls_ = 0, characters_ = 0, cards_ = 0, textures_ = 0;
};
